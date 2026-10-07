#include <WiFi.h>   // vor WebServer.h, da WebServer auf Network/WiFi aufbaut
#include "ApiServer.h"

// Minimale Verbindungsseite (HTML + JavaScript). Ruft die /api/*-Endpunkte auf.
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MC5000 Bridge</title>
<style>
body{font-family:system-ui,sans-serif;max-width:640px;margin:2rem auto;padding:0 1rem;color:#222}
h1{font-size:1.3rem}
.card{border:1px solid #ccc;border-radius:8px;padding:1rem;margin:1rem 0}
button{padding:.4rem .8rem;margin:.2rem;cursor:pointer}
.ok{color:#0a0}.off{color:#a00}
table{width:100%;border-collapse:collapse}
td,th{padding:.4rem;text-align:left;border-bottom:1px solid #eee}
code{background:#f4f4f4;padding:.1rem .3rem;border-radius:4px}
</style>
</head>
<body>
<h1>MC5000 Bridge &ndash; Verbindung</h1>
<div class="card">
  <strong>Status:</strong> <span id="status">&#8230;</span><br>
  <button onclick="scan()">Nach Ger&auml;ten suchen</button>
  <button onclick="disconnect()" id="btnDisconnect" hidden>Trennen</button>
</div>
<div class="card">
  <strong>WLAN:</strong> <span id="wifiStatus">&#8230;</span><br>
  <input id="ssid" placeholder="SSID" style="margin:.2rem;padding:.4rem;width:14rem">
  <input id="password" type="password" placeholder="Passwort" style="margin:.2rem;padding:.4rem;width:14rem"><br>
  <button onclick="saveWifi()">WLAN speichern</button>
</div>
<div class="card" id="devicesCard" hidden>
  <strong>Gefundene Ger&auml;te:</strong>
  <table><tbody id="deviceTable"></tbody></table>
</div>
<script>
async function api(path, opts){
  var r = await fetch(path, opts);
  return r.json().catch(function(){ return {}; });
}
async function refresh(){
  var info = await api('/api/info');
  var st = document.getElementById('status');
  if (info.bleConnected) {
    st.textContent = 'Verbunden mit ' + (info.address || '?');
    st.className = 'ok';
    document.getElementById('btnDisconnect').hidden = false;
  } else {
    st.textContent = info.address ? ('Nicht verbunden (gespeichert: ' + info.address + ')') : 'Nicht verbunden';
    st.className = 'off';
    document.getElementById('btnDisconnect').hidden = !info.address;
  }
}
async function scan(){
  document.getElementById('status').textContent = 'Suche ...';
  var res = await api('/api/scan');
  var tbody = document.getElementById('deviceTable');
  tbody.innerHTML = '';
  var devices = res.devices || [];
  document.getElementById('devicesCard').hidden = (devices.length === 0);
  devices.forEach(function(d){
    var tr = document.createElement('tr');
    var name = d.name ? d.name : '(ohne Name)';
    tr.innerHTML = '<td>' + name + '</td><td><code>' + d.address + '</code></td>' +
      '<td><button onclick="connect(\'' + d.address + '\')">Verbinden</button></td>';
    tbody.appendChild(tr);
  });
  refresh();
}
async function connect(address){
  document.getElementById('status').textContent = 'Verbinde ...';
  await api('/api/connect', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({address: address})});
  refresh();
}
async function disconnect(){
  await api('/api/disconnect', {method:'POST'});
  refresh();
}
async function refreshWifi(){
  var w = await api('/api/wifi');
  var el = document.getElementById('wifiStatus');
  if (w.connected) {
    el.textContent = 'Verbunden mit ' + w.ssid + ' (' + w.ip + ')';
    el.className = 'ok';
  } else {
    el.textContent = 'Nicht verbunden' + (w.ssid ? (' (gespeichert: ' + w.ssid + ')') : '') + ' - Setup-AP aktiv';
    el.className = 'off';
  }
}
async function saveWifi(){
  var ssid = document.getElementById('ssid').value;
  var password = document.getElementById('password').value;
  await api('/api/wifi', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ssid:ssid, password:password})});
  refreshWifi();
}
refresh();
refreshWifi();
setInterval(function(){ refresh(); refreshWifi(); }, 3000);
</script>
</body>
</html>
)rawliteral";

ApiServer::ApiServer(uint16_t port) : _server(port) {}

void ApiServer::begin() {
    _server.on("/", HTTP_GET, [this]() {
        _server.send(200, "text/html", INDEX_HTML);
    });

    _server.on("/api/info", HTTP_GET, [this]() {
        String json = infoProvider ? infoProvider() : String("{\"error\":\"no provider\"}");
        _server.send(200, "application/json", json);
    });

    _server.on("/api/status", HTTP_GET, [this]() {
        String json = statusProvider ? statusProvider() : String("{\"error\":\"no provider\"}");
        _server.send(200, "application/json", json);
    });

    _server.on("/api/scan", HTTP_GET, [this]() {
        String json = scanProvider ? scanProvider() : String("{\"devices\":[]}");
        _server.send(200, "application/json", json);
    });

    _server.on("/api/connect", HTTP_POST, [this]() {
        StaticJsonDocument<128> doc;
        DeserializationError err = deserializeJson(doc, _server.arg("plain"));
        if (err) {
            _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        String address = doc["address"] | "";
        if (address.length() == 0) {
            _server.send(400, "application/json", "{\"error\":\"address missing\"}");
            return;
        }
        bool ok = connectHandler ? connectHandler(address) : false;
        _server.send(ok ? 200 : 502, "application/json",
                     ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"connect failed\"}");
    });

    _server.on("/api/disconnect", HTTP_POST, [this]() {
        bool ok = disconnectHandler ? disconnectHandler() : false;
        _server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    _server.on("/api/wifi", HTTP_GET, [this]() {
        String json = wifiProvider ? wifiProvider() : String("{\"connected\":false}");
        _server.send(200, "application/json", json);
    });

    _server.on("/api/wifi", HTTP_POST, [this]() {
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, _server.arg("plain"));
        if (err) {
            _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        String ssid = doc["ssid"] | "";
        String pass = doc["password"] | "";
        bool ok = wifiSaveHandler ? wifiSaveHandler(ssid, pass) : false;
        _server.send(ok ? 200 : 400, "application/json",
                     ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"ssid missing\"}");
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
