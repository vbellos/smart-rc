// Wire types mirroring the firmware's REST + WebSocket payloads.
// Keep these in sync with src/portal/Portal.cpp and src/transport/WsServer.cpp.

export type NetMode = 'down' | 'sta' | 'ap' | string

export interface DeviceStatus {
  uptime_ms: number
  heap_free: number
  net: {
    mode: NetMode
    ip: string
    ssid: string
    rssi: number
    hostname: string
    connected: boolean
  }
  motors: {
    drive_moving: boolean
    steer_state: number
    steer_last_dir: number
  }
  safety: {
    emergency: boolean
    stale: boolean
  }
  /** Auto-brake (front-distance obstacle gate). Present when configured. */
  auto_brake?: AutoBrakeState
  /** Future sensors.appendStatusJson() output lives here. */
  sensors?: Record<string, unknown>
}

export interface AutoBrakeState {
  enabled: boolean
  engaged: boolean
  trigger_cm: number
  /** Last front reading in cm; null when sensor invalid / absent. */
  distance_cm: number | null
}

export interface DeviceConfig {
  wifiSsid: string
  wifiPassword: string
  apSsid: string
  apPassword: string
  hostname: string
  useDhcp: boolean
  staticIp: string
  staticGateway: string
  staticSubnet: string
  staticDns: string
  steeringPulseMs: number
  steeringCooldownMs: number
  defaultDrivePwm: number
  defaultSteerPwm: number
  heartbeatTimeoutMs: number
  driveInverted: boolean
  steerInverted: boolean
  apShutdownAfterProvision: boolean
  controlToken: string
  logLevel: number
  imuInvertX: boolean
  imuInvertY: boolean
  imuInvertZ: boolean
  activeBrakePwm: number
  activeBrakeMaxMs: number
  autoBrakeEnabled: boolean
  autoBrakeBaseCm: number
  autoBrakeSlopeCmPerMs: number
  autoBrakeMinSpeedCmPs: number
}

export interface WifiNetwork {
  ssid: string
  rssi: number
  secure: boolean
  channel: number
}

// ---------------------------------------------------------------------------
// WebSocket frames
// ---------------------------------------------------------------------------

export interface HelloFrame {
  t: 'hello'
  proto: number
  device: string
  features: string[]
  authRequired: boolean
}

export interface TelemetryFrame {
  t: 'telemetry'
  ts: number
  drive: { moving: boolean }
  steer: { state: number; lastDir: number }
  safety: { emergency: boolean; stale: boolean }
  net: { mode: number; rssi: number }
  /** Auto-brake (front-distance obstacle gate). Present when configured. */
  auto_brake?: AutoBrakeState
  /** Sensor plug-in surface (gyro/accel/distance/battery/...). */
  sensors?: Record<string, unknown>
}

export interface EventFrame {
  t: 'event'
  kind: string
  ts?: number
}

export interface AckFrame {
  t: 'ack'
  id: number
  ok: boolean
  reason: string
}

export interface PongFrame { t: 'pong'; id: number }
export interface ErrFrame  { t: 'err';  code: string; detail?: string }

export type InboundFrame =
  | HelloFrame
  | TelemetryFrame
  | EventFrame
  | AckFrame
  | PongFrame
  | ErrFrame

export type Command =
  | 'forward' | 'reverse' | 'stop' | 'brake'
  | 'left'    | 'right'   | 'steer_stop'
  | 'estop'   | 'clear_estop'

// ---------------------------------------------------------------------------
// Client-side persisted state
// ---------------------------------------------------------------------------

export interface SavedDevice {
  host: string        // IP or hostname (`smartrc.local`)
  label?: string      // user-friendly name, defaults to hostname
  lastSeen?: number   // epoch ms
}

// Steering state enum values (must match Steering::State in firmware)
export const SteerStateName = ['idle', 'pulsing', 'cooldown'] as const
export const SteerDirName   = ['none', 'left',    'right'] as const

/**
 * Display-only label: merges `cooldown` (state 2) into `idle` because
 * the motor is physically off in both cases. The raw `cooldown` state
 * is still exposed on the wire for anyone needing the invariant.
 */
export function steerStateLabel(state: number | undefined): 'idle' | 'pulsing' {
  return state === 1 ? 'pulsing' : 'idle'
}
