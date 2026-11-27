#pragma once

// Recubin native Process SDK.
// A Process DLL exports `start()` and may export `stop()` and `bindHost()`.
#include <recubin_process_api.h>

namespace recubin {
using Instance = process::InstanceRef;
using System = process::System;
using process::GetSystem;
namespace task = process::task;
}
