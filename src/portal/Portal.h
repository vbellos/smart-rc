#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

namespace smartrc {

struct Config;
class NetworkManager;
class CommandHandler;
class Drive;
class Steering;
class Safety;
class StuntEngine;

// Bundles all the dependencies the portal needs to render status,
// edit config, and dispatch commands.
struct PortalDeps {
    Config*          config;
    NetworkManager*  network;
    CommandHandler*  commands;
    Drive*           drive;
    Steering*        steering;
    Safety*          safety;
    StuntEngine*     stunts;
};

class Portal {
public:
    void begin(const PortalDeps& deps);

    // Kept for API stability with main.cpp's loop(). AsyncWebServer runs on
    // its own FreeRTOS task, so this is a no-op after the async migration.
    void handle();

    // Exposed so other transports (e.g. WsServer) can add their own handlers
    // onto the same :80 listener — one port, one origin, one CORS story.
    AsyncWebServer& server() { return server_; }

private:
    void registerRoutes();

    // GET / body-less POST handlers: take only the request.
    void handleRoot           (AsyncWebServerRequest* req);
    void handleStatus         (AsyncWebServerRequest* req);
    void handleGetConfig      (AsyncWebServerRequest* req);
    void handleSchema         (AsyncWebServerRequest* req);
    void handleWifiScan       (AsyncWebServerRequest* req);
    void handleReboot         (AsyncWebServerRequest* req);
    void handleResetNetwork   (AsyncWebServerRequest* req);
    void handleResetFactory   (AsyncWebServerRequest* req);
    void handleImuCalibrate   (AsyncWebServerRequest* req);
    void handleStuntAbort     (AsyncWebServerRequest* req);
    void handleNotFound       (AsyncWebServerRequest* req);

    // JSON-body POST handlers: the async JSON helper delivers the parsed body.
    void handlePostConfig     (AsyncWebServerRequest* req, JsonVariant& body);
    void handleControl        (AsyncWebServerRequest* req, JsonVariant& body);
    void handleWifiProvision  (AsyncWebServerRequest* req, JsonVariant& body);
    void handleStunt          (AsyncWebServerRequest* req, JsonVariant& body);

    PortalDeps      deps_{};
    AsyncWebServer  server_{80};
};

}  // namespace smartrc
