#include <WiFi.h>   // vor WebServer.h, da WebServer auf Network/WiFi aufbaut
#include "ApiServer.h"

ApiServer::ApiServer(uint16_t port) : _server(port) {}

void ApiServer::begin() {
    _server.on("/api/info", HTTP_GET, [this]() {
        String json = infoProvider ? infoProvider() : String("{\"error\":\"no provider\"}");
        _server.send(200, "application/json", json);
    });

    _server.on("/api/status", HTTP_GET, [this]() {
        String json = statusProvider ? statusProvider() : String("{\"error\":\"no provider\"}");
        _server.send(200, "application/json", json);
    });

    _server.on("/api/startstop", HTTP_POST, [this]() {
        StaticJsonDocument<128> doc;
        DeserializationError err = deserializeJson(doc, _server.arg("plain"));
        if (err) {
            _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        int action = doc["action"] | -1;
        if (action < 0 || action > 255) {
            _server.send(400, "application/json", "{\"error\":\"action out of range\"}");
            return;
        }
        bool ok = startStopHandler ? startStopHandler(action) : false;
        _server.send(ok ? 200 : 502, "application/json",
                     ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"ble not connected\"}");
    });

    _server.on("/api/charge", HTTP_POST, [this]() {
        DynamicJsonDocument doc(1024);
        DeserializationError err = deserializeJson(doc, _server.arg("plain"));
        if (err) {
            _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        JsonObject obj = doc.as<JsonObject>();
        bool ok = chargeHandler ? chargeHandler(obj) : false;
        _server.send(ok ? 200 : 502, "application/json",
                     ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"ble not connected\"}");
    });

    _server.onNotFound([this]() {
        _server.send(404, "application/json", "{\"error\":\"not found\"}");
    });

    _server.begin();
}

void ApiServer::handle() {
    _server.handleClient();
}
