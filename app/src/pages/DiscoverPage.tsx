import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useDevice } from '../context/DeviceContext'
import { scanDefaults, type DiscoveredDevice } from '../lib/discovery'
import { forgetDevice, getLastHost, getSavedDevices } from '../lib/storage'
import type { SavedDevice } from '../lib/types'
import { Search, Plug, Wifi, X, Check } from '../components/Icons'

export default function DiscoverPage() {
  const { connect, connecting } = useDevice()
  const nav = useNavigate()
  const [scanning, setScanning] = useState(false)
  const [found, setFound] = useState<DiscoveredDevice[]>([])
  const [saved, setSaved] = useState<SavedDevice[]>(getSavedDevices())
  const [manual, setManual] = useState('')
  const [err, setErr] = useState<string | null>(null)

  // Auto-scan on mount + auto-connect to last known host if reachable.
  useEffect(() => {
    void runScan()
    const last = getLastHost()
    if (last) {
      ;(async () => {
        const ok = await connect(last)
        if (ok) nav('/drive')
      })()
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  async function runScan() {
    setScanning(true)
    setErr(null)
    try {
      const extra = saved.map((d) => d.host)
      const result = await scanDefaults(extra)
      setFound(result)
    } finally {
      setScanning(false)
    }
  }

  async function doConnect(host: string) {
    setErr(null)
    const ok = await connect(host)
    if (ok) { nav('/drive'); return }
    setErr(`Couldn't reach ${host}. Check that the RC is powered and on the same network.`)
  }

  async function submitManual(e: React.FormEvent) {
    e.preventDefault()
    const h = manual.trim()
    if (!h) return
    await doConnect(h)
  }

  function handleForget(host: string) {
    forgetDevice(host)
    setSaved(getSavedDevices())
  }

  return (
    <div className="max-w-3xl mx-auto">
      <div className="text-center pt-6 sm:pt-10 mb-8">
        <div className="mx-auto mb-4 size-16 rounded-3xl grid place-items-center
                        bg-gradient-to-br from-lime-400 to-emerald-600
                        text-ink-950 shadow-lg shadow-lime-500/20 ring-1 ring-white/10">
          <Plug className="size-7" />
        </div>
        <div className="caption mb-2">Smart RC — Controller</div>
        <h1 className="text-3xl sm:text-4xl font-semibold tracking-tight text-white">
          Pick a device to drive
        </h1>
        <p className="text-ink-300 mt-2 text-sm">
          Devices on your network that advertise the Smart RC firmware.
        </p>
      </div>

      {err && (
        <div className="card p-4 mb-6 ring-1 ring-rose-500/30 bg-rose-500/5 text-rose-300 text-sm">
          {err}
        </div>
      )}

      <section className="mb-8">
        <div className="flex items-center justify-between mb-3">
          <h2 className="caption">Found on network</h2>
          <button onClick={runScan}
            disabled={scanning}
            className="pill pill-muted hover:text-white disabled:opacity-50">
            <Search className="size-3.5"/> {scanning ? 'Scanning…' : 'Rescan'}
          </button>
        </div>

        {found.length === 0 && !scanning && (
          <div className="card p-6 text-center text-ink-400 text-sm">
            No devices answered a probe. Hit <span className="text-ink-200">Rescan</span>,
            or connect manually below.
          </div>
        )}

        <div className="grid gap-3 sm:grid-cols-2">
          {found.map((d) => (
            <DeviceCard
              key={d.host}
              host={d.host}
              label={d.status.net.hostname || d.host}
              subtitle={`${d.status.net.mode.toUpperCase()} · ${d.status.net.ip}`}
              onConnect={() => doConnect(d.host)}
              connecting={connecting}
            />
          ))}
        </div>
      </section>

      {saved.length > 0 && (
        <section className="mb-8">
          <h2 className="caption mb-3">Recent devices</h2>
          <div className="grid gap-3 sm:grid-cols-2">
            {saved.map((d) => (
              <DeviceCard
                key={d.host}
                host={d.host}
                label={d.label || d.host}
                subtitle={d.lastSeen ? `Seen ${relativeTime(d.lastSeen)}` : d.host}
                onConnect={() => doConnect(d.host)}
                onForget={() => handleForget(d.host)}
                connecting={connecting}
              />
            ))}
          </div>
        </section>
      )}

      <section>
        <h2 className="caption mb-3">Connect manually</h2>
        <form onSubmit={submitManual} className="card p-4 flex gap-2">
          <div className="flex items-center gap-2 flex-1 px-3 rounded-xl bg-[#0f0f11] ring-1 ring-[#26262e] focus-within:ring-white/20">
            <Wifi className="size-4 text-ink-400"/>
            <input
              value={manual}
              onChange={(e) => setManual(e.target.value)}
              placeholder="smartrc.local or 192.168.1.42"
              className="flex-1 bg-transparent py-2.5 text-sm outline-none placeholder:text-ink-500"
              autoCapitalize="none"
              autoCorrect="off"
            />
          </div>
          <button type="submit"
            disabled={connecting}
            className="px-4 rounded-xl bg-lime-500 text-ink-950 font-semibold
                       hover:bg-lime-400 active:scale-[0.98] disabled:opacity-50">
            Connect
          </button>
        </form>
      </section>
    </div>
  )
}

function DeviceCard({
  host, label, subtitle, onConnect, onForget, connecting,
}: {
  host: string
  label: string
  subtitle: string
  onConnect: () => void
  onForget?: () => void
  connecting: boolean
}) {
  return (
    <div className="card p-4 flex items-center gap-3">
      <div className="size-10 rounded-xl grid place-items-center bg-lime-500/10 text-lime-400 ring-1 ring-lime-500/20">
        <Check className="size-5"/>
      </div>
      <div className="flex-1 min-w-0">
        <div className="text-white font-medium truncate">{label}</div>
        <div className="text-xs text-ink-400 truncate">{host} · {subtitle}</div>
      </div>
      <div className="flex items-center gap-1">
        {onForget && (
          <button onClick={onForget}
            className="size-9 grid place-items-center rounded-lg text-ink-400
                       hover:text-rose-400 hover:bg-white/5"
            title="Forget">
            <X className="size-4"/>
          </button>
        )}
        <button onClick={onConnect}
          disabled={connecting}
          className="px-4 h-9 rounded-lg bg-lime-500 text-ink-950 text-sm font-semibold
                     hover:bg-lime-400 active:scale-[0.98] disabled:opacity-50">
          Connect
        </button>
      </div>
    </div>
  )
}

function relativeTime(ts: number): string {
  const diff = Date.now() - ts
  const s = Math.floor(diff / 1000)
  if (s < 60) return 'just now'
  const m = Math.floor(s / 60)
  if (m < 60) return `${m}m ago`
  const h = Math.floor(m / 60)
  if (h < 24) return `${h}h ago`
  const d = Math.floor(h / 24)
  return `${d}d ago`
}
