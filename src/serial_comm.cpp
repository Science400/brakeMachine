#include "serial_comm.h"
#include "config.h"

SerialComm::SerialComm()
    : _state(CaptureState::IDLE)
    , _lastByteTime(0)
    , _lastDumpTime(0)
    , _dumpCount(0)
    , _dumpCallback(nullptr) {
}

void SerialComm::begin() {
    Serial1.begin(RS232_BAUD, RS232_CONFIG, RS232_RX_PIN, RS232_TX_PIN);
    _buffer.reserve(DUMP_BUFFER_SIZE);
    Serial.printf("[SerialComm] UART1 initialized: %d baud\n", RS232_BAUD);
}

void SerialComm::update() {
    // Read all available bytes from the 920i
    while (Serial1.available()) {
        char c = Serial1.read();
        _lastByteTime = millis();

        if (_state == CaptureState::IDLE) {
            _state = CaptureState::RECEIVING;
            _buffer = "";
            Serial.println("[SerialComm] Receiving data...");
        }

        // Guard against buffer overflow
        if (_buffer.length() < DUMP_BUFFER_SIZE) {
            _buffer += c;
        }
    }

    // Check for dump completion: in RECEIVING state and timeout elapsed
    if (_state == CaptureState::RECEIVING) {
        if (millis() - _lastByteTime >= DUMP_COMPLETE_TIMEOUT_MS) {
            _finalizeDump();
        }
    }
}

void SerialComm::onDumpComplete(DumpCallback callback) {
    _dumpCallback = callback;
}

void SerialComm::sendCommand(const String& command) {
    Serial1.print(command);
    Serial1.print(RS232_LINE_TERMINATOR);
    Serial.printf("[SerialComm] Sent: %s\n", command.c_str());
}

CaptureState SerialComm::getState() const {
    return _state;
}

const String& SerialComm::getLastDump() const {
    return _lastDump;
}

unsigned long SerialComm::getLastDumpTime() const {
    return _lastDumpTime;
}

uint32_t SerialComm::getDumpCount() const {
    return _dumpCount;
}

void SerialComm::_finalizeDump() {
    _state = CaptureState::COMPLETE;
    _lastDump = _buffer;
    _lastDumpTime = millis();
    _dumpCount++;

    Serial.printf("[SerialComm] Dump #%u complete: %u bytes\n",
                  _dumpCount, _buffer.length());

    if (_dumpCallback) {
        _dumpCallback(_buffer, _buffer.length());
    }

    _buffer = "";
    _state = CaptureState::IDLE;
}
