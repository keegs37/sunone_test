#ifndef MAKCUKEYBOARD_H
#define MAKCUKEYBOARD_H

#include <string>
#include <atomic>
#include <thread>
#include <array>
#include <unordered_map>

#include "../modules/makcu/include/makcu.h"

// Monitors a keyboard plugged into a second Makcu via USB pass-through.
// Uses the KMBox Pro raw protocol: sends km.keyboard_listen(1) and parses
// HID keyboard report packets streamed from the device's serial port.
class MakcuKeyboardMonitor
{
public:
    MakcuKeyboardMonitor(const std::string& port, unsigned int baud_rate);
    ~MakcuKeyboardMonitor();

    bool isOpen() const { return is_open_.load(); }

    // Returns true if the key with the given name is currently held down.
    // Key names match the strings used in config (e.g. "A", "F1", "LeftShift").
    bool isKeyDown(const std::string& keyName) const;

private:
    void readerThread();
    void parsePacket(const std::string& line);

    makcu::Device              device_;
    std::atomic<bool>          is_open_{ false };
    std::atomic<bool>          stop_thread_{ false };
    std::thread                reader_thread_;

    // HID usage codes 0x00-0xFF — true when that key is currently pressed.
    std::array<std::atomic<bool>, 256> hid_key_states_{};

    // key name (same strings as keycodes.h) → HID usage code
    std::unordered_map<std::string, uint8_t> key_name_to_hid_;
};

#endif // MAKCUKEYBOARD_H
