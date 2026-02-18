#include <Arduino.h>
#include "config.h"
#include "serial_comm.h"
#include "wifi_manager.h"
#include "data_uploader.h"
#include "dashboard.h"
#include <WebServer.h>
#include <ArduinoJson.h>

SerialComm serialComm;
WiFiManager wifiMgr;
DataUploader uploader;
WebServer server(WEB_SERVER_PORT);

void onDumpReceived(const String& data, size_t length) {
    Serial.printf("[main] Dump received: %u bytes\n", length);

    String ts = wifiMgr.isTimeSynced()
        ? wifiMgr.getTimestamp()
        : String("boot+") + String(millis() / 1000) + "s";

    uploader.submitDump(data, length, ts);
}

void handleRoot() {
    server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleStatus() {
    JsonDocument doc;

    const char* mode = "unknown";
    switch (wifiMgr.getMode()) {
        case WiFiMode::CONNECTED:    mode = "connected"; break;
        case WiFiMode::AP_MODE:      mode = "ap_mode"; break;
        case WiFiMode::CONNECTING:   mode = "connecting"; break;
        case WiFiMode::DISCONNECTED: mode = "disconnected"; break;
    }

    doc["wifi_mode"] = mode;
    doc["ip"] = wifiMgr.getIP();
    doc["ssid"] = wifiMgr.getSSID();
    doc["time_synced"] = wifiMgr.isTimeSynced();
    doc["uptime"] = millis() / 1000;
    doc["dump_count"] = serialComm.getDumpCount();

    const UploadStats& stats = uploader.getStats();
    doc["upload_success"] = stats.totalSuccess;
    doc["upload_failed"] = stats.totalFailed;
    doc["queue_depth"] = stats.queueDepth;
    doc["last_upload_time"] = stats.lastUploadTime;
    doc["receiver_url"] = stats.receiverUrl;

    const DumpRecord& ld = uploader.getLastDump();
    if (ld.id > 0) {
        JsonObject last = doc["last_dump"].to<JsonObject>();
        last["id"] = ld.id;
        last["timestamp"] = ld.timestamp;
        last["size"] = ld.size;
        last["uploaded"] = ld.uploaded;
        last["preview"] = ld.preview;
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void handleSave() {
    if (server.hasArg("ssid") && server.hasArg("pass")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        wifiMgr.setCredentials(ssid, pass);
        server.send(200, "text/html",
            "<html><body style='font-family:sans-serif;text-align:center;padding:40px;"
            "background:#0d1117;color:#c9d1d9'>"
            "<h2>Connecting...</h2>"
            "<p>If it fails, reconnect to the <b>brakeMachine-setup</b> network.</p>"
            "</body></html>");
    } else {
        server.send(400, "text/plain", "Missing ssid or pass");
    }
}

void handleSetReceiver() {
    if (server.hasArg("url")) {
        uploader.setReceiverUrl(server.arg("url"));
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing url");
    }
}

void handleClearWifi() {
    wifiMgr.clearCredentials();
    server.send(200, "text/html",
        "<html><body style='font-family:sans-serif;text-align:center;padding:40px;"
        "background:#0d1117;color:#c9d1d9'>"
        "<h2>WiFi credentials cleared.</h2>"
        "<p>Connect to <b>brakeMachine-setup</b> to reconfigure.</p>"
        "</body></html>");
}

void handleTestDump() {
    // Simulate a 920i print dump for testing without the machine
    static const char TEST_DATA[] =
        "920i Print Output\r\n"
        "Date: 2026-02-18\r\n"
        "Time: 10:30:00\r\n"
        "\r\n"
        "ID\tGross\tTare\tNet\tUnit\r\n"
        "1\t1250.5\t120.0\t1130.5\tlb\r\n"
        "2\t2340.0\t120.0\t2220.0\tlb\r\n"
        "3\t985.5\t120.0\t865.5\tlb\r\n"
        "4\t3100.0\t120.0\t2980.0\tlb\r\n"
        "5\t1875.0\t120.0\t1755.0\tlb\r\n";

    String data(TEST_DATA);
    Serial.println("[Test] Simulating dump...");
    onDumpReceived(data, data.length());
    server.send(200, "text/plain", "Test dump submitted");
}

void handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/api/set-receiver", HTTP_POST, handleSetReceiver);
    server.on("/api/clear-wifi", HTTP_POST, handleClearWifi);
    server.on("/api/test-dump", HTTP_POST, handleTestDump);
    // Captive portal detection — serve dashboard directly (no redirect)
    server.on("/generate_204", HTTP_GET, handleRoot);
    server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
    server.on("/connecttest.txt", HTTP_GET, handleRoot);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.printf("[Web] Server started on port %d\n", WEB_SERVER_PORT);
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial monitor to connect
    Serial.println("\n[brakeMachine] Starting...");

    pinMode(STATUS_LED_PIN, OUTPUT);

    serialComm.onDumpComplete(onDumpReceived);
    serialComm.begin();

    wifiMgr.begin();

    uploader.setConnectivityCheck([]() {
        return wifiMgr.getMode() == WiFiMode::CONNECTED;
    });
    uploader.begin();

    setupWebServer();

    Serial.println("[brakeMachine] Ready.");
}

void loop() {
    serialComm.update();
    wifiMgr.update();
    uploader.update();
    server.handleClient();
}
