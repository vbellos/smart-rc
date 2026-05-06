#include "portal/Portal.h"

#include <AsyncJson.h>
#include <ArduinoJson.h>

#include "config/Config.h"
#include "control/AutoBrake.h"
#include "control/CommandHandler.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/Steering.h"
#include "network/NetworkManager.h"
#include "portal/PortalHtml.h"
#include "sensors/Sensors.h"

namespace smartrc {

namespace {
constexpr const char* JSON_MIME = "application/json";

// Serialize a JsonDocument straight into an Async streaming response.
// Avoids the intermediate String buffer the sync WebServer forced on us.
void sendJsonDoc(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
    auto* rs = req->beginResponseStream(JSON_MIME);
    rs->setCode(code);
    serializeJson(doc, *rs);
    req->send(rs);
}

bool parseIp(const String& s, IPAddress& out) {
    if (s.length() == 0) { out = IPAddress(0, 0, 0, 0); return true; }
    return out.fromString(s);
}
}  // namespace

void Portal::begin(const PortalDeps& deps) {
    deps_ = deps;
    // CORS: same-origin is fine for the built-in portal, but a
    // mobile/web app hosted elsewhere needs these headers on every
    // /api/* response. Install them globally before routes register.
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
    registerRoutes();
    server_.begin();
}

void Portal::handle() {
    // AsyncWebServer runs in its own task — nothing to pump here. Function
    // retained so main.cpp's loop() doesn't need a version-specific diff.
}

void Portal::registerRoutes() {
    // ---- GET / body-less POST ---------------------------------------------
    server_.on("/",                  HTTP_GET,
               [this](AsyncWebServerRequest* r) { handleRoot(r); });
    server_.on("/api/status",        HTTP_GET,
               [this](AsyncWebServerRequest* r) { handleStatus(r); });
    server_.on("/api/config",        HTTP_GET,
               [this](AsyncWebServerRequest* r) { handleGetConfig(r); });
    server_.on("/api/wifi/scan",     HTTP_GET,
               [this](AsyncWebServerRequest* r) { handleWifiScan(r); });
    server_.on("/api/schema",        HTTP_GET,
               [this](AsyncWebServerRequest* r) { handleSchema(r); });

    // CORS preflight — browsers send this before any non-"simple" POST
    // (our application/json POSTs all qualify). Return the headers and a
    // 204 so the real request proceeds.
    server_.on("/api/*",             HTTP_OPTIONS,
               [](AsyncWebServerRequest* r) { r->send(204); });

    server_.on("/api/reboot",        HTTP_POST,
               [this](AsyncWebServerRequest* r) { handleReboot(r); });
    server_.on("/api/reset/network", HTTP_POST,
               [this](AsyncWebServerRequest* r) { handleResetNetwork(r); });
    server_.on("/api/reset/factory", HTTP_POST,
               [this](AsyncWebServerRequest* r) { handleResetFactory(r); });
    server_.on("/api/imu/calibrate", HTTP_POST,
               [this](AsyncWebServerRequest* r) { handleImuCalibrate(r); });

    // ---- JSON-body POSTs ---------------------------------------------------
    // AsyncCallbackJsonWebHandler buffers the chunked body, parses once, and
    // hands us a JsonVariant. Replaces the sync `server.arg("plain")` dance.
    auto* postConfig = new AsyncCallbackJsonWebHandler("/api/config",
        [this](AsyncWebServerRequest* r, JsonVariant& j) { handlePostConfig(r, j); });
    postConfig->setMaxContentLength(4096);
    server_.addHandler(postConfig);

    auto* postControl = new AsyncCallbackJsonWebHandler("/api/control",
        [this](AsyncWebServerRequest* r, JsonVariant& j) { handleControl(r, j); });
    postControl->setMaxContentLength(512);
    server_.addHandler(postControl);

    auto* postProv = new AsyncCallbackJsonWebHandler("/api/wifi/provision",
        [this](AsyncWebServerRequest* r, JsonVariant& j) { handleWifiProvision(r, j); });
    postProv->setMaxContentLength(512);
    server_.addHandler(postProv);

    server_.onNotFound([this](AsyncWebServerRequest* r) { handleNotFound(r); });
}

// ---------------------------------------------------------------------------
// Handlers. JSON shapes are byte-for-byte identical to the pre-async version.
// ---------------------------------------------------------------------------

void Portal::handleRoot(AsyncWebServerRequest* req) {
    // AsyncWebServer ≥3.x deprecated send_P; plain send() accepts a PROGMEM
    // `const char*` directly and streams it from flash on ESP32.
    req->send(200, "text/html", PORTAL_HTML);
}

void Portal::handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["uptime_ms"]  = millis();
    doc["heap_free"]  = ESP.getFreeHeap();

    auto net = doc["net"].to<JsonObject>();
    const char* m = "down";
    if (deps_.network->mode() == NetMode::Sta) m = "sta";
    else if (deps_.network->mode() == NetMode::Ap) m = "ap";
    net["mode"]      = m;
    net["ip"]        = deps_.network->ip().toString();
    net["ssid"]      = deps_.network->ssid();
    net["rssi"]      = deps_.network->rssi();
    net["hostname"]  = deps_.network->hostname();
    net["connected"] = deps_.network->isConnected();

    auto motors = doc["motors"].to<JsonObject>();
    motors["drive_moving"]         = deps_.drive->isMoving();
    motors["drive_active_braking"] = deps_.drive->isActiveBraking();
    motors["steer_state"]          = (int)deps_.steering->state();
    motors["steer_last_dir"]       = (int)deps_.steering->lastDirection();

    auto safety = doc["safety"].to<JsonObject>();
    safety["emergency"] = deps_.safety->isEmergency();
    safety["stale"]     = deps_.safety->isStale();

    if (deps_.autoBrake) {
        auto ab = doc["auto_brake"].to<JsonObject>();
        ab["enabled"] = deps_.autoBrake->enabled();
        ab["engaged"] = deps_.autoBrake->engaged();

        using AB = AutoBrake;
        auto sideJson = [&](AB::Side side, JsonObject obj, bool active) {
            obj["active"]     = active;
            obj["bypassed"]   = deps_.autoBrake->bypassed(side);
            obj["trigger_cm"] = deps_.autoBrake->triggerCm(side);
            const uint16_t d  = deps_.autoBrake->distanceCm(side);
            if (d == 0xFFFF) obj["distance_cm"] = nullptr;
            else             obj["distance_cm"] = d;
        };
        sideJson(AB::Front, ab["front"].to<JsonObject>(),
                 deps_.autoBrake->activeFront());
        sideJson(AB::Rear,  ab["rear"].to<JsonObject>(),
                 deps_.autoBrake->activeRear());
    }

    // Let every wired sensor contribute under `sensors.*`.
    sensors::appendSensorsJson(doc.as<JsonObject>());

    sendJsonDoc(req, 200, doc);
}

void Portal::handleGetConfig(AsyncWebServerRequest* req) {
    const Config& c = *deps_.config;
    JsonDocument doc;
    doc["wifiSsid"]           = c.wifiSsid;
    doc["wifiPassword"]       = c.wifiPassword;  // present so the form can show it
    doc["apSsid"]             = c.apSsid;
    doc["apPassword"]         = c.apPassword;
    doc["hostname"]           = c.hostname;
    doc["useDhcp"]            = c.useDhcp;
    doc["staticIp"]           = c.staticIp.toString();
    doc["staticGateway"]      = c.staticGateway.toString();
    doc["staticSubnet"]       = c.staticSubnet.toString();
    doc["staticDns"]          = c.staticDns.toString();
    doc["steeringPulseMs"]    = c.steeringPulseMs;
    doc["steeringCooldownMs"] = c.steeringCooldownMs;
    doc["defaultDrivePwm"]    = c.defaultDrivePwm;
    doc["defaultSteerPwm"]    = c.defaultSteerPwm;
    doc["activeBrakePwm"]     = c.activeBrakePwm;
    doc["activeBrakeMaxMs"]   = c.activeBrakeMaxMs;
    doc["heartbeatTimeoutMs"] = c.heartbeatTimeoutMs;
    doc["driveInverted"]      = c.driveInverted;
    doc["steerInverted"]      = c.steerInverted;
    doc["apShutdownAfterProvision"] = c.apShutdownAfterProvision;
    doc["controlToken"]       = c.controlToken;
    doc["logLevel"]           = c.logLevel;
    doc["imuInvertX"]         = c.imuInvertX;
    doc["imuInvertY"]         = c.imuInvertY;
    doc["imuInvertZ"]         = c.imuInvertZ;
    doc["autoBrakeEnabled"]           = c.autoBrakeEnabled;
    doc["autoBrakeFrontBaseCm"]       = c.autoBrakeFrontBaseCm;
    doc["autoBrakeFrontSlopeCmPerMs"] = c.autoBrakeFrontSlopeCmPerMs;
    doc["autoBrakeFrontMinSpeedCmPs"] = c.autoBrakeFrontMinSpeedCmPs;
    doc["autoBrakeRearBaseCm"]        = c.autoBrakeRearBaseCm;
    doc["autoBrakeRearSlopeCmPerMs"]  = c.autoBrakeRearSlopeCmPerMs;
    doc["autoBrakeRearMinSpeedCmPs"]  = c.autoBrakeRearMinSpeedCmPs;
    sendJsonDoc(req, 200, doc);
}

void Portal::handlePostConfig(AsyncWebServerRequest* req, JsonVariant& body) {
    Config& c = *deps_.config;
    if (body["wifiSsid"].is<const char*>())     c.wifiSsid     = body["wifiSsid"].as<const char*>();
    if (body["wifiPassword"].is<const char*>()) c.wifiPassword = body["wifiPassword"].as<const char*>();
    if (body["apSsid"].is<const char*>())       c.apSsid       = body["apSsid"].as<const char*>();
    if (body["apPassword"].is<const char*>())   c.apPassword   = body["apPassword"].as<const char*>();
    if (body["hostname"].is<const char*>())     c.hostname     = body["hostname"].as<const char*>();
    if (body["useDhcp"].is<bool>())             c.useDhcp      = body["useDhcp"].as<bool>();

    if (body["staticIp"].is<const char*>())      parseIp(body["staticIp"].as<const char*>(),      c.staticIp);
    if (body["staticGateway"].is<const char*>()) parseIp(body["staticGateway"].as<const char*>(), c.staticGateway);
    if (body["staticSubnet"].is<const char*>())  parseIp(body["staticSubnet"].as<const char*>(),  c.staticSubnet);
    if (body["staticDns"].is<const char*>())     parseIp(body["staticDns"].as<const char*>(),     c.staticDns);

    if (body["steeringPulseMs"].is<int>())    c.steeringPulseMs    = body["steeringPulseMs"];
    if (body["steeringCooldownMs"].is<int>()) c.steeringCooldownMs = body["steeringCooldownMs"];
    if (body["defaultDrivePwm"].is<int>())    c.defaultDrivePwm    = body["defaultDrivePwm"];
    if (body["defaultSteerPwm"].is<int>())    c.defaultSteerPwm    = body["defaultSteerPwm"];
    if (body["activeBrakePwm"].is<int>())     c.activeBrakePwm     = (uint8_t)constrain((int)body["activeBrakePwm"], 0, 255);
    if (body["activeBrakeMaxMs"].is<int>())   c.activeBrakeMaxMs   = (uint16_t)constrain((int)body["activeBrakeMaxMs"], 100, 5000);
    if (body["heartbeatTimeoutMs"].is<int>()) c.heartbeatTimeoutMs = body["heartbeatTimeoutMs"];
    if (body["driveInverted"].is<bool>())     c.driveInverted      = body["driveInverted"].as<bool>();
    if (body["steerInverted"].is<bool>())     c.steerInverted      = body["steerInverted"].as<bool>();
    if (body["apShutdownAfterProvision"].is<bool>())
        c.apShutdownAfterProvision = body["apShutdownAfterProvision"].as<bool>();
    if (body["controlToken"].is<const char*>()) c.controlToken = body["controlToken"].as<const char*>();
    if (body["logLevel"].is<int>()) c.logLevel = (uint8_t)constrain((int)body["logLevel"], 0, 4);
    if (body["imuInvertX"].is<bool>()) c.imuInvertX = body["imuInvertX"].as<bool>();
    if (body["imuInvertY"].is<bool>()) c.imuInvertY = body["imuInvertY"].as<bool>();
    if (body["imuInvertZ"].is<bool>()) c.imuInvertZ = body["imuInvertZ"].as<bool>();

    if (body["autoBrakeEnabled"].is<bool>())
        c.autoBrakeEnabled = body["autoBrakeEnabled"].as<bool>();
    if (body["autoBrakeFrontBaseCm"].is<int>())
        c.autoBrakeFrontBaseCm = (uint16_t)constrain((int)body["autoBrakeFrontBaseCm"], 0, 1000);
    if (body["autoBrakeFrontSlopeCmPerMs"].is<int>())
        c.autoBrakeFrontSlopeCmPerMs = (uint16_t)constrain((int)body["autoBrakeFrontSlopeCmPerMs"], 0, 500);
    if (body["autoBrakeFrontMinSpeedCmPs"].is<int>())
        c.autoBrakeFrontMinSpeedCmPs = (uint16_t)constrain((int)body["autoBrakeFrontMinSpeedCmPs"], 0, 500);
    if (body["autoBrakeRearBaseCm"].is<int>())
        c.autoBrakeRearBaseCm = (uint16_t)constrain((int)body["autoBrakeRearBaseCm"], 0, 1000);
    if (body["autoBrakeRearSlopeCmPerMs"].is<int>())
        c.autoBrakeRearSlopeCmPerMs = (uint16_t)constrain((int)body["autoBrakeRearSlopeCmPerMs"], 0, 500);
    if (body["autoBrakeRearMinSpeedCmPs"].is<int>())
        c.autoBrakeRearMinSpeedCmPs = (uint16_t)constrain((int)body["autoBrakeRearMinSpeedCmPs"], 0, 500);

    const bool ok = saveConfig(c);

    // Apply tunables that take effect without reboot.
    deps_.steering->setPulseDuration(c.steeringPulseMs);
    deps_.steering->setCooldown(c.steeringCooldownMs);
    deps_.steering->setDefaultPwm(c.defaultSteerPwm);
    deps_.steering->setInverted(c.steerInverted);
    deps_.drive->setDefaultPwm(c.defaultDrivePwm);
    deps_.drive->setInverted(c.driveInverted);
    deps_.drive->setActiveBrakePwm(c.activeBrakePwm);
    deps_.drive->setActiveBrakeMaxMs(c.activeBrakeMaxMs);
    deps_.safety->setTimeout(c.heartbeatTimeoutMs);
    sensors::setImuInverts(c.imuInvertX, c.imuInvertY, c.imuInvertZ);
    if (deps_.autoBrake) {
        deps_.autoBrake->setEnabled(c.autoBrakeEnabled);
        deps_.autoBrake->setFrontParams(c.autoBrakeFrontBaseCm,
                                        c.autoBrakeFrontSlopeCmPerMs,
                                        c.autoBrakeFrontMinSpeedCmPs);
        deps_.autoBrake->setRearParams (c.autoBrakeRearBaseCm,
                                        c.autoBrakeRearSlopeCmPerMs,
                                        c.autoBrakeRearMinSpeedCmPs);
    }

    JsonDocument resp;
    resp["ok"]      = ok;
    resp["message"] = ok ? "saved (Wi-Fi/IP changes apply on reboot)" : "save failed";
    sendJsonDoc(req, ok ? 200 : 500, resp);
}

void Portal::handleControl(AsyncWebServerRequest* req, JsonVariant& body) {
    const char* action = body["action"].as<const char*>();
    const uint8_t speed = body["speed"].is<int>()
        ? (uint8_t)constrain((int)body["speed"], 0, 255)
        : 0;

    Command cmd = CommandHandler::parse(action);
    auto res = deps_.commands->execute(cmd, speed);

    JsonDocument resp;
    resp["ok"]     = res.accepted;
    resp["reason"] = res.reason;
    sendJsonDoc(req, res.accepted ? 200 : 409, resp);
}

void Portal::handleWifiScan(AsyncWebServerRequest* req) {
    // NOTE: WiFi.scanNetworks() is synchronous (~2-5 s). It blocks the
    // AsyncTCP server task (not loop()) for that duration — other HTTP
    // requests will queue but the motor/safety loop stays fluid. A future
    // phase could switch to the async scan API and complete via a poll
    // endpoint, but it's fine for provisioning-time use.
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    int n = deps_.network->scanNetworks(
        [&](const char* ssid, int32_t rssi, bool secure, uint8_t channel) {
            if (!ssid || ssid[0] == '\0') return;  // skip hidden
            JsonObject e = arr.add<JsonObject>();
            e["ssid"]    = ssid;
            e["rssi"]    = rssi;
            e["secure"]  = secure;
            e["channel"] = channel;
        });
    doc["count"] = n < 0 ? 0 : n;
    sendJsonDoc(req, 200, doc);
}

void Portal::handleWifiProvision(AsyncWebServerRequest* req, JsonVariant& body) {
    String ssid = body["ssid"]     | "";
    String pass = body["password"] | "";
    ssid.trim();
    if (ssid.length() == 0) {
        JsonDocument err;
        err["ok"] = false; err["reason"] = "ssid empty";
        sendJsonDoc(req, 400, err);
        return;
    }

    // Like the scan endpoint, tryConnect blocks the server task up to ~12 s.
    auto r = deps_.network->tryConnect(ssid, pass);

    JsonDocument resp;
    resp["ok"] = r.ok;
    if (r.ok) {
        // Persist only after a verified connection.
        deps_.config->wifiSsid     = ssid;
        deps_.config->wifiPassword = pass;
        const bool saved  = saveConfig(*deps_.config);
        const bool dropAp = deps_.config->apShutdownAfterProvision;
        resp["saved"]          = saved;
        resp["ip"]             = r.ip.toString();
        resp["rssi"]           = r.rssi;
        resp["ssid"]           = ssid;
        resp["apWillShutDown"] = dropAp;
        if (saved) {
            resp["message"] = dropAp
                ? "connected and saved — AP will close in ~1.5 s"
                : "connected and saved — AP kept up (see config)";
        } else {
            resp["message"] = "connected but NVS save failed";
        }
        sendJsonDoc(req, 200, resp);

        // Tear down the hotspot only if the user opted in. Delay lets the
        // HTTP response flush to the client before the radio half dies.
        if (dropAp) deps_.network->requestApShutdown(1500);
    } else {
        resp["reason"] = r.reason;
        sendJsonDoc(req, 409, resp);
    }
}

void Portal::handleReboot(AsyncWebServerRequest* req) {
    JsonDocument doc; doc["ok"] = true; doc["message"] = "rebooting";
    sendJsonDoc(req, 200, doc);
    delay(200);
    ESP.restart();
}

void Portal::handleResetNetwork(AsyncWebServerRequest* req) {
    networkReset();
    JsonDocument doc; doc["ok"] = true; doc["message"] = "network reset; reboot to apply";
    sendJsonDoc(req, 200, doc);
}

void Portal::handleResetFactory(AsyncWebServerRequest* req) {
    factoryReset();
    JsonDocument doc; doc["ok"] = true; doc["message"] = "factory reset; rebooting";
    sendJsonDoc(req, 200, doc);
    delay(200);
    ESP.restart();
}

void Portal::handleSchema(AsyncWebServerRequest* req) {
    // Static protocol/config description that a mobile or web client can
    // fetch to build UI without hard-coding enums. Bumping kProtocolVersion
    // in WsServer.h is the forward-compat signal.
    JsonDocument doc;
    doc["proto"]    = 1;
    doc["device"]   = "smartrc-esp32";
    auto tr = doc["transports"].to<JsonArray>();
    tr.add("http");
    tr.add("ws");
    doc["ws"]       = "/ws";

    auto cmds = doc["commands"].to<JsonArray>();
    struct { const char* action; const char* takes; } kCmds[] = {
        {"forward",     "speed"}, {"reverse",     "speed"},
        {"stop",        nullptr}, {"brake",       nullptr},
        {"left",        "speed"}, {"right",       "speed"},
        {"steer_stop",  nullptr},
        {"estop",       nullptr}, {"clear_estop", nullptr},
    };
    for (auto& k : kCmds) {
        JsonObject o = cmds.add<JsonObject>();
        o["action"] = k.action;
        if (k.takes) o["takes"] = k.takes;
    }

    auto evs = doc["events"].to<JsonArray>();
    for (auto* k : { "estop_latched", "estop_cleared",
                     "stale_timeout", "stale_cleared",
                     "net_mode_changed" }) {
        JsonObject o = evs.add<JsonObject>();
        o["kind"] = k;
    }

    auto streams = doc["streams"].to<JsonArray>();
    JsonObject st = streams.add<JsonObject>();
    st["name"] = "telemetry";
    st["hzMin"] = 1; st["hzMax"] = 50; st["hzDefault"] = 20;

    auto cfg = doc["configRanges"].to<JsonObject>();
    cfg["steeringPulseMs"]["min"]    = 50;
    cfg["steeringPulseMs"]["max"]    = 2000;
    cfg["steeringCooldownMs"]["min"] = 0;
    cfg["steeringCooldownMs"]["max"] = 2000;
    cfg["defaultDrivePwm"]["min"]    = 0;
    cfg["defaultDrivePwm"]["max"]    = 255;
    cfg["defaultSteerPwm"]["min"]    = 0;
    cfg["defaultSteerPwm"]["max"]    = 255;
    cfg["heartbeatTimeoutMs"]["min"] = 50;
    cfg["heartbeatTimeoutMs"]["max"] = 60000;

    sendJsonDoc(req, 200, doc);
}

void Portal::handleImuCalibrate(AsyncWebServerRequest* req) {
    // Blocks the HTTP task for ~2.4 s while we resample — small price for
    // a one-shot action. Motor/safety loop stays fluid on the other task.
    sensors::recalibrateImu();
    JsonDocument doc;
    doc["ok"]      = true;
    doc["message"] = "gyro bias re-sampled";
    sendJsonDoc(req, 200, doc);
}

void Portal::handleNotFound(AsyncWebServerRequest* req) {
    JsonDocument doc; doc["error"] = "not found"; doc["uri"] = req->url();
    sendJsonDoc(req, 404, doc);
}

}  // namespace smartrc
