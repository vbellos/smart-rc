import { useMemo } from 'react'
import { PageHeader, ConnectionBadge } from '../components/Layout'
import { StatCard } from '../components/StatCard'
import { ChartCard, type ChartSeries } from '../components/Chart'
import { useEvents, useTelemetry } from '../hooks/useTelemetry'
import { useMultiSeries, useTimeSeries } from '../hooks/useSeries'
import { SteerDirName, steerStateLabel } from '../lib/types'
import { useDevice } from '../context/DeviceContext'
import { Battery, Compass, Gauge, Radio, Ruler } from '../components/Icons'

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
          <span className="text-[11px] text-ink-500">
            from <code className="text-ink-300">sensors::appendStatusJson()</code>
          </span>
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
