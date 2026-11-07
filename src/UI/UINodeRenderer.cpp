#include "UINodeRenderer.h"
#include "Scene/Scene.h"
#include "Scene/Node.h"
#include "Gameplay/GameLoop.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <functional>
#include <iostream>
#include <fstream>
#include <filesystem>

static std::map<std::string, TTFFontInfo> s_loadedFontRegistry;

static uint16_t ReadBE16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

static int16_t ReadBES16(const uint8_t* p) {
    return static_cast<int16_t>(ReadBE16(p));
}

static uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static void WriteBE16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

static void WriteBE32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

std::vector<uint8_t> UINodeRenderer::GenerateMinimalValidTTF(const std::string& familyName,
                                                             uint16_t unitsPerEm,
                                                             uint16_t numGlyphs) {
    std::vector<uint8_t> out;
    // Offset table: sfntVersion = 0x00010000 (TrueType), numTables = 4 ('head', 'hhea', 'maxp', 'name')
    WriteBE32(out, 0x00010000u);
    WriteBE16(out, 4);   // numTables
    WriteBE16(out, 64);  // searchRange
    WriteBE16(out, 2);   // entrySelector
    WriteBE16(out, 0);   // rangeShift

    // 4 table directory entries (16 bytes each = 64 bytes); header total = 12 + 64 = 76 bytes
    uint32_t headOffset = 76;
    uint32_t headLen = 54;
    uint32_t hheaOffset = headOffset + 56; // padded to 4-byte boundary
    uint32_t hheaLen = 36;
    uint32_t maxpOffset = hheaOffset + 36;
    uint32_t maxpLen = 6;
    uint32_t nameOffset = maxpOffset + 8;
    uint32_t nameLen = static_cast<uint32_t>(18 + familyName.size());

    auto writeDirEntry = [&](const char tag[4], uint32_t offset, uint32_t length) {
        out.push_back(static_cast<uint8_t>(tag[0]));
        out.push_back(static_cast<uint8_t>(tag[1]));
        out.push_back(static_cast<uint8_t>(tag[2]));
        out.push_back(static_cast<uint8_t>(tag[3]));
        WriteBE32(out, 0); // checksum
        WriteBE32(out, offset);
        WriteBE32(out, length);
    };

    writeDirEntry("head", headOffset, headLen);
    writeDirEntry("hhea", hheaOffset, hheaLen);
    writeDirEntry("maxp", maxpOffset, maxpLen);
    writeDirEntry("name", nameOffset, nameLen);

    // 'head' table (54 bytes + 2 pad = 56)
    size_t startHead = out.size();
    WriteBE32(out, 0x00010000u); // version
    WriteBE32(out, 0x00010000u); // fontRevision
    WriteBE32(out, 0);           // checkSumAdjustment
    WriteBE32(out, 0x5F0F3CF5u); // magicNumber
    WriteBE16(out, 0);           // flags
    WriteBE16(out, unitsPerEm);  // unitsPerEm at offset 18
    while (out.size() < startHead + 56) out.push_back(0);

    // 'hhea' table (36 bytes)
    size_t startHhea = out.size();
    WriteBE32(out, 0x00010000u); // version
    WriteBE16(out, static_cast<uint16_t>(unitsPerEm * 4 / 5)); // ascender at offset 4
    WriteBE16(out, static_cast<uint16_t>(static_cast<int16_t>(-(int)unitsPerEm / 5))); // descender at offset 6
    while (out.size() < startHhea + 36) out.push_back(0);

    // 'maxp' table (6 bytes + 2 pad = 8)
    WriteBE32(out, 0x00005000u); // version 0.5
    WriteBE16(out, numGlyphs);   // numGlyphs at offset 4
    WriteBE16(out, 0);           // pad

    // 'name' table
    WriteBE16(out, 0);  // format
    WriteBE16(out, 1);  // count = 1 record
    WriteBE16(out, 18); // stringOffset (6 + 12)
    // NameRecord: platformID=1 (Mac), encodingID=0, languageID=0, nameID=1 (Font Family), length, offset=0
    WriteBE16(out, 1);
    WriteBE16(out, 0);
    WriteBE16(out, 0);
    WriteBE16(out, 1);
    WriteBE16(out, static_cast<uint16_t>(familyName.size()));
    WriteBE16(out, 0);
    for (char c : familyName) {
        out.push_back(static_cast<uint8_t>(c));
    }

    return out;
}

bool UINodeRenderer::LoadCustomTTFFromMemory(const std::string& virtualPath, const std::vector<uint8_t>& ttfBytes, float sizePx) {
    if (ttfBytes.size() < 12) return false;

    uint32_t sfntVersion = ReadBE32(ttfBytes.data());
    // Valid TrueType/OpenType signatures: 0x00010000, 'OTTO' (0x4F54544F), 'true' (0x74727565)
    if (sfntVersion != 0x00010000u && sfntVersion != 0x4F54544Fu && sfntVersion != 0x74727565u) {
        return false;
    }

    uint16_t numTables = ReadBE16(ttfBytes.data() + 4);
    if (ttfBytes.size() < static_cast<size_t>(12 + numTables * 16)) {
        return false;
    }

    TTFFontInfo info{};
    info.valid = true;
    info.filePath = virtualPath;
    info.requestedSizePx = sizePx;
    info.familyName = std::filesystem::path(virtualPath).stem().string();

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* entry = ttfBytes.data() + 12 + i * 16;
        std::string tag(reinterpret_cast<const char*>(entry), 4);
        uint32_t offset = ReadBE32(entry + 8);
        uint32_t length = ReadBE32(entry + 12);

        if (offset + length > ttfBytes.size()) continue;
        const uint8_t* table = ttfBytes.data() + offset;

        if (tag == "head" && length >= 20) {
            info.unitsPerEm = ReadBE16(table + 18);
        } else if (tag == "hhea" && length >= 8) {
            info.ascender = ReadBES16(table + 4);
            info.descender = ReadBES16(table + 6);
        } else if (tag == "maxp" && length >= 6) {
            info.numGlyphs = ReadBE16(table + 4);
        } else if (tag == "name" && length >= 6) {
            uint16_t count = ReadBE16(table + 2);
            uint16_t strOffset = ReadBE16(table + 4);
            for (uint16_t n = 0; n < count; ++n) {
                if (6u + (n + 1u) * 12u > length) break;
                const uint8_t* rec = table + 6 + n * 12;
                uint16_t platformID = ReadBE16(rec + 0);
                uint16_t nameID = ReadBE16(rec + 6);
                uint16_t strLen = ReadBE16(rec + 8);
                uint16_t strPos = ReadBE16(rec + 10);
                if (nameID == 1 && strOffset + strPos + strLen <= length && strLen > 0) {
                    const uint8_t* strBytes = table + strOffset + strPos;
                    std::string parsedName;
                    if (platformID == 3) {
                        // UTF-16BE Windows name record
                        for (uint16_t b = 1; b < strLen; b += 2) {
                            if (strBytes[b] >= 32 && strBytes[b] < 127) {
                                parsedName.push_back(static_cast<char>(strBytes[b]));
                            }
                        }
                    } else {
                        for (uint16_t b = 0; b < strLen; ++b) {
                            if (strBytes[b] >= 32 && strBytes[b] < 127) {
                                parsedName.push_back(static_cast<char>(strBytes[b]));
                            }
                        }
                    }
                    if (!parsedName.empty()) {
                        info.familyName = parsedName;
                        break;
                    }
                }
            }
        }
    }

    s_loadedFontRegistry[virtualPath] = info;
    return true;
}

bool UINodeRenderer::LoadCustomTTFFont(const std::string& filePath, float sizePx) {
    if (filePath.empty()) return false;
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize fsize = file.tellg();
    if (fsize < 12) return false;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(fsize));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), fsize)) return false;

    return LoadCustomTTFFromMemory(filePath, bytes, sizePx);
}

TTFFontInfo UINodeRenderer::GetLoadedFontInfo(const std::string& filePath) {
    auto it = s_loadedFontRegistry.find(filePath);
    if (it != s_loadedFontRegistry.end()) {
        return it->second;
    }
    return TTFFontInfo{};
}

void UINodeRenderer::ComputeUIRect(const Scene& scene, const std::shared_ptr<Node>& uiNode, ImVec2 canvasPos, float w, float h, ImVec2& anchorOrigin, ImVec2& rectMin, ImVec2& rectMax) {
    ImVec2 origin = canvasPos;
    float contentW = w;
    float contentH = h;

    // Evaluate parent bounds if this node is parented to another UI node
    if (uiNode->parentId != 0) {
        auto parent = scene.GetNodeById(uiNode->parentId);
        if (parent && parent->IsUINode()) {
            ImVec2 pAnchor, pMin, pMax;
            ComputeUIRect(scene, parent, canvasPos, w, h, pAnchor, pMin, pMax);
            if (parent->nodeType == NodeType::UI_Window) {
                // Content origin below title bar (26px height)
                float titleH = 26.0f;
                origin = ImVec2(pMin.x, pMin.y + titleH);
                contentW = (std::max)(1.0f, pMax.x - pMin.x);
                contentH = (std::max)(1.0f, (pMax.y - pMin.y) - titleH);
            } else {
                origin = pMin;
                contentW = (std::max)(1.0f, pMax.x - pMin.x);
                contentH = (std::max)(1.0f, pMax.y - pMin.y);
            }
        }
    }

    anchorOrigin = origin;
    switch (uiNode->uiAnchor) {
        case UIAnchor::TopLeft:      anchorOrigin = origin; break;
        case UIAnchor::TopCenter:    anchorOrigin = ImVec2(origin.x + contentW * 0.5f, origin.y); break;
        case UIAnchor::TopRight:     anchorOrigin = ImVec2(origin.x + contentW, origin.y); break;
        case UIAnchor::CenterLeft:   anchorOrigin = ImVec2(origin.x, origin.y + contentH * 0.5f); break;
        case UIAnchor::Center:       anchorOrigin = ImVec2(origin.x + contentW * 0.5f, origin.y + contentH * 0.5f); break;
        case UIAnchor::CenterRight:  anchorOrigin = ImVec2(origin.x + contentW, origin.y + contentH * 0.5f); break;
        case UIAnchor::BottomLeft:   anchorOrigin = ImVec2(origin.x, origin.y + contentH); break;
        case UIAnchor::BottomCenter: anchorOrigin = ImVec2(origin.x + contentW * 0.5f, origin.y + contentH); break;
        case UIAnchor::BottomRight:  anchorOrigin = ImVec2(origin.x + contentW, origin.y + contentH); break;
    }

    rectMin = anchorOrigin;
    switch (uiNode->uiAnchor) {
        case UIAnchor::TopLeft:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x, anchorOrigin.y + uiNode->uiPosition.y);
            break;
        case UIAnchor::TopCenter:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x - uiNode->uiSize.x * 0.5f, anchorOrigin.y + uiNode->uiPosition.y);
            break;
        case UIAnchor::TopRight:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x - uiNode->uiSize.x, anchorOrigin.y + uiNode->uiPosition.y);
            break;
        case UIAnchor::CenterLeft:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x, anchorOrigin.y + uiNode->uiPosition.y - uiNode->uiSize.y * 0.5f);
            break;
        case UIAnchor::Center:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x - uiNode->uiSize.x * 0.5f, anchorOrigin.y + uiNode->uiPosition.y - uiNode->uiSize.y * 0.5f);
            break;
        case UIAnchor::CenterRight:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x - uiNode->uiSize.x, anchorOrigin.y + uiNode->uiPosition.y - uiNode->uiSize.y * 0.5f);
            break;
        case UIAnchor::BottomLeft:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x, anchorOrigin.y + uiNode->uiPosition.y - uiNode->uiSize.y);
            break;
        case UIAnchor::BottomCenter:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x - uiNode->uiSize.x * 0.5f, anchorOrigin.y + uiNode->uiPosition.y - uiNode->uiSize.y);
            break;
        case UIAnchor::BottomRight:
            rectMin = ImVec2(anchorOrigin.x + uiNode->uiPosition.x - uiNode->uiSize.x, anchorOrigin.y + uiNode->uiPosition.y - uiNode->uiSize.y);
            break;
    }
    rectMax = ImVec2(rectMin.x + uiNode->uiSize.x, rectMin.y + uiNode->uiSize.y);
}


void UINodeRenderer::Render(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize, bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state, uint32_t selectedId) {
    if (!drawList || canvasSize.x <= 1.0f || canvasSize.y <= 1.0f) return;

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    if (selectedId == 0) selectedId = (uint32_t)scene.GetSelectedId();

    float w = canvasSize.x;
    float h = canvasSize.y;
    bool isGamePlaying = (gameLoop != nullptr) && (gameLoop->IsPlaying() || gameLoop->IsPaused());
    bool isInteractive = (!isEditorMode && isGamePlaying) || (isEditorMode && !isGamePlaying);
    bool isCanvasHovered = isInteractive &&
                           (mousePos.x >= canvasPos.x && mousePos.x <= canvasPos.x + w &&
                            mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + h);

    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + w, canvasPos.y + h), true);

    std::vector<std::shared_ptr<Node>> renderList;
    std::unordered_set<uint32_t> visited;

    std::function<void(const std::shared_ptr<Node>&)> collectHierarchy = [&](const std::shared_ptr<Node>& node) {
        if (!node || !node->IsUINode() || visited.count(node->id)) return;
        visited.insert(node->id);
        renderList.push_back(node);
        for (uint32_t cid : node->childrenIds) {
            auto child = scene.GetNodeById(cid);
            if (child && child->IsUINode()) {
                collectHierarchy(child);
            }
        }
    };

    for (const auto& node : scene.GetNodes()) {
        if (!node->IsUINode()) continue;
        auto parent = (node->parentId != 0) ? scene.GetNodeById(node->parentId) : nullptr;
        bool parentIsUI = (parent && parent->IsUINode());
        if (!parentIsUI) {
            collectHierarchy(node);
        }
    }
    for (const auto& node : scene.GetNodes()) {
        if (node->IsUINode() && !visited.count(node->id)) {
            collectHierarchy(node);
        }
    }

    for (const auto& uiNode : renderList) {
        if (!uiNode->IsUINode() || !uiNode->uiVisible) continue;

        bool isHierarchyVisible = uiNode->uiVisible;
        uint32_t checkPid = uiNode->parentId;
        while (checkPid != 0 && isHierarchyVisible) {
            auto pNode = scene.GetNodeById(checkPid);
            if (!pNode) break;
            if (pNode->nodeType == NodeType::UI_Window && !pNode->uiWindowIsOpen && !isEditorMode) {
                isHierarchyVisible = false;
                break;
            }
            if (!pNode->uiVisible) {
                isHierarchyVisible = false;
                break;
            }
            checkPid = pNode->parentId;
        }
        if (!isHierarchyVisible) continue;

        if (!uiNode->uiFontPath.empty() && !GetLoadedFontInfo(uiNode->uiFontPath).valid) {
            LoadCustomTTFFont(uiNode->uiFontPath, uiNode->uiFontSize);
        }

        ImVec2 anchorOrigin, rectMin, rectMax;
        ComputeUIRect(scene, uiNode, canvasPos, w, h, anchorOrigin, rectMin, rectMax);

        bool isHovered = isCanvasHovered && (mousePos.x >= rectMin.x && mousePos.x <= rectMax.x &&
                                            mousePos.y >= rectMin.y && mousePos.y <= rectMax.y);
        bool isClicked = isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        uiNode->DrawUI(scene, drawList, canvasPos, w, h, isEditorMode, gameLoop, state, rectMin, rectMax, isHovered, isClicked);
    }

    drawList->PopClipRect();

    if (isInteractive) {
        if (state.activeColorPickerNodeId != 0) {
            auto cpNode = scene.GetNodeById(state.activeColorPickerNodeId);
            if (!cpNode || cpNode->nodeType != NodeType::UI_ColorPicker || !cpNode->uiVisible) {
                state.activeColorPickerNodeId = 0;
            } else {
                bool cpOpen = true;
                ImVec2 anchorOrigin, cpRectMin, cpRectMax;
                ComputeUIRect(scene, cpNode, canvasPos, w, h, anchorOrigin, cpRectMin, cpRectMax);

                ImGui::SetNextWindowPos(ImVec2(cpRectMax.x + 10.0f, cpRectMin.y), ImGuiCond_Appearing);
                ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_Appearing);
                std::string winTitle = "Color Picker: " + cpNode->name + "###ColorPickerPopup";
                if (ImGui::Begin(winTitle.c_str(), &cpOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
                    if (state.lastHexNodeId != cpNode->id || !state.isEditingPickerHex) {
                        snprintf(state.pickerHexBuf, sizeof(state.pickerHexBuf), "#%02X%02X%02X",
                                 (int)(cpNode->uiColor.r * 255.0f),
                                 (int)(cpNode->uiColor.g * 255.0f),
                                 (int)(cpNode->uiColor.b * 255.0f));
                    }
                    if (ImGui::InputText("Hex Code (#RRGGBB)", state.pickerHexBuf, sizeof(state.pickerHexBuf))) {
                        std::string s = state.pickerHexBuf;
                        if (!s.empty() && s[0] == '#') s = s.substr(1);
                        if (s.length() == 6) {
                            try {
                                unsigned int val = std::stoul(s, nullptr, 16);
                                cpNode->uiColor.r = ((val >> 16) & 0xFF) / 255.0f;
                                cpNode->uiColor.g = ((val >> 8) & 0xFF) / 255.0f;
                                cpNode->uiColor.b = (val & 0xFF) / 255.0f;
                            } catch (...) {}
                        }
                    }
                    state.isEditingPickerHex = ImGui::IsItemActive();

                    ImGui::ColorPicker4("##wheel", glm::value_ptr(cpNode->uiColor),
                                        ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_AlphaBar |
                                        ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHex);

                    ImGui::Text("Quick Swatches:");
                    const ImVec4 swatches[] = {
                        ImVec4(1.0f, 0.2f, 0.2f, 1.0f), ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
                        ImVec4(0.2f, 0.4f, 1.0f, 1.0f), ImVec4(1.0f, 0.8f, 0.1f, 1.0f),
                        ImVec4(0.9f, 0.3f, 0.9f, 1.0f), ImVec4(0.1f, 0.9f, 0.9f, 1.0f),
                        ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.1f, 0.1f, 0.15f, 1.0f)
                    };
                    for (int si = 0; si < 8; ++si) {
                        if (si > 0) ImGui::SameLine();
                        ImGui::PushID(si);
                        if (ImGui::ColorButton("##swatch", swatches[si], ImGuiColorEditFlags_NoPicker, ImVec2(24, 24))) {
                            cpNode->uiColor = glm::vec4(swatches[si].x, swatches[si].y, swatches[si].z, swatches[si].w);
                        }
                        ImGui::PopID();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    if (ImGui::Button("Done / Close", ImVec2(-1, 26))) {
                        cpOpen = false;
                    }
                }
                ImGui::End();
                if (!cpOpen) {
                    state.activeColorPickerNodeId = 0;
                }
            }
        }

        if (state.activeDatePickerNodeId != 0) {
            auto dpNode = scene.GetNodeById(state.activeDatePickerNodeId);
            if (!dpNode || dpNode->nodeType != NodeType::UI_DatePicker || !dpNode->uiVisible) {
                state.activeDatePickerNodeId = 0;
            } else {
                bool dpOpen = true;
                ImVec2 anchorOrigin, dpRectMin, dpRectMax;
                ComputeUIRect(scene, dpNode, canvasPos, w, h, anchorOrigin, dpRectMin, dpRectMax);

                ImGui::SetNextWindowPos(ImVec2(dpRectMin.x, dpRectMax.y + 6.0f), ImGuiCond_Appearing);
                ImGui::SetNextWindowSize(ImVec2(270.0f, 0.0f), ImGuiCond_Appearing);
                std::string winTitle = "Calendar: " + dpNode->name + "###DatePickerPopup";
                if (ImGui::Begin(winTitle.c_str(), &dpOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
                    const char* monthNames[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
                    if (ImGui::ArrowButton("##prevY", ImGuiDir_Left)) { state.calendarYear--; }
                    ImGui::SameLine();
                    ImGui::Text("%04d", state.calendarYear);
                    ImGui::SameLine();
                    if (ImGui::ArrowButton("##nextY", ImGuiDir_Right)) { state.calendarYear++; }
                    ImGui::SameLine();
                    ImGui::Spacing();
                    ImGui::SameLine();
                    if (ImGui::ArrowButton("##prevM", ImGuiDir_Left)) {
                        state.calendarMonth--;
                        if (state.calendarMonth < 1) { state.calendarMonth = 12; state.calendarYear--; }
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", monthNames[std::clamp(state.calendarMonth - 1, 0, 11)]);
                    ImGui::SameLine();
                    if (ImGui::ArrowButton("##nextM", ImGuiDir_Right)) {
                        state.calendarMonth++;
                        if (state.calendarMonth > 12) { state.calendarMonth = 1; state.calendarYear++; }
                    }
                    ImGui::PopStyleVar();
                    ImGui::Separator();

                    const char* dayHeaders[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
                    for (int d = 0; d < 7; ++d) {
                        if (d > 0) ImGui::SameLine();
                        ImGui::TextDisabled("%3s", dayHeaders[d]);
                    }

                    int daysInMonth = 31;
                    if (state.calendarMonth == 2) {
                        bool isLeap = (state.calendarYear % 4 == 0 && state.calendarYear % 100 != 0) || (state.calendarYear % 400 == 0);
                        daysInMonth = isLeap ? 29 : 28;
                    } else if (state.calendarMonth == 4 || state.calendarMonth == 6 || state.calendarMonth == 9 || state.calendarMonth == 11) {
                        daysInMonth = 30;
                    }

                    for (int day = 1; day <= daysInMonth; ++day) {
                        int col = (day - 1) % 7;
                        if (col > 0) ImGui::SameLine();
                        char dayNumStr[8];
                        snprintf(dayNumStr, sizeof(dayNumStr), "%2d", day);
                        std::string dayBtnId = std::string(dayNumStr) + "##calDay" + std::to_string(day);
                        if (ImGui::SmallButton(dayBtnId.c_str())) {
                            char dtBuf[64];
                            snprintf(dtBuf, sizeof(dtBuf), "%04d-%02d-%02d 12:00", state.calendarYear, state.calendarMonth, day);
                            dpNode->uiText = dtBuf;
                            dpOpen = false;
                        }
                    }

                    ImGui::Separator();
                    char editBuf[128];
                    strncpy(editBuf, dpNode->uiText.c_str(), sizeof(editBuf));
                    editBuf[sizeof(editBuf) - 1] = '\0';
                    if (ImGui::InputText("DateTime", editBuf, sizeof(editBuf))) {
                        dpNode->uiText = editBuf;
                    }

                    if (ImGui::Button("Today", ImVec2(75, 24))) {
                        state.calendarYear = 2026;
                        state.calendarMonth = 10;
                        dpNode->uiText = "2026-10-25 15:00";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Close", ImVec2(75, 24))) {
                        dpOpen = false;
                    }
                }
                ImGui::End();
                if (!dpOpen) {
                    state.activeDatePickerNodeId = 0;
                }
            }
        }
    }
}
