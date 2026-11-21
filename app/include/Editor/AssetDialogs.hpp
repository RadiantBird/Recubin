#pragma once
#include <Core/AssetExporter.hpp>
#include <Core/AssetImporter.hpp>
#include <memory>
#include <string>
#include <vector>

class CommandHistory;
class Instance;

// ===================================================
//  AssetDialogs — アセット(.rcaet)の書き出しダイアログと取り込みフロー
//   書き出し: 依存ファイルを埋め込むか構造のみかを選んで保存する。
//   取り込み: 実行形式が含まれる場合は確認を取り、Undo可能な1コマンドでツリーへ追加する。
//  OSのファイルダイアログはポップアップ外（render先頭/末尾）で開く。
// ===================================================
class AssetDialogs {
public:
    CommandHistory*        history           = nullptr;
    // 取り込んだルートを選択状態にする（EditorManagerがExplorerの選択へ配線する）
    Instance**             selectedInstance  = nullptr;
    std::vector<Instance*>* selectedInstances = nullptr;

    // 書き出し対象に1つでも書き出せるInstanceが含まれるか（メニューの有効判定）
    static bool canExport(const std::vector<Instance*>& roots);

    void requestExport(const std::vector<Instance*>& roots);
    // path が空ならOSのファイルダイアログで .rcaet を選ばせる。parent は取り込み先。
    void requestImport(const std::shared_ptr<Instance>& parent, const std::string& path = {});

    // 毎フレーム、トップレベル（他のウィンドウの外）から呼ぶ。
    void render();

private:
    // ---- 書き出し ----
    AssetExporter::Plan m_plan;
    char                m_name[128] = {};
    int                 m_mode = 0;              // 0=構造のみ, 1=依存ファイルを埋め込む
    std::vector<char>   m_embed;                 // m_plan.dependencies と同順の埋め込み選択
    std::string         m_exportError;
    bool                m_openExport  = false;
    bool                m_closeExport = false;
    bool                m_doSaveExport = false;

    // ---- 取り込み ----
    // 取り込み先。弱参照で持つ（強参照だとシーン破棄後もSound等を生かし続け、終了時に
    // 解放済みの音声エンジンへ触れてクラッシュする）。
    std::weak_ptr<Instance>   m_importParent;
    bool                      m_doPickImport = false;
    std::string               m_pendingImportPath;
    // YAML::Nodeを含むため、値の代入ではなくポインタで受け渡す（Nodeの代入は共有実体を書き換える）。
    std::unique_ptr<AssetImporter::Loaded> m_loaded;
    bool                      m_openExecutableConfirm = false;

    // ---- 結果メッセージ ----
    std::string              m_messageTitle;
    std::vector<std::string> m_messageLines;
    bool                     m_openMessage = false;

    void renderExportDialog();
    void renderExecutableConfirm();
    void renderMessage();
    void runExport(const std::string& path);
    void beginImport(const std::string& path);
    void executeImport(bool allowExecutables);
    void showMessage(const std::string& title, std::vector<std::string> lines);
};
