#pragma once

#include "physx-scene-shapes.h"

struct PhysXExclusiveApi {
    void* (*create)(void*, int);
    void (*destroy)(void*);
    int (*parameters)(void*, const float*, float);
    int (*positions)(void*, const float*, int);
    int (*velocities)(void*, const float*, int);
    int (*active)(void*, const int*, int);
    int (*rigids)(void*, const int*, const int*, const float*, const float*, const float*, int, int);
    int (*shapes)(void*, const PhysXSceneShape*, int);
    int (*step)(void*, float, int);
    int (*getPositions)(void*, float*, int);
    int (*getVelocities)(void*, float*, int);
    int (*getRigids)(void*, float*, float*, int);
};
