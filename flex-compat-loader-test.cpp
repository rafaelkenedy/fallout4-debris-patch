#include <windows.h>
#include <cstdio>
#include <filesystem>
#include "flex-legacy-container.h"
#include "flex-legacy-particles.h"

static void Report(int level, const char* message, const char*, int) {
    fprintf(stderr, "FleX [%d] %s\n", level, message);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto stage = std::filesystem::absolute(argv[1]);
    constexpr DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    auto core = LoadLibraryExW((stage / L"flexRelease_x64.dll").c_str(), nullptr, flags);
    auto ext = LoadLibraryExW((stage / L"flexExtRelease_x64.dll").c_str(), nullptr, flags);
    if (!core || !ext) {
        fprintf(stderr, "Could not load compatibility DLLs: %lu\n", GetLastError());
        return 1;
    }
#define LOAD(module, name) auto name = reinterpret_cast<decltype(&::name)>(GetProcAddress(module, #name)); \
    if (!name) { fprintf(stderr, "Missing %s\n", #name); return 1; }
    LOAD(core, flexInit);
    LOAD(core, flexShutdown);
    LOAD(core, flexCreateSolver);
    LOAD(core, flexDestroySolver);
    LOAD(core, flexSetParams);
    LOAD(core, flexSetParticles);
    LOAD(core, flexGetParticles);
    LOAD(core, flexSetVelocities);
    LOAD(core, flexSetPhases);
    LOAD(core, flexSetActive);
    LOAD(core, flexUpdateSolver);
    LOAD(ext, flexExtCreateContainer);
    LOAD(ext, flexExtGetParticleData);
    LOAD(ext, flexExtDestroyContainer);
#undef LOAD
    if (flexInit(90, Report, -1) != 0) return 1;
    constexpr int capacity = 32768;
    auto* solver = flexCreateSolver(capacity, 0, 64);
    if (!solver) return 1;
    auto* container = flexExtCreateContainer(solver, capacity);
    if (!container) return 1;
    float* particles = nullptr;
    float* velocities = nullptr;
    int* phases = nullptr;
    int* indices = nullptr;
    flexExtGetParticleData(container, &particles, &velocities, &phases, nullptr, &indices);
    if (!particles || !velocities || !phases || !indices) return 1;
    LegacyFlexParams params{};
    params.iterations = 3;
    params.gravity[1] = -9.8f;
    params.radius = params.solidRestDistance = .15f;
    params.relaxationFactor = 1;
    flexSetParams(solver, &params);
    particles[1] = particles[3] = 1;
    flexSetParticles(solver, particles, 1, 0);
    flexSetVelocities(solver, velocities, 1, 0);
    flexSetPhases(solver, phases, 1, 0);
    flexSetActive(solver, indices, 1, 0);
    for (int i = 0; i < 60; ++i) flexUpdateSolver(solver, 1.0f / 60, 1, nullptr);
    flexGetParticles(solver, particles, 1, 0);
    const float y = particles[1];
    printf("DLL loader test after 60 steps: y=%.6f\n", y);
    flexExtDestroyContainer(container);
    flexDestroySolver(solver);
    flexShutdown();
    return (y < -3.97f && y > -3.99f) ? 0 : 1;
}
