#include <cmath>
#include <algorithm>
#include <limits>
#include <malloc.h>
#include <new>
#include <vector>
#include "flex-legacy-particles.h"
#include "flex-legacy-shapes.h"
#include "experiments/physx-exclusive-api.h"

namespace {
const LegacySolverBackend* backend;
NvFlexLibrary* library;
void* exclusiveRuntime;
const PhysXExclusiveApi* exclusiveApi;
void (*destroyExclusiveRuntime)(void*);
LegacyFlexError errorCallback;
unsigned liveSolvers;
void (*runtimeLog)(const char*);
void (*captureConvex)(const unsigned char*, int);
void* (*probePhysXConvex)(const unsigned char*, int, const float*, const float*);
void (*destroyPhysXProbe)(void*);
int (*stepPhysXProbe)(void*, float, const float*, const float*, const float*, float, float*);
bool usePhysXParticleOutput = false;
bool probePhysXRigids = false;
bool usePhysXRigidOutput = false;
bool usePhysXSceneShapes = false;
int (*setPhysXSceneShapes)(void*, int, const PhysXSceneShape*, int);
int (*uploadPhysXRigids)(void*, int, const float*, int, const float*, int, const int*, int,
                        const int*, const int*, const float*, const float*, const float*,
                        int, int, const float*, float);
int (*stepPhysXRigids)(void*, float, int, float*, float*, int, float*, int, float*, int);
bool traceLegacyCalls = false;

void ReportError(NvFlexErrorSeverity severity, const char* message, const char* file, int line) {
    if (errorCallback) errorCallback(static_cast<int>(severity), message, file, line);
}

void SavePhysXInput(LegacyParticleSolver& state, std::vector<float>& output,
                    const void* data, int count, int stride) {
    if (!probePhysXRigids || !state.physxProbe) return;
    output.clear();
    if (!count || !data) return;
    try {
        const auto* input = static_cast<const float*>(data);
        output.assign(input, input + static_cast<std::size_t>(count) * stride);
    } catch (const std::bad_alloc&) {
        state.physxRigidsReady = false;
    }
}

bool AcceptHostTransferImpl(LegacyParticleSolver* state, int count, int memory, const char* operation) {
    if (!state || state->failed) return false;
    state->lastOperation = operation;
    // The shipped DLL uses host copies for 0 and 2, and device copies for 1 and 3.
    if (count < 0 || count > state->capacity || (memory != 0 && memory != 2)) {
        state->failed = true;
        return false;
    }
    return true;
}
#define AcceptHostTransfer(state, count, memory) AcceptHostTransferImpl(state, count, memory, __func__)
}

void TraceLegacyCall(LegacyParticleSolver* state, const char* operation, int count, int detail, bool rigid) {
    if (!traceLegacyCalls || !runtimeLog || !state) return;
    auto& calls = rigid ? state->traceRigidCalls : state->traceCalls;
    if (calls >= (rigid ? 40u : 240u)) return;
    ++calls;
    char message[160];
    std::snprintf(message, sizeof(message), "FleX ABI trace: solver=%p frame=%u %s count=%d detail=%d",
                  static_cast<void*>(state), state->traceFrame, operation, count, detail);
    runtimeLog(message);
}

void BindLegacySolverBackend(const LegacySolverBackend* binding) {
    if (!library) backend = binding;
}

void BindLegacyRuntimeLog(void (*log)(const char*)) {
    runtimeLog = log;
}

NvFlexLibrary* LegacySolverLibrary() {
    return library;
}
bool LegacyPhysXExclusive() { return exclusiveRuntime != nullptr; }

int flexInit(int version, LegacyFlexError callback, int device) {
    if (!backend || library || version != 90 || device < -1) return 1;
    NvFlexInitDesc desc{};
    desc.deviceIndex = device < 0 ? 0 : device;
    desc.computeType = eNvFlexD3D11;
    desc.runOnRenderContext = true;
    errorCallback = callback;
    library = backend->init(NV_FLEX_VERSION, ReportError, &desc);
    if (library) return 0;
    errorCallback = nullptr;
    return 1;
}

void flexShutdown() {
    if (exclusiveRuntime && !liveSolvers) {
        destroyExclusiveRuntime(exclusiveRuntime);
        exclusiveRuntime = nullptr;
        return;
    }
    if (!library || liveSolvers) return;
    backend->shutdown(library);
    library = nullptr;
    errorCallback = nullptr;
}

void* flexAlloc(int bytes) {
    return (library || exclusiveRuntime) && bytes > 0 ? _aligned_malloc(static_cast<size_t>(bytes), 16) : nullptr;
}

void flexFree(void* pointer) {
    _aligned_free(pointer);
}

LegacyParticleSolver* flexCreateSolver(int capacity, int legacyArgument, std::uint8_t maxNeighbors) {
    if ((!library && !exclusiveRuntime) || capacity <= 0 || legacyArgument != 0 || !maxNeighbors) return nullptr;
    if (exclusiveRuntime) {
        auto state = std::unique_ptr<LegacyParticleSolver>(new (std::nothrow) LegacyParticleSolver(
            nullptr, {}, nullptr, capacity, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
        if (!state) return nullptr;
        state->physxExclusive = exclusiveApi->create(exclusiveRuntime, capacity);
        if (!state->physxExclusive) return nullptr;
        ++liveSolvers;
        return state.release();
    }
    NvFlexSolverDesc desc{};
    backend->defaults(&desc);
    desc.maxParticles = capacity;
    desc.maxNeighborsPerParticle = maxNeighbors;
    auto* solver = backend->create(library, &desc);
    if (!solver) return nullptr;
    auto* state = new (std::nothrow) LegacyParticleSolver(
        library, backend->buffers, solver, capacity, backend->setPositions,
        backend->getPositions, backend->setVelocities, backend->setActive,
        backend->setActiveCount, backend->update);
    if (!state) {
        backend->destroy(solver);
        return nullptr;
    }
    state->destroy = backend->destroy;
    state->setParams = backend->setParams;
    ++liveSolvers;
    TraceLegacyCall(state, __func__, capacity, maxNeighbors);
    return state;
}

void flexDestroySolver(LegacyParticleSolver* state) {
    if (!state) return;
    TraceLegacyCall(state, __func__, state->activeCount, 0, true);
    if (state->physxExclusive) {
        exclusiveApi->destroy(state->physxExclusive);
        delete state;
        --liveSolvers;
        return;
    }
    auto* solver = state->solver;
    const auto destroy = state->destroy;
    if (state->physxProbe) destroyPhysXProbe(state->physxProbe);
    if (!state->convexShapes.empty()) backend->wait(library);
    delete state;
    destroy(solver);
    --liveSolvers;
}

void flexSetParams(LegacyParticleSolver* state, const LegacyFlexParams* source) {
    if (!state || state->failed) return;
    state->lastOperation = __func__;
    TraceLegacyCall(state, __func__, source ? source->numPlanes : -1);
    if (!source || source->iterations <= 0 || !std::isfinite(source->radius) ||
        source->radius <= 0 || source->numPlanes < 0 || source->numPlanes > 8 ||
        (source->relaxationMode != 0 && source->relaxationMode != 1)) {
        state->failed = true;
        if (runtimeLog) runtimeLog("FleX D3D11 rejected legacy solver parameters.");
        return;
    }
    if (state->physxExclusive) {
        state->failed = exclusiveApi->parameters(state->physxExclusive, source->gravity, source->radius) != 1;
        state->lastParams = *source;
        if (source->numPlanes && runtimeLog) runtimeLog("PhysX exclusive: solver planes are not implemented.");
        return;
    }
    NvFlexParams params{};
    params.numIterations = source->iterations;
    std::memcpy(params.gravity, source->gravity, sizeof(params.gravity));
    params.radius = source->radius;
    params.solidRestDistance = source->solidRestDistance;
    params.fluidRestDistance = source->fluidRestDistance;
    params.dynamicFriction = source->dynamicFriction;
    params.staticFriction = source->staticFriction;
    params.particleFriction = source->particleFriction;
    params.restitution = source->restitution;
    params.adhesion = source->adhesion;
    params.sleepThreshold = source->sleepThreshold;
    params.maxSpeed = source->maxSpeed > 0 ? source->maxSpeed : std::numeric_limits<float>::max();
    params.maxAcceleration = std::numeric_limits<float>::max();
    params.dissipation = source->dissipation;
    params.damping = source->damping;
    params.collisionDistance = source->collisionDistance;
    params.particleCollisionMargin = source->particleCollisionMargin;
    params.shapeCollisionMargin = source->shapeCollisionMargin;
    std::memcpy(params.planes, source->planes, sizeof(params.planes));
    params.numPlanes = source->numPlanes;
    params.relaxationMode = static_cast<NvFlexRelaxationMode>(source->relaxationMode);
    params.relaxationFactor = source->relaxationFactor;
    state->setParams(state->solver, &params);
    state->lastParams = *source;
}

void flexSetParticles(LegacyParticleSolver* state, const void* data, int count, int memory) {
    if (AcceptHostTransfer(state, count, memory)) {
        if (state->physxExclusive) {
            state->failed = exclusiveApi->positions(state->physxExclusive, static_cast<const float*>(data), count) != 1;
            return;
        }
        TraceLegacyCall(state, __func__, count, memory);
        state->failed = !state->positions.Upload(state->solver, data, count, state->setPositions);
        if (!state->failed) SavePhysXInput(*state, state->physxInputPositions, data, count, 4);
    }
}

void flexGetParticles(LegacyParticleSolver* state, void* data, int count, int memory) {
    if (AcceptHostTransfer(state, count, memory)) {
        if (state->physxExclusive) {
            state->failed = exclusiveApi->getPositions(state->physxExclusive, static_cast<float*>(data), count) != 1;
            if (!state->failed && count > 0 && !state->loggedReadback && runtimeLog) {
                runtimeLog("PhysX exclusive: returned particle positions.");
                state->loggedReadback = true;
            }
            return;
        }
        if (usePhysXRigidOutput && state->physxRigidOutputReady && data && count > 0 &&
            state->physxOutputPositions.size() >= 4ull * count) {
            std::memcpy(data, state->physxOutputPositions.data(), 4ull * count * sizeof(float));
            if (!state->loggedPhysXPositions && runtimeLog) {
                runtimeLog("PhysX rigid output: returned particle positions to Fallout 4.");
                state->loggedPhysXPositions = true;
            }
            return;
        }
        TraceLegacyCall(state, __func__, count, memory);
        state->failed = !state->positions.Download(state->solver, data, count, state->getPositions);
        if (usePhysXParticleOutput && !state->failed && data && state->physxProbe &&
            state->physxFrames > 0 && state->physxFrames <= 60 &&
            state->physxParticle >= 0 && state->physxParticle < count &&
            std::find(state->activeIndices.begin(), state->activeIndices.end(), state->physxParticle) !=
                state->activeIndices.end()) {
            auto* particle = static_cast<float*>(data) + 4ull * state->physxParticle;
            std::copy_n(state->physxPosition, 3, particle);
            if (!state->loggedPhysXOutput && runtimeLog) {
                runtimeLog("PhysX probe: returned one particle position to Fallout 4.");
                state->loggedPhysXOutput = true;
            }
        }
        if (!state->failed && state->loggedFrame && !state->loggedReadback && count > 0 && runtimeLog) {
            runtimeLog("FleX D3D11 particle readback succeeded.");
            state->loggedReadback = true;
        }
    }
}

void flexSetVelocities(LegacyParticleSolver* state, const void* data, int count, int memory) {
    if (AcceptHostTransfer(state, count, memory)) {
        if (state->physxExclusive) {
            state->failed = exclusiveApi->velocities(state->physxExclusive, static_cast<const float*>(data), count) != 1;
            return;
        }
        TraceLegacyCall(state, __func__, count, memory);
        state->failed = !state->velocities.Upload(state->solver, data, count, state->setVelocities);
        if (!state->failed) SavePhysXInput(*state, state->physxInputVelocities, data, count, 3);
    }
}

void flexGetVelocities(LegacyParticleSolver* state, void* data, int count, int memory) {
    if (AcceptHostTransfer(state, count, memory)) {
        if (state->physxExclusive) {
            state->failed = exclusiveApi->getVelocities(state->physxExclusive, static_cast<float*>(data), count) != 1;
            return;
        }
        if (usePhysXRigidOutput && state->physxRigidOutputReady && data && count > 0 &&
            state->physxOutputVelocities.size() >= 3ull * count) {
            std::memcpy(data, state->physxOutputVelocities.data(), 3ull * count * sizeof(float));
            if (!state->loggedPhysXVelocities && runtimeLog) {
                runtimeLog("PhysX rigid output: returned particle velocities to Fallout 4.");
                state->loggedPhysXVelocities = true;
            }
            return;
        }
        TraceLegacyCall(state, __func__, count, memory);
        state->failed = !state->velocities.Download(state->solver, data, count, backend->getVelocities);
    }
}

void flexSetPhases(LegacyParticleSolver* state, const int* data, int count, int memory) {
    if (!AcceptHostTransfer(state, count, memory) || count == 0) return;
    TraceLegacyCall(state, __func__, count, memory);
    if (!data) {
        state->failed = true;
        return;
    }
    if (state->physxExclusive) return; // The experimental backend does not translate phase filters.
    try {
        std::vector<int> translated(data, data + count);
        for (int& phase : translated) phase |= eNvFlexPhaseShapeChannelMask;
        state->failed = !state->phases.Upload(state->solver, translated.data(), count, backend->setPhases);
    } catch (const std::bad_alloc&) {
        state->failed = true;
    }
}

void flexGetBounds(LegacyParticleSolver* state, float* lower, float* upper, int memory) {
    if (!AcceptHostTransfer(state, 0, memory)) return;
    if (state->physxExclusive) {
        if (!lower || !upper) { state->failed = true; return; }
        std::fill_n(lower, 3, 0.0f);
        std::fill_n(upper, 3, 0.0f);
        if (state->activeIndices.empty()) return;
        try {
            const int count = *std::max_element(state->activeIndices.begin(), state->activeIndices.end()) + 1;
            std::vector<float> positions(4ull * count);
            if (exclusiveApi->getPositions(state->physxExclusive, positions.data(), count) != 1) {
                state->failed = true;
                return;
            }
            const auto* first = positions.data() + 4ull * state->activeIndices.front();
            std::copy_n(first, 3, lower);
            std::copy_n(first, 3, upper);
            for (int index : state->activeIndices) for (int axis = 0; axis < 3; ++axis) {
                const float value = positions[4ull * index + axis];
                lower[axis] = std::min(lower[axis], value);
                upper[axis] = std::max(upper[axis], value);
            }
        } catch (const std::bad_alloc&) { state->failed = true; }
        return;
    }
    auto* lowerBuffer = state->lower.Prepare(1);
    auto* upperBuffer = state->upper.Prepare(1);
    if (!lower || !upper || !lowerBuffer || !upperBuffer) {
        state->failed = true;
        return;
    }
    backend->getBounds(state->solver, lowerBuffer, upperBuffer);
    state->failed = !state->lower.Read(lower, 1) || !state->upper.Read(upper, 1);
}

void flexSetFence() {
    if (library) backend->flush(library);
}

void flexWaitFence() {
    if (library) backend->wait(library);
}

// D3D11 does not use a thread-current CUDA context.
void flexAcquireContext() {}

namespace {
bool WriteDiagnosticFrame(LegacyParticleSolver& state, std::FILE* file) {
    try {
        std::vector<float> positions(static_cast<std::size_t>(state.capacity) * 4);
        std::vector<float> velocities(static_cast<std::size_t>(state.capacity) * 3);
        const bool read = state.physxExclusive ?
            exclusiveApi->getPositions(state.physxExclusive, positions.data(), state.capacity) == 1 &&
            exclusiveApi->getVelocities(state.physxExclusive, velocities.data(), state.capacity) == 1 :
            state.positions.Download(state.solver, positions.data(), state.capacity, state.getPositions) &&
            state.velocities.Download(state.solver, velocities.data(), state.capacity, backend->getVelocities);
        if (!read) {
            return false;
        }
        const auto count = state.activeIndices.size();
        return std::fwrite("FO4DX11\0", 8, 1, file) == 1 &&
               std::fwrite(&state.lastParams, sizeof(state.lastParams), 1, file) == 1 &&
               std::fwrite(&state.capacity, sizeof(state.capacity), 1, file) == 1 &&
               std::fwrite(&state.activeCount, sizeof(state.activeCount), 1, file) == 1 &&
               std::fwrite(state.activeIndices.data(), sizeof(int), count, file) == count &&
               std::fwrite(positions.data(), sizeof(float), positions.size(), file) == positions.size() &&
               std::fwrite(velocities.data(), sizeof(float), velocities.size(), file) == velocities.size();
    } catch (const std::bad_alloc&) {
        return false;
    }
}
}

void flexStartRecord(LegacyParticleSolver* state, const char* path) {
    if (state && path && !state->recording) state->recording.reset(std::fopen(path, "wb"));
}

void flexStopRecord(LegacyParticleSolver* state) {
    if (state) state->recording.reset();
}

void flexSnapshot(LegacyParticleSolver* state, const char* path) {
    if (!state || !path || state->failed) return;
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::fopen(path, "wb"), std::fclose);
    if (file && !WriteDiagnosticFrame(*state, file.get()) && errorCallback) {
        errorCallback(0, "Could not write the Direct3D FleX snapshot", __FILE__, __LINE__);
    }
}

namespace {
std::vector<PhysXSceneShape> DescribePhysXShapes(const unsigned char* entries, int geometryCount,
    const int* starts, const float* positions, const float* rotations, const int* flags, int shapeCount) {
    std::vector<PhysXSceneShape> shapes(shapeCount);
    for (int i = 0; i < shapeCount; ++i) {
        const int start = starts[i];
        if (start < 0 || start >= geometryCount) continue;
        const int end = (flags[i] & 7) == 2 ? (i + 1 < shapeCount ? starts[i + 1] : geometryCount) : start + 1;
        if (end <= start || end > geometryCount) continue;
        auto& shape = shapes[i];
        shape.flags = flags[i];
        shape.records = entries + 16ull * start;
        shape.count = end - start;
        shape.position = positions + 4ull * i;
        shape.rotation = rotations + 4ull * i;
        if ((flags[i] & 7) == 3) {
            LegacyTriangleMesh* mesh = nullptr;
            std::memcpy(&mesh, shape.records, sizeof(mesh));
            std::memcpy(&shape.scale, static_cast<const unsigned char*>(shape.records) + 8, sizeof(float));
            DescribeLegacyMesh(mesh, shape);
        }
    }
    return shapes;
}
}

void flexSetShapes(LegacyParticleSolver* state, const void* geometry, int geometryCount,
                   const float*, const float*, const int* starts,
                   const float* positions, const float* rotations,
                   const float* previousPositions, const float* previousRotations,
                   const int* flags, int shapeCount, int memory) {
    if (!AcceptHostTransfer(state, 0, memory)) return;
    TraceLegacyCall(state, __func__, shapeCount, geometryCount);
    if (shapeCount == 0 || geometryCount == 0) {
        if (state->physxExclusive) {
            state->failed = exclusiveApi->shapes(state->physxExclusive, nullptr, 0) < 0;
            return;
        }
        if (usePhysXSceneShapes && state->physxProbe) setPhysXSceneShapes(state->physxProbe, state->capacity, nullptr, 0);
        backend->setShapes(state->solver, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0);
        if (!state->convexShapes.empty()) {
            backend->wait(library);
            state->convexShapes.clear();
        }
        return;
    }
    if (shapeCount < 0 || geometryCount < 0 || !geometry || !starts || !flags ||
        !positions || !rotations) {
        state->failed = true;
        return;
    }
    try {
        auto* entries = static_cast<const unsigned char*>(geometry);
        if (state->physxExclusive) {
            const auto shapes = DescribePhysXShapes(entries, geometryCount, starts, positions, rotations, flags, shapeCount);
            const int accepted = exclusiveApi->shapes(state->physxExclusive, shapes.data(), shapeCount);
            state->failed = accepted < 0;
            if ((!state->loggedPhysXShapes || state->failed) && runtimeLog) {
                char message[128];
                std::snprintf(message, sizeof(message), "PhysX exclusive: accepted %d of %d scene shapes.", accepted, shapeCount);
                runtimeLog(message);
                state->loggedPhysXShapes = true;
            }
            return;
        }
        std::vector<NvFlexCollisionGeometry> shapes(shapeCount);
        std::vector<int> flags12(shapeCount);
        std::vector<float> pos12(4ull * shapeCount), rot12(4ull * shapeCount);
        std::vector<float> prevPos12(4ull * shapeCount), prevRot12(4ull * shapeCount);
        std::vector<std::unique_ptr<LegacyConvexMesh>> convexShapes;
        FlexHostTransfer planeTransfer(library, backend->buffers, 16);
        int accepted = 0;
        int reducedFrom = 0;
        int reducedInput = 0;
        for (int i = 0; i < shapeCount; ++i) {
            int distinctPlanes = 0;
            const int end = i + 1 < shapeCount ? starts[i + 1] : geometryCount;
            const int limit = (flags[i] & 7) == 2 ? end : starts[i] + 1;
            if (probePhysXConvex && !state->physxProbe && (flags[i] & 15) == 2 && starts[i] >= 0 &&
                limit > starts[i] && limit <= geometryCount && limit - starts[i] >= 125) {
                state->physxProbe = probePhysXConvex(entries + 16ull * starts[i], limit - starts[i],
                                                    positions + 4ull * i, rotations + 4ull * i);
            }
            if (captureConvex && (flags[i] & 7) == 2 && starts[i] >= 0 &&
                limit > starts[i] && limit <= geometryCount && limit - starts[i] >= 125) {
                captureConvex(entries + 16ull * starts[i], limit - starts[i]);
            }
            if (starts[i] < 0 || starts[i] >= geometryCount || limit <= starts[i] ||
                limit > geometryCount ||
                !LegacyShapes::Convert(library, *backend, planeTransfer,
                    entries + 16ull * starts[i], limit - starts[i], flags[i],
                    shapes[accepted], flags12[accepted], convexShapes, distinctPlanes)) {
                if (runtimeLog && !state->loggedShapeRejection) {
                    char message[160];
                    std::snprintf(message, sizeof(message),
                        "FleX D3D11 skipped shape: flags=%#x, index=%d, end=%d, geometry=%d, distinct planes seen=%d.",
                        static_cast<unsigned>(flags[i]), starts[i], end, geometryCount, distinctPlanes);
                    runtimeLog(message);
                    state->loggedShapeRejection = true;
                }
                continue;
            }
            if (distinctPlanes > static_cast<int>(LegacyShapes::maxConvexPlanes)) {
                reducedFrom = distinctPlanes;
                reducedInput = limit - starts[i];
            }
            std::memcpy(pos12.data() + 4ull * accepted, positions + 4ull * i, 16);
            std::memcpy(rot12.data() + 4ull * accepted, rotations + 4ull * i, 16);
            std::memcpy(prevPos12.data() + 4ull * accepted,
                        (previousPositions ? previousPositions : positions) + 4ull * i, 16);
            std::memcpy(prevRot12.data() + 4ull * accepted,
                        (previousRotations ? previousRotations : rotations) + 4ull * i, 16);
            ++accepted;
        }
        if (usePhysXSceneShapes && state->physxProbe && state->physxFrames < 60) {
            const auto physxShapes = DescribePhysXShapes(entries, geometryCount, starts, positions, rotations, flags, shapeCount);
            const int translated = setPhysXSceneShapes(state->physxProbe, state->capacity,
                                                       physxShapes.data(), shapeCount);
            if ((!state->loggedPhysXShapes || translated < 0) && runtimeLog) {
                char message[128];
                std::snprintf(message, sizeof(message), "PhysX scene shapes: accepted %d of %d.", translated, shapeCount);
                runtimeLog(message);
                state->loggedPhysXShapes = true;
            }
            if (translated < 0) {
                destroyPhysXProbe(state->physxProbe);
                state->physxProbe = nullptr;
            }
        }
        if (!accepted) {
            backend->setShapes(state->solver, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0);
            backend->wait(library);
            state->convexShapes.clear();
            return;
        }
        const auto buffers = backend->buffers;
        FlexHostTransfer g(library, buffers, sizeof(NvFlexCollisionGeometry));
        FlexHostTransfer p(library, buffers, 16), q(library, buffers, 16);
        FlexHostTransfer pp(library, buffers, 16), pq(library, buffers, 16);
        FlexHostTransfer f(library, buffers, 4);
        auto* gb = g.Stage(shapes.data(), accepted);
        auto* pb = p.Stage(pos12.data(), accepted);
        auto* qb = q.Stage(rot12.data(), accepted);
        auto* ppb = pp.Stage(prevPos12.data(), accepted);
        auto* pqb = pq.Stage(prevRot12.data(), accepted);
        auto* fb = f.Stage(flags12.data(), accepted);
        if (!gb || !pb || !qb || !ppb || !pqb || !fb) {
            state->failed = true;
            return;
        }
        backend->setShapes(state->solver, gb, pb, qb, ppb, pqb, fb, accepted);
        backend->wait(library);
        state->convexShapes = std::move(convexShapes);
        if (runtimeLog && reducedFrom && !state->loggedConvexReduction) {
            char message[160];
            std::snprintf(message, sizeof(message),
                "FleX D3D11 submitted reduced convex: input=%d, distinct=%d, retained=%zu.",
                reducedInput, reducedFrom, LegacyShapes::maxConvexPlanes);
            runtimeLog(message);
            state->loggedConvexReduction = true;
        }
    } catch (const std::bad_alloc&) {
        state->failed = true;
    }
}

void flexSetRigids(LegacyParticleSolver* state, const int* offsets, const int* indices,
                   const FlexPoint* rest, const float* normals, const float* stiffness,
                   const FlexQuaternion* rotations, const FlexPoint* translations,
                   int count, int memory) {
    if (!AcceptHostTransfer(state, count, memory)) return;
    state->physxRigidsReady = false;
    state->physxRigidOutputReady = false;
    TraceLegacyCall(state, __func__, count, count > 0 && offsets ? offsets[count] : 0, count > 0);
    if (state->physxExclusive) {
        state->failed = exclusiveApi->rigids(state->physxExclusive, offsets, indices,
            reinterpret_cast<const float*>(rest), reinterpret_cast<const float*>(rotations),
            reinterpret_cast<const float*>(translations), count, count && offsets ? offsets[count] : 0) != 1;
        if (!state->failed) state->rigidCount = count;
        return;
    }
    if (!count) {
        if (state->rigid) state->rigid->Clear(state->solver);
        state->rigidCount = 0;
        return;
    }
    if (!offsets || !indices || offsets[count] <= 0 || offsets[count] > state->capacity) {
        state->failed = true;
        return;
    }
    const int indexCount = offsets[count];
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] < 0 || indices[i] >= state->capacity) {
            state->failed = true;
            return;
        }
    }
    if (!state->rigid) {
        state->rigid.reset(new (std::nothrow)
            FlexRigidTransfer(library, backend->buffers, backend->setRigids, backend->getRigids));
    }
    state->failed = !state->rigid || !state->rigid->Upload(
        state->solver, offsets, indices, rest, stiffness, rotations, translations,
        count, indexCount, normals);
    if (!state->failed) state->rigidCount = count;
    if (!state->failed && probePhysXRigids && state->physxProbe && state->physxFrames < 60 &&
        !state->physxInputPositions.empty() && !state->physxInputVelocities.empty()) {
        try {
            state->physxRigidRotations.resize(4ull * count);
            state->physxRigidTranslations.resize(3ull * count);
            state->physxOutputPositions.resize(state->physxInputPositions.size());
            state->physxOutputVelocities.resize(state->physxInputVelocities.size());
            state->physxRigidsReady = uploadPhysXRigids(
                state->physxProbe, state->capacity, state->physxInputPositions.data(),
                static_cast<int>(state->physxInputPositions.size() / 4),
                state->physxInputVelocities.data(), static_cast<int>(state->physxInputVelocities.size() / 3),
                state->activeIndices.data(), state->activeCount, offsets, indices,
                reinterpret_cast<const float*>(rest), reinterpret_cast<const float*>(rotations),
                reinterpret_cast<const float*>(translations), count, indexCount,
                state->lastParams.gravity, state->lastParams.radius) != 0;
        } catch (const std::bad_alloc&) {
            state->physxRigidsReady = false;
        }
        if (!state->physxRigidsReady) {
            runtimeLog("PhysX rigid probe: rejected frame input.");
            destroyPhysXProbe(state->physxProbe);
            state->physxProbe = nullptr;
        }
    }
}

void flexGetRigidTransforms(LegacyParticleSolver* state, FlexQuaternion* rotations,
                            FlexPoint* translations, int memory) {
    TraceLegacyCall(state, __func__, state && state->rigid ? 1 : 0, memory);
    if (!AcceptHostTransfer(state, 0, memory)) return;
    if (state->physxExclusive) {
        state->failed = exclusiveApi->getRigids(state->physxExclusive, reinterpret_cast<float*>(rotations),
            reinterpret_cast<float*>(translations), state->rigidCount) != 1;
        return;
    }
    if (state->rigid) {
        const auto count = static_cast<std::size_t>(state->rigidCount);
        if (usePhysXRigidOutput && state->physxProbe && state->physxRigidOutputReady && count &&
            (rotations || translations) && state->physxRigidRotations.size() == 4 * count &&
            state->physxRigidTranslations.size() == 3 * count) {
            static_assert(sizeof(FlexQuaternion) == 4 * sizeof(float));
            static_assert(sizeof(FlexPoint) == 3 * sizeof(float));
            if (rotations) std::memcpy(rotations, state->physxRigidRotations.data(), count * sizeof(*rotations));
            if (translations) std::memcpy(translations, state->physxRigidTranslations.data(), count * sizeof(*translations));
            if (!state->loggedPhysXOutput && runtimeLog) {
                runtimeLog("PhysX rigid output: returned group transforms to Fallout 4.");
                state->loggedPhysXOutput = true;
            }
            return;
        }
        state->failed = !state->rigid->Download(state->solver, rotations, translations);
    }
}

void flexSetActive(LegacyParticleSolver* state, const int* indices, int count, int memory) {
    if (!AcceptHostTransfer(state, count, memory)) return;
    if (traceLegacyCalls && count > 0 && !state->traceActivityStarted) {
        state->traceActivityStarted = true;
        state->traceCalls = 0;
    }
    TraceLegacyCall(state, __func__, count, memory);
    for (int i = 0; i < count; ++i) {
        if (!indices || indices[i] < 0 || indices[i] >= state->capacity) {
            state->failed = true;
            return;
        }
    }
    if (state->physxExclusive ? exclusiveApi->active(state->physxExclusive, indices, count) != 1 :
        !state->active.Upload(state->solver, indices, count, state->setActive)) {
        state->failed = true;
        return;
    }
    try {
        if (count) state->activeIndices.assign(indices, indices + count);
        else state->activeIndices.clear();
    } catch (const std::bad_alloc&) {
        state->failed = true;
        return;
    }
    if (!state->physxExclusive) state->setActiveCount(state->solver, count);
    state->activeCount = count;
}

int flexGetActiveCount(const LegacyParticleSolver* state) {
    return state ? state->activeCount : 0;
}

namespace {
struct ForcePosition { float x, y, z, inverseMass; };
struct ForceVelocity { float x, y, z; };

void StepPhysXProbe(LegacyParticleSolver& state, float dt, int substeps) {
    if (!state.physxProbe) return;
    if (state.physxFrames >= 60 || (state.physxParticle >= 0 &&
        std::find(state.activeIndices.begin(), state.activeIndices.end(), state.physxParticle) ==
            state.activeIndices.end())) {
        destroyPhysXProbe(state.physxProbe);
        state.physxProbe = nullptr;
        return;
    }
    if (dt <= 0 || state.activeIndices.empty()) return;
    float position[3]{}, velocity[3]{};
    if (!state.physxFrames) {
        const int index = state.activeIndices.front();
        if (index < 0 || index >= state.capacity) return;
        std::vector<float> positions(4ull * (index + 1));
        std::vector<float> velocities(3ull * (index + 1));
        if (!state.positions.Download(state.solver, positions.data(), index + 1, state.getPositions) ||
            !state.velocities.Download(state.solver, velocities.data(), index + 1, backend->getVelocities)) return;
        std::copy_n(positions.data() + 4ull * index, 3, position);
        std::copy_n(velocities.data() + 3ull * index, 3, velocity);
        state.physxParticle = index;
    }
    float pose[7]{};
    for (int i = 0; i < substeps; ++i) {
        if (!stepPhysXProbe(state.physxProbe, dt / substeps, position, velocity,
                            state.lastParams.gravity, state.lastParams.radius, pose)) {
            runtimeLog("PhysX probe: stepping or transform readback failed.");
            destroyPhysXProbe(state.physxProbe);
            state.physxProbe = nullptr;
            return;
        }
    }
    std::copy_n(pose, 3, state.physxPosition);
    if (++state.physxFrames == 1 || state.physxFrames == 60) {
        char message[180];
        std::snprintf(message, sizeof(message),
            "PhysX probe: frame %u, position=(%.4f, %.4f, %.4f), rotation=(%.4f, %.4f, %.4f, %.4f).",
            state.physxFrames, pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], pose[6]);
        runtimeLog(message);
    }
}

void ApplyForceFields(LegacyParticleSolver& state, float dt) {
    const int count = *std::max_element(state.activeIndices.begin(), state.activeIndices.end()) + 1;
    std::vector<ForcePosition> positions(count);
    std::vector<ForceVelocity> velocities(count);
    if (!state.positions.Download(state.solver, positions.data(), count, state.getPositions)) {
        state.failed = true;
        return;
    }
    if (!state.velocities.Download(state.solver, velocities.data(), count, backend->getVelocities)) {
        state.failed = true;
        return;
    }
    for (int index : state.activeIndices) {
        const auto& p = positions[index];
        auto& v = velocities[index];
        state.forceSeed = 1664525u * state.forceSeed + 1013904223u;
        const float random = static_cast<float>(state.forceSeed >> 8) / 16777216.0f;
        for (const auto& field : state.forceFields) {
            const float x = p.x - field.position[0];
            const float y = p.y - field.position[1];
            const float z = p.z - field.position[2];
            const float distance = std::sqrt(x*x + y*y + z*z);
            if (distance >= field.radius) continue;
            const float inverseDistance = distance > 0 ? 1.0f / distance : 0;
            float scale = field.minStrength + random * (field.maxStrength - field.minStrength);
            if (field.linearFalloff) scale *= 1 - distance / field.radius;
            if (field.mode != 2) scale *= p.inverseMass;
            if (field.mode == 0) scale *= dt;
            v.x += (x * inverseDistance + field.direction[0]) * scale;
            v.y += (y * inverseDistance + field.direction[1]) * scale;
            v.z += (z * inverseDistance + field.direction[2]) * scale;
        }
    }
    flexSetVelocities(&state, velocities.data(), count, 0);
}
}

void flexUpdateSolver(LegacyParticleSolver* state, float dt, int substeps, void* timers) {
    if (!state) return;
    state->physxRigidOutputReady = false;
    if (traceLegacyCalls) {
        TraceLegacyCall(state, __func__, state->activeCount, substeps);
        ++state->traceFrame;
    }
    if (state->failed && runtimeLog && !state->loggedFailure) {
        runtimeLog("FleX D3D11 solver rejected an operation before simulation.");
        if (state->lastOperation) runtimeLog(state->lastOperation);
        state->loggedFailure = true;
    }
    if (state->failed || !state->activeCount) return;
    if (timers || !std::isfinite(dt) || dt < 0 || substeps <= 0) {
        state->failed = true;
        return;
    }
    if (state->recording && !WriteDiagnosticFrame(*state, state->recording.get())) {
        state->recording.reset();
    }
    if (state->physxExclusive) {
        state->failed = exclusiveApi->step(state->physxExclusive, dt, substeps) != 1;
        if (!state->failed && !state->loggedFrame && runtimeLog) {
            runtimeLog("PhysX exclusive: first active simulation step completed.");
            if (!state->forceFields.empty()) runtimeLog("PhysX exclusive: force fields are not implemented.");
            state->loggedFrame = true;
        }
        return;
    }
    if (state->physxProbe && probePhysXRigids && state->physxRigidsReady && dt > 0) {
        const int groups = static_cast<int>(state->physxRigidTranslations.size() / 3);
        state->physxRigidsReady = false;
        if (!stepPhysXRigids(state->physxProbe, dt, substeps, state->physxRigidRotations.data(),
                             state->physxRigidTranslations.data(), groups, state->physxOutputPositions.data(),
                             static_cast<int>(state->physxOutputPositions.size() / 4),
                             state->physxOutputVelocities.data(),
                             static_cast<int>(state->physxOutputVelocities.size() / 3))) {
            runtimeLog("PhysX rigid probe: simulation or transform readback failed.");
            destroyPhysXProbe(state->physxProbe);
            state->physxProbe = nullptr;
        } else {
            state->physxRigidOutputReady = true;
            if (++state->physxFrames == 1 || state->physxFrames == 60) {
                char message[128];
                std::snprintf(message, sizeof(message), "PhysX rigid probe: frame %u returned %d group transforms.",
                              state->physxFrames, groups);
                runtimeLog(message);
            }
        }
    } else if (state->physxProbe && !probePhysXRigids) {
        try {
            StepPhysXProbe(*state, dt, substeps);
        } catch (const std::bad_alloc&) {
            runtimeLog("PhysX probe: could not allocate initial particle buffers.");
            destroyPhysXProbe(state->physxProbe);
            state->physxProbe = nullptr;
        }
    }
    state->update(state->solver, dt, substeps, false);
    if (!state->forceFields.empty()) {
        try {
            ApplyForceFields(*state, dt);
        } catch (const std::bad_alloc&) {
            state->failed = true;
        }
    }
    if (!state->failed && runtimeLog && !state->loggedFrame) {
        runtimeLog("FleX D3D11 submitted its first active simulation step.");
        state->loggedFrame = true;
    }
}
