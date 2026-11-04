#pragma once

#include "flex-triangle-transfer.h"
#include "experiments/physx-scene-shapes.h"

struct LegacyTriangleMesh;

struct LegacyMeshApi {
    FlexBufferApi buffers;
    decltype(&NvFlexCreateTriangleMesh) create;
    decltype(&NvFlexUpdateTriangleMesh) update;
    decltype(&NvFlexDestroyTriangleMesh) destroy;
};

void BindLegacyMeshApi(const LegacyMeshApi* api);
NvFlexTriangleMeshId NativeMeshId(const LegacyTriangleMesh* mesh);
bool LegacyMeshReady(const LegacyTriangleMesh* mesh);
void EnableLegacyMeshCapture();
bool DescribeLegacyMesh(const LegacyTriangleMesh* mesh, PhysXSceneShape& shape);

extern "C" {
LegacyTriangleMesh* flexCreateTriangleMesh();
void flexDestroyTriangleMesh(LegacyTriangleMesh*);
void flexUpdateTriangleMesh(LegacyTriangleMesh*, const FlexPoint*, const int*,
                            int vertexCount, int triangleCount,
                            const float* lower, const float* upper, int memory);
}
