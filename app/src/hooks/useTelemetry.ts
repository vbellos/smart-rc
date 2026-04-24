import { useEffect, useState } from 'react'
import { useDevice } from '../context/DeviceContext'
import type { EventFrame, TelemetryFrame } from '../lib/types'

/** Latest telemetry frame, or null before the first arrives. */
export function useTelemetry(): TelemetryFrame | null {
  const { ws, wsConnected } = useDevice()
  const [frame, setFrame] = useState<TelemetryFrame | null>(null)

  useEffect(() => {
    if (!ws) return
    const un = ws.on('telemetry', setFrame)
    return un
  }, [ws, wsConnected])

  return frame
}

/** Ring buffer of recent event frames (estop/stale/net-mode). */
export function useEvents(max = 25): EventFrame[] {
  const { ws } = useDevice()
  const [events, setEvents] = useState<EventFrame[]>([])

  useEffect(() => {
    if (!ws) return
    const un = ws.on('event', (ev) => {
      setEvents((prev) => [ev, ...prev].slice(0, max))
    })
    return un
  }, [ws, max])

  return events
}

/**
 * Rolling count of commands-per-second sent from this client. Useful
 * as a live "is my input actually reaching the device?" indicator.
 */
export function useCmdRate(): { tick: () => void; rate: number } {
  const [rate, setRate] = useState(0)
  const [count, setCount] = useState(0)

  useEffect(() => {
    const id = setInterval(() => {
      setRate(count)
      setCount(0)
    }, 1000)
    return () => clearInterval(id)
  }, [count])

  return {
    rate,
    tick: () => setCount((c) => c + 1),
  }
}
