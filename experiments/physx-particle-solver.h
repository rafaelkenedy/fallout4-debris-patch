#pragma once

#include "PxPhysicsAPI.h"
#include "physx-scene-shapes.h"

#include <array>
#include <memory>
#include <vector>

namespace Debris {
template<class T>
struct Release {
    void operator()(T* object) const { object->release(); }
};

template<class T>
using Owner = std::unique_ptr<T, Release<T>>;

struct Particle {
    std::array<float, 4> position{};
    std::array<float, 3> velocity{};
};

// The game owns slot IDs. Inactive slots have no PhysX actor and retain
// their host values until the game uploads a new particle into the slot.
class ParticleSolver {
public:
    ParticleSolver(physx::PxPhysics& physics, physx::PxCudaContextManager& cuda, int capacity);
    bool Ready() const { return scene_ && material_; }
    bool SetActive(const int* indices, int count);
    bool SetPositions(const float* positions, int count);
    bool SetVelocities(const float* velocities, int count);
    bool SetParameters(const float* gravity, float radius);
    bool SetGround(const physx::PxPlane& plane);
    bool SetStaticMesh(physx::PxTriangleMesh& mesh, const physx::PxTransform& pose);
    int SetSceneShapes(const PhysXSceneShape* shapes, int count);
    bool SetRigidGroups(const int* offsets, const int* indices, const float* rest,
                        const float* rotations, const float* translations,
                        int groupCount, int indexCount, const char** reason = nullptr);
    bool GetRigidTransforms(float* rotations, float* translations, int groupCount) const;
    bool Step(float dt, int substeps);
    bool GetPositions(float* positions, int count) const;
    bool GetVelocities(float* velocities, int count) const;

private:
    bool SyncActor(int index);

    struct RigidGroup {
        Owner<physx::PxRigidDynamic> actor;
        std::vector<int> indices;
        std::vector<physx::PxVec3> rest;
    };
    struct SceneShape {
        Owner<physx::PxTriangleMesh> mesh;
        Owner<physx::PxRigidActor> actor;
        std::vector<unsigned char> records;
        const void* meshKey = nullptr;
        unsigned long long revision = 0;
        int flags = 0;
        float scale = 0;
    };

    physx::PxPhysics& physics_;
    Owner<physx::PxDefaultCpuDispatcher> dispatcher_;
    Owner<physx::PxScene> scene_;
    Owner<physx::PxMaterial> material_;
    Owner<physx::PxRigidStatic> ground_;
    std::vector<Particle> particles_;
    std::vector<Owner<physx::PxRigidDynamic>> actors_;
    std::vector<int> active_;
    std::vector<unsigned char> activeMask_;
    std::vector<unsigned char> grouped_;
    std::vector<RigidGroup> groups_;
    std::vector<SceneShape> sceneShapes_;
    float radius_ = 0.05f;
};
}
