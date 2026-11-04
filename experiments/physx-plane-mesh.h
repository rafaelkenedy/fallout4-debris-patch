#pragma once

#include "PxPhysicsAPI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Debris {
using Vector = physx::PxVec3T<double>;
using Plane = std::array<float, 4>;

struct TriangleMesh {
    std::vector<physx::PxVec3> vertices;
    std::vector<physx::PxU32> indices;
};

// The interior is n.dot(x) + d <= 0. Build a closed, outward-facing surface.
inline TriangleMesh PlaneMesh(const std::vector<Plane>& input) {
    struct HalfSpace { Vector normal; double offset; };
    constexpr double parallel = 1.e-10;
    constexpr double tolerance = 1.e-6;
    std::vector<HalfSpace> planes;
    for (const auto& p : input) {
        const Vector normal(p[0], p[1], p[2]);
        const double length = normal.magnitude();
        if (!std::isfinite(length) || length <= parallel || !std::isfinite(p[3])) return {};
        const HalfSpace plane{normal / length, p[3] / length};
        const auto duplicate = std::find_if(planes.begin(), planes.end(), [&](const auto& old) {
            return (old.normal - plane.normal).magnitudeSquared() <= parallel * parallel &&
                   std::abs(old.offset - plane.offset) <= parallel;
        });
        if (duplicate == planes.end()) planes.push_back(plane);
    }
    if (planes.size() < 4) return {};

    TriangleMesh mesh;
    std::vector<std::vector<physx::PxU32>> faces(planes.size());
    for (size_t i = 0; i < planes.size(); ++i) {
        const auto& a = planes[i];
        for (size_t j = i + 1; j < planes.size(); ++j) {
            const auto& b = planes[j];
            const Vector axis = a.normal.cross(b.normal);
            if (axis.magnitudeSquared() <= parallel * parallel) continue;
            // A feasible recession ray means the half-spaces are unbounded.
            const auto ray = axis.getNormalized();
            bool positive = false, negative = false;
            for (const auto& plane : planes) {
                const double dot = plane.normal.dot(ray);
                positive |= dot > parallel;
                negative |= dot < -parallel;
                if (positive && negative) break;
            }
            if (!positive || !negative) return {};
            for (size_t k = j + 1; k < planes.size(); ++k) {
                const auto& c = planes[k];
                const double determinant = axis.dot(c.normal);
                if (std::abs(determinant) <= parallel) continue;
                const Vector vertex = (-a.offset * b.normal.cross(c.normal) -
                                       b.offset * c.normal.cross(a.normal) -
                                       c.offset * axis) / determinant;
                if (std::any_of(planes.begin(), planes.end(), [&](const auto& plane) {
                    return plane.normal.dot(vertex) + plane.offset > tolerance;
                })) continue;
                const physx::PxVec3 point(static_cast<float>(vertex.x),
                                         static_cast<float>(vertex.y),
                                         static_cast<float>(vertex.z));
                if (!point.isFinite()) return {};
                const auto duplicate = std::find_if(mesh.vertices.begin(), mesh.vertices.end(),
                    [&](const auto& old) { return (old - point).magnitudeSquared() <= tolerance * tolerance; });
                const auto index = static_cast<physx::PxU32>(duplicate - mesh.vertices.begin());
                if (duplicate == mesh.vertices.end()) mesh.vertices.push_back(point);
                for (const auto face : {i, j, k}) {
                    auto& indices = faces[face];
                    if (std::find(indices.begin(), indices.end(), index) == indices.end()) indices.push_back(index);
                }
            }
        }
    }
    if (mesh.vertices.size() < 4) return {};
    Vector interior(0);
    for (const auto& point : mesh.vertices) interior += Vector(point.x, point.y, point.z);
    interior /= static_cast<double>(mesh.vertices.size());
    for (const auto& plane : planes) {
        if (plane.normal.dot(interior) + plane.offset >= -tolerance) return {};
    }
    for (size_t i = 0; i < faces.size(); ++i) {
        auto& face = faces[i];
        if (face.size() < 3) continue;
        const auto point = [&](physx::PxU32 index) {
            const auto& p = mesh.vertices[index];
            return Vector(p.x, p.y, p.z);
        };
        Vector center(0);
        for (auto index : face) center += point(index);
        center /= static_cast<double>(face.size());
        const auto u = (point(face.front()) - center).getNormalized();
        const auto v = planes[i].normal.cross(u);
        const auto angle = [&](physx::PxU32 index) {
            const auto relative = point(index) - center;
            return std::atan2(v.dot(relative), u.dot(relative));
        };
        std::sort(face.begin(), face.end(), [&](auto a, auto b) { return angle(a) < angle(b); });
        for (size_t j = 1; j + 1 < face.size(); ++j) {
            mesh.indices.insert(mesh.indices.end(), {face[0], face[j], face[j + 1]});
        }
    }
    return mesh;
}
}
