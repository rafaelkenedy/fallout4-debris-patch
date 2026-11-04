#pragma once

#include <cstddef>
#include <cstring>
#include "NvFlex.h"

struct LegacyFlexParams {
    int iterations;
    float gravity[3];
    float radius;
    float solidRestDistance;
    float fluidRestDistance;
    float dynamicFriction;
    float staticFriction;
    float particleFriction;
    float restitution;
    float adhesion;
    float sleepThreshold;
    float maxSpeed;
    unsigned char unmapped34[4];
    float dissipation;
    float damping;
    unsigned char unmapped44[0xb0 - 0x44];
    float collisionDistance;
    float particleCollisionMargin;
    float shapeCollisionMargin;
    float planes[8][4];
    int numPlanes;
    int relaxationMode;
    float relaxationFactor;
};

static_assert(sizeof(LegacyFlexParams) == 0x148);
static_assert(offsetof(LegacyFlexParams, dynamicFriction) == 0x1c);
static_assert(offsetof(LegacyFlexParams, sleepThreshold) == 0x30);
static_assert(offsetof(LegacyFlexParams, maxSpeed) == 0x34);
static_assert(offsetof(LegacyFlexParams, damping) == 0x40);
static_assert(offsetof(LegacyFlexParams, collisionDistance) == 0xb0);
static_assert(offsetof(LegacyFlexParams, planes) == 0xbc);
static_assert(offsetof(LegacyFlexParams, numPlanes) == 0x13c);
