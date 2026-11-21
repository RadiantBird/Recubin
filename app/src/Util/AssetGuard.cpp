#include <Util/AssetGuard.hpp>
#include <Util/AssetPath.hpp>
#include <Util/Logger.hpp>
#include <system_error>

namespace fs = std::filesystem;

namespace {
    bool  s_enabled = false;
    fs::path s_root;

    // p が root を接頭辞に持つ（root 自身も可）かを字句的に判定する。
    bool isWithin(const fs::path& root, const fs::path& p) {
        auto rit = root.begin();
        auto pit = p.begin();
        for (; rit != root.end(); ++rit, ++pit) {
            if (pit == p.end() || *pit != *rit) return false;
        }
        return true;
    }
}

namespace AssetGuard {

void enableSandbox(const fs::path& root) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(root, ec);
    s_root = ec ? fs::absolute(root).lexically_normal() : canon;
    s_enabled = true;
    RCBN_LOG("AssetGuard enabled. Root: " << s_root.string());
}

bool allow(const std::string& path) {
    if (!s_enabled) return true;      // エディター/テストは無制限
    if (path.empty()) return true;    // 空パスは各ローダ側の既存処理に委ねる

    std::error_code ec;
    // 相対パスはルート基準で解決。絶対パスはそのまま正規化される。
    // フォールバックでも lexically_normal で `..` を必ず畳み込む（脱出検出のため）。
    const fs::path storedPath = AssetPath::fromStored(path);
    fs::path resolved = fs::weakly_canonical(s_root / storedPath, ec);
    if (ec) resolved = (s_root / storedPath).lexically_normal();

    if (!isWithin(s_root, resolved)) {
        RCBN_WARN("Blocked out-of-root asset path: " << path
                  << " (resolved: " << resolved.string() << ")");
        return false;
    }
    return true;
}

} // namespace AssetGuard
