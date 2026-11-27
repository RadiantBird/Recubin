#pragma once

// Recubin native Process SDK.
// Include this header in the source selected by a Process Instance.
//
// A DLL exports `void start()`. Export `void stop()` only when explicit
// cleanup is needed. The DLL uses the engine-owned Instance tree through
// non-owning, generation-checked InstanceRef<T> handles.
//
// Example:
//   RCBN_PROCESS_START {
//       recubin::InstanceRef<recubin::Cube> cube =
//           recubin::Instance::New<recubin::Cube>();
//       cube.Name.Set("MovingCube");
//       std::optional<recubin::InstanceRef<recubin::System>> system =
//           recubin::GetSystem();
//       if (!system) return;
//       cube.Parent.Set(*system);
//       cube.Color.Set(recubin::Color4(1, 0, 0, 1));
//       recubin::Connection update =
//           system->Heartbeat.Connect([](double deltaSeconds) {});
//       while (!recubin::task::shouldStop()) recubin::task::wait(0.016);
//   }
//
// Property access uses `.Get()` / `.Set()`. `FindChild<T>` returns an optional
// typed reference and assumes the caller's requested cast is correct.
// `FindChildWithClass<T>` returns an optional only for an exact class match.
// `Parent.Get<T>()` returns an optional reference; `Parent.Set(ref)` reparents.
// Touch events use `.Touched.Connect(function)` and `.TouchEnded.Connect(function)`.
#include <recubin_process_api.h>

namespace recubin {
using Instance = process::Instance;
template<class T> using InstanceRef = process::InstanceRef<T>;
using System = process::System;
using Process = process::Process;
using Workspace = process::Workspace;
using BaseCube = process::BaseCube;
using Cube = process::Cube;
using ::Color4;
using ::Vector3;
using ::CFrame;
using Connection = process::Connection;
using process::GetSystem;
using process::GetSelfProcess;
namespace task = process::task;
}
