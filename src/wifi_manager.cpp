#include "wifi_manager.h"
#include "config.h"
#include <time.h>
#include <nvs_flash.h>
#include <esp_wifi.h>

// WiFi event handler — logs actual reason codes for connection failures
static void _wifiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            const char* tag = "unknown";
            switch (reason) {
                case 2:   tag = "AUTH_EXPIRE"; break;
                case 3:   tag = "AUTH_LEAVE"; break;
                case 4:   tag = "ASSOC_EXPIRE"; break;
                case 6:   tag = "NOT_AUTHED"; break;
                case 7:   tag = "NOT_ASSOCED"; break;
                case 8:   tag = "ASSOC_LEAVE"; break;
                case 15:  tag = "4WAY_HANDSHAKE_TIMEOUT"; break;
                case 200: tag = "BEACON_TIMEOUT"; break;
                case 201: tag = "NO_AP_FOUND"; break;
                case 202: tag = "AUTH_FAIL"; break;
                case 203: tag = "ASSOC_FAIL"; break;
                case 204: tag = "HANDSHAKE_TIMEOUT"; break;
                case 205: tag = "CONNECTION_FAIL"; break;
            }
            Serial.printf("[WiFi] STA disconnected — reason %d (%s)\n", reason, tag);
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WiFi] Got IP: %s\n", WiFi.localIP().toString().c_str());
            break;
        default:
            break;
    }
}

WiFiManager::WiFiManager()
    : _mode(WiFiMode::DISCONNECTED)
    , _lastConnectAttempt(0)
    , _reconnectInterval(5000)
    , _connectAttempts(0)
    , _timeSynced(false) {
}

void WiFiManager::begin() {
    // Register WiFi event handler for diagnostics
    WiFi.onEvent(_wifiEventHandler);

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
    _savedSSID.trim();
    _savedPassword.trim();
    _prefs.end();

    if (_savedSSID.length() > 0) {
        Serial.printf("[WiFi] Saved network: %s\n", _savedSSID.c_str());
        _scanNetworks();
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
            // Process DNS while in AP+STA mode
            if (WiFi.getMode() == WIFI_AP_STA) {
                _dnsServer.processNextRequest();
            }
            if (WiFi.status() == WL_CONNECTED) {
                _mode = WiFiMode::CONNECTED;
                _connectAttempts = 0;
                _reconnectInterval = 5000;
                Serial.printf("[WiFi] Connected! IP: %s  RSSI: %d dBm\n",
                              WiFi.localIP().toString().c_str(), WiFi.RSSI());

                // If we were in AP+STA mode, shut down the AP
                if (WiFi.getMode() == WIFI_AP_STA) {
                    _dnsServer.stop();
                    WiFi.softAPdisconnect(true);
                    WiFi.mode(WIFI_STA);
                    Serial.println("[WiFi] AP shut down (STA connected)");
                }

                // Start mDNS
                if (MDNS.begin(DEVICE_HOSTNAME)) {
                    MDNS.addService("http", "tcp", WEB_SERVER_PORT);
                    Serial.printf("[WiFi] mDNS: http://%s.local\n", DEVICE_HOSTNAME);
                }

                _initNTP();
            } else if (millis() - _lastConnectAttempt > 20000) {
                // Connection attempt timed out (20s per attempt)
                _connectAttempts++;
                Serial.printf("[WiFi] Attempt %d timed out (status=%d)\n",
                              _connectAttempts, WiFi.status());

                // Scan every 3rd attempt to check signal
                if (_connectAttempts % 3 == 0) {
                    _scanNetworks();
                }

                if (_connectAttempts >= 5 && WiFi.getMode() != WIFI_AP_STA) {
                    // Start AP alongside STA so dashboard is reachable while retrying
                    Serial.println("[WiFi] Starting AP+STA for dashboard access");
                    _startAPSTA();
                } else {
                    // Retry — preserve AP+STA if already in that mode
                    _retrySTA();
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
                    Serial.printf("[WiFi] NTP synced: %s\n", getTimestamp().c_str());
                }
            }
            break;

        case WiFiMode::DISCONNECTED:
            if (_savedSSID.length() > 0 && millis() - _lastConnectAttempt > _reconnectInterval) {
                Serial.println("[WiFi] Attempting reconnect...");
                _connectAttempts = 0;
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
    // Trim whitespace — trailing spaces from form input will break matching
    String trimmedSSID = ssid;
    String trimmedPass = password;
    trimmedSSID.trim();
    trimmedPass.trim();

    _prefs.begin("wifi", false);  // read-write
    _prefs.putString("ssid", trimmedSSID);
    _prefs.putString("pass", trimmedPass);
    _prefs.end();

    _savedSSID = trimmedSSID;
    _savedPassword = trimmedPass;
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
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    // Don't limit TX power in STA mode — need full power to reach the router
    WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str());
    _mode = WiFiMode::CONNECTING;
    _lastConnectAttempt = millis();
    Serial.printf("[WiFi] Connecting to %s...\n", _savedSSID.c_str());
}

void WiFiManager::_retrySTA() {
    // Retry without changing WiFi mode — preserves AP+STA if active
    WiFi.disconnect(false);  // disconnect STA but don't erase config
    delay(200);
    WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str());
    _lastConnectAttempt = millis();
    Serial.printf("[WiFi] Retrying %s... (attempt %d, mode=%s)\n",
                  _savedSSID.c_str(), _connectAttempts + 1,
                  WiFi.getMode() == WIFI_AP_STA ? "AP+STA" : "STA");
}

void WiFiManager::_startAPSTA() {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    // Configure AP side
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);
    // No TX power limit — need full power for STA to reach the router
    WiFi.softAP(AP_SSID, nullptr, 1, 0, 4);
    _dnsServer.start(53, "*", WiFi.softAPIP());

    // Start STA connection attempt
    WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str());
    _mode = WiFiMode::CONNECTING;
    _lastConnectAttempt = millis();

    Serial.printf("[WiFi] AP+STA mode — dashboard at %s, still trying %s\n",
                  WiFi.softAPIP().toString().c_str(), _savedSSID.c_str());
}

void WiFiManager::_scanNetworks() {
    Serial.println("[WiFi] Scanning...");
    WiFi.mode(WIFI_STA);
    delay(100);
    int n = WiFi.scanNetworks(false, false, false, 300);  // active scan, 300ms/channel
    if (n <= 0) {
        Serial.println("[WiFi] No networks found!");
    } else {
        bool found = false;
        for (int i = 0; i < n; i++) {
            Serial.printf("[WiFi]   %-20s  ch%-2d  %d dBm  %s\n",
                          WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "encrypted");
            if (WiFi.SSID(i) == _savedSSID) {
                found = true;
            }
        }
        if (!found) {
            Serial.printf("[WiFi] WARNING: '%s' not found in scan results!\n", _savedSSID.c_str());
        }
    }
    WiFi.scanDelete();
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
