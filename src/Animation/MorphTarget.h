#pragma once

#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include "Scene/Mesh.h"

namespace Animation {

struct MorphTarget {
    std::string name;
    std::vector<glm::vec3> positionDeltas;
    std::vector<glm::vec3> normalDeltas;
};

class MorphTargetSet {
public:
    void AddTarget(const MorphTarget& target);
    void AddTarget(const std::string& name,
                   const std::vector<glm::vec3>& posDeltas,
                   const std::vector<glm::vec3>& normDeltas = {});

    const MorphTarget* GetTarget(const std::string& name) const;
    size_t GetTargetCount() const { return m_targets.size(); }
    const std::vector<MorphTarget>& GetTargets() const { return m_targets; }
    void Clear() { m_targets.clear(); }

    // Deforms baseVertices into outVertices by blending all active morph target weights (typically in [0.0, 1.0])
    void Apply(const std::vector<Vertex>& baseVertices,
               std::vector<Vertex>& outVertices,
               const std::map<std::string, float>& weights) const;

private:
    std::vector<MorphTarget> m_targets;
};

} // namespace Animation

