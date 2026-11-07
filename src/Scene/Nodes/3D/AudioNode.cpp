#include "AudioNode.h"
#include "Scene/Scene.h"
#include "Audio/AudioEngine.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

AudioNode::AudioNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::AudioSource;
    parentId = pId;
    name = nodeName.empty() ? "Audio Source" : nodeName;
    color = glm::vec4(0.35f, 0.90f, 0.55f, 1.0f);
}

std::shared_ptr<Node> AudioNode::Clone() const {
    return std::make_shared<AudioNode>(*this);
}

void AudioNode::ComputeSpatialGainAndPan(const Scene& scene,
                                         const glm::vec3& listenerPos,
                                         const glm::vec3& listenerRight,
                                         float& outLeftGain,
                                         float& outRightGain) const {
    float baseGain = std::clamp(volume, 0.0f, 2.0f);
    if (!is3DSpatial) {
        outLeftGain = baseGain;
        outRightGain = baseGain;
        return;
    }

    glm::vec3 srcPos = GetWorldPosition(scene);
    glm::vec3 toSrc = srcPos - listenerPos;
    float dist = glm::length(toSrc);

    float minD = std::max(0.01f, minDistance);
    float maxD = std::max(minD + 0.01f, maxDistance);
    float dClamped = std::clamp(dist, minD, maxD);

    float atten = 1.0f;
    switch (attenuationModel) {
        case AudioAttenuationModel::Linear:
            atten = 1.0f - rolloffFactor * ((dClamped - minD) / (maxD - minD));
            break;
        case AudioAttenuationModel::Exponential:
            atten = std::pow(dClamped / minD, -rolloffFactor);
            break;
        case AudioAttenuationModel::InverseDistance:
        default:
            if (dist >= maxD) {
                atten = 0.0f;
            } else {
                atten = minD / (minD + rolloffFactor * (dClamped - minD));
            }
            break;
    }
    atten = std::clamp(atten, 0.0f, 1.0f);

    // Stereo panning using equal-power pan law
    float pan = 0.0f;
    if (dist > 0.001f && glm::length(listenerRight) > 0.001f) {
        pan = std::clamp(glm::dot(toSrc / dist, glm::normalize(listenerRight)), -1.0f, 1.0f);
    }

    // Map pan [-1, +1] to angle [0, pi/2]
    float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    outLeftGain = baseGain * atten * std::cos(angle);
    outRightGain = baseGain * atten * std::sin(angle);
}

float AudioNode::EvaluateReverbZoneWeight(const Scene& scene, const glm::vec3& listenerPos) const {
    if (!isReverbZone) return 0.0f;
    glm::vec3 center = GetWorldPosition(scene);
    float dist = glm::distance(center, listenerPos);

    float innerR = std::max(0.0f, reverbInnerRadius);
    float outerR = std::max(innerR + 0.01f, reverbOuterRadius);

    if (dist <= innerR) return reverbWetMix;
    if (dist >= outerR) return 0.0f;

    float t = 1.0f - ((dist - innerR) / (outerR - innerR));
    return reverbWetMix * std::clamp(t, 0.0f, 1.0f);
}

void AudioNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader("Audio Source & Spatialization", ImGuiTreeNodeFlags_DefaultOpen)) {
        char pathBuf[512];
        std::strncpy(pathBuf, audioClipPath.c_str(), sizeof(pathBuf));
        pathBuf[sizeof(pathBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 85.0f);
        if (ImGui::InputText("##AudioClipPath", pathBuf, sizeof(pathBuf))) {
            audioClipPath = pathBuf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse .wav", ImVec2(80.0f, 0.0f))) {
#ifdef _WIN32
            OPENFILENAMEA ofn;
            char szFile[MAX_PATH] = "";
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = "WAV Audio Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrTitle = "Select WAV Audio File";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&ofn) == TRUE) {
                audioClipPath = ofn.lpstrFile;
                std::replace(audioClipPath.begin(), audioClipPath.end(), '\\', '/');
                Audio::AudioEngine::Get().LoadWavFromFile(audioClipPath, audioClipPath);
            }
#endif
        }

        ImGui::TextDisabled("Built-in Synth Presets:");
        ImGui::SameLine();
        if (ImGui::SmallButton("440Hz Tone")) audioClipPath = "Sine_440Hz";
        ImGui::SameLine();
        if (ImGui::SmallButton("880Hz Chime")) audioClipPath = "Chime_880Hz";
        ImGui::SameLine();
        if (ImGui::SmallButton("220Hz Bass")) audioClipPath = "Bass_220Hz";

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.30f, 1.0f));
        if (ImGui::Button("▶ Test Play Audio (Speaker)", ImVec2(200.0f, 28.0f))) {
            glm::vec3 listenerPos(0.0f, 1.5f, 5.0f);
            glm::vec3 listenerRight(1.0f, 0.0f, 0.0f);
            if (&scene != nullptr) {
                if (auto cam = scene.GetActiveCamera()) {
                    listenerPos = cam->GetWorldPosition(scene);
                }
            }
            float lGain = volume, rGain = volume;
            if (&scene != nullptr) {
                ComputeSpatialGainAndPan(scene, listenerPos, listenerRight, lGain, rGain);
            }
            float revWet = isReverbZone ? reverbWetMix : 0.0f;
            Audio::AudioEngine::Get().TriggerHardwarePlayback(
                audioClipPath.empty() ? "Sine_440Hz" : audioClipPath,
                busName, lGain, rGain, pitch, loop, revWet, reverbRoomSize, reverbDamping
            );
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("⏹ Stop", ImVec2(80.0f, 28.0f))) {
            Audio::AudioEngine::Get().StopHardwarePlayback();
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const char* buses[] = { "Master", "SFX", "Music", "Voice", "Ambient" };
        int curBus = 1;
        for (int i = 0; i < IM_ARRAYSIZE(buses); ++i) {
            if (busName == buses[i]) { curBus = i; break; }
        }
        if (ImGui::Combo("Mixing Bus", &curBus, buses, IM_ARRAYSIZE(buses))) {
            busName = buses[curBus];
        }

        ImGui::Checkbox("3D Spatial Audio", &is3DSpatial);
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &loop);
        ImGui::SameLine();
        ImGui::Checkbox("Play On Awake", &playOnAwake);

        ImGui::SliderFloat("Volume", &volume, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Pitch", &pitch, 0.1f, 3.0f, "%.2f");

        if (is3DSpatial) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "3D Attenuation Settings:");
            const char* models[] = { "Inverse Distance", "Linear Rolloff", "Exponential" };
            int curModel = static_cast<int>(attenuationModel);
            if (ImGui::Combo("Attenuation Model", &curModel, models, IM_ARRAYSIZE(models))) {
                attenuationModel = static_cast<AudioAttenuationModel>(curModel);
            }
            ImGui::DragFloat("Min Distance", &minDistance, 0.1f, 0.1f, 500.0f, "%.1f m");
            ImGui::DragFloat("Max Distance", &maxDistance, 0.5f, 0.5f, 1000.0f, "%.1f m");
            ImGui::SliderFloat("Rolloff Factor", &rolloffFactor, 0.0f, 5.0f, "%.2f");
        }

        ImGui::Separator();
        ImGui::Checkbox("Enable 3D Reverb Zone", &isReverbZone);
        if (isReverbZone) {
            ImGui::DragFloat("Inner Radius (Full Wet)", &reverbInnerRadius, 0.2f, 0.0f, 500.0f, "%.1f m");
            ImGui::DragFloat("Outer Radius (Fade Out)", &reverbOuterRadius, 0.5f, 0.5f, 1000.0f, "%.1f m");
            ImGui::SliderFloat("Room Size", &reverbRoomSize, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Decay Time (RT60)", &reverbDecayTime, 0.1f, 10.0f, "%.2f s");
            ImGui::SliderFloat("High-Freq Damping", &reverbDamping, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Wet Mix Level", &reverbWetMix, 0.0f, 1.0f, "%.2f");
        }
    }
}

