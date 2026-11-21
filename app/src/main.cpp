#include "Studio/Studio.hpp"

int main(int argc, char* argv[])
{
    const auto launch = StudioMain::prepareLaunch(argc, argv);
    if (const auto* exitCode = std::get_if<int>(&launch)) {
        return *exitCode;
    }
    return StudioMain::runStudio(std::get<StudioMain::LaunchOptions>(launch));
}
