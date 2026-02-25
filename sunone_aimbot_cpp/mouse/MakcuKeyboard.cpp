#include "MakcuKeyboard.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// HID usage-code  →  key-name table
// The key names match the strings used in keycodes.h / config.ini so that
// isKeyDown() is compatible with the rest of the key-binding system.
// ---------------------------------------------------------------------------
static const std::unordered_map<uint8_t, std::string> kHidToKeyName = {
    // Letters A-Z
    {0x04,"A"}, {0x05,"B"}, {0x06,"C"}, {0x07,"D"}, {0x08,"E"}, {0x09,"F"},
    {0x0A,"G"}, {0x0B,"H"}, {0x0C,"I"}, {0x0D,"J"}, {0x0E,"K"}, {0x0F,"L"},
    {0x10,"M"}, {0x11,"N"}, {0x12,"O"}, {0x13,"P"}, {0x14,"Q"}, {0x15,"R"},
    {0x16,"S"}, {0x17,"T"}, {0x18,"U"}, {0x19,"V"}, {0x1A,"W"}, {0x1B,"X"},
    {0x1C,"Y"}, {0x1D,"Z"},
    // Digit row (top of keyboard)
    {0x1E,"Key1"}, {0x1F,"Key2"}, {0x20,"Key3"}, {0x21,"Key4"}, {0x22,"Key5"},
    {0x23,"Key6"}, {0x24,"Key7"}, {0x25,"Key8"}, {0x26,"Key9"}, {0x27,"Key0"},
    // Common control keys
    {0x28,"Enter"}, {0x29,"Escape"}, {0x2A,"Backspace"}, {0x2B,"Tab"},
    {0x2C,"Space"},  {0x39,"CapsLock"},
    // Function keys
    {0x3A,"F1"}, {0x3B,"F2"},  {0x3C,"F3"},  {0x3D,"F4"},
    {0x3E,"F5"}, {0x3F,"F6"},  {0x40,"F7"},  {0x41,"F8"},
    {0x42,"F9"}, {0x43,"F10"}, {0x44,"F11"}, {0x45,"F12"},
    // Navigation cluster
    {0x49,"Insert"},     {0x4A,"Home"},      {0x4B,"PageUp"},
    {0x4C,"Delete"},     {0x4D,"End"},       {0x4E,"PageDown"},
    // Arrow keys
    {0x4F,"RightArrow"}, {0x50,"LeftArrow"},
    {0x51,"DownArrow"},  {0x52,"UpArrow"},
    // Misc
    {0x46,"PrintScreen"}, {0x48,"Pause"},
    // Numpad
    {0x53,"NumpadKey0"}, {0x54,"NumpadKey1"}, {0x55,"NumpadKey2"},
    {0x56,"NumpadKey3"}, {0x57,"NumpadKey4"}, {0x58,"NumpadKey5"},
    {0x59,"NumpadKey6"}, {0x5A,"NumpadKey7"}, {0x5B,"NumpadKey8"},
    {0x5C,"NumpadKey9"},
    {0x55,"Multiply"}, {0x57,"Add"}, {0x56,"Subtract"}, {0x54,"Divide"},
    // Modifier keys – encoded as virtual HID codes 0xE0-0xE7
    // (these are set from the modifier byte, not the key slots)
    {0xE0,"LeftControl"}, {0xE1,"LeftShift"},  {0xE2,"LeftAlt"},
    {0xE3,"LeftWindowsKey"},
    {0xE4,"RightControl"},{0xE5,"RightShift"}, {0xE6,"RightAlt"},
    {0xE7,"RightWindowsKey"},
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MakcuKeyboardMonitor::MakcuKeyboardMonitor(const std::string& port,
                                           unsigned int        baud_rate)
{
    // Initialise all key states to false
    for (auto& s : hid_key_states_)
        s.store(false);

    // Build reverse lookup: name → HID code
    for (const auto& kv : kHidToKeyName)
        key_name_to_hid_[kv.second] = kv.first;

    try
    {
        if (!device_.connect(port))
        {
            std::cerr << "[MakcuKeyboard] Failed to connect on " << port << std::endl;
            return;
        }

        if (baud_rate > 0)
            device_.setBaudRate(baud_rate, true);

        // Enable keyboard event streaming on the KMBox Pro device
        device_.sendRawCommand("km.keyboard_listen(1)\n");

        is_open_.store(true);
        reader_thread_ = std::thread(&MakcuKeyboardMonitor::readerThread, this);

        std::cout << "[MakcuKeyboard] Connected on " << port << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[MakcuKeyboard] Exception during connect: " << e.what() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
MakcuKeyboardMonitor::~MakcuKeyboardMonitor()
{
    stop_thread_.store(true);

    try
    {
        // Send a stop command so the device stops streaming and the reader
        // thread can unblock from receiveRawResponse().
        device_.sendRawCommand("km.keyboard_listen(0)\n");
        device_.disconnect();
    }
    catch (...) {}

    if (reader_thread_.joinable())
        reader_thread_.join();
}

// ---------------------------------------------------------------------------
// Background reader thread
// Calls receiveRawResponse() in a loop; each non-empty string is a keyboard
// event packet and is passed to parsePacket().
// ---------------------------------------------------------------------------
void MakcuKeyboardMonitor::readerThread()
{
    while (!stop_thread_.load())
    {
        try
        {
            std::string line = device_.receiveRawResponse();
            if (!line.empty())
                parsePacket(line);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        catch (const std::exception& e)
        {
            if (!stop_thread_.load())
                std::cerr << "[MakcuKeyboard] Reader error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        catch (...)
        {
            if (!stop_thread_.load())
                std::cerr << "[MakcuKeyboard] Reader unknown error" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

// ---------------------------------------------------------------------------
// parsePacket
// Expected format from the KMBox Pro protocol:
//   km.keyboard_status(modifier,reserved,k0,k1,k2,k3,k4,k5)
// where all values are decimal integers.
// modifier is a bitmask: bit0=LeftCtrl, bit1=LeftShift, bit2=LeftAlt,
//   bit3=LeftWin, bit4=RightCtrl, bit5=RightShift, bit6=RightAlt,
//   bit7=RightWin.
// k0-k5 are HID usage codes of currently pressed keys (0 = empty slot).
// ---------------------------------------------------------------------------
void MakcuKeyboardMonitor::parsePacket(const std::string& line)
{
    // Quick prefix check
    const std::string prefix = "km.keyboard_status(";
    auto ppos = line.find(prefix);
    if (ppos == std::string::npos)
        return;

    // Extract the content between parentheses
    auto start = ppos + prefix.size();
    auto end   = line.find(')', start);
    if (end == std::string::npos)
        return;

    std::string inner = line.substr(start, end - start);

    // Parse comma-separated integers
    std::vector<int> vals;
    vals.reserve(8);
    std::stringstream ss(inner);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        try { vals.push_back(std::stoi(token)); }
        catch (...) { vals.push_back(0); }
    }

    if (vals.size() < 8)
        vals.resize(8, 0);

    // Clear all current key states
    for (auto& s : hid_key_states_)
        s.store(false);

    // Modifier byte (vals[0]): bits 0-7 → HID codes 0xE0-0xE7
    uint8_t mod = static_cast<uint8_t>(vals[0]);
    for (int bit = 0; bit < 8; ++bit)
    {
        if (mod & (1 << bit))
            hid_key_states_[0xE0 + bit].store(true);
    }

    // Key slots vals[2]-vals[7]
    for (int i = 2; i < 8 && i < static_cast<int>(vals.size()); ++i)
    {
        int code = vals[i];
        if (code > 0 && code < 256)
            hid_key_states_[static_cast<uint8_t>(code)].store(true);
    }
}

// ---------------------------------------------------------------------------
// isKeyDown
// ---------------------------------------------------------------------------
bool MakcuKeyboardMonitor::isKeyDown(const std::string& keyName) const
{
    auto it = key_name_to_hid_.find(keyName);
    if (it == key_name_to_hid_.end())
        return false;
    return hid_key_states_[it->second].load();
}
