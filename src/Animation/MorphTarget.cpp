#include "MorphTarget.h"
#include <cmath>

namespace Animation {

void MorphTargetSet::AddTarget(const MorphTarget& target) {
    for (auto& existing : m_targets) {
        if (existing.name == target.name) {
            existing = target;
            return;
        }
    }
    m_targets.push_back(target);
}

void MorphTargetSet::AddTarget(const std::string& name,
                               const std::vector<glm::vec3>& posDeltas,
                               const std::vector<glm::vec3>& normDeltas) {
    MorphTarget t;
    t.name = name;
    t.positionDeltas = posDeltas;
    t.normalDeltas = normDeltas;
    AddTarget(t);
}

const MorphTarget* MorphTargetSet::GetTarget(const std::string& name) const {
    for (const auto& t : m_targets) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

void MorphTargetSet::Apply(const std::vector<Vertex>& baseVertices,
                           std::vector<Vertex>& outVertices,
                           const std::map<std::string, float>& weights) const {
    outVertices = baseVertices;
    if (m_targets.empty() || weights.empty() || outVertices.empty()) return;

    size_t vCount = outVertices.size();
    for (const auto& target : m_targets) {
        auto it = weights.find(target.name);
        if (it == weights.end()) continue;

        float w = it->second;
        if (std::abs(w) < 1e-5f) continue;

        size_t posCount = std::min(vCount, target.positionDeltas.size());
        for (size_t i = 0; i < posCount; ++i) {
            outVertices[i].pos += target.positionDeltas[i] * w;
        }

        size_t normCount = std::min(vCount, target.normalDeltas.size());
        for (size_t i = 0; i < normCount; ++i) {
            glm::vec3 n = outVertices[i].normal + target.normalDeltas[i] * w;
            if (glm::length(n) > 1e-5f) {
                outVertices[i].normal = glm::normalize(n);
            }
        }
    }
}

} // namespace Animation

