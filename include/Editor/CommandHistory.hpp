#pragma once
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Lighting.hpp>
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
    std::shared_ptr<Lighting> m_target;
    int         m_faceIndex;
    std::string m_before, m_after;

    SetSkyboxFaceCommand(std::shared_ptr<Lighting> target, int faceIndex,
                         std::string before, std::string after)
        : m_target(std::move(target)), m_faceIndex(faceIndex),
          m_before(std::move(before)), m_after(std::move(after)) {}

    void execute() override { if (m_target) m_target->setSkyboxPath(m_faceIndex, m_after); }
    void undo()    override { if (m_target) m_target->setSkyboxPath(m_faceIndex, m_before); }
};
