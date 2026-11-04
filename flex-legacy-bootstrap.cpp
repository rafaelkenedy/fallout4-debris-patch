#include <windows.h>
#include <filesystem>
#include <cstdio>
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

void LogRuntime(const char* message) {
    OutputDebugStringA(message);
    if (logPath.empty()) return;
    if (auto* log = _wfopen(logPath.c_str(), L"a")) {
        fprintf(log, "%s\n", message);
        fclose(log);
    }
}

template<class Function>
bool Resolve(HMODULE module, const char* name, Function& output) {
    output = reinterpret_cast<Function>(GetProcAddress(module, name));
    return output != nullptr;
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
        const int result = InitializeLegacyRuntime(version, callback, device);
        LogRuntime(result == 0 ? "FleX D3D11 initialized." : "FleX D3D11 initialization failed.");
        return result;
    } catch (...) {
        return 1;
    }
}
