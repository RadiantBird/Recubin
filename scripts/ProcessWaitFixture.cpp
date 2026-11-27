#include <recubin_process.h>
#include <recubin.h>

#include <cstdint>
#include <stdexcept>

namespace {
void onTypedHeartbeat(double) {}
void onTypedTouch(recubin::InstanceRef<recubin::BaseCube> other) {
    if (!other.valid()) throw std::runtime_error("Touched supplied an invalid InstanceRef");
    other.Name.Set("TouchedCallbackPassed");
}

[[maybe_unused]] void compileSdkApiSurface() {
    recubin::InstanceRef<recubin::Cube> cube = recubin::Instance::New<recubin::Cube>();
    cube.Name.Set("ProcessFixture");
    recubin::Color4 color = cube.Color.Get();
    cube.Color.Set(color);
    std::optional<recubin::InstanceRef<recubin::process::Instance>> parent = cube.Parent.Get();
    std::optional<recubin::InstanceRef<recubin::process::Workspace>> workspace = cube.Parent.GetAs<recubin::process::Workspace>();
    (void)parent;
    (void)workspace;
}

const RCBNProcessHostApi* g_host = nullptr;
std::uint64_t g_connection = 0;
volatile std::uint32_t g_heartbeatCount = 0;

void onHeartbeat(void*, double) {
    ++g_heartbeatCount;
    if (g_heartbeatCount >= 3 && g_connection != 0 && g_host &&
        g_host->disconnectHeartbeat) {
        g_host->disconnectHeartbeat(g_host->userdata, g_connection);
        g_connection = 0;
    }
}

bool exerciseSdkApi() noexcept {
    try {
        recubin::InstanceRef<recubin::Process> self = recubin::GetSelfProcess();
        std::optional<recubin::InstanceRef<recubin::System>> system = recubin::GetSystem();
        if (!self.valid() || !system || !system->EnableExecutingProcess.Get()) return false;
        recubin::Connection connection = system->Heartbeat.Connect([](double deltaSeconds) {
            if (deltaSeconds < 0.0) return;
        });
        if (!connection.valid()) return false;
        recubin::Connection functionPointerConnection =
            system->Heartbeat.Connect(&onTypedHeartbeat);
        if (!functionPointerConnection.valid() || !functionPointerConnection.Disconnect()) return false;
        recubin::InstanceRef<recubin::Cube> cube = recubin::Instance::New<recubin::Cube>();
        cube.Name.Set("ProcessFixtureCube");
        cube.Color.Set(cube.Color.Get());
        cube.CFrame.Set(CFrame(Vector3(1.0f, 2.0f, 3.0f)));
        recubin::Connection touched = cube.Touched.Connect(&onTypedTouch);
        if (!touched.valid()) return false;
        recubin::Connection disconnectedTouch = cube.Touched.Connect(&onTypedTouch);
        if (!disconnectedTouch.valid() || !disconnectedTouch.Disconnect()) return false;
        recubin::Connection touchEnded = cube.TouchEnded.Connect(
            [](recubin::InstanceRef<recubin::BaseCube> other) {
                if (!other.valid()) throw std::runtime_error("TouchEnded supplied an invalid InstanceRef");
            });
        if (!touchEnded.valid()) return false;
        cube.Parent.Set(*system);
        std::optional<recubin::InstanceRef<recubin::System>> parentSystem = cube.Parent.GetAs<recubin::System>();
        std::optional<recubin::InstanceRef<recubin::Cube>> found =
            system->FindChildWithClass<recubin::Cube>("ProcessFixtureCube");
        std::optional<recubin::InstanceRef<recubin::Instance>> foundByName =
            system->FindChild<recubin::Instance>("ProcessFixtureCube");
        std::optional<recubin::InstanceRef<recubin::BaseCube>> foundAsBaseClass =
            system->FindChildWithClass<recubin::BaseCube>("ProcessFixtureCube");
        return parentSystem && found && found->Name.Get() == "ProcessFixtureCube" &&
            foundByName && foundByName->isA("Cube") && !foundAsBaseClass &&
            found->Position.Get() == Vector3(1.0f, 2.0f, 3.0f) &&
            found->CFrame.Get().Position == Vector3(1.0f, 2.0f, 3.0f);
    } catch (...) {
        return false;
    }
}
}

RCBN_PROCESS_BIND_HOST {
    g_host = api;
    recubin::process::InstallHostApi(api);
}

RCBN_PROCESS_START {
    if (!g_host || !g_host->wait) return;
    RCBNProcessInstanceHandle selfHandle{};
    if (g_host->getSelfProcess && g_host->getSelfProcess(g_host->userdata, &selfHandle)) {
        if (!exerciseSdkApi() && g_host->reportError)
            g_host->reportError(g_host->userdata, "Process SDK typed API regression failed");
    }
    g_heartbeatCount = 0;
    g_connection = 0;
    if (g_host->connectHeartbeat) {
        g_host->connectHeartbeat(g_host->userdata, &onHeartbeat, nullptr, &g_connection);
    }
}

RCBN_PROCESS_STOP {
    if (g_host && g_connection != 0 && g_host->disconnectHeartbeat) {
        g_host->disconnectHeartbeat(g_host->userdata, g_connection);
    }
    g_connection = 0;
    g_host = nullptr;
}
