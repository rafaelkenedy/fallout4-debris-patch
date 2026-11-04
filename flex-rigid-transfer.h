#pragma once

#include <limits>
#include "flex-triangle-transfer.h"

struct FlexQuaternion { float x, y, z, w; };

class FlexRigidTransfer {
public:
    FlexRigidTransfer(NvFlexLibrary* library, FlexBufferApi api,
                      decltype(&NvFlexSetRigids) set, decltype(&NvFlexGetRigids) get)
        : set_(set), get_(get), offsets_(library, api, sizeof(int)),
          indices_(library, api, sizeof(int)), rest_(library, api, sizeof(FlexPoint)),
          normals_(library, api, sizeof(float) * 4),
          stiffness_(library, api, sizeof(float)), rotations_(library, api, sizeof(FlexQuaternion)),
          translations_(library, api, sizeof(FlexPoint)) {}

    bool Upload(NvFlexSolver* solver, const int* offsets, const int* indices,
                const FlexPoint* rest, const float* stiffness,
                const FlexQuaternion* rotations, const FlexPoint* translations,
                int bodyCount, int indexCount, const float* normals = nullptr) {
        if (!solver || !offsets || bodyCount <= 0 ||
            bodyCount == std::numeric_limits<int>::max() || indexCount <= 0 ||
            offsets[0] != 0 || offsets[bodyCount] != indexCount) return false;
        for (int i = 0; i < bodyCount; ++i) {
            if (offsets[i] >= offsets[i + 1]) return false;
        }
        auto* bodyOffsets = offsets_.Stage(offsets, bodyCount + 1);
        auto* bodyIndices = indices_.Stage(indices, indexCount);
        auto* localPositions = rest_.Stage(rest, indexCount);
        auto* coefficients = stiffness_.Stage(stiffness, bodyCount);
        auto* orientations = rotations_.Stage(rotations, bodyCount);
        auto* centers = translations_.Stage(translations, bodyCount);
        auto* localNormals = normals ? normals_.Stage(normals, indexCount) : nullptr;
        if (!bodyOffsets || !bodyIndices || !localPositions || !coefficients ||
            !orientations || !centers || (normals && !localNormals)) return false;
        set_(solver, bodyOffsets, bodyIndices, localPositions, localNormals, coefficients,
             nullptr, nullptr, orientations, centers, bodyCount, indexCount);
        count_ = bodyCount;
        return true;
    }

    bool Download(NvFlexSolver* solver, FlexQuaternion* rotations, FlexPoint* translations) {
        if (!solver) return false;
        if (!count_) return true;
        get_(solver, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
             rotations ? rotations_.Prepare(count_) : nullptr,
             translations ? translations_.Prepare(count_) : nullptr);
        return (!rotations || rotations_.Read(rotations, count_)) &&
               (!translations || translations_.Read(translations, count_));
    }

    void Clear(NvFlexSolver* solver) {
        set_(solver, nullptr, nullptr, nullptr, nullptr, nullptr,
             nullptr, nullptr, nullptr, nullptr, 0, 0);
        count_ = 0;
    }

private:
    decltype(&NvFlexSetRigids) set_;
    decltype(&NvFlexGetRigids) get_;
    FlexHostTransfer offsets_, indices_, rest_, normals_, stiffness_, rotations_, translations_;
    int count_ = 0;
};
