#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <string>
#include <cstring>

#include "imgui/imgui.h"
#include "sunone_aimbot_cpp.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "overlay/ui_sections.h"
#include "MakcuKeyboard.h"

// Baud-rate options exposed in the UI
static const int kBaudRates[] = { 9600, 115200, 500000, 1000000, 2000000, 4000000 };
static const char* kBaudRateLabels[] = { "9600", "115200", "500000", "1000000", "2000000", "4000000" };
static const int kBaudRateCount = 6;

// ------------------------------------------------------------------
void draw_keyboard_monitor()
{
    if (OverlayUI::BeginSection("Second Makcu - Keyboard Monitor", "kb_monitor_section_main"))
    {
        ImGui::TextWrapped(
            "Connect a second Makcu device with a keyboard plugged in via USB pass-through. "
            "The device streams native HID keyboard events which are used by the key-binding system. "
            "All physical keys are detected automatically — no manual mapping needed."
        );
        ImGui::Spacing();

        // Enable / disable
        bool kbEnabled = config.makcu_kb_enabled;
        if (ImGui::Checkbox("Enable Keyboard Monitor", &kbEnabled))
        {
            config.makcu_kb_enabled = kbEnabled;
            OverlayConfig_MarkDirty();
            makcu_kb_changed.store(true);
        }

        ImGui::Spacing();

        // COM port
        {
            char portBuf[64] = {};
            std::strncpy(portBuf, config.makcu_kb_port.c_str(), sizeof(portBuf) - 1);
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("COM Port##kb_port", portBuf, sizeof(portBuf)))
            {
                config.makcu_kb_port = portBuf;
                OverlayConfig_MarkDirty();
            }
        }

        // Baud rate
        {
            int selectedBaud = 1; // default 115200
            for (int i = 0; i < kBaudRateCount; ++i)
            {
                if (kBaudRates[i] == config.makcu_kb_baudrate)
                {
                    selectedBaud = i;
                    break;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::Combo("Baud##kb_baud", &selectedBaud, kBaudRateLabels, kBaudRateCount))
            {
                config.makcu_kb_baudrate = kBaudRates[selectedBaud];
                OverlayConfig_MarkDirty();
            }
        }

        ImGui::Spacing();

        // Reconnect button
        if (ImGui::Button("Reconnect##kb_reconnect"))
        {
            makcu_kb_changed.store(true);
        }

        ImGui::SameLine();

        // Connection status indicator
        if (makcuKeyboard && makcuKeyboard->isOpen())
        {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Disconnected");
        }

        OverlayUI::EndSection();
    }

    // Live key display — only shown while connected
    if (makcuKeyboard && makcuKeyboard->isOpen())
    {
        if (OverlayUI::BeginSection("Live Key State", "kb_monitor_section_keys"))
        {
            ImGui::TextDisabled("Keys currently held on the monitored keyboard:");
            ImGui::Spacing();

            // Known key names to probe
            static const char* kAllKeyNames[] = {
                "A","B","C","D","E","F","G","H","I","J","K","L","M",
                "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
                "Key1","Key2","Key3","Key4","Key5","Key6","Key7","Key8","Key9","Key0",
                "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12",
                "Enter","Escape","Backspace","Tab","Space","CapsLock",
                "LeftArrow","RightArrow","UpArrow","DownArrow",
                "Home","End","PageUp","PageDown","Insert","Delete",
                "PrintScreen","Pause",
                "LeftControl","LeftShift","LeftAlt","LeftWindowsKey",
                "RightControl","RightShift","RightAlt","RightWindowsKey",
            };

            std::string pressedKeys;
            for (const char* name : kAllKeyNames)
            {
                if (makcuKeyboard->isKeyDown(name))
                {
                    if (!pressedKeys.empty()) pressedKeys += "  ";
                    pressedKeys += name;
                }
            }

            if (pressedKeys.empty())
                ImGui::TextDisabled("(none)");
            else
                ImGui::TextUnformatted(pressedKeys.c_str());

            OverlayUI::EndSection();
        }
    }
}
