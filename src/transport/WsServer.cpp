#include "transport/WsServer.h"

#include "config/Config.h"
#include "control/CommandHandler.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/Steering.h"
#include "network/NetworkManager.h"
#include "sensors/Sensors.h"

using smartrc::Drive;

namespace smartrc {

namespace {
constexpr const char* kDeviceName   = "smartrc-esp32";
constexpr const char* kFeatures[]   = { "drive", "steer", "estop", "sensors_stub" };
constexpr const int   kFeaturesLen  = sizeof(kFeatures) / sizeof(kFeatures[0]);
}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WsServer::begin(AsyncWebServer& httpServer, const WsServerDeps& deps) {
    deps_ = deps;
    ws_.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                       AwsEventType t, void* a, uint8_t* d, size_t l) {
        onEvent(s, c, t, a, d, l);
    });
    httpServer.addHandler(&ws_);
    Serial.println("[ws] /ws endpoint registered");
}

void WsServer::update() {
    ws_.cleanupClients();
    pollEvents();
    pushTelemetry();
}

size_t WsServer::telemetrySubscriberCount() const {
    size_t n = 0;
    // Safe to read without the lock — worst case we're off by one briefly.
    for (const auto& c : clients_) if (c.subTelemetry) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// AsyncWebSocket event dispatch (runs on the AsyncTCP task)
// ---------------------------------------------------------------------------

void WsServer::onEvent(AsyncWebSocket* /*server*/,
                       AsyncWebSocketClient* client,
                       AwsEventType type, void* arg,
                       uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            addClient(client->id());
            sendHello(client);
            Serial.printf("[ws] client %u connected (total=%u)\n",
                          (unsigned)client->id(),
                          (unsigned)ws_.count());
            break;

        case WS_EVT_DISCONNECT:
            dropClient(client->id());
            Serial.printf("[ws] client %u disconnected\n",
                          (unsigned)client->id());
            break;

        case WS_EVT_DATA: {
            auto* info = static_cast<AwsFrameInfo*>(arg);
            // Single text frame, not fragmented. JSON payload fits easily.
            if (info && info->final && info->index == 0 && info->len == len
                && info->opcode == WS_TEXT) {
                handleText(client, (const char*)data, len);
            }
            break;
        }

        case WS_EVT_ERROR:
        case WS_EVT_PONG:
        default:
            break;
    }
}

void WsServer::handleText(AsyncWebSocketClient* client,
                          const char* data, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) { sendErr(client, "bad_json", err.c_str()); return; }

    const char* t = doc["t"].as<const char*>();
    if (!t) { sendErr(client, "missing_type"); return; }

    ClientState* cs = nullptr;
    portENTER_CRITICAL(&clientsMux_);
    cs = findClientLocked(client->id());
    portEXIT_CRITICAL(&clientsMux_);
    if (!cs) return;

    // hello: (re)handshake, optional token.
    if (!strcmp(t, "hello")) {
        String token = doc["token"] | "";
        const bool ok = tokenOk(token);
        cs->authed = ok;
        if (!ok) { sendErr(client, "unauth"); return; }
        sendHello(client);
        return;
    }

    // ping: app-level RTT probe, doesn't require auth.
    if (!strcmp(t, "ping")) {
        long id = doc["id"] | 0L;
        sendPong(client, id);
        return;
    }

    // sub / unsub: stream management, doesn't require auth.
    if (!strcmp(t, "sub")) {
        int hz = doc["hz"] | 20;
        hz = constrain(hz, 1, 50);
        JsonArray streams = doc["streams"].as<JsonArray>();
        for (JsonVariant v : streams) {
            const char* s = v.as<const char*>();
            if (s && !strcmp(s, "telemetry")) {
                cs->subTelemetry = true;
                cs->pushHz       = (uint16_t)hz;
                cs->lastPushMs   = 0;  // fire first frame immediately
            }
        }
        return;
    }
    if (!strcmp(t, "unsub")) {
        JsonArray streams = doc["streams"].as<JsonArray>();
        for (JsonVariant v : streams) {
            const char* s = v.as<const char*>();
            if (s && !strcmp(s, "telemetry")) cs->subTelemetry = false;
        }
        return;
    }

    // hb: bare heartbeat. Refreshes safety watchdog without any motor
    // effect. Useful for idle mobile clients that want to keep the
    // connection "alive" to the safety layer without actively commanding.
    if (!strcmp(t, "hb")) {
        deps_.safety->notifyHeartbeat();
        return;
    }

    // cmd: motor command. Gated on auth if a token is configured.
    if (!strcmp(t, "cmd")) {
        if (deps_.config->controlToken.length() > 0 && !cs->authed) {
            sendErr(client, "unauth"); return;
        }
        const char* action = doc["action"].as<const char*>();
        uint8_t speed = 0;
        if (doc["speed"].is<int>()) {
            speed = (uint8_t)constrain((int)doc["speed"], 0, 255);
        }
        const long id = doc["id"] | -1L;
        Command c = CommandHandler::parse(action);
        auto res = deps_.commands->execute(c, speed);
        if (id >= 0) sendAck(client, id, res.accepted, res.reason);
        return;
    }

    sendErr(client, "unknown_type", t);
}

// ---------------------------------------------------------------------------
// Outbound helpers
// ---------------------------------------------------------------------------

void WsServer::sendHello(AsyncWebSocketClient* client) {
    JsonDocument doc;
    doc["t"]            = "hello";
    doc["proto"]        = kProtocolVersion;
    doc["device"]       = kDeviceName;
    doc["authRequired"] = deps_.config->controlToken.length() > 0;
    auto feats = doc["features"].to<JsonArray>();
    for (int i = 0; i < kFeaturesLen; ++i) feats.add(kFeatures[i]);
    String out; serializeJson(doc, out);
    client->text(out);
}

void WsServer::sendErr(AsyncWebSocketClient* client,
                       const char* code, const char* detail) {
    JsonDocument doc;
    doc["t"]    = "err";
    doc["code"] = code;
    if (detail) doc["detail"] = detail;
    String out; serializeJson(doc, out);
    client->text(out);
}

void WsServer::sendAck(AsyncWebSocketClient* client, long id, bool ok,
                       const char* reason) {
    JsonDocument doc;
    doc["t"]      = "ack";
    doc["id"]     = id;
    doc["ok"]     = ok;
    doc["reason"] = reason ? reason : "";
    String out; serializeJson(doc, out);
    client->text(out);
}

void WsServer::sendPong(AsyncWebSocketClient* client, long id) {
    JsonDocument doc;
    doc["t"]  = "pong";
    doc["id"] = id;
    String out; serializeJson(doc, out);
    client->text(out);
}

// ---------------------------------------------------------------------------
// Telemetry + events
// ---------------------------------------------------------------------------

void WsServer::buildTelemetryJson(JsonDocument& doc) {
    auto drive = doc["drive"].to<JsonObject>();
    drive["moving"] = deps_.drive->isMoving();

    auto steer = doc["steer"].to<JsonObject>();
    steer["state"]   = (int)deps_.steering->state();
    steer["lastDir"] = (int)deps_.steering->lastDirection();

    auto safety = doc["safety"].to<JsonObject>();
    safety["emergency"] = deps_.safety->isEmergency();
    safety["stale"]     = deps_.safety->isStale();

    auto net = doc["net"].to<JsonObject>();
    net["mode"] = (int)deps_.network->mode();
    net["rssi"] = deps_.network->rssi();

    // Sensor payload (IMU today, more to come). Same shape as /api/status
    // so the app can render either stream with one code path.
    sensors::appendSensorsJson(doc.as<JsonObject>());
}

void WsServer::pushTelemetry() {
    // Snapshot due-client IDs under lock, then serialize + send outside the
    // critical section. Keeps the lock hold-time tiny and lets us skip the
    // JSON serialization entirely when no one is subscribed.
    const uint32_t now = millis();
    std::vector<uint32_t> due;
    portENTER_CRITICAL(&clientsMux_);
    for (auto& cs : clients_) {
        if (!cs.subTelemetry) continue;
        const uint32_t interval = 1000u / (cs.pushHz ? cs.pushHz : 1);
        if (now - cs.lastPushMs < interval) continue;
        cs.lastPushMs = now;
        due.push_back(cs.id);
    }
    portEXIT_CRITICAL(&clientsMux_);
    if (due.empty()) return;

    JsonDocument doc;
    doc["t"]  = "telemetry";
    doc["ts"] = now;
    buildTelemetryJson(doc);
    String out; serializeJson(doc, out);

    for (uint32_t id : due) {
        AsyncWebSocketClient* c = ws_.client(id);
        if (!c || c->status() != WS_CONNECTED) continue;
        // Drop rather than queue when backpressured — telemetry is
        // idempotent, next tick will have fresher data.
        if (c->queueIsFull()) continue;
        c->text(out);
    }
}

void WsServer::pollEvents() {
    const bool estop = deps_.safety->isEmergency();
    if (estop != lastEstop_) {
        lastEstop_ = estop;
        broadcastEvent(estop ? "estop_latched" : "estop_cleared");
    }

    const bool stale = deps_.safety->isStale();
    if (stale != lastStale_) {
        lastStale_ = stale;
        broadcastEvent(stale ? "stale_timeout" : "stale_cleared");
    }

    const uint8_t mode = (uint8_t)deps_.network->mode();
    if (lastNetMode_ == 0xFF) {
        lastNetMode_ = mode;   // prime on first tick, no event
    } else if (mode != lastNetMode_) {
        lastNetMode_ = mode;
        broadcastEvent("net_mode_changed");
    }

    // Drain any brake-decision events queued by Drive. We broadcast a
    // richer payload (v, a) so the app's Events panel can show what the
    // firmware saw at the moment it decided.
    Drive::Event ev;
    while (deps_.drive->takeEvent(ev)) {
        JsonDocument doc;
        doc["t"]    = "event";
        doc["kind"] = ev.kind;
        doc["ts"]   = ev.ts_ms;
        doc["v"]    = ev.v;
        if (ev.a != 0) doc["a"] = ev.a;
        String out; serializeJson(doc, out);
        ws_.textAll(out);
        Serial.printf("[ws] event: %s (v=%+.2f a=%+.2f)\n", ev.kind, ev.v, ev.a);
    }
}

void WsServer::broadcastEvent(const char* kind) {
    JsonDocument doc;
    doc["t"]    = "event";
    doc["kind"] = kind;
    doc["ts"]   = millis();
    String out; serializeJson(doc, out);
    ws_.textAll(out);
    Serial.printf("[ws] event: %s\n", kind);
}

// ---------------------------------------------------------------------------
// Client-state management
// ---------------------------------------------------------------------------

WsServer::ClientState* WsServer::findClientLocked(uint32_t id) {
    for (auto& c : clients_) if (c.id == id) return &c;
    return nullptr;
}

void WsServer::addClient(uint32_t id) {
    portENTER_CRITICAL(&clientsMux_);
    clients_.push_back(ClientState{id, /*authed*/false, /*subTelemetry*/false,
                                   /*pushHz*/20, /*lastPushMs*/0});
    portEXIT_CRITICAL(&clientsMux_);
}

void WsServer::dropClient(uint32_t id) {
    portENTER_CRITICAL(&clientsMux_);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it->id == id) { clients_.erase(it); break; }
    }
    portEXIT_CRITICAL(&clientsMux_);
}

bool WsServer::tokenOk(const String& provided) const {
    const String& expected = deps_.config->controlToken;
    if (expected.length() == 0) return true;  // auth disabled
    return expected == provided;
}

}  // namespace smartrc
