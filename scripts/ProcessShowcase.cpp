#include <recubin.h>

#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace {
constexpr float PI = 3.14159265358979323846f;
std::array<recubin::InstanceRef<recubin::Cube>, 12> g_cubes;
std::optional<recubin::Connection> g_heartbeat;
double g_elapsed = 0.0;

void animate(double deltaSeconds) {
    g_elapsed += deltaSeconds;
    for (std::size_t index = 0; index < g_cubes.size(); ++index) {
        const float phase = static_cast<float>(g_elapsed * 1.8 + index * (2.0 * PI / g_cubes.size()));
        const float radius = 12.0f + 2.0f * std::sin(static_cast<float>(g_elapsed * 1.4));
        const float x = radius * std::cos(phase);
        const float z = radius * std::sin(phase);
        const float y = 4.0f + 2.0f * std::sin(phase * 2.0f);
        g_cubes[index].Position.Set(recubin::Vector3(x, y, z));
        g_cubes[index].Color.Set(recubin::Color4(
            0.5f + 0.5f * std::sin(phase),
            0.5f + 0.5f * std::sin(phase + 2.0f),
            0.5f + 0.5f * std::sin(phase + 4.0f), 1.0f));
    }
}
}

RCBN_PROCESS_BIND_HOST {
    recubin::process::InstallHostApi(api);
}

RCBN_PROCESS_START {
    std::optional<recubin::InstanceRef<recubin::System>> system = recubin::GetSystem();
    if (!system) throw std::runtime_error("System was not available");
    std::optional<recubin::InstanceRef<recubin::Workspace>> workspace =
        system->FindChildWithClass<recubin::Workspace>("Workspace");
    if (!workspace) throw std::runtime_error("Workspace was not found");

    constexpr std::array<const char*, 12> names = {
        "NEON 01", "NEON 02", "NEON 03", "NEON 04", "NEON 05", "NEON 06",
        "NEON 07", "NEON 08", "NEON 09", "NEON 10", "NEON 11", "NEON 12"};
    for (std::size_t index = 0; index < g_cubes.size(); ++index) {
        g_cubes[index] = recubin::Instance::New<recubin::Cube>();
        g_cubes[index].Name.Set(names[index]);
        g_cubes[index].Size.Set(recubin::Vector3(3.0f, 3.0f, 3.0f));
        g_cubes[index].Anchored.Set(true);
        g_cubes[index].CanCollide.Set(false);
        g_cubes[index].Parent.Set(*workspace);
    }
    g_elapsed = 0.0;
    g_heartbeat.emplace(system->Heartbeat.Connect(&animate));
    while (!recubin::task::shouldStop()) recubin::task::wait(0.016);
}

RCBN_PROCESS_STOP {
    if (g_heartbeat) {
        g_heartbeat->Disconnect();
        g_heartbeat.reset();
    }
    for (recubin::InstanceRef<recubin::Cube>& cube : g_cubes) {
        if (cube.valid()) cube.Parent.Clear();
        cube = {};
    }
}
