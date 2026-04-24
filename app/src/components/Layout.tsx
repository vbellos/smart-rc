import { NavLink, Outlet, useNavigate } from 'react-router-dom'
import type { ReactNode } from 'react'
import { useDevice } from '../context/DeviceContext'
import { Compass, Gauge, Radio, Settings, Plug, Check } from './Icons'
import { KeyboardToggle } from './KeyboardToggle'

const navItems = [
  { to: '/drive',   label: 'Drive',   icon: Compass,  tone: 'bg-lime-500/15 text-lime-400 ring-lime-500/25' },
  { to: '/monitor', label: 'Monitor', icon: Gauge,    tone: 'bg-sky-500/15 text-sky-400 ring-sky-500/25' },
  { to: '/setup',   label: 'Setup',   icon: Check,    tone: 'bg-amber-400/15 text-amber-400 ring-amber-400/25' },
  { to: '/config',  label: 'Config',  icon: Settings, tone: 'bg-violet-500/15 text-violet-400 ring-violet-500/25' },
] as const

export function AppShell({ children }: { children?: ReactNode }) {
  return (
    <div className="min-h-full flex text-ink-100">
      <Sidebar />
      <main className="flex-1 min-w-0 px-4 sm:px-8 py-6 sm:py-8 max-w-[1400px] mx-auto w-full">
        {children ?? <Outlet />}
      </main>
    </div>
  )
}

function Sidebar() {
  const { host, wsConnected, disconnect } = useDevice()
  const nav = useNavigate()

  return (
    <aside
      className="sticky top-0 h-screen shrink-0 w-20 sm:w-24
                 flex flex-col items-center py-6 gap-3
                 border-r border-[#1c1c22] bg-[#0d0d0f]/70 backdrop-blur z-20"
    >
      <div className="size-11 rounded-2xl grid place-items-center
                      bg-gradient-to-br from-lime-400 to-emerald-600
                      text-ink-950 font-bold text-lg shadow-lg shadow-lime-500/20
                      ring-1 ring-white/10">
        RC
      </div>

      <div className="h-px w-8 bg-[#1c1c22] my-1" />

      <nav className="flex flex-col items-center gap-2.5">
        {navItems.map(({ to, label, icon: Icon, tone }) => (
          <NavLink key={to} to={to} title={label}
            className={({ isActive }) =>
              `nav-tile ${
                isActive
                  ? tone + ' scale-105'
                  : 'bg-[#161619] text-ink-300 ring-[#26262e] hover:text-white hover:ring-white/15'
              }`
            }>
            <Icon className="size-5" />
          </NavLink>
        ))}
      </nav>

      <div className="mt-auto flex flex-col items-center gap-3">
        <KeyboardToggle />
        <StatusDot connected={wsConnected} />
        {host && (
          <button
            onClick={() => { disconnect(); nav('/', { replace: true }) }}
            className="nav-tile bg-[#161619] text-ink-300 ring-[#26262e]
                       hover:text-rose-400 hover:ring-rose-500/30"
            title={`Disconnect ${host}`}>
            <Plug className="size-5" />
          </button>
        )}
      </div>
    </aside>
  )
}

function StatusDot({ connected }: { connected: boolean }) {
  return (
    <div className="relative grid place-items-center" title={connected ? 'Connected' : 'Disconnected'}>
      <span className={`size-2.5 rounded-full ${connected ? 'bg-lime-400' : 'bg-ink-500'}`} />
      {connected && (
        <span className="absolute size-2.5 rounded-full bg-lime-400 animate-ping opacity-60" />
      )}
    </div>
  )
}

/** Common page header block — title + subtitle + right-side slot. */
export function PageHeader({
  eyebrow,
  title,
  subtitle,
  right,
}: {
  eyebrow?: string
  title: string
  subtitle?: string
  right?: ReactNode
}) {
  return (
    <header className="flex items-end justify-between gap-4 mb-6 sm:mb-8">
      <div>
        {eyebrow && <div className="caption mb-1">{eyebrow}</div>}
        <h1 className="text-2xl sm:text-3xl font-semibold text-white tracking-tight">{title}</h1>
        {subtitle && <p className="text-sm text-ink-300 mt-1">{subtitle}</p>}
      </div>
      {right && <div>{right}</div>}
    </header>
  )
}

export function ConnectionBadge() {
  const { host, wsConnected, status } = useDevice()
  if (!host) return null
  return (
    <div className="flex items-center gap-2">
      <span className={`pill ${wsConnected ? 'pill-ok' : 'pill-muted'}`}>
        <Radio className="size-3.5" />
        {wsConnected ? 'Live' : 'HTTP only'}
      </span>
      <span className="pill pill-muted">
        {status?.net.hostname || host}
      </span>
    </div>
  )
}
