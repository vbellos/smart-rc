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

    out.stuntSpinTargetDeg   = p.getUShort("st_sp_deg",  340);
    out.stuntSpinTimeoutMs   = p.getUShort("st_sp_tout", 2500);
    out.stuntSpinPwm         = p.getUChar ("st_sp_pwm",  255);
    out.stuntJturnFwdMs      = p.getUShort("st_jt_fwd",  700);
    out.stuntJturnBrakeMs    = p.getUShort("st_jt_brk",  300);
    out.stuntJturnRevMs      = p.getUShort("st_jt_rev",  900);
    out.stuntJturnPwm        = p.getUChar ("st_jt_pwm",  255);
    out.stuntWiggleKickMs    = p.getUShort("st_wg_kick", 150);
    out.stuntWiggleHoldMs    = p.getUShort("st_wg_hold", 280);
    out.stuntWiggleCycles    = p.getUChar ("st_wg_cyc",  3);
    out.stuntWigglePwm       = p.getUChar ("st_wg_pwm",  255);
    out.stuntDriftFwd1Ms     = p.getUShort("st_dr_fwd",  300);
    out.stuntDriftLockMs     = p.getUShort("st_dr_lock", 400);
    out.stuntDriftCounterMs  = p.getUShort("st_dr_ctr",  300);
    out.stuntDriftPwm        = p.getUChar ("st_dr_pwm",  255);
    out.stuntPwrRevFwdMs     = p.getUShort("st_pr_fwd",  600);
    out.stuntPwrRevBrakeMs   = p.getUShort("st_pr_brk",  250);
    out.stuntPwrRevRevMs     = p.getUShort("st_pr_rev",  700);
    out.stuntPwrRevPwm       = p.getUChar ("st_pr_pwm",  255);

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

    p.putUShort("st_sp_deg",  cfg.stuntSpinTargetDeg);
    p.putUShort("st_sp_tout", cfg.stuntSpinTimeoutMs);
    p.putUChar ("st_sp_pwm",  cfg.stuntSpinPwm);
    p.putUShort("st_jt_fwd",  cfg.stuntJturnFwdMs);
    p.putUShort("st_jt_brk",  cfg.stuntJturnBrakeMs);
    p.putUShort("st_jt_rev",  cfg.stuntJturnRevMs);
    p.putUChar ("st_jt_pwm",  cfg.stuntJturnPwm);
    p.putUShort("st_wg_kick", cfg.stuntWiggleKickMs);
    p.putUShort("st_wg_hold", cfg.stuntWiggleHoldMs);
    p.putUChar ("st_wg_cyc",  cfg.stuntWiggleCycles);
    p.putUChar ("st_wg_pwm",  cfg.stuntWigglePwm);
    p.putUShort("st_dr_fwd",  cfg.stuntDriftFwd1Ms);
    p.putUShort("st_dr_lock", cfg.stuntDriftLockMs);
    p.putUShort("st_dr_ctr",  cfg.stuntDriftCounterMs);
    p.putUChar ("st_dr_pwm",  cfg.stuntDriftPwm);
    p.putUShort("st_pr_fwd",  cfg.stuntPwrRevFwdMs);
    p.putUShort("st_pr_brk",  cfg.stuntPwrRevBrakeMs);
    p.putUShort("st_pr_rev",  cfg.stuntPwrRevRevMs);
    p.putUChar ("st_pr_pwm",  cfg.stuntPwrRevPwm);

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
