import { ApiClient } from './api'
import type { DeviceStatus } from './types'

export interface DiscoveredDevice {
  host: string
  status: DeviceStatus
}

/** Probe a single host. Returns status payload or null on any failure. */
export async function probe(host: string, timeoutMs = 1800): Promise<DiscoveredDevice | null> {
  const ctrl = new AbortController()
  const to = setTimeout(() => ctrl.abort(), timeoutMs)
  try {
    const status = await new ApiClient(host).status(ctrl.signal)
    return { host, status }
  } catch {
    return null
  } finally {
    clearTimeout(to)
  }
}

/**
 * Scan well-known hostnames in parallel. Browsers can't do raw mDNS, but
 * most modern OSes (macOS, iOS, Windows w/ Bonjour, Android 12+) resolve
 * `.local` names the firmware advertises via ESPmDNS. If we're on the
 * device's AP, 192.168.4.1 is the guaranteed address. Extra hosts can be
 * layered on top (e.g., saved devices).
 */
export async function scanDefaults(extra: string[] = []): Promise<DiscoveredDevice[]> {
  const seen = new Set<string>()
  const candidates = ['smartrc.local', '192.168.4.1', ...extra]
    .map((h) => h.trim())
    .filter((h) => h.length > 0 && !seen.has(h) && seen.add(h))
  const results = await Promise.all(candidates.map((h) => probe(h)))
  return results.filter((r): r is DiscoveredDevice => r != null)
}
