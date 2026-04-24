import { useCallback } from 'react'
import { useDevice } from '../context/DeviceContext'
import type { Command } from '../lib/types'

/**
 * Prefers WebSocket (single-digit-ms latency); falls back to the HTTP
 * /api/control endpoint when the socket isn't open. Both paths land in
 * the same CommandHandler::execute() chokepoint on the device.
 */
export function useControl() {
  const { ws, api } = useDevice()

  return useCallback(
    (action: Command, speed?: number) => {
      if (ws && ws.isOpen) {
        ws.cmd(action, speed)
        return
      }
      api?.control(action, speed).catch(() => {})
    },
    [ws, api]
  )
}
