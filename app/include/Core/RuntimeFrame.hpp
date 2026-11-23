#pragma once

class LuauEngine;
class Physics;
class User;
class Workspace;
class System;

namespace RuntimeFrame {

// Shared simulation ordering used by the public runtime and game executable.
bool simulateScripts(LuauEngine& luau, Workspace& workspace,
                     float deltaSeconds, Physics* physics);

bool processInput(User& user, Workspace& workspace, System& system,
                  float deltaSeconds, bool viewportFocused,
                  bool viewportHovered, bool gameplayInput,
                  bool wantsTextInput);

void updateTerrainForUser(User& user, Workspace& workspace, bool cpuTerrainGeometry);

void simulateAfterInput(User& user, Workspace& workspace, float deltaSeconds,
                        Physics* physics, bool cpuTerrainGeometry = false);

} // namespace RuntimeFrame
