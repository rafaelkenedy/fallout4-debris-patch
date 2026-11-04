#include <cmath>
#include <algorithm>
#include <limits>
#include <malloc.h>
#include <new>
#include <vector>
#include "flex-legacy-particles.h"
#include "flex-legacy-shapes.h"

namespace {
const LegacySolverBackend* backend;
NvFlexLibrary* library;
LegacyFlexError errorCallback;
unsigned liveSolvers;
void (*runtimeLog)(const char*);

void ReportError(NvFlexErrorSeverity severity, const char* message, const char* file, int line) {
    if (errorCallback) errorCallback(static_cast<int>(severity), message, file, line);
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

void BindLegacySolverBackend(const LegacySolverBackend* binding) {
    if (!library) backend = binding;
}

void BindLegacyRuntimeLog(void (*log)(const char*)) {
    runtimeLog = log;
}

NvFlexLibrary* LegacySolverLibrary() {
    return library;
}

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
    if (!library || liveSolvers) return;
    backend->shutdown(library);
    library = nullptr;
    errorCallback = nullptr;
}

void* flexAlloc(int bytes) {
    return library && bytes > 0 ? _aligned_malloc(static_cast<size_t>(bytes), 16) : nullptr;
}

void flexFree(void* pointer) {
    _aligned_free(pointer);
}

LegacyParticleSolver* flexCreateSolver(int capacity, int legacyArgument, std::uint8_t maxNeighbors) {
    if (!library || capacity <= 0 || legacyArgument != 0 || !maxNeighbors) return nullptr;
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
    return state;
}

void flexDestroySolver(LegacyParticleSolver* state) {
    if (!state) return;
    auto* solver = state->solver;
    const auto destroy = state->destroy;
    if (!state->convexShapes.empty()) backend->wait(library);
    delete state;
    destroy(solver);
    --liveSolvers;
}

void flexSetParams(LegacyParticleSolver* state, const LegacyFlexParams* source) {
    if (!state || state->failed) return;
    state->lastOperation = __func__;
    if (!source || source->iterations <= 0 || !std::isfinite(source->radius) ||
        source->radius <= 0 || source->numPlanes < 0 || source->numPlanes > 8 ||
        (source->relaxationMode != 0 && source->relaxationMode != 1)) {
        state->failed = true;
        if (runtimeLog) runtimeLog("FleX D3D11 rejected legacy solver parameters.");
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
        state->failed = !state->positions.Upload(state->solver, data, count, state->setPositions);
    }
}

void flexGetParticles(LegacyParticleSolver* state, void* data, int count, int memory) {
    if (AcceptHostTransfer(state, count, memory)) {
        state->failed = !state->positions.Download(state->solver, data, count, state->getPositions);
        if (!state->failed && state->loggedFrame && !state->loggedReadback && count > 0 && runtimeLog) {
            runtimeLog("FleX D3D11 particle readback succeeded.");
            state->loggedReadback = true;
        }
    }
}

void flexSetVelocities(LegacyParticleSolver* state, const void* data, int count, int memory) {
    if (AcceptHostTransfer(state, count, memory)) {
        state->failed = !state->velocities.Upload(state->solver, data, count, state->setVelocities);
    }
}

void flexGetVelocities(LegacyParticleSolver* state, void* data, int count, int memory) {
    if (AcceptHostTransfer(state, count, memory)) {
        state->failed = !state->velocities.Download(state->solver, data, count, backend->getVelocities);
    }
}

void flexSetPhases(LegacyParticleSolver* state, const int* data, int count, int memory) {
    if (!AcceptHostTransfer(state, count, memory) || count == 0) return;
    if (!data) {
        state->failed = true;
        return;
    }
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
        if (!state.positions.Download(state.solver, positions.data(), state.capacity, state.getPositions) ||
            !state.velocities.Download(state.solver, velocities.data(), state.capacity, backend->getVelocities)) {
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

void flexSetShapes(LegacyParticleSolver* state, const void* geometry, int geometryCount,
                   const float*, const float*, const int* starts,
                   const float* positions, const float* rotations,
                   const float* previousPositions, const float* previousRotations,
                   const int* flags, int shapeCount, int memory) {
    if (!AcceptHostTransfer(state, 0, memory)) return;
    if (shapeCount == 0 || geometryCount == 0) {
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
        std::vector<NvFlexCollisionGeometry> shapes(shapeCount);
        std::vector<int> flags12(shapeCount);
        std::vector<float> pos12(4ull * shapeCount), rot12(4ull * shapeCount);
        std::vector<float> prevPos12(4ull * shapeCount), prevRot12(4ull * shapeCount);
        std::vector<std::unique_ptr<LegacyConvexMesh>> convexShapes;
        FlexHostTransfer planeTransfer(library, backend->buffers, 16);
        auto* entries = static_cast<const unsigned char*>(geometry);
        int accepted = 0;
        int reducedFrom = 0;
        int reducedInput = 0;
        for (int i = 0; i < shapeCount; ++i) {
            int distinctPlanes = 0;
            const int end = i + 1 < shapeCount ? starts[i + 1] : geometryCount;
            const int limit = (flags[i] & 7) == 2 ? end : starts[i] + 1;
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
    if (!count) {
        if (state->rigid) state->rigid->Clear(state->solver);
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
}

void flexGetRigidTransforms(LegacyParticleSolver* state, FlexQuaternion* rotations,
                            FlexPoint* translations, int memory) {
    if (AcceptHostTransfer(state, 0, memory) && state->rigid) {
        state->failed = !state->rigid->Download(state->solver, rotations, translations);
    }
}

void flexSetActive(LegacyParticleSolver* state, const int* indices, int count, int memory) {
    if (!AcceptHostTransfer(state, count, memory)) return;
    for (int i = 0; i < count; ++i) {
        if (!indices || indices[i] < 0 || indices[i] >= state->capacity) {
            state->failed = true;
            return;
        }
    }
    if (!state->active.Upload(state->solver, indices, count, state->setActive)) {
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
    state->setActiveCount(state->solver, count);
    state->activeCount = count;
}

int flexGetActiveCount(const LegacyParticleSolver* state) {
    return state ? state->activeCount : 0;
}

namespace {
struct ForcePosition { float x, y, z, inverseMass; };
struct ForceVelocity { float x, y, z; };

void ApplyForceFields(LegacyParticleSolver& state, float dt) {
    const int count = *std::max_element(state.activeIndices.begin(), state.activeIndices.end()) + 1;
    std::vector<ForcePosition> positions(count);
    std::vector<ForceVelocity> velocities(count);
    flexGetParticles(&state, positions.data(), count, 0);
    flexGetVelocities(&state, velocities.data(), count, 0);
    if (state.failed) return;
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
