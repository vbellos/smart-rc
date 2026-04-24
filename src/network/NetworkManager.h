#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include <functional>

namespace smartrc {

struct Config;

enum class NetMode : uint8_t { Down, Sta, Ap };

// Result of a "try these credentials" attempt driven by the portal.
struct WifiTryResult {
    bool      ok = false;
    String    reason;   // populated on failure
    IPAddress ip;       // populated on success
    int32_t   rssi = 0; // populated on success
};

// Callback invoked once per scan entry. Hidden SSIDs are skipped by the caller.
using WifiScanCb = std::function<void(const char* ssid,
                                      int32_t     rssi,
                                      bool        secure,
                                      uint8_t     channel)>;

class NetworkManager {
public:
    void begin(const Config& cfg);

    // Tick from loop(). Drives reconnect attempts when in STA mode.
    void update();

    NetMode    mode()      const { return mode_; }
    IPAddress  ip()        const;
    String     ssid()      const;       // AP SSID when in AP mode, STA SSID otherwise
    bool       isConnected() const;
    int32_t    rssi()      const;
    String     hostname()  const { return hostname_; }

    // Scan visible APs. Switches the radio to AP_STA temporarily if currently
    // AP-only so the portal session stays alive during the scan. Returns the
    // number of networks found (or -1 on error). Blocks ~2-5 s.
    int scanNetworks(const WifiScanCb& cb);

    // Try the given credentials WITHOUT saving them. Keeps the AP up if we
    // started in AP mode, so the caller's HTTP session isn't severed. On
    // success the radio is left in STA (or AP_STA) and mode_ is updated so
    // subsequent /api/status reports reflect the new STA IP. On failure the
    // previous mode is restored.
    WifiTryResult tryConnect(const String& ssid,
                             const String& password,
                             uint32_t      timeoutMs = 12000);

    // Schedule the AP half of the radio to shut down after `delayMs`. The
    // delay exists so the HTTP response announcing "connected & saved"
    // flushes to the client before the hotspot disappears. Once down, the
    // radio is STA-only and mode_ reflects that.
    void requestApShutdown(uint32_t delayMs = 1500);

    // Tear down the softAP immediately. No-op if we're not running one.
    void shutdownAp();

private:
    bool tryConnectSta();
    void startAp();

    const Config* cfg_           = nullptr;
    NetMode       mode_          = NetMode::Down;
    String        hostname_;
    uint32_t      lastReconnectMs_ = 0;
    uint8_t       reconnectAttempts_ = 0;
    uint32_t      apShutdownAtMs_ = 0;   // 0 = no pending shutdown
};

}  // namespace smartrc
