#include <memory>
#include <new>
#include <cmath>
#include "flex-legacy-container.h"
#include "flex-legacy-particles.h"

struct LegacyFlexContainer {
    LegacyFlexContainer(LegacyParticleSolver* state, std::size_t count)
        : solver(state), positions(new float[count * 4]{}), velocities(new float[count * 3]{}),
          phases(new int[count]{}), normals(new float[count * 4]{}),
          activeIndices(new int[count]{}) {}

    LegacyParticleSolver* solver;
    std::unique_ptr<float[]> positions;
    std::unique_ptr<float[]> velocities;
    std::unique_ptr<int[]> phases;
    std::unique_ptr<float[]> normals;
    std::unique_ptr<int[]> activeIndices;
};

LegacyFlexContainer* flexExtCreateContainer(LegacyParticleSolver* solver, int capacity) {
    if (!solver || capacity <= 0 || capacity > solver->capacity) return nullptr;
    try {
        return new LegacyFlexContainer(solver, static_cast<std::size_t>(capacity));
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void flexExtSetForceFields(LegacyFlexContainer* container, const LegacyForceField* fields,
                           int count, int memory) {
    if (!container) return;
    auto& state = *container->solver;
    if (state.failed) return;
    state.lastOperation = __func__;
    if (count == 0) {
        state.forceFields.clear();
        return;
    }
    if (!fields || count < 0 || (memory != 0 && memory != 2)) {
        state.failed = true;
        return;
    }
    for (int i = 0; i < count; ++i) {
        const auto& field = fields[i];
        if (!std::isfinite(field.radius) || field.radius <= 0 ||
            !std::isfinite(field.minStrength) || !std::isfinite(field.maxStrength) ||
            field.mode < 0 || field.mode > 2) {
            state.failed = true;
            return;
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(field.position[axis]) || !std::isfinite(field.direction[axis])) {
                state.failed = true;
                return;
            }
        }
    }
    try {
        state.forceFields.assign(fields, fields + count);
    } catch (const std::bad_alloc&) {
        state.failed = true;
    }
}

void flexExtDestroyContainer(LegacyFlexContainer* container) {
    delete container;
}

void flexExtGetParticleData(LegacyFlexContainer* container, float** positions,
                            float** velocities, int** phases, float** normals,
                            int** activeIndices) {
    if (!container) return;
    if (positions) *positions = container->positions.get();
    if (velocities) *velocities = container->velocities.get();
    if (phases) *phases = container->phases.get();
    if (normals) *normals = container->normals.get();
    if (activeIndices) *activeIndices = container->activeIndices.get();
}
