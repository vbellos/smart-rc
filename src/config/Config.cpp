#include "config/Config.h"

#include <Preferences.h>

namespace smartrc {

namespace {
constexpr const char* NS = "smartrc";

// Defaults applied when no value is persisted.
constexpr const char* DEFAULT_AP_SSID  = "SmartRC-Setup";
constexpr const char* DEFAULT_AP_PASS  = "smart1234";  // >= 8 chars for WPA2
constexpr const char* DEFAULT_HOSTNAME = "smartrc";

uint32_t ipToU32(const IPAddress& ip) {
    return (uint32_t)ip;
}
IPAddress u32ToIp(uint32_t v) {
    return IPAddress(v);
}
}  // namespace

void loadConfig(Config& out) {
    Preferences p;
    p.begin(NS, /*readOnly=*/true);

    out.wifiSsid     = p.getString("wifi_ssid", "");
    out.wifiPassword = p.getString("wifi_pass", "");
    out.apSsid       = p.getString("ap_ssid",   DEFAULT_AP_SSID);
    out.apPassword   = p.getString("ap_pass",   DEFAULT_AP_PASS);
    out.hostname     = p.getString("hostname",  DEFAULT_HOSTNAME);

    out.useDhcp        = p.getBool("dhcp", true);
    out.staticIp       = u32ToIp(p.getUInt("ip",   0));
    out.staticGateway  = u32ToIp(p.getUInt("gw",   0));
    out.staticSubnet   = u32ToIp(p.getUInt("mask", 0xFFFFFF00U));  // /24
    out.staticDns      = u32ToIp(p.getUInt("dns",  0));

    out.steeringPulseMs    = p.getUShort("st_ms",   600);
    out.steeringCooldownMs = p.getUShort("st_cd",   120);
    out.defaultDrivePwm    = p.getUChar ("dr_pwm",  200);
    out.defaultSteerPwm    = p.getUChar ("st_pwm",  220);
    out.activeBrakePwm     = p.getUChar ("ab_pwm",  220);
    out.activeBrakeMaxMs   = p.getUShort("ab_max",  600);
    out.heartbeatTimeoutMs = p.getUShort("hb_ms",   1500);
    out.driveInverted      = p.getBool  ("dr_inv",  false);
    out.steerInverted      = p.getBool  ("st_inv",  false);
    out.apShutdownAfterProvision = p.getBool("ap_sdn", true);
    out.controlToken             = p.getString("ctl_tok", "");
    out.logLevel                 = p.getUChar("log_lvl", 3);
    out.imuInvertX               = p.getBool("imu_invx", false);
    out.imuInvertY               = p.getBool("imu_invy", false);
    out.imuInvertZ               = p.getBool("imu_invz", false);

    out.autoBrakeEnabled            = p.getBool  ("obr_en",     false);
    out.autoBrakeFrontBaseCm        = p.getUShort("obr_fbase",  20);
    out.autoBrakeFrontSlopeCmPerMs  = p.getUShort("obr_fslope", 30);
    out.autoBrakeFrontMinSpeedCmPs  = p.getUShort("obr_fminv",  10);
    out.autoBrakeRearBaseCm         = p.getUShort("obr_rbase",  20);
    out.autoBrakeRearSlopeCmPerMs   = p.getUShort("obr_rslope", 30);
    out.autoBrakeRearMinSpeedCmPs   = p.getUShort("obr_rminv",  10);

    p.end();
}

bool saveConfig(const Config& cfg) {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) return false;

    p.putString("wifi_ssid", cfg.wifiSsid);
    p.putString("wifi_pass", cfg.wifiPassword);
    p.putString("ap_ssid",   cfg.apSsid);
    p.putString("ap_pass",   cfg.apPassword);
    p.putString("hostname",  cfg.hostname);

    p.putBool ("dhcp", cfg.useDhcp);
    p.putUInt ("ip",   ipToU32(cfg.staticIp));
    p.putUInt ("gw",   ipToU32(cfg.staticGateway));
    p.putUInt ("mask", ipToU32(cfg.staticSubnet));
    p.putUInt ("dns",  ipToU32(cfg.staticDns));

    p.putUShort("st_ms",  cfg.steeringPulseMs);
    p.putUShort("st_cd",  cfg.steeringCooldownMs);
    p.putUChar ("dr_pwm", cfg.defaultDrivePwm);
    p.putUChar ("st_pwm", cfg.defaultSteerPwm);
    p.putUChar ("ab_pwm", cfg.activeBrakePwm);
    p.putUShort("ab_max", cfg.activeBrakeMaxMs);
    p.putUShort("hb_ms",  cfg.heartbeatTimeoutMs);
    p.putBool  ("dr_inv", cfg.driveInverted);
    p.putBool  ("st_inv", cfg.steerInverted);
    p.putBool  ("ap_sdn", cfg.apShutdownAfterProvision);
    p.putString("ctl_tok", cfg.controlToken);
    p.putUChar ("log_lvl", cfg.logLevel);
    p.putBool  ("imu_invx", cfg.imuInvertX);
    p.putBool  ("imu_invy", cfg.imuInvertY);
    p.putBool  ("imu_invz", cfg.imuInvertZ);

    p.putBool  ("obr_en",     cfg.autoBrakeEnabled);
    p.putUShort("obr_fbase",  cfg.autoBrakeFrontBaseCm);
    p.putUShort("obr_fslope", cfg.autoBrakeFrontSlopeCmPerMs);
    p.putUShort("obr_fminv",  cfg.autoBrakeFrontMinSpeedCmPs);
    p.putUShort("obr_rbase",  cfg.autoBrakeRearBaseCm);
    p.putUShort("obr_rslope", cfg.autoBrakeRearSlopeCmPerMs);
    p.putUShort("obr_rminv",  cfg.autoBrakeRearMinSpeedCmPs);

    p.end();
    return true;
}

void factoryReset() {
    Preferences p;
    p.begin(NS, /*readOnly=*/false);
    p.clear();
    p.end();
}

void networkReset() {
    Preferences p;
    p.begin(NS, /*readOnly=*/false);
    p.remove("wifi_ssid");
    p.remove("wifi_pass");
    p.remove("dhcp");
    p.remove("ip");
    p.remove("gw");
    p.remove("mask");
    p.remove("dns");
    p.end();
}

}  // namespace smartrc
