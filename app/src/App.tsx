import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom'
import { DeviceProvider, useDevice } from './context/DeviceContext'
import { AppShell } from './components/Layout'
import DiscoverPage from './pages/DiscoverPage'
import DrivePage from './pages/DrivePage'
import MonitorPage from './pages/MonitorPage'
import ConfigPage from './pages/ConfigPage'
import SetupPage from './pages/SetupPage'
import StuntsPage from './pages/StuntsPage'
import type { PropsWithChildren } from 'react'

/** Routes that require an active device connection. */
function RequireDevice({ children }: PropsWithChildren) {
  const { host } = useDevice()
  if (!host) return <Navigate to="/" replace />
  return <>{children}</>
}

export default function App() {
  return (
    <DeviceProvider>
      <BrowserRouter>
        <Routes>
          {/* Discovery has no sidebar — it's the entry point. */}
          <Route path="/" element={<DiscoverPage />} />

          {/* Everything else lives inside the app shell. */}
          <Route element={<AppShell />}>
            <Route path="/drive"   element={<RequireDevice><DrivePage /></RequireDevice>} />
            <Route path="/monitor" element={<RequireDevice><MonitorPage /></RequireDevice>} />
            <Route path="/stunts"  element={<RequireDevice><StuntsPage /></RequireDevice>} />
            <Route path="/config"  element={<RequireDevice><ConfigPage /></RequireDevice>} />
            <Route path="/setup"   element={<RequireDevice><SetupPage /></RequireDevice>} />
          </Route>

          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </BrowserRouter>
    </DeviceProvider>
  )
}
