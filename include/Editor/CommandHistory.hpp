#pragma once
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Decal.hpp>
#include <Instances/SurfaceMark.hpp>
#include <Instances/Texture.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/PostEffect.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Motor.hpp>
#include <Instances/Script.hpp>
#include <Instances/Tool.hpp>
#include <Instances/System.hpp>
#include <Core/Terrain.hpp>
#include <Core/TerrainStreamer.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Core/SceneLoader.hpp>
#include <yaml-cpp/yaml.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

// ===================================================
//  Command インターフェース
// ===================================================
struct Command {
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual ~Command();
};

// ===================================================
//  CommandHistory  — Undo/Redo スタック管理
// ===================================================
class CommandHistory {
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
    std::function<void()> m_onChange;  // 変更が起きるたびに呼ばれる（未保存マーク用）
public:
    // 履歴に変更が加わった時（execute/record/undo/redo）に呼ばれるコールバックを設定する
    void setOnChange(std::function<void()> cb);
    // ライブ編集中の変更通知。Undo履歴は編集確定時にrecord()で1操作だけ積む。
    void notifyChanged();

    // execute: コマンドを適用してUndoスタックに積む（redo クリア）
    void execute(std::unique_ptr<Command> cmd);
    // record: すでに適用済みの変更をUndoスタックに記録する（インタラクティブ編集用）
    void record(std::unique_ptr<Command> cmd);
    void undo();
    void redo();
    void clear();
    bool canUndo() const;
    bool canRedo() const;
};

// ===================================================
//  Command サブクラス
// ===================================================

// --- 汎用プロパティ変更（PropertyRegistry スキーマ駆動） ---
//  スキーマ移行済みクラスの任意プロパティ編集に使える。将来的に個別の
//  Set*Command 群をこれ1つへ集約できる（現状は共存）。
struct SetPropertyCommand : Command {
    std::shared_ptr<Instance> m_target;
    const PropertyDesc*       m_desc;
    PropValue                 m_before, m_after;

    SetPropertyCommand(std::shared_ptr<Instance> target, const PropertyDesc* desc,
                       PropValue before, PropValue after);

    void execute() override;
    void undo() override;
};

struct SetSurfaceMarkFilterCommand : Command {
    std::shared_ptr<SurfaceMark> m_target;
    std::vector<std::shared_ptr<Instance>> m_beforeInstances, m_afterInstances;
    std::vector<std::string> m_beforePaths, m_afterPaths;
    SetSurfaceMarkFilterCommand(std::shared_ptr<SurfaceMark> target,
                                 std::vector<std::shared_ptr<Instance>> beforeInstances,
                                 std::vector<std::string> beforePaths,
                                 std::vector<std::shared_ptr<Instance>> afterInstances,
                                 std::vector<std::string> afterPaths);
    void execute() override;
    void undo() override;
};

// --- インスタンス追加 ---
struct AddInstanceCommand : Command {
    std::shared_ptr<Instance> m_parent;
    std::shared_ptr<Instance> m_child;

    AddInstanceCommand(std::shared_ptr<Instance> parent, std::shared_ptr<Instance> child);
    void execute() override;
    void undo() override;
};

// Replace one scene node while retaining its name, compatible schema fields,
// and the exact child objects.  The command owns both nodes, so undo/redo does
// not serialize or clone the subtree and child identity remains stable.
struct ReplaceInstanceCommand : Command {
    struct ReferenceChange {
        std::function<void(std::shared_ptr<Instance>)> set;
        std::shared_ptr<Instance> before;
        std::shared_ptr<Instance> after;
        bool compatible = false;
        std::string ownerLabel;
    };
    std::shared_ptr<Instance> m_parent;
    std::shared_ptr<Instance> m_before;
    std::shared_ptr<Instance> m_after;
    std::string m_className;
    std::function<void(Instance*)> m_onSelection;
    std::shared_ptr<Instance> m_systemRoot;
    std::vector<ReferenceChange> m_referenceChanges;
    bool m_referencesAnalyzed = false;
    std::vector<std::string> m_incompatibleOwners;

    ReplaceInstanceCommand(std::shared_ptr<Instance> parent,
                           std::shared_ptr<Instance> before,
                           std::string className,
                           std::function<void(Instance*)> onSelection = {},
                           std::shared_ptr<Instance> systemRoot = {});

    const std::vector<std::string>& incompatibleReferenceOwners() const;

    void analyzeReferences(const std::shared_ptr<Instance>& replacement);

    void execute() override;

    void undo() override;
};

// --- インスタンス削除 ---
struct RemoveInstanceCommand : Command {
    std::shared_ptr<Instance> m_parent;
    std::string m_name;
    std::shared_ptr<Instance> m_child;

    RemoveInstanceCommand(std::shared_ptr<Instance> parent,
                          std::string name,
                          std::shared_ptr<Instance> child);
    void execute() override;
    void undo() override;
};

// --- インスタンス移動（親変更） ---
struct MoveInstanceCommand : Command {
    std::shared_ptr<Instance> m_oldParent;
    std::shared_ptr<Instance> m_newParent;
    std::shared_ptr<Instance> m_child;

    MoveInstanceCommand(std::shared_ptr<Instance> oldParent,
                        std::shared_ptr<Instance> newParent,
                        std::shared_ptr<Instance> child);
    void execute() override;
    void undo() override;
};

// --- 複合コマンド（複数操作を1つの Undo 単位に束ねる。複数ペースト/複数親変更用） ---
struct CompositeCommand : Command {
    std::vector<std::unique_ptr<Command>> m_cmds;

    void add(std::unique_ptr<Command> c);
    bool empty() const;
    void execute() override;
    void undo() override;
};

// --- Vector3プロパティ変更（Position / Size） ---
// Spatial 全般を対象にする（Model 等の非 BaseCube も履歴に残す）。
// BaseCube のときは物理同期付きの teleportTo/setSize、それ以外は cframe/Size を直接更新。
struct SetVec3Command : Command {
    std::shared_ptr<Spatial> m_target;
    std::string m_prop;
    Vector3 m_before, m_after;

    SetVec3Command(std::shared_ptr<Spatial> target,
                   std::string prop,
                   Vector3 before, Vector3 after);
    void execute() override;
    void undo() override;

private:
    void apply(const Vector3& v);
};

// --- Color変更 ---
struct SetColorCommand : Command {
    std::shared_ptr<BaseCube> m_target;
    Color4 m_before, m_after;

    SetColorCommand(std::shared_ptr<BaseCube> target, Color4 before, Color4 after);
    void execute() override;
    void undo() override;
};

// --- Material変更（type + friction/restitution） ---
struct SetMaterialCommand : Command {
    std::shared_ptr<BaseCube> m_target;
    Material m_before, m_after;

    SetMaterialCommand(std::shared_ptr<BaseCube> target, Material before, Material after);
    void execute() override;
    void undo() override;
};

// --- MassDensity変更 ---
struct SetMassDensityCommand : Command {
    std::shared_ptr<BaseCube> m_target;
    float m_before, m_after;

    SetMassDensityCommand(std::shared_ptr<BaseCube> target, float before, float after);
    void execute() override;
    void undo() override;
};

// --- bool プロパティ変更（Anchored / CanCollide） ---
struct SetBoolCommand : Command {
    std::shared_ptr<BaseCube> m_target;
    std::string m_prop;
    bool m_before, m_after;

    SetBoolCommand(std::shared_ptr<BaseCube> target, std::string prop, bool before, bool after);
    void execute() override;
    void undo() override;

private:
    void apply(bool v);
};

// --- インスタンスリネーム ---
struct RenameInstanceCommand : Command {
    std::shared_ptr<Instance> m_target;
    std::string m_before, m_after;

    RenameInstanceCommand(std::shared_ptr<Instance> target, std::string before, std::string after);
    void execute() override;
    void undo() override;
};

// 複数インスタンスの名前変更。実行時は一時名を経由して兄弟名の衝突を避ける。
struct MultiRenameInstanceCommand : Command {
    struct Entry {
        std::shared_ptr<Instance> target;
        std::string before, after;
    };
    std::vector<Entry> m_entries;
    explicit MultiRenameInstanceCommand(std::vector<Entry> entries);
    void execute() override;
    void undo() override;
private:
    void apply(bool after);
};

// --- Rotation 変更 ---
struct SetRotationCommand : Command {
    std::shared_ptr<Spatial> m_target;
    Quaternion m_before, m_after;

    SetRotationCommand(std::shared_ptr<Spatial> target, Quaternion before, Quaternion after);
    void execute() override;
    void undo() override;
};

// --- Tool Position 変更 ---
struct SetToolPositionCommand : Command {
    std::shared_ptr<Tool> m_target;
    Vector3 m_before, m_after;

    SetToolPositionCommand(std::shared_ptr<Tool> target, Vector3 before, Vector3 after);
    void execute() override;
    void undo() override;
};

// --- Tool Rotation 変更 ---
struct SetToolRotationCommand : Command {
    std::shared_ptr<Tool> m_target;
    Quaternion m_before, m_after;

    SetToolRotationCommand(std::shared_ptr<Tool> target, Quaternion before, Quaternion after);
    void execute() override;
    void undo() override;
};

// --- CFrame 一括変更（Position + Rotation をまとめて undo できる） ---
struct SetSpatialCFrameCommand : Command {
    std::shared_ptr<Spatial> m_target;
    CFrame m_before, m_after;

    SetSpatialCFrameCommand(std::shared_ptr<Spatial> target, CFrame before, CFrame after);
    void execute() override;
    void undo() override;

private:
    void apply(const CFrame& v);
};

// 複数SpatialのワールドCFrameとSizeを一つのUndo単位で変更する。
struct MultiSpatialTransformCommand : Command {
    struct Entry {
        std::shared_ptr<Spatial> target;
        CFrame beforeCFrame, afterCFrame;
        Vector3 beforeSize, afterSize;
    };
    std::vector<Entry> m_entries;
    explicit MultiSpatialTransformCommand(std::vector<Entry> entries);
    void execute() override;
    void undo() override;
private:
    void apply(bool after);
};

// --- Gizmo操作（位置/サイズ/回転をまとめてundoできる） ---
struct GizmoState {
    Vector3    position;
    Vector3    size;
    Quaternion rotation;
};

struct GizmoCommand : Command {
    std::shared_ptr<BaseCube> m_target;
    GizmoState m_before, m_after;

    GizmoCommand(std::shared_ptr<BaseCube> target, GizmoState before, GizmoState after);
    void execute() override;
    void undo() override;

private:
    void apply(const GizmoState& s);
};

// --- 複数オブジェクトのGizmo操作（位置/サイズ/回転をまとめてundoできる） ---
struct MultiGizmoCommand : Command {
    struct Entry {
        std::shared_ptr<Spatial> target;
        GizmoState before, after;
    };
    std::vector<Entry> m_entries;

    explicit MultiGizmoCommand(std::vector<Entry> entries);
    void execute() override;
    void undo() override;

private:
    static void applyState(const std::shared_ptr<Spatial>& sp, const GizmoState& s);
};

// --- Decal Color 変更 ---
struct SetDecalColorCommand : Command {
    std::shared_ptr<Decal> m_target;
    Color4 m_before, m_after;

    SetDecalColorCommand(std::shared_ptr<Decal> target, Color4 before, Color4 after);
    void execute() override; void undo() override;
};

// --- Decal Face 変更 ---
struct SetDecalFaceCommand : Command {
    std::shared_ptr<Decal> m_target;
    Face m_before, m_after;

    SetDecalFaceCommand(std::shared_ptr<Decal> target, Face before, Face after);
    void execute() override; void undo() override;
};

// --- Decal Mode 変更 ---
struct SetDecalModeCommand : Command {
    std::shared_ptr<Decal> m_target;
    DecalMode m_before, m_after;

    SetDecalModeCommand(std::shared_ptr<Decal> target, DecalMode before, DecalMode after);
    void execute() override; void undo() override;
};

// --- Decal Texture 変更 ---
struct SetDecalTextureCommand : Command {
    std::shared_ptr<Decal> m_target;
    std::string   m_beforePath, m_afterPath;
    unsigned int  m_beforeID,   m_afterID;

    SetDecalTextureCommand(std::shared_ptr<Decal> target,
                           std::string beforePath, unsigned int beforeID,
                           std::string afterPath,  unsigned int afterID);
    void execute() override; void undo() override;
};

// --- Decal UVCenter/UVRadius 変更 (MeshCube配下のUV空間配置) ---
struct SetDecalUVCommand : Command {
    std::shared_ptr<Decal> m_target;
    Vector2 m_beforeCenter, m_afterCenter;
    float   m_beforeRadius, m_afterRadius;

    SetDecalUVCommand(std::shared_ptr<Decal> target,
                       Vector2 beforeCenter, float beforeRadius,
                       Vector2 afterCenter,  float afterRadius);
    void execute() override; void undo() override;
};

// --- Texture Face 変更 ---
struct SetTextureFaceCommand : Command {
    std::shared_ptr<Texture> m_target;
    Face m_before, m_after;

    SetTextureFaceCommand(std::shared_ptr<Texture> target, Face before, Face after);
    void execute() override; void undo() override;
};

// --- Texture テクスチャパス変更 ---
struct SetTextureTextureCommand : Command {
    std::shared_ptr<Texture> m_target;
    std::string  m_beforePath, m_afterPath;
    unsigned int m_beforeID,   m_afterID;

    SetTextureTextureCommand(std::shared_ptr<Texture> target,
                             std::string beforePath, unsigned int beforeID,
                             std::string afterPath,  unsigned int afterID);
    void execute() override; void undo() override;
};

// --- Texture Color 変更 ---
struct SetTextureColorCommand : Command {
    std::shared_ptr<Texture> m_target;
    Color4 m_before, m_after;

    SetTextureColorCommand(std::shared_ptr<Texture> target, Color4 before, Color4 after);
    void execute() override; void undo() override;
};

// --- Texture StudsPerTile 変更 ---
struct SetTextureStudsCommand : Command {
    std::shared_ptr<Texture> m_target;
    float m_beforeU, m_afterU;
    float m_beforeV, m_afterV;

    SetTextureStudsCommand(std::shared_ptr<Texture> target,
                           float beforeU, float beforeV,
                           float afterU,  float afterV);
    void execute() override; void undo() override;
};

// --- Sound bool プロパティ変更 ---
struct SetSoundBoolCommand : Command {
    std::shared_ptr<Sound> m_target;
    std::string m_prop;
    bool m_before, m_after;

    SetSoundBoolCommand(std::shared_ptr<Sound> target, std::string prop, bool before, bool after);
    void execute() override; void undo() override;

private:
    void apply(bool v);
};

// --- Sound float プロパティ変更 ---
struct SetSoundFloatCommand : Command {
    std::shared_ptr<Sound> m_target;
    std::string m_prop;
    float m_before, m_after;

    SetSoundFloatCommand(std::shared_ptr<Sound> target, std::string prop, float before, float after);
    void execute() override; void undo() override;

private:
    void apply(float v);
};

// --- Light Direction 変更 ---
struct SetLightDirCommand : Command {
    std::shared_ptr<Lighting> m_target;
    Vector3 m_before, m_after;

    SetLightDirCommand(std::shared_ptr<Lighting> target, Vector3 before, Vector3 after);
    void execute() override; void undo() override;
};

// --- Brightness 変更 ---
struct SetLightBrightnessCommand : Command {
    std::shared_ptr<Lighting> m_target;
    float m_before, m_after;

    SetLightBrightnessCommand(std::shared_ptr<Lighting> target, float before, float after);
    void execute() override; void undo() override;
};

// --- PostEffect bool プロパティ変更（Enabled） ---
struct SetPostEffectBoolCommand : Command {
    std::shared_ptr<PostEffect> m_target;
    std::string m_prop;
    bool m_before, m_after;

    SetPostEffectBoolCommand(std::shared_ptr<PostEffect> target, std::string prop, bool before, bool after);
    void execute() override; void undo() override;

private:
    void apply(bool v);
};

// --- PostEffect int プロパティ変更（ZIndex） ---
struct SetPostEffectIntCommand : Command {
    std::shared_ptr<PostEffect> m_target;
    std::string m_prop;
    int m_before, m_after;

    SetPostEffectIntCommand(std::shared_ptr<PostEffect> target, std::string prop, int before, int after);
    void execute() override; void undo() override;

private:
    void apply(int v);
};

// --- PostEffect Type 変更 ---
struct SetPostEffectTypeCommand : Command {
    std::shared_ptr<PostEffect> m_target;
    PostEffectKind m_before, m_after;

    SetPostEffectTypeCommand(std::shared_ptr<PostEffect> target, PostEffectKind before, PostEffectKind after);
    void execute() override; void undo() override;
};

// --- PostEffect float プロパティ変更（Intensity / Param1 / Param2） ---
struct SetPostEffectFloatCommand : Command {
    std::shared_ptr<PostEffect> m_target;
    std::string m_prop;
    float m_before, m_after;

    SetPostEffectFloatCommand(std::shared_ptr<PostEffect> target, std::string prop, float before, float after);
    void execute() override; void undo() override;

private:
    void apply(float v);
};

// --- Skybox 1面のパス変更 ---
struct SetSkyboxFaceCommand : Command {
    std::shared_ptr<Skybox> m_target;
    int         m_faceIndex;
    std::string m_before, m_after;

    SetSkyboxFaceCommand(std::shared_ptr<Skybox> target, int faceIndex,
                         std::string before, std::string after);
    void execute() override; void undo() override;
};

// --- 制約の Cube0/Cube1 参照名変更（全制約型に使える） ---
struct SetConstraintCubeNameCommand : Command {
    std::shared_ptr<Instance> m_target;
    std::string m_prop;
    std::string m_before, m_after;

    SetConstraintCubeNameCommand(std::shared_ptr<Instance> target,
                                  std::string prop,
                                  std::string before, std::string after);
    void execute() override; void undo() override;
private:
    void apply(const std::string& v);
};

// --- NumberValue.Value 変更 ---
struct SetNumberValueCommand : Command {
    std::shared_ptr<Instance> m_target;
    double m_before, m_after;

    SetNumberValueCommand(std::shared_ptr<Instance> target, double before, double after)
        ;
    void execute() override; void undo() override;
private:
    void apply(double v);
};

// --- QuaternionValue.Value 変更 ---
struct SetQuaternionValueCommand : Command {
    std::shared_ptr<Instance> m_target;
    Quaternion m_before, m_after;

    SetQuaternionValueCommand(std::shared_ptr<Instance> target, Quaternion before, Quaternion after)
        ;
    void execute() override; void undo() override;
private:
    void apply(const Quaternion& v);
};

// --- CFrameValue.Value 変更 ---
struct SetCFrameValueCommand : Command {
    std::shared_ptr<Instance> m_target;
    CFrame m_before, m_after;

    SetCFrameValueCommand(std::shared_ptr<Instance> target, CFrame before, CFrame after)
        ;
    void execute() override; void undo() override;
private:
    void apply(const CFrame& v);
};

// --- Rope の float プロパティ変更（MaxDistance / Stiffness / Damping） ---
struct SetRopeFloatCommand : Command {
    std::shared_ptr<Rope> m_target;
    std::string m_prop;
    float m_before, m_after;

    SetRopeFloatCommand(std::shared_ptr<Rope> target, std::string prop, float before, float after);
    void execute() override; void undo() override;
private:
    void apply(float v);
};

// --- Motor の float プロパティ変更（DriveVelocity / MaxForce） ---
struct SetMotorFloatCommand : Command {
    std::shared_ptr<Motor> m_target;
    std::string m_prop;
    float m_before, m_after;

    SetMotorFloatCommand(std::shared_ptr<Motor> target, std::string prop, float before, float after);
    void execute() override; void undo() override;
private:
    void apply(float v);
};

// --- Rod の Color 変更 ---
struct SetRodColorCommand : Command {
    std::shared_ptr<Rod> m_target;
    Color4 m_before, m_after;
    SetRodColorCommand(std::shared_ptr<Rod> t, Color4 before, Color4 after);
    void execute() override; void undo() override;
};

// --- Rod の LineWidth 変更 ---
struct SetRodLineWidthCommand : Command {
    std::shared_ptr<Rod> m_target;
    float m_before, m_after;
    SetRodLineWidthCommand(std::shared_ptr<Rod> t, float before, float after);
    void execute() override; void undo() override;
};

// --- Rope の Color 変更 ---
struct SetRopeColorCommand : Command {
    std::shared_ptr<Rope> m_target;
    Color4 m_before, m_after;
    SetRopeColorCommand(std::shared_ptr<Rope> t, Color4 before, Color4 after);
    void execute() override; void undo() override;
};

// --- Rope の LineWidth 変更 ---
struct SetRopeLineWidthCommand : Command {
    std::shared_ptr<Rope> m_target;
    float m_before, m_after;
    SetRopeLineWidthCommand(std::shared_ptr<Rope> t, float before, float after);
    void execute() override; void undo() override;
};

// --- Motor の Axis 変更 ---
struct SetMotorAxisCommand : Command {
    std::shared_ptr<Motor> m_target;
    Vector3 m_before, m_after;

    SetMotorAxisCommand(std::shared_ptr<Motor> target, Vector3 before, Vector3 after);
    void execute() override; void undo() override;
};

// --- Script bool プロパティ変更（Enabled） ---
struct SetScriptBoolCommand : Command {
    std::shared_ptr<Script> m_target;
    std::string m_prop;
    bool m_before, m_after;

    SetScriptBoolCommand(std::shared_ptr<Script> target, std::string prop, bool before, bool after);
    void execute() override; void undo() override;

private:
    void apply(bool v);
};

// --- System int プロパティ変更（MaxClonesPerFrame / MaxRestartsPerFrame） ---
struct SetSystemIntCommand : Command {
    std::shared_ptr<System> m_target;
    std::string m_prop;
    int m_before, m_after;

    SetSystemIntCommand(std::shared_ptr<System> target, std::string prop, int before, int after);
    void execute() override; void undo() override;

private:
    void apply(int v);
};

// --- System float プロパティ変更（ScriptLoopTimeoutSeconds） ---
struct SetSystemFloatCommand : Command {
    std::shared_ptr<System> m_target;
    std::string m_prop;
    float m_before, m_after;

    SetSystemFloatCommand(std::shared_ptr<System> target, std::string prop, float before, float after);
    void execute() override; void undo() override;

private:
    void apply(float v);
};

// --- Terrain bool プロパティ変更（Enabled / Flat） ---
struct SetTerrainBoolCommand : Command {
    std::shared_ptr<Terrain> m_target;
    std::string m_prop;
    bool m_before, m_after;

    SetTerrainBoolCommand(std::shared_ptr<Terrain> target, std::string prop, bool before, bool after);
    void execute() override;
    void undo() override;

private:
    void apply(bool v);
};

// --- Terrain int プロパティ変更（Seed） ---
struct SetTerrainIntCommand : Command {
    std::shared_ptr<Terrain> m_target;
    std::string m_prop;
    int m_before, m_after;

    SetTerrainIntCommand(std::shared_ptr<Terrain> target, std::string prop, int before, int after);
    void execute() override;
    void undo() override;

private:
    void apply(int v);
};

// --- Terrain string プロパティ変更（DataPath） ---
struct SetTerrainStringCommand : Command {
    std::shared_ptr<Terrain> m_target;
    std::string m_prop;
    std::string m_before, m_after;

    SetTerrainStringCommand(std::shared_ptr<Terrain> target, std::string prop, std::string before, std::string after);
    void execute() override;
    void undo() override;

private:
    void apply(const std::string& v);
};

// --- 地形ブラシの1ストローク分の変更をまとめてUndo/Redoする ---
struct TerrainBrushStrokeCommand : Command {
    std::shared_ptr<Terrain> m_target;
    std::vector<TerrainStreamer::VoxelDiffEntry> m_entries;

    TerrainBrushStrokeCommand(std::shared_ptr<Terrain> target,
                               std::vector<TerrainStreamer::VoxelDiffEntry> entries);
    void execute() override;
    void undo() override;
private:
    void apply(bool useAfter);
};
