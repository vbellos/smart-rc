import { useEffect, useMemo, useRef, useState } from 'react'
import { PageHeader, ConnectionBadge } from '../components/Layout'
import { StatCard } from '../components/StatCard'
import { useEvents, useTelemetry } from '../hooks/useTelemetry'
import { SteerDirName, SteerStateName } from '../lib/types'
import { useDevice } from '../context/DeviceContext'
import { Battery, Compass, Gauge, Radio, Ruler } from '../components/Icons'

/**
 * Live telemetry + sensor dashboard. The drive/steer/safety/net rows come
 * straight off the WS telemetry stream. The sensor grid (gyro / accel /
 * distance / battery) pulls from the `sensors` object in telemetry or
 * /api/status — which the firmware's sensors/ extension point is designed
 * to fill in. Until then we show friendly placeholders so the UI's ready
 * the moment hardware is added.
 */
export default function MonitorPage() {
  const t = useTelemetry()
  const { status } = useDevice()
  const events = useEvents(40)

  const sensors: Record<string, unknown> =
    (t?.sensors as Record<string, unknown>) ??
    (status?.sensors as Record<string, unknown>) ?? {}

  const rssi = t?.net.rssi ?? status?.net.rssi ?? 0
  const driveMoving = t?.drive.moving ?? false
  const steerState  = SteerStateName[t?.steer.state ?? 0]
  const steerDir    = SteerDirName[t?.steer.lastDir ?? 0]

  return (
    <div>
      <PageHeader
        eyebrow="Telemetry"
        title="Monitor"
        subtitle="Live data stream from the vehicle."
        right={<ConnectionBadge />}
      />

      {/* Core RC telemetry */}
      <section className="mb-8">
        <h2 className="caption mb-3">Core</h2>
        <div className="grid gap-3 grid-cols-2 md:grid-cols-4">
          <StatCard
            eyebrow="Signal"
            value={rssi || '—'} unit="dBm"
            tone={rssi > -60 ? 'ok' : rssi > -75 ? 'warn' : 'err'}
            icon={<Radio className="size-4"/>}
          />
          <StatCard
            eyebrow="Drive"
            value={driveMoving ? 'MOVING' : 'IDLE'}
            tone={driveMoving ? 'ok' : 'neutral'}
            icon={<Gauge className="size-4"/>}
          />
          <StatCard
            eyebrow="Steering"
            value={steerState.toUpperCase()}
            sub={steerDir === 'none' ? 'centered' : `last: ${steerDir}`}
            tone={steerState === 'pulsing' ? 'ok' : 'neutral'}
            icon={<Compass className="size-4"/>}
          />
          <StatCard
            eyebrow="Uptime"
            value={formatUptime(status?.uptime_ms ?? 0)}
            sub={`Heap: ${formatKB(status?.heap_free ?? 0)} free`}
          />
        </div>
      </section>

      {/* Sensor grid — wired to the firmware's sensors/ extension */}
      <section className="mb-8">
        <div className="flex items-baseline justify-between mb-3">
          <h2 className="caption">Sensors</h2>
          <span className="text-[11px] text-ink-500">
            Populated by <code className="text-ink-300">sensors::appendStatusJson()</code>
          </span>
        </div>
        <div className="grid gap-3 grid-cols-2 md:grid-cols-3">
          <SensorIMU data={pick(sensors, 'imu')} />
          <SensorDistance data={pick(sensors, 'distance')} />
          <SensorBattery data={pick(sensors, 'battery')} />
        </div>
      </section>

      {/* Live RSSI timeline */}
      <section className="mb-8">
        <h2 className="caption mb-3">Signal over time</h2>
        <Sparkline value={rssi} />
      </section>

      {/* Event log */}
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
                <span className={`size-2 rounded-full ${eventColor(e.kind)}`}/>
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

/* ---- Sensor cards (placeholders until hardware is wired) -------------- */

function SensorIMU({ data }: { data: Record<string, unknown> | null }) {
  const gx = num(data, 'gx'), gy = num(data, 'gy'), gz = num(data, 'gz')
  const ax = num(data, 'ax'), ay = num(data, 'ay'), az = num(data, 'az')
  const has = data != null
  return (
    <div className={`card p-5 ${has ? 'ring-lime-500/25' : ''}`}>
      <div className="flex items-center justify-between">
        <span className="caption">IMU · Gyro + Accel</span>
        <Compass className="size-4 text-ink-400"/>
      </div>
      {has ? (
        <div className="mt-3 grid grid-cols-2 gap-2 text-sm">
          <MiniRow label="gx" val={gx} unit="°/s" />
          <MiniRow label="ax" val={ax} unit="m/s²" />
          <MiniRow label="gy" val={gy} unit="°/s" />
          <MiniRow label="ay" val={ay} unit="m/s²" />
          <MiniRow label="gz" val={gz} unit="°/s" />
          <MiniRow label="az" val={az} unit="m/s²" />
        </div>
      ) : (
        <Placeholder hint="MPU6050 / BNO055 / LSM6DS3" />
      )}
    </div>
  )
}

function SensorDistance({ data }: { data: Record<string, unknown> | null }) {
  const front = num(data, 'front')
  const has = data != null
  return (
    <div className={`card p-5 ${has ? 'ring-sky-500/25' : ''}`}>
      <div className="flex items-center justify-between">
        <span className="caption">Distance</span>
        <Ruler className="size-4 text-ink-400"/>
      </div>
      {has ? (
        <>
          <div className="mt-3 flex items-baseline gap-1.5">
            <span className="hero-num">{front != null ? front.toFixed(0) : '—'}</span>
            <span className="text-sm text-ink-400">cm</span>
          </div>
          <div className="mt-3 h-1.5 rounded-full bg-white/5 overflow-hidden">
            <div
              className="h-full bg-sky-400"
              style={{ width: `${Math.min(100, ((front ?? 0) / 300) * 100)}%` }}
            />
          </div>
          <div className="mt-2 text-xs text-ink-400">Front (HC-SR04 / VL53L0X)</div>
        </>
      ) : (
        <Placeholder hint="HC-SR04 / VL53L0X / TF-Luna" />
      )}
    </div>
  )
}

function SensorBattery({ data }: { data: Record<string, unknown> | null }) {
  const v = num(data, 'voltage')
  const pct = num(data, 'percent')
  const has = data != null
  const tone: 'ok' | 'warn' | 'err' | 'neutral' =
    pct == null ? 'neutral' : pct > 40 ? 'ok' : pct > 15 ? 'warn' : 'err'
  return (
    <div className={`card p-5 ${has ? (tone === 'err' ? 'ring-rose-500/30' : 'ring-lime-500/25') : ''}`}>
      <div className="flex items-center justify-between">
        <span className="caption">Battery</span>
        <Battery className="size-4 text-ink-400"/>
      </div>
      {has ? (
        <>
          <div className="mt-3 flex items-baseline gap-1.5">
            <span className="hero-num">{v != null ? v.toFixed(2) : '—'}</span>
            <span className="text-sm text-ink-400">V</span>
            {pct != null && <span className="ml-auto text-sm text-ink-300">{pct.toFixed(0)}%</span>}
          </div>
          <div className="mt-3 h-1.5 rounded-full bg-white/5 overflow-hidden">
            <div
              className={`h-full ${tone === 'err' ? 'bg-rose-500' : tone === 'warn' ? 'bg-amber-400' : 'bg-lime-400'}`}
              style={{ width: `${Math.max(0, Math.min(100, pct ?? 0))}%` }}
            />
          </div>
        </>
      ) : (
        <Placeholder hint="Voltage divider on VM / INA219" />
      )}
    </div>
  )
}

function MiniRow({ label, val, unit }: { label: string; val: number | null; unit: string }) {
  return (
    <div className="flex items-center justify-between tabular-nums">
      <span className="text-ink-400">{label}</span>
      <span className="text-white">
        {val != null ? val.toFixed(2) : '—'}<span className="text-ink-500 ml-1">{unit}</span>
      </span>
    </div>
  )
}

function Placeholder({ hint }: { hint: string }) {
  return (
    <div className="mt-3 flex flex-col items-start gap-2">
      <span className="pill pill-muted">Not connected</span>
      <span className="text-xs text-ink-400">Add one of: {hint}</span>
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

/* ---- tiny inline sparkline (RSSI over time) -------------------------- */

function Sparkline({ value }: { value: number }) {
  const [buf, setBuf] = useState<number[]>([])
  const last = useRef(value)
  useEffect(() => { last.current = value }, [value])

  useEffect(() => {
    const id = setInterval(() => {
      setBuf((b) => [...b.slice(-119), last.current])
    }, 500)
    return () => clearInterval(id)
  }, [])

  const { path, min, max } = useMemo(() => {
    if (buf.length < 2) return { path: '', min: 0, max: 0 }
    const mn = Math.min(...buf)
    const mx = Math.max(...buf)
    const span = mx - mn || 1
    const w = 100, h = 30
    const step = w / (buf.length - 1)
    const pts = buf.map((v, i) => {
      const x = i * step
      const y = h - ((v - mn) / span) * h
      return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
    })
    return { path: pts.join(' '), min: mn, max: mx }
  }, [buf])

  return (
    <div className="card p-5 dotted-grid">
      <div className="flex items-baseline justify-between mb-2">
        <span className="text-xs text-ink-400">RSSI · last 60 s</span>
        <span className="text-xs tabular-nums text-ink-500">
          min {min || '—'} · max {max || '—'} dBm
        </span>
      </div>
      <svg viewBox="0 0 100 30" preserveAspectRatio="none" className="w-full h-24">
        <path d={path} fill="none" stroke="currentColor"
              className="text-lime-400" strokeWidth="0.6"
              strokeLinecap="round" strokeLinejoin="round"/>
      </svg>
    </div>
  )
}
