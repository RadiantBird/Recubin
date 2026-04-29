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

// localRot: 親 Spatial からの相対回転
void BaseCube::setRotation(Quaternion localRot) {
    cframe.Rotation = localRot;
    if (!actor) return;
    Quaternion worldRot = getWorldCFrame().Rotation;
    if (lastWorkspace && lastWorkspace->physicsEngine && SystemState::get().isPlaying) {
        lastWorkspace->physicsEngine->enqueueSetRotation(this, worldRot);
    } else {
        physx::PxTransform pose = actor->getGlobalPose();
        pose.q = physx::PxQuat(worldRot.x, worldRot.y, worldRot.z, worldRot.w);
        actor->setGlobalPose(pose);
    }
}

void BaseCube::syncPhysics() {
    if (!actor || Anchored) return;

    physx::PxTransform pose = actor->getGlobalPose();
    Vector3    worldPos(pose.p.x, pose.p.y, pose.p.z);
    Quaternion worldRot(pose.q.w, pose.q.x, pose.q.y, pose.q.z);

    auto par = Parent.lock();
    if (par && par->IsA("Spatial")) {
        CFrame pw = static_cast<Spatial*>(par.get())->getWorldCFrame();
        Quaternion pConj = pw.Rotation.conjugate();
        cframe.Position = pConj.rotate(worldPos - pw.Position);
        cframe.Rotation = pConj * worldRot;
    } else {
        cframe.Position = worldPos;
        cframe.Rotation = worldRot;
    }
}

// localPos: 親 Spatial からの相対座標
void BaseCube::teleportTo(Vector3 localPos) {
    cframe.Position = localPos;
    if (actor) {
        Vector3 worldPos = getWorldCFrame().Position;
        physx::PxTransform pose = actor->getGlobalPose();
        pose.p = physx::PxVec3(worldPos.x, worldPos.y, worldPos.z);
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
