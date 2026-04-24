import type {
  DeviceConfig,
  DeviceStatus,
  WifiNetwork,
} from './types'

/**
 * Thin wrapper around the firmware's REST surface. All endpoints are
 * same-origin CORS-friendly (the device sets Access-Control-Allow-Origin: *),
 * so this works regardless of where the app is hosted.
 */
export class ApiClient {
  constructor(public host: string) {}

  private base(): string {
    // Accept either bare host ("smartrc.local"), host:port, or a full URL.
    if (this.host.startsWith('http://') || this.host.startsWith('https://')) {
      return this.host.replace(/\/$/, '')
    }
    return `http://${this.host}`
  }

  private async fetchJson<T>(path: string, init?: RequestInit): Promise<T> {
    const res = await fetch(`${this.base()}${path}`, init)
    if (!res.ok) {
      const text = await res.text().catch(() => '')
      throw new Error(`${res.status} ${res.statusText}${text ? ` — ${text}` : ''}`)
    }
    return res.json() as Promise<T>
  }

  /* --------- Status + config --------- */

  status(signal?: AbortSignal) {
    return this.fetchJson<DeviceStatus>('/api/status', { signal })
  }

  getConfig() {
    return this.fetchJson<DeviceConfig>('/api/config')
  }

  postConfig(partial: Partial<DeviceConfig>) {
    return this.fetchJson<{ ok: boolean; message: string }>('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(partial),
    })
  }

  schema() {
    return this.fetchJson<unknown>('/api/schema')
  }

  /* --------- Control (HTTP fallback for WS) --------- */

  control(action: string, speed?: number) {
    return this.fetchJson<{ ok: boolean; reason: string }>('/api/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(speed ? { action, speed } : { action }),
    })
  }

  /* --------- Wi-Fi --------- */

  wifiScan() {
    return this.fetchJson<{ count: number; networks: WifiNetwork[] }>(
      '/api/wifi/scan'
    )
  }

  wifiProvision(ssid: string, password: string) {
    return this.fetchJson<{
      ok: boolean
      saved?: boolean
      ssid?: string
      ip?: string
      rssi?: number
      apWillShutDown?: boolean
      reason?: string
      message?: string
    }>('/api/wifi/provision', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password }),
    })
  }

  /* --------- Reset --------- */

  reboot() { return this.fetchJson('/api/reboot', { method: 'POST' }) }
  resetNetwork() { return this.fetchJson('/api/reset/network', { method: 'POST' }) }
  resetFactory() { return this.fetchJson('/api/reset/factory', { method: 'POST' }) }
}
