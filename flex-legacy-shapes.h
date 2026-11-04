#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "flex-legacy-mesh.h"
#include "flex-legacy-particles.h"

namespace LegacyShapes {
struct Vector { double x, y, z; };
inline Vector Cross(Vector a, Vector b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline double Dot(Vector a, Vector b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

using Plane = std::array<float, 4>;
inline constexpr std::size_t maxConvexPlanes = 63;

// Retain original half-spaces, favoring coverage of distinct normal directions.
// Dropping planes gives an outer approximation and cannot cut into the source.
inline bool ReduceConvex(std::vector<Plane>& planes) {
    if (planes.size() <= maxConvexPlanes) return true;
    std::vector<Vector> normals;
    normals.reserve(planes.size());
    for (const auto& p : planes) {
        const Vector n{p[0], p[1], p[2]};
        const double length = std::sqrt(Dot(n, n));
        if (length <= 1.e-12) return false;
        normals.push_back({n.x / length, n.y / length, n.z / length});
    }

    std::vector<Plane> selected;
    std::vector<Vector> selectedNormals;
    std::vector<double> nearest(planes.size(), -2);
    auto select = [&](std::size_t index) {
        if (nearest[index] == 2) return;
        selected.push_back(planes[index]);
        selectedNormals.push_back(normals[index]);
        nearest[index] = 2;
        for (std::size_t i = 0; i < normals.size(); ++i) {
            nearest[i] = std::max(nearest[i], Dot(normals[i], normals[index]));
        }
    };
    constexpr Vector axes[]{{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
    for (const auto axis : axes) {
        const auto best = std::max_element(normals.begin(), normals.end(), [&](Vector a, Vector b) {
            return Dot(a, axis) < Dot(b, axis);
        });
        select(static_cast<std::size_t>(best - normals.begin()));
    }
    while (selected.size() < maxConvexPlanes) {
        const auto farthest = std::min_element(nearest.begin(), nearest.end());
        select(static_cast<std::size_t>(farthest - nearest.begin()));
    }

    // A full-dimensional unbounded recession cone has a feasible direction
    // along a plane-pair intersection, or a lineality direction.
    bool hasIndependentPair = false;
    for (std::size_t i = 0; i < selectedNormals.size(); ++i) {
        for (std::size_t j = i + 1; j < selectedNormals.size(); ++j) {
            const auto cross = Cross(selectedNormals[i], selectedNormals[j]);
            const double length = std::sqrt(Dot(cross, cross));
            if (length <= 1.e-10) continue;
            hasIndependentPair = true;
            const Vector ray{cross.x / length, cross.y / length, cross.z / length};
            bool positive = false, negative = false;
            for (const auto n : selectedNormals) {
                const double projection = Dot(n, ray);
                positive |= projection > 1.e-9;
                negative |= projection < -1.e-9;
                if (positive && negative) break;
            }
            if (!positive || !negative) return false;
        }
    }
    if (!hasIndependentPair) return false;
    planes = std::move(selected);
    return true;
}

inline bool ConvexBounds(const std::vector<float>& planes, float* lower, float* upper) {
    const auto count = planes.size() / 4;
    std::vector<Vector> normals;
    normals.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float* plane = &planes[i * 4];
        for (int j = 0; j < 4; ++j) {
            if (!std::isfinite(plane[j])) return false;
        }
        normals.push_back({plane[0], plane[1], plane[2]});
    }
    bool found = false;
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            const auto ij = Cross(normals[i], normals[j]);
            for (std::size_t k = j + 1; k < count; ++k) {
                const double det = Dot(ij, normals[k]);
                if (std::abs(det) < 1.e-10) continue;
                const auto jk = Cross(normals[j], normals[k]);
                const auto ki = Cross(normals[k], normals[i]);
                const double di = -planes[i * 4 + 3] / det;
                const double dj = -planes[j * 4 + 3] / det;
                const double dk = -planes[k * 4 + 3] / det;
                const Vector vertex{
                    di * jk.x + dj * ki.x + dk * ij.x,
                    di * jk.y + dj * ki.y + dk * ij.y,
                    di * jk.z + dj * ki.z + dk * ij.z
                };
                bool inside = true;
                for (std::size_t n = 0; n < count; ++n) {
                    if (Dot(normals[n], vertex) + planes[n * 4 + 3] > 1.e-5) {
                        inside = false;
                        break;
                    }
                }
                if (!inside) continue;
                const float xyz[] = {
                    static_cast<float>(vertex.x), static_cast<float>(vertex.y),
                    static_cast<float>(vertex.z)
                };
                for (int axis = 0; axis < 3; ++axis) {
                    if (!std::isfinite(xyz[axis])) return false;
                    lower[axis] = found ? std::min(lower[axis], xyz[axis]) : xyz[axis];
                    upper[axis] = found ? std::max(upper[axis], xyz[axis]) : xyz[axis];
                }
                found = true;
            }
        }
    }
    return found;
}

inline bool Convert(NvFlexLibrary* library, const LegacySolverBackend& backend,
                    FlexHostTransfer& planeTransfer, const unsigned char* record,
                    int planeCount, int oldFlags, NvFlexCollisionGeometry& shape, int& newFlags,
                    std::vector<std::unique_ptr<LegacyConvexMesh>>& convexShapes,
                    int& distinctPlanes) {
    NvFlexCollisionShapeType modernType;
    switch (oldFlags & 7) {
    case 0:
    case 1: {
        float values[2]{};
        std::memcpy(values, record, sizeof(values));
        if (!std::isfinite(values[0]) || values[0] <= 0) return false;
        if ((oldFlags & 7) == 0) {
            shape.sphere.radius = values[0];
            modernType = eNvFlexShapeSphere;
        } else {
            if (!std::isfinite(values[1]) || values[1] < 0) return false;
            shape.capsule = {values[0], values[1]};
            modernType = eNvFlexShapeCapsule;
        }
        break;
    }
    case 2: {
        if (planeCount < 4 || !backend.createConvexMesh ||
            !backend.updateConvexMesh || !backend.destroyConvexMesh) return false;
        std::vector<Plane> unique;
        unique.reserve(planeCount);
        for (int i = 0; i < planeCount; ++i) {
            std::array<float, 4> plane{};
            std::memcpy(plane.data(), record + 16ull * i, 16);
            if (!std::all_of(plane.begin(), plane.end(), [](float value) {
                return std::isfinite(value);
            })) return false;
            if (std::find(unique.begin(), unique.end(), plane) != unique.end()) continue;
            unique.push_back(plane);
        }
        distinctPlanes = static_cast<int>(unique.size());
        if (unique.size() < 4) return false;
        if (!ReduceConvex(unique)) return false;
        std::vector<float> planes;
        planes.reserve(unique.size() * 4);
        for (const auto& plane : unique) planes.insert(planes.end(), plane.begin(), plane.end());
        const int count = static_cast<int>(unique.size());
        float lower[3]{}, upper[3]{};
        if (!ConvexBounds(planes, lower, upper)) return false;
        auto* buffer = planeTransfer.Stage(planes.data(), count);
        if (!buffer) return false;
        auto mesh = std::make_unique<LegacyConvexMesh>();
        mesh->library = library;
        mesh->destroy = backend.destroyConvexMesh;
        mesh->id = backend.createConvexMesh(library);
        if (!mesh->id) return false;
        backend.updateConvexMesh(library, mesh->id, buffer, count, lower, upper);
        backend.wait(library);
        shape.convexMesh.mesh = mesh->id;
        shape.convexMesh.scale[0] = shape.convexMesh.scale[1] = shape.convexMesh.scale[2] = 1;
        convexShapes.push_back(std::move(mesh));
        modernType = eNvFlexShapeConvexMesh;
        break;
    }
    case 3: {
        LegacyTriangleMesh* mesh = nullptr;
        float scale = 0;
        std::memcpy(&mesh, record, sizeof(mesh));
        std::memcpy(&scale, record + 8, sizeof(scale));
        shape.triMesh.mesh = NativeMeshId(mesh);
        if (!shape.triMesh.mesh || !std::isfinite(scale) || scale <= 0) return false;
        shape.triMesh.scale[0] = shape.triMesh.scale[1] = shape.triMesh.scale[2] = scale;
        modernType = eNvFlexShapeTriangleMesh;
        break;
    }
    default:
        return false;
    }
    newFlags = NvFlexMakeShapeFlags(modernType, (oldFlags & 8) != 0);
    return true;
}
}
