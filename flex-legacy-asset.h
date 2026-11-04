#pragma once

#include "NvFlexExt.h"

struct LegacyRigidAsset {
    float* particles;
    int particleCount;
    unsigned char reserved1[0x24];
    int* shapeIndices;
    int shapeIndexCount;
    int* shapeOffsets;
    float* shapeStiffness;
    float* shapeCenters;
    int shapeCount;
    unsigned char reserved2[0x24];
};

void BindLegacyAssetBackend(decltype(&NvFlexExtCreateRigidFromMesh) create,
                            decltype(&NvFlexExtDestroyAsset) destroy);

extern "C" {
LegacyRigidAsset* flexExtCreateRigidFromMesh(const float* vertices, int vertexCount,
    const int* indices, int indexCount, float spacing, float expand);
void flexExtDestroyAsset(LegacyRigidAsset*);
}
