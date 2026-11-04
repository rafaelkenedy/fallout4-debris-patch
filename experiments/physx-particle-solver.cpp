#include "physx-particle-solver.h"
#include "physx-plane-mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace physx;

namespace Debris {
namespace {
PxVec3 ReadVector(const float* xyz) {
    return PxVec3(xyz[0], xyz[1], xyz[2]);
}
}

ParticleSolver::ParticleSolver(PxPhysics& physics, PxCudaContextManager& cuda, int capacity)
    : physics_(physics), dispatcher_(PxDefaultCpuDispatcherCreate(0)),
      particles_(capacity), actors_(capacity), activeMask_(capacity), grouped_(capacity) {
    if (!dispatcher_) return;
    PxSceneDesc desc(physics.getTolerancesScale());
    desc.cpuDispatcher = dispatcher_.get();
    desc.filterShader = PxDefaultSimulationFilterShader;
    desc.cudaContextManager = &cuda;
    desc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
    desc.broadPhaseType = PxBroadPhaseType::eGPU;
    scene_.reset(physics.createScene(desc));
    if (!scene_ || !scene_->getFlags().isSet(PxSceneFlag::eENABLE_GPU_DYNAMICS) ||
        scene_->getBroadPhaseType() != PxBroadPhaseType::eGPU) {
        scene_.reset();
        return;
    }
    material_.reset(physics.createMaterial(0.4f, 0.4f, 0.0f));
}

bool ParticleSolver::SyncActor(int index) {
    const auto& p = particles_[index];
    auto& actor = actors_[index];
    if (!activeMask_[index] || grouped_[index] || p.position[3] == 0) {
        actor.reset();
        return true;
    }
    const float mass = 1.0f / p.position[3];
    const PxTransform pose(ReadVector(p.position.data()));
    if (!actor) {
        actor.reset(PxCreateDynamic(physics_, pose, PxSphereGeometry(radius_), *material_, 1.0f));
        if (!actor || !PxRigidBodyExt::setMassAndUpdateInertia(*actor, mass)) {
            actor.reset();
            return false;
        }
        actor->setLinearVelocity(ReadVector(p.velocity.data()));
        scene_->addActor(*actor);
        return true;
    }
    actor->setGlobalPose(pose);
    actor->setLinearVelocity(ReadVector(p.velocity.data()));
    return PxRigidBodyExt::setMassAndUpdateInertia(*actor, mass);
}

bool ParticleSolver::SetActive(const int* indices, int count) {
    if (!Ready() || count < 0 || static_cast<std::size_t>(count) > particles_.size() ||
        (count && !indices)) return false;
    std::vector<unsigned char> mask(particles_.size());
    for (int i = 0; i < count; ++i) {
        const int index = indices[i];
        if (index < 0 || static_cast<std::size_t>(index) >= mask.size() || mask[index]) return false;
        mask[index] = 1;
    }
    for (auto& group : groups_) {
        if (group.actor && std::any_of(group.indices.begin(), group.indices.end(),
                                      [&](int index) { return !mask[index]; })) {
            group.actor.reset();
            for (int index : group.indices) grouped_[index] = 0;
        }
    }
    for (int index : active_) {
        if (!mask[index]) actors_[index].reset();
    }
    active_.clear();
    if (count) active_.assign(indices, indices + count);
    activeMask_.swap(mask);
    for (int index : active_) {
        if (!actors_[index] && !SyncActor(index)) return false;
    }
    return true;
}

bool ParticleSolver::SetRigidGroups(const int* offsets, const int* indices, const float* rest,
                                   const float* rotations, const float* translations,
                                   int groupCount, int indexCount, const char** reason) {
    const auto reject = [&](const char* message) {
        if (reason) *reason = message;
        return false;
    };
    if (!Ready() || groupCount < 0 || indexCount < groupCount ||
        static_cast<std::size_t>(indexCount) > particles_.size() ||
        (!groupCount && indexCount) ||
        (groupCount && (!offsets || !indices || !rest || !rotations || !translations ||
                        offsets[0] != 0 || offsets[groupCount] != indexCount))) return reject("invalid group buffers or counts");

    std::vector<unsigned char> nextGrouped(particles_.size());
    std::vector<RigidGroup> next;
    std::vector<std::size_t> reuse;
    next.reserve(groupCount);
    reuse.reserve(groupCount);
    for (int group = 0; group < groupCount; ++group) {
        const int begin = offsets[group], end = offsets[group + 1];
        if (begin < 0 || end <= begin || end > indexCount) return reject("invalid group offsets");
        const auto* q = rotations + 4ull * group;
        PxQuat rotation(q[0], q[1], q[2], q[3]);
        const float norm = rotation.magnitudeSquared();
        const bool hasRotation = std::isfinite(norm) && norm > 1.e-12f;
        RigidGroup body;
        double mass = 0.0;
        PxVec3 momentum(0);
        for (int i = begin; i < end; ++i) {
            const int slot = indices[i];
            if (slot < 0 || static_cast<std::size_t>(slot) >= particles_.size()) return reject("out-of-range group member");
            if (!activeMask_[slot]) return reject("inactive group member");
            if (nextGrouped[slot]) return reject("overlapping group member");
            const auto offset = ReadVector(rest + 3ull * i);
            const auto inverseMass = particles_[slot].position[3];
            if (!offset.isFinite()) return reject("nonfinite rest position");
            if (inverseMass <= 0) return reject("immovable group member");
            const float particleMass = 1.0f / inverseMass;
            mass += particleMass;
            momentum += ReadVector(particles_[slot].velocity.data()) * particleMass;
            body.indices.push_back(slot);
            body.rest.push_back(offset);
            nextGrouped[slot] = 1;
        }
        if (mass > PX_MAX_F32 || !momentum.isFinite()) return reject("invalid group mass or momentum");
        auto prior = std::find_if(groups_.begin(), groups_.end(), [&](const RigidGroup& old) {
            return old.actor && old.indices == body.indices && old.rest == body.rest;
        });
        if (hasRotation) rotation = rotation.getNormalized();
        else rotation = prior != groups_.end() ? prior->actor->getGlobalPose().q : PxQuat(PxIdentity);
        const PxTransform pose(ReadVector(translations + 3ull * group), rotation);
        if (!pose.isValid()) return reject("invalid group pose");
        if (prior != groups_.end()) {
            const auto previous = prior->actor->getGlobalPose();
            if ((previous.p - pose.p).magnitudeSquared() > 1.e-8f ||
                std::abs(previous.q.dot(pose.q)) < 0.99999f ||
                prior->actor->getMass() != static_cast<float>(mass)) prior = groups_.end();
        }
        reuse.push_back(static_cast<std::size_t>(prior - groups_.begin()));
        if (prior == groups_.end()) {
            body.actor.reset(physics_.createRigidDynamic(pose));
            if (!body.actor) return reject("actor creation failed");
            for (const auto& offset : body.rest) {
                const Owner<PxShape> sphere(physics_.createShape(PxSphereGeometry(radius_), *material_, true));
                if (!sphere) return reject("sphere creation failed");
                sphere->setLocalPose(PxTransform(offset));
                if (!body.actor->attachShape(*sphere)) return reject("sphere attachment failed");
            }
            if (!PxRigidBodyExt::setMassAndUpdateInertia(*body.actor, static_cast<float>(mass))) return reject("inertia setup failed");
            body.actor->setLinearVelocity(momentum / static_cast<float>(mass));
        }
        next.push_back(std::move(body));
    }
    for (std::size_t i = 0; i < next.size(); ++i) {
        if (reuse[i] < groups_.size()) next[i].actor = std::move(groups_[reuse[i]].actor);
        else scene_->addActor(*next[i].actor);
    }
    groups_ = std::move(next);
    grouped_.swap(nextGrouped);
    for (int index : active_) {
        if (grouped_[index]) actors_[index].reset();
        else if (!actors_[index] && !SyncActor(index)) return reject("ungrouped actor creation failed");
    }
    return true;
}

bool ParticleSolver::GetRigidTransforms(float* rotations, float* translations, int groupCount) const {
    if (groupCount < 0 || static_cast<std::size_t>(groupCount) != groups_.size()) return false;
    for (int i = 0; i < groupCount; ++i) {
        if (!groups_[i].actor) return false;
        const auto pose = groups_[i].actor->getGlobalPose();
        if (!pose.isValid()) return false;
        const float q[]{pose.q.x, pose.q.y, pose.q.z, pose.q.w};
        const float p[]{pose.p.x, pose.p.y, pose.p.z};
        if (rotations) std::copy_n(q, 4, rotations + 4ull * i);
        if (translations) std::copy_n(p, 3, translations + 3ull * i);
    }
    return true;
}

bool ParticleSolver::SetPositions(const float* positions, int count) {
    if (!Ready() || count < 0 || static_cast<std::size_t>(count) > particles_.size() ||
        (count && !positions)) return false;
    for (int i = 0; i < count; ++i) {
        const auto* p = positions + 4ull * i;
        if (!ReadVector(p).isFinite() || !std::isfinite(p[3]) || p[3] < 0 ||
            (p[3] > 0 && !std::isfinite(1.0f / p[3]))) return false;
    }
    for (int i = 0; i < count; ++i) {
        const auto* p = positions + 4ull * i;
        std::copy_n(p, 4, particles_[i].position.begin());
        if (!SyncActor(i)) return false;
    }
    return true;
}

bool ParticleSolver::SetVelocities(const float* velocities, int count) {
    if (!Ready() || count < 0 || static_cast<std::size_t>(count) > particles_.size() ||
        (count && !velocities)) return false;
    for (int i = 0; i < count; ++i) {
        if (!ReadVector(velocities + 3ull * i).isFinite()) return false;
    }
    for (int i = 0; i < count; ++i) {
        const auto* velocity = velocities + 3ull * i;
        std::copy_n(velocity, 3, particles_[i].velocity.begin());
        if (actors_[i]) actors_[i]->setLinearVelocity(ReadVector(velocity));
    }
    return true;
}

bool ParticleSolver::SetParameters(const float* gravity, float radius) {
    if (!Ready() || !gravity || !ReadVector(gravity).isFinite() || !std::isfinite(radius) || radius <= 0) return false;
    if (radius != radius_) {
        if (!groups_.empty()) return false;
        for (int index : active_) {
            if (!actors_[index]) continue;
            PxShape* shape = nullptr;
            actors_[index]->getShapes(&shape, 1);
            shape->setGeometry(PxSphereGeometry(radius));
            const float mass = 1.0f / particles_[index].position[3];
            if (!PxRigidBodyExt::setMassAndUpdateInertia(*actors_[index], mass)) return false;
        }
        radius_ = radius;
    }
    scene_->setGravity(ReadVector(gravity));
    return true;
}

bool ParticleSolver::SetGround(const PxPlane& plane) {
    if (!Ready() || !plane.n.isFinite() || !plane.n.isNormalized() || !std::isfinite(plane.d)) return false;
    Owner<PxRigidStatic> next(PxCreatePlane(physics_, plane, *material_));
    if (!next) return false;
    scene_->addActor(*next);
    ground_ = std::move(next);
    return true;
}

bool ParticleSolver::SetStaticMesh(PxTriangleMesh& mesh, const PxTransform& pose) {
    if (!Ready() || !pose.isValid()) return false;
    Owner<PxRigidStatic> next(PxCreateStatic(physics_, pose, PxTriangleMeshGeometry(&mesh), *material_));
    if (!next) return false;
    scene_->addActor(*next);
    ground_ = std::move(next);
    return true;
}

int ParticleSolver::SetSceneShapes(const PhysXSceneShape* shapes, int count) {
    if (!Ready() || count < 0 || (count && !shapes)) return -1;
    std::vector<SceneShape> next(count);
    int accepted = 0;
    for (int i = 0; i < count; ++i) {
        const auto& input = shapes[i];
        const int type = input.flags & 7;
        if (type > 3 || !input.position || !input.rotation) continue;
        const auto* q = input.rotation;
        const PxQuat rotation(q[0], q[1], q[2], q[3]);
        const float norm = rotation.magnitudeSquared();
        if (!std::isfinite(norm) || norm <= 1.e-12f) continue;
        const PxTransform pose(ReadVector(input.position), rotation.getNormalized());
        if (!pose.isValid()) continue;
        const bool dynamic = (input.flags & 8) != 0;
        if (type == 3) {
            if (!input.meshKey || !input.vertices || input.vertexCount < 3 ||
                !input.triangles || input.triangleCount <= 0 ||
                !std::isfinite(input.scale) || input.scale <= 0) continue;
        } else if (!input.records || input.count < (type == 2 ? 4 : 1) || input.count > 256) {
            continue;
        }
        const auto bytes = type == 3 ? 0 : static_cast<std::size_t>(input.count) * 16;
        const auto matches = [&](const SceneShape& cached) {
            return cached.flags == input.flags && cached.meshKey == input.meshKey &&
                   cached.revision == input.meshRevision && cached.scale == input.scale &&
                   cached.records.size() == bytes &&
                   (!bytes || std::memcmp(cached.records.data(), input.records, bytes) == 0);
        };
        auto prior = sceneShapes_.end();
        if (static_cast<std::size_t>(i) < sceneShapes_.size() && matches(sceneShapes_[i])) {
            prior = sceneShapes_.begin() + i;
        } else {
            prior = std::find_if(sceneShapes_.begin(), sceneShapes_.end(), matches);
        }
        auto& output = next[i];
        if (prior != sceneShapes_.end()) {
            output = std::move(*prior);
            prior->meshKey = nullptr;
            prior->flags = -1;
            if (output.actor) {
                if (dynamic) static_cast<PxRigidDynamic&>(*output.actor).setKinematicTarget(pose);
                else output.actor->setGlobalPose(pose);
                ++accepted;
            }
            continue;
        }
        output.flags = input.flags;
        output.meshKey = input.meshKey;
        output.revision = input.meshRevision;
        output.scale = input.scale;
        if (bytes) {
            const auto* records = static_cast<const unsigned char*>(input.records);
            output.records.assign(records, records + bytes);
        }
        Owner<PxShape> shape;
        if (type <= 1) {
            float values[2]{};
            std::memcpy(values, input.records, sizeof(values));
            if (!std::isfinite(values[0]) || values[0] <= 0) continue;
            if (type == 0 || values[1] == 0) shape.reset(physics_.createShape(PxSphereGeometry(values[0]), *material_, true));
            else if (std::isfinite(values[1]) && values[1] > 0) {
                shape.reset(physics_.createShape(PxCapsuleGeometry(values[0], values[1]), *material_, true));
            }
        } else {
            TriangleMesh convex;
            PxTriangleMeshDesc desc;
            if (type == 2) {
                std::vector<Plane> planes(input.count);
                std::memcpy(planes.data(), input.records, bytes);
                convex = PlaneMesh(planes);
                if (convex.indices.empty()) continue;
                desc.points = {convex.vertices.data(), sizeof(PxVec3), static_cast<PxU32>(convex.vertices.size())};
                desc.triangles = {convex.indices.data(), 3 * sizeof(PxU32), static_cast<PxU32>(convex.indices.size() / 3)};
            } else {
                desc.points = {const_cast<float*>(input.vertices), 3 * sizeof(float), static_cast<PxU32>(input.vertexCount)};
                desc.triangles = {const_cast<int*>(input.triangles), 3 * sizeof(int), static_cast<PxU32>(input.triangleCount)};
            }
            PxCookingParams cooking(physics_.getTolerancesScale());
            cooking.buildGPUData = true;
            output.mesh.reset(PxCreateTriangleMesh(cooking, desc, physics_.getPhysicsInsertionCallback()));
            if (!output.mesh) continue;
            const PxMeshScale scale(PxVec3(type == 3 ? input.scale : 1.0f));
            shape.reset(physics_.createShape(PxTriangleMeshGeometry(output.mesh.get(), scale), *material_, true));
        }
        if (!shape) continue;
        if (dynamic) {
            Owner<PxRigidDynamic> actor(physics_.createRigidDynamic(pose));
            if (!actor) continue;
            actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
            output.actor.reset(actor.release());
        } else {
            output.actor.reset(physics_.createRigidStatic(pose));
        }
        if (!output.actor || !output.actor->attachShape(*shape)) {
            output.actor.reset();
            continue;
        }
        scene_->addActor(*output.actor);
        ++accepted;
    }
    const bool removed = ground_ || std::any_of(sceneShapes_.begin(), sceneShapes_.end(),
                                                [](const SceneShape& old) { return bool(old.actor); });
    sceneShapes_ = std::move(next);
    ground_.reset();
    if (removed) {
        for (int index : active_) if (actors_[index]) actors_[index]->wakeUp();
        for (const auto& group : groups_) if (group.actor) group.actor->wakeUp();
    }
    return accepted;
}

bool ParticleSolver::Step(float dt, int substeps) {
    if (!Ready() || !std::isfinite(dt) || dt < 0 || substeps <= 0) return false;
    if (dt == 0 || active_.empty()) return true;
    for (int step = 0; step < substeps; ++step) {
        scene_->simulate(dt / substeps);
        if (!scene_->fetchResults(true)) return false;
    }
    for (int index : active_) {
        if (!actors_[index]) continue;
        const auto p = actors_[index]->getGlobalPose().p;
        const auto v = actors_[index]->getLinearVelocity();
        if (!p.isFinite() || !v.isFinite()) return false;
        auto& output = particles_[index];
        output.position[0] = p.x;
        output.position[1] = p.y;
        output.position[2] = p.z;
        output.velocity = {v.x, v.y, v.z};
    }
    for (const auto& group : groups_) {
        if (!group.actor) continue;
        const auto pose = group.actor->getGlobalPose();
        const auto center = pose.transform(group.actor->getCMassLocalPose().p);
        const auto linear = group.actor->getLinearVelocity();
        const auto angular = group.actor->getAngularVelocity();
        if (!pose.isValid() || !linear.isFinite() || !angular.isFinite()) return false;
        for (std::size_t i = 0; i < group.indices.size(); ++i) {
            const auto position = pose.transform(group.rest[i]);
            const auto velocity = linear + angular.cross(position - center);
            if (!position.isFinite() || !velocity.isFinite()) return false;
            auto& output = particles_[group.indices[i]];
            output.position[0] = position.x;
            output.position[1] = position.y;
            output.position[2] = position.z;
            output.velocity = {velocity.x, velocity.y, velocity.z};
        }
    }
    return true;
}

bool ParticleSolver::GetPositions(float* positions, int count) const {
    if (count < 0 || static_cast<std::size_t>(count) > particles_.size() || (count && !positions)) return false;
    for (int i = 0; i < count; ++i) {
        std::copy_n(particles_[i].position.begin(), 4, positions + 4ull * i);
    }
    return true;
}

bool ParticleSolver::GetVelocities(float* velocities, int count) const {
    if (count < 0 || static_cast<std::size_t>(count) > particles_.size() || (count && !velocities)) return false;
    for (int i = 0; i < count; ++i) {
        std::copy_n(particles_[i].velocity.begin(), 3, velocities + 3ull * i);
    }
    return true;
}
}
