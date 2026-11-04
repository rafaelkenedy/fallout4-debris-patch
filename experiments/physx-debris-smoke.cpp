#include "PxPhysicsAPI.h"
#include "physx-plane-mesh.h"
#include "physx-particle-solver.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

using namespace physx;

template<class T>
struct Release {
    void operator()(T* object) const { object->release(); }
};

template<class T>
auto Own(T* object) {
    return std::unique_ptr<T, Release<T>>(object);
}

struct Contacts : PxSimulationEventCallback {
    bool touched = false;
    void onContact(const PxContactPairHeader&, const PxContactPair* pairs, PxU32 count) override {
        for (PxU32 i = 0; i < count; ++i) {
            touched |= pairs[i].events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
        }
    }
    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onTrigger(PxTriggerPair*, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, PxU32) override {}
};

PxFilterFlags ReportContacts(PxFilterObjectAttributes, PxFilterData, PxFilterObjectAttributes,
                             PxFilterData, PxPairFlags& pair, const void*, PxU32) {
    pair = PxPairFlag::eCONTACT_DEFAULT | PxPairFlag::eNOTIFY_TOUCH_FOUND;
    return PxFilterFlag::eDEFAULT;
}

int TestParticleSolver(PxPhysics& physics, PxCudaContextManager& cuda) {
    Debris::ParticleSolver solver(physics, cuda, 8);
    const int active[]{0, 2};
    const float gravity[]{0, -9.81f, 0};
    const float input[]{0, 2, 0, 1, 4, 4, 4, 0, 2, 3, 0, 0.5f};
    const float velocities[]{0, 0, 0, 0, 0, 0, 1, 0, 0};
    if (!solver.Ready() || !solver.SetParameters(gravity, 0.075f) ||
        !solver.SetGround(PxPlane(PxVec3(0, 1, 0), 0)) ||
        !solver.SetActive(active, 2) || !solver.SetPositions(input, 3) ||
        !solver.SetVelocities(velocities, 3)) return 1;
    for (int i = 0; i < 120; ++i) {
        if (!solver.Step(1.0f / 60.0f, 2)) return 1;
    }
    float positions[12]{}, speeds[9]{};
    if (!solver.GetPositions(positions, 3) || !solver.GetVelocities(speeds, 3) ||
        std::abs(positions[1] - 0.075f) >= 0.02f || std::abs(positions[9] - 0.075f) >= 0.02f ||
        positions[8] <= 2.1f || positions[5] != 4.0f || positions[11] != 0.5f ||
        std::abs(speeds[1]) >= 0.05f || std::abs(speeds[7]) >= 0.05f) return 1;
    const float inactiveX = positions[8];
    if (!solver.SetActive(active, 1) || !solver.Step(1.0f / 60.0f, 1) ||
        !solver.GetPositions(positions, 3) || positions[8] != inactiveX ||
        !solver.SetPositions(input, 3) || !solver.SetActive(active, 2) ||
        !solver.Step(1.0f / 60.0f, 1) || !solver.GetPositions(positions, 3) ||
        positions[9] >= 3 || positions[9] < 2.9f) return 1;
    std::printf("PhysX-only particle channels: gravity, contact, position/velocity readback and slot reuse passed.\n");
    return 0;
}

int TestRigidGroups(PxPhysics& physics, PxCudaContextManager& cuda) {
    Debris::ParticleSolver solver(physics, cuda, 4);
    const int active[]{0, 1};
    const int offsets[]{0, 2};
    const float gravity[]{0, -9.81f, 0};
    const float positions[]{-0.15f, 2.85f, 0, 1, 0.15f, 3.15f, 0, 1};
    const float rest[]{-0.15f, -0.15f, 0, 0.15f, 0.15f, 0};
    const float rotations[]{0, 0, 0, 1};
    const float translations[]{0, 3, 0};
    if (!solver.Ready() || !solver.SetParameters(gravity, 0.075f) ||
        !solver.SetGround(PxPlane(PxVec3(0, 1, 0), 0)) ||
        !solver.SetPositions(positions, 2) || !solver.SetActive(active, 2) ||
        !solver.SetRigidGroups(offsets, active, rest, rotations, translations, 1, 2)) return 1;
    float q[4]{}, center[3]{}, result[8]{}, velocity[6]{};
    const float missingRotation[4]{};
    for (int i = 0; i < 120; ++i) {
        if (!solver.Step(1.0f / 60.0f, 2) ||
            !solver.GetRigidTransforms(q, center, 1) || !solver.GetPositions(result, 2) ||
            !solver.GetVelocities(velocity, 2) || !solver.SetPositions(result, 2) ||
            !solver.SetVelocities(velocity, 2) || !solver.SetActive(active, 2) ||
            !solver.SetRigidGroups(offsets, active, rest, i % 2 ? q : missingRotation,
                                   center, 1, 2)) return 1;
    }
    const auto delta = PxVec3(result[4] - result[0], result[5] - result[1], result[6] - result[2]);
    const float separation = delta.magnitude();
    std::printf("Rigid group: center y=%.6f, rotation z=%.6f, separation=%.6f\n",
                center[1], q[2], separation);
    if (center[1] < 0.04f || center[1] > 0.25f || std::abs(q[2]) < 0.1f ||
        std::abs(separation - std::sqrt(0.18f)) > 0.001f ||
        !solver.SetActive(active, 1) || solver.GetRigidTransforms(q, center, 1) ||
        !solver.SetRigidGroups(nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0) ||
        !solver.Step(1.0f / 60.0f, 1)) return 1;
    return 0;
}

int TestSceneShapes(PxPhysics& physics, PxCudaContextManager& cuda) {
    Debris::ParticleSolver solver(physics, cuda, 8);
    const float gravity[]{0, -9.81f, 0};
    const float identity[]{0, 0, 0, 1};
    const float sphere[]{0.5f, 0, 0, 0}, capsule[]{0.5f, 0.25f, 0, 0};
    const float cube[][4]{{1,0,0,-0.5f},{-1,0,0,-0.5f},{0,1,0,-0.5f},
                         {0,-1,0,-0.5f},{0,0,1,-0.5f},{0,0,-1,-0.5f}};
    const float vertices[]{-2,0,-2, -2,0,2, 2,0,-2, 2,0,2};
    const int triangles[]{0,1,2, 2,1,3};
    float shapePositions[][3]{{0,0,0},{3,0,0},{6,0,0},{9,0.5f,0}};
    PhysXSceneShape shapes[]{
        {0,sphere,1,shapePositions[0],identity},
        {9,capsule,1,shapePositions[1],identity},
        {2,cube,6,shapePositions[2],identity},
        {3,nullptr,0,shapePositions[3],identity,vertices,1,vertices,4,triangles,2,1}
    };
    const int active[]{0,1,2,3};
    const float positions[]{0,2,0,1, 3,2,0,1, 6,2,0,1, 9,2,0,1};
    if (!solver.SetParameters(gravity, 0.075f) || solver.SetSceneShapes(shapes, 4) != 4 ||
        !solver.SetPositions(positions, 4) || !solver.SetActive(active, 4)) return 1;
    for (int i = 0; i < 120; ++i) {
        if (solver.SetSceneShapes(shapes, 4) != 4 || !solver.Step(1.0f / 60.0f, 2)) return 1;
    }
    float result[16]{};
    if (!solver.GetPositions(result, 4)) return 1;
    std::printf("Scene shapes contact heights: %.5f %.5f %.5f %.5f\n", result[1], result[5], result[9], result[13]);
    for (int i = 1; i < 16; i += 4) if (std::abs(result[i] - 0.575f) >= 0.03f) return 1;
    shapePositions[1][1] = -0.25f;
    if (solver.SetSceneShapes(shapes, 4) != 4) return 1;
    for (int i = 0; i < 120; ++i) if (!solver.Step(1.0f / 60.0f, 2)) return 1;
    if (!solver.GetPositions(result, 4)) return 1;
    std::printf("Moved capsule contact height: %.5f\n", result[5]);
    if (std::abs(result[5] - 0.325f) >= 0.03f ||
        solver.SetSceneShapes(nullptr, 0) != 0) return 1;
    for (int i = 0; i < 60; ++i) if (!solver.Step(1.0f / 60.0f, 2)) return 1;
    if (!solver.GetPositions(result, 4)) return 1;
    std::printf("Scene shapes removed: %.5f %.5f %.5f %.5f\n", result[1], result[5], result[9], result[13]);
    for (int i = 1; i < 16; i += 4) if (result[i] > -1.0f) return 1;
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 2) return 2;
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    const auto foundation = Own(PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errors));
    if (!foundation) return 1;
    const auto cuda = Own(PxCreateCudaContextManager(*foundation, PxCudaContextManagerDesc{}));
    if (!cuda || !cuda->contextIsValid()) {
        std::fprintf(stderr, "PhysX CUDA initialization failed\n");
        return 1;
    }
    std::printf("PhysX GPU: %s\n", cuda->getDeviceName());
    const auto physics = Own(PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale{}));
    if (physics && argc == 2 && std::wstring_view(argv[1]) == L"--particles") {
        return TestParticleSolver(*physics, *cuda);
    }
    if (physics && argc == 2 && std::wstring_view(argv[1]) == L"--rigids") {
        return TestRigidGroups(*physics, *cuda);
    }
    if (physics && argc == 2 && std::wstring_view(argv[1]) == L"--shapes") {
        return TestSceneShapes(*physics, *cuda);
    }
    const auto dispatcher = Own(PxDefaultCpuDispatcherCreate(1));
    if (!physics || !dispatcher) return 1;

    Contacts contacts;
    PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0, -9.81f, 0);
    sceneDesc.cpuDispatcher = dispatcher.get();
    sceneDesc.filterShader = ReportContacts;
    sceneDesc.simulationEventCallback = &contacts;
    sceneDesc.cudaContextManager = cuda.get();
    sceneDesc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
    sceneDesc.broadPhaseType = PxBroadPhaseType::eGPU;
    const auto scene = Own(physics->createScene(sceneDesc));
    const auto material = Own(physics->createMaterial(0.6f, 0.6f, 0.0f));
    if (!scene || !material || !scene->getFlags().isSet(PxSceneFlag::eENABLE_GPU_DYNAMICS) ||
        scene->getBroadPhaseType() != PxBroadPhaseType::eGPU) return 1;

    // Legacy-format half-spaces for a 123-sided prism plus two caps.
    constexpr PxU32 sides = 123;
    constexpr float tau = 6.283185307179586f;
    std::vector<Debris::Plane> planes{{0, -1, 0, 0}};
    for (PxU32 i = 0; i < sides; ++i) {
        const float angle = tau * static_cast<float>(i) / sides;
        planes.push_back({std::cos(angle), 0, std::sin(angle), -1});
    }
    if (argc == 2) {
        std::ifstream capture(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
        const auto size = static_cast<std::streamoff>(capture.tellg());
        static_assert(sizeof(Debris::Plane) == 16);
        if (size < 4 * 16 || size > 256 * 16 || size % 16) return 2;
        planes.resize(static_cast<std::size_t>(size / 16));
        capture.seekg(0);
        if (!capture.read(reinterpret_cast<char*>(planes.data()), size)) return 2;
    } else {
        if (!Debris::PlaneMesh(planes).indices.empty()) return 1;
        planes.push_back({0, 1, 0, -0.5f});
    }
    auto [vertices, triangles] = Debris::PlaneMesh(planes);
    if (vertices.empty() || triangles.empty()) {
        std::fprintf(stderr, "Could not reconstruct a closed volume from %zu planes\n", planes.size());
        return 1;
    }
    if (argc == 1 && (vertices.size() != 2 * sides || triangles.size() != 3 * (4 * sides - 4))) return 1;
    std::printf("Converted %zu planes to %zu vertices and %zu triangles\n",
                planes.size(), vertices.size(), triangles.size() / 3);
    PxTriangleMeshDesc meshDesc;
    meshDesc.points = {vertices.data(), sizeof(PxVec3), static_cast<PxU32>(vertices.size())};
    meshDesc.triangles = {triangles.data(), 3 * sizeof(PxU32), static_cast<PxU32>(triangles.size() / 3)};
    PxCookingParams cooking(physics->getTolerancesScale());
    cooking.buildGPUData = true;
    const auto mesh = Own(PxCreateTriangleMesh(cooking, meshDesc, physics->getPhysicsInsertionCallback()));
    if (!mesh) return 1;
    const auto prism = Own(PxCreateStatic(*physics, PxTransform(PxIdentity),
                                         PxTriangleMeshGeometry(mesh.get()), *material));
    if (!prism) return 1;
    scene->addActor(*prism);
    if (argc == 2) {
        PxVec3 center(0), normal(0);
        for (std::size_t i = 0; i < triangles.size(); i += 3) {
            const auto a = vertices[triangles[i]];
            const auto b = vertices[triangles[i + 1]];
            const auto c = vertices[triangles[i + 2]];
            const auto cross = (b - a).cross(c - a);
            if (cross.magnitudeSquared() > normal.magnitudeSquared()) {
                normal = cross;
                center = (a + b + c) / 3.0f;
            }
        }
        if (normal.isZero()) return 1;
        normal.normalize();
        scene->setGravity(-9.81f * normal);
        const auto impactor = Own(PxCreateDynamic(*physics, PxTransform(center + normal),
                                                  PxSphereGeometry(0.075f), *material, 1.0f));
        if (!impactor) return 1;
        scene->addActor(*impactor);
        for (int step = 0; step < 120 && !contacts.touched; ++step) {
            scene->simulate(1.0f / 60.0f);
            if (!scene->fetchResults(true)) return 1;
        }
        std::printf("Captured mesh impact: %s\n", contacts.touched ? "contact reported" : "no contact");
        return contacts.touched ? 0 : 1;
    }
    const PxBoxGeometry fragmentGeometry(0.075f, 0.075f, 0.075f);
    const auto fragment = Own(PxCreateDynamic(*physics, PxTransform(PxVec3(0, 2, 0)),
                                              fragmentGeometry, *material, 1.0f));
    const auto control = Own(PxCreateDynamic(*physics, PxTransform(PxVec3(3, 2, 0)),
                                             fragmentGeometry, *material, 1.0f));
    if (!fragment || !control) return 1;
    scene->addActor(*fragment);
    scene->addActor(*control);
    for (int step = 0; step < 120; ++step) {
        scene->simulate(1.0f / 60.0f);
        if (!scene->fetchResults(true)) return 1;
    }
    const float landed = fragment->getGlobalPose().p.y;
    const float fell = control->getGlobalPose().p.y;
    std::printf("125-plane prism: fragment y=%.6f, control y=%.6f\n", landed, fell);
    return std::abs(landed - 0.575f) < 0.02f && fell < -10.0f ? 0 : 1;
}
