import type { SavedDevice } from './types'

const KEY_DEVICES   = 'smartrc.devices'
const KEY_LAST_HOST = 'smartrc.lastHost'

export function getSavedDevices(): SavedDevice[] {
  try {
    const raw = localStorage.getItem(KEY_DEVICES)
    if (!raw) return []
    const parsed = JSON.parse(raw) as SavedDevice[]
    return Array.isArray(parsed) ? parsed : []
  } catch {
    return []
  }
}

export function rememberDevice(dev: SavedDevice) {
  const list = getSavedDevices()
  const now = Date.now()
  const entry: SavedDevice = { ...dev, lastSeen: now }
  const i = list.findIndex((d) => d.host === dev.host)
  if (i >= 0) list[i] = { ...list[i], ...entry }
  else list.unshift(entry)
  localStorage.setItem(KEY_DEVICES, JSON.stringify(list.slice(0, 12)))
  localStorage.setItem(KEY_LAST_HOST, dev.host)
}

export function forgetDevice(host: string) {
  const list = getSavedDevices().filter((d) => d.host !== host)
  localStorage.setItem(KEY_DEVICES, JSON.stringify(list))
  if (localStorage.getItem(KEY_LAST_HOST) === host) {
    localStorage.removeItem(KEY_LAST_HOST)
  }
}

export function getLastHost(): string | null {
  return localStorage.getItem(KEY_LAST_HOST)
}
