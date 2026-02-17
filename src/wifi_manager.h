#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

enum class WiFiMode {
    CONNECTING,   // Trying to connect to saved network
    CONNECTED,    // Connected to WiFi
    AP_MODE,      // Running as access point (captive portal)
    DISCONNECTED  // Lost connection, will retry
};

class WiFiManager {
public:
    WiFiManager();

    // Initialize WiFi — tries saved credentials, falls back to AP mode
    void begin();

    // Call from loop() — handles reconnection and DNS for captive portal
    void update();

    // Get current mode
    WiFiMode getMode() const;

    // Get IP address as string
    String getIP() const;

    // Get SSID we're connected to (or AP SSID if in AP mode)
    String getSSID() const;

    // Save new WiFi credentials and attempt connection
    void setCredentials(const String& ssid, const String& password);

    // Check if we have saved credentials
    bool hasSavedCredentials() const;

    // Clear saved credentials
    void clearCredentials();

    // Is NTP time synced?
    bool isTimeSynced() const;

    // Get formatted timestamp string
    String getTimestamp() const;

private:
    WiFiMode _mode;
    Preferences _prefs;
    DNSServer _dnsServer;
    String _savedSSID;
    String _savedPassword;
    unsigned long _lastConnectAttempt;
    unsigned long _reconnectInterval;
    uint8_t _connectAttempts;
    bool _timeSynced;

    void _startAP();
    void _startSTA();
    void _initNTP();
    void _updateLED();
};

#endif // WIFI_MANAGER_H
