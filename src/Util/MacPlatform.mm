#ifdef __APPLE__

#include <Util/MacPlatform.hpp>
#import <Cocoa/Cocoa.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <cctype>
#include <functional>
#include <memory>
#include <sstream>

namespace {

std::string trimAsciiWhitespace(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

std::string normalizedExtension(const std::string& value) {
    std::string extension = trimAsciiWhitespace(value);
    while (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    return extension;
}

NSMutableArray<NSString*>* allowedExtensions(const std::vector<FileFilter>& filters, bool& unrestricted) {
    NSMutableArray<NSString*>* extensions = [NSMutableArray array];
    unrestricted = filters.empty();
    for (const FileFilter& filter : filters) {
        std::istringstream patterns(filter.spec);
        std::string pattern;
        while (std::getline(patterns, pattern, ';')) {
            pattern = trimAsciiWhitespace(pattern);
            if (pattern == "*.*" || pattern == "*") {
                unrestricted = true;
                return extensions;
            }
            if (pattern.rfind("*.", 0) == 0) pattern.erase(0, 2);
            else if (!pattern.empty() && pattern.front() == '.') pattern.erase(0, 1);
            if (pattern.empty() || pattern.find('*') != std::string::npos) continue;

            NSString* extension = [NSString stringWithUTF8String:pattern.c_str()];
            if (extension && ![extensions containsObject:extension]) [extensions addObject:extension];
        }
    }
    return extensions;
}

std::string fileSystemPath(NSString* path) {
    if (!path) return {};
    const char* bytes = [path fileSystemRepresentation];
    return bytes ? std::string(bytes) : std::string();
}

std::string runStringOperationOnMain(const std::function<std::string()>& operation) {
    if ([NSThread isMainThread]) return operation();

    auto result = std::make_shared<std::string>();
    dispatch_sync(dispatch_get_main_queue(), ^{
        *result = operation();
    });
    return *result;
}

void runVoidOperationOnMain(const std::function<void()>& operation) {
    if ([NSThread isMainThread]) {
        operation();
        return;
    }
    dispatch_sync(dispatch_get_main_queue(), ^{
        operation();
    });
}

void configureFileTypes(NSSavePanel* panel, const std::vector<FileFilter>& filters) {
    bool unrestricted = false;
    NSMutableArray<NSString*>* extensions = allowedExtensions(filters, unrestricted);
    if (!unrestricted && extensions.count > 0) {
        panel.allowedFileTypes = extensions;
        panel.allowsOtherFileTypes = NO;
    }
}

} // namespace

std::string MacPlatform::openFileDialog(const std::vector<FileFilter>& filters) {
    return runStringOperationOnMain([&filters]() -> std::string {
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = YES;
            panel.canChooseDirectories = NO;
            panel.allowsMultipleSelection = NO;
            configureFileTypes(panel, filters);
            if ([panel runModal] != NSModalResponseOK) return {};
            return fileSystemPath(panel.URL.path);
        }
    });
}

std::string MacPlatform::saveFileDialog(const std::vector<FileFilter>& filters,
                                        const std::string& defaultExt) {
    return runStringOperationOnMain([&filters, &defaultExt]() -> std::string {
        @autoreleasepool {
            NSSavePanel* panel = [NSSavePanel savePanel];
            configureFileTypes(panel, filters);
            panel.extensionHidden = NO;
            if ([panel runModal] != NSModalResponseOK) return {};

            NSString* path = panel.URL.path;
            std::string extension = normalizedExtension(defaultExt);
            if (!extension.empty() && path.pathExtension.length == 0) {
                NSString* defaultExtension = [NSString stringWithUTF8String:extension.c_str()];
                if (!defaultExtension) return {};
                path = [path stringByAppendingPathExtension:defaultExtension];
            }
            return fileSystemPath(path);
        }
    });
}

std::string MacPlatform::openFolderDialog() {
    return runStringOperationOnMain([]() -> std::string {
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = NO;
            panel.canChooseDirectories = YES;
            panel.allowsMultipleSelection = NO;
            if ([panel runModal] != NSModalResponseOK) return {};
            return fileSystemPath(panel.URL.path);
        }
    });
}

void MacPlatform::revealInFileManager(const std::string& path) {
    runVoidOperationOnMain([&path]() {
        @autoreleasepool {
            NSString* filePath = [NSString stringWithUTF8String:path.c_str()];
            if (!filePath) return;
            [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:filePath]];
        }
    });
}

ApplicationIconResult MacPlatform::setApplicationIcon(const std::string& path) {
    ApplicationIconResult result = ApplicationIconResult::Failed;
    runVoidOperationOnMain([&path, &result]() {
        @autoreleasepool {
            NSApplication* application = [NSApplication sharedApplication];
            if (path.empty()) {
                application.applicationIconImage = nil;
                result = ApplicationIconResult::Applied;
                return;
            }

            NSString* filePath = [NSString stringWithUTF8String:path.c_str()];
            if (!filePath) return;

            NSImage* image = [[[NSImage alloc] initWithContentsOfFile:filePath] autorelease];
            if (!image) return;

            application.applicationIconImage = image;
            result = ApplicationIconResult::Applied;
        }
    });
    return result;
}

void MacPlatform::setupConsoleUtf8() {}

void MacPlatform::setupDllSearchPath() {}

void* MacPlatform::loadDynamicLibrary(const std::string& name) {
    return dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* MacPlatform::getSymbol(void* handle, const std::string& symbolName) {
    return handle ? dlsym(handle, symbolName.c_str()) : nullptr;
}

void MacPlatform::freeDynamicLibrary(void* handle) {
    if (handle) dlclose(handle);
}

#endif // __APPLE__
