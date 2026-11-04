#include "physx-plane-mesh.h"
#include "physx-particle-solver.h"
#include "physx-exclusive-api.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace physx;

namespace {
template<class T>
struct Release {
    void operator()(T* object) const { object->release(); }
};
template<class T>
using Owner = std::unique_ptr<T, Release<T>>;

struct BridgeErrors : PxErrorCallback {
    void (*log)(const char*);
    explicit BridgeErrors(void (*callback)(const char*)) : log(callback) {}
    void reportError(PxErrorCode::Enum, const char* message, const char*, int) override {
        log(message);
    }
};

struct Bridge {
    PxDefaultAllocator allocator;
    BridgeErrors errors;
    explicit Bridge(void (*log)(const char*)) : errors(log) {}
    Owner<PxFoundation> foundation;
    Owner<PxCudaContextManager> cuda;
    Owner<PxPhysics> physics;
    Owner<PxDefaultCpuDispatcher> dispatcher;
    Owner<PxScene> scene;
    Owner<PxMaterial> material;
    Owner<PxRigidStatic> collider;
    Owner<PxRigidDynamic> fragment;
    Owner<PxTriangleMesh> rigidMesh;
    std::unique_ptr<Debris::ParticleSolver> rigidSolver;
    std::vector<Debris::Plane> planes;
};
}

extern "C" __declspec(dllexport) int PhysXDebrisStep(
    void* instance, float dt, const float* position, const float* velocity,
    const float* gravity, float radius, float* pose) noexcept {
    if (!instance || !position || !velocity || !gravity || !pose || !std::isfinite(dt) || dt <= 0 ||
        !std::isfinite(radius) || radius <= 0) return 0;
    auto& bridge = *static_cast<Bridge*>(instance);
    if (!bridge.fragment) {
        const PxVec3 start(position[0], position[1], position[2]);
        const PxVec3 speed(velocity[0], velocity[1], velocity[2]);
        if (!start.isFinite() || !speed.isFinite()) return 0;
        bridge.fragment.reset(PxCreateDynamic(*bridge.physics, PxTransform(start),
            PxSphereGeometry(radius), *bridge.material, 1.0f));
        if (!bridge.fragment) return 0;
        bridge.fragment->setLinearVelocity(speed);
        bridge.scene->addActor(*bridge.fragment);
    }
    const PxVec3 acceleration(gravity[0], gravity[1], gravity[2]);
    if (!acceleration.isFinite()) return 0;
    bridge.scene->setGravity(acceleration);
    bridge.scene->simulate(dt);
    if (!bridge.scene->fetchResults(true)) return 0;
    const auto transform = bridge.fragment->getGlobalPose();
    if (!transform.isValid()) return 0;
    const float values[]{transform.p.x, transform.p.y, transform.p.z,
                         transform.q.x, transform.q.y, transform.q.z, transform.q.w};
    std::memcpy(pose, values, sizeof(values));
    return 1;
}

static void* CreateBridge(void (*log)(const char*)) noexcept {
    if (!log) return nullptr;
    try {
        auto bridge = std::make_unique<Bridge>(log);
        log("PhysX bridge: creating foundation.");
        bridge->foundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION, bridge->allocator, bridge->errors));
        if (!bridge->foundation) return nullptr;
        log("PhysX bridge: creating CUDA context manager.");
        bridge->cuda.reset(PxCreateCudaContextManager(*bridge->foundation, PxCudaContextManagerDesc{}));
        if (!bridge->cuda || !bridge->cuda->contextIsValid()) return nullptr;
        log("PhysX bridge: CUDA context is valid. Creating physics.");
        bridge->physics.reset(PxCreatePhysics(PX_PHYSICS_VERSION, *bridge->foundation, PxTolerancesScale{}));
        if (!bridge->physics) return nullptr;
        log("PhysX bridge: physics created. Creating dispatcher.");
        // The one-shot mesh probe does not need a worker. PhysX's default
        // worker names itself by raising the legacy 0x406D1388 exception.
        bridge->dispatcher.reset(PxDefaultCpuDispatcherCreate(0));
        if (!bridge->dispatcher) return nullptr;
        PxSceneDesc desc(bridge->physics->getTolerancesScale());
        desc.cpuDispatcher = bridge->dispatcher.get();
        desc.filterShader = PxDefaultSimulationFilterShader;
        desc.cudaContextManager = bridge->cuda.get();
        desc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
        desc.broadPhaseType = PxBroadPhaseType::eGPU;
        log("PhysX bridge: creating GPU dynamics scene.");
        bridge->scene.reset(bridge->physics->createScene(desc));
        log("PhysX bridge: scene creation returned.");
        bridge->material.reset(bridge->physics->createMaterial(0.6f, 0.6f, 0.0f));
        if (!bridge->scene || !bridge->material ||
            !bridge->scene->getFlags().isSet(PxSceneFlag::eENABLE_GPU_DYNAMICS) ||
            bridge->scene->getBroadPhaseType() != PxBroadPhaseType::eGPU) return nullptr;
        return bridge.release();
    } catch (...) {
        return nullptr;
    }
}

static int LogStartupException(void (*log)(const char*), DWORD code) {
    char message[100];
    std::snprintf(message, sizeof(message), "PhysX bridge: startup SEH 0x%08lX.",
                  static_cast<unsigned long>(code));
    log(message);
    return EXCEPTION_CONTINUE_SEARCH;
}

extern "C" __declspec(dllexport) void* PhysXDebrisCreate(void (*log)(const char*)) noexcept {
    if (!log) return nullptr;
    __try {
        return CreateBridge(log);
    } __except (LogStartupException(log, GetExceptionCode())) {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) void PhysXDebrisDestroy(void* instance) noexcept {
    delete static_cast<Bridge*>(instance);
}

extern "C" __declspec(dllexport) int PhysXDebrisSubmitConvex(
    void* instance, const void* records, int count, const float* position, const float* rotation) noexcept {
    if (!instance || !records || count < 4 || count > 256 || !position || !rotation) return 0;
    try {
        auto& bridge = *static_cast<Bridge*>(instance);
        const PxTransform pose(PxVec3(position[0], position[1], position[2]),
                               PxQuat(rotation[0], rotation[1], rotation[2], rotation[3]));
        if (!pose.isValid()) return 0;
        static_assert(sizeof(Debris::Plane) == 16);
        const auto size = static_cast<std::size_t>(count) * sizeof(Debris::Plane);
        if (bridge.collider && bridge.planes.size() == static_cast<std::size_t>(count) &&
            std::memcmp(bridge.planes.data(), records, size) == 0) {
            bridge.collider->setGlobalPose(pose);
            return 1;
        }
        std::vector<Debris::Plane> planes(count);
        std::memcpy(planes.data(), records, size);
        auto [vertices, triangles] = Debris::PlaneMesh(planes);
        if (vertices.empty() || triangles.empty()) return 0;
        PxTriangleMeshDesc desc;
        desc.points = {vertices.data(), sizeof(PxVec3), static_cast<PxU32>(vertices.size())};
        desc.triangles = {triangles.data(), 3 * sizeof(PxU32), static_cast<PxU32>(triangles.size() / 3)};
        PxCookingParams cooking(bridge.physics->getTolerancesScale());
        cooking.buildGPUData = true;
        Owner<PxTriangleMesh> mesh(PxCreateTriangleMesh(
            cooking, desc, bridge.physics->getPhysicsInsertionCallback()));
        if (!mesh) return 0;
        Owner<PxRigidStatic> collider(PxCreateStatic(
            *bridge.physics, pose, PxTriangleMeshGeometry(mesh.get()), *bridge.material));
        if (!collider) return 0;
        bridge.scene->addActor(*collider);
        bridge.collider = std::move(collider);
        bridge.rigidMesh = std::move(mesh);
        bridge.planes = std::move(planes);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int PhysXDebrisUploadRigids(
    void* instance, int capacity, const float* positions, int positionCount,
    const float* velocities, int velocityCount, const int* active, int activeCount,
    const int* offsets, const int* indices, const float* rest, const float* rotations,
    const float* translations, int groups, int indexCount, const float* gravity, float radius) noexcept {
    try {
        auto& bridge = *static_cast<Bridge*>(instance);
        if (!bridge.rigidSolver) {
            auto solver = std::make_unique<Debris::ParticleSolver>(*bridge.physics, *bridge.cuda, capacity);
            if (!bridge.rigidMesh || !solver->SetStaticMesh(*bridge.rigidMesh, bridge.collider->getGlobalPose())) return 0;
            bridge.rigidSolver = std::move(solver);
        }
        auto& solver = *bridge.rigidSolver;
        const char* failure = nullptr;
        if (!solver.SetParameters(gravity, radius)) failure = "parameters";
        else if (!solver.SetPositions(positions, positionCount)) failure = "positions";
        else if (!solver.SetVelocities(velocities, velocityCount)) failure = "velocities";
        else if (!solver.SetActive(active, activeCount)) failure = "active indices";
        else if (!solver.SetRigidGroups(offsets, indices, rest, rotations, translations,
                                        groups, indexCount, &failure) && !failure) failure = "rigid groups";
        if (!failure) return 1;
        char message[128];
        std::snprintf(message, sizeof(message), "PhysX rigid probe: rejected %s.", failure);
        bridge.errors.log(message);
        return 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int PhysXDebrisStepFrame(
    void* instance, float dt, int substeps, float* rotations, float* translations, int groups,
    float* positions, int positionCount, float* velocities, int velocityCount) noexcept {
    auto& bridge = *static_cast<Bridge*>(instance);
    return bridge.rigidSolver && bridge.rigidSolver->Step(dt, substeps) &&
           bridge.rigidSolver->GetRigidTransforms(rotations, translations, groups) &&
           bridge.rigidSolver->GetPositions(positions, positionCount) &&
           bridge.rigidSolver->GetVelocities(velocities, velocityCount);
}

extern "C" __declspec(dllexport) int PhysXDebrisSetSceneShapes(
    void* instance, int capacity, const PhysXSceneShape* shapes, int count) noexcept {
    if (!instance || capacity <= 0) return -1;
    try {
        auto& bridge = *static_cast<Bridge*>(instance);
        if (!bridge.rigidSolver) {
            auto solver = std::make_unique<Debris::ParticleSolver>(*bridge.physics, *bridge.cuda, capacity);
            if (!solver->Ready()) return -1;
            bridge.rigidSolver = std::move(solver);
        }
        return bridge.rigidSolver->SetSceneShapes(shapes, count);
    } catch (...) {
        return -1;
    }
}

namespace {
using Solver = Debris::ParticleSolver;
template<auto Method, class... Args>
int ExclusiveCall(void* instance, Args... args) noexcept {
    try {
        return (static_cast<Solver*>(instance)->*Method)(args...);
    } catch (...) {
        return -1;
    }
}
}

extern "C" __declspec(dllexport) const PhysXExclusiveApi* PhysXDebrisExclusiveApiV1() {
    static const PhysXExclusiveApi api{
        [](void* runtime, int capacity) -> void* {
            try {
                auto& bridge = *static_cast<Bridge*>(runtime);
                auto solver = std::make_unique<Solver>(*bridge.physics, *bridge.cuda, capacity);
                return solver->Ready() ? solver.release() : nullptr;
            } catch (...) {
                return nullptr;
            }
        },
        [](void* solver) { delete static_cast<Solver*>(solver); },
        ExclusiveCall<&Solver::SetParameters, const float*, float>,
        ExclusiveCall<&Solver::SetPositions, const float*, int>,
        ExclusiveCall<&Solver::SetVelocities, const float*, int>,
        ExclusiveCall<&Solver::SetActive, const int*, int>,
        [](void* solver, const int* offsets, const int* indices, const float* rest,
           const float* rotations, const float* translations, int groups, int count) {
            return ExclusiveCall<&Solver::SetRigidGroups>(solver, offsets, indices, rest,
                       rotations, translations, groups, count, static_cast<const char**>(nullptr));
        },
        ExclusiveCall<&Solver::SetSceneShapes, const PhysXSceneShape*, int>,
        ExclusiveCall<&Solver::Step, float, int>,
        ExclusiveCall<&Solver::GetPositions, float*, int>,
        ExclusiveCall<&Solver::GetVelocities, float*, int>,
        ExclusiveCall<&Solver::GetRigidTransforms, float*, float*, int>,
    };
    return &api;
}
