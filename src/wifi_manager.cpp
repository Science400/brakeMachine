#include "wifi_manager.h"
#include "config.h"
#include <time.h>
#include <nvs_flash.h>

WiFiManager::WiFiManager()
    : _mode(WiFiMode::DISCONNECTED)
    , _lastConnectAttempt(0)
    , _reconnectInterval(5000)
    , _connectAttempts(0)
    , _timeSynced(false) {
}

void WiFiManager::begin() {
    // Load saved credentials from NVS
    // First call with read-write to create the namespace if it doesn't exist
    if (!_prefs.begin("wifi", false)) {
        Serial.println("[WiFi] NVS init failed, formatting...");
        nvs_flash_erase();
        nvs_flash_init();
        _prefs.begin("wifi", false);
    }
    _savedSSID = _prefs.getString("ssid", "");
    _savedPassword = _prefs.getString("pass", "");
    _prefs.end();

    if (_savedSSID.length() > 0) {
        Serial.printf("[WiFi] Saved network: %s\n", _savedSSID.c_str());
        _startSTA();
    } else {
        Serial.println("[WiFi] No saved credentials, starting AP mode");
        _startAP();
    }
}

void WiFiManager::update() {
    _updateLED();

    switch (_mode) {
        case WiFiMode::AP_MODE:
            _dnsServer.processNextRequest();
            break;

        case WiFiMode::CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _mode = WiFiMode::CONNECTED;
                _connectAttempts = 0;
                _reconnectInterval = 5000;
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

                // Start mDNS
                if (MDNS.begin(DEVICE_HOSTNAME)) {
                    MDNS.addService("http", "tcp", WEB_SERVER_PORT);
                    Serial.printf("[WiFi] mDNS: http://%s.local\n", DEVICE_HOSTNAME);
                }

                _initNTP();
            } else if (millis() - _lastConnectAttempt > 15000) {
                // Connection attempt timed out
                _connectAttempts++;
                if (_connectAttempts >= 3) {
                    Serial.println("[WiFi] Failed to connect after 3 attempts, starting AP mode");
                    WiFi.disconnect();
                    _startAP();
                } else {
                    Serial.printf("[WiFi] Retrying... (attempt %d/3)\n", _connectAttempts + 1);
                    _startSTA();
                }
            }
            break;

        case WiFiMode::CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                _mode = WiFiMode::DISCONNECTED;
                Serial.println("[WiFi] Connection lost");
            }
            // Check if NTP has synced
            if (!_timeSynced) {
                struct tm timeinfo;
                if (getLocalTime(&timeinfo, 0)) {
                    _timeSynced = true;
                    Serial.printf("[WiFi] NTP synced: %s", getTimestamp().c_str());
                }
            }
            break;

        case WiFiMode::DISCONNECTED:
            if (millis() - _lastConnectAttempt > _reconnectInterval) {
                Serial.println("[WiFi] Attempting reconnect...");
                _startSTA();
                // Exponential backoff: 5s, 10s, 20s, 40s, max 60s
                _reconnectInterval = min(_reconnectInterval * 2, (unsigned long)60000);
            }
            break;
    }
}

WiFiMode WiFiManager::getMode() const {
    return _mode;
}

String WiFiManager::getIP() const {
    if (_mode == WiFiMode::CONNECTED) {
        return WiFi.localIP().toString();
    } else if (_mode == WiFiMode::AP_MODE) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

String WiFiManager::getSSID() const {
    if (_mode == WiFiMode::AP_MODE) {
        return AP_SSID;
    }
    return _savedSSID;
}

void WiFiManager::setCredentials(const String& ssid, const String& password) {
    _prefs.begin("wifi", false);  // read-write
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", password);
    _prefs.end();

    _savedSSID = ssid;
    _savedPassword = password;
    _connectAttempts = 0;

    Serial.printf("[WiFi] Credentials saved for: %s\n", ssid.c_str());

    // Stop AP and try connecting
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    _startSTA();
}

bool WiFiManager::hasSavedCredentials() const {
    return _savedSSID.length() > 0;
}

void WiFiManager::clearCredentials() {
    _prefs.begin("wifi", false);
    _prefs.remove("ssid");
    _prefs.remove("pass");
    _prefs.end();
    _savedSSID = "";
    _savedPassword = "";
    Serial.println("[WiFi] Credentials cleared");
}

bool WiFiManager::isTimeSynced() const {
    return _timeSynced;
}

String WiFiManager::getTimestamp() const {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        return "no-time";
    }
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return String(buf);
}

void WiFiManager::_startAP() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);

    // Explicit IP config
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);
    delay(100);

    // Set TX power — some C3 Super Mini boards need lower power for stable RF
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // Channel 1, not hidden, max 4 connections, open network for testing
    bool result = WiFi.softAP(AP_SSID, nullptr, 1, 0, 4);
    _mode = WiFiMode::AP_MODE;

    Serial.printf("[WiFi] AP softAP() returned: %s\n", result ? "true" : "false");
    Serial.printf("[WiFi] AP started: %s (password: %s)\n", AP_SSID, AP_PASSWORD);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[WiFi] AP MAC: %s\n", WiFi.softAPmacAddress().c_str());

    // DNS server redirects all requests to the AP IP (captive portal)
    _dnsServer.start(53, "*", WiFi.softAPIP());
}

void WiFiManager::_startSTA() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str());
    _mode = WiFiMode::CONNECTING;
    _lastConnectAttempt = millis();
    Serial.printf("[WiFi] Connecting to %s...\n", _savedSSID.c_str());
}

void WiFiManager::_initNTP() {
    configTime(DEFAULT_UTC_OFFSET, DEFAULT_DST_OFFSET, NTP_SERVER);
    Serial.println("[WiFi] NTP sync started");
}

void WiFiManager::_updateLED() {
    static unsigned long lastBlink = 0;
    static bool ledState = false;

    switch (_mode) {
        case WiFiMode::CONNECTED:
            // Solid on
            digitalWrite(STATUS_LED_PIN, LOW);  // LED is active-low on Super Mini
            break;
        case WiFiMode::AP_MODE:
            // Fast blink (250ms)
            if (millis() - lastBlink > 250) {
                ledState = !ledState;
                digitalWrite(STATUS_LED_PIN, ledState ? LOW : HIGH);
                lastBlink = millis();
            }
            break;
        case WiFiMode::CONNECTING:
            // Slow blink (1000ms)
            if (millis() - lastBlink > 1000) {
                ledState = !ledState;
                digitalWrite(STATUS_LED_PIN, ledState ? LOW : HIGH);
                lastBlink = millis();
            }
            break;
        case WiFiMode::DISCONNECTED:
            // Off
            digitalWrite(STATUS_LED_PIN, HIGH);
            break;
    }
}
