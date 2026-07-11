#include <Editor/Localization.hpp>
#include <array>
#include <cstddef>

namespace Loc {

namespace {

struct Entry { const char* ja; const char* en; };

// LocKey の宣言順と1対1で対応させること（インデックスアクセスのため）。
constexpr std::array<Entry, static_cast<size_t>(LocKey::Count)> kTable = { {
    // ---- 共通 ----
    { "キャンセル",        "Cancel" },
    { "参照...",           "Browse..." },
    { "OK",                "OK" },

    // ---- Settingsメニュー ----
    { "設定",              "Settings" },
    { "日本語",            "Japanese" },
    { "English",           "English" },

    // ---- EditorManager: メニューバー / パネルタイトル ----
    { "ファイル",           "File" },
    { "シーンを保存",        "Save Scene" },
    { "シーンを開く",        "Open Scene" },
    { "ゲームをパッケージ化...", "Package Game..." },
    { "終了",               "Quit" },
    { "表示",               "View" },
    { "エクスプローラー",     "Explorer" },
    { "プロパティ",          "Properties" },
    { "ビューポート",        "Viewport" },
    { "コンテンツブラウザ",   "Content Browser" },
    { "コンソール",          "Console" },
    { "アニメーション",       "Animation" },
    { "アニメーションエディター", "Animation Editor" },
    { "物理デバッグ表示",     "Physics Debug" },

    // ---- EditorManager: ダイアログ ----
    { "テストプレイ中のシーン読込", "Load Scene During Play" },
    { "テストプレイ中です。",       "You are currently in Play mode." },
    { "終了してこのシーンを読み込みますか？", "Stop and load this scene?" },
    { "終了して読み込む",           "Stop and Load" },
    { "未保存の変更",               "Unsaved Changes" },
    { "シーンに未保存の変更があります。", "The scene has unsaved changes." },
    { "終了する前に保存しますか？",   "Save before exiting?" },
    { "保存「して」終了",            "Save && Quit" },
    { "保存「せず」終了",            "Quit Without Saving" },
    { "ゲームをパッケージ化",        "Package Game" },
    { "ゲーム名:",                  "Game Name:" },
    { "出力先フォルダ:",             "Output Directory:" },
    { "パッケージ化",                "Package" },
    { "閉じる",                     "Close" },
    { "処理中...",                  "Processing..." },

    // ---- EditorManager: ツールバー ----
    { "  再生  ",           "  Play  " },
    { " 一時停止 ",          " Pause " },
    { "  停止  ",           "  Stop  " },
    { "選択",               "Select" },
    { "移動",               "Move" },
    { "リサイズ",            "Resize" },
    { "回転",               "Rotate" },
    { "移動スナップ",         "Move Snap" },
    { "回転スナップ",         "Rotate Snap" },
    { "リサイズスナップ",      "Resize Snap" },
    { "衝突フィット",         "Collision Fit" },
    { "オブジェクト追加 v",   "Add Object v" },
    { "保存",               "Save" },
    { "読込",               "Load" },

    // ---- SceneHierarchyPanel ----
    { "(ワークスペースなし)", "(No workspace)" },
    { "新規スクリプト",       "New Script" },
    { "新規作成",            "Create New" },
    { "既存ファイルを選択",   "Select Existing File" },
    { "スクリプト名:",        "Script name:" },
    { "ファイルピッカーで .luau/.luar を選択します", "Select a .luau/.luar file using the file picker" },
    { "Cube系",             "Basic Shapes" },
    { "Workspace内で描画される基本的なクラス。", "Basic classes rendered within the Workspace." },
    { "効果",               "Effects" },
    { "世界を彩りましょう。", "Add color and flair to your world." },
    { "AudioService が利用できません", "AudioService is unavailable" },
    { "環境",               "Environment" },
    { "7日で宇宙を創るように。", "Like creating a universe in seven days." },
    { "その他",              "Other" },
    { "分類するには少ない...でも重要。", "Too few to categorize... but important." },
    { "GUI",                "GUI" },
    { "画面にテキストや画像を表示します。", "Displays text or images on screen." },
    { "物理制約",            "Physics Constraints" },
    { "物理挙動に影響を与えます。", "Affects physical behavior." },
    { "このworkspaceに切り替える", "Switch to this Workspace" },
    { "新しいビューポートで開く(非推奨、バグあり)", "Open in New Viewport (deprecated, buggy)" },
    { "オブジェクトを挿入",    "Insert Object" },
    { "削除",               "Delete" },
    { "コピー",              "Copy" },
    { "貼り付け",            "Paste" },
    { "子として貼り付け",     "Paste as Child" },

    // ---- PropertiesPanel ----
    { "丸",                 "Round" },
    { "整数に丸める",         "Round to nearest integer" },
    { "外部エディタで開く",   "Open in External Editor" },
    { "リセット",            "Reset" },
    { "乱数化",              "Randomize" },
    { "Flat（平坦生成）",     "Flat (Generate Flat)" },
    { "再生成",              "Regenerate" },
    { "Terrain再生成の確認",  "Confirm Terrain Regeneration" },
    { "DataPath の地形（ブラシ編集を含む）を破棄して、", "This discards the terrain at DataPath (including brush edits)" },
    { "現在の Seed / Flat 設定で作り直します。よろしいですか？", "and regenerates it using the current Seed/Flat settings. Continue?" },
    { "再生成する",          "Regenerate" },
    { "ブラシで編集",         "Edit with Brush" },
    { "半径",               "Radius" },
    { "モード",              "Mode" },
    { "Lower（削る）",       "Lower (Carve)" },
    { "Smooth（滑らかに）",   "Smooth" },
    { "Raise（盛る）",       "Raise (Build)" },
    { "ビューポート上で左クリック長押しで適用", "Hold left-click in the viewport to apply" },
    { "Viewport またはヒエラルキーで Attachment をクリックして指定", "Click an Attachment in the Viewport or Hierarchy to assign" },
    { "Viewport またはヒエラルキーでキューブをクリックして指定", "Click a cube in the Viewport or Hierarchy to assign" },
    { "(未解決)",            "(unresolved)" },
    { "選択されていません",   "Nothing selected" },

    // ---- AnimationEditorPanel ----
    { "HierarchyでキャラクターのModelまたはその子Cubeを選択してください。", "Select a character's Model or one of its child Cubes in the Hierarchy." },
    { "対象Model: %s",       "Target Model: %s" },
    { "アニメーションを作成", "Create Animation" },
    { "エクスポート...",     "Export..." },
    { "インポート...",       "Import..." },
    { "キーを追加",          "Add Key" },
    { "Cube(Model直下)を選択するとキーを追加できます", "Select a Cube directly under the Model to add a key" },
    { "パーツ: %s",          "part: %s" },

    // ---- ConsolePanel ----
    { "システム",            "System" },
    { "Luau",               "Luau" },
    { "クリア",              "Clear" },
    { "フィルタ",            "Filter" },

    // ---- ContentBrowserPanel ----
    { "<- 戻る",             "<- Back" },
    { "(assetsフォルダが見つかりません)", "(assets/ folder not found)" },

    // ---- PropertiesPanel: MeshCube UV再生成 ----
    { "UV再生成",            "Regenerate UV" },
    { "UV再生成の確認",       "Confirm UV Regeneration" },
    { "現在のUVを破棄して、", "This discards the current UV," },
    { "xatlasで自動生成し直します。よろしいですか？", "and regenerates it automatically with xatlas. Continue?" },
} };

Lang g_lang = Lang::JA;

} // namespace

void setLanguage(Lang lang) { g_lang = lang; }
Lang getLanguage() { return g_lang; }

const char* t(LocKey key) {
    const Entry& e = kTable[static_cast<size_t>(key)];
    return (g_lang == Lang::JA) ? e.ja : e.en;
}

} // namespace Loc
