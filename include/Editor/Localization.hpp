#pragma once

// ===================================================
//  Localization  — エディターUI文字列のローカライズテーブル
//  プロパティ欄のラベル・クラス名は対象外（英語固定）。
//  ボタン・メニュー・ツールチップ・ポップアップ等の
//  UIコピーのみをここに集約する。
// ===================================================
namespace Loc {

enum class Lang { JA, EN };

enum class LocKey {
    // ---- 共通 ----
    Cancel, Browse, OK,

    // ---- Settingsメニュー ----
    MenuSettings, LanguageJapanese, LanguageEnglish,

    // ---- EditorManager: メニューバー / パネルタイトル ----
    MenuFile, MenuSaveScene, MenuOpenScene, MenuNewScene, MenuPackageGame, MenuQuit,
    MenuView,
    PanelExplorer, PanelProperties, PanelViewport, PanelContentBrowser,
    PanelConsole, PanelAnimation, AnimationEditorWindowTitle,
    MenuPhysicsDebug,

    // ---- EditorManager: ダイアログ ----
    PlayLoadTitle, PlayLoadLine1, PlayLoadLine2, PlayLoadConfirm,
    UnsavedTitle, UnsavedLine1, UnsavedLine2, SaveAndQuit, QuitWithoutSaving,
    PackageGameTitle, GameNameLabel, OutputDirLabel, PackageButton,
    CloseButton, ProcessingText,
    NavMeshBuildTitle, NavMeshBuildMessage, NavMeshBuildProgress,
    PlayStartErrorTitle, PlayStartErrorMessage, LocalServerRequiresNetwork,

    // ---- EditorManager: ツールバー ----
    PlayButton, PauseButton, StopButton,
    PlayModeLabel, PlayModeNormal, PlayModeHere, PlayModeLocalServer,
    NetworkClientCountLabel, NetworkClientCountFormat, NetworkClientStatus,
    SelectTool, MoveTool, ResizeTool, RotateTool,
    SnapTranslate, SnapRotate, SnapScale, CollisionFit,
    AddObjectDropdown, SaveButton, LoadButton,

    // ---- SceneHierarchyPanel ----
    NoWorkspace, NewScriptTitle, ScriptModeNew, ScriptModeExisting,
    ScriptNameLabel, ScriptPickHint,
    CategoryCubes, CategoryCubesDesc,
    CategoryEffects, CategoryEffectsDesc, AudioServiceUnavailable,
    CategoryEnvironment, CategoryEnvironmentDesc,
    CategoryOther, CategoryOtherDesc,
    CategoryGui, CategoryGuiDesc,
    CategoryPhysicsConstraints, CategoryPhysicsConstraintsDesc,
    CategoryValue, CategoryValueDesc,
    SwitchToWorkspace, OpenInNewViewport, InsertObjectMenu,
    MenuDelete, MenuCopy, MenuPaste, MenuPasteAsChild,

    // ---- PropertiesPanel ----
    RoundButton, RoundTooltip, OpenExternalEditor, ResetButton,
    TerrainRandomize, TerrainFlatCheckbox, TerrainRegenerateButton,
    TerrainRegenConfirmTitle, TerrainRegenConfirmLine1, TerrainRegenConfirmLine2,
    RegenerateConfirmButton,
    TerrainBrushEdit, TerrainBrushRadius, TerrainBrushMode,
    TerrainBrushModeLower, TerrainBrushModeSmooth, TerrainBrushModeRaise,
    TerrainBrushHint,
    TerrainBrushSculptTab, TerrainBrushPaintTab, TerrainBrushPaintColor,
    PickerPromptAttachment, PickerPromptCube, PickerPromptAny,
    ToolUnresolved, NothingSelected,

    // ---- AnimationEditorPanel ----
    SelectModelHint, TargetModelLabel, CreateAnimationButton,
    ExportButton, ImportButton, AddKeyButton, SelectCubeHint, PartLabel,
    StartEditButton, EndEditButton, StartEditHint,

    // ---- ConsolePanel ----
    TabSystem, TabLuau, ClearButton, FilterLabel,

    // ---- ContentBrowserPanel ----
    BackButton, AssetsFolderNotFound,
    CreateFileRef, CopyPath,

    // ---- PropertiesPanel: MeshCube UV再生成 ----
    RegenerateUVButton, RegenerateUVConfirmTitle,
    RegenerateUVConfirmLine1, RegenerateUVConfirmLine2,

    // ---- EditorManager: ツールバー2段化(Cubes/Physics/Characterタブ) ----
    ToolbarTabBasic, ToolbarTabCubes, ToolbarTabTerrain, ToolbarTabPhysics, ToolbarTabCharacter,
    NewScriptButton, AddHumanoidButton, RigBuilderButton,

    // ---- ViewportPanel: ギズモ軸モード切替 ----
    GizmoWorld, GizmoLocal, GizmoModeTooltip,

    // ---- PropertiesPanel: 複数選択時の一括編集 ----
    MixedValue, MultiSelectedCount,

    // ---- WelcomePanel ----
    PanelWelcome, WelcomeMessage, WelcomeBtnNew, WelcomeBtnContinue, WelcomeBtnOpen,

    Count
};

void        setLanguage(Lang lang);
Lang        getLanguage();
const char* t(LocKey key);

} // namespace Loc
