#pragma once

#include "flex-host-transfer.h"
#include "flex-legacy-params.h"
#include "flex-rigid-transfer.h"
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdint>

struct LegacyConvexMesh {
    NvFlexLibrary* library;
    NvFlexConvexMeshId id;
    decltype(&NvFlexDestroyConvexMesh) destroy;

    ~LegacyConvexMesh() { if (id) destroy(library, id); }
};

struct LegacyForceField {
    float position[3];
    float radius;
    float minStrength;
    float maxStrength;
    float direction[3];
    int mode;
    unsigned char linearFalloff;
};
static_assert(sizeof(LegacyForceField) == 44);

// Internal state for the standalone ABI slice. The game-facing lifecycle is pending.
struct LegacyParticleSolver {
    NvFlexSolver* solver;
    int capacity;
    int activeCount = 0;
    bool failed = false;
    FlexHostTransfer positions;
    FlexHostTransfer velocities;
    FlexHostTransfer active;
    FlexHostTransfer phases;
    FlexHostTransfer lower;
    FlexHostTransfer upper;
    FlexHostTransfer::Transfer setPositions;
    FlexHostTransfer::Transfer getPositions;
    FlexHostTransfer::Transfer setVelocities;
    FlexHostTransfer::Transfer setActive;
    decltype(&NvFlexSetActiveCount) setActiveCount;
    decltype(&NvFlexUpdateSolver) update;
    decltype(&NvFlexDestroySolver) destroy = nullptr;
    decltype(&NvFlexSetParams) setParams = nullptr;
    std::unique_ptr<FlexRigidTransfer> rigid;
    std::vector<LegacyForceField> forceFields;
    std::vector<int> activeIndices;
    unsigned forceSeed = 1;
    bool loggedFrame = false;
    bool loggedReadback = false;
    bool loggedShapeRejection = false;
    bool loggedConvexReduction = false;
    bool loggedFailure = false;
    const char* lastOperation = nullptr;
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> recording{nullptr, std::fclose};
    LegacyFlexParams lastParams{};
    std::vector<std::unique_ptr<LegacyConvexMesh>> convexShapes;

    LegacyParticleSolver(NvFlexLibrary* library, FlexBufferApi buffers, NvFlexSolver* native,
                         int capacity, FlexHostTransfer::Transfer setPositions,
                         FlexHostTransfer::Transfer getPositions,
                         FlexHostTransfer::Transfer setVelocities,
                         FlexHostTransfer::Transfer setActive,
                         decltype(&NvFlexSetActiveCount) setActiveCount,
                         decltype(&NvFlexUpdateSolver) update)
        : solver(native), capacity(capacity), positions(library, buffers, 16),
          velocities(library, buffers, 12), active(library, buffers, 4),
          phases(library, buffers, 4), lower(library, buffers, 12), upper(library, buffers, 12),
          setPositions(setPositions), getPositions(getPositions), setVelocities(setVelocities),
          setActive(setActive), setActiveCount(setActiveCount), update(update) {}
};

struct LegacySolverBackend {
    decltype(&NvFlexInit) init;
    decltype(&NvFlexShutdown) shutdown;
    FlexBufferApi buffers;
    decltype(&NvFlexSetSolverDescDefaults) defaults;
    decltype(&NvFlexCreateSolver) create;
    decltype(&NvFlexDestroySolver) destroy;
    FlexHostTransfer::Transfer setPositions;
    FlexHostTransfer::Transfer getPositions;
    FlexHostTransfer::Transfer setVelocities;
    FlexHostTransfer::Transfer setActive;
    decltype(&NvFlexSetActiveCount) setActiveCount;
    decltype(&NvFlexUpdateSolver) update;
    decltype(&NvFlexSetParams) setParams;
    FlexHostTransfer::Transfer getVelocities;
    FlexHostTransfer::Transfer setPhases;
    decltype(&NvFlexGetBounds) getBounds;
    decltype(&NvFlexFlush) flush;
    decltype(&NvFlexWait) wait;
    decltype(&NvFlexSetRigids) setRigids;
    decltype(&NvFlexGetRigids) getRigids;
    decltype(&NvFlexSetShapes) setShapes;
    decltype(&NvFlexCreateConvexMesh) createConvexMesh;
    decltype(&NvFlexUpdateConvexMesh) updateConvexMesh;
    decltype(&NvFlexDestroyConvexMesh) destroyConvexMesh;
};

// Standalone harness binding. Keep the backend loaded until its solvers are destroyed.
void BindLegacySolverBackend(const LegacySolverBackend* backend);
void BindLegacyRuntimeLog(void (*log)(const char*));
NvFlexLibrary* LegacySolverLibrary();

extern "C" {
using LegacyFlexError = void (*)(int, const char*, const char*, int);
int flexInit(int version, LegacyFlexError callback, int device);
void flexShutdown();
void* flexAlloc(int bytes);
void flexFree(void* pointer);
LegacyParticleSolver* flexCreateSolver(int capacity, int legacyArgument, std::uint8_t maxNeighbors);
void flexDestroySolver(LegacyParticleSolver*);
void flexSetParams(LegacyParticleSolver*, const LegacyFlexParams*);
void flexSetParticles(LegacyParticleSolver*, const void*, int count, int memory);
void flexGetParticles(LegacyParticleSolver*, void*, int count, int memory);
void flexSetVelocities(LegacyParticleSolver*, const void*, int count, int memory);
void flexGetVelocities(LegacyParticleSolver*, void*, int count, int memory);
void flexSetPhases(LegacyParticleSolver*, const int*, int count, int memory);
void flexGetBounds(LegacyParticleSolver*, float* lower, float* upper, int memory);
void flexSetFence();
void flexWaitFence();
void flexAcquireContext();
void flexStartRecord(LegacyParticleSolver*, const char* path);
void flexStopRecord(LegacyParticleSolver*);
void flexSnapshot(LegacyParticleSolver*, const char* path);
void flexSetShapes(LegacyParticleSolver*, const void* geometry, int geometryCount,
                   const float* lower, const float* upper, const int* starts,
                   const float* positions, const float* rotations,
                   const float* previousPositions, const float* previousRotations,
                   const int* flags, int shapeCount, int memory);
void flexSetRigids(LegacyParticleSolver*, const int*, const int*, const FlexPoint*,
                   const float* normals, const float* stiffness, const FlexQuaternion*,
                   const FlexPoint*, int count, int memory);
void flexGetRigidTransforms(LegacyParticleSolver*, FlexQuaternion*, FlexPoint*, int memory);
void flexSetActive(LegacyParticleSolver*, const int*, int count, int memory);
int flexGetActiveCount(const LegacyParticleSolver*);
void flexUpdateSolver(LegacyParticleSolver*, float dt, int substeps, void* timers);
}
