#include <new>
#include "flex-legacy-mesh.h"
#include "flex-legacy-particles.h"

struct LegacyTriangleMesh {
    NvFlexLibrary* library;
    NvFlexTriangleMeshId id;
    decltype(&NvFlexDestroyTriangleMesh) destroy;
    FlexTriangleTransfer transfer;
    bool ready = false;
};

namespace {
const LegacyMeshApi* meshApi;
}

void BindLegacyMeshApi(const LegacyMeshApi* api) {
    meshApi = api;
}

NvFlexTriangleMeshId NativeMeshId(const LegacyTriangleMesh* mesh) {
    return LegacyMeshReady(mesh) ? mesh->id : 0;
}

bool LegacyMeshReady(const LegacyTriangleMesh* mesh) {
    return mesh && mesh->ready;
}

LegacyTriangleMesh* flexCreateTriangleMesh() {
    auto* library = LegacySolverLibrary();
    if (!meshApi || !library) return nullptr;
    const auto id = meshApi->create(library);
    if (!id) return nullptr;
    auto* mesh = new (std::nothrow) LegacyTriangleMesh{
        library, id, meshApi->destroy, FlexTriangleTransfer(library, meshApi->buffers, meshApi->update)
    };
    if (!mesh) meshApi->destroy(library, id);
    return mesh;
}

void flexDestroyTriangleMesh(LegacyTriangleMesh* mesh) {
    if (!mesh) return;
    mesh->destroy(mesh->library, mesh->id);
    delete mesh;
}

void flexUpdateTriangleMesh(LegacyTriangleMesh* mesh, const FlexPoint* vertices,
                            const int* indices, int vertexCount, int triangleCount,
                            const float* lower, const float* upper, int memory) {
    if (!mesh) return;
    mesh->ready = false;
    if (memory != 0 && memory != 2) return;
    try {
        mesh->ready = mesh->transfer.Upload(mesh->id, vertices, vertexCount, indices,
                                            triangleCount, lower, upper);
    } catch (const std::bad_alloc&) {
        // Leave the handle unusable until a subsequent upload succeeds.
    }
}
