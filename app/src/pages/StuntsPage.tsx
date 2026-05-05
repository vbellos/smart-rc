import { useEffect, useState } from 'react'
import { PageHeader, ConnectionBadge } from '../components/Layout'
import { useDevice } from '../context/DeviceContext'
import { useEvents } from '../hooks/useTelemetry'
import {
  AlertOctagon,
  CornerUpLeft,
  CornerUpRight,
  RotateCCW,
  RotateCW,
  Sparkle,
  Wind,
  X,
} from '../components/Icons'
import type { ReactNode } from 'react'

/**
 * Scripted maneuvers — "stunts". Each button fires a short, pre-baked
 * sequence of drive + steering commands that the firmware's StuntEngine
 * runs as a small state machine. Any direct user command (keyboard,
 * drive pad, WS `cmd` frame) immediately cancels an active stunt.
 */

interface StuntDef {
  id: string
  name: string
  blurb: string
  icon: ReactNode
  experimental?: boolean
  /** Tailwind tone — accent ring + hover color per card. */
  tone: 'lime' | 'sky' | 'violet' | 'amber' | 'rose'
}

const STUNTS: StuntDef[] = [
  { id: 'spin_left',     name: 'Spin Left',     blurb: '360° rotation, full lock + throttle (gyro-feedback).',
    icon: <RotateCCW className="size-6"/>, tone: 'lime' },
  { id: 'spin_right',    name: 'Spin Right',    blurb: '360° rotation, full lock + throttle (gyro-feedback).',
    icon: <RotateCW className="size-6"/>,  tone: 'lime' },
  { id: 'j_turn_left',   name: 'J-Turn Left',   blurb: 'Forward, hard brake, full-lock reverse — end facing back.',
    icon: <CornerUpLeft className="size-6"/>, tone: 'sky' },
  { id: 'j_turn_right',  name: 'J-Turn Right',  blurb: 'Mirror of J-Turn Left.',
    icon: <CornerUpRight className="size-6"/>, tone: 'sky' },
  { id: 'wiggle',        name: 'Wiggle',        blurb: 'Rapid L-R steering while cruising. Always works.',
    icon: <Wind className="size-6"/>, tone: 'violet' },
  { id: 'drift_left',    name: 'Drift Left',    blurb: 'Hard steer + throttle + counter-steer. Needs slippery floor.',
    icon: <CornerUpLeft className="size-6"/>, tone: 'amber', experimental: true },
  { id: 'drift_right',   name: 'Drift Right',   blurb: 'Mirror of Drift Left.',
    icon: <CornerUpRight className="size-6"/>, tone: 'amber', experimental: true },
  { id: 'power_reverse', name: 'Power Reverse', blurb: 'Forward, hard brake, full reverse.',
    icon: <Wind className="size-6"/>, tone: 'rose' },
]

const TONE = {
  lime:   { ring: 'ring-lime-500/25',   icon: 'text-lime-400',   hover: 'hover:ring-lime-500/50',  active: 'bg-lime-500/15'   },
  sky:    { ring: 'ring-sky-500/25',    icon: 'text-sky-400',    hover: 'hover:ring-sky-500/50',   active: 'bg-sky-500/15'    },
  violet: { ring: 'ring-violet-500/25', icon: 'text-violet-400', hover: 'hover:ring-violet-500/50',active: 'bg-violet-500/15' },
  amber:  { ring: 'ring-amber-400/25',  icon: 'text-amber-400',  hover: 'hover:ring-amber-400/50', active: 'bg-amber-400/15'  },
  rose:   { ring: 'ring-rose-500/25',   icon: 'text-rose-400',   hover: 'hover:ring-rose-500/50',  active: 'bg-rose-500/15'   },
} as const

export default function StuntsPage() {
  const { api } = useDevice()
  const events = useEvents(20)

  const [activeStunt, setActiveStunt] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  // Track stunt state from broadcast events. Firmware emits:
  //   stunt_start / stunt_end / stunt_abort  with `stunt: <name>` payload.
  useEffect(() => {
    const latest = events[0]
    if (!latest) return
    const anyEvent = latest as unknown as { kind: string; stunt?: string }
    if (anyEvent.kind === 'stunt_start' && anyEvent.stunt) {
      setActiveStunt(anyEvent.stunt)
    } else if (anyEvent.kind === 'stunt_end' || anyEvent.kind === 'stunt_abort') {
      setActiveStunt(null)
    }
  }, [events])

  async function run(id: string) {
    if (!api || busy) return
    setBusy(true)
    try {
      await api.runStunt(id)
    } catch (e) {
      console.warn('stunt run failed', e)
    } finally {
      setBusy(false)
    }
  }

  async function abort() {
    if (!api) return
    try { await api.abortStunt() } catch {}
  }

  return (
    <div>
      <PageHeader
        eyebrow="Fun"
        title="Stunts"
        subtitle="Scripted maneuvers. One click fires a sequence — press anything else to abort."
        right={<ConnectionBadge />}
      />

      {/* Warning / space-check banner */}
      <div className="card p-4 mb-6 ring-1 ring-amber-400/20 bg-amber-400/5">
        <div className="flex items-start gap-3">
          <AlertOctagon className="size-5 text-amber-400 shrink-0 mt-0.5"/>
          <div className="text-sm text-ink-200">
            <div className="font-medium text-amber-300 mb-1">Give the RC some space</div>
            <ul className="list-disc pl-5 space-y-0.5 text-ink-300">
              <li>Spins need ~30 cm around the car.</li>
              <li>J-turn / power-reverse need ~1 m clearance front &amp; back.</li>
              <li>Drift works best on tile / hard floor. On carpet it just accelerates.</li>
              <li>Any drive input (keyboard, pad, E-stop) aborts the active stunt.</li>
            </ul>
          </div>
        </div>
      </div>

      {/* Active-stunt banner */}
      {activeStunt && (
        <div className="card p-4 mb-6 ring-1 ring-lime-500/30 bg-lime-500/5
                        flex items-center gap-3 animate-pulse">
          <Sparkle className="size-5 text-lime-400"/>
          <div className="flex-1 min-w-0">
            <div className="text-sm text-white font-medium">
              Running: {prettyName(activeStunt)}
            </div>
            <div className="text-xs text-ink-300 mt-0.5">Any drive input will abort.</div>
          </div>
          <button onClick={abort}
            className="px-4 h-10 rounded-xl bg-rose-500/10 ring-1 ring-rose-500/30
                       text-rose-400 font-semibold hover:bg-rose-500/20">
            <X className="size-4 inline -mt-0.5 mr-1"/> Abort
          </button>
        </div>
      )}

      {/* Stunt grid */}
      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
        {STUNTS.map((s) => {
          const tone = TONE[s.tone]
          const isActive = activeStunt === s.id
          return (
            <button key={s.id}
              onClick={() => run(s.id)}
              disabled={busy || !!activeStunt}
              className={`group text-left card p-5 ring-1 ${tone.ring} ${tone.hover}
                         transition-all disabled:opacity-40 disabled:cursor-not-allowed
                         ${isActive ? tone.active : ''}`}>
              <div className="flex items-start justify-between gap-3">
                <div className={`size-11 rounded-xl grid place-items-center
                               ring-1 ${tone.ring} ${tone.active ?? ''} ${tone.icon}`}>
                  {s.icon}
                </div>
                {s.experimental && (
                  <span className="pill pill-muted text-[10px]">experimental</span>
                )}
              </div>
              <h3 className="mt-3 text-white font-medium">{s.name}</h3>
              <p className="text-xs text-ink-400 mt-1 leading-relaxed">{s.blurb}</p>
            </button>
          )
        })}
      </div>

      {/* Recent events log (only stunt-related) */}
      <section className="mt-8">
        <h2 className="caption mb-3">Recent stunt events</h2>
        <div className="card p-0 overflow-hidden">
          {events.filter(eventIsStunt).length === 0 ? (
            <div className="p-6 text-center text-sm text-ink-400">
              No stunt events yet. Hit a button to try one.
            </div>
          ) : (
            <ul className="divide-y divide-[#1c1c22]">
              {events.filter(eventIsStunt).map((e, i) => {
                const ev = e as unknown as { kind: string; stunt?: string; ts?: number }
                return (
                  <li key={i} className="px-4 py-3 flex items-center gap-3 text-sm">
                    <span className={`size-2 rounded-full ${dotForKind(ev.kind)}`}/>
                    <span className="font-medium text-white">{ev.kind}</span>
                    {ev.stunt && <span className="text-ink-400">· {prettyName(ev.stunt)}</span>}
                    {ev.ts != null && (
                      <span className="ml-auto text-xs text-ink-500 tabular-nums">{ev.ts}ms</span>
                    )}
                  </li>
                )
              })}
            </ul>
          )}
        </div>
      </section>
    </div>
  )
}

function eventIsStunt(e: { kind: string }): boolean {
  return e.kind.startsWith('stunt_')
}

function dotForKind(kind: string): string {
  if (kind === 'stunt_start') return 'bg-lime-400'
  if (kind === 'stunt_end')   return 'bg-sky-400'
  if (kind === 'stunt_abort') return 'bg-rose-400'
  return 'bg-ink-500'
}

function prettyName(id: string): string {
  return id.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase())
}
