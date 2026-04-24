import { useEffect, useRef } from 'react'
import { useDevice } from '../context/DeviceContext'
import type { EventFrame, TelemetryFrame } from '../lib/types'

export interface TestSample {
  ts: number      // device ts_ms
  ax: number | null
  vx: number | null
  stationary: boolean | null
}

export interface TestRecording {
  samples: TestSample[]
  events: EventFrame[]
}

/**
 * Captures `telemetry` + `event` WS frames between `start()` and `stop()`.
 * Lets the setup wizard fire a test sequence, observe the firmware's
 * view of what happened, and render per-test charts + pass/fail hints.
 *
 * Non-destructive: subscribes to the WS as a listener, doesn't change
 * any other state.
 */
export function useTestRecorder() {
  const { ws } = useDevice()
  const samplesRef   = useRef<TestSample[]>([])
  const eventsRef    = useRef<EventFrame[]>([])
  const recordingRef = useRef<boolean>(false)

  useEffect(() => {
    if (!ws) return
    const un1 = ws.on('telemetry', (frame: TelemetryFrame) => {
      if (!recordingRef.current) return
      const imu = (frame.sensors as Record<string, unknown> | undefined)?.imu as
        Record<string, unknown> | undefined
      samplesRef.current.push({
        ts: frame.ts,
        ax: typeof imu?.ax === 'number' ? (imu.ax as number) : null,
        vx: typeof imu?.vx === 'number' ? (imu.vx as number) : null,
        stationary: typeof imu?.stationary === 'boolean'
          ? (imu.stationary as boolean) : null,
      })
    })
    const un2 = ws.on('event', (ev: EventFrame) => {
      if (!recordingRef.current) return
      eventsRef.current.push(ev)
    })
    return () => { un1(); un2() }
  }, [ws])

  return {
    start() {
      samplesRef.current = []
      eventsRef.current  = []
      recordingRef.current = true
    },
    stop(): TestRecording {
      recordingRef.current = false
      return {
        samples: samplesRef.current.slice(),
        events:  eventsRef.current.slice(),
      }
    },
  }
}

export function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms))
}

/**
 * Like `sleep(ms)` but pumps a heartbeat every 300 ms so the firmware's
 * stale-timeout watchdog doesn't fire mid-test (and stop motors during
 * a brake phase). Safe to call with `hb` undefined — degrades to sleep.
 */
export async function sleepWithHb(
  ms: number,
  hb: (() => void) | undefined
): Promise<void> {
  const end = Date.now() + ms
  while (Date.now() < end) {
    hb?.()
    await sleep(Math.min(300, end - Date.now()))
  }
}
