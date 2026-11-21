#include <Util/AtomicFile.hpp>
#include <Util/UUID.hpp>
#include <fstream>

namespace fs = std::filesystem;

namespace AtomicFile {

bool writeReplacing(const fs::path& target, std::string_view data, std::string& error) {
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        error = "target is a directory";
        return false;
    }
    fs::path temporary = target;
    temporary += "." + RecubinUUID::generate() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::noreplace);
        if (!output) {
            error = "cannot create temporary file";
            return false;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            error = "write failed";
            return false;
        }
    }
    std::error_code renameError;
    fs::rename(temporary, target, renameError);
    if (renameError) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error = "rename failed: " + renameError.message();
        return false;
    }
    return true;
}

}
