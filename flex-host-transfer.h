#pragma once

#include <cstring>
#include "NvFlex.h"

struct FlexBufferApi {
    decltype(&NvFlexAllocBuffer) allocate;
    decltype(&NvFlexFreeBuffer) free;
    decltype(&NvFlexMap) map;
    decltype(&NvFlexUnmap) unmap;
};

// One staging buffer per solver channel. Callers serialize access to a channel.
class FlexHostTransfer {
public:
    using Transfer = void (*)(NvFlexSolver*, NvFlexBuffer*, const NvFlexCopyDesc*);

    FlexHostTransfer(NvFlexLibrary* library, FlexBufferApi api, int stride)
        : library_(library), api_(api), stride_(stride) {}

    FlexHostTransfer(const FlexHostTransfer&) = delete;
    FlexHostTransfer& operator=(const FlexHostTransfer&) = delete;

    ~FlexHostTransfer() {
        if (buffer_) api_.free(buffer_);
    }

    bool Upload(NvFlexSolver* solver, const void* source, int count, Transfer set) {
        if (count == 0) return true;
        if (!solver || !set) return false;
        auto* buffer = Stage(source, count);
        if (!buffer) return false;
        const NvFlexCopyDesc copy{0, 0, count};
        set(solver, buffer, &copy);
        return true;
    }

    NvFlexBuffer* Stage(const void* source, int count) {
        if (!source || count <= 0 || !Reserve(count)) return nullptr;
        void* mapped = api_.map(buffer_, eNvFlexMapWait);
        if (!mapped) return nullptr;
        std::memcpy(mapped, source, Bytes(count));
        api_.unmap(buffer_);
        return buffer_;
    }

    bool Download(NvFlexSolver* solver, void* destination, int count, Transfer get) {
        if (count == 0) return true;
        if (!solver || !destination || !get || !Reserve(count)) return false;
        const NvFlexCopyDesc copy{0, 0, count};
        get(solver, buffer_, &copy);
        return Read(destination, count);
    }

    NvFlexBuffer* Prepare(int count) {
        return count > 0 && Reserve(count) ? buffer_ : nullptr;
    }

    bool Read(void* destination, int count) {
        if (!destination || count <= 0 || count > capacity_) return false;
        const void* mapped = api_.map(buffer_, eNvFlexMapWait);
        if (!mapped) return false;
        std::memcpy(destination, mapped, Bytes(count));
        api_.unmap(buffer_);
        return true;
    }

private:
    std::size_t Bytes(int count) const {
        return static_cast<std::size_t>(count) * stride_;
    }

    bool Reserve(int count) {
        if (count < 0 || stride_ <= 0 || !library_) return false;
        if (capacity_ >= count) return true;
        auto* next = api_.allocate(library_, count, stride_, eNvFlexBufferHost);
        if (!next) return false;
        if (buffer_) api_.free(buffer_);
        buffer_ = next;
        capacity_ = count;
        return true;
    }

    NvFlexLibrary* library_;
    FlexBufferApi api_;
    int stride_;
    NvFlexBuffer* buffer_ = nullptr;
    int capacity_ = 0;
};
