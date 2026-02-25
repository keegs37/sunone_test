#ifndef MAKCU_KEYBOARD_MONITOR_H
#define MAKCU_KEYBOARD_MONITOR_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

#include "../modules/makcu/include/makcu.h"

// Monitors a physical keyboard connected to a second MAKCU device via USB3 passthrough.
// Uses MAKCU's keyboard streaming (km.stream.keyboard) to read HID keyboard reports,
// allowing physical keystroke detection without relying on Win32 GetAsyncKeyState().
class MakcuKeyboardMonitor
{
public:
    MakcuKeyboardMonitor(const std::string& port, unsigned int baud_rate);
    ~MakcuKeyboardMonitor();

    bool isOpen() const;

    // Returns true if the named key is currently pressed on the monitored keyboard.
    // key_name must match the names used in the config buttons (e.g. "A", "F1", "LeftShift").
    bool isKeyPressed(const std::string& key_name) const;

private:
    makcu::Device          device_;
    std::atomic<bool>      is_open_{ false };
    std::atomic<bool>      stop_thread_{ false };
    std::thread            monitor_thread_;

    mutable std::mutex     state_mutex_;
    uint8_t                modifier_byte_{ 0 };
    std::unordered_set<uint8_t> pressed_hid_codes_;

    void monitorLoop();
    void parseHidReport(const uint8_t* report, size_t len);

    // Maps a config key name to its USB HID Usage ID (keyboard page 0x07).
    // Returns 0 if the key is a modifier or not found in the HID table.
    static uint8_t getHidCode(const std::string& key_name);

    // Returns the modifier bitmask bit for keys like Ctrl/Shift/Alt.
    // Returns 0 if the key is not a modifier key.
    static uint8_t getModifierBit(const std::string& key_name);

    static const std::unordered_map<std::string, uint8_t>& hidMap();
    static const std::unordered_map<std::string, uint8_t>& modifierMap();
};

#endif // MAKCU_KEYBOARD_MONITOR_H
