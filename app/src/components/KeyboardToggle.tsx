import { useEffect, useState } from 'react'
import { useKeyboardControl, KEY_BINDINGS } from '../hooks/useKeyboardControl'
import { Keyboard } from './Icons'

const STORAGE_KEY = 'smartrc.keyboard'

function readStoredEnabled(): boolean {
  const v = localStorage.getItem(STORAGE_KEY)
  // Default: enabled on desktop (pointer: fine), disabled on touch-only.
  if (v === 'on')  return true
  if (v === 'off') return false
  return typeof window !== 'undefined' &&
    window.matchMedia?.('(pointer: fine)').matches === true
}

/**
 * Sidebar toggle that wires up `useKeyboardControl` globally. Persists
 * state in localStorage so preference survives reloads. Hover the tile
 * to see the keybinding help popover.
 */
export function KeyboardToggle() {
  const [enabled, setEnabled] = useState<boolean>(readStoredEnabled)
  const [showHelp, setShowHelp] = useState(false)

  useKeyboardControl(enabled)

  useEffect(() => {
    localStorage.setItem(STORAGE_KEY, enabled ? 'on' : 'off')
  }, [enabled])

  const toneOn  = 'bg-lime-500/15 text-lime-400 ring-lime-500/25'
  const toneOff = 'bg-[#161619] text-ink-500 ring-[#26262e] hover:text-white'

  return (
    <div
      className="relative"
      onMouseEnter={() => setShowHelp(true)}
      onMouseLeave={() => setShowHelp(false)}
    >
      <button
        type="button"
        onClick={() => setEnabled((v) => !v)}
        className={`nav-tile ${enabled ? toneOn : toneOff}`}
        title={enabled ? 'Keyboard control: ON' : 'Keyboard control: OFF'}
        aria-pressed={enabled}
      >
        <Keyboard className="size-5" />
      </button>

      {showHelp && (
        <div
          className="absolute left-full ml-2 top-1/2 -translate-y-1/2 w-56
                     card p-3 text-xs z-30 pointer-events-none"
          role="tooltip"
        >
          <div className="flex items-center justify-between mb-2">
            <span className="caption">Keyboard</span>
            <span className={`pill ${enabled ? 'pill-ok' : 'pill-muted'}`}>
              {enabled ? 'ON' : 'OFF'}
            </span>
          </div>
          <ul className="space-y-1.5">
            {KEY_BINDINGS.map((b) => (
              <li key={b.label} className="flex items-center justify-between gap-3">
                <kbd className="font-mono text-ink-200 bg-[#0f0f11] px-1.5 py-0.5 rounded ring-1 ring-[#26262e]">
                  {b.label}
                </kbd>
                <span className="text-ink-300">{b.action}</span>
              </li>
            ))}
          </ul>
          <div className="mt-2 pt-2 border-t border-[#1c1c22] text-[10px] text-ink-500">
            Ignored while typing in a form.
          </div>
        </div>
      )}
    </div>
  )
}
