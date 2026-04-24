import { useEffect, useRef } from 'react'
import type { PointerEvent as RPointerEvent } from 'react'

interface HoldOptions {
  onPress: () => void
  /** How often to re-fire while held (ms). */
  repeatMs?: number
  /** Custom repeat callback; defaults to onPress. */
  onRepeat?: () => void
  /** Fires once on release. Typical use: send an explicit stop command. */
  onRelease?: () => void
}

/**
 * Press-and-hold hook built on Pointer Events + setPointerCapture.
 * Once pressed, the button owns that pointer until pointerup/cancel — so
 * a wobbling finger / drifting cursor CAN'T silently kill the hold.
 * Safety nets: tab-hide + window-blur both trigger the release.
 *
 * Returns props to spread onto any HTMLElement.
 */
export function useHold(opts: HoldOptions) {
  const { onPress, onRelease, onRepeat, repeatMs = 200 } = opts

  // Stable refs so handlers capture the latest callbacks without
  // re-attaching listeners every render.
  const pressRef   = useRef(onPress);   pressRef.current   = onPress
  const releaseRef = useRef(onRelease); releaseRef.current = onRelease
  const repeatRef  = useRef(onRepeat ?? onPress); repeatRef.current = onRepeat ?? onPress

  const timerRef     = useRef<number | null>(null)
  const pointerIdRef = useRef<number | null>(null)
  const targetRef    = useRef<HTMLElement | null>(null)

  const release = () => {
    if (timerRef.current != null) {
      clearInterval(timerRef.current)
      timerRef.current = null
    }
    const t = targetRef.current
    if (pointerIdRef.current != null && t?.releasePointerCapture) {
      try { t.releasePointerCapture(pointerIdRef.current) } catch { /* ignore */ }
    }
    pointerIdRef.current = null
    targetRef.current = null
    releaseRef.current?.()
  }

  const press = (e: RPointerEvent<HTMLElement>) => {
    e.preventDefault()
    if (timerRef.current != null) return // already holding
    targetRef.current = e.currentTarget
    if (e.currentTarget.setPointerCapture) {
      try {
        e.currentTarget.setPointerCapture(e.pointerId)
        pointerIdRef.current = e.pointerId
      } catch { /* some browsers refuse on non-primary pointers */ }
    }
    pressRef.current()
    timerRef.current = window.setInterval(() => repeatRef.current(), repeatMs)
  }

  // Global safety nets — release even if the browser swallows pointer events.
  useEffect(() => {
    const onVis = () => { if (document.hidden) release() }
    const onBlur = () => release()
    window.addEventListener('blur', onBlur)
    document.addEventListener('visibilitychange', onVis)
    return () => {
      window.removeEventListener('blur', onBlur)
      document.removeEventListener('visibilitychange', onVis)
      release() // ensure cleanup if unmounting mid-hold
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  return {
    onPointerDown:         press,
    onPointerUp:           release,
    onPointerCancel:       release,
    onLostPointerCapture:  release,
  }
}
