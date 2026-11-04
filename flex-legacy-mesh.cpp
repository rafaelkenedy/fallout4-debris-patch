#include <new>
#include <atomic>
#include "flex-legacy-mesh.h"
#include "flex-legacy-particles.h"

struct LegacyTriangleMesh {
    NvFlexLibrary* library;
    NvFlexTriangleMeshId id;
    decltype(&NvFlexDestroyTriangleMesh) destroy;
    FlexTriangleTransfer transfer;
    bool ready = false;
    std::vector<FlexPoint> vertices;
    std::vector<int> triangles;
    unsigned long long revision = 0;
};

namespace {
const LegacyMeshApi* meshApi;
bool captureMeshes = false;
std::atomic<unsigned long long> nextRevision{1};
}

void EnableLegacyMeshCapture() { captureMeshes = true; }

bool DescribeLegacyMesh(const LegacyTriangleMesh* mesh, PhysXSceneShape& shape) {
    if (!LegacyMeshReady(mesh) || !mesh->revision) return false;
    shape.meshKey = mesh;
    shape.meshRevision = mesh->revision;
    shape.vertices = reinterpret_cast<const float*>(mesh->vertices.data());
    shape.vertexCount = static_cast<int>(mesh->vertices.size());
    shape.triangles = mesh->triangles.data();
    shape.triangleCount = static_cast<int>(mesh->triangles.size() / 3);
    return true;
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
    if (LegacyPhysXExclusive()) {
        return new (std::nothrow) LegacyTriangleMesh{nullptr, 0, nullptr, FlexTriangleTransfer(nullptr, {}, nullptr)};
    }
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
    if (mesh->destroy) mesh->destroy(mesh->library, mesh->id);
    delete mesh;
}

void flexUpdateTriangleMesh(LegacyTriangleMesh* mesh, const FlexPoint* vertices,
                            const int* indices, int vertexCount, int triangleCount,
                            const float* lower, const float* upper, int memory) {
    if (!mesh) return;
    mesh->ready = false;
    mesh->revision = 0;
    if (memory != 0 && memory != 2) return;
    try {
        if (mesh->destroy) {
            mesh->ready = mesh->transfer.Upload(mesh->id, vertices, vertexCount, indices,
                                                triangleCount, lower, upper);
        } else {
            if (!vertices || !indices || vertexCount < 3 || triangleCount <= 0 ||
                triangleCount > std::numeric_limits<int>::max() / 3) return;
            for (int i = 0; i < triangleCount * 3; ++i) {
                if (indices[i] < 0 || indices[i] >= vertexCount) return;
            }
            for (int i = 0; i < vertexCount; ++i) {
                const auto& p = vertices[i];
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
            }
            mesh->ready = true;
        }
        if (mesh->ready && captureMeshes) {
            mesh->vertices.assign(vertices, vertices + vertexCount);
            mesh->triangles.assign(indices, indices + 3ull * triangleCount);
            mesh->revision = nextRevision.fetch_add(1);
        }
    } catch (const std::bad_alloc&) {
        // Leave the handle unusable until a subsequent upload succeeds.
        mesh->ready = false;
    }
}
