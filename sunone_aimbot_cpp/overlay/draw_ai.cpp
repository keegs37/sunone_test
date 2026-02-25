#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "imgui/imgui.h"

#include "sunone_aimbot_cpp.h"
#include "include/other_tools.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "draw_settings.h"
#include "overlay/ui_sections.h"
#ifdef USE_CUDA
#include "trt_monitor.h"
#endif

namespace
{
    constexpr float kAiInputW  = 72.0f;
    constexpr float kAiSpacing = 4.0f;

    bool SliderFloatInput(const char* label, float* v, float vMin, float vMax, const char* fmt = "%.2f")
    {
        bool changed = false;
        const float avail  = ImGui::GetContentRegionAvail().x;
        const float sliderW = std::max(60.0f, avail - kAiInputW - kAiSpacing);

        ImGui::PushID(label);
        ImGui::SetNextItemWidth(sliderW);
        changed |= ImGui::SliderFloat("##s", v, vMin, vMax, fmt);
        ImGui::SameLine(0.0f, kAiSpacing);
        ImGui::SetNextItemWidth(kAiInputW);
        changed |= ImGui::InputFloat("##i", v, 0.0f, 0.0f, fmt);
        *v = std::clamp(*v, vMin, vMax);
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        return changed;
    }
}

std::string prev_backend = config.backend;
float prev_confidence_threshold      = config.confidence_threshold;
float prev_head_confidence_threshold = config.head_confidence_threshold;
float prev_body_confidence_threshold = config.body_confidence_threshold;
float prev_nms_threshold = config.nms_threshold;
int prev_max_detections = config.max_detections;

static bool wasExporting = false;
static bool ai_state_initialized = false;

void draw_ai()
{
    if (!ai_state_initialized)
    {
        prev_backend = config.backend;
        prev_confidence_threshold      = config.confidence_threshold;
        prev_head_confidence_threshold = config.head_confidence_threshold;
        prev_body_confidence_threshold = config.body_confidence_threshold;
        prev_nms_threshold = config.nms_threshold;
        prev_max_detections = config.max_detections;
        ai_state_initialized = true;
    }

#ifdef USE_CUDA
    if (gIsTrtExporting)
    {
        ImGui::OpenPopup("TensorRT Export Progress");
    }

    if (ImGui::BeginPopupModal("TensorRT Export Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        bool hasPhases = false;
        {
            std::lock_guard<std::mutex> lock(gProgressMutex);
            hasPhases = !gProgressPhases.empty();
            if (hasPhases)
            {
                for (auto& [name, phase] : gProgressPhases)
                {
                    float percent = phase.max > 0 ? phase.current / float(phase.max) : 0.0f;
                    ImGui::Text("%s: %d/%d", name.c_str(), phase.current, phase.max);
                    ImGui::ProgressBar(percent, ImVec2(300, 0));
                }
            }
        }
        if (!hasPhases)
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::Text("Engine export in progress, please wait...");
        long long lastUpdate = gTrtExportLastUpdateMs.load();
        if (lastUpdate > 0)
        {
            double secondsSince = (TrtNowMs() - lastUpdate) / 1000.0;
            ImGui::Text("Last progress update: %.1f s ago", secondsSince);
        }
        bool cancelRequested = gTrtExportCancelRequested.load();
        if (cancelRequested)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Cancel export"))
        {
            gTrtExportCancelRequested = true;
        }
        if (cancelRequested)
        {
            ImGui::EndDisabled();
            ImGui::Text("Cancel requested...");
        }
        ImGui::EndPopup();
    }
#endif
    std::vector<std::string> availableModels = getAvailableModels();
    if (OverlayUI::BeginSection("Model", "ai_section_model"))
    {
        if (availableModels.empty())
        {
            ImGui::Text("No models available in the 'models' folder.");
        }
        else
        {
            int currentModelIndex = 0;
            auto it = std::find(availableModels.begin(), availableModels.end(), config.ai_model);

            if (it != availableModels.end())
            {
                currentModelIndex = static_cast<int>(std::distance(availableModels.begin(), it));
            }

            std::vector<const char*> modelsItems;
            modelsItems.reserve(availableModels.size());

            for (const auto& modelName : availableModels)
            {
                modelsItems.push_back(modelName.c_str());
            }

            if (ImGui::Combo("Model", &currentModelIndex, modelsItems.data(), static_cast<int>(modelsItems.size())))
            {
                if (config.ai_model != availableModels[currentModelIndex])
                {
                    config.ai_model = availableModels[currentModelIndex];
                    OverlayConfig_MarkDirty();
                    detector_model_changed.store(true);
                }
            }
            ImGui::SameLine();
            ImGui::Text("Fixed model size: %s", config.fixed_input_size ? "Enabled" : "Disabled");
        }
        OverlayUI::EndSection();
    }

#ifdef USE_CUDA
    if (OverlayUI::BeginSection("Backend", "ai_section_backend"))
    {
        std::vector<std::string> backendOptions = { "TRT", "DML" };
        std::vector<const char*> backendItems = { "TensorRT (CUDA)", "DirectML (CPU/GPU)" };

        int currentBackendIndex = config.backend == "DML" ? 1 : 0;

        if (ImGui::Combo("Backend", &currentBackendIndex, backendItems.data(), static_cast<int>(backendItems.size())))
        {
            std::string newBackend = backendOptions[currentBackendIndex];
            if (config.backend != newBackend)
            {
                config.backend = newBackend;
                OverlayConfig_MarkDirty();
                detector_model_changed.store(true);
            }
        }
        OverlayUI::EndSection();
    }
#endif

    if (OverlayUI::BeginSection("Detection", "ai_section_detection"))
    {
        SliderFloatInput("Body Confidence Threshold", &config.body_confidence_threshold, 0.01f, 1.00f, "%.2f");
        SliderFloatInput("Head Confidence Threshold", &config.head_confidence_threshold, 0.01f, 1.00f, "%.2f");
        ImGui::TextDisabled("Set per-class minimum confidence for detection. Head/body use separate thresholds.");
        ImGui::SliderFloat("NMS Threshold", &config.nms_threshold, 0.00f, 1.00f, "%.2f");
        ImGui::SliderInt("Max Detections", &config.max_detections, 1, 100);
        OverlayUI::EndSection();
    }

    draw_depth();
        
    if (prev_confidence_threshold      != config.confidence_threshold      ||
        prev_head_confidence_threshold != config.head_confidence_threshold ||
        prev_body_confidence_threshold != config.body_confidence_threshold ||
        prev_nms_threshold             != config.nms_threshold             ||
        prev_max_detections            != config.max_detections)
    {
        prev_confidence_threshold      = config.confidence_threshold;
        prev_head_confidence_threshold = config.head_confidence_threshold;
        prev_body_confidence_threshold = config.body_confidence_threshold;
        prev_nms_threshold             = config.nms_threshold;
        prev_max_detections            = config.max_detections;
        // Keep the legacy confidence_threshold in sync with the minimum of the two thresholds
        // so existing code paths that use it directly continue to work.
        config.confidence_threshold = std::min(config.head_confidence_threshold,
                                               config.body_confidence_threshold);
        OverlayConfig_MarkDirty();
    }

    if (prev_backend != config.backend)
    {
        prev_backend = config.backend;
        detector_model_changed.store(true);
        OverlayConfig_MarkDirty();
    }
}
