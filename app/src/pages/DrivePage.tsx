import { useState } from 'react'
import { PageHeader, ConnectionBadge } from '../components/Layout'
import { ControlPad } from '../components/ControlPad'
import { useEvents, useTelemetry } from '../hooks/useTelemetry'
import { SteerDirName, steerStateLabel, type AutoBrakeState } from '../lib/types'
import { StatCard } from '../components/StatCard'
import { AlertOctagon, Radio } from '../components/Icons'
import { useDevice } from '../context/DeviceContext'

export default function DrivePage() {
  const t = useTelemetry()
  const events = useEvents(100)
  const staleDrops = events.filter((e) => e.kind === 'stale_timeout').length
  const { status, api } = useDevice()
  const rssi = t?.net.rssi ?? status?.net.rssi ?? 0
  const driveMoving   = t?.drive.moving ?? false
  const activeBraking = t?.drive.active_braking
                     ?? status?.motors.drive_active_braking
                     ?? false
  const steerState  = steerStateLabel(t?.steer.state)
  const steerDir    = SteerDirName[t?.steer.lastDir ?? 0]
  const stale       = t?.safety.stale ?? false
  const estop       = t?.safety.emergency ?? false
  const autoBrake   = t?.auto_brake ?? status?.auto_brake

  return (
    <div>
      <PageHeader
        eyebrow="Controller"
        title="Drive"
        subtitle="Press and hold the pad to move. Release to stop."
        right={<ConnectionBadge />}
      />

      <div className="grid gap-4 lg:grid-cols-[minmax(0,1fr)_340px]">
        <ControlPad />

        <aside className="grid gap-4 content-start">
          <StatCard
            eyebrow="Signal"
            value={rssi}
            unit="dBm"
            tone={rssi > -60 ? 'ok' : rssi > -75 ? 'warn' : 'err'}
            progress={rssiToPct(rssi)}
            icon={<Radio className="size-4" />}
            sub={rssiLabel(rssi)}
          />
          <div className="grid grid-cols-2 gap-3">
            <StatCard
              eyebrow="Drive"
              value={activeBraking ? 'BRAKING' : driveMoving ? 'ON' : 'IDLE'}
              tone={activeBraking ? 'warn' : driveMoving ? 'ok' : 'neutral'}
              sub={activeBraking ? 'commands locked' : undefined}
            />
            <StatCard
              eyebrow="Steering"
              value={steerState.toUpperCase()}
              sub={steerDir === 'none' ? 'centered' : `last: ${steerDir}`}
              tone={steerState === 'pulsing' ? 'ok' : 'neutral'}
            />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <StatCard
              eyebrow="E-Stop"
              value={estop ? 'LATCHED' : 'OK'}
              tone={estop ? 'err' : 'ok'}
            />
            <StatCard
              eyebrow="Heartbeat"
              value={stale ? 'STALE' : 'FRESH'}
              tone={stale ? 'warn' : 'ok'}
              sub={
                staleDrops > 0
                  ? `${staleDrops} drop${staleDrops === 1 ? '' : 's'} this session`
                  : stale ? 'Safety has stopped motors' : 'Commands recent'
              }
            />
          </div>

          {autoBrake && (
            <AutoBrakeToggle
              ab={autoBrake}
              onToggle={async (next) => {
                if (!api) return
                await api.postConfig({ autoBrakeEnabled: next })
              }}
            />
          )}
        </aside>
      </div>
    </div>
  )
}

/**
 * Live toggle + status pill for auto-brake. Reads engaged state from
 * telemetry every push (typically 20 Hz), so the user sees the brake fire
 * in real time. Click toggles enabled via /api/config — the device
 * applies it without reboot.
 */
function AutoBrakeToggle({ ab, onToggle }: {
  ab: AutoBrakeState
  onToggle: (next: boolean) => Promise<void>
}) {
  // Optimistic local state — flips on click, then telemetry confirms.
  // Without this, the toggle feels laggy at low telemetry rates.
  const [pending, setPending] = useState<boolean | null>(null)
  const enabled = pending ?? ab.enabled
  const state: 'off' | 'armed' | 'engaged' =
    !enabled ? 'off' : ab.engaged ? 'engaged' : 'armed'

  const ring  = state === 'engaged' ? 'ring-rose-500/40 bg-rose-500/5'
              : state === 'armed'   ? 'ring-lime-500/30 bg-lime-500/5'
              : 'ring-[#26262e]'
  const tone  = state === 'engaged' ? 'text-rose-400'
              : state === 'armed'   ? 'text-lime-400'
              : 'text-ink-300'
  const label = state === 'engaged' ? 'ENGAGED'
              : state === 'armed'   ? 'ARMED'
              : 'OFF'

  // Defensive: older firmware (or a partial frame mid-flash) may not yet
  // emit nested front/rear objects. Treat missing as "no signal".
  const front = ab.front
  const rear  = ab.rear
  const engagedSide =
    front?.active && rear?.active ? 'both'
    : front?.active ? 'front'
    : rear?.active  ? 'rear'
    : null

  const sub = state === 'engaged'
    ? `Blocked ${engagedSide ?? ''}`.trim()
    : state === 'armed'
      ? `front ${formatDist(front?.distance_cm ?? null)} · rear ${formatDist(rear?.distance_cm ?? null)}`
      : 'Tap to enable'

  async function flip() {
    const next = !enabled
    setPending(next)
    try {
      await onToggle(next)
    } finally {
      // Clear local override once the next telemetry frame arrives;
      // a 600 ms watchdog covers a slow round-trip.
      setTimeout(() => setPending(null), 600)
    }
  }

  return (
    <button
      onClick={flip}
      className={`text-left p-4 rounded-2xl ring-1 transition-all
                  hover:ring-white/30 active:scale-[0.99] ${ring}`}>
      <div className="flex items-center justify-between">
        <span className="caption">Auto-Brake</span>
        <AlertOctagon className={`size-4 ${tone}`}/>
      </div>
      <div className="mt-2 flex items-baseline gap-2">
        <span className={`text-2xl font-semibold tabular-nums ${tone}`}>
          {label}
        </span>
      </div>
      <div className="mt-1 text-xs text-ink-400 truncate">{sub}</div>
    </button>
  )
}

function formatDist(cm: number | null): string {
  return cm == null ? '—' : `${cm}cm`
}

function rssiToPct(rssi: number): number {
  // -30 dBm = great, -90 dBm = unusable. Map linearly, clamp.
  const pct = (rssi + 90) / 60
  return Math.max(0, Math.min(1, pct))
}
function rssiLabel(rssi: number): string {
  if (rssi === 0) return '—'
  if (rssi > -55) return 'Excellent'
  if (rssi > -65) return 'Good'
  if (rssi > -75) return 'Fair'
  return 'Weak'
}
