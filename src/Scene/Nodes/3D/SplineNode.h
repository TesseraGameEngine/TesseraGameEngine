#pragma once

#include "Scene/Node.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>

class Scene;
struct NodeInspectorState;

enum class SplineMode {
    CatmullRom,
    CubicBezier,
    Linear
};

inline const char* SplineModeToString(SplineMode mode) {
    switch (mode) {
        case SplineMode::CatmullRom: return "CatmullRom";
        case SplineMode::CubicBezier: return "CubicBezier";
        case SplineMode::Linear: return "Linear";
        default: return "CatmullRom";
    }
}

inline SplineMode StringToSplineMode(const std::string& str) {
    if (str == "CubicBezier" || str == "Bezier") return SplineMode::CubicBezier;
    if (str == "Linear") return SplineMode::Linear;
    return SplineMode::CatmullRom;
}

class SplineNode : public Node {
public:
    SplineNode(const std::string& name = "Spline Curve", uint32_t parentId = 0);

    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;

    // Curve evaluation (t in [0.0, 1.0])
    glm::vec3 Evaluate(float t) const;
    glm::vec3 EvaluateWorld(const Scene& scene, float t) const;
    glm::vec3 EvaluateTangent(float t) const;

    // Arc-length utilities
    float GetLength(int samples = 64) const;
    glm::vec3 GetPointAtDistance(float distance, int samples = 64) const;
    std::vector<glm::vec3> SampleCurve(int numSamples = 64) const;

    SplineMode splineMode{SplineMode::CatmullRom};
    std::vector<glm::vec3> controlPoints;
    bool closedLoop{false};
    float tension{0.5f};
    int subdivisions{32};
    glm::vec4 lineColor{0.95f, 0.75f, 0.20f, 1.0f};
};

