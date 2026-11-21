#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// スクリプトAPI(Luauバインディング)のホットパスを探すための、名前付き区間の累積プロファイラー。
// 無効時のコストは分岐1つ。GUI自動化の `script_profile on|off|reset|dump` で操作する。
// FrameProfilerはフレーム単位の区間用で、こちらは「呼び出し回数 × 1回あたりの時間」を
// 名前ごとに集計する。入れ子の区間は、自分だけの時間(self)と子を含む時間(total)を分けて持つ。
// メインスレッドから呼ぶ前提だが、無効時以外は排他をかけるので他スレッドから呼んでも安全。
class ApiProfiler {
public:
    struct Entry {
        std::string name;
        std::uint64_t calls = 0;
        double totalMs = 0.0;  // 子の区間を含む
        double selfMs = 0.0;   // 子の区間を除く
        double maxMs = 0.0;    // 1回あたりの最大(total)
    };

    static bool enabled() { return s_enabled; }
    static void setEnabled(bool enabled) { s_enabled = enabled; }
    static void reset();
    // selfMsの大きい順。
    static std::vector<Entry> snapshot();
    // 上位topN件を1行のJSONにする: {"enabled":..,"entries":[{"name":..,"calls":..,"totalMs":..,"selfMs":..,"avgUs":..,"maxMs":..}]}
    static std::string toJson(std::size_t topN = 40);

    // RAII区間。detailを渡すと "name:detail" が名前になる(プロパティ名など。有効時だけ文字列を作る)。
    class Scope {
    public:
        explicit Scope(const char* name);
        Scope(const char* name, std::string_view detail);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        void begin(std::string name);
        bool m_active = false;
        std::string m_name;
        std::int64_t m_startNs = 0;
        std::int64_t m_childNs = 0;
        Scope* m_parent = nullptr;
    };

private:
    static inline bool s_enabled = false;
    friend class Scope;
};
