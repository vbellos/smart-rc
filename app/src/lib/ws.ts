import type { Command, InboundFrame } from './types'

type ListenerMap = {
  [K in InboundFrame['t']]?: Array<(frame: Extract<InboundFrame, { t: K }>) => void>
}

/**
 * WebSocket client for the firmware's `/ws` endpoint. Auto-reconnects with
 * exponential backoff, re-subscribes telemetry after reconnect, and
 * exposes a typed event bus for the React tree.
 */
export class WsClient {
  private ws: WebSocket | null = null
  private cmdId = 0
  private reconnectMs = 500
  private destroyed = false
  private telemetryHz: number | null = 10
  private listeners: ListenerMap = {}
  private statusListeners: Array<(connected: boolean) => void> = []
  private lastOpenTs = 0

  constructor(public host: string) {}

  /* ---------- lifecycle ---------- */

  connect() {
    if (this.destroyed) return
    const url = `ws://${this.host}/ws`
    try {
      this.ws = new WebSocket(url)
    } catch {
      this.scheduleReconnect()
      return
    }
    this.ws.addEventListener('open', this.onOpen)
    this.ws.addEventListener('message', this.onMessage)
    this.ws.addEventListener('close', this.onClose)
    this.ws.addEventListener('error', () => { /* close follows */ })
  }

  destroy() {
    this.destroyed = true
    this.ws?.close()
    this.ws = null
  }

  private onOpen = () => {
    this.reconnectMs = 500
    this.lastOpenTs = Date.now()
    this.notifyStatus(true)
    this.rawSend({ t: 'hello' })
    if (this.telemetryHz != null) {
      this.rawSend({ t: 'sub', streams: ['telemetry'], hz: this.telemetryHz })
    }
  }

  private onMessage = (e: MessageEvent) => {
    let msg: InboundFrame
    try {
      msg = JSON.parse(e.data)
    } catch {
      return
    }
    const list = this.listeners[msg.t]
    if (!list) return
    for (const fn of list) (fn as (f: InboundFrame) => void)(msg)
  }

  private onClose = () => {
    this.notifyStatus(false)
    if (!this.destroyed) this.scheduleReconnect()
  }

  private scheduleReconnect() {
    setTimeout(() => this.connect(), this.reconnectMs)
    this.reconnectMs = Math.min(this.reconnectMs * 2, 8000)
  }

  /* ---------- outbound ---------- */

  private rawSend(obj: unknown): boolean {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(obj))
      return true
    }
    return false
  }

  /** Returns true if the frame was actually sent. */
  cmd(action: Command, speed?: number): boolean {
    this.cmdId++
    const payload: Record<string, unknown> = { t: 'cmd', action, id: this.cmdId }
    if (speed !== undefined && speed !== 0) payload.speed = speed
    return this.rawSend(payload)
  }

  hb(): boolean { return this.rawSend({ t: 'hb' }) }

  setTelemetryHz(hz: number | null) {
    this.telemetryHz = hz
    if (hz == null) this.rawSend({ t: 'unsub', streams: ['telemetry'] })
    else this.rawSend({ t: 'sub', streams: ['telemetry'], hz })
  }

  /* ---------- subscriptions ---------- */

  on<K extends InboundFrame['t']>(
    type: K,
    fn: (frame: Extract<InboundFrame, { t: K }>) => void
  ): () => void {
    ;(this.listeners[type] ||= []).push(fn as never)
    return () => {
      this.listeners[type] = (this.listeners[type] || []).filter(
        (l) => l !== fn
      ) as never
    }
  }

  onStatus(fn: (connected: boolean) => void) {
    this.statusListeners.push(fn)
    return () => {
      this.statusListeners = this.statusListeners.filter((l) => l !== fn)
    }
  }

  private notifyStatus(connected: boolean) {
    for (const fn of this.statusListeners) fn(connected)
  }

  /* ---------- introspection ---------- */

  get isOpen(): boolean {
    return this.ws?.readyState === WebSocket.OPEN
  }
  get connectedSince(): number { return this.lastOpenTs }
}
