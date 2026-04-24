import {
  createContext,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type PropsWithChildren,
} from 'react'
import { ApiClient } from '../lib/api'
import { WsClient } from '../lib/ws'
import { probe } from '../lib/discovery'
import { forgetDevice, rememberDevice } from '../lib/storage'
import type { DeviceStatus } from '../lib/types'

interface DeviceContextValue {
  host: string | null
  status: DeviceStatus | null
  api: ApiClient | null
  ws: WsClient | null
  wsConnected: boolean
  connecting: boolean
  connect: (host: string) => Promise<boolean>
  disconnect: () => void
}

const DeviceContext = createContext<DeviceContextValue | null>(null)

export function DeviceProvider({ children }: PropsWithChildren) {
  const [host, setHost] = useState<string | null>(null)
  const [status, setStatus] = useState<DeviceStatus | null>(null)
  const [connecting, setConnecting] = useState(false)
  const [wsConnected, setWsConnected] = useState(false)
  const wsRef = useRef<WsClient | null>(null)

  const api = useMemo(() => (host ? new ApiClient(host) : null), [host])

  const connect = async (h: string): Promise<boolean> => {
    setConnecting(true)
    try {
      const probed = await probe(h, 3000)
      if (!probed) return false
      setHost(h)
      setStatus(probed.status)
      rememberDevice({
        host: h,
        label: probed.status.net.hostname || h,
      })
      // Fresh WS
      wsRef.current?.destroy()
      const w = new WsClient(h)
      w.onStatus(setWsConnected)
      w.connect()
      wsRef.current = w
      return true
    } finally {
      setConnecting(false)
    }
  }

  const disconnect = () => {
    wsRef.current?.destroy()
    wsRef.current = null
    setHost(null)
    setStatus(null)
    setWsConnected(false)
  }

  // Teardown on unmount
  useEffect(() => () => { wsRef.current?.destroy() }, [])

  const value: DeviceContextValue = {
    host,
    status,
    api,
    ws: wsRef.current,
    wsConnected,
    connecting,
    connect,
    disconnect,
  }

  return <DeviceContext.Provider value={value}>{children}</DeviceContext.Provider>
}

export function useDevice(): DeviceContextValue {
  const ctx = useContext(DeviceContext)
  if (!ctx) throw new Error('useDevice must be used inside <DeviceProvider>')
  return ctx
}

/** Small helper so pages can forget the current device in one click. */
export function useForgetCurrentDevice() {
  const { host, disconnect } = useDevice()
  return () => {
    if (host) forgetDevice(host)
    disconnect()
  }
}
