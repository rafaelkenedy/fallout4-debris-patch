#pragma once

#include <cmath>
#include <limits>
#include <vector>
#include "flex-host-transfer.h"

struct FlexPoint { float x, y, z; };

class FlexTriangleTransfer {
public:
    FlexTriangleTransfer(NvFlexLibrary* library, FlexBufferApi api,
                         decltype(&NvFlexUpdateTriangleMesh) update)
        : library_(library), update_(update),
          vertices_(library, api, sizeof(Vertex)), indices_(library, api, sizeof(int)) {}

    bool Upload(NvFlexTriangleMeshId mesh, const FlexPoint* points, int vertexCount,
                const int* indices, int triangleCount,
                const float* suppliedLower = nullptr, const float* suppliedUpper = nullptr) {
        if (!mesh || !points || !indices || vertexCount <= 0 || triangleCount <= 0 ||
            triangleCount > std::numeric_limits<int>::max() / 3) return false;
        const int indexCount = triangleCount * 3;
        for (int i = 0; i < indexCount; ++i) {
            if (indices[i] < 0 || indices[i] >= vertexCount) return false;
        }

        const float limit = std::numeric_limits<float>::infinity();
        float lower[] = {limit, limit, limit};
        float upper[] = {-limit, -limit, -limit};
        std::vector<Vertex> packed;
        packed.reserve(vertexCount);
        for (int i = 0; i < vertexCount; ++i) {
            const auto& point = points[i];
            const float axes[] = {point.x, point.y, point.z};
            for (int axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(axes[axis])) return false;
                if (axes[axis] < lower[axis]) lower[axis] = axes[axis];
                if (axes[axis] > upper[axis]) upper[axis] = axes[axis];
            }
            packed.push_back({point.x, point.y, point.z, 0});
        }
        auto* vertices = vertices_.Stage(packed.data(), vertexCount);
        auto* triangles = indices_.Stage(indices, indexCount);
        if (!vertices || !triangles) return false;
        if (suppliedLower && suppliedUpper) {
            for (int axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(suppliedLower[axis]) || !std::isfinite(suppliedUpper[axis]) ||
                    suppliedLower[axis] > suppliedUpper[axis]) return false;
                lower[axis] = suppliedLower[axis];
                upper[axis] = suppliedUpper[axis];
            }
        }
        update_(library_, mesh, vertices, triangles, vertexCount, triangleCount, lower, upper);
        return true;
    }

private:
    struct Vertex { float x, y, z, padding; };
    NvFlexLibrary* library_;
    decltype(&NvFlexUpdateTriangleMesh) update_;
    FlexHostTransfer vertices_;
    FlexHostTransfer indices_;
};
