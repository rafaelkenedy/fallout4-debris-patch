#include <windows.h>
#include <cstdio>
#include <cmath>
#include "flex-triangle-transfer.h"
#include "flex-rigid-transfer.h"
#include "flex-legacy-particles.h"
#include "flex-legacy-container.h"
#include "flex-legacy-asset.h"
#include "flex-legacy-mesh.h"

static bool backendError;

static void Report(int severity, const char* message,
                   const char* file, int line) {
    fprintf(stderr, "FleX [%d] %s (%s:%d)\n", severity, message, file, line);
    if (severity == eNvFlexLogError) backendError = true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    HMODULE dll = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) {
        fprintf(stderr, "LoadLibraryExW failed: %lu\n", GetLastError());
        return 1;
    }
#define LOAD(name) auto name = reinterpret_cast<decltype(&::name)>(GetProcAddress(dll, #name)); \
    if (!name) { fprintf(stderr, "Missing %s\n", #name); return 1; }
    LOAD(NvFlexInit);
    LOAD(NvFlexShutdown);
    LOAD(NvFlexGetDeviceName);
    LOAD(NvFlexSetSolverDescDefaults);
    LOAD(NvFlexCreateSolver);
    LOAD(NvFlexDestroySolver);
    LOAD(NvFlexAllocBuffer);
    LOAD(NvFlexFreeBuffer);
    LOAD(NvFlexMap);
    LOAD(NvFlexUnmap);
    LOAD(NvFlexSetParams);
    LOAD(NvFlexGetParams);
    LOAD(NvFlexSetParticles);
    LOAD(NvFlexSetVelocities);
    LOAD(NvFlexSetPhases);
    LOAD(NvFlexSetActive);
    LOAD(NvFlexSetActiveCount);
    LOAD(NvFlexUpdateSolver);
    LOAD(NvFlexGetParticles);
    LOAD(NvFlexCreateTriangleMesh);
    LOAD(NvFlexUpdateTriangleMesh);
    LOAD(NvFlexDestroyTriangleMesh);
    LOAD(NvFlexSetShapes);
    LOAD(NvFlexSetRigids);
    LOAD(NvFlexGetRigids);
    LOAD(NvFlexGetVelocities);
    LOAD(NvFlexGetBounds);
    LOAD(NvFlexFlush);
    LOAD(NvFlexWait);
    LOAD(NvFlexCreateConvexMesh);
    LOAD(NvFlexUpdateConvexMesh);
    LOAD(NvFlexDestroyConvexMesh);
#undef LOAD
    HMODULE extension = LoadLibraryExW(argv[2], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!extension) {
        fprintf(stderr, "Loading FleX extension failed: %lu\n", GetLastError());
        return 1;
    }
    BindLegacyAssetBackend(
        reinterpret_cast<decltype(&NvFlexExtCreateRigidFromMesh)>(
            GetProcAddress(extension, "NvFlexExtCreateRigidFromMesh")),
        reinterpret_cast<decltype(&NvFlexExtDestroyAsset)>(
            GetProcAddress(extension, "NvFlexExtDestroyAsset")));
    const float tetrahedron[] = {0,0,0, 1,0,0, 0,1,0, 0,0,1};
    const int facesForAsset[] = {0,2,1, 0,1,3, 0,3,2, 1,2,3};
    auto* asset = flexExtCreateRigidFromMesh(tetrahedron, 4, facesForAsset, 12, .15f, 0);
    const bool assetValid = asset && asset->particles && asset->particleCount > 0 &&
        asset->shapeCount == 1 && asset->shapeOffsets[0] == asset->shapeIndexCount;
    if (assetValid) printf("Rigid mesh asset: %d particles\n", asset->particleCount);
    flexExtDestroyAsset(asset);
    BindLegacyAssetBackend(nullptr, nullptr);
    FreeLibrary(extension);
    if (!assetValid) return 1;
    const FlexBufferApi buffers{NvFlexAllocBuffer, NvFlexFreeBuffer, NvFlexMap, NvFlexUnmap};
    const LegacySolverBackend backend{
        NvFlexInit, NvFlexShutdown, buffers, NvFlexSetSolverDescDefaults,
        NvFlexCreateSolver, NvFlexDestroySolver,
        NvFlexSetParticles, NvFlexGetParticles, NvFlexSetVelocities, NvFlexSetActive,
        NvFlexSetActiveCount, NvFlexUpdateSolver, NvFlexSetParams,
        NvFlexGetVelocities, NvFlexSetPhases, NvFlexGetBounds, NvFlexFlush, NvFlexWait,
        NvFlexSetRigids, NvFlexGetRigids, NvFlexSetShapes,
        NvFlexCreateConvexMesh, NvFlexUpdateConvexMesh, NvFlexDestroyConvexMesh
    };
    BindLegacySolverBackend(&backend);
    if (flexInit(90, Report, -1)) return 1;
    auto* library = LegacySolverLibrary();
    printf("D3D11 adapter: %s\n", NvFlexGetDeviceName(library));
    void* host = flexAlloc(120);
    if (!host) {
        flexShutdown();
        return 1;
    }
    memset(host, 0, 120);
    flexFree(host);
    puts("Legacy 120-byte host allocation initialized and freed.");
    auto* legacy = flexCreateSolver(64, 0, 48);
    bool moved = false;
    if (legacy) {
        auto* solver = legacy->solver;
        auto* container = flexExtCreateContainer(legacy, 64);
        struct Position { float x, y, z, inverseMass; };
        struct Velocity { float x, y, z; };
        const Position initial[] = {
            {0, 1, 0, 1}, {12, 3, 0, 1},
            {15.2f, 4.2f, 0.2f, 1}, {15.2f, 3.8f, -0.2f, 1},
            {14.8f, 4.2f, -0.2f, 1}, {14.8f, 3.8f, 0.2f, 1},
            {20, 1, 0, 1}, {25, 1, 0, 1}, {30, 1, 0, 1}
        };
        const Velocity velocities[9]{};
        const int phases[] = {0, 0, 1, 1, 1, 1, 0, 0, 0};
        const int indices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        const int rigidOffsets[] = {0, 4};
        const int rigidIndices[] = {2, 3, 4, 5};
        const FlexPoint rest[] = {{.2f,.2f,.2f}, {.2f,-.2f,-.2f},
                                  {-.2f,.2f,-.2f}, {-.2f,-.2f,.2f}};
        const float stiffness = 1;
        const FlexQuaternion rigidRotation{0, 0, 0, 1};
        const FlexPoint rigidCenter{15, 4, 0};
        const LegacyMeshApi meshApi{
            buffers, NvFlexCreateTriangleMesh, NvFlexUpdateTriangleMesh, NvFlexDestroyTriangleMesh
        };
        BindLegacyMeshApi(&meshApi);
        auto* mesh = flexCreateTriangleMesh();
        const FlexPoint floor[] = {{-10, .5f, -10}, {-10, .5f, 10}, {10, .5f, -10}, {10, .5f, 10}};
        const int faces[] = {0, 1, 2, 2, 1, 3};
        const int invalidFace[] = {0, 1, 4};
        flexUpdateTriangleMesh(mesh, floor, invalidFace, 4, 1, nullptr, nullptr, 0);
        const bool invalidRejected = !LegacyMeshReady(mesh);
        flexUpdateTriangleMesh(mesh, floor, faces, 4, 2, nullptr, nullptr, 0);
        const struct { LegacyTriangleMesh* handle; float scale; float padding; } triangle{mesh, .5f, 0};
        float geometry[9][4]{
            {}, {.5f, 0, 0, 0}, {.5f, .7f, 0, 0},
            {1, 0, 0, -.5f}, {-1, 0, 0, -.5f}, {0, 1, 0, -.5f},
            {0, -1, 0, -.5f}, {0, 0, 1, -.5f}, {0, 0, -1, -.5f}
        };
        static_assert(sizeof(triangle) == sizeof(geometry[0]));
        std::memcpy(geometry[0], &triangle, sizeof(triangle));
        const float origins[4][4]{{}, {20,0,0,0}, {25,0,0,0}, {30,0,0,0}};
        const float rotations[4][4]{{0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1}};
        const int flags[] = {3, 0, 1, 2};
        const int starts[] = {0, 1, 2, 3};
        float* hostPositions = nullptr;
        float* hostVelocities = nullptr;
        int* hostPhases = nullptr;
        int* hostIndices = nullptr;
        flexExtGetParticleData(container, &hostPositions, &hostVelocities,
                               &hostPhases, nullptr, &hostIndices);
        if (hostPositions && hostVelocities && hostPhases && hostIndices &&
            invalidRejected && LegacyMeshReady(mesh)) {
            LegacyFlexParams params{};
            params.gravity[1] = -9.8f;
            params.radius = 0.15f;
            params.solidRestDistance = params.radius;
            params.collisionDistance = 0.075f;
            params.shapeCollisionMargin = 0.0375f;
            params.iterations = 3;
            params.dynamicFriction = .6f;
            params.staticFriction = .7f;
            params.particleFriction = .3f;
            params.restitution = .1f;
            params.adhesion = .02f;
            params.sleepThreshold = .005f;
            params.relaxationFactor = 1.0f;
            flexSetParams(legacy, &params);
            NvFlexParams actual{};
            NvFlexGetParams(solver, &actual);
            const bool paramsMatch = actual.dynamicFriction == params.dynamicFriction &&
                actual.staticFriction == params.staticFriction &&
                actual.particleFriction == params.particleFriction &&
                actual.restitution == params.restitution && actual.adhesion == params.adhesion &&
                actual.sleepThreshold == params.sleepThreshold;
            printf("Legacy physical parameters: %s\n", paramsMatch ? "verified" : "mismatch");
            params.restitution = 0;
            params.adhesion = 0;
            params.sleepThreshold = 0;
            flexSetParams(legacy, &params);
            flexSetShapes(legacy, geometry, 9, nullptr, nullptr, starts,
                          origins[0], rotations[0], nullptr, nullptr, flags, 4, 2);
            std::memcpy(hostPositions, initial, sizeof(initial));
            std::memcpy(hostVelocities, velocities, sizeof(velocities));
            std::memcpy(hostPhases, phases, sizeof(phases));
            std::memcpy(hostIndices, indices, sizeof(indices));
            flexSetParticles(legacy, hostPositions, 1, 0);
            flexSetParticles(legacy, hostPositions, 9, 2);
            flexSetVelocities(legacy, hostVelocities, 9, 0);
            flexSetActive(legacy, hostIndices, 9, 2);
            flexSetPhases(legacy, hostPhases, 9, 0);
            flexSetRigids(legacy, rigidOffsets, rigidIndices, rest, nullptr, &stiffness,
                          &rigidRotation, &rigidCenter, 1, 2);
            for (int i = 0; i < 60; ++i) flexUpdateSolver(legacy, 1.0f / 60, 1, nullptr);
            flexSetFence();
            flexWaitFence();
            flexGetVelocities(legacy, hostVelocities, 6, 0);
            float lower[3]{}, upper[3]{};
            flexGetBounds(legacy, lower, upper, 0);
            Position final[9]{};
            flexGetParticles(legacy, hostPositions, 9, 0);
            std::memcpy(final, hostPositions, sizeof(final));
            float* samePositions = nullptr;
            flexExtGetParticleData(container, &samePositions, nullptr, nullptr, nullptr, nullptr);
            if (paramsMatch && samePositions == hostPositions &&
                !legacy->failed && flexGetActiveCount(legacy) == 9) {
                printf("Particle y after 60 steps: %.6f, %.6f\n", final[0].y, final[1].y);
                moved = std::abs(final[0].y - .25f - params.collisionDistance) < 0.02f &&
                        std::abs(final[1].y + 1.981667f) < 0.01f &&
                        std::abs(hostVelocities[4] + 9.8f) < 0.02f &&
                        lower[0] <= 0.01f && upper[0] >= 15.1f;
                printf("Sphere/capsule/convex final heights: %.6f, %.6f, %.6f\n",
                       final[6].y, final[7].y, final[8].y);
                for (int i = 6; i < 9; ++i) {
                    moved = moved && std::abs(final[i].y - .5f - params.collisionDistance) < 0.02f;
                }
                printf("Control velocity y: %.6f; bounds x: %.6f to %.6f\n",
                       hostVelocities[4], lower[0], upper[0]);
            }
            FlexQuaternion orientation{};
            FlexPoint center{};
            flexGetRigidTransforms(legacy, &orientation, &center, 2);
            if (!legacy->failed) {
                printf("Rigid center: %.6f, %.6f, %.6f; orientation w: %.6f\n",
                       center.x, center.y, center.z, orientation.w);
                moved = moved && std::abs(center.x - 15) < .01f &&
                        std::abs(center.y + .981667f) < .01f &&
                        std::abs(center.z) < .01f &&
                        std::abs(std::abs(orientation.w) - 1) < .01f;
            } else {
                moved = false;
            }
            const LegacyForceField impulse{
                {11, -2.1478f, 0}, 2, 2, 2, {0, 0, 0}, 1, 0
            };
            flexExtSetForceFields(container, &impulse, 1, 2);
            flexUpdateSolver(legacy, 1.0f / 60, 1, nullptr);
            flexGetVelocities(legacy, hostVelocities, 6, 0);
            const float impulseVelocity = hostVelocities[3];
            flexExtSetForceFields(container, nullptr, 0, 2);
            flexUpdateSolver(legacy, 1.0f / 60, 1, nullptr);
            flexGetVelocities(legacy, hostVelocities, 6, 0);
            printf("Force field x velocity: %.6f; after clearing: %.6f\n",
                   impulseVelocity, hostVelocities[3]);
            moved = moved && !legacy->failed && std::abs(impulseVelocity - 2) < .02f &&
                    std::abs(hostVelocities[3] - impulseVelocity) < .02f;
            char tempDirectory[MAX_PATH]{}, recordPath[MAX_PATH]{};
            bool recordOk = false;
            if (GetTempPathA(MAX_PATH, tempDirectory) &&
                GetTempFileNameA(tempDirectory, "flx", 0, recordPath)) {
                flexSnapshot(legacy, recordPath);
                auto verifyRecord = [&]() {
                    char signature[8]{};
                    std::FILE* file = std::fopen(recordPath, "rb");
                    if (!file) return false;
                    const bool matches = std::fread(signature, sizeof(signature), 1, file) == 1 &&
                        std::memcmp(signature, "FO4DX11", sizeof(signature)) == 0;
                    std::fclose(file);
                    return matches;
                };
                recordOk = verifyRecord();
                flexStartRecord(legacy, recordPath);
                flexUpdateSolver(legacy, 1.0f / 60, 1, nullptr);
                flexStopRecord(legacy);
                recordOk = recordOk && verifyRecord();
                DeleteFileA(recordPath);
            }
            puts(recordOk ? "Snapshot and recording verified." : "Diagnostic recording failed.");
            moved = moved && recordOk;
        }
        flexExtDestroyContainer(container);
        flexDestroySolver(legacy);
        flexDestroyTriangleMesh(mesh);
        BindLegacyMeshApi(nullptr);
    }
    flexShutdown();
    BindLegacySolverBackend(nullptr);
    FreeLibrary(dll);
    return moved && !backendError ? 0 : 1;
}
