import { useState } from 'react'
import { PageHeader, ConnectionBadge } from '../components/Layout'
import { LineChart } from '../components/Chart'
import { useControl } from '../hooks/useControl'
import { useDevice } from '../context/DeviceContext'
import { sleep, sleepWithHb, useTestRecorder, type TestRecording } from '../hooks/useTestRecorder'
import { Check, X, AlertOctagon } from '../components/Icons'
import type { Command } from '../lib/types'

/**
 * Setup wizard. Walks through a short sequence of motion tests and
 * reports what the firmware saw for each one.
 *
 * Design rules:
 * - Every automated test is SHORT (< 800 ms of motor-on time) and uses
 *   reduced PWM (120/255) so the vehicle never goes far.
 * - Each test is self-contained: record → analyze → show result.
 * - A top-level "Run all" runs the full sequence back-to-back; the user
 *   can also fire individual tests.
 */

type Status = 'idle' | 'running' | 'pass' | 'warn' | 'fail'

interface TestResult {
  status: Status
  title: string
  summary: string
  hint?: string
  recording?: TestRecording
  peakVx?: number
  peakAx?: number
}

interface TestDef {
  id: string
  title: string
  blurb: string
  run: (ctx: RunCtx) => Promise<TestResult>
}

interface RunCtx {
  send: (action: Command, speed?: number) => void
  hb: () => void
  record: ReturnType<typeof useTestRecorder>
  calibrate: () => Promise<unknown>
}

// Reduced throttle so the car travels ~30-50 cm per test even at full pulse.
const TEST_PWM = 120
const PULSE_MS = 600
const SETTLE_MS = 1500

// ---------------------------------------------------------------------------
// Test definitions
// ---------------------------------------------------------------------------

const TESTS: TestDef[] = [
  {
    id: 'calibrate',
    title: 'IMU bias calibration',
    blurb: 'Car must be still. Resamples gyro + accel zero point.',
    async run({ calibrate }) {
      await calibrate()
      return {
        title: 'IMU bias calibration',
        status: 'pass',
        summary: 'Bias captured. Next tests reference this zero point.',
      }
    },
  },
  {
    id: 'forward',
    title: 'Forward pulse',
    blurb: 'Short forward pulse. vx should go positive.',
    async run({ send, record, hb }) {
      record.start()
      send('forward', TEST_PWM)
      await sleep(PULSE_MS)
      send('stop')
      await sleepWithHb(SETTLE_MS, hb)
      const rec = record.stop()
      return analyzeDirection(rec, 'forward')
    },
  },
  {
    id: 'reverse',
    title: 'Reverse pulse',
    blurb: 'Short reverse pulse. vx should go negative.',
    async run({ send, record, hb }) {
      record.start()
      send('reverse', TEST_PWM)
      await sleep(PULSE_MS)
      send('stop')
      await sleepWithHb(SETTLE_MS, hb)
      const rec = record.stop()
      return analyzeDirection(rec, 'reverse')
    },
  },
  {
    id: 'brake-stationary',
    title: 'Brake while stopped',
    blurb: 'Car already still — brake should be a no-op.',
    async run({ send, record, hb }) {
      record.start()
      await sleep(300)
      send('brake')
      await sleepWithHb(SETTLE_MS, hb)
      const rec = record.stop()
      return analyzeBrakeStationary(rec)
    },
  },
  {
    id: 'brake-forward',
    title: 'Brake from forward',
    blurb: 'Forward pulse, then immediate brake. Car should stop quickly.',
    async run({ send, record, hb }) {
      record.start()
      send('forward', TEST_PWM)
      await sleep(PULSE_MS)
      send('brake')
      await sleepWithHb(SETTLE_MS, hb)
      const rec = record.stop()
      return analyzeBrake(rec, 'forward')
    },
  },
  {
    id: 'brake-reverse',
    title: 'Brake from reverse',
    blurb: 'Reverse pulse, then immediate brake.',
    async run({ send, record, hb }) {
      record.start()
      send('reverse', TEST_PWM)
      await sleep(PULSE_MS)
      send('brake')
      await sleepWithHb(SETTLE_MS, hb)
      const rec = record.stop()
      return analyzeBrake(rec, 'reverse')
    },
  },
]

// ---------------------------------------------------------------------------
// Analyzers
// ---------------------------------------------------------------------------

function peakSigned(samples: TestRecording['samples'], key: 'vx' | 'ax'): number {
  let peak = 0
  for (const s of samples) {
    const v = s[key]
    if (v == null) continue
    if (Math.abs(v) > Math.abs(peak)) peak = v
  }
  return peak
}

function analyzeDirection(rec: TestRecording, expected: 'forward' | 'reverse'): TestResult {
  const peakVx = peakSigned(rec.samples, 'vx')
  const expectedSign = expected === 'forward' ? 1 : -1
  const gotSign      = peakVx >= 0 ? 1 : -1
  const magOK        = Math.abs(peakVx) > 0.15

  if (!magOK) {
    return {
      title: expected === 'forward' ? 'Forward pulse' : 'Reverse pulse',
      status: 'warn',
      summary: `Peak vx = ${peakVx.toFixed(2)} m/s — too small to judge`,
      hint: 'Increase default drive PWM, or the motor/drivetrain is stuck. Try driving manually to confirm the car actually moves.',
      recording: rec,
      peakVx,
    }
  }
  if (gotSign === expectedSign) {
    return {
      title: expected === 'forward' ? 'Forward pulse' : 'Reverse pulse',
      status: 'pass',
      summary: `Peak vx = ${peakVx.toFixed(2)} m/s (correct sign)`,
      recording: rec,
      peakVx,
    }
  }
  return {
    title: expected === 'forward' ? 'Forward pulse' : 'Reverse pulse',
    status: 'fail',
    summary: `Peak vx = ${peakVx.toFixed(2)} m/s — wrong sign for "${expected}"`,
    hint: expected === 'forward'
      ? 'When driving forward, vx should be POSITIVE. It\'s negative → toggle "Invert IMU X" in Config.'
      : 'When driving reverse, vx should be NEGATIVE. It went positive → toggle "Invert drive direction" in Config (motor is wired reversed) OR toggle IMU X invert if forward also failed.',
    recording: rec,
    peakVx,
  }
}

function analyzeBrakeStationary(rec: TestRecording): TestResult {
  const engaged = rec.events.some((e) => e.kind === 'brake_engaged')
  const noop    = rec.events.some((e) => e.kind.startsWith('brake_noop'))
  if (!engaged && noop) {
    return {
      title: 'Brake while stopped',
      status: 'pass',
      summary: 'No-op as expected (car was stationary).',
      recording: rec,
    }
  }
  if (engaged) {
    return {
      title: 'Brake while stopped',
      status: 'warn',
      summary: 'Brake engaged even though car was at rest.',
      hint: 'The firmware thinks the car is moving when it isn\'t. Recalibrate the IMU (Monitor → Recalibrate gyro) and retry. If persistent, accel bias may be off.',
      recording: rec,
    }
  }
  return {
    title: 'Brake while stopped',
    status: 'warn',
    summary: 'No brake decision event received — check WS connection.',
    recording: rec,
  }
}

function analyzeBrake(rec: TestRecording, expected: 'forward' | 'reverse'): TestResult {
  const engaged = rec.events.find((e) => e.kind === 'brake_engaged')
  const settled = rec.events.find((e) => e.kind === 'brake_settled')
  const aborted = rec.events.find((e) => e.kind === 'brake_aborted_wrong_dir')
  const timeout = rec.events.find((e) => e.kind === 'brake_timeout')

  const peakVx = peakSigned(rec.samples, 'vx')
  const peakAx = peakSigned(rec.samples, 'ax')

  if (aborted) {
    return {
      title: `Brake from ${expected}`,
      status: 'fail',
      summary: `Brake aborted — motor was driving the car further, not stopping it.`,
      hint: `This is a sign/axis-mismatch. Check the previous "${expected} pulse" test: if its sign was wrong, fix that first. Otherwise try toggling "Invert drive direction".`,
      recording: rec, peakVx, peakAx,
    }
  }
  if (timeout) {
    return {
      title: `Brake from ${expected}`,
      status: 'warn',
      summary: 'Brake timed out — did not detect "stopped" within max window.',
      hint: 'Active brake PWM may be too low, or IMU quiet threshold too tight. Try increasing activeBrakePwm in Config.',
      recording: rec, peakVx, peakAx,
    }
  }
  if (engaged && settled) {
    const elapsed = (settled.ts ?? 0) - (engaged.ts ?? 0)
    return {
      title: `Brake from ${expected}`,
      status: 'pass',
      summary: `Engaged at v=${(engaged as any).v?.toFixed?.(2)}, settled in ~${elapsed} ms.`,
      recording: rec, peakVx, peakAx,
    }
  }
  if (!engaged) {
    return {
      title: `Brake from ${expected}`,
      status: 'warn',
      summary: 'Brake was not engaged — probably registered as no-op.',
      hint: `If the car was already at rest by the time brake fired, this is fine; otherwise vx magnitude is under the 0.15 m/s threshold.`,
      recording: rec, peakVx, peakAx,
    }
  }
  return {
    title: `Brake from ${expected}`,
    status: 'warn',
    summary: 'Incomplete event trace',
    recording: rec, peakVx, peakAx,
  }
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

export default function SetupPage() {
  const { api, ws } = useDevice()
  const send = useControl()
  const record = useTestRecorder()
  const [results, setResults] = useState<Record<string, TestResult>>({})
  const [running, setRunning] = useState<string | null>(null)
  const [applyingFix, setApplyingFix] = useState(false)

  const ctx: RunCtx = {
    send,
    hb: () => ws?.hb(),
    record,
    calibrate: () => api?.calibrateImu() ?? Promise.resolve({}),
  }

  // Detect the "IMU X axis is flipped" pattern: forward pulse gave
  // negative peak vx AND/OR reverse pulse gave positive peak vx.
  const fwd = results['forward']
  const rev = results['reverse']
  const imuFlipDetected =
    (fwd?.status === 'fail' && fwd.peakVx != null && fwd.peakVx < -0.15) ||
    (rev?.status === 'fail' && rev.peakVx != null && rev.peakVx > 0.15)

  async function applyImuInvertFix() {
    if (!api || applyingFix) return
    setApplyingFix(true)
    try {
      const cfg = await api.getConfig()
      await api.postConfig({ imuInvertX: !cfg.imuInvertX })
      // Re-calibrate so integrator starts clean under the new sign.
      await api.calibrateImu()
      setResults({})   // force user to re-run for confirmation
    } catch (e) {
      console.warn('apply fix failed', e)
    } finally {
      setApplyingFix(false)
    }
  }

  async function runOne(t: TestDef) {
    if (running) return
    setRunning(t.id)
    setResults((r) => ({ ...r, [t.id]: { status: 'running', title: t.title, summary: 'Running...' } }))
    try {
      const r = await t.run(ctx)
      setResults((rs) => ({ ...rs, [t.id]: r }))
    } catch (e) {
      setResults((rs) => ({
        ...rs,
        [t.id]: { status: 'fail', title: t.title, summary: String(e) },
      }))
    } finally {
      send('stop')
      setRunning(null)
    }
  }

  async function runAll() {
    for (const t of TESTS) {
      await runOne(t)
      // Longer inter-test gap lets the car's actual motion die + gives
      // the IMU's ZUPT window (1 s) enough time to zero vx cleanly
      // before the next test reads it.
      await sleepWithHb(1200, () => ws?.hb())
    }
  }

  return (
    <div>
      <PageHeader
        eyebrow="Diagnostics"
        title="Setup wizard"
        subtitle="Scripted motion tests. Short pulses, reduced throttle."
        right={<ConnectionBadge />}
      />

      <div className="card p-5 mb-6 ring-1 ring-amber-400/20 bg-amber-400/5">
        <div className="flex items-start gap-3">
          <AlertOctagon className="size-5 text-amber-400 shrink-0 mt-0.5"/>
          <div className="text-sm text-ink-200">
            <div className="font-medium text-amber-300 mb-1">Before starting</div>
            <ul className="list-disc pl-5 space-y-0.5 text-ink-300">
              <li>Place the car on a flat surface with at least <b>1 m</b> of clearance front &amp; back.</li>
              <li>Tests use reduced throttle ({TEST_PWM}/255) and short pulses ({PULSE_MS} ms on, {SETTLE_MS} ms settle).</li>
              <li>You can hit E-Stop on the Drive page at any time to cut power.</li>
            </ul>
          </div>
        </div>
      </div>

      {imuFlipDetected && (
        <div className="card p-4 mb-4 ring-1 ring-lime-500/30 bg-lime-500/5
                        flex items-center gap-3">
          <Check className="size-5 text-lime-400 shrink-0"/>
          <div className="flex-1 min-w-0">
            <div className="text-sm text-white font-medium">
              Recommended fix detected
            </div>
            <div className="text-xs text-ink-300 mt-0.5">
              Your IMU X axis is flipped. One click fixes both forward &amp;
              reverse, then re-run tests to confirm.
            </div>
          </div>
          <button onClick={applyImuInvertFix}
            disabled={applyingFix}
            className="shrink-0 px-4 h-10 rounded-xl bg-lime-500 text-ink-950
                       font-semibold hover:bg-lime-400 active:scale-[0.98]
                       disabled:opacity-50">
            {applyingFix ? 'Applying…' : 'Toggle Invert IMU X'}
          </button>
        </div>
      )}

      <div className="mb-6 flex items-center gap-3">
        <button
          onClick={runAll}
          disabled={!!running}
          className="px-5 h-11 rounded-xl bg-lime-500 text-ink-950 font-semibold
                     hover:bg-lime-400 active:scale-[0.98] disabled:opacity-50">
          {running ? 'Running…' : 'Run all tests'}
        </button>
        <button
          onClick={() => setResults({})}
          disabled={!!running}
          className="px-4 h-11 rounded-xl bg-[#161619] ring-1 ring-[#26262e]
                     text-ink-300 hover:text-white">
          Reset
        </button>
        <span className="text-xs text-ink-500">or run each one individually below</span>
      </div>

      <div className="grid gap-4">
        {TESTS.map((t) => (
          <TestRow
            key={t.id}
            def={t}
            result={results[t.id]}
            busy={running === t.id}
            onRun={() => runOne(t)}
          />
        ))}
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Test row
// ---------------------------------------------------------------------------

function TestRow({
  def, result, busy, onRun,
}: {
  def: TestDef
  result?: TestResult
  busy: boolean
  onRun: () => void
}) {
  const status = result?.status ?? 'idle'
  const recording = result?.recording

  const vxSeries = recording
    ? recording.samples.map((s) => s.vx).filter((v): v is number => v != null)
    : []
  const axSeries = recording
    ? recording.samples.map((s) => s.ax).filter((v): v is number => v != null)
    : []

  return (
    <div className={`card p-5 ring-1 ${ringForStatus(status)}`}>
      <div className="flex items-start gap-4">
        <StatusBadge status={status} />
        <div className="flex-1 min-w-0">
          <div className="flex items-start justify-between gap-3">
            <div>
              <h3 className="text-white font-medium">{def.title}</h3>
              <p className="text-xs text-ink-400 mt-0.5">{def.blurb}</p>
            </div>
            <button
              onClick={onRun}
              disabled={busy}
              className="shrink-0 px-3 h-8 rounded-lg text-xs font-medium
                         bg-[#0f0f11] ring-1 ring-[#26262e] text-ink-200
                         hover:text-white hover:ring-white/20
                         disabled:opacity-50">
              {busy ? 'Running…' : 'Run'}
            </button>
          </div>
          {result && (
            <>
              <p className={`text-sm mt-3 ${textForStatus(status)}`}>
                {result.summary}
              </p>
              {result.hint && (
                <p className="text-xs text-ink-300 mt-2 px-3 py-2 rounded bg-[#0f0f11] ring-1 ring-[#26262e]">
                  💡 {result.hint}
                </p>
              )}
              {recording && (recording.samples.length > 1 || recording.events.length > 0) && (
                <div className="mt-3 grid gap-3 sm:grid-cols-2">
                  {vxSeries.length > 1 && (
                    <MiniChart
                      label="vx · m/s"
                      data={vxSeries}
                      color={result.peakVx != null && result.peakVx < 0
                        ? 'rgb(244,63,94)' : 'rgb(163,230,53)'}
                      peak={result.peakVx}
                    />
                  )}
                  {axSeries.length > 1 && (
                    <MiniChart
                      label="ax · m/s²"
                      data={axSeries}
                      color="rgb(56,189,248)"
                      peak={result.peakAx}
                    />
                  )}
                  {recording.events.length > 0 && (
                    <div className="sm:col-span-2 text-xs text-ink-400">
                      <span className="text-ink-500">Events: </span>
                      {recording.events.map((e, i) => (
                        <span key={i} className="inline-block mr-3">
                          <span className="text-ink-200">{e.kind}</span>
                          {(e as any).v != null && (
                            <span className="text-ink-500"> v={(e as any).v.toFixed?.(2)}</span>
                          )}
                        </span>
                      ))}
                    </div>
                  )}
                </div>
              )}
            </>
          )}
        </div>
      </div>
    </div>
  )
}

function MiniChart({
  label, data, color, peak,
}: { label: string; data: number[]; color: string; peak?: number }) {
  return (
    <div className="rounded-xl bg-[#0f0f11] ring-1 ring-[#26262e] p-3">
      <div className="flex items-baseline justify-between mb-1">
        <span className="text-[10px] uppercase tracking-wider text-ink-400">{label}</span>
        {peak != null && (
          <span className="text-[11px] tabular-nums text-ink-300">
            peak {peak >= 0 ? '+' : ''}{peak.toFixed(2)}
          </span>
        )}
      </div>
      <LineChart
        series={[{ label, color, data }]}
        height={60}
        fill
      />
    </div>
  )
}

function StatusBadge({ status }: { status: Status }) {
  if (status === 'pass') return (
    <div className="size-8 rounded-lg grid place-items-center bg-lime-500/15 ring-1 ring-lime-500/25 text-lime-400">
      <Check className="size-4"/>
    </div>
  )
  if (status === 'fail') return (
    <div className="size-8 rounded-lg grid place-items-center bg-rose-500/15 ring-1 ring-rose-500/25 text-rose-400">
      <X className="size-4"/>
    </div>
  )
  if (status === 'warn') return (
    <div className="size-8 rounded-lg grid place-items-center bg-amber-400/15 ring-1 ring-amber-400/25 text-amber-400">
      <AlertOctagon className="size-4"/>
    </div>
  )
  if (status === 'running') return (
    <div className="size-8 rounded-lg grid place-items-center bg-sky-500/15 ring-1 ring-sky-500/25 text-sky-400">
      <span className="size-2 rounded-full bg-sky-400 animate-ping"/>
    </div>
  )
  return (
    <div className="size-8 rounded-lg grid place-items-center bg-[#0f0f11] ring-1 ring-[#26262e] text-ink-500">
      <span className="size-2 rounded-full bg-ink-500"/>
    </div>
  )
}

function ringForStatus(s: Status): string {
  return s === 'pass'    ? 'ring-lime-500/20'
       : s === 'fail'    ? 'ring-rose-500/25'
       : s === 'warn'    ? 'ring-amber-400/25'
       : s === 'running' ? 'ring-sky-500/25'
       : 'ring-[#26262e]'
}
function textForStatus(s: Status): string {
  return s === 'pass' ? 'text-lime-300'
       : s === 'fail' ? 'text-rose-300'
       : s === 'warn' ? 'text-amber-300'
       : 'text-ink-300'
}
