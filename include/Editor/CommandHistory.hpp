#pragma once
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Texture.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Motor.hpp>
#include <yaml-cpp/yaml.h>
#include <memory>
#include <string>
#include <vector>

// ===================================================
//  Command インターフェース
// ===================================================
struct Command {
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual ~Command() = default;
};

// ===================================================
//  CommandHistory  — Undo/Redo スタック管理
// ===================================================
class CommandHistory {
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
public:
    // execute: コマンドを適用してUndoスタックに積む（redo クリア）
    void execute(std::unique_ptr<Command> cmd);
    // record: すでに適用済みの変更をUndoスタックに記録する（インタラクティブ編集用）
    void record(std::unique_ptr<Command> cmd);
    void undo();
    void redo();
    void clear();
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
};

// ===================================================
//  Command サブクラス
// ===================================================

// --- インスタンス追加 ---
struct AddInstanceCommand : Command {
    std::shared_ptr<Instance> m_parent;
    std::shared_ptr<Instance> m_child;

    AddInstanceCommand(std::shared_ptr<Instance> parent, std::shared_ptr<Instance> child)
        : m_parent(std::move(parent)), m_child(std::move(child)) {}

    void execute() override {
        if (m_parent && m_child) m_parent->addChild(m_child);
    }
    void undo() override {
        if (m_parent && m_child) m_parent->removeChild(m_child->Name);
    }
};

// --- インスタンス削除 ---
struct RemoveInstanceCommand : Command {
    std::shared_ptr<Instance> m_parent;
    std::string m_name;
    std::shared_ptr<Instance> m_child;

    RemoveInstanceCommand(std::shared_ptr<Instance> parent,
                          std::string name,
                          std::shared_ptr<Instance> child)
        : m_parent(std::move(parent)), m_name(std::move(name)), m_child(std::move(child)) {}

    void execute() override {
        if (m_parent) m_parent->removeChild(m_name);
    }
    void undo() override {
        if (m_parent && m_child) m_parent->addChild(m_child);
    }
};

// --- インスタンス移動（親変更） ---
struct MoveInstanceCommand : Command {
    std::shared_ptr<Instance> m_oldParent;
    std::shared_ptr<Instance> m_newParent;
    std::shared_ptr<Instance> m_child;

    MoveInstanceCommand(std::shared_ptr<Instance> oldParent,
                        std::shared_ptr<Instance> newParent,
                        std::shared_ptr<Instance> child)
        : m_oldParent(std::move(oldParent)), m_newParent(std::move(newParent)), m_child(std::move(child)) {}

    void execute() override {
        if (m_oldParent) m_oldParent->removeChild(m_child->Name);
        if (m_newParent) m_newParent->addChild(m_child);
    }
    void undo() override {
        if (m_newParent) m_newParent->removeChild(m_child->Name);
        if (m_oldParent) m_oldParent->addChild(m_child);
    }
};

// --- Vector3プロパティ変更（Position / Size） ---
struct SetVec3Command : Command {
    std::shared_ptr<BaseCube> m_target;
    std::string m_prop;
    Vector3 m_before, m_after;

    SetVec3Command(std::shared_ptr<BaseCube> target,
                   std::string prop,
                   Vector3 before, Vector3 after)
        : m_target(std::move(target)), m_prop(std::move(prop)),
          m_before(before), m_after(after) {}

    void execute() override { apply(m_after); }
    void undo()    override { apply(m_before); }

private:
    void apply(const Vector3& v) {
        if (!m_target) return;
        if (m_prop == "Position") m_target->teleportTo(v);
        else if (m_prop == "Size") m_target->setSize(v);
    }
};

// --- Color変更 ---
struct SetColorCommand : Command {
    std::shared_ptr<BaseCube> m_target;
    Color4 m_before, m_after;

    SetColorCommand(std::shared_ptr<BaseCube> target, Color4 before, Color4 after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->Color = m_after; }
    void undo()    override { if (m_target) m_target->Color = m_before; }
};

// --- bool プロパティ変更（Anchored / CanCollide） ---
struct SetBoolCommand : Command {
    std::shared_ptr<BaseCube> m_target;
    std::string m_prop;
    bool m_before, m_after;

    SetBoolCommand(std::shared_ptr<BaseCube> target, std::string prop, bool before, bool after)
        : m_target(std::move(target)), m_prop(std::move(prop)),
          m_before(before), m_after(after) {}

    void execute() override { apply(m_after); }
    void undo()    override { apply(m_before); }

private:
    void apply(bool v) {
        if (!m_target) return;
        if (m_prop == "Anchored")   m_target->setAnchored(v);
        else if (m_prop == "CanCollide") m_target->CanCollide = v;
    }
};

// --- インスタンスリネーム ---
struct RenameInstanceCommand : Command {
    std::shared_ptr<Instance> m_target;
    std::string m_before, m_after;

    RenameInstanceCommand(std::shared_ptr<Instance> target, std::string before, std::string after)
        : m_target(std::move(target)), m_before(std::move(before)), m_after(std::move(after)) {}

    void execute() override { if (m_target) m_target->Name = m_after; }
    void undo()    override { if (m_target) m_target->Name = m_before; }
};

// --- Rotation 変更 ---
struct SetRotationCommand : Command {
    std::shared_ptr<Spatial> m_target;
    Quaternion m_before, m_after;

    SetRotationCommand(std::shared_ptr<Spatial> target, Quaternion before, Quaternion after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->cframe.Rotation = m_after; }
    void undo()    override { if (m_target) m_target->cframe.Rotation = m_before; }
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

    GizmoCommand(std::shared_ptr<BaseCube> target, GizmoState before, GizmoState after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { apply(m_after); }
    void undo()    override { apply(m_before); }

private:
    void apply(const GizmoState& s) {
        if (!m_target) return;
        m_target->teleportTo(s.position);
        m_target->setSize(s.size);
        m_target->setRotation(s.rotation);
    }
};

// --- 複数オブジェクトのGizmo操作（位置/サイズ/回転をまとめてundoできる） ---
struct MultiGizmoCommand : Command {
    struct Entry {
        std::shared_ptr<BaseCube> target;
        GizmoState before, after;
    };
    std::vector<Entry> m_entries;

    explicit MultiGizmoCommand(std::vector<Entry> entries)
        : m_entries(std::move(entries)) {}

    void execute() override { for (auto& e : m_entries) applyState(e.target, e.after);  }
    void undo()    override { for (auto& e : m_entries) applyState(e.target, e.before); }

private:
    static void applyState(const std::shared_ptr<BaseCube>& bc, const GizmoState& s) {
        if (!bc || bc->Parent.expired()) return;
        bc->teleportTo(s.position);
        bc->setSize(s.size);
        bc->setRotation(s.rotation);
    }
};

// --- Decal Color 変更 ---
struct SetDecalColorCommand : Command {
    std::shared_ptr<Decal> m_target;
    Color4 m_before, m_after;

    SetDecalColorCommand(std::shared_ptr<Decal> target, Color4 before, Color4 after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->Color = m_after; }
    void undo()    override { if (m_target) m_target->Color = m_before; }
};

// --- Decal Face 変更 ---
struct SetDecalFaceCommand : Command {
    std::shared_ptr<Decal> m_target;
    Face m_before, m_after;

    SetDecalFaceCommand(std::shared_ptr<Decal> target, Face before, Face after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->setFace(m_after); }
    void undo()    override { if (m_target) m_target->setFace(m_before); }
};

// --- Decal Texture 変更 ---
struct SetDecalTextureCommand : Command {
    std::shared_ptr<Decal> m_target;
    std::string   m_beforePath, m_afterPath;
    unsigned int  m_beforeID,   m_afterID;

    SetDecalTextureCommand(std::shared_ptr<Decal> target,
                           std::string beforePath, unsigned int beforeID,
                           std::string afterPath,  unsigned int afterID)
        : m_target(std::move(target)),
          m_beforePath(std::move(beforePath)), m_afterPath(std::move(afterPath)),
          m_beforeID(beforeID), m_afterID(afterID) {}

    void execute() override { if (m_target) { m_target->texturePath = m_afterPath;  m_target->TextureID = m_afterID;  } }
    void undo()    override { if (m_target) { m_target->texturePath = m_beforePath; m_target->TextureID = m_beforeID; } }
};

// --- Texture Face 変更 ---
struct SetTextureFaceCommand : Command {
    std::shared_ptr<Texture> m_target;
    Face m_before, m_after;

    SetTextureFaceCommand(std::shared_ptr<Texture> target, Face before, Face after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->setFace(m_after); }
    void undo()    override { if (m_target) m_target->setFace(m_before); }
};

// --- Texture テクスチャパス変更 ---
struct SetTextureTextureCommand : Command {
    std::shared_ptr<Texture> m_target;
    std::string  m_beforePath, m_afterPath;
    unsigned int m_beforeID,   m_afterID;

    SetTextureTextureCommand(std::shared_ptr<Texture> target,
                             std::string beforePath, unsigned int beforeID,
                             std::string afterPath,  unsigned int afterID)
        : m_target(std::move(target)),
          m_beforePath(std::move(beforePath)), m_afterPath(std::move(afterPath)),
          m_beforeID(beforeID), m_afterID(afterID) {}

    void execute() override { if (m_target) { m_target->texturePath = m_afterPath;  m_target->TextureID = m_afterID;  } }
    void undo()    override { if (m_target) { m_target->texturePath = m_beforePath; m_target->TextureID = m_beforeID; } }
};

// --- Texture Color 変更 ---
struct SetTextureColorCommand : Command {
    std::shared_ptr<Texture> m_target;
    Color4 m_before, m_after;

    SetTextureColorCommand(std::shared_ptr<Texture> target, Color4 before, Color4 after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->Color = m_after; }
    void undo()    override { if (m_target) m_target->Color = m_before; }
};

// --- Texture StudsPerTile 変更 ---
struct SetTextureStudsCommand : Command {
    std::shared_ptr<Texture> m_target;
    float m_beforeU, m_afterU;
    float m_beforeV, m_afterV;

    SetTextureStudsCommand(std::shared_ptr<Texture> target,
                           float beforeU, float beforeV,
                           float afterU,  float afterV)
        : m_target(std::move(target)),
          m_beforeU(beforeU), m_afterU(afterU),
          m_beforeV(beforeV), m_afterV(afterV) {}

    void execute() override { if (m_target) { m_target->StudsPerTileU = m_afterU;  m_target->StudsPerTileV = m_afterV;  } }
    void undo()    override { if (m_target) { m_target->StudsPerTileU = m_beforeU; m_target->StudsPerTileV = m_beforeV; } }
};

// --- Sound bool プロパティ変更 ---
struct SetSoundBoolCommand : Command {
    std::shared_ptr<Sound> m_target;
    std::string m_prop;
    bool m_before, m_after;

    SetSoundBoolCommand(std::shared_ptr<Sound> target, std::string prop, bool before, bool after)
        : m_target(std::move(target)), m_prop(std::move(prop)),
          m_before(before), m_after(after) {}

    void execute() override { apply(m_after); }
    void undo()    override { apply(m_before); }

private:
    void apply(bool v) {
        if (!m_target) return;
        if (m_prop == "AutoPlay")  m_target->autoPlay = v;
        else if (m_prop == "Looped") m_target->setLooping(v);
    }
};

// --- Light Direction 変更 ---
struct SetLightDirCommand : Command {
    std::shared_ptr<Lighting> m_target;
    Vector3 m_before, m_after;

    SetLightDirCommand(std::shared_ptr<Lighting> target, Vector3 before, Vector3 after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->lightDir = m_after; }
    void undo()    override { if (m_target) m_target->lightDir = m_before; }
};

// --- Brightness 変更 ---
struct SetLightBrightnessCommand : Command {
    std::shared_ptr<Lighting> m_target;
    float m_before, m_after;

    SetLightBrightnessCommand(std::shared_ptr<Lighting> target, float before, float after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->brightness = m_after; }
    void undo()    override { if (m_target) m_target->brightness = m_before; }
};

// --- Skybox 1面のパス変更 ---
struct SetSkyboxFaceCommand : Command {
    std::shared_ptr<Skybox> m_target;
    int         m_faceIndex;
    std::string m_before, m_after;

    SetSkyboxFaceCommand(std::shared_ptr<Skybox> target, int faceIndex,
                         std::string before, std::string after)
        : m_target(std::move(target)), m_faceIndex(faceIndex),
          m_before(std::move(before)), m_after(std::move(after)) {}

    void execute() override { if (m_target) m_target->setSkyboxPath(m_faceIndex, m_after); }
    void undo()    override { if (m_target) m_target->setSkyboxPath(m_faceIndex, m_before); }
};

// --- 制約の Cube0/Cube1 参照名変更（全制約型に使える） ---
struct SetConstraintCubeNameCommand : Command {
    std::shared_ptr<Instance> m_target;
    std::string m_prop;
    std::string m_before, m_after;

    SetConstraintCubeNameCommand(std::shared_ptr<Instance> target,
                                  std::string prop,
                                  std::string before, std::string after)
        : m_target(std::move(target)), m_prop(std::move(prop)),
          m_before(std::move(before)), m_after(std::move(after)) {}

    void execute() override { apply(m_after); }
    void undo()    override { apply(m_before); }
private:
    void apply(const std::string& v) {
        if (!m_target) return;
        YAML::Node n; n = v;
        m_target->setProperty(m_prop, n);
    }
};

// --- Rope の float プロパティ変更（MaxDistance / Stiffness / Damping） ---
struct SetRopeFloatCommand : Command {
    std::shared_ptr<Rope> m_target;
    std::string m_prop;
    float m_before, m_after;

    SetRopeFloatCommand(std::shared_ptr<Rope> target, std::string prop, float before, float after)
        : m_target(std::move(target)), m_prop(std::move(prop)),
          m_before(before), m_after(after) {}

    void execute() override { apply(m_after); }
    void undo()    override { apply(m_before); }
private:
    void apply(float v) {
        if (!m_target) return;
        if      (m_prop == "MaxDistance") m_target->setMaxDistance(v);
        else if (m_prop == "Stiffness")   m_target->setStiffness(v);
        else if (m_prop == "Damping")     m_target->setDamping(v);
    }
};

// --- Motor の float プロパティ変更（DriveVelocity / MaxForce） ---
struct SetMotorFloatCommand : Command {
    std::shared_ptr<Motor> m_target;
    std::string m_prop;
    float m_before, m_after;

    SetMotorFloatCommand(std::shared_ptr<Motor> target, std::string prop, float before, float after)
        : m_target(std::move(target)), m_prop(std::move(prop)),
          m_before(before), m_after(after) {}

    void execute() override { apply(m_after); }
    void undo()    override { apply(m_before); }
private:
    void apply(float v) {
        if (!m_target) return;
        if      (m_prop == "DriveVelocity") m_target->setDriveVelocity(v);
        else if (m_prop == "MaxForce")      m_target->setMaxForce(v);
    }
};

// --- Rod の Color 変更 ---
struct SetRodColorCommand : Command {
    std::shared_ptr<Rod> m_target;
    Color4 m_before, m_after;
    SetRodColorCommand(std::shared_ptr<Rod> t, Color4 before, Color4 after)
        : m_target(std::move(t)), m_before(before), m_after(after) {}
    void execute() override { if (m_target) m_target->Color = m_after; }
    void undo()    override { if (m_target) m_target->Color = m_before; }
};

// --- Rod の LineWidth 変更 ---
struct SetRodLineWidthCommand : Command {
    std::shared_ptr<Rod> m_target;
    float m_before, m_after;
    SetRodLineWidthCommand(std::shared_ptr<Rod> t, float before, float after)
        : m_target(std::move(t)), m_before(before), m_after(after) {}
    void execute() override { if (m_target) m_target->LineWidth = m_after; }
    void undo()    override { if (m_target) m_target->LineWidth = m_before; }
};

// --- Rope の Color 変更 ---
struct SetRopeColorCommand : Command {
    std::shared_ptr<Rope> m_target;
    Color4 m_before, m_after;
    SetRopeColorCommand(std::shared_ptr<Rope> t, Color4 before, Color4 after)
        : m_target(std::move(t)), m_before(before), m_after(after) {}
    void execute() override { if (m_target) m_target->Color = m_after; }
    void undo()    override { if (m_target) m_target->Color = m_before; }
};

// --- Rope の LineWidth 変更 ---
struct SetRopeLineWidthCommand : Command {
    std::shared_ptr<Rope> m_target;
    float m_before, m_after;
    SetRopeLineWidthCommand(std::shared_ptr<Rope> t, float before, float after)
        : m_target(std::move(t)), m_before(before), m_after(after) {}
    void execute() override { if (m_target) m_target->LineWidth = m_after; }
    void undo()    override { if (m_target) m_target->LineWidth = m_before; }
};

// --- Motor の Axis 変更（次回 Play 時に適用） ---
struct SetMotorAxisCommand : Command {
    std::shared_ptr<Motor> m_target;
    Vector3 m_before, m_after;

    SetMotorAxisCommand(std::shared_ptr<Motor> target, Vector3 before, Vector3 after)
        : m_target(std::move(target)), m_before(before), m_after(after) {}

    void execute() override { if (m_target) m_target->Axis = m_after; }
    void undo()    override { if (m_target) m_target->Axis = m_before; }
};
