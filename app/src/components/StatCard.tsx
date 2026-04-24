import type { ReactNode } from 'react'

interface StatCardProps {
  eyebrow: string
  value: ReactNode
  unit?: string
  sub?: ReactNode
  icon?: ReactNode
  tone?: 'neutral' | 'ok' | 'warn' | 'err'
  /** 0..1 — shows a progress bar under the value. */
  progress?: number
}

const toneBorder = {
  neutral: 'ring-[#26262e]',
  ok:      'ring-lime-500/25',
  warn:    'ring-amber-400/25',
  err:     'ring-rose-500/25',
} as const

const toneBar = {
  neutral: 'bg-ink-300',
  ok:      'bg-lime-400',
  warn:    'bg-amber-400',
  err:     'bg-rose-500',
} as const

/** Dashboard-style stat card — big hero number + tiny caption. */
export function StatCard({ eyebrow, value, unit, sub, icon, tone = 'neutral', progress }: StatCardProps) {
  const pct = progress == null ? null : Math.max(0, Math.min(1, progress)) * 100
  return (
    <div className={`card p-5 ring-1 ${toneBorder[tone]}`}>
      <div className="flex items-center justify-between">
        <span className="caption">{eyebrow}</span>
        {icon && <span className="text-ink-400">{icon}</span>}
      </div>
      <div className="mt-3 flex items-baseline gap-1.5">
        <span className="hero-num">{value}</span>
        {unit && <span className="text-sm text-ink-400 font-medium">{unit}</span>}
      </div>
      {pct != null && (
        <div className="mt-3 h-1.5 rounded-full bg-white/5 overflow-hidden">
          <div
            className={`h-full ${toneBar[tone]} rounded-full transition-all duration-300`}
            style={{ width: `${pct}%` }}
          />
        </div>
      )}
      {sub && <div className="mt-2 text-xs text-ink-300">{sub}</div>}
    </div>
  )
}
