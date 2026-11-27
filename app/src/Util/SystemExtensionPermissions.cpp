#include <Util/SystemExtensionPermissions.hpp>
#include <fstream>

namespace {
std::filesystem::path receiptPath(const std::filesystem::path& root) { return root / "system_extensions.receipt"; }
}
namespace SystemExtensionConsent {
bool read(const std::filesystem::path& root, SystemExtensionPermissions& p) {
    std::ifstream in(receiptPath(root));
    int schema = 0;
    int io = 0;
    int ipc = 0;
    int external = 0;
    int executingProcess = 0;
    if (!(in >> schema >> io >> ipc >> external >> executingProcess) ||
        schema != SystemExtensionPermissions::SCHEMA_VERSION) {
        return false;
    }
    p.io = io != 0;
    p.ipc = ipc != 0;
    p.external = external != 0;
    p.executingProcess = executingProcess != 0;
    return true;
}
bool shouldWarn(const std::filesystem::path& root, const SystemExtensionPermissions& permissions) {
    SystemExtensionPermissions existing;
    if (!read(root, existing)) {
        return permissions.io || permissions.ipc || permissions.external || permissions.executingProcess;
    }
    return existing.io != permissions.io || existing.ipc != permissions.ipc ||
           existing.external != permissions.external ||
           existing.executingProcess != permissions.executingProcess;
}
bool write(const std::filesystem::path& root, const SystemExtensionPermissions& p) {
    std::error_code error; std::filesystem::create_directories(root, error); if (error) return false;
    std::ofstream out(receiptPath(root), std::ios::trunc); if (!out) return false;
    out << SystemExtensionPermissions::SCHEMA_VERSION << ' ' << (p.io ? 1 : 0) << ' '
        << (p.ipc ? 1 : 0) << ' ' << (p.external ? 1 : 0) << ' '
        << (p.executingProcess ? 1 : 0) << '\n';
    return static_cast<bool>(out);
}
}
