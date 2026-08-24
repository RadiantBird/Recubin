#pragma once
#include <string>
#include <string_view>
#include <vector>
struct GLFWwindow;
namespace GuiAutomation {
void configureFromArgs(int argc, char** argv);
void start();
void beforeNewFrame();
void afterNewFrame();
void registerLastItem(std::string_view id);
void afterRender(GLFWwindow* window);
bool enabled();
}
