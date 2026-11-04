#include <cmath>
#include <cstddef>
#include <new>
#include "flex-legacy-asset.h"

static_assert(sizeof(LegacyRigidAsset) == 0x80);
static_assert(offsetof(LegacyRigidAsset, shapeIndices) == 0x30);
static_assert(offsetof(LegacyRigidAsset, shapeCenters) == 0x50);
static_assert(offsetof(LegacyRigidAsset, shapeCount) == 0x58);

namespace {
decltype(&NvFlexExtCreateRigidFromMesh) createAsset;
decltype(&NvFlexExtDestroyAsset) destroyAsset;

struct OwnedAsset {
    LegacyRigidAsset legacy{};
    NvFlexExtAsset* native;
    decltype(&NvFlexExtDestroyAsset) destroy;
    float center[4]{};
};
}

void BindLegacyAssetBackend(decltype(&NvFlexExtCreateRigidFromMesh) create,
                            decltype(&NvFlexExtDestroyAsset) destroy) {
    createAsset = create;
    destroyAsset = destroy;
}

LegacyRigidAsset* flexExtCreateRigidFromMesh(const float* vertices, int vertexCount,
    const int* indices, int indexCount, float spacing, float expand) {
    if (!createAsset || !destroyAsset || !vertices || vertexCount < 3 || !indices ||
        indexCount < 3 || indexCount % 3 || !std::isfinite(spacing) || spacing <= 0 ||
        !std::isfinite(expand)) return nullptr;
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] < 0 || indices[i] >= vertexCount) return nullptr;
    }
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
    owned->destroy(owned->native);
    delete owned;
}
