#include "include/Instances/BaseCube.hpp"
#include "include/Core/Physics.hpp"
#include "include/Core/SystemState.hpp"
#include "include/Util/Logger.hpp"

BaseCube::BaseCube(Vector3 Pos, Vector3 Sz) 
    : Spatial(Pos, Sz, "BaseCube"), Color(1, 1, 1, 1) {
    onAncestorChanged();
}

std::string BaseCube::GetClassName() {
    return "BaseCube";
}

bool BaseCube::IsA(std::string className) {
    if (className == "BaseCube") {
        return true;
    }
    return Spatial::IsA(className);
}

void BaseCube::onAncestorChanged() {
    // 1. 先祖を遡って Workspace を探す (O(h))
    Instance* ws_raw = findFirstAncestorWorkspace();
    
    if (ws_raw) {
        // Workspace を発見した場合
        Workspace* ws = static_cast<Workspace*>(ws_raw);
        
        // 重複登録を防ぎつつ、物理エンジンの待機リストへ
        // std::cout << "Adding to workspace...\n";
        ws->registerCube(this);
        lastWorkspace = ws;
    } else {
        // std::cout << "Workspace is null!\n";
        // Workspace の外に出た場合は Physics から削除
        if (lastWorkspace) {
            if (lastWorkspace->physicsEngine) {
                lastWorkspace->physicsEngine->removeCube(this);
            } else {
                // physicsEngine が nullptr の場合は手動でクリーンアップ
                if (actor) {
                    actor->release();
                    actor = nullptr;
                }
            }
        }
        lastWorkspace = nullptr;
    }

    // 2. 子階層への通知も継続（BaseCube の中に何か入っている場合のため）
    Instance::onAncestorChanged();
}

void BaseCube::setSize(Vector3 newSize) {
    Size = newSize;
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        if (SystemState::get().isPlaying) {
            lastWorkspace->physicsEngine->enqueueResize(this);
        } else {
            lastWorkspace->physicsEngine->recreateActor(this);
        }
    }
}

void BaseCube::setRotation(Quaternion rot) {
    cframe.Rotation = rot;
    if (!actor) return;
    if (lastWorkspace && lastWorkspace->physicsEngine && SystemState::get().isPlaying) {
        lastWorkspace->physicsEngine->enqueueSetRotation(this, rot);
    } else {
        physx::PxTransform pose = actor->getGlobalPose();
        pose.q = physx::PxQuat(rot.x, rot.y, rot.z, rot.w);
        actor->setGlobalPose(pose);
    }
}

void BaseCube::syncPhysics() {
    if (!actor || Anchored) return;

    physx::PxTransform pose = actor->getGlobalPose();
    this->cframe.Position = Vector3(pose.p.x, pose.p.y, pose.p.z);
    this->cframe.Rotation = Quaternion(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
}

void BaseCube::teleportTo(Vector3 pos) {
    cframe.Position = pos;
    if (actor) {
        physx::PxTransform pose = actor->getGlobalPose();
        pose.p = physx::PxVec3(pos.x, pos.y, pos.z);
        actor->setGlobalPose(pose);
    }
}

BaseCube::~BaseCube() {
    // RCBN_LOG("BaseCube Destructor: " << this->Name);
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        lastWorkspace->physicsEngine->removeCube(this);
    } else if (actor) {
        // Workspace がない場合でもアクターは解放する
        actor->release();
        actor = nullptr;
    }
}
unsigned int BaseCube::getDecalTexture(Face face, unsigned int fallback) const {
    for (auto const& [name, child] : children) {
        if (child->IsA("Decal")) {
            Decal* d = static_cast<Decal*>(child.get());
            if (d->face == face && d->TextureID != 0) {
                return d->TextureID;
            }
        }
    }
    return fallback;
}

void BaseCube::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Anchored") {
        this->Anchored = value.as<bool>();
    } else if (name == "CanCollide") {
        this->CanCollide = value.as<bool>();
    } else if (name == "Color") {
        Color4 color(0,0,0,0);
        color.r = value[0].as<float>();
        color.g = value[1].as<float>();
        color.b = value[2].as<float>();
        color.a = value[3].as<float>();
        this->Color = color;
    } else {
        Spatial::setProperty(name, value);
    }
}
