#include "network/NetworkManager.h"

#include <WiFi.h>

#include "config/Config.h"

namespace smartrc {

namespace {
constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 12000;
constexpr uint32_t RECONNECT_INTERVAL_MS  = 15000;
}  // namespace

void NetworkManager::begin(const Config& cfg) {
    cfg_      = &cfg;
    hostname_ = cfg.hostname.length() ? cfg.hostname : String("smartrc");

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(hostname_.c_str());

    if (cfg.wifiSsid.length() && tryConnectSta()) {
        mode_ = NetMode::Sta;
        Serial.printf("[net] STA connected: %s ip=%s rssi=%d\n",
                      cfg.wifiSsid.c_str(), WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        return;
    }
    Serial.println("[net] STA failed or unconfigured -> starting AP fallback");
    startAp();
}

bool NetworkManager::tryConnectSta() {
    WiFi.mode(WIFI_STA);

    if (!cfg_->useDhcp) {
        // Apply static config. Empty fields fall back to DHCP-style zeros.
        WiFi.config(cfg_->staticIp,
                    cfg_->staticGateway,
                    cfg_->staticSubnet,
                    cfg_->staticDns);
    } else {
        // Force DHCP — clear any previous static binding.
        WiFi.config(IPAddress(0, 0, 0, 0),
                    IPAddress(0, 0, 0, 0),
                    IPAddress(0, 0, 0, 0));
    }

    WiFi.begin(cfg_->wifiSsid.c_str(), cfg_->wifiPassword.c_str());

    const uint32_t start = millis();
    while (millis() - start < STA_CONNECT_TIMEOUT_MS) {
        if (WiFi.status() == WL_CONNECTED) return true;
        delay(200);
    }
    WiFi.disconnect(true, false);
    return false;
}

void NetworkManager::startAp() {
    WiFi.mode(WIFI_AP);
    const String& ssid = cfg_->apSsid;
    const String& pass = cfg_->apPassword;
    bool ok;
    if (pass.length() >= 8) {
        ok = WiFi.softAP(ssid.c_str(), pass.c_str());
    } else {
        ok = WiFi.softAP(ssid.c_str());  // open
    }
    mode_ = ok ? NetMode::Ap : NetMode::Down;
    if (ok) {
        Serial.printf("[net] AP up: ssid=%s ip=%s\n",
                      ssid.c_str(), WiFi.softAPIP().toString().c_str());
    }
}

void NetworkManager::update() {
    // Deferred AP teardown after a successful provision: fire once the
    // scheduled time arrives, regardless of current primary mode.
    if (apShutdownAtMs_ != 0 && (int32_t)(millis() - apShutdownAtMs_) >= 0) {
        apShutdownAtMs_ = 0;
        shutdownAp();
    }

    if (mode_ != NetMode::Sta) return;  // AP mode: nothing to babysit

    if (WiFi.status() == WL_CONNECTED) {
        reconnectAttempts_ = 0;
        return;
    }

    const uint32_t now = millis();
    if (now - lastReconnectMs_ < RECONNECT_INTERVAL_MS) return;
    lastReconnectMs_ = now;
    reconnectAttempts_++;
    Serial.printf("[net] STA dropped — reconnect attempt %u\n",
                  (unsigned)reconnectAttempts_);
    WiFi.disconnect();
    WiFi.begin(cfg_->wifiSsid.c_str(), cfg_->wifiPassword.c_str());
}

int NetworkManager::scanNetworks(const WifiScanCb& cb) {
    const wifi_mode_t prev = WiFi.getMode();

    // The STA half of the radio must be active for a scan. Keep the AP up.
    if (prev == WIFI_AP)                         WiFi.mode(WIFI_AP_STA);
    else if (prev == WIFI_OFF || prev == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);

    const int n = WiFi.scanNetworks(/*async*/false, /*show_hidden*/false);
    if (n > 0 && cb) {
        for (int i = 0; i < n; ++i) {
            cb(WiFi.SSID(i).c_str(),
               WiFi.RSSI(i),
               WiFi.encryptionType(i) != WIFI_AUTH_OPEN,
               WiFi.channel(i));
        }
    }
    WiFi.scanDelete();

    // Only downgrade if we explicitly upgraded AP → AP_STA. Leave any
    // pre-existing STA/AP_STA alone.
    if (prev == WIFI_AP) WiFi.mode(WIFI_AP);
    return n;
}

WifiTryResult NetworkManager::tryConnect(const String& ssid,
                                         const String& password,
                                         uint32_t      timeoutMs) {
    WifiTryResult r;
    if (ssid.length() == 0) { r.reason = "ssid empty"; return r; }

    const wifi_mode_t prev = WiFi.getMode();

    // If AP is up, keep it up by entering AP_STA; otherwise go STA-only.
    const bool hadAp = (prev == WIFI_AP || prev == WIFI_AP_STA);
    WiFi.mode(hadAp ? WIFI_AP_STA : WIFI_STA);

    // Clear any previous station config so stale statics don't poison DHCP.
    WiFi.config(IPAddress(0, 0, 0, 0),
                IPAddress(0, 0, 0, 0),
                IPAddress(0, 0, 0, 0));
    WiFi.disconnect(false, true);  // disconnect STA but keep WiFi on
    delay(80);

    Serial.printf("[net] trying STA: %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());

    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        const wl_status_t s = WiFi.status();
        if (s == WL_CONNECTED) {
            r.ok   = true;
            r.ip   = WiFi.localIP();
            r.rssi = WiFi.RSSI();
            // Reflect the new primary mode. AP stays up if we had it.
            mode_ = NetMode::Sta;
            Serial.printf("[net] try OK: ip=%s rssi=%d\n",
                          r.ip.toString().c_str(), (int)r.rssi);
            return r;
        }
        if (s == WL_NO_SSID_AVAIL) { r.reason = "ssid not found"; break; }
        if (s == WL_CONNECT_FAILED) { r.reason = "auth failed";   break; }
        delay(200);
    }
    if (r.reason.length() == 0) r.reason = "timeout";
    Serial.printf("[net] try FAILED: %s\n", r.reason.c_str());

    // Roll back: drop the half-open STA and restore the previous mode.
    WiFi.disconnect(true, false);
    if (hadAp) {
        WiFi.mode(WIFI_AP);
        mode_ = NetMode::Ap;
    } else {
        WiFi.mode(prev == WIFI_OFF ? WIFI_OFF : WIFI_STA);
    }
    return r;
}

void NetworkManager::requestApShutdown(uint32_t delayMs) {
    // Only meaningful if the AP half is actually up.
    const wifi_mode_t m = WiFi.getMode();
    if (m != WIFI_AP && m != WIFI_AP_STA) return;
    apShutdownAtMs_ = millis() + delayMs;
    if (apShutdownAtMs_ == 0) apShutdownAtMs_ = 1;  // guard against wrap sentinel
    Serial.printf("[net] AP shutdown scheduled in %u ms\n", (unsigned)delayMs);
}

void NetworkManager::shutdownAp() {
    const wifi_mode_t m = WiFi.getMode();
    if (m != WIFI_AP && m != WIFI_AP_STA) return;

    WiFi.softAPdisconnect(/*wifioff=*/false);
    // Drop AP half. If STA is connected we go pure STA, else we end up Down.
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
        mode_ = NetMode::Sta;
    } else {
        WiFi.mode(WIFI_OFF);
        mode_ = NetMode::Down;
    }
    Serial.println("[net] AP shut down");
}

bool NetworkManager::isConnected() const {
    if (mode_ == NetMode::Sta) return WiFi.status() == WL_CONNECTED;
    if (mode_ == NetMode::Ap)  return true;
    return false;
}

IPAddress NetworkManager::ip() const {
    if (mode_ == NetMode::Ap) return WiFi.softAPIP();
    return WiFi.localIP();
}

String NetworkManager::ssid() const {
    if (mode_ == NetMode::Ap) return cfg_ ? cfg_->apSsid : String();
    return WiFi.SSID();
}

int32_t NetworkManager::rssi() const {
    return mode_ == NetMode::Sta ? WiFi.RSSI() : 0;
}

}  // namespace smartrc
