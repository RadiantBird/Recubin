#pragma once
#include <string>
#include <string_view>
#include <vector>
struct GLFWwindow;
namespace GuiAutomation {
// Syntax-only parser hook used by regression tests; does not affect the live queue.
void configureFromArgs(int argc, char** argv);
void start();
void beforeNewFrame();
void afterNewFrame();
void registerLastItem(std::string_view id);
void afterRender(GLFWwindow* window);
bool enabled();
// --ui-automation-hidden が指定されたとき真(ウィンドウを表示せずに起動する)。--ui-automation なしでは常に偽。
bool hiddenWindow();
}
