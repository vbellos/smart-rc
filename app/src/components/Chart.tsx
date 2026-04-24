import { useMemo } from 'react'
import type { ReactNode } from 'react'

/**
 * Minimal SVG line/area chart. Hand-rolled (zero chart-library deps) so it
 * matches the app's 60-KB-gzipped weight budget. Designed for one or many
 * series on a shared Y axis with auto-scaling and a subtle dotted grid.
 *
 * Shape rules:
 *   - vbox is a fixed 100 × 30 plane — CSS height scales it up non-
 *     uniformly; stroke uses `vectorEffect=non-scaling-stroke` so lines
 *     stay a consistent pixel thickness regardless of aspect ratio.
 *   - If `fill` is true, we also emit a faded area under the first series.
 */

export interface ChartSeries {
  label: string
  /** CSS color (hex / rgb / named). */
  color: string
  data: number[]
  /** Stroke width in vbox units. 0.6 looks right at default heights. */
  width?: number
  dashed?: boolean
}

export interface LineChartProps {
  series: ChartSeries[]
  /** Rendered height in CSS pixels. */
  height?: number
  /** Override auto-ranged min/max. */
  min?: number
  max?: number
  /** Draw area fill under the first series. */
  fill?: boolean
}

export function LineChart({
  series,
  height = 110,
  min,
  max,
  fill = false,
}: LineChartProps) {
  const vboxW = 100
  const vboxH = 30

  const { effMin, effMax } = useMemo(() => {
    const all = series.flatMap((s) => s.data)
    if (all.length === 0) return { effMin: 0, effMax: 1 }
    const autoMin = Math.min(...all)
    const autoMax = Math.max(...all)
    // Pad 8% so the line never sits flush with the top/bottom edges.
    const pad = Math.max(0.0001, (autoMax - autoMin) * 0.08)
    return {
      effMin: min ?? autoMin - pad,
      effMax: max ?? autoMax + pad,
    }
  }, [series, min, max])

  const span = Math.max(0.0001, effMax - effMin)

  function pathFor(data: number[]): string {
    if (data.length < 1) return ''
    const step = data.length > 1 ? vboxW / (data.length - 1) : 0
    return data
      .map((v, i) => {
        const x = i * step
        const y = vboxH - ((v - effMin) / span) * vboxH
        return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
      })
      .join(' ')
  }

  function areaFor(data: number[]): string {
    const line = pathFor(data)
    if (!line || data.length < 2) return ''
    const step = vboxW / (data.length - 1)
    const lastX = ((data.length - 1) * step).toFixed(2)
    return `${line} L${lastX},${vboxH} L0,${vboxH} Z`
  }

  const gridLines = [0.2, 0.4, 0.6, 0.8]

  return (
    <svg
      viewBox={`0 0 ${vboxW} ${vboxH}`}
      preserveAspectRatio="none"
      className="block w-full"
      style={{ height }}
      aria-hidden
    >
      {/* Horizontal grid */}
      {gridLines.map((f) => (
        <line
          key={f}
          x1={0} x2={vboxW}
          y1={vboxH * f} y2={vboxH * f}
          stroke="rgba(255,255,255,0.04)"
          strokeWidth={0.15}
        />
      ))}
      {/* Series — draw each on top of any previous area fill */}
      {series.map((s, i) => (
        <g key={i}>
          {fill && i === 0 && (
            <path
              d={areaFor(s.data)}
              fill={s.color}
              opacity={0.12}
            />
          )}
          <path
            d={pathFor(s.data)}
            fill="none"
            stroke={s.color}
            strokeWidth={s.width ?? 0.6}
            strokeDasharray={s.dashed ? '1 1' : undefined}
            strokeLinecap="round"
            strokeLinejoin="round"
            vectorEffect="non-scaling-stroke"
          />
        </g>
      ))}
    </svg>
  )
}

/* --------------------------------------------------------------------- */
/* Chart card — title + hero value + chart + legend, ready to grid-drop.  */
/* --------------------------------------------------------------------- */

export interface ChartCardProps {
  /** Uppercase caption, e.g. "Signal". */
  title: string
  subtitle?: string
  series: ChartSeries[]
  unit?: string
  height?: number
  min?: number
  max?: number
  windowLabel?: string
  /** Icon element (e.g. <Radio className="size-4" />). */
  icon?: ReactNode
  /** Override which series' latest value is shown as the hero number. */
  primaryIndex?: number
  /** Format override for the hero value. Default: 1 decimal when non-integer. */
  format?: (v: number) => string
  /** Area fill under the primary line. */
  fill?: boolean
}

export function ChartCard({
  title,
  subtitle,
  series,
  unit,
  height = 110,
  min,
  max,
  windowLabel = 'last 60 s',
  icon,
  primaryIndex = 0,
  format,
  fill = true,
}: ChartCardProps) {
  const primary = series[primaryIndex]
  const latest = primary?.data.at(-1)
  const latestText = latest != null
    ? (format ? format(latest) : defaultFormat(latest))
    : '—'

  return (
    <div className="card p-5">
      <div className="flex items-center justify-between mb-1">
        <span className="caption">{title}</span>
        {icon && <span className="text-ink-400">{icon}</span>}
      </div>
      {subtitle && <div className="text-[11px] text-ink-500 mb-2">{subtitle}</div>}

      <div className="flex items-baseline gap-1.5 mb-3">
        <span className="hero-num">{latestText}</span>
        {unit && <span className="text-sm text-ink-400 font-medium">{unit}</span>}
      </div>

      <LineChart series={series} height={height} min={min} max={max} fill={fill} />

      <div className="flex items-center justify-between mt-3 text-[10px]">
        <div className="flex flex-wrap gap-x-3 gap-y-1">
          {series.map((s, i) => (
            <div key={i} className="flex items-center gap-1.5 text-ink-300">
              <span className="size-2 rounded-full" style={{ background: s.color }} />
              {s.label}
              {s.data.length > 0 && (
                <span className="tabular-nums text-ink-500">
                  {defaultFormat(s.data.at(-1)!)}
                </span>
              )}
            </div>
          ))}
        </div>
        <span className="text-ink-500">{windowLabel}</span>
      </div>
    </div>
  )
}

function defaultFormat(v: number): string {
  if (!Number.isFinite(v)) return '—'
  if (Number.isInteger(v)) return v.toString()
  if (Math.abs(v) >= 100) return v.toFixed(0)
  if (Math.abs(v) >= 10) return v.toFixed(1)
  return v.toFixed(2)
}
