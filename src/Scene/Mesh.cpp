#include "Mesh.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MeshData MeshGenerator::CreateCube(float size) {
    MeshData mesh;
    float h = size * 0.5f;

    // 6 faces * 4 vertices = 24 vertices
    // Front face (Z+)
    mesh.vertices.push_back({ {-h, -h,  h}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ { h, -h,  h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ { h,  h,  h}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ {-h,  h,  h}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} });

    // Back face (Z-)
    mesh.vertices.push_back({ { h, -h, -h}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ {-h, -h, -h}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ {-h,  h, -h}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ { h,  h, -h}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f} });

    // Left face (X-)
    mesh.vertices.push_back({ {-h, -h, -h}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ {-h, -h,  h}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ {-h,  h,  h}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ {-h,  h, -h}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} });

    // Right face (X+)
    mesh.vertices.push_back({ { h, -h,  h}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ { h, -h, -h}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ { h,  h, -h}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ { h,  h,  h}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} });

    // Top face (Y+)
    mesh.vertices.push_back({ {-h,  h,  h}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ { h,  h,  h}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ { h,  h, -h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ {-h,  h, -h}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} });

    // Bottom face (Y-)
    mesh.vertices.push_back({ {-h, -h, -h}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ { h, -h, -h}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ { h, -h,  h}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ {-h, -h,  h}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f} });

    for (uint32_t i = 0; i < 6; ++i) {
        uint32_t offset = i * 4;
        mesh.indices.push_back(offset + 0);
        mesh.indices.push_back(offset + 1);
        mesh.indices.push_back(offset + 2);
        mesh.indices.push_back(offset + 2);
        mesh.indices.push_back(offset + 3);
        mesh.indices.push_back(offset + 0);
    }

    return mesh;
}

MeshData MeshGenerator::CreateSphere(float radius, uint32_t rings, uint32_t sectors) {
    MeshData mesh;
    float const R = 1.0f / (float)(rings - 1);
    float const S = 1.0f / (float)(sectors - 1);

    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            float y = sinf(-static_cast<float>(M_PI) / 2.0f + static_cast<float>(M_PI) * r * R);
            float x = cosf(2.0f * static_cast<float>(M_PI) * s * S) * sinf(static_cast<float>(M_PI) * r * R);
            float z = sinf(2.0f * static_cast<float>(M_PI) * s * S) * sinf(static_cast<float>(M_PI) * r * R);

            glm::vec3 pos = glm::vec3(x, y, z) * radius;
            glm::vec3 normal = glm::normalize(glm::vec3(x, y, z));
            glm::vec2 uv = glm::vec2(s * S, r * R);

            mesh.vertices.push_back({ pos, normal, uv });
        }
    }

    for (uint32_t r = 0; r < rings - 1; ++r) {
        for (uint32_t s = 0; s < sectors - 1; ++s) {
            uint32_t cur = r * sectors + s;
            uint32_t next = (r + 1) * sectors + s;

            mesh.indices.push_back(cur);
            mesh.indices.push_back(next);
            mesh.indices.push_back(next + 1);

            mesh.indices.push_back(cur);
            mesh.indices.push_back(next + 1);
            mesh.indices.push_back(cur + 1);
        }
    }

    return mesh;
}

MeshData MeshGenerator::CreateCone(float radius, float height, uint32_t segments) {
    MeshData mesh;
    float halfH = height * 0.5f;

    // Apex vertex
    glm::vec3 apex(0.0f, halfH, 0.0f);
    glm::vec3 baseCenter(0.0f, -halfH, 0.0f);

    uint32_t apexIndex = (uint32_t)mesh.vertices.size();
    mesh.vertices.push_back({ apex, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f} });

    // Base center vertex
    uint32_t baseCenterIndex = (uint32_t)mesh.vertices.size();
    mesh.vertices.push_back({ baseCenter, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f} });

    // Base ring vertices for mantle and bottom
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = (float)i / (float)segments * 2.0f * static_cast<float>(M_PI);
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;

        glm::vec3 normal = glm::normalize(glm::vec3(x, radius / height, z));
        mesh.vertices.push_back({ {x, -halfH, z}, normal, {(float)i / segments, 0.0f} });
    }

    // Cone mantle indices (counter-clockwise seen from outside, like the cube)
    uint32_t ringStart = 2;
    for (uint32_t i = 0; i < segments; ++i) {
        mesh.indices.push_back(apexIndex);
        mesh.indices.push_back(ringStart + i + 1);
        mesh.indices.push_back(ringStart + i);
    }

    // Cone bottom cap
    uint32_t bottomRingStart = (uint32_t)mesh.vertices.size();
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = (float)i / (float)segments * 2.0f * static_cast<float>(M_PI);
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;
        mesh.vertices.push_back({ {x, -halfH, z}, {0.0f, -1.0f, 0.0f}, {cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f} });
    }

    for (uint32_t i = 0; i < segments; ++i) {
        mesh.indices.push_back(baseCenterIndex);
        mesh.indices.push_back(bottomRingStart + i);
        mesh.indices.push_back(bottomRingStart + i + 1);
    }

    return mesh;
}

MeshData MeshGenerator::CreateCylinder(float radius, float height, uint32_t segments) {
    MeshData mesh;
    float halfH = height * 0.5f;

    // Side vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = (float)i / (float)segments * 2.0f * static_cast<float>(M_PI);
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;
        glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
        float u = (float)i / segments;

        mesh.vertices.push_back({ {x,  halfH, z}, normal, {u, 1.0f} });
        mesh.vertices.push_back({ {x, -halfH, z}, normal, {u, 0.0f} });
    }

    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t top1 = i * 2;
        uint32_t bot1 = i * 2 + 1;
        uint32_t top2 = (i + 1) * 2;
        uint32_t bot2 = (i + 1) * 2 + 1;

        // Counter-clockwise seen from outside, like the cube
        mesh.indices.push_back(top1);
        mesh.indices.push_back(top2);
        mesh.indices.push_back(bot1);

        mesh.indices.push_back(top2);
        mesh.indices.push_back(bot2);
        mesh.indices.push_back(bot1);
    }

    // Top cap
    uint32_t topCenterIndex = (uint32_t)mesh.vertices.size();
    mesh.vertices.push_back({ {0.0f, halfH, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f} });
    uint32_t topRingStart = (uint32_t)mesh.vertices.size();
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = (float)i / (float)segments * 2.0f * static_cast<float>(M_PI);
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;
        mesh.vertices.push_back({ {x, halfH, z}, {0.0f, 1.0f, 0.0f}, {cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f} });
    }
    for (uint32_t i = 0; i < segments; ++i) {
        mesh.indices.push_back(topCenterIndex);
        mesh.indices.push_back(topRingStart + i + 1);
        mesh.indices.push_back(topRingStart + i);
    }

    // Bottom cap
    uint32_t botCenterIndex = (uint32_t)mesh.vertices.size();
    mesh.vertices.push_back({ {0.0f, -halfH, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f} });
    uint32_t botRingStart = (uint32_t)mesh.vertices.size();
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = (float)i / (float)segments * 2.0f * static_cast<float>(M_PI);
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;
        mesh.vertices.push_back({ {x, -halfH, z}, {0.0f, -1.0f, 0.0f}, {cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f} });
    }
    for (uint32_t i = 0; i < segments; ++i) {
        mesh.indices.push_back(botCenterIndex);
        mesh.indices.push_back(botRingStart + i);
        mesh.indices.push_back(botRingStart + i + 1);
    }

    return mesh;
}

MeshData MeshGenerator::CreatePlane(float size) {
    MeshData mesh;
    float h = size * 0.5f;

    mesh.vertices.push_back({ {-h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ { h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ { h, 0.0f,  h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ {-h, 0.0f,  h}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} });

    mesh.indices.push_back(0);
    mesh.indices.push_back(2);
    mesh.indices.push_back(1);

    mesh.indices.push_back(0);
    mesh.indices.push_back(3);
    mesh.indices.push_back(2);

    return mesh;
}


MeshData MeshGenerator::CreateQuad(float size) {
    MeshData mesh;
    float h = size * 0.5f;

    mesh.vertices.push_back({ {-h, -h, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ { h, -h, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f} });
    mesh.vertices.push_back({ { h,  h, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} });
    mesh.vertices.push_back({ {-h,  h, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} });

    mesh.indices.push_back(0);
    mesh.indices.push_back(1);
    mesh.indices.push_back(2);

    mesh.indices.push_back(0);
    mesh.indices.push_back(2);
    mesh.indices.push_back(3);

    return mesh;
}

MeshData MeshGenerator::CreateGrid(float size, int divisions) {
    MeshData mesh;
    float halfSize = size * 0.5f;
    float step = size / (float)divisions;

    uint32_t index = 0;
    // Lines parallel to Z axis (varying X)
    for (int i = 0; i <= divisions; ++i) {
        float x = -halfSize + i * step;
        mesh.vertices.push_back({ {x, 0.0f, -halfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} });
        mesh.vertices.push_back({ {x, 0.0f,  halfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} });
        mesh.indices.push_back(index++);
        mesh.indices.push_back(index++);
    }

    // Lines parallel to X axis (varying Z)
    for (int i = 0; i <= divisions; ++i) {
        float z = -halfSize + i * step;
        mesh.vertices.push_back({ {-halfSize, 0.0f, z}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} });
        mesh.vertices.push_back({ { halfSize, 0.0f, z}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} });
        mesh.indices.push_back(index++);
        mesh.indices.push_back(index++);
    }

    return mesh;
}

MeshData MeshGenerator::CreateAxes(float size) {
    MeshData mesh;
    float halfSize = size * 0.5f;

    // X Axis line (Red)
    mesh.vertices.push_back({ {-halfSize, 0.001f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ { halfSize, 0.001f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} });
    mesh.indices.push_back(0);
    mesh.indices.push_back(1);

    // Z Axis line (Blue)
    mesh.vertices.push_back({ {0.0f, 0.001f, -halfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} });
    mesh.vertices.push_back({ {0.0f, 0.001f,  halfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} });
    mesh.indices.push_back(2);
    mesh.indices.push_back(3);

    return mesh;
}

