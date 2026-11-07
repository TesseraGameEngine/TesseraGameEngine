#include "MeshSimplifier.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <map>
#include <iostream>

namespace Geometry {

// 10 unique elements of 4x4 symmetric matrix for Quadric Error Metric
struct SymmMatrix {
    double m[10];

    SymmMatrix(double c = 0.0) {
        for (int i = 0; i < 10; ++i) m[i] = c;
    }

    SymmMatrix(double a, double b, double c, double d) {
        m[0] = a * a;  m[1] = a * b;  m[2] = a * c;  m[3] = a * d;
                       m[4] = b * b;  m[5] = b * c;  m[6] = b * d;
                                      m[7] = c * c;  m[8] = c * d;
                                                     m[9] = d * d;
    }

    SymmMatrix operator+(const SymmMatrix& n) const {
        SymmMatrix r;
        for (int i = 0; i < 10; ++i) r.m[i] = m[i] + n.m[i];
        return r;
    }

    SymmMatrix& operator+=(const SymmMatrix& n) {
        for (int i = 0; i < 10; ++i) m[i] += n.m[i];
        return *this;
    }

    double Evaluate(double x, double y, double z) const {
        return m[0] * x * x + 2.0 * m[1] * x * y + 2.0 * m[2] * x * z + 2.0 * m[3] * x +
               m[4] * y * y + 2.0 * m[5] * y * z + 2.0 * m[6] * y +
               m[7] * z * z + 2.0 * m[8] * z +
               m[9];
    }
};

struct SimpTriangle {
    uint32_t v[3];
    double err[3]{0.0, 0.0, 0.0};
    bool deleted{false};
    bool dirty{false};
    glm::vec3 normal{0.0f};
};

struct SimpVertex {
    Vertex v;
    SymmMatrix q;
    int tstart{0};
    int tcount{0};
    bool border{false};
};

struct EdgeRef {
    uint32_t v0;
    uint32_t v1;
    bool operator==(const EdgeRef& o) const {
        return (v0 == o.v0 && v1 == o.v1) || (v0 == o.v1 && v1 == o.v0);
    }
};

struct EdgeHash {
    size_t operator()(const EdgeRef& e) const {
        uint32_t mn = std::min(e.v0, e.v1);
        uint32_t mx = std::max(e.v0, e.v1);
        return (static_cast<size_t>(mn) << 32) | mx;
    }
};

static double ComputeVertexError(const SymmMatrix& q, const glm::vec3& p) {
    return q.Evaluate(p.x, p.y, p.z);
}

// Checks if flipping occurs when collapsing vertex u to candidate position
static bool WouldFlipNormal(const std::vector<SimpVertex>& vertices,
                            const std::vector<SimpTriangle>& triangles,
                            const std::vector<int>& refs,
                            uint32_t u, uint32_t v,
                            const glm::vec3& newPos) {
    const SimpVertex& su = vertices[u];
    for (int k = 0; k < su.tcount; ++k) {
        int tIdx = refs[su.tstart + k];
        const SimpTriangle& t = triangles[tIdx];
        if (t.deleted) continue;
        if (t.v[0] == v || t.v[1] == v || t.v[2] == v) continue; // Triangles to be deleted

        // Indices with u replaced by newPos
        glm::vec3 p0 = (t.v[0] == u) ? newPos : vertices[t.v[0]].v.pos;
        glm::vec3 p1 = (t.v[1] == u) ? newPos : vertices[t.v[1]].v.pos;
        glm::vec3 p2 = (t.v[2] == u) ? newPos : vertices[t.v[2]].v.pos;

        glm::vec3 d1 = p1 - p0;
        glm::vec3 d2 = p2 - p0;
        glm::vec3 newN = glm::cross(d1, d2);
        float len = glm::length(newN);
        if (len < 1e-9f) return true; // Degenerate / inverted face

        newN /= len;
        if (glm::dot(newN, t.normal) < 0.05f) {
            return true; // Flipped normal!
        }
    }
    return false;
}

static double CalculateEdgeCollapseError(const std::vector<SimpVertex>& vertices,
                                        const std::vector<SimpTriangle>& triangles,
                                        const std::vector<int>& refs,
                                        uint32_t u, uint32_t v,
                                        glm::vec3& outPos) {
    SymmMatrix q = vertices[u].q + vertices[v].q;
    bool isBorder = vertices[u].border || vertices[v].border;

    glm::vec3 pU = vertices[u].v.pos;
    glm::vec3 pV = vertices[v].v.pos;
    glm::vec3 pMid = 0.5f * (pU + pV);

    double errU = ComputeVertexError(q, pU);
    double errV = ComputeVertexError(q, pV);
    double errMid = ComputeVertexError(q, pMid);

    // Prefer midpoint or endpoint with minimal quadric error
    double minErr = errMid;
    outPos = pMid;

    if (errU < minErr) { minErr = errU; outPos = pU; }
    if (errV < minErr) { minErr = errV; outPos = pV; }

    if (isBorder) {
        minErr += 200.0; // Penalty for collapsing along borders
    }

    if (WouldFlipNormal(vertices, triangles, refs, u, v, outPos) ||
        WouldFlipNormal(vertices, triangles, refs, v, u, outPos)) {
        return 1e30; // Reject flip
    }

    return minErr;
}

MeshData MeshSimplifier::Simplify(const MeshData& inputMesh, float targetRatio, float aggressiveness) {
    if (inputMesh.indices.empty() || inputMesh.vertices.empty()) {
        return inputMesh;
    }
    targetRatio = std::clamp(targetRatio, 0.01f, 1.0f);
    size_t targetTriangles = static_cast<size_t>(std::max(1.0f, (inputMesh.indices.size() / 3) * targetRatio));
    return SimplifyToTriangleCount(inputMesh, targetTriangles, aggressiveness);
}

MeshData MeshSimplifier::SimplifyToTriangleCount(const MeshData& inputMesh, size_t targetTriangles, float aggressiveness) {
    size_t initialTriangles = inputMesh.indices.size() / 3;
    if (initialTriangles <= targetTriangles || initialTriangles < 4) {
        return inputMesh;
    }

    std::vector<SimpVertex> vertices(inputMesh.vertices.size());
    for (size_t i = 0; i < inputMesh.vertices.size(); ++i) {
        vertices[i].v = inputMesh.vertices[i];
        vertices[i].q = SymmMatrix(0.0);
    }

    std::vector<SimpTriangle> triangles(initialTriangles);
    for (size_t i = 0; i < initialTriangles; ++i) {
        triangles[i].v[0] = inputMesh.indices[i * 3 + 0];
        triangles[i].v[1] = inputMesh.indices[i * 3 + 1];
        triangles[i].v[2] = inputMesh.indices[i * 3 + 2];
        triangles[i].deleted = false;
        triangles[i].dirty = false;
    }

    // 1. Compute initial plane quadrics for each face
    for (size_t i = 0; i < triangles.size(); ++i) {
        SimpTriangle& t = triangles[i];
        glm::vec3 p0 = vertices[t.v[0]].v.pos;
        glm::vec3 p1 = vertices[t.v[1]].v.pos;
        glm::vec3 p2 = vertices[t.v[2]].v.pos;

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;
        glm::vec3 n = glm::cross(e1, e2);
        float len = glm::length(n);
        if (len > 1e-9f) {
            n /= len;
            t.normal = n;
            float d = -glm::dot(n, p0);
            SymmMatrix q(n.x, n.y, n.z, d);
            vertices[t.v[0]].q += q;
            vertices[t.v[1]].q += q;
            vertices[t.v[2]].q += q;
        } else {
            t.deleted = true;
        }
    }

    // 2. Detect border vertices and add penalty planes
    std::unordered_map<EdgeRef, int, EdgeHash> edgeCount;
    for (const auto& t : triangles) {
        if (t.deleted) continue;
        edgeCount[{t.v[0], t.v[1]}]++;
        edgeCount[{t.v[1], t.v[2]}]++;
        edgeCount[{t.v[2], t.v[0]}]++;
    }

    for (const auto& kv : edgeCount) {
        if (kv.second == 1) { // Border edge
            uint32_t v0 = kv.first.v0;
            uint32_t v1 = kv.first.v1;
            vertices[v0].border = true;
            vertices[v1].border = true;

            glm::vec3 e = vertices[v1].v.pos - vertices[v0].v.pos;
            float eLen = glm::length(e);
            if (eLen > 1e-7f) {
                e /= eLen;
                // Add boundary constraint quadric
                glm::vec3 n = vertices[v0].v.normal;
                glm::vec3 bPlane = glm::normalize(glm::cross(e, n));
                float d = -glm::dot(bPlane, vertices[v0].v.pos);
                SymmMatrix bQ(bPlane.x, bPlane.y, bPlane.z, d);
                vertices[v0].q += bQ;
                vertices[v1].q += bQ;
            }
        }
    }

    // 3. Build vertex-to-triangle reference table
    std::vector<int> refs;
    auto UpdateReferences = [&]() {
        for (auto& v : vertices) v.tcount = 0;
        for (const auto& t : triangles) {
            if (t.deleted) continue;
            vertices[t.v[0]].tcount++;
            vertices[t.v[1]].tcount++;
            vertices[t.v[2]].tcount++;
        }
        int total = 0;
        for (auto& v : vertices) {
            v.tstart = total;
            total += v.tcount;
            v.tcount = 0;
        }
        refs.resize(total);
        for (int i = 0; i < static_cast<int>(triangles.size()); ++i) {
            const auto& t = triangles[i];
            if (t.deleted) continue;
            refs[vertices[t.v[0]].tstart + vertices[t.v[0]].tcount++] = i;
            refs[vertices[t.v[1]].tstart + vertices[t.v[1]].tcount++] = i;
            refs[vertices[t.v[2]].tstart + vertices[t.v[2]].tcount++] = i;
        }
    };

    UpdateReferences();

    // 4. Iterative Edge Collapse Passes
    size_t activeTriangles = 0;
    for (const auto& t : triangles) if (!t.deleted) activeTriangles++;

    struct EdgeCandidate {
        uint32_t u;
        uint32_t v;
        double error;
        glm::vec3 optPos;
    };

    int maxIterations = 35;
    for (int iter = 0; iter < maxIterations; ++iter) {
        if (activeTriangles <= targetTriangles) break;

        // Collect all edges in active mesh
        std::vector<EdgeCandidate> candidates;
        for (const auto& t : triangles) {
            if (t.deleted) continue;
            for (int e = 0; e < 3; ++e) {
                uint32_t u = t.v[e];
                uint32_t v = t.v[(e + 1) % 3];
                if (u > v) std::swap(u, v);

                glm::vec3 optPos(0.0f);
                double err = CalculateEdgeCollapseError(vertices, triangles, refs, u, v, optPos);
                if (err < 1e20) {
                    candidates.push_back({u, v, err, optPos});
                }
            }
        }

        if (candidates.empty()) break;

        // Sort collapses by increasing error
        std::sort(candidates.begin(), candidates.end(), [](const EdgeCandidate& a, const EdgeCandidate& b) {
            return a.error < b.error;
        });

        // Threshold error for this pass based on aggressiveness
        double threshold = (iter < 5) ? 0.001 * std::pow(iter + 1, 2) : 1e25;

        std::vector<bool> vertexLocked(vertices.size(), false);
        size_t collapsedInThisPass = 0;

        for (const auto& cand : candidates) {
            if (cand.error > threshold && iter < 5) continue;
            if (activeTriangles <= targetTriangles) break;
            if (vertexLocked[cand.u] || vertexLocked[cand.v]) continue;

            uint32_t u = cand.u;
            uint32_t v = cand.v;

            // Execute Collapse u -> v
            vertexLocked[u] = true;
            vertexLocked[v] = true;

            // Interpolate vertex attributes
            vertices[v].v.pos = cand.optPos;
            vertices[v].v.normal = glm::normalize(vertices[u].v.normal + vertices[v].v.normal);
            vertices[v].v.texCoord = 0.5f * (vertices[u].v.texCoord + vertices[v].v.texCoord);
            if (vertices[u].v.boneWeights.x > vertices[v].v.boneWeights.x) {
                vertices[v].v.boneIndices = vertices[u].v.boneIndices;
                vertices[v].v.boneWeights = vertices[u].v.boneWeights;
            }
            vertices[v].q += vertices[u].q;

            // Remap references from u to v, delete degenerates
            for (int k = 0; k < vertices[u].tcount; ++k) {
                int tIdx = refs[vertices[u].tstart + k];
                SimpTriangle& t = triangles[tIdx];
                if (t.deleted) continue;

                if (t.v[0] == u) t.v[0] = v;
                if (t.v[1] == u) t.v[1] = v;
                if (t.v[2] == u) t.v[2] = v;

                if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[2] == t.v[0]) {
                    t.deleted = true;
                    activeTriangles--;
                }
            }

            collapsedInThisPass++;
        }

        UpdateReferences();
        if (collapsedInThisPass == 0 && iter >= 5) break;
    }

    // 5. Compact Vertices and Assemble Resulting MeshData
    MeshData result;
    std::vector<int> remap(vertices.size(), -1);

    for (const auto& t : triangles) {
        if (t.deleted) continue;
        for (int k = 0; k < 3; ++k) {
            uint32_t vIdx = t.v[k];
            if (remap[vIdx] == -1) {
                remap[vIdx] = static_cast<int>(result.vertices.size());
                result.vertices.push_back(vertices[vIdx].v);
            }
            result.indices.push_back(static_cast<uint32_t>(remap[vIdx]));
        }
    }

    return result;
}

LODChainResult MeshSimplifier::GenerateLODChain(const MeshData& baseMesh,
                                                const std::vector<float>& targetRatios,
                                                const std::vector<float>& distances) {
    LODChainResult chain;
    chain.levels.resize(targetRatios.size());
    chain.suggestedDistances = distances;
    chain.reductionRatios = targetRatios;
    chain.triangleCounts.resize(targetRatios.size(), 0);

    for (size_t i = 0; i < targetRatios.size(); ++i) {
        chain.levels[i] = Simplify(baseMesh, targetRatios[i]);
        chain.triangleCounts[i] = static_cast<uint32_t>(chain.levels[i].indices.size() / 3);
    }

    return chain;
}

} // namespace Geometry

