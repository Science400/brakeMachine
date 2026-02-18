#include "data_uploader.h"
#include "config.h"
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFi.h>

DataUploader::DataUploader()
    : _nextFileId(1)
    , _lastRetryTime(0)
    , _retryInterval(UPLOAD_RETRY_INTERVAL_MS)
    , _consecutiveFailures(0)
    , _isConnected(nullptr) {
}

void DataUploader::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Uploader] LittleFS mount failed!");
        return;
    }

    if (!LittleFS.exists(QUEUE_DIR)) {
        LittleFS.mkdir(QUEUE_DIR);
    }

    _prefs.begin("uploader", false);
    _stats.receiverUrl = _prefs.getString("url", DEFAULT_RECEIVER_URL);
    _nextFileId = _prefs.getUInt("next_id", 1);
    _prefs.end();

    _stats.queueDepth = _countQueue();

    Serial.printf("[Uploader] Ready. Queue: %u pending\n", _stats.queueDepth);
    if (_stats.receiverUrl.length() > 0) {
        Serial.printf("[Uploader] URL: %s\n", _stats.receiverUrl.c_str());
    } else {
        Serial.println("[Uploader] No receiver URL configured — set via dashboard");
    }
}

void DataUploader::update() {
    if (_stats.queueDepth == 0) return;
    if (_isConnected && !_isConnected()) return;
    if (_stats.receiverUrl.length() == 0) return;
    if (millis() - _lastRetryTime < _retryInterval) return;

    _lastRetryTime = millis();
    Serial.printf("[Uploader] Retrying queue (%u pending, next in %lus)...\n",
                  _stats.queueDepth, _retryInterval / 1000);

    if (_retryOldest()) {
        _stats.queueDepth = _countQueue();
        _consecutiveFailures = 0;
        _retryInterval = UPLOAD_RETRY_INTERVAL_MS;
    } else {
        _consecutiveFailures++;
        // Backoff: 30s, 60s, 120s, max 5 minutes
        _retryInterval = min((unsigned long)UPLOAD_RETRY_INTERVAL_MS << _consecutiveFailures,
                             (unsigned long)300000);
    }
}

void DataUploader::submitDump(const String& data, size_t length, const String& timestamp) {
    uint32_t id = _nextFileId++;

    _prefs.begin("uploader", false);
    _prefs.putUInt("next_id", _nextFileId);
    _prefs.end();

    _lastDump.id = id;
    _lastDump.timestamp = timestamp;
    _lastDump.size = length;
    _lastDump.uploaded = false;
    _lastDump.preview = _extractPreview(data);

    if (_stats.receiverUrl.length() == 0) {
        Serial.printf("[Uploader] Dump #%u: no receiver URL configured, queuing\n", id);
        _stats.totalFailed++;
        _saveToDisk(data, id, timestamp);
        _stats.queueDepth = _countQueue();
        return;
    }

    // Try up to 3 times immediately before queuing
    bool uploaded = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (_attemptUpload(data, timestamp, id)) {
            uploaded = true;
            break;
        }
        if (attempt < 3) {
            Serial.printf("[Uploader] Dump #%u attempt %d failed, retrying...\n", id, attempt);
            delay(500);
        }
    }

    if (uploaded) {
        _lastDump.uploaded = true;
        _stats.totalSuccess++;
        _stats.lastUploadTime = timestamp;
        Serial.printf("[Uploader] Dump #%u uploaded OK\n", id);
    } else {
        _stats.totalFailed++;
        _saveToDisk(data, id, timestamp);
        _stats.queueDepth = _countQueue();
        Serial.printf("[Uploader] Dump #%u queued after 3 attempts\n", id);
    }
}

void DataUploader::setConnectivityCheck(ConnectivityCheck check) {
    _isConnected = check;
}

void DataUploader::setReceiverUrl(const String& url) {
    _stats.receiverUrl = url;
    _prefs.begin("uploader", false);
    _prefs.putString("url", url);
    _prefs.end();
    // Reset backoff so new URL gets tried quickly
    _consecutiveFailures = 0;
    _retryInterval = UPLOAD_RETRY_INTERVAL_MS;
    _lastRetryTime = 0;
    Serial.printf("[Uploader] Receiver URL set: %s\n", url.c_str());
}

String DataUploader::getReceiverUrl() const {
    return _stats.receiverUrl;
}

const UploadStats& DataUploader::getStats() const {
    return _stats;
}

const DumpRecord& DataUploader::getLastDump() const {
    return _lastDump;
}

bool DataUploader::_attemptUpload(const String& data, const String& timestamp, uint32_t id) {
    Serial.printf("[Uploader] POST %u bytes to %s\n", data.length(), _stats.receiverUrl.c_str());

    WiFiClient client;
    client.setTimeout(3);  // 3 second connect timeout

    HTTPClient http;
    if (!http.begin(client, _stats.receiverUrl)) {
        Serial.println("[Uploader] HTTP begin failed (bad URL?)");
        return false;
    }

    http.setTimeout(5000);  // 5 second response timeout
    http.addHeader("Content-Type", "text/tab-separated-values");
    http.addHeader("X-Device-Name", DEVICE_NAME);
    http.addHeader("X-Timestamp", timestamp);
    http.addHeader("X-Dump-Id", String(id));

    int code = http.POST((uint8_t*)data.c_str(), data.length());
    http.end();

    if (code >= 200 && code < 300) {
        return true;
    }

    Serial.printf("[Uploader] POST failed: %d\n", code);
    return false;
}

void DataUploader::_saveToDisk(const String& data, uint32_t id, const String& timestamp) {
    if (_countQueue() >= MAX_QUEUED_DUMPS) {
        Serial.println("[Uploader] Queue full, dropping dump");
        return;
    }

    String path = String(QUEUE_DIR) + "/" + String(id) + ".tsv";
    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.printf("[Uploader] Failed to write %s\n", path.c_str());
        return;
    }

    // Metadata header line — stripped before retry upload
    f.printf("# id=%u ts=%s sz=%u\n", id, timestamp.c_str(), data.length());
    f.print(data);
    f.close();

    Serial.printf("[Uploader] Saved to %s\n", path.c_str());
}

bool DataUploader::_retryOldest() {
    if (_stats.receiverUrl.length() == 0) return false;

    File dir = LittleFS.open(QUEUE_DIR);
    if (!dir || !dir.isDirectory()) return false;

    uint32_t lowestId = UINT32_MAX;
    String lowestName;

    File entry = dir.openNextFile();
    while (entry) {
        String name = String(entry.name());
        if (name.endsWith(".tsv")) {
            // Extract numeric ID from filename
            int lastSlash = name.lastIndexOf('/');
            String numPart = (lastSlash >= 0) ? name.substring(lastSlash + 1) : name;
            uint32_t fileId = numPart.toInt();
            if (fileId > 0 && fileId < lowestId) {
                lowestId = fileId;
                lowestName = name;
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();

    if (lowestName.isEmpty()) return false;

    // Build the full path
    String path = lowestName;
    if (!path.startsWith("/")) {
        path = String(QUEUE_DIR) + "/" + path;
    }

    File f = LittleFS.open(path, "r");
    if (!f) return false;

    // First line is metadata comment — parse timestamp from it
    String metaLine = f.readStringUntil('\n');
    String data = f.readString();
    f.close();

    // Parse timestamp from "# id=N ts=YYYY-MM-DDTHH:MM:SS sz=NNNNN"
    String timestamp = "retried";
    int tsIdx = metaLine.indexOf("ts=");
    if (tsIdx >= 0) {
        int spaceIdx = metaLine.indexOf(' ', tsIdx + 3);
        timestamp = (spaceIdx >= 0)
            ? metaLine.substring(tsIdx + 3, spaceIdx)
            : metaLine.substring(tsIdx + 3);
    }

    if (_attemptUpload(data, timestamp, lowestId)) {
        LittleFS.remove(path);
        _stats.totalSuccess++;
        Serial.printf("[Uploader] Retry #%u succeeded, removed from queue\n", lowestId);
        return true;
    }

    return false;
}

uint32_t DataUploader::_countQueue() {
    File dir = LittleFS.open(QUEUE_DIR);
    if (!dir || !dir.isDirectory()) return 0;

    uint32_t count = 0;
    File entry = dir.openNextFile();
    while (entry) {
        String name = String(entry.name());
        if (name.endsWith(".tsv")) count++;
        entry = dir.openNextFile();
    }
    dir.close();
    return count;
}

String DataUploader::_extractPreview(const String& data) {
    String preview;
    int pos = 0;
    for (int i = 0; i < DUMP_PREVIEW_LINES; i++) {
        int nl = data.indexOf('\n', pos);
        if (nl < 0) {
            preview += data.substring(pos);
            break;
        }
        if (i > 0) preview += '\n';
        preview += data.substring(pos, nl);
        pos = nl + 1;
    }
    return preview;
}
