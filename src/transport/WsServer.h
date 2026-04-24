#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include <vector>

namespace smartrc {

struct Config;
class CommandHandler;
class Drive;
class Steering;
class Safety;
class NetworkManager;

struct WsServerDeps {
    const Config*   config;
    CommandHandler* commands;
    Drive*          drive;
    Steering*       steering;
    Safety*         safety;
    NetworkManager* network;
};

// WebSocket endpoint for realtime control + telemetry. Mounts /ws onto the
// AsyncWebServer owned by Portal. Inbound `cmd` frames are funnelled
// through CommandHandler::execute() — the same chokepoint HTTP uses — so
// safety / heartbeat semantics don't fork. Outbound telemetry is pushed
// per-client at the rate each client requested via a `sub` frame.
//
// Message protocol (JSON, single frame, text):
//   Client -> Device
//     { t:"hello", token:"..." }           // auth / handshake
//     { t:"hb" }                           // bare heartbeat
//     { t:"cmd", action, speed?, id? }     // motor command
//     { t:"sub",   streams:[...], hz:20 }  // subscribe (telemetry)
//     { t:"unsub", streams:[...] }         // unsubscribe
//     { t:"ping", id? }                    // app-level RTT probe
//   Device -> Client
//     { t:"hello", proto, device, features, authRequired }
//     { t:"telemetry", ts, drive, steer, safety, net }
//     { t:"event", kind, ... }
//     { t:"ack",   id, ok, reason }
//     { t:"pong",  id }
//     { t:"err",   code, detail? }
class WsServer {
public:
    // Must be called AFTER the HTTP server exists (Portal::begin ran).
    // Adding handlers before/after AsyncWebServer::begin() is both fine.
    void begin(AsyncWebServer& httpServer, const WsServerDeps& deps);

    // Pump from loop(): cleanup zombie clients, detect edge-triggered
    // events (estop toggle, stale toggle, net-mode change) and push
    // telemetry to subscribed clients at their configured Hz.
    void update();

    // Protocol version advertised in the hello frame. Bump on breaking
    // changes to the wire format so older clients can gate themselves.
    static constexpr int kProtocolVersion = 1;

    // Expose the live subscriber count for status/debug.
    size_t telemetrySubscriberCount() const;

private:
    // Per-client state — keyed by the AsyncWebSocketClient's id().
    struct ClientState {
        uint32_t id           = 0;
        bool     authed       = false;  // true when token matches (or auth off)
        bool     subTelemetry = false;
        uint16_t pushHz       = 20;     // 1..50
        uint32_t lastPushMs   = 0;
    };

    // AsyncWebSocket callbacks (run on the AsyncTCP task).
    void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                 AwsEventType type, void* arg, uint8_t* data, size_t len);
    void handleText(AsyncWebSocketClient* client, const char* data, size_t len);

    // Outbound helpers.
    void sendHello(AsyncWebSocketClient* client);
    void sendErr  (AsyncWebSocketClient* client, const char* code,
                   const char* detail = nullptr);
    void sendAck  (AsyncWebSocketClient* client, long id, bool ok,
                   const char* reason);
    void sendPong (AsyncWebSocketClient* client, long id);

    // Server-driven.
    void pushTelemetry();
    void pollEvents();
    void broadcastEvent(const char* kind);
    void buildTelemetryJson(JsonDocument& doc);

    // Client-state management (protected by clientsMux_).
    ClientState* findClientLocked(uint32_t id);
    void         addClient(uint32_t id);
    void         dropClient(uint32_t id);

    bool tokenOk(const String& provided) const;

    AsyncWebSocket ws_{"/ws"};
    WsServerDeps   deps_{};

    std::vector<ClientState> clients_;
    // ESP32 SMP-safe spinlock guarding clients_ across the AsyncTCP task
    // (which mutates on connect/disconnect) and the main loop() task
    // (which reads in pushTelemetry). Critical sections must be short.
    portMUX_TYPE clientsMux_ = portMUX_INITIALIZER_UNLOCKED;

    // Edge-trigger cache for event frames.
    bool    lastEstop_   = false;
    bool    lastStale_   = false;
    uint8_t lastNetMode_ = 0xFF;  // sentinel so the first tick doesn't emit
};

}  // namespace smartrc
