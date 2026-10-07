#pragma once

// Minimaler HTTP-REST-Server (synchron, läuft in loop()).
// Nutzt die in Arduino-Core 3.x enthaltene WebServer-Bibliothek.

#include <ArduinoJson.h>
#include <WebServer.h>
#include <functional>

#include "DataStore.h"

class ApiServer {
public:
    ApiServer(DataStore& store, uint16_t port = 80);

    // Vom Hauptprogramm gesetzte Callbacks.
    std::function<String()> infoProvider;                     // GET  /api/info
    std::function<String()> statusProvider;                   // GET  /api/status
    std::function<String()> scanProvider;                     // GET  /api/scan
    std::function<bool(const String& addr)> connectHandler;   // POST /api/connect {address}
    std::function<bool()> disconnectHandler;                  // POST /api/disconnect
    std::function<String()> wifiProvider;                     // GET  /api/wifi
    std::function<bool(const String& ssid, const String& pass)> wifiSaveHandler; // POST /api/wifi
    std::function<bool(int action)> startStopHandler;         // POST /api/startstop {action}
    std::function<bool(const JsonObject& obj)> chargeHandler; // POST /api/charge

    void begin();
    void handle();   // in loop() aufrufen

private:
    WebServer _server;
    DataStore& _store;

    // Führt eine persistente Mutation aus (Route+Methode -> EntityType/OpType).
    void handleMutation(EntityType entity, OpType op, const char* id);
};
