#pragma once

struct LegacyParticleSolver;
struct LegacyFlexContainer;
struct LegacyForceField;

extern "C" {
LegacyFlexContainer* flexExtCreateContainer(LegacyParticleSolver*, int capacity);
void flexExtDestroyContainer(LegacyFlexContainer*);
void flexExtGetParticleData(LegacyFlexContainer*, float** positions, float** velocities,
                            int** phases, float** normals, int** activeIndices);
void flexExtSetForceFields(LegacyFlexContainer*, const LegacyForceField*, int count, int memory);
}
