#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <imgui.h>

enum class RulerUnit {
    Meters,
    Centimeters,
    Millimeters,
    Feet,
    Inches
};

struct RulerItem {
    std::string name{"Measure 1"};
    glm::vec3 pointA{0.0f};
    glm::vec3 pointB{0.0f};
    bool hasA{false};
    bool hasB{false};
    ImVec4 color{1.0f, 0.85f, 0.20f, 1.0f};
    bool visible{true};
};

namespace RulerMath {
    inline float CalculateDistance(const glm::vec3& a, const glm::vec3& b) {
        return glm::distance(a, b);
    }

    inline glm::vec3 CalculateDelta(const glm::vec3& a, const glm::vec3& b) {
        return glm::abs(b - a);
    }

    inline float CalculateHorizontalDistance(const glm::vec3& a, const glm::vec3& b) {
        return std::hypot(b.x - a.x, b.z - a.z);
    }

    inline float CalculateElevationAngle(const glm::vec3& a, const glm::vec3& b) {
        float horiz = CalculateHorizontalDistance(a, b);
        float deltaY = std::abs(b.y - a.y);
        return (horiz > 0.0001f) ? glm::degrees(std::atan2(deltaY, horiz)) : 90.0f;
    }

    inline float CalculateHeading(const glm::vec3& a, const glm::vec3& b) {
        float heading = glm::degrees(std::atan2(b.x - a.x, -(b.z - a.z)));
        if (heading < 0.0f) heading += 360.0f;
        return heading;
    }

    inline const char* GetCompassDirection(float heading) {
        static const char* const compassDirs[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        int cIdx = static_cast<int>(std::floor((heading + 22.5f) / 45.0f)) % 8;
        if (cIdx < 0) cIdx += 8;
        return compassDirs[cIdx];
    }

    inline glm::vec3 SnapToGrid(const glm::vec3& point, float step) {
        if (step <= 0.0001f) return point;
        return glm::vec3(
            std::round(point.x / step) * step,
            std::round(point.y / step) * step,
            std::round(point.z / step) * step
        );
    }
}

struct RulerToolState {
    bool active{false};
    bool showRulerWindow{false};
    bool snapToGrid{true};
    float gridSnapStep{0.5f};
    bool snapToGeometry{true};
    bool showDeltaTriad{true};
    bool showTicks{true};
    bool showAngle{true};
    RulerUnit unit{RulerUnit::Meters};

    RulerItem current{"Current Measure", glm::vec3(0.0f), glm::vec3(0.0f), false, false, ImVec4(1.0f, 0.85f, 0.20f, 1.0f), true};
    int draggingPoint{-1}; // -1: none, 0: point A, 1: point B
    bool isDragging{false};
    glm::vec3 hoveredPoint{0.0f};
    bool hasHoveredPoint{false};

    std::vector<RulerItem> savedMeasurements;

    float GetUnitFactor() const {
        switch (unit) {
            case RulerUnit::Centimeters: return 100.0f;
            case RulerUnit::Millimeters: return 1000.0f;
            case RulerUnit::Feet: return 3.28084f;
            case RulerUnit::Inches: return 39.3701f;
            case RulerUnit::Meters:
            default: return 1.0f;
        }
    }

    const char* GetUnitSuffix() const {
        switch (unit) {
            case RulerUnit::Centimeters: return "cm";
            case RulerUnit::Millimeters: return "mm";
            case RulerUnit::Feet: return "ft";
            case RulerUnit::Inches: return "in";
            case RulerUnit::Meters:
            default: return "m";
        }
    }

    std::string FormatDist(float distInMeters) const {
        float val = distInMeters * GetUnitFactor();
        char buf[64];
        if (unit == RulerUnit::Millimeters) {
            snprintf(buf, sizeof(buf), "%.0f %s", val, GetUnitSuffix());
        } else {
            snprintf(buf, sizeof(buf), "%.2f %s", val, GetUnitSuffix());
        }
        return std::string(buf);
    }
};

