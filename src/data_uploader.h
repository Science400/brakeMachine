#ifndef DATA_UPLOADER_H
#define DATA_UPLOADER_H

#include <Arduino.h>
#include <Preferences.h>
#include <functional>

struct UploadStats {
    uint32_t totalSuccess = 0;
    uint32_t totalFailed = 0;
    uint32_t queueDepth = 0;
    String lastUploadTime;
    String receiverUrl;
};

struct DumpRecord {
    uint32_t id = 0;
    String timestamp;
    String preview;
    size_t size = 0;
    bool uploaded = false;
};

using ConnectivityCheck = std::function<bool()>;

class DataUploader {
public:
    DataUploader();

    // Call once at startup — mounts LittleFS, loads NVS config
    void begin();

    // Call from loop() — drives retry logic
    void update();

    // Called from onDumpReceived — tries upload, queues on failure
    void submitDump(const String& data, size_t length, const String& timestamp);

    // Set a function that returns true when WiFi is connected
    void setConnectivityCheck(ConnectivityCheck check);

    // Receiver URL management (persisted to NVS)
    void setReceiverUrl(const String& url);
    String getReceiverUrl() const;

    // Status accessors for dashboard
    const UploadStats& getStats() const;
    const DumpRecord& getLastDump() const;

private:
    UploadStats _stats;
    DumpRecord _lastDump;
    Preferences _prefs;
    uint32_t _nextFileId;
    unsigned long _lastRetryTime;
    unsigned long _retryInterval;
    uint8_t _consecutiveFailures;
    ConnectivityCheck _isConnected;

    bool _attemptUpload(const String& data, const String& timestamp, uint32_t id);
    void _saveToDisk(const String& data, uint32_t id, const String& timestamp);
    bool _retryOldest();
    uint32_t _countQueue();
    String _extractPreview(const String& data);
};

#endif // DATA_UPLOADER_H
