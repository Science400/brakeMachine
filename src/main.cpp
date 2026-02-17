#include <Arduino.h>
#include "config.h"
#include "serial_comm.h"
#include "wifi_manager.h"
#include <WebServer.h>

SerialComm serialComm;
WiFiManager wifiMgr;
WebServer server(WEB_SERVER_PORT);

void onDumpReceived(const String& data, size_t length) {
    Serial.println("=== DUMP RECEIVED ===");
    Serial.printf("Size: %u bytes\n", length);

    if (wifiMgr.isTimeSynced()) {
        Serial.printf("Time: %s\n", wifiMgr.getTimestamp().c_str());
    }

    Serial.println("--- Begin Data ---");
    Serial.print(data);
    Serial.println("\n--- End Data ---");

    // TODO: Phase 3 — upload to HTTP receiver
    // TODO: Phase 3 — queue to LittleFS if upload fails
}

// Captive portal / WiFi setup page
const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>brakeMachine Setup</title>
<style>
  body { font-family: -apple-system, sans-serif; max-width: 400px; margin: 40px auto; padding: 0 20px; background: #1a1a2e; color: #eee; }
  h1 { color: #e94560; font-size: 1.4em; }
  input, select { width: 100%; padding: 10px; margin: 6px 0 16px; border: 1px solid #444; border-radius: 6px; background: #16213e; color: #eee; box-sizing: border-box; }
  button { width: 100%; padding: 12px; background: #e94560; color: white; border: none; border-radius: 6px; font-size: 1em; cursor: pointer; }
  button:hover { background: #c73650; }
  .status { padding: 10px; border-radius: 6px; background: #16213e; margin-bottom: 20px; }
  .ok { color: #4ecca3; }
  .warn { color: #e94560; }
</style>
</head><body>
<h1>brakeMachine Setup</h1>
<div class="status">
  <p>Mode: <span id="mode">--</span></p>
  <p>IP: <span id="ip">--</span></p>
</div>
<form action="/save" method="POST">
  <label>WiFi Network (SSID)</label>
  <input name="ssid" id="ssid" required>
  <label>Password</label>
  <input name="pass" type="password" required>
  <button type="submit">Connect</button>
</form>
<script>
fetch('/api/status').then(r=>r.json()).then(d=>{
  document.getElementById('mode').textContent=d.wifi_mode;
  document.getElementById('mode').className=d.wifi_mode==='connected'?'ok':'warn';
  document.getElementById('ip').textContent=d.ip;
  if(d.ssid) document.getElementById('ssid').value=d.ssid;
});
</script>
</body></html>
)rawliteral";

void handleRoot() {
    server.send_P(200, "text/html", SETUP_HTML);
}

void handleStatus() {
    char json[256];
    const char* mode = "unknown";
    switch (wifiMgr.getMode()) {
        case WiFiMode::CONNECTED:    mode = "connected"; break;
        case WiFiMode::AP_MODE:      mode = "ap_mode"; break;
        case WiFiMode::CONNECTING:   mode = "connecting"; break;
        case WiFiMode::DISCONNECTED: mode = "disconnected"; break;
    }

    snprintf(json, sizeof(json),
        "{\"wifi_mode\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\","
        "\"time_synced\":%s,\"dump_count\":%u,\"uptime\":%lu}",
        mode,
        wifiMgr.getIP().c_str(),
        wifiMgr.getSSID().c_str(),
        wifiMgr.isTimeSynced() ? "true" : "false",
        serialComm.getDumpCount(),
        millis() / 1000);

    server.send(200, "application/json", json);
}

void handleSave() {
    if (server.hasArg("ssid") && server.hasArg("pass")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        wifiMgr.setCredentials(ssid, pass);
        server.send(200, "text/html",
            "<html><body style='font-family:sans-serif;text-align:center;padding:40px;"
            "background:#1a1a2e;color:#eee'>"
            "<h2>Connecting...</h2>"
            "<p>If it fails, reconnect to the <b>brakeMachine-setup</b> network.</p>"
            "</body></html>");
    } else {
        server.send(400, "text/plain", "Missing ssid or pass");
    }
}

void handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/generate_204", HTTP_GET, handleRoot);        // Android captive portal
    server.on("/hotspot-detect.html", HTTP_GET, handleRoot); // Apple captive portal
    server.on("/connecttest.txt", HTTP_GET, handleRoot);     // Windows captive portal
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.printf("[Web] Server started on port %d\n", WEB_SERVER_PORT);
}

void setup() {
    // USB-CDC serial for debug output
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[brakeMachine] Starting...");

    // Status LED
    pinMode(STATUS_LED_PIN, OUTPUT);

    // Initialize 920i serial connection
    serialComm.onDumpComplete(onDumpReceived);
    serialComm.begin();

    // Initialize WiFi
    wifiMgr.begin();

    // Start web server (works in both AP and STA mode)
    setupWebServer();

    Serial.println("[brakeMachine] Ready.");
}

void loop() {
    serialComm.update();
    wifiMgr.update();
    server.handleClient();
}
