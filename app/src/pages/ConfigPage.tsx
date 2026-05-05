import { useEffect, useState } from 'react'
import { useDevice } from '../context/DeviceContext'
import { PageHeader, ConnectionBadge } from '../components/Layout'
import type { DeviceConfig, WifiNetwork } from '../lib/types'
import { Lock, Wifi, Search, Check, Power } from '../components/Icons'

type Tab = 'motors' | 'stunts' | 'wifi' | 'system'

export default function ConfigPage() {
  const { api } = useDevice()
  const [cfg, setCfg] = useState<DeviceConfig | null>(null)
  const [tab, setTab] = useState<Tab>('motors')
  const [status, setStatus] = useState<string | null>(null)
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    if (!api) return
    api.getConfig().then(setCfg).catch(e => setStatus(String(e)))
  }, [api])

  async function save(partial: Partial<DeviceConfig>) {
    if (!api) return
    setSaving(true); setStatus(null)
    try {
      const res = await api.postConfig(partial)
      setStatus(res.message || 'Saved')
      // refresh
      const fresh = await api.getConfig()
      setCfg(fresh)
    } catch (e) {
      setStatus(`Save failed: ${e}`)
    } finally {
      setSaving(false)
    }
  }

  if (!cfg) {
    return (
      <div>
        <PageHeader title="Configuration" right={<ConnectionBadge/>}/>
        <div className="card p-10 text-center text-ink-400">Loading…</div>
      </div>
    )
  }

  return (
    <div>
      <PageHeader
        eyebrow="Settings"
        title="Configuration"
        subtitle="Everything persists to NVS on the device."
        right={<ConnectionBadge/>}
      />

      <Tabs tab={tab} onChange={setTab}/>

      {tab === 'motors' && <MotorsTab cfg={cfg} save={save} saving={saving}/>}
      {tab === 'stunts' && <StuntsTab cfg={cfg} save={save} saving={saving}/>}
      {tab === 'wifi'   && <WifiTab   cfg={cfg} save={save}/>}
      {tab === 'system' && <SystemTab cfg={cfg} save={save}/>}

      {status && (
        <div className="fixed bottom-6 right-6 card px-4 py-3 text-sm text-ink-200 animate-in">
          {status}
        </div>
      )}
    </div>
  )
}

function Tabs({ tab, onChange }: { tab: Tab; onChange: (t: Tab) => void }) {
  const items: { id: Tab; label: string }[] = [
    { id: 'motors', label: 'Motors & Safety' },
    { id: 'stunts', label: 'Stunts' },
    { id: 'wifi',   label: 'Wi-Fi & Network' },
    { id: 'system', label: 'System' },
  ]
  return (
    <div className="mb-6 inline-flex p-1 rounded-2xl bg-[#0f0f11] ring-1 ring-[#26262e]">
      {items.map(i => (
        <button key={i.id} onClick={() => onChange(i.id)}
          className={`px-4 py-2 text-sm rounded-xl transition-all
                     ${tab === i.id
                        ? 'bg-[#161619] text-white ring-1 ring-[#26262e] shadow'
                        : 'text-ink-300 hover:text-white'}`}>
          {i.label}
        </button>
      ))}
    </div>
  )
}

/* --------------------------------------------------------------------- */
/* Motors tab                                                             */
/* --------------------------------------------------------------------- */

function MotorsTab({ cfg, save, saving }: {
  cfg: DeviceConfig
  save: (c: Partial<DeviceConfig>) => Promise<void>
  saving: boolean
}) {
  const [local, setLocal] = useState<DeviceConfig>(cfg)
  useEffect(() => setLocal(cfg), [cfg])

  return (
    <div className="card p-6 space-y-6">
      <div className="grid gap-6 md:grid-cols-2">
        <SliderField
          label="Default drive PWM"
          value={local.defaultDrivePwm} min={0} max={255}
          onChange={(v) => setLocal({ ...local, defaultDrivePwm: v })}
        />
        <SliderField
          label="Default steering PWM"
          value={local.defaultSteerPwm} min={0} max={255}
          onChange={(v) => setLocal({ ...local, defaultSteerPwm: v })}
        />
        <SliderField
          label="Steering pulse (ms)"
          value={local.steeringPulseMs} min={100} max={1500} step={50}
          onChange={(v) => setLocal({ ...local, steeringPulseMs: v })}
          sub="Upper bound on continuous hold; JS keeps re-pinging."
        />
        <SliderField
          label="Heartbeat timeout (ms)"
          value={local.heartbeatTimeoutMs} min={100} max={5000} step={50}
          onChange={(v) => setLocal({ ...local, heartbeatTimeoutMs: v })}
          sub="Safety stops motors if no command within this window."
        />
      </div>

      <div className="grid gap-3 md:grid-cols-2">
        <ToggleField
          label="Invert drive direction"
          sub="Flip forward/reverse if wired backwards."
          checked={local.driveInverted}
          onChange={(b) => setLocal({ ...local, driveInverted: b })}
        />
        <ToggleField
          label="Invert steering direction"
          sub="Flip left/right if wired backwards."
          checked={local.steerInverted}
          onChange={(b) => setLocal({ ...local, steerInverted: b })}
        />
      </div>

      <div>
        <div className="caption mb-2">IMU mount orientation</div>
        <p className="text-xs text-ink-400 mb-3">
          Toggle these if the IMU board is mounted rotated/flipped.
          Diagnose via Setup → Forward pulse test (vx sign).
        </p>
        <div className="grid gap-3 md:grid-cols-3">
          <ToggleField
            label="Invert IMU X"
            sub="fwd/back axis — flip if forward drive gives negative vx"
            checked={local.imuInvertX}
            onChange={(b) => setLocal({ ...local, imuInvertX: b })}
          />
          <ToggleField
            label="Invert IMU Y"
            sub="left/right axis"
            checked={local.imuInvertY}
            onChange={(b) => setLocal({ ...local, imuInvertY: b })}
          />
          <ToggleField
            label="Invert IMU Z"
            sub="up/down axis"
            checked={local.imuInvertZ}
            onChange={(b) => setLocal({ ...local, imuInvertZ: b })}
          />
        </div>
      </div>

      <SaveBar
        saving={saving}
        onSave={() => save(diff(cfg, local))}
        onReset={() => setLocal(cfg)}
        dirty={JSON.stringify(cfg) !== JSON.stringify(local)}
      />
    </div>
  )
}

/* --------------------------------------------------------------------- */
/* Stunts tab — per-stunt timing & PWM tunables                           */
/* --------------------------------------------------------------------- */

function StuntsTab({ cfg, save, saving }: {
  cfg: DeviceConfig
  save: (c: Partial<DeviceConfig>) => Promise<void>
  saving: boolean
}) {
  const [local, setLocal] = useState<DeviceConfig>(cfg)
  useEffect(() => setLocal(cfg), [cfg])

  // One field setter factory keeps JSX tight.
  const set = <K extends keyof DeviceConfig>(k: K, v: DeviceConfig[K]) =>
    setLocal((s) => ({ ...s, [k]: v }))

  return (
    <div className="space-y-4">
      <StuntCard title="Spin (L/R)"
        blurb="Full lock + full throttle, stopped by gyro once the integrated yaw exceeds target. <360° undershoots, >360° overshoots to compensate for motor run-on.">
        <SliderField label="Target rotation (°)" value={local.stuntSpinTargetDeg}
          min={90} max={720} step={10}
          onChange={(v) => set('stuntSpinTargetDeg', v)}
          sub="360 = full circle. Tune down if the car consistently overshoots." />
        <SliderField label="Hard timeout (ms)" value={local.stuntSpinTimeoutMs}
          min={500} max={10000} step={100}
          onChange={(v) => set('stuntSpinTimeoutMs', v)}
          sub="Abort if the rotation target isn't reached within this window." />
        <SliderField label="Drive PWM" value={local.stuntSpinPwm}
          min={0} max={255}
          onChange={(v) => set('stuntSpinPwm', v)}
          sub="Higher = more spin torque; may overshoot on slippery floors." />
      </StuntCard>

      <StuntCard title="J-Turn (L/R)"
        blurb="Forward accel → lock steer → hard brake → reverse with wheels locked. The brake + lock combo swings the tail 180°.">
        <SliderField label="Forward duration (ms)" value={local.stuntJturnFwdMs}
          min={50} max={3000} step={50}
          onChange={(v) => set('stuntJturnFwdMs', v)}
          sub="Longer = more entry speed = bigger tail swing." />
        <SliderField label="Brake duration (ms)" value={local.stuntJturnBrakeMs}
          min={50} max={1500} step={50}
          onChange={(v) => set('stuntJturnBrakeMs', v)}
          sub="Short and hard works best; active-brake terminates early on stop." />
        <SliderField label="Reverse duration (ms)" value={local.stuntJturnRevMs}
          min={50} max={3000} step={50}
          onChange={(v) => set('stuntJturnRevMs', v)}
          sub="How long to reverse with wheels locked. Tune until you end ~180°." />
        <SliderField label="Drive PWM" value={local.stuntJturnPwm}
          min={0} max={255}
          onChange={(v) => set('stuntJturnPwm', v)} />
      </StuntCard>

      <StuntCard title="Wiggle"
        blurb="Forward kick then N full-swing L/R steering cycles. Drive stays on throughout.">
        <SliderField label="Forward kick (ms)" value={local.stuntWiggleKickMs}
          min={50} max={1000} step={25}
          onChange={(v) => set('stuntWiggleKickMs', v)}
          sub="Breaks static friction before the wiggle starts." />
        <SliderField label="Steer hold per flick (ms)" value={local.stuntWiggleHoldMs}
          min={50} max={800} step={20}
          onChange={(v) => set('stuntWiggleHoldMs', v)}
          sub="How long each side is held. Shorter = faster wiggle." />
        <SliderField label="Number of L-R cycles" value={local.stuntWiggleCycles}
          min={1} max={8}
          onChange={(v) => set('stuntWiggleCycles', v)}
          sub="Each cycle = one left + one right." />
        <SliderField label="Drive PWM" value={local.stuntWigglePwm}
          min={0} max={255}
          onChange={(v) => set('stuntWigglePwm', v)} />
      </StuntCard>

      <StuntCard title="Drift (L/R)"
        blurb="Entry throttle → hard lock → sustained throttle → counter-steer. Real drift needs a slippery floor; on carpet it becomes a tight aggressive turn."
        experimental>
        <SliderField label="Pre-lock forward (ms)" value={local.stuntDriftFwd1Ms}
          min={50} max={2000} step={25}
          onChange={(v) => set('stuntDriftFwd1Ms', v)}
          sub="Entry speed before the hard lock." />
        <SliderField label="Primary lock hold (ms)" value={local.stuntDriftLockMs}
          min={50} max={2000} step={25}
          onChange={(v) => set('stuntDriftLockMs', v)}
          sub="Sustained-throttle-while-locked phase. Biggest effect." />
        <SliderField label="Counter-steer (ms)" value={local.stuntDriftCounterMs}
          min={50} max={1500} step={25}
          onChange={(v) => set('stuntDriftCounterMs', v)}
          sub="How long to hold the opposite lock to maintain the slide." />
        <SliderField label="Drive PWM" value={local.stuntDriftPwm}
          min={0} max={255}
          onChange={(v) => set('stuntDriftPwm', v)} />
      </StuntCard>

      <StuntCard title="Power Reverse"
        blurb="Forward accel → hard brake → full reverse. A showy direction-flip.">
        <SliderField label="Forward duration (ms)" value={local.stuntPwrRevFwdMs}
          min={50} max={2500} step={25}
          onChange={(v) => set('stuntPwrRevFwdMs', v)} />
        <SliderField label="Brake duration (ms)" value={local.stuntPwrRevBrakeMs}
          min={50} max={1500} step={25}
          onChange={(v) => set('stuntPwrRevBrakeMs', v)} />
        <SliderField label="Reverse duration (ms)" value={local.stuntPwrRevRevMs}
          min={50} max={2500} step={25}
          onChange={(v) => set('stuntPwrRevRevMs', v)} />
        <SliderField label="Drive PWM" value={local.stuntPwrRevPwm}
          min={0} max={255}
          onChange={(v) => set('stuntPwrRevPwm', v)} />
      </StuntCard>

      <div className="sticky bottom-4 z-10">
        <div className="card p-3 flex items-center justify-end gap-2 ring-white/10">
          <span className="text-xs text-ink-400 mr-auto">
            Edits apply to the <em>next</em> stunt run.
          </span>
          <button onClick={() => setLocal(cfg)} disabled={!isDirty(cfg, local) || saving}
            className="px-4 h-10 rounded-xl bg-[#0f0f11] ring-1 ring-[#26262e]
                       text-ink-300 hover:text-white disabled:opacity-50">
            Reset
          </button>
          <button onClick={() => save(diff(cfg, local))}
            disabled={!isDirty(cfg, local) || saving}
            className="px-5 h-10 rounded-xl bg-lime-500 text-ink-950 font-semibold
                       hover:bg-lime-400 disabled:opacity-50 active:scale-[0.98]">
            {saving ? 'Saving…' : 'Save changes'}
          </button>
        </div>
      </div>
    </div>
  )
}

function StuntCard({ title, blurb, experimental, children }: {
  title: string
  blurb: string
  experimental?: boolean
  children: React.ReactNode
}) {
  return (
    <div className="card p-6">
      <div className="flex items-baseline justify-between mb-1">
        <h3 className="text-white font-medium">{title}</h3>
        {experimental && <span className="pill pill-muted">experimental</span>}
      </div>
      <p className="text-xs text-ink-400 mb-4">{blurb}</p>
      <div className="grid gap-4 md:grid-cols-2">
        {children}
      </div>
    </div>
  )
}

function isDirty(a: DeviceConfig, b: DeviceConfig): boolean {
  return JSON.stringify(a) !== JSON.stringify(b)
}

/* --------------------------------------------------------------------- */
/* Wi-Fi tab                                                              */
/* --------------------------------------------------------------------- */

function WifiTab({ cfg, save }: {
  cfg: DeviceConfig
  save: (c: Partial<DeviceConfig>) => Promise<void>
}) {
  const { api } = useDevice()
  const [scanning, setScanning] = useState(false)
  const [nets, setNets] = useState<WifiNetwork[]>([])
  const [ssid, setSsid] = useState('')
  const [pass, setPass] = useState('')
  const [result, setResult] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  async function scan() {
    if (!api) return
    setScanning(true)
    try {
      const res = await api.wifiScan()
      const seen = new Set<string>()
      setNets(res.networks
        .filter(n => n.ssid && !seen.has(n.ssid) && (seen.add(n.ssid), true))
        .sort((a, b) => b.rssi - a.rssi))
    } finally {
      setScanning(false)
    }
  }

  async function provision() {
    if (!api || !ssid) return
    setBusy(true); setResult('Connecting…')
    try {
      const r = await api.wifiProvision(ssid, pass)
      if (r.ok) setResult(`✓ ${r.message || 'Connected'} · ip=${r.ip}`)
      else setResult(`✗ ${r.reason || 'Failed'}`)
    } catch (e) {
      setResult(`✗ ${e}`)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="grid gap-4">
      <div className="card p-6">
        <div className="flex items-center justify-between mb-4">
          <div>
            <h3 className="text-white font-medium">Provision new network</h3>
            <p className="text-xs text-ink-400 mt-0.5">
              Try the creds first — only saved on verified connection.
            </p>
          </div>
          <button onClick={scan}
            disabled={scanning}
            className="pill pill-muted hover:text-white disabled:opacity-50">
            <Search className="size-3.5"/> {scanning ? 'Scanning…' : 'Scan'}
          </button>
        </div>

        {nets.length > 0 && (
          <div className="mb-4 max-h-60 overflow-y-auto rounded-xl ring-1 ring-[#26262e] divide-y divide-[#1c1c22]">
            {nets.map(n => (
              <button key={n.ssid}
                onClick={() => setSsid(n.ssid)}
                className={`w-full text-left px-4 py-2.5 flex items-center gap-3 hover:bg-white/5
                           ${ssid === n.ssid ? 'bg-lime-500/5' : ''}`}>
                {n.secure ? <Lock className="size-3.5 text-ink-400"/> : <Wifi className="size-3.5 text-ink-400"/>}
                <span className="text-sm text-white flex-1 truncate">{n.ssid}</span>
                <SignalBars rssi={n.rssi}/>
                <span className="text-xs text-ink-400 tabular-nums">{n.rssi} dBm</span>
              </button>
            ))}
          </div>
        )}

        <div className="grid gap-3">
          <TextField label="SSID"     value={ssid} onChange={setSsid} />
          <TextField label="Password" value={pass} onChange={setPass} type="password" />
          <div className="flex gap-2">
            <button onClick={provision}
              disabled={!ssid || busy}
              className="px-5 h-11 rounded-xl bg-lime-500 text-ink-950 font-semibold
                         hover:bg-lime-400 active:scale-[0.98] disabled:opacity-50">
              Connect &amp; save
            </button>
            {result && <span className="self-center text-sm text-ink-300">{result}</span>}
          </div>
        </div>
      </div>

      <div className="card p-6">
        <h3 className="text-white font-medium mb-4">Hotspot &amp; host</h3>
        <div className="grid gap-3 md:grid-cols-2">
          <TextField label="Hostname (mDNS)" value={cfg.hostname}
            onChange={v => save({ hostname: v })} />
          <TextField label="AP SSID" value={cfg.apSsid}
            onChange={v => save({ apSsid: v })} />
          <TextField label="AP Password" value={cfg.apPassword} type="password"
            onChange={v => save({ apPassword: v })} />
          <ToggleField
            label="Close hotspot after provision"
            sub="AP shuts down ~1.5s after a successful Wi-Fi save."
            checked={cfg.apShutdownAfterProvision}
            onChange={b => save({ apShutdownAfterProvision: b })}
          />
        </div>
      </div>
    </div>
  )
}

/* --------------------------------------------------------------------- */
/* System tab                                                             */
/* --------------------------------------------------------------------- */

function SystemTab({ cfg, save }: {
  cfg: DeviceConfig
  save: (c: Partial<DeviceConfig>) => Promise<void>
}) {
  const { api, disconnect } = useDevice()
  const [token, setToken] = useState(cfg.controlToken)
  useEffect(() => setToken(cfg.controlToken), [cfg.controlToken])

  return (
    <div className="grid gap-4">
      <div className="card p-6 space-y-4">
        <h3 className="text-white font-medium">Security</h3>
        <TextField label="WebSocket control token (empty = auth off)"
          value={token} onChange={setToken} type="password"/>
        <div className="flex gap-2">
          <button onClick={() => save({ controlToken: token })}
            className="px-4 h-10 rounded-xl bg-lime-500 text-ink-950 font-semibold
                       hover:bg-lime-400 active:scale-[0.98]">
            <Check className="size-4 inline -mt-0.5 mr-1"/> Save token
          </button>
          <button onClick={() => { setToken(''); save({ controlToken: '' }) }}
            className="px-4 h-10 rounded-xl bg-[#0f0f11] ring-1 ring-[#26262e] text-ink-300 hover:text-white">
            Clear
          </button>
        </div>
      </div>

      <div className="card p-6">
        <h3 className="text-white font-medium mb-4">Log verbosity</h3>
        <div className="flex gap-2 flex-wrap">
          {[0, 1, 2, 3, 4].map(lvl => (
            <button key={lvl}
              onClick={() => save({ logLevel: lvl })}
              className={`px-4 h-9 rounded-lg text-sm font-medium ring-1
                         ${cfg.logLevel === lvl
                            ? 'bg-lime-500/15 text-lime-400 ring-lime-500/30'
                            : 'bg-[#0f0f11] text-ink-300 ring-[#26262e] hover:text-white'}`}>
              {['off','error','warn','info','debug'][lvl]}
            </button>
          ))}
        </div>
      </div>

      <div className="card p-6">
        <h3 className="text-white font-medium mb-4">Danger zone</h3>
        <div className="grid gap-3 md:grid-cols-3">
          <DangerAction label="Reboot" sub="Soft-reset only. Keeps all config."
            onClick={async () => { await api?.reboot(); disconnect() }}/>
          <DangerAction label="Reset Wi-Fi" sub="Wipes SSID/password/IP. Keeps motor tuning."
            onClick={async () => { await api?.resetNetwork() }}/>
          <DangerAction label="Factory reset" sub="Wipes ALL NVS. Reboots to AP defaults."
            onClick={async () => { await api?.resetFactory(); disconnect() }}
            danger/>
        </div>
      </div>
    </div>
  )
}

/* --------------------------------------------------------------------- */
/* Shared form bits                                                       */
/* --------------------------------------------------------------------- */

function SliderField({
  label, value, min, max, step = 1, sub, onChange,
}: {
  label: string
  value: number
  min: number
  max: number
  step?: number
  sub?: string
  onChange: (v: number) => void
}) {
  return (
    <label className="block">
      <div className="flex items-baseline justify-between mb-1.5">
        <span className="text-sm text-ink-200">{label}</span>
        <span className="text-sm tabular-nums text-white">{value}</span>
      </div>
      <input
        type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange(parseInt(e.target.value))}
        className="w-full accent-lime-400"
      />
      {sub && <p className="text-xs text-ink-400 mt-1">{sub}</p>}
    </label>
  )
}

function ToggleField({ label, sub, checked, onChange }: {
  label: string
  sub?: string
  checked: boolean
  onChange: (b: boolean) => void
}) {
  return (
    <label className="flex items-start justify-between gap-3 p-3 rounded-xl ring-1 ring-[#26262e] bg-[#0f0f11] cursor-pointer">
      <div className="flex-1">
        <div className="text-sm text-white">{label}</div>
        {sub && <div className="text-xs text-ink-400 mt-0.5">{sub}</div>}
      </div>
      <button type="button" onClick={() => onChange(!checked)}
        className={`shrink-0 w-11 h-6 rounded-full transition-all ring-1
                   ${checked ? 'bg-lime-500 ring-lime-400' : 'bg-[#26262e] ring-[#3a3a44]'}`}>
        <span className={`block size-5 bg-white rounded-full shadow transition-transform
                         ${checked ? 'translate-x-[22px]' : 'translate-x-[2px]'}
                         mt-[1px]`}/>
      </button>
    </label>
  )
}

function TextField({ label, value, onChange, type = 'text' }: {
  label: string
  value: string
  onChange: (v: string) => void
  type?: 'text' | 'password'
}) {
  return (
    <label className="block">
      <div className="text-xs text-ink-400 mb-1.5">{label}</div>
      <input
        type={type}
        value={value}
        onChange={e => onChange(e.target.value)}
        autoCapitalize="none" autoCorrect="off"
        className="w-full bg-[#0f0f11] ring-1 ring-[#26262e] rounded-xl
                   px-3 py-2.5 text-sm text-white outline-none
                   focus:ring-white/30 placeholder:text-ink-500"
      />
    </label>
  )
}

function SaveBar({ saving, onSave, onReset, dirty }: {
  saving: boolean
  dirty: boolean
  onSave: () => void
  onReset: () => void
}) {
  return (
    <div className="flex items-center justify-end gap-2 pt-2 border-t border-[#1c1c22]">
      <button onClick={onReset} disabled={!dirty || saving}
        className="px-4 h-10 rounded-xl bg-[#0f0f11] ring-1 ring-[#26262e] text-ink-300 hover:text-white disabled:opacity-50">
        Reset
      </button>
      <button onClick={onSave} disabled={!dirty || saving}
        className="px-5 h-10 rounded-xl bg-lime-500 text-ink-950 font-semibold hover:bg-lime-400 disabled:opacity-50 active:scale-[0.98]">
        {saving ? 'Saving…' : 'Save changes'}
      </button>
    </div>
  )
}

function DangerAction({ label, sub, onClick, danger }: {
  label: string; sub: string; onClick: () => void; danger?: boolean
}) {
  return (
    <button onClick={() => confirm(`${label}?`) && onClick()}
      className={`text-left p-4 rounded-xl ring-1 transition-all
                  ${danger
                    ? 'bg-rose-500/5 ring-rose-500/25 hover:bg-rose-500/10 text-rose-300'
                    : 'bg-[#0f0f11] ring-[#26262e] text-ink-200 hover:ring-white/20 hover:text-white'}`}>
      <div className="flex items-center gap-2 font-medium"><Power className="size-4"/> {label}</div>
      <div className="text-xs text-ink-400 mt-1">{sub}</div>
    </button>
  )
}

function SignalBars({ rssi }: { rssi: number }) {
  const strength = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1
  return (
    <div className="flex items-end gap-[2px] h-4">
      {[1,2,3,4].map(i => (
        <span key={i}
          className={`w-[3px] rounded-sm ${i <= strength ? 'bg-lime-400' : 'bg-ink-600'}`}
          style={{ height: `${i * 25}%` }}/>
      ))}
    </div>
  )
}

function diff(a: DeviceConfig, b: DeviceConfig): Partial<DeviceConfig> {
  const out: Partial<DeviceConfig> = {}
  ;(Object.keys(b) as (keyof DeviceConfig)[]).forEach(k => {
    if (a[k] !== b[k]) (out as Record<string, unknown>)[k] = b[k]
  })
  return out
}
