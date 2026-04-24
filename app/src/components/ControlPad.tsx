import { useHold } from '../hooks/useHold'
import { useControl } from '../hooks/useControl'
import { ArrowUp, ArrowDown, ArrowLeft, ArrowRight, StopIcon, AlertOctagon } from './Icons'
import type { ReactNode } from 'react'
import type { Command } from '../lib/types'

interface HoldBtnProps {
  action: Command
  releaseAction?: Command
  repeatMs?: number
  children: ReactNode
  className?: string
}

function HoldButton({ action, releaseAction, repeatMs = 150, children, className = '' }: HoldBtnProps) {
  const send = useControl()
  const handlers = useHold({
    onPress:   () => send(action),
    onRepeat:  () => send(action),
    onRelease: releaseAction ? () => send(releaseAction) : undefined,
    repeatMs,
  })
  return (
    <button
      {...handlers}
      className={
        `group relative grid place-items-center
         rounded-3xl h-24 sm:h-28
         bg-[#161619] ring-1 ring-[#26262e] text-ink-200
         transition-all duration-100
         hover:text-white hover:ring-white/20
         active:scale-[0.98]
         data-[held=true]:held
         ${className}`
      }
      data-held={undefined /* toggled via CSS .held utility; see useHold */}
    >
      {children}
    </button>
  )
}

export function ControlPad() {
  const send = useControl()
  return (
    <div className="card p-4 sm:p-6">
      {/* 3x3 pad — directional holds around a center stop */}
      <div className="grid grid-cols-3 gap-3 sm:gap-4 max-w-md mx-auto">
        <div />
        <HoldButton action="forward" releaseAction="stop"> <PadFace label="Forward" Icon={ArrowUp}/> </HoldButton>
        <div />

        <HoldButton action="left"  releaseAction="steer_stop" repeatMs={100}> <PadFace label="Left"  Icon={ArrowLeft}/>  </HoldButton>
        <CenterStop onStop={() => send('stop')} />
        <HoldButton action="right" releaseAction="steer_stop" repeatMs={100}> <PadFace label="Right" Icon={ArrowRight}/> </HoldButton>

        <div />
        <HoldButton action="reverse" releaseAction="stop"> <PadFace label="Reverse" Icon={ArrowDown}/> </HoldButton>
        <div />
      </div>

      <div className="flex items-center justify-between gap-3 mt-6">
        <button
          onClick={() => send('estop')}
          className="flex-1 h-14 rounded-2xl bg-rose-500/10 ring-1 ring-rose-500/30
                     text-rose-400 font-semibold tracking-tight
                     hover:bg-rose-500/15 hover:text-rose-300 active:scale-[0.98]
                     inline-flex items-center justify-center gap-2">
          <AlertOctagon className="size-5"/> E-STOP
        </button>
        <button
          onClick={() => send('clear_estop')}
          className="px-5 h-14 rounded-2xl bg-[#161619] ring-1 ring-[#26262e]
                     text-ink-300 font-medium
                     hover:text-white hover:ring-white/20 active:scale-[0.98]">
          Clear
        </button>
        <button
          onClick={() => send('brake')}
          className="px-5 h-14 rounded-2xl bg-[#161619] ring-1 ring-[#26262e]
                     text-ink-300 font-medium
                     hover:text-white hover:ring-white/20 active:scale-[0.98]">
          Brake
        </button>
      </div>
    </div>
  )
}

function PadFace({ label, Icon }: { label: string; Icon: (p: { className?: string }) => ReactNode }) {
  return (
    <div className="flex flex-col items-center gap-2">
      <Icon className="size-7 transition-transform group-active:scale-90"/>
      <span className="caption">{label}</span>
    </div>
  )
}

function CenterStop({ onStop }: { onStop: () => void }) {
  return (
    <button
      onClick={onStop}
      className="group grid place-items-center h-24 sm:h-28 rounded-3xl
                 bg-[#0f0f11] ring-1 ring-[#26262e] text-ink-300
                 hover:text-white hover:ring-white/20 active:scale-[0.98]
                 transition-all">
      <div className="flex flex-col items-center gap-1.5">
        <StopIcon className="size-6 text-rose-400 group-hover:text-rose-300"/>
        <span className="caption">Stop</span>
      </div>
    </button>
  )
}
