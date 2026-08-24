#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <optional>

struct GuiAutomationCommand {
    std::string name;
    std::vector<std::string> arguments;
};

std::optional<GuiAutomationCommand> parseGuiAutomationCommand(std::string_view command);

// GUI automation command syntax validation shared by runtime and RecubinTest.
// This function is pure: it does not inspect ImGui state or mutate a queue.
bool validateGuiAutomationCommand(std::string_view command);
