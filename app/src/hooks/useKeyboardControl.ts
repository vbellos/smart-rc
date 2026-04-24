import { useEffect, useRef } from 'react'
import { useControl } from './useControl'
import type { Command } from '../lib/types'

export interface KeyBinding {
  /** Keys that trigger this binding, matched against `KeyboardEvent.key`. */
  keys: string[]
  /** Command fired on keydown (and every `repeatMs` while held). */
  action: Command
  /** If > 0, keep re-sending `action` at this cadence while the key is held. */
  repeatMs?: number
  /** Single command fired on keyup. Typical use: 'stop' / 'steer_stop'. */
  releaseAction?: Command
  /** Human label for the shortcut overlay. */
  label: string
}

/**
 * Drive bindings. Keep the steer repeat faster than drive repeat because
 * steering uses the pulse state machine on-device — we want fresh pings
 * arriving well inside the pulse window. Matches the ControlPad rates.
 */
export const KEY_BINDINGS: KeyBinding[] = [
  { keys: ['ArrowUp',    'w', 'W'], action: 'forward', repeatMs: 200, releaseAction: 'stop',       label: '↑ / W' },
  { keys: ['ArrowDown',  's', 'S'], action: 'reverse', repeatMs: 200, releaseAction: 'stop',       label: '↓ / S' },
  { keys: ['ArrowLeft',  'a', 'A'], action: 'left',    repeatMs: 100, releaseAction: 'steer_stop', label: '← / A' },
  { keys: ['ArrowRight', 'd', 'D'], action: 'right',   repeatMs: 100, releaseAction: 'steer_stop', label: '→ / D' },
  { keys: [' '],                    action: 'brake',                                               label: 'Space' },
]

function findBinding(key: string): KeyBinding | undefined {
  return KEY_BINDINGS.find((b) => b.keys.includes(key))
}

function isTypingTarget(): boolean {
  const el = document.activeElement as HTMLElement | null
  if (!el) return false
  const tag = el.tagName
  if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return true
  if (el.isContentEditable) return true
  return false
}

/**
 * Global keyboard-control hook. Registers once on the document while
 * `enabled` is true; no-op while false. Sends every keystroke through
 * the same `useControl()` pipeline the pad uses (WS first, HTTP fallback)
 * so both input methods fight for the same heartbeat stream.
 *
 * Contract:
 *   - First keydown per key  -> send(action)
 *   - Held keys              -> send(action) every repeatMs (no browser-repeat)
 *   - Keyup / blur / hidden  -> send(releaseAction) exactly once per key
 *   - Modifier keys (Ctrl/Alt/Cmd)  -> ignored so browser shortcuts stay alive
 *   - Focus inside an input  -> ignored so typing works
 */
export function useKeyboardControl(enabled: boolean): void {
  const send = useControl()
  const sendRef = useRef(send)
  sendRef.current = send

  useEffect(() => {
    if (!enabled) return

    // Track state across the whole session lifetime of this effect.
    const held = new Set<string>()
    const intervals = new Map<string, number>()

    function release(key: string) {
      if (!held.has(key)) return
      held.delete(key)
      const id = intervals.get(key)
      if (id !== undefined) {
        clearInterval(id)
        intervals.delete(key)
      }
      const b = findBinding(key)
      if (b?.releaseAction) sendRef.current(b.releaseAction)
    }

    function releaseAll() {
      for (const k of Array.from(held)) release(k)
    }

    function onKeyDown(e: KeyboardEvent) {
      if (e.repeat) return                          // kill browser auto-repeat
      if (e.ctrlKey || e.altKey || e.metaKey) return
      if (isTypingTarget()) return
      const b = findBinding(e.key)
      if (!b) return
      e.preventDefault()                            // block page scroll on arrows/space
      if (held.has(e.key)) return
      held.add(e.key)
      sendRef.current(b.action)
      if (b.repeatMs && b.repeatMs > 0) {
        const id = window.setInterval(
          () => sendRef.current(b.action),
          b.repeatMs
        )
        intervals.set(e.key, id)
      }
    }

    function onKeyUp(e: KeyboardEvent) {
      if (!findBinding(e.key)) return
      e.preventDefault()
      release(e.key)
    }

    function onBlur() { releaseAll() }
    function onVisibility() { if (document.hidden) releaseAll() }

    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('keyup', onKeyUp)
    window.addEventListener('blur', onBlur)
    document.addEventListener('visibilitychange', onVisibility)

    return () => {
      releaseAll()
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('keyup', onKeyUp)
      window.removeEventListener('blur', onBlur)
      document.removeEventListener('visibilitychange', onVisibility)
    }
  }, [enabled])
}
