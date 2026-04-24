import { PageHeader, ConnectionBadge } from '../components/Layout'
import { ControlPad } from '../components/ControlPad'
import { useEvents, useTelemetry } from '../hooks/useTelemetry'
import { SteerDirName, steerStateLabel } from '../lib/types'
import { StatCard } from '../components/StatCard'
import { Radio } from '../components/Icons'
import { useDevice } from '../context/DeviceContext'

export default function DrivePage() {
  const t = useTelemetry()
  const events = useEvents(100)
  const staleDrops = events.filter((e) => e.kind === 'stale_timeout').length
  const { status } = useDevice()
  const rssi = t?.net.rssi ?? status?.net.rssi ?? 0
  const driveMoving = t?.drive.moving ?? false
  const steerState  = steerStateLabel(t?.steer.state)
  const steerDir    = SteerDirName[t?.steer.lastDir ?? 0]
  const stale       = t?.safety.stale ?? false
  const estop       = t?.safety.emergency ?? false

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
              value={driveMoving ? 'ON' : 'IDLE'}
              tone={driveMoving ? 'ok' : 'neutral'}
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
        </aside>
      </div>
    </div>
  )
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
