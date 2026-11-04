#include <cmath>
#include <cstddef>
#include <new>
#include <algorithm>
#include <memory>
#include <vector>
#include "flex-legacy-asset.h"

static_assert(sizeof(LegacyRigidAsset) == 0x80);
static_assert(offsetof(LegacyRigidAsset, shapeIndices) == 0x30);
static_assert(offsetof(LegacyRigidAsset, shapeCenters) == 0x50);
static_assert(offsetof(LegacyRigidAsset, shapeCount) == 0x58);

namespace {
decltype(&NvFlexExtCreateRigidFromMesh) createAsset;
decltype(&NvFlexExtDestroyAsset) destroyAsset;
bool useHostAssets = false;

struct OwnedAsset {
    LegacyRigidAsset legacy{};
    NvFlexExtAsset* native = nullptr;
    decltype(&NvFlexExtDestroyAsset) destroy = nullptr;
    float center[4]{};
    std::vector<float> particles;
    std::vector<int> indices;
    int offsets[2]{};
    float stiffness = 1;
};

LegacyRigidAsset* MakeHostAsset(const float* vertices, const int* indices, int indexCount,
                               float spacing, float expand) {
    try {
        auto asset = std::make_unique<OwnedAsset>();
        const double minDistance = static_cast<double>(spacing) * spacing;
        double sum[3]{};
        for (int i = 0; i < indexCount; ++i) {
            const auto* vertex = vertices + 3ull * indices[i];
            if (!std::isfinite(vertex[0]) || !std::isfinite(vertex[1]) || !std::isfinite(vertex[2])) return nullptr;
            bool near = false;
            for (std::size_t j = 0; j < asset->particles.size(); j += 4) {
                const auto* p = asset->particles.data() + j;
                const double x = double(vertex[0]) - p[0], y = double(vertex[1]) - p[1], z = double(vertex[2]) - p[2];
                if (x*x + y*y + z*z < minDistance) { near = true; break; }
            }
            if (near) continue;
            asset->indices.push_back(static_cast<int>(asset->indices.size()));
            asset->particles.insert(asset->particles.end(), {vertex[0], vertex[1], vertex[2], 1.0f});
            for (int axis = 0; axis < 3; ++axis) sum[axis] += vertex[axis];
        }
        const int count = static_cast<int>(asset->indices.size());
        if (!count) return nullptr;
        for (int axis = 0; axis < 3; ++axis) asset->center[axis] = static_cast<float>(sum[axis] / count);
        if (expand != 0) {
            for (std::size_t i = 0; i < asset->particles.size(); i += 4) {
                auto* p = asset->particles.data() + i;
                double offset[3]{};
                for (int axis = 0; axis < 3; ++axis) offset[axis] = double(p[axis]) - asset->center[axis];
                const auto length = std::sqrt(offset[0]*offset[0] + offset[1]*offset[1] + offset[2]*offset[2]);
                if (length > 0) for (int axis = 0; axis < 3; ++axis) {
                    p[axis] = static_cast<float>(p[axis] + offset[axis] * expand / length);
                    if (!std::isfinite(p[axis])) return nullptr;
                }
            }
        }
        auto& result = asset->legacy;
        asset->offsets[1] = count;
        result.particles = asset->particles.data();
        result.particleCount = count;
        result.shapeIndices = asset->indices.data();
        result.shapeIndexCount = count;
        result.shapeOffsets = asset->offsets;
        result.shapeStiffness = &asset->stiffness;
        result.shapeCenters = asset->center;
        result.shapeCount = 1;
        return &asset.release()->legacy;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
}

void EnableHostRigidAssets() { useHostAssets = true; }

void BindLegacyAssetBackend(decltype(&NvFlexExtCreateRigidFromMesh) create,
                            decltype(&NvFlexExtDestroyAsset) destroy) {
    createAsset = create;
    destroyAsset = destroy;
}

LegacyRigidAsset* flexExtCreateRigidFromMesh(const float* vertices, int vertexCount,
    const int* indices, int indexCount, float spacing, float expand) {
    if ((!useHostAssets && (!createAsset || !destroyAsset)) || !vertices || vertexCount < 3 || !indices ||
        indexCount < 3 || indexCount % 3 || !std::isfinite(spacing) || spacing <= 0 ||
        !std::isfinite(expand)) return nullptr;
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] < 0 || indices[i] >= vertexCount) return nullptr;
    }
    if (useHostAssets) return MakeHostAsset(vertices, indices, indexCount, spacing, expand);
    auto* native = createAsset(vertices, vertexCount, indices, indexCount, spacing, expand);
    if (!native) return nullptr;
    auto* asset = new (std::nothrow) OwnedAsset{};
    if (!asset || native->numShapes != 1 || !native->shapeCenters) {
        destroyAsset(native);
        delete asset;
        return nullptr;
    }
    asset->native = native;
    asset->destroy = destroyAsset;
    auto& result = asset->legacy;
    result.particles = native->particles;
    result.particleCount = native->numParticles;
    result.shapeIndices = native->shapeIndices;
    result.shapeIndexCount = native->numShapeIndices;
    result.shapeOffsets = native->shapeOffsets;
    result.shapeStiffness = native->shapeCoefficients;
    for (int i = 0; i < 3; ++i) asset->center[i] = native->shapeCenters[i];
    result.shapeCenters = asset->center;
    result.shapeCount = 1;
    return &result;
}

void flexExtDestroyAsset(LegacyRigidAsset* asset) {
    if (!asset) return;
    auto* owned = reinterpret_cast<OwnedAsset*>(asset);
    if (owned->native) owned->destroy(owned->native);
    delete owned;
}
