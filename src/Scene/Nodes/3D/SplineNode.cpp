#include "SplineNode.h"
#include "Scene/Scene.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

SplineNode::SplineNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Spline;
    parentId = pId;
    name = nodeName.empty() ? "Spline Curve" : nodeName;
    color = glm::vec4(0.95f, 0.75f, 0.20f, 1.0f);

    // Default 4 control points forming an S-curve
    controlPoints = {
        glm::vec3(-2.0f, 0.0f, -2.0f),
        glm::vec3(-1.0f, 1.0f,  0.0f),
        glm::vec3( 1.0f, 1.0f,  0.0f),
        glm::vec3( 2.0f, 0.0f,  2.0f)
    };
}

std::shared_ptr<Node> SplineNode::Clone() const {
    return std::make_shared<SplineNode>(*this);
}

static glm::vec3 EvalCatmullRomSegment(const glm::vec3& p0, const glm::vec3& p1,
                                       const glm::vec3& p2, const glm::vec3& p3,
                                       float t, float tension) {
    float t2 = t * t;
    float t3 = t2 * t;
    float s = (1.0f - tension);

    glm::vec3 m1 = s * (p2 - p0) * 0.5f;
    glm::vec3 m2 = s * (p3 - p1) * 0.5f;

    float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 =         t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 =         t3 -        t2;

    return h00 * p1 + h10 * m1 + h01 * p2 + h11 * m2;
}

static glm::vec3 EvalCubicBezier(const glm::vec3& p0, const glm::vec3& p1,
                                 const glm::vec3& p2, const glm::vec3& p3, float t) {
    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;

    return uuu * p0 + 3.0f * uu * t * p1 + 3.0f * u * tt * p2 + ttt * p3;
}

glm::vec3 SplineNode::Evaluate(float t) const {
    if (controlPoints.empty()) return glm::vec3(0.0f);
    if (controlPoints.size() == 1) return controlPoints[0];

    t = std::clamp(t, 0.0f, 1.0f);
    int n = static_cast<int>(controlPoints.size());

    if (splineMode == SplineMode::Linear) {
        int segCount = closedLoop ? n : (n - 1);
        float scaled = t * static_cast<float>(segCount);
        int idx = std::min(static_cast<int>(std::floor(scaled)), segCount - 1);
        float localT = scaled - static_cast<float>(idx);
        const glm::vec3& a = controlPoints[idx % n];
        const glm::vec3& b = controlPoints[(idx + 1) % n];
        return glm::mix(a, b, localT);
    }

    if (splineMode == SplineMode::CubicBezier && n >= 4) {
        int numCurves = (n - 1) / 3;
        if (numCurves <= 0) numCurves = 1;
        float scaled = t * static_cast<float>(numCurves);
        int curveIdx = std::min(static_cast<int>(std::floor(scaled)), numCurves - 1);
        float localT = scaled - static_cast<float>(curveIdx);
        int base = curveIdx * 3;
        if (base + 3 < n) {
            return EvalCubicBezier(controlPoints[base], controlPoints[base + 1],
                                   controlPoints[base + 2], controlPoints[base + 3], localT);
        }
    }

    // Catmull-Rom spline
    int segCount = closedLoop ? n : (n - 1);
    float scaled = t * static_cast<float>(segCount);
    int idx = std::min(static_cast<int>(std::floor(scaled)), segCount - 1);
    float localT = scaled - static_cast<float>(idx);

    auto getPt = [&](int i) -> const glm::vec3& {
        if (closedLoop) {
            int wrapped = ((i % n) + n) % n;
            return controlPoints[wrapped];
        }
        return controlPoints[std::clamp(i, 0, n - 1)];
    };

    return EvalCatmullRomSegment(getPt(idx - 1), getPt(idx), getPt(idx + 1), getPt(idx + 2), localT, tension);
}

glm::vec3 SplineNode::EvaluateWorld(const Scene& scene, float t) const {
    glm::vec3 localPt = Evaluate(t);
    glm::mat4 worldMat = GetWorldTransformMatrix(scene);
    return glm::vec3(worldMat * glm::vec4(localPt, 1.0f));
}

glm::vec3 SplineNode::EvaluateTangent(float t) const {
    const float eps = 0.001f;
    float t0 = std::clamp(t - eps, 0.0f, 1.0f);
    float t1 = std::clamp(t + eps, 0.0f, 1.0f);
    glm::vec3 diff = Evaluate(t1) - Evaluate(t0);
    float len = glm::length(diff);
    if (len < 1e-6f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return diff / len;
}

float SplineNode::GetLength(int samples) const {
    if (controlPoints.size() < 2 || samples < 1) return 0.0f;
    float totalLen = 0.0f;
    glm::vec3 prev = Evaluate(0.0f);
    for (int i = 1; i <= samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(samples);
        glm::vec3 cur = Evaluate(t);
        totalLen += glm::distance(prev, cur);
        prev = cur;
    }
    return totalLen;
}

glm::vec3 SplineNode::GetPointAtDistance(float distance, int samples) const {
    if (controlPoints.empty()) return glm::vec3(0.0f);
    if (controlPoints.size() == 1 || distance <= 0.0f) return Evaluate(0.0f);

    float accum = 0.0f;
    glm::vec3 prev = Evaluate(0.0f);
    for (int i = 1; i <= samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(samples);
        glm::vec3 cur = Evaluate(t);
        float segLen = glm::distance(prev, cur);
        if (accum + segLen >= distance && segLen > 1e-6f) {
            float frac = (distance - accum) / segLen;
            return glm::mix(prev, cur, frac);
        }
        accum += segLen;
        prev = cur;
    }
    return Evaluate(1.0f);
}

std::vector<glm::vec3> SplineNode::SampleCurve(int numSamples) const {
    std::vector<glm::vec3> pts;
    if (numSamples < 2) numSamples = 2;
    pts.reserve(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(numSamples - 1);
        pts.push_back(Evaluate(t));
    }
    return pts;
}

void SplineNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader("3D Spline / Curve Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* modeNames[] = { "Catmull-Rom", "Cubic Bezier", "Linear" };
        int curMode = static_cast<int>(splineMode);
        if (ImGui::Combo("Spline Mode", &curMode, modeNames, IM_ARRAYSIZE(modeNames))) {
            splineMode = static_cast<SplineMode>(curMode);
        }

        ImGui::Checkbox("Closed Loop", &closedLoop);
        if (splineMode == SplineMode::CatmullRom) {
            ImGui::SliderFloat("Tension", &tension, 0.0f, 1.0f, "%.2f");
        }
        ImGui::SliderInt("Subdivisions", &subdivisions, 4, 128);
        ImGui::ColorEdit4("Curve Color", glm::value_ptr(lineColor));

        ImGui::Spacing();
        ImGui::TextDisabled("Total Arc Length: %.2f m", GetLength(64));
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Control Points (%zu):", controlPoints.size());

        if (ImGui::Button("Add Control Point")) {
            glm::vec3 nextPt(0.0f, 0.0f, 0.0f);
            if (!controlPoints.empty()) {
                nextPt = controlPoints.back() + glm::vec3(1.0f, 0.0f, 0.0f);
            }
            controlPoints.push_back(nextPt);
        }

        for (size_t i = 0; i < controlPoints.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("[%zu]", i);
            ImGui::SameLine();
            ImGui::DragFloat3("##CP", glm::value_ptr(controlPoints[i]), 0.1f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X") && controlPoints.size() > 2) {
                controlPoints.erase(controlPoints.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
}

