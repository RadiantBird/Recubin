#include "include/Instances/BaseCube.hpp"
#include "include/Core/Physics.hpp"
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
    return Instance::IsA(className);
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

void BaseCube::syncPhysics() {
    if (!actor || Anchored) return;

    physx::PxTransform pose = actor->getGlobalPose();
    this->cframe.Position = Vector3(pose.p.x, pose.p.y, pose.p.z);
    this->cframe.Rotation = Quaternion(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
}

BaseCube::~BaseCube() {
    RCBN_LOG("BaseCube Destructor: " << this->Name);
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        lastWorkspace->physicsEngine->removeCube(this);
    } else if (actor) {
        // Workspace がない場合でもアクターは解放する
        actor->release();
        actor = nullptr;
    }
}
void BaseCube::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Anchored") {
        this->Anchored = value.as<bool>();
    } else if (name == "CanCollide") {
        this->CanCollide = value.as<bool>();
    } else if (name == "Color") {
        this->Color = value.as<Color4>();
    } else {
        Spatial::setProperty(name, value);
    }
}
