#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>
#include <iostream>

#include "MakcuKeyboardMonitor.h"

// ---------------------------------------------------------------------------
// HID Usage Table (Keyboard/Keypad page 0x07)
// Maps config key names to USB HID key codes.
// ---------------------------------------------------------------------------
const std::unordered_map<std::string, uint8_t>& MakcuKeyboardMonitor::hidMap()
{
    static const std::unordered_map<std::string, uint8_t> s_map = {
        // Letters
        { "A", 0x04 }, { "B", 0x05 }, { "C", 0x06 }, { "D", 0x07 },
        { "E", 0x08 }, { "F", 0x09 }, { "G", 0x0A }, { "H", 0x0B },
        { "I", 0x0C }, { "J", 0x0D }, { "K", 0x0E }, { "L", 0x0F },
        { "M", 0x10 }, { "N", 0x11 }, { "O", 0x12 }, { "P", 0x13 },
        { "Q", 0x14 }, { "R", 0x15 }, { "S", 0x16 }, { "T", 0x17 },
        { "U", 0x18 }, { "V", 0x19 }, { "W", 0x1A }, { "X", 0x1B },
        { "Y", 0x1C }, { "Z", 0x1D },
        // Digits (top row)
        { "Key1", 0x1E }, { "Key2", 0x1F }, { "Key3", 0x20 },
        { "Key4", 0x21 }, { "Key5", 0x22 }, { "Key6", 0x23 },
        { "Key7", 0x24 }, { "Key8", 0x25 }, { "Key9", 0x26 },
        { "Key0", 0x27 },
        // Common keys
        { "Enter",       0x28 },
        { "Escape",      0x29 },
        { "Backspace",   0x2A },
        { "Tab",         0x2B },
        { "Space",       0x2C },
        { "CapsLock",    0x39 },
        // Function keys
        { "F1",  0x3A }, { "F2",  0x3B }, { "F3",  0x3C }, { "F4",  0x3D },
        { "F5",  0x3E }, { "F6",  0x3F }, { "F7",  0x40 }, { "F8",  0x41 },
        { "F9",  0x42 }, { "F10", 0x43 }, { "F11", 0x44 }, { "F12", 0x45 },
        // Navigation cluster
        { "PrintScreen", 0x46 },
        { "Pause",       0x48 },
        { "Ins",         0x49 },
        { "Home",        0x4A },
        { "PageUp",      0x4B },
        { "Delete",      0x4C },
        { "End",         0x4D },
        { "PageDown",    0x4E },
        // Arrow keys
        { "RightArrow",  0x4F },
        { "LeftArrow",   0x50 },
        { "DownArrow",   0x51 },
        { "UpArrow",     0x52 },
        // Numpad
        { "NumLock",     0x53 },
        { "Numpad0",     0x62 }, { "Numpad1", 0x59 }, { "Numpad2", 0x5A },
        { "Numpad3",     0x5B }, { "Numpad4", 0x5C }, { "Numpad5", 0x5D },
        { "Numpad6",     0x5E }, { "Numpad7", 0x5F }, { "Numpad8", 0x60 },
        { "Numpad9",     0x61 },
    };
    return s_map;
}

// ---------------------------------------------------------------------------
// Modifier bitmask table (HID keyboard modifier byte, byte 0 of the report)
// Bit 0 = Left Ctrl, Bit 1 = Left Shift, Bit 2 = Left Alt, Bit 3 = Left GUI
// Bit 4 = Right Ctrl, Bit 5 = Right Shift, Bit 6 = Right Alt, Bit 7 = Right GUI
// ---------------------------------------------------------------------------
const std::unordered_map<std::string, uint8_t>& MakcuKeyboardMonitor::modifierMap()
{
    static const std::unordered_map<std::string, uint8_t> s_map = {
        { "LeftCtrl",    0x01 },
        { "LeftShift",   0x02 },
        { "LeftAlt",     0x04 },
        { "LeftWindowsKey",  0x08 },
        { "RightCtrl",   0x10 },
        { "RightShift",  0x20 },
        { "RightAlt",    0x40 },
        { "RightWindowsKey", 0x80 },
    };
    return s_map;
}

uint8_t MakcuKeyboardMonitor::getHidCode(const std::string& key_name)
{
    const auto& m = hidMap();
    auto it = m.find(key_name);
    return (it != m.end()) ? it->second : 0;
}

uint8_t MakcuKeyboardMonitor::getModifierBit(const std::string& key_name)
{
    const auto& m = modifierMap();
    auto it = m.find(key_name);
    return (it != m.end()) ? it->second : 0;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
MakcuKeyboardMonitor::MakcuKeyboardMonitor(const std::string& port, unsigned int baud_rate)
{
    try
    {
        if (!device_.connect(port))
        {
            std::cerr << "[MakcuKeyboard] Unable to connect to port: " << port << std::endl;
            return;
        }

        // Keyboard streaming requires >= 1 Mbps. Upgrade baud rate if necessary.
        const unsigned int minStreamBaud = 1000000u;
        unsigned int targetBaud = (baud_rate < minStreamBaud) ? minStreamBaud : baud_rate;
        if (!device_.setBaudRate(targetBaud, true))
        {
            std::cerr << "[MakcuKeyboard] Failed to set baud rate to " << targetBaud
                      << ", continuing at current rate." << std::endl;
        }

        // Enable keyboard streaming: mode 1 (raw HID), 5 ms period.
        if (!device_.sendRawCommand("km.stream.keyboard(1,5)"))
        {
            std::cerr << "[MakcuKeyboard] Failed to enable keyboard streaming." << std::endl;
            device_.disconnect();
            return;
        }

        is_open_.store(true);
        std::cout << "[MakcuKeyboard] Connected! PORT: " << port << std::endl;

        stop_thread_.store(false);
        monitor_thread_ = std::thread(&MakcuKeyboardMonitor::monitorLoop, this);
    }
    catch (const makcu::MakcuException& e)
    {
        std::cerr << "[MakcuKeyboard] Error: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[MakcuKeyboard] Error: " << e.what() << std::endl;
    }
}

MakcuKeyboardMonitor::~MakcuKeyboardMonitor()
{
    stop_thread_.store(true);
    is_open_.store(false);

    try { device_.disconnect(); } catch (...) {}

    if (monitor_thread_.joinable())
        monitor_thread_.join();
}

bool MakcuKeyboardMonitor::isOpen() const
{
    return is_open_.load() && device_.isConnected();
}

// ---------------------------------------------------------------------------
// Streaming monitor loop
// ---------------------------------------------------------------------------
void MakcuKeyboardMonitor::monitorLoop()
{
    // The MAKCU firmware prefixes keyboard HID frames with "km.keyboard"
    // followed by 8 raw bytes: [modifier, reserved, key0..key5].
    static constexpr char kKbPrefix[] = "km.keyboard";
    static constexpr size_t kPrefixLen = sizeof(kKbPrefix) - 1; // 11 chars
    static constexpr size_t kReportLen = 8;

    while (!stop_thread_.load())
    {
        try
        {
            std::string response = device_.receiveRawResponse();
            if (response.empty())
                continue;

            // Scan for the keyboard frame prefix in the response.
            size_t pos = response.find(kKbPrefix);
            while (pos != std::string::npos)
            {
                size_t dataStart = pos + kPrefixLen;
                if (dataStart + kReportLen <= response.size())
                {
                    parseHidReport(
                        reinterpret_cast<const uint8_t*>(response.data() + dataStart),
                        kReportLen
                    );
                }
                pos = response.find(kKbPrefix, pos + 1);
            }
        }
        catch (const std::exception& e)
        {
            if (!stop_thread_.load())
            {
                std::cerr << "[MakcuKeyboard] Monitor error: " << e.what() << std::endl;
                is_open_.store(false);
                break;
            }
        }
        catch (...)
        {
            if (!stop_thread_.load())
            {
                std::cerr << "[MakcuKeyboard] Monitor unknown error." << std::endl;
                is_open_.store(false);
                break;
            }
        }
    }
}

void MakcuKeyboardMonitor::parseHidReport(const uint8_t* report, size_t len)
{
    if (len < 8) return;

    std::lock_guard<std::mutex> lock(state_mutex_);

    // Byte 0: modifier keys bitmask
    modifier_byte_ = report[0];
    // Byte 1: reserved

    // Bytes 2-7: up to 6 simultaneous key codes (0x00 = no key)
    pressed_hid_codes_.clear();
    for (size_t i = 2; i < 8 && i < len; ++i)
    {
        if (report[i] != 0x00)
            pressed_hid_codes_.insert(report[i]);
    }
}

// ---------------------------------------------------------------------------
// Key query
// ---------------------------------------------------------------------------
bool MakcuKeyboardMonitor::isKeyPressed(const std::string& key_name) const
{
    // Check modifier keys first (they live in the modifier byte, not the key array)
    uint8_t modBit = getModifierBit(key_name);
    if (modBit != 0)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return (modifier_byte_ & modBit) != 0;
    }

    // Regular key: look up HID code and check presence in the pressed set
    uint8_t hidCode = getHidCode(key_name);
    if (hidCode == 0)
        return false; // Key name not in HID table (e.g. mouse buttons)

    std::lock_guard<std::mutex> lock(state_mutex_);
    return pressed_hid_codes_.count(hidCode) > 0;
}
