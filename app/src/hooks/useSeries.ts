import { useEffect, useRef, useState } from 'react'

/**
 * Fixed-capacity rolling buffer of a numeric value, sampled at a regular
 * cadence regardless of how often the source actually changes. Gives the
 * chart a steady timeline even when telemetry is bursty or idle.
 *
 *   value     — live value; `null`/`undefined` = "no sample this tick"
 *   capacity  — max number of points to retain
 *   sampleMs  — sampling period; 500 ms → ~60 s window at capacity=120
 */
export function useTimeSeries(
  value: number | null | undefined,
  capacity = 120,
  sampleMs = 500
): number[] {
  const [buf, setBuf] = useState<number[]>([])
  const valueRef = useRef(value)
  useEffect(() => {
    valueRef.current = value
  }, [value])

  useEffect(() => {
    const id = setInterval(() => {
      const v = valueRef.current
      if (v == null || Number.isNaN(v)) return
      setBuf((prev) => {
        const next = prev.length >= capacity ? prev.slice(-(capacity - 1)) : prev
        return [...next, v]
      })
    }, sampleMs)
    return () => clearInterval(id)
  }, [capacity, sampleMs])

  return buf
}

/**
 * Multi-series variant — one buffer per key. Re-samples every tick;
 * missing keys in this tick are simply not appended to that buffer.
 */
export function useMultiSeries(
  getValues: () => Record<string, number | null | undefined>,
  capacity = 120,
  sampleMs = 500
): Record<string, number[]> {
  const [bufs, setBufs] = useState<Record<string, number[]>>({})
  const getRef = useRef(getValues)
  useEffect(() => { getRef.current = getValues }, [getValues])

  useEffect(() => {
    const id = setInterval(() => {
      const vals = getRef.current()
      setBufs((prev) => {
        const next: Record<string, number[]> = { ...prev }
        for (const [k, v] of Object.entries(vals)) {
          if (v == null || Number.isNaN(v)) continue
          const arr = next[k] ?? []
          const trimmed = arr.length >= capacity ? arr.slice(-(capacity - 1)) : arr
          next[k] = [...trimmed, v]
        }
        return next
      })
    }, sampleMs)
    return () => clearInterval(id)
  }, [capacity, sampleMs])

  return bufs
}

/** Tally how many times a counter has advanced per second over a window. */
export function useRateSeries(counter: number, capacity = 120, sampleMs = 1000): number[] {
  const [buf, setBuf] = useState<number[]>([])
  const prev = useRef(counter)
  const cur = useRef(counter)
  useEffect(() => { cur.current = counter }, [counter])

  useEffect(() => {
    const id = setInterval(() => {
      const delta = cur.current - prev.current
      prev.current = cur.current
      setBuf((b) => {
        const trimmed = b.length >= capacity ? b.slice(-(capacity - 1)) : b
        return [...trimmed, Math.max(0, delta)]
      })
    }, sampleMs)
    return () => clearInterval(id)
  }, [capacity, sampleMs])

  return buf
}
