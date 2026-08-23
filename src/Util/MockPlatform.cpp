#include <Util/MockPlatform.hpp>
#include <filesystem>

std::string MockPlatform::openFileDialog(const std::vector<FileFilter>&) {
    return {};
}

std::string MockPlatform::saveFileDialog(const std::vector<FileFilter>&, const std::string&) {
    return {};
}

std::string MockPlatform::openFolderDialog() {
    return {};
}

void MockPlatform::revealInFileManager(const std::string&) {}

ApplicationIconResult MockPlatform::setApplicationIcon(const std::string&) {
    return ApplicationIconResult::Unsupported;
}

void MockPlatform::setupConsoleUtf8() {}

void MockPlatform::setupDllSearchPath() {}

void* MockPlatform::loadDynamicLibrary(const std::string&) {
    return nullptr;
}

void* MockPlatform::getSymbol(void*, const std::string&) {
    return nullptr;
}

void MockPlatform::freeDynamicLibrary(void*) {}

std::unique_ptr<IChildProcess> MockPlatform::launchChildProcess(
    const ChildProcessLaunchOptions&) {
    return nullptr;
}

std::filesystem::path MockPlatform::userDataRoot() const {
    auto path = std::filesystem::temp_directory_path() / "Recubin";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}
