#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <Arduino.h>
#include <functional>

// State machine for capturing data dumps from the 920i
enum class CaptureState {
    IDLE,       // Waiting for incoming data
    RECEIVING,  // Accumulating data into buffer
    COMPLETE    // Dump finished (timeout elapsed with no new data)
};

// Callback type: called when a complete dump is captured
using DumpCallback = std::function<void(const String& data, size_t length)>;

class SerialComm {
public:
    SerialComm();

    // Initialize the UART connection to the 920i
    void begin();

    // Call from loop() — processes incoming serial data
    void update();

    // Register a callback for when a dump is complete
    void onDumpComplete(DumpCallback callback);

    // Send a command to the 920i (appends CR terminator)
    void sendCommand(const String& command);

    // Get current state
    CaptureState getState() const;

    // Get the last completed dump data
    const String& getLastDump() const;

    // Get the timestamp of the last completed dump
    unsigned long getLastDumpTime() const;

    // Get total number of dumps captured this session
    uint32_t getDumpCount() const;

private:
    CaptureState _state;
    String _buffer;
    unsigned long _lastByteTime;
    unsigned long _lastDumpTime;
    uint32_t _dumpCount;
    String _lastDump;
    DumpCallback _dumpCallback;

    void _finalizeDump();
};

#endif // SERIAL_COMM_H
