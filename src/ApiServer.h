#pragma once

// Minimaler HTTP-REST-Server (synchron, läuft in loop()).
// Nutzt die in Arduino-Core 3.x enthaltene WebServer-Bibliothek.

#include <ArduinoJson.h>
#include <WebServer.h>
#include <functional>

class ApiServer {
public:
    ApiServer(uint16_t port = 80);

    // Vom Hauptprogramm gesetzte Callbacks.
    std::function<String()> infoProvider;                    // GET  /api/info
    std::function<String()> statusProvider;                  // GET  /api/status
    std::function<bool(int action)> startStopHandler;        // POST /api/startstop {action}
    std::function<bool(const JsonObject& obj)> chargeHandler; // POST /api/charge

    void begin();
    void handle();   // in loop() aufrufen

private:
    WebServer _server;
};
