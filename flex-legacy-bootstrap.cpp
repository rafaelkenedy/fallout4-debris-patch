#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <atomic>
#include "flex-legacy-asset.h"
#include "flex-legacy-mesh.h"

#define flexInit InitializeLegacyRuntime
#include "flex-legacy-particles.cpp"
#undef flexInit

namespace {
LegacySolverBackend solverApi{};
LegacyMeshApi meshApi{};
INIT_ONCE initialization = INIT_ONCE_STATIC_INIT;
std::filesystem::path logPath;
std::filesystem::path capturePath;
std::atomic_flag captureAttempted = ATOMIC_FLAG_INIT;
std::filesystem::path physxPath;
std::atomic_flag physxAttempted = ATOMIC_FLAG_INIT;
void* (*createExclusiveRuntime)(void (*)(const char*));

void LogRuntime(const char* message) {
    OutputDebugStringA(message);
    if (logPath.empty()) return;
    if (auto* log = _wfopen(logPath.c_str(), L"a")) {
        fprintf(log, "%s\n", message);
        fclose(log);
    }
}

void CaptureConvex(const unsigned char* planes, int count) {
    if (count > 256 || captureAttempted.test_and_set()) return;
    // Each record is four little-endian IEEE-754 floats, with no header.
    const auto file = CreateFileW(capturePath.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const auto size = static_cast<DWORD>(count * 16);
    DWORD written = 0;
    const bool complete = WriteFile(file, planes, size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (complete) {
        LogRuntime("Captured legacy convex planes in flex-convex-planes.bin.");
    } else {
        DeleteFileW(capturePath.c_str());
        LogRuntime("Could not write the legacy convex plane capture.");
    }
}

template<class Function>
bool Resolve(HMODULE module, const char* name, Function& output) {
    output = reinterpret_cast<Function>(GetProcAddress(module, name));
    return output != nullptr;
}

HMODULE LoadPhysXBridge() {
    LogRuntime("Loading the PhysX bridge DLL.");
    constexpr DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    const auto directory = physxPath.parent_path();
    for (const auto* name : {"PhysXFoundation_64.dll", "PhysXCommon_64.dll", "PhysXGpu_64.dll"}) {
        if (!LoadLibraryExW((directory / name).c_str(), nullptr, flags)) {
            const auto error = GetLastError();
            char message[160];
            std::snprintf(message, sizeof(message), "PhysX probe could not preload %s (Win32 %lu).",
                          name, static_cast<unsigned long>(error));
            LogRuntime(message);
            return nullptr;
        }
    }
    LogRuntime("PhysX Foundation, Common and GPU DLLs preloaded.");
    const auto module = LoadLibraryExW(physxPath.c_str(), nullptr, flags);
    if (!module) {
        LogRuntime("PhysX probe could not load the bridge DLL.");
    }
    return module;
}

void* ProbePhysXConvex(const unsigned char* planes, int count, const float* position, const float* rotation) {
    if (count > 256 || physxAttempted.test_and_set()) return nullptr;
    const auto module = LoadPhysXBridge();
    if (!module) return nullptr;
    void* (*create)(void (*)(const char*)) = nullptr;
    void (*destroy)(void*) = nullptr;
    int (*submit)(void*, const void*, int, const float*, const float*) = nullptr;
    decltype(stepPhysXProbe) step = nullptr;
    decltype(uploadPhysXRigids) uploadRigids = nullptr;
    decltype(stepPhysXRigids) stepRigids = nullptr;
    decltype(setPhysXSceneShapes) setSceneShapes = nullptr;
    if (Resolve(module, "PhysXDebrisCreate", create) &&
        Resolve(module, "PhysXDebrisDestroy", destroy) &&
        Resolve(module, "PhysXDebrisSubmitConvex", submit) &&
        Resolve(module, "PhysXDebrisStep", step) &&
        (!probePhysXRigids || (Resolve(module, "PhysXDebrisUploadRigids", uploadRigids) &&
                              Resolve(module, "PhysXDebrisStepFrame", stepRigids))) &&
        (!usePhysXSceneShapes || Resolve(module, "PhysXDebrisSetSceneShapes", setSceneShapes))) {
        LogRuntime("PhysX probe: creating CUDA scene.");
        if (auto* instance = create(LogRuntime)) {
            LogRuntime("PhysX probe: submitting live convex.");
            char message[120];
            const bool accepted = submit(instance, planes, count, position, rotation) != 0;
            std::snprintf(message, sizeof(message), "PhysX GPU probe: %s %d live convex planes.",
                          accepted ? "accepted" : "rejected", count);
            LogRuntime(message);
            if (accepted) {
                destroyPhysXProbe = destroy;
                stepPhysXProbe = step;
                uploadPhysXRigids = uploadRigids;
                stepPhysXRigids = stepRigids;
                setPhysXSceneShapes = setSceneShapes;
                return instance;
            }
            destroy(instance);
        } else {
            LogRuntime("PhysX probe could not initialize its CUDA scene.");
        }
    } else {
        LogRuntime("PhysX probe could not resolve the bridge exports.");
    }
    // Keep the module and CUDA dependencies loaded until process exit.
    return nullptr;
}

BOOL CALLBACK BindRuntime(PINIT_ONCE, PVOID, PVOID*) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&initialization), &self)) return FALSE;
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
    if (!length || length == std::size(path)) return FALSE;
    const auto directory = std::filesystem::path(path).parent_path();
    logPath = directory / L"flex-compat.log";
    traceLegacyCalls =
        GetFileAttributesW((directory / L"flex-trace-abi").c_str()) != INVALID_FILE_ATTRIBUTES;
    if (GetFileAttributesW((directory / L"physx-exclusive").c_str()) != INVALID_FILE_ATTRIBUTES) {
        physxPath = directory / L"physx-debris-runtime" / L"physx-debris-bridge.dll";
        const auto module = LoadPhysXBridge();
        const PhysXExclusiveApi* (*api)() = nullptr;
        if (!module || !Resolve(module, "PhysXDebrisExclusiveApiV1", api) ||
            !Resolve(module, "PhysXDebrisCreate", createExclusiveRuntime) ||
            !Resolve(module, "PhysXDebrisDestroy", destroyExclusiveRuntime)) return FALSE;
        exclusiveApi = api();
        if (!exclusiveApi) return FALSE;
        EnableLegacyMeshCapture();
        EnableHostRigidAssets();
        BindLegacyRuntimeLog(LogRuntime);
        return TRUE;
    }
    if (GetFileAttributesW((directory / L"flex-probe-physx").c_str()) != INVALID_FILE_ATTRIBUTES) {
        physxPath = directory / L"physx-debris-runtime" / L"physx-debris-bridge.dll";
        probePhysXConvex = ProbePhysXConvex;
        probePhysXRigids =
            GetFileAttributesW((directory / L"flex-probe-physx-rigids").c_str()) != INVALID_FILE_ATTRIBUTES;
        usePhysXRigidOutput = probePhysXRigids &&
            GetFileAttributesW((directory / L"flex-use-physx-rigids").c_str()) != INVALID_FILE_ATTRIBUTES;
        usePhysXSceneShapes = probePhysXRigids &&
            GetFileAttributesW((directory / L"flex-physx-scene-shapes").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (usePhysXSceneShapes) EnableLegacyMeshCapture();
        usePhysXParticleOutput =
            GetFileAttributesW((directory / L"flex-use-physx-particle").c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    if (GetFileAttributesW((directory / L"flex-capture-convex").c_str()) != INVALID_FILE_ATTRIBUTES) {
        capturePath = directory / L"flex-convex-planes.bin";
        captureConvex = CaptureConvex;
    }
    const auto runtime = directory / L"flex-compat-runtime";
    constexpr DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    const auto core = LoadLibraryExW((runtime / L"NvFlexReleaseD3D_x64.dll").c_str(), nullptr, flags);
    if (!core) {
        LogRuntime("Could not load NvFlexReleaseD3D_x64.dll or its dependencies.");
        return FALSE;
    }
    const auto extension = LoadLibraryExW((runtime / L"NvFlexExtReleaseD3D_x64.dll").c_str(),
                                          nullptr, flags);
    decltype(&NvFlexExtCreateRigidFromMesh) createAsset = nullptr;
    decltype(&NvFlexExtDestroyAsset) destroyAsset = nullptr;
#define RESOLVE(member, name) Resolve(core, #name, member)
    const bool ready = extension &&
        RESOLVE(solverApi.init, NvFlexInit) &&
        RESOLVE(solverApi.shutdown, NvFlexShutdown) &&
        RESOLVE(solverApi.buffers.allocate, NvFlexAllocBuffer) &&
        RESOLVE(solverApi.buffers.free, NvFlexFreeBuffer) &&
        RESOLVE(solverApi.buffers.map, NvFlexMap) &&
        RESOLVE(solverApi.buffers.unmap, NvFlexUnmap) &&
        RESOLVE(solverApi.defaults, NvFlexSetSolverDescDefaults) &&
        RESOLVE(solverApi.create, NvFlexCreateSolver) &&
        RESOLVE(solverApi.destroy, NvFlexDestroySolver) &&
        RESOLVE(solverApi.setPositions, NvFlexSetParticles) &&
        RESOLVE(solverApi.getPositions, NvFlexGetParticles) &&
        RESOLVE(solverApi.setVelocities, NvFlexSetVelocities) &&
        RESOLVE(solverApi.setActive, NvFlexSetActive) &&
        RESOLVE(solverApi.setActiveCount, NvFlexSetActiveCount) &&
        RESOLVE(solverApi.update, NvFlexUpdateSolver) &&
        RESOLVE(solverApi.setParams, NvFlexSetParams) &&
        RESOLVE(solverApi.getVelocities, NvFlexGetVelocities) &&
        RESOLVE(solverApi.setPhases, NvFlexSetPhases) &&
        RESOLVE(solverApi.getBounds, NvFlexGetBounds) &&
        RESOLVE(solverApi.flush, NvFlexFlush) &&
        RESOLVE(solverApi.wait, NvFlexWait) &&
        RESOLVE(solverApi.setRigids, NvFlexSetRigids) &&
        RESOLVE(solverApi.getRigids, NvFlexGetRigids) &&
        RESOLVE(solverApi.setShapes, NvFlexSetShapes) &&
        RESOLVE(solverApi.createConvexMesh, NvFlexCreateConvexMesh) &&
        RESOLVE(solverApi.updateConvexMesh, NvFlexUpdateConvexMesh) &&
        RESOLVE(solverApi.destroyConvexMesh, NvFlexDestroyConvexMesh) &&
        RESOLVE(meshApi.create, NvFlexCreateTriangleMesh) &&
        RESOLVE(meshApi.update, NvFlexUpdateTriangleMesh) &&
        RESOLVE(meshApi.destroy, NvFlexDestroyTriangleMesh) &&
        Resolve(extension, "NvFlexExtCreateRigidFromMesh", createAsset) &&
        Resolve(extension, "NvFlexExtDestroyAsset", destroyAsset);
#undef RESOLVE
    if (!ready) {
        LogRuntime("Could not load the FleX D3D11 extension or resolve an SDK entry point.");
        if (extension) FreeLibrary(extension);
        FreeLibrary(core);
        return FALSE;
    }
    meshApi.buffers = solverApi.buffers;
    BindLegacySolverBackend(&solverApi);
    BindLegacyMeshApi(&meshApi);
    BindLegacyAssetBackend(createAsset, destroyAsset);
    BindLegacyRuntimeLog(LogRuntime);
    LogRuntime("FleX D3D11 entry points resolved.");
    return TRUE;
}
}

extern "C" int flexInit(int version, LegacyFlexError callback, int device) {
    if (version != 90) return 1;
    try {
        if (!InitOnceExecuteOnce(&initialization, BindRuntime, nullptr, nullptr)) return 1;
        if (exclusiveApi) {
            if (exclusiveRuntime || device < -1) return 1;
            exclusiveRuntime = createExclusiveRuntime(LogRuntime);
            if (!exclusiveRuntime) return 1;
            LogRuntime("PhysX exclusive initialized; FleX runtime loading bypassed.");
            return 0;
        }
        const int result = InitializeLegacyRuntime(version, callback, device);
        LogRuntime(result == 0 ? "FleX D3D11 initialized." : "FleX D3D11 initialization failed.");
        return result;
    } catch (...) {
        return 1;
    }
}
