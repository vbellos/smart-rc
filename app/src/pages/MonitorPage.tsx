import { useMemo, useState } from 'react'
import { PageHeader, ConnectionBadge } from '../components/Layout'
import { StatCard } from '../components/StatCard'
import { ChartCard, type ChartSeries } from '../components/Chart'
import { useEvents, useTelemetry } from '../hooks/useTelemetry'
import { useMultiSeries, useTimeSeries } from '../hooks/useSeries'
import { SteerDirName, steerStateLabel, type AutoBrakeState } from '../lib/types'
import { useDevice } from '../context/DeviceContext'
import { AlertOctagon, Battery, Compass, Gauge, Radio, Ruler } from '../components/Icons'

/**
 * Live telemetry + sensor dashboard.
 *
 * - Core stat cards pull from the WS telemetry stream.
 * - Chart cards are time-series buffered client-side via useTimeSeries /
 *   useMultiSeries at a steady 500 ms cadence (so the lines stay smooth
 *   even when the underlying source is bursty or idle).
 * - Sensor grid pulls from `telemetry.sensors` / `status.sensors`, which
 *   the firmware's `sensors/` extension point is designed to fill in.
 *   Placeholders show until hardware is wired.
 */
export default function MonitorPage() {
  const t = useTelemetry()
  const { status } = useDevice()
  const events = useEvents(60)

  const sensors: Record<string, unknown> =
    (t?.sensors as Record<string, unknown>) ??
    (status?.sensors as Record<string, unknown>) ?? {}

  const rssi = t?.net.rssi ?? status?.net.rssi ?? 0
  const heapFree = status?.heap_free ?? 0
  const driveMoving = t?.drive.moving ?? false
  const steerState = steerStateLabel(t?.steer.state)
  const steerDir = SteerDirName[t?.steer.lastDir ?? 0]
  const autoBrake = t?.auto_brake ?? status?.auto_brake

  // ---------- Series buffers ---------------------------------------------

  const rssiSeries    = useTimeSeries(rssi === 0 ? null : rssi, 120, 500)
  const heapSeries    = useTimeSeries(heapFree / 1024, 120, 500)
  const imuData       = pick(sensors, 'imu')
  const batteryData   = pick(sensors, 'battery')
  const distanceData  = pick(sensors, 'distance')

  const imuSeries = useMultiSeries(
    () => ({
      gx: num(imuData, 'gx'),
      gy: num(imuData, 'gy'),
      gz: num(imuData, 'gz'),
    }),
    120, 500
  )
  const accelSeries = useMultiSeries(
    () => ({
      ax: num(imuData, 'ax'),
      ay: num(imuData, 'ay'),
      az: num(imuData, 'az'),
    }),
    120, 500
  )
  const batterySeries  = useTimeSeries(num(batteryData, 'voltage'), 240, 1000)
  const distanceSeries = useTimeSeries(num(distanceData, 'front'),  120, 500)

  // Velocity diagnostics — the signal the brake uses to decide direction.
  // Watch this while pushing the car forward/backward by hand to verify
  // the IMU orientation is correctly set (see imuInvertX in Config).
  const vxNow        = num(imuData, 'vx')
  const axNow        = num(imuData, 'ax')
  const stationary   = imuData != null && (imuData['stationary'] === true)
  const vxSeries     = useTimeSeries(vxNow, 120, 200)
  const axSeries     = useTimeSeries(axNow, 120, 200)

  return (
    <div>
      <PageHeader
        eyebrow="Telemetry"
        title="Monitor"
        subtitle="Live data stream and timeline from the vehicle."
        right={<ConnectionBadge />}
      />

      {/* ------- Core stats -------- */}
      <section className="mb-8">
        <h2 className="caption mb-3">Core</h2>
        <div className="grid gap-3 grid-cols-2 md:grid-cols-4">
          <StatCard
            eyebrow="Signal"
            value={rssi || '—'} unit="dBm"
            tone={rssi > -60 ? 'ok' : rssi > -75 ? 'warn' : 'err'}
            icon={<Radio className="size-4" />}
          />
          <StatCard
            eyebrow="Drive"
            value={driveMoving ? 'MOVING' : 'IDLE'}
            tone={driveMoving ? 'ok' : 'neutral'}
            icon={<Gauge className="size-4" />}
          />
          <StatCard
            eyebrow="Steering"
            value={steerState.toUpperCase()}
            sub={steerDir === 'none' ? 'centered' : `last: ${steerDir}`}
            tone={steerState === 'pulsing' ? 'ok' : 'neutral'}
            icon={<Compass className="size-4" />}
          />
          <StatCard
            eyebrow="Uptime"
            value={formatUptime(status?.uptime_ms ?? 0)}
            sub={`Heap: ${formatKB(heapFree)} free`}
          />
        </div>
      </section>

      {/* ------- Auto-brake -----------------------------------------------
         Front-distance obstacle gate. Visible only when the firmware is
         publishing auto_brake state (i.e., the feature has been wired on
         the device). Three states: OFF (disabled), ARMED (enabled but
         clear), ENGAGED (currently forcing the brake).
         ----------------------------------------------------------------- */}
      {autoBrake && (
        <section className="mb-8">
          <h2 className="caption mb-3">Safety</h2>
          <div className="grid gap-3 grid-cols-1 md:grid-cols-2">
            <AutoBrakeCard ab={autoBrake} />
          </div>
        </section>
      )}

      {/* ------- Motion diagnostics ---------------------------------------
         Big signed display of the firmware's estimated forward velocity
         (vx) + raw forward acceleration (ax). This is the signal the
         active brake uses to pick direction. If pushing the car forward
         makes the number go NEGATIVE, your IMU X axis is mirrored —
         tick "Invert IMU X" in Config to correct it.
         ----------------------------------------------------------------- */}
      {imuData && (
        <section className="mb-8">
          <div className="flex items-baseline justify-between mb-3">
            <h2 className="caption">Motion (brake reference)</h2>
            <span className="text-[11px] text-ink-500">
              push the car forward → vx should go{' '}
              <span className="text-lime-400">positive</span>
            </span>
          </div>
          <div className="grid gap-3 md:grid-cols-3">
            <VelocityCard vx={vxNow ?? 0} stationary={stationary} />
            <ChartCard
              title="vx · integrated velocity"
              series={[{ label: 'm/s', color: vxNow != null && vxNow < 0 ? 'rgb(244,63,94)' : 'rgb(163,230,53)', data: vxSeries }]}
              unit="m/s"
              windowLabel="last 24 s"
              fill
            />
            <ChartCard
              title="ax · forward acceleration"
              series={[{ label: 'm/s²', color: 'rgb(56,189,248)', data: axSeries }]}
              unit="m/s²"
              windowLabel="last 24 s"
              fill
            />
          </div>
        </section>
      )}

      {/* ------- Chart grid -------- */}
      <section className="mb-8">
        <div className="flex items-baseline justify-between mb-3">
          <h2 className="caption">Charts</h2>
          <span className="text-[11px] text-ink-500">client-side · 500 ms samples · 60 s window</span>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          <ChartCard
            title="Signal · RSSI"
            series={[{ label: 'dBm', color: 'rgb(163,230,53)', data: rssiSeries }]}
            unit="dBm"
            min={-95} max={-30}
            icon={<Radio className="size-4" />}
            fill
          />
          <ChartCard
            title="Free heap"
            series={[{ label: 'KB', color: 'rgb(56,189,248)', data: heapSeries }]}
            unit="KB"
            icon={<Gauge className="size-4" />}
            fill
          />
        </div>
      </section>

      {/* ------- Sensors (firmware extension point) -------- */}
      <section className="mb-8">
        <div className="flex items-baseline justify-between mb-3">
          <h2 className="caption">Sensors</h2>
          <div className="flex items-center gap-2">
            <CalibrateImuButton hasImu={imuData != null} />
            <span className="text-[11px] text-ink-500">
              from <code className="text-ink-300">sensors::appendSensorsJson()</code>
            </span>
          </div>
        </div>
        <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-3">
          <IMUCard title="Gyro" axes={imuSeries} unit="°/s" icon={<Compass className="size-4"/>} />
          <IMUCard title="Accelerometer" axes={accelSeries} unit="m/s²" />
          <BatteryCard series={batterySeries} data={batteryData} />
          <DistanceCard series={distanceSeries} data={distanceData} />
        </div>
      </section>

      {/* ------- Event log -------- */}
      <section>
        <h2 className="caption mb-3">Events</h2>
        <div className="card p-0 overflow-hidden">
          {events.length === 0 && (
            <div className="p-6 text-center text-sm text-ink-400">
              No events yet. Latched e-stops, stale-timeouts, and net-mode
              changes will appear here.
            </div>
          )}
          <ul className="divide-y divide-[#1c1c22]">
            {events.map((e, i) => (
              <li key={i} className="px-4 py-3 flex items-center gap-3 text-sm">
                <span className={`size-2 rounded-full ${eventColor(e.kind)}`} />
                <span className="font-medium text-white">{e.kind}</span>
                {e.ts && <span className="ml-auto text-xs text-ink-500 tabular-nums">{e.ts}ms</span>}
              </li>
            ))}
          </ul>
        </div>
      </section>
    </div>
  )
}

/* --------------------------------------------------------------------- */
/* Sensor chart cards                                                     */
/* --------------------------------------------------------------------- */

function IMUCard({
  title, axes, unit, icon,
}: {
  title: string
  axes: Record<string, number[]>
  unit: string
  icon?: React.ReactNode
}) {
  const hasData = Object.values(axes).some((a) => a.length > 0)
  const prefixes = useMemo(() => {
    // Expected keys are either gx/gy/gz (gyro) or ax/ay/az (accel).
    const keys = Object.keys(axes)
    if (keys.length === 0) return []
    const colors: Record<string, string> = {
      gx: 'rgb(163,230,53)', gy: 'rgb(56,189,248)', gz: 'rgb(167,139,250)',
      ax: 'rgb(163,230,53)', ay: 'rgb(56,189,248)', az: 'rgb(167,139,250)',
    }
    return keys.map((k): ChartSeries => ({
      label: k,
      color: colors[k] ?? 'rgb(200,200,200)',
      data: axes[k] ?? [],
    }))
  }, [axes])

  if (!hasData) {
    return <PlaceholderCard title={title} icon={icon} hint="MPU6050 / BNO055 / LSM6DS3" />
  }
  return (
    <ChartCard
      title={title}
      series={prefixes}
      unit={unit}
      icon={icon}
      fill={false}
    />
  )
}

function BatteryCard({ series, data }: {
  series: number[]
  data: Record<string, unknown> | null
}) {
  const v   = num(data, 'voltage')
  const pct = num(data, 'percent')
  if (!data) {
    return <PlaceholderCard title="Battery" icon={<Battery className="size-4"/>} hint="Voltage divider on VM / INA219" />
  }
  const tone: 'ok' | 'warn' | 'err' =
    pct == null ? 'ok' : pct > 40 ? 'ok' : pct > 15 ? 'warn' : 'err'
  const colors = { ok: 'rgb(163,230,53)', warn: 'rgb(251,191,36)', err: 'rgb(244,63,94)' }

  return (
    <ChartCard
      title="Battery"
      series={[{ label: 'V', color: colors[tone], data: series }]}
      unit="V"
      icon={<Battery className="size-4"/>}
      subtitle={pct != null ? `${pct.toFixed(0)}% remaining` : undefined}
      format={() => (v != null ? v.toFixed(2) : '—')}
      fill
    />
  )
}

function DistanceCard({ series, data }: {
  series: number[]
  data: Record<string, unknown> | null
}) {
  if (!data) {
    return <PlaceholderCard title="Distance · Front" icon={<Ruler className="size-4"/>} hint="HC-SR04 / VL53L0X / TF-Luna" />
  }
  return (
    <ChartCard
      title="Distance · Front"
      series={[{ label: 'cm', color: 'rgb(56,189,248)', data: series }]}
      unit="cm"
      icon={<Ruler className="size-4"/>}
      min={0} max={300}
      fill
    />
  )
}

function AutoBrakeCard({ ab }: { ab: AutoBrakeState }) {
  const state: 'off' | 'armed' | 'engaged' =
    !ab.enabled ? 'off' : ab.engaged ? 'engaged' : 'armed'
  const ring   = state === 'engaged' ? 'ring-rose-500/40'
               : state === 'armed'   ? 'ring-lime-500/30'
               : 'ring-[#26262e]'
  const tone   = state === 'engaged' ? 'text-rose-400'
               : state === 'armed'   ? 'text-lime-400'
               : 'text-ink-300'
  const label  = state === 'engaged' ? 'ENGAGED'
               : state === 'armed'   ? 'ARMED'
               : 'OFF'
  const dist = ab.distance_cm
  const trig = ab.trigger_cm

  // Visual gauge — distance vs trigger. 0% = at trigger (about to engage),
  // 100% = far (>= 2× trigger). Helps the user tune the slope while
  // driving slowly toward an obstacle.
  const ratio = dist == null ? 1 : Math.max(0, Math.min(1, (dist - trig) / Math.max(1, trig)))
  const barColor = state === 'engaged' ? 'bg-rose-500'
                 : state === 'armed'   ? 'bg-lime-400'
                 : 'bg-ink-600'

  return (
    <div className={`card p-5 ring-1 ${ring}`}>
      <div className="flex items-center justify-between">
        <span className="caption">Auto-brake</span>
        <AlertOctagon className="size-4 text-ink-400"/>
      </div>
      <div className="mt-3 flex items-baseline gap-3">
        <span className={`text-3xl font-semibold tabular-nums ${tone}`}>{label}</span>
        {state !== 'off' && (
          <span className="text-sm text-ink-300 tabular-nums">
            {dist == null ? '— ' : `${dist} `}
            <span className="text-ink-500">/ {trig} cm</span>
          </span>
        )}
      </div>
      {state !== 'off' && (
        <div className="mt-4">
          <div className="h-1.5 rounded-full bg-[#1c1c22] overflow-hidden">
            <div className={`h-full ${barColor} transition-all`}
                 style={{ width: `${(1 - ratio) * 100}%` }}/>
          </div>
          <div className="mt-1.5 flex justify-between text-[10px] text-ink-500">
            <span>at trigger</span><span>clear</span>
          </div>
        </div>
      )}
      {state === 'off' && (
        <p className="text-xs text-ink-400 mt-2">
          Enable in Configuration → Motors &amp; Safety.
        </p>
      )}
    </div>
  )
}

function VelocityCard({ vx, stationary }: { vx: number; stationary: boolean }) {
  // Direction-first display. Color tells you at a glance which way the
  // firmware thinks you're moving — compare with what you actually see.
  const isStill = stationary || Math.abs(vx) < 0.05
  const dir: 'fwd' | 'rev' | 'stop' = isStill ? 'stop' : vx > 0 ? 'fwd' : 'rev'
  const color = dir === 'fwd' ? 'text-lime-400'
              : dir === 'rev' ? 'text-rose-400'
              : 'text-ink-300'
  const ring  = dir === 'fwd' ? 'ring-lime-500/25'
              : dir === 'rev' ? 'ring-rose-500/25'
              : 'ring-[#26262e]'
  const label = dir === 'fwd' ? '▲ FORWARD'
              : dir === 'rev' ? '▼ REVERSE'
              : '■ STOPPED'
  return (
    <div className={`card p-5 ring-1 ${ring}`}>
      <div className="flex items-center justify-between">
        <span className="caption">Velocity estimate</span>
        <Compass className="size-4 text-ink-400"/>
      </div>
      <div className="mt-3 flex items-baseline gap-1.5">
        <span className={`hero-num ${color}`}>
          {vx >= 0 ? '+' : ''}{vx.toFixed(2)}
        </span>
        <span className="text-sm text-ink-400 font-medium">m/s</span>
      </div>
      <div className={`mt-3 pill ${dir === 'fwd' ? 'pill-ok' : dir === 'rev' ? 'pill-err' : 'pill-muted'}`}>
        {label}
      </div>
      <div className="mt-2 text-xs text-ink-400">
        Integrated from accelerometer X. Resets to 0 after ~1.5 s of quiescence.
      </div>
    </div>
  )
}

function CalibrateImuButton({ hasImu }: { hasImu: boolean }) {
  const { api } = useDevice()
  const [busy, setBusy] = useState(false)
  const [toast, setToast] = useState<string | null>(null)
  if (!hasImu) return null

  async function run() {
    if (!api) return
    setBusy(true); setToast('Hold still \u2014 sampling bias...')
    try {
      const r = await api.calibrateImu()
      setToast(r.ok ? '\u2713 Gyro bias re-sampled' : 'Failed')
    } catch (e) {
      setToast(`Failed: ${e}`)
    } finally {
      setBusy(false)
      setTimeout(() => setToast(null), 4000)
    }
  }
  return (
    <>
      <button onClick={run} disabled={busy}
        className="pill pill-muted hover:text-white disabled:opacity-50">
        {busy ? 'Calibrating\u2026' : 'Recalibrate gyro'}
      </button>
      {toast && <span className="text-[11px] text-ink-300">{toast}</span>}
    </>
  )
}

function PlaceholderCard({ title, hint, icon }: {
  title: string
  hint: string
  icon?: React.ReactNode
}) {
  return (
    <div className="card p-5 min-h-[200px] flex flex-col">
      <div className="flex items-center justify-between mb-1">
        <span className="caption">{title}</span>
        {icon && <span className="text-ink-400">{icon}</span>}
      </div>
      <div className="flex-1 grid place-items-center">
        <div className="text-center">
          <span className="pill pill-muted">Not connected</span>
          <div className="text-xs text-ink-400 mt-3">{hint}</div>
        </div>
      </div>
    </div>
  )
}

/* ---- utilities -------------------------------------------------------- */

function pick(o: Record<string, unknown>, k: string): Record<string, unknown> | null {
  const v = o?.[k]
  if (v && typeof v === 'object') return v as Record<string, unknown>
  return null
}

function num(o: Record<string, unknown> | null, k: string): number | null {
  if (!o) return null
  const v = o[k]
  return typeof v === 'number' ? v : null
}

function formatUptime(ms: number): string {
  const s = Math.floor(ms / 1000)
  if (s < 60) return `${s}s`
  const m = Math.floor(s / 60)
  if (m < 60) return `${m}m ${s % 60}s`
  const h = Math.floor(m / 60)
  return `${h}h ${m % 60}m`
}
function formatKB(bytes: number): string {
  return `${(bytes / 1024).toFixed(1)} KB`
}

function eventColor(kind: string): string {
  if (kind.startsWith('estop_latched')) return 'bg-rose-500'
  if (kind.startsWith('estop_cleared')) return 'bg-lime-400'
  if (kind.startsWith('stale'))         return 'bg-amber-400'
  return 'bg-sky-400'
}
