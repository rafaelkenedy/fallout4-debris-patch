#pragma once

// Plain-data ABI shared by the MinGW game adapter and MSVC PhysX bridge.
struct PhysXSceneShape {
    int flags;
    const void* records;
    int count;
    const float* position;
    const float* rotation;
    const void* meshKey;
    unsigned long long meshRevision;
    const float* vertices;
    int vertexCount;
    const int* triangles;
    int triangleCount;
    float scale;
};
