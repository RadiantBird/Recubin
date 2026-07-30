#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Script.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Model.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/Motor.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Tool.hpp>

#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/AudioService.hpp>
#include <Core/TimeStretchNode.hpp>
#include <Core/NullInputBackend.hpp>
#include <Core/Physics.hpp>
#include <Core/Terrain.hpp>
#include <Core/TerrainStreamer.hpp>
#include <Core/User.hpp>
#include <Editor/CommandHistory.hpp>
#include <Network/NatProtocol.hpp>
#include <Util/Logger.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
const char* findSceneArgument(int argc, char* argv[], int startIndex) {
    for (int i = startIndex; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--expect-error") {
            ++i;
            continue;
        }
        if (argument.starts_with("--")) continue;
        return argv[i];
    }
    return nullptr;
}

void collectBaseCubes(const std::shared_ptr<Instance>& instance,
                      std::vector<std::shared_ptr<BaseCube>>& cubes) {
    if (!instance) return;
    if (auto cube = std::dynamic_pointer_cast<BaseCube>(instance)) cubes.push_back(cube);
    for (const auto& [name, child] : instance->getChildren()) {
        (void)name;
        collectBaseCubes(child, cubes);
    }
}

float positionDistance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

int runWeldRegression(const std::shared_ptr<Workspace>& workspace) {
    struct PoseSample {
        std::shared_ptr<BaseCube> cube;
        Vector3 position;
    };

    std::vector<std::shared_ptr<BaseCube>> allCubes;
    collectBaseCubes(workspace, allCubes);

    std::unordered_set<BaseCube*> visited;
    std::vector<PoseSample> samples;
    size_t assemblyCount = 0;
    for (const auto& cube : allCubes) {
        if (!cube->Anchored || visited.contains(cube.get())) continue;
        auto assembly = Weld::collectAssembly(cube, *workspace);
        if (assembly.size() < 2) continue;

        ++assemblyCount;
        std::cout << "[WeldRegression] anchored assembly root=" << cube->Name
                  << " members=" << assembly.size() << "\n";
        for (const auto& member : assembly) {
            if (!member || !visited.insert(member.get()).second) continue;
            samples.push_back({member, member->getWorldPosition()});
        }
    }

    if (samples.empty()) {
        std::cout << "[WeldRegression] No anchored Weld assembly found.\n";
        return 2;
    }

    workspace->initPhysics();
    auto* physics = workspace->getPhysicsEngine();
    float overallMaxDelta = 0.0f;
    for (int frame = 1; frame <= 3; ++frame) {
        physics->update(*workspace, 1.0f / 60.0f);
        physics->syncWeldKinematics();

        float maxDelta = 0.0f;
        const PoseSample* worst = nullptr;
        for (const auto& sample : samples) {
            const float delta = positionDistance(sample.position, sample.cube->getWorldPosition());
            if (delta > maxDelta) {
                maxDelta = delta;
                worst = &sample;
            }
        }
        overallMaxDelta = std::max(overallMaxDelta, maxDelta);
        std::cout << "[WeldRegression] frame=" << frame << " maxPositionDelta=" << maxDelta;
        if (worst) {
            const auto now = worst->cube->getWorldPosition();
            std::cout << " cube=" << worst->cube->Name
                      << " before=[" << worst->position.x << ',' << worst->position.y << ',' << worst->position.z << ']'
                      << " after=[" << now.x << ',' << now.y << ',' << now.z << ']';
            if (physics->hasBody(*worst->cube)) {
                const CFrame bodyPose = physics->getBodyWorldCFrame(*worst->cube);
                std::cout << " body=[" << bodyPose.Position.x << ',' << bodyPose.Position.y << ',' << bodyPose.Position.z << ']'
                          << " bodyQ=[" << bodyPose.Rotation.x << ',' << bodyPose.Rotation.y << ','
                          << bodyPose.Rotation.z << ',' << bodyPose.Rotation.w << ']';
            }
        }
        std::cout << "\n";
    }

    std::cout << "[WeldRegression] assemblies=" << assemblyCount
              << " sampledCubes=" << samples.size() << "\n";
    if (overallMaxDelta > 0.001f) {
        std::cout << "[WeldRegression] FAIL: anchored assembly moved by "
                  << overallMaxDelta << " studs.\n";
        return 1;
    }
    std::cout << "[WeldRegression] PASS\n";
    return 0;
}

bool near(float a, float b, float epsilon = 0.001f) {
    return std::abs(a - b) <= epsilon;
}

bool sameCFrame(const CFrame& a, const CFrame& b) {
    const float rotationDot = std::abs(
        a.Rotation.w * b.Rotation.w + a.Rotation.x * b.Rotation.x +
        a.Rotation.y * b.Rotation.y + a.Rotation.z * b.Rotation.z);
    return near(a.Position.x, b.Position.x) && near(a.Position.y, b.Position.y) &&
           near(a.Position.z, b.Position.z) && rotationDot >= 0.9999f;
}

int runToolWeldRegression() {
    auto workspace = std::make_shared<Workspace>();
    auto tool = std::make_shared<Tool>("Tool");
    auto plainTool = std::make_shared<Tool>("PlainTool");
    auto handle = std::make_shared<BaseCube>(Vector3(1.0f, 2.0f, 3.0f), Vector3(1, 1, 1));
    auto blade  = std::make_shared<BaseCube>(Vector3(1.0f, 2.0f, 6.0f), Vector3(1, 4, 1));
    auto guard  = std::make_shared<BaseCube>(Vector3(-1.0f, 2.0f, 3.0f), Vector3(4, 0.5f, 0.5f));
    auto plain  = std::make_shared<BaseCube>(Vector3(-4.0f, 1.0f, 0.0f), Vector3(1, 1, 1));
    handle->Name = "Handle";
    blade->Name  = "Blade";
    guard->Name  = "Guard";
    plain->Name  = "PlainToolPart";

    workspace->addChild(tool);
    tool->addChild(handle);
    tool->Handle = handle;
    workspace->addChild(blade); // Tool外のCubeもHandleのWeld連結体として追従させる。
    workspace->addChild(guard);
    workspace->addChild(plainTool);
    plainTool->addChild(plain);
    plainTool->Handle = plain;

    auto handleBlade = std::make_shared<Weld>(handle, blade);
    auto handleGuard = std::make_shared<Weld>(handle, guard);
    handleBlade->Name = "HandleBladeWeld";
    handleGuard->Name = "HandleGuardWeld";
    workspace->addChild(handleBlade);
    workspace->addChild(handleGuard);

    auto expect = [](bool condition, const char* name, int& failures) {
        std::cout << "[ToolWeldRegression] " << (condition ? "PASS" : "FAIL") << ": " << name << "\n";
        if (!condition) ++failures;
    };

    const CFrame initialHandle = handle->getWorldCFrame();
    const CFrame initialBlade  = blade->getWorldCFrame();
    const CFrame initialGuard  = guard->getWorldCFrame();
    const CFrame firstTarget(Vector3(10.0f, 5.0f, -2.0f),
                             Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f));
    const CFrame firstDelta = firstTarget * initialHandle.inverse();

    workspace->initPhysics();
    auto* physics = workspace->getPhysicsEngine();
    physics->moveWeldAssembly(handle, firstTarget);

    int failures = 0;
    expect(sameCFrame(handle->getWorldCFrame(), firstTarget),
           "actor未作成でもHandleが目標姿勢へ移動する", failures);
    expect(sameCFrame(blade->getWorldCFrame(), firstDelta * initialBlade),
           "actor未作成でもTool外のWeld部品が同じ変換で追従する", failures);
    expect(sameCFrame(guard->getWorldCFrame(), firstDelta * initialGuard),
           "actor未作成でも複数のWeld部品が同じ変換で追従する", failures);

    physics->update(*workspace, 1.0f / 60.0f);
    expect(physics->hasBody(*handle) &&
           physics->sharesBody(*handle, *blade) &&
           physics->sharesBody(*handle, *guard),
           "物理登録後にWeld連結体が1つのcompound actorを共有する", failures);

    const CFrame bladeRelative = handle->getWorldCFrame().inverse() * blade->getWorldCFrame();
    const CFrame guardRelative = handle->getWorldCFrame().inverse() * guard->getWorldCFrame();
    const CFrame secondTarget(Vector3(-8.0f, 7.0f, 4.0f),
                              Quaternion::fromAxisAngle(Vector3(1, 0, 0), 35.0f));
    physics->moveWeldAssembly(handle, secondTarget);

    expect(sameCFrame(handle->getWorldCFrame(), secondTarget),
           "compound構築後もHandleが目標姿勢へ移動する", failures);
    expect(sameCFrame(handle->getWorldCFrame().inverse() * blade->getWorldCFrame(), bladeRelative),
           "compound構築後もBladeのHandle相対姿勢を維持する", failures);
    expect(sameCFrame(handle->getWorldCFrame().inverse() * guard->getWorldCFrame(), guardRelative),
           "compound構築後もGuardのHandle相対姿勢を維持する", failures);

    const CFrame plainTarget(Vector3(3.0f, 4.0f, 5.0f),
                             Quaternion::fromAxisAngle(Vector3(0, 0, 1), 20.0f));
    physics->moveWeldAssembly(plain, plainTarget);
    expect(sameCFrame(plain->getWorldCFrame(), plainTarget),
           "WeldなしTool部品も単体で追従する", failures);

    return failures == 0 ? 0 : 1;
}

int runToolWeldReequipRegression() {
    auto system = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
    auto character = std::make_shared<Model>();
    character->Name = "Character";
    auto tool = std::make_shared<Tool>("Tool");
    auto handle = std::make_shared<BaseCube>(Vector3(0.0f, 8.0f, 0.0f), Vector3(1, 1, 1));
    auto member1 = std::make_shared<BaseCube>(Vector3(0.0f, 8.0f, 2.0f), Vector3(1, 1, 3));
    auto member2 = std::make_shared<BaseCube>(Vector3(2.0f, 8.0f, 0.0f), Vector3(3, 1, 1));
    auto member3 = std::make_shared<BaseCube>(Vector3(-2.0f, 8.0f, 0.0f), Vector3(3, 1, 1));
    handle->Name = "Handle";
    member1->Name = "Member1";
    member2->Name = "Member2";
    member3->Name = "Member3";

    system->addChild(workspace);
    system->addChild(user);
    user->initializeInventory();
    workspace->addChild(character);
    user->character = character;
    user->Inventory->addChild(tool);
    tool->addChild(handle);
    tool->addChild(member1);
    tool->addChild(member2);
    tool->addChild(member3);
    tool->Handle = handle;

    for (const auto& member : {member1, member2, member3}) {
        auto weld = std::make_shared<Weld>(handle, member);
        weld->Name = "Handle" + member->Name + "Weld";
        handle->addChild(weld);
    }

    auto expect = [](bool condition, const char* name, int& failures) {
        std::cout << "[ToolWeldReequipRegression] " << (condition ? "PASS" : "FAIL")
                  << ": " << name << "\n";
        if (!condition) ++failures;
    };
    auto updatePhysics = [&] {
        workspace->getPhysicsEngine()->update(*workspace, 1.0f / 60.0f);
    };
    auto allShareActor = [&] {
        Physics* physics = workspace->getPhysicsEngine();
        return physics->hasBody(*handle) &&
               physics->sharesBody(*handle, *member1) &&
               physics->sharesBody(*handle, *member2) &&
               physics->sharesBody(*handle, *member3);
    };

    workspace->initPhysics();
    int failures = 0;

    // Inventory is outside Workspace.  Enter it, leave it, then enter it again: this
    // mirrors the Tool equip lifecycle without relying on input state.
    user->Inventory->removeChild(tool->Name);
    character->addChild(tool);
    updatePhysics();
    expect(allShareActor(), "初回装備で全Weld部品がcompound actorを共有する", failures);

    character->removeChild(tool->Name);
    user->Inventory->addChild(tool);
    updatePhysics();
    expect(!workspace->getPhysicsEngine()->hasBody(*handle) &&
           !workspace->getPhysicsEngine()->hasBody(*member1) &&
           !workspace->getPhysicsEngine()->hasBody(*member2) &&
           !workspace->getPhysicsEngine()->hasBody(*member3),
           "解除時にWorkspace外のTool部品のactorを解放する", failures);

    user->Inventory->removeChild(tool->Name);
    character->addChild(tool);
    updatePhysics();
    expect(allShareActor(), "再装備で全Weld部品がcompound actorを再共有する", failures);

    const CFrame member1Relative = handle->getWorldCFrame().inverse() * member1->getWorldCFrame();
    const CFrame member2Relative = handle->getWorldCFrame().inverse() * member2->getWorldCFrame();
    const CFrame member3Relative = handle->getWorldCFrame().inverse() * member3->getWorldCFrame();
    const CFrame target(Vector3(12.0f, 10.0f, -4.0f),
                        Quaternion::fromAxisAngle(Vector3(0, 1, 0), 45.0f));
    workspace->getPhysicsEngine()->moveWeldAssembly(handle, target);
    expect(sameCFrame(handle->getWorldCFrame(), target), "再装備後もHandleを移動できる", failures);
    expect(sameCFrame(handle->getWorldCFrame().inverse() * member1->getWorldCFrame(), member1Relative) &&
           sameCFrame(handle->getWorldCFrame().inverse() * member2->getWorldCFrame(), member2Relative) &&
           sameCFrame(handle->getWorldCFrame().inverse() * member3->getWorldCFrame(), member3Relative),
           "再装備後も全Weld部品の相対CFrameを維持する", failures);

    return failures == 0 ? 0 : 1;
}

int runToolRespawnRegression() {
    auto system = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
    system->addChild(workspace);
    system->addChild(user);
    user->initializeInventory();

    user->spawnCharacter(system.get());
    workspace->addChild(user->character);
    auto oldCharacter = user->character;

    auto tool = std::make_shared<Tool>("RespawnTool");
    auto handle = std::make_shared<BaseCube>(
        Vector3(40.0f, 12.0f, -30.0f), Vector3(1, 1, 1));
    auto member = std::make_shared<BaseCube>(
        Vector3(40.0f, 12.0f, -27.0f), Vector3(1, 1, 3));
    handle->Name = "Handle";
    member->Name = "Member";
    tool->addChild(handle);
    tool->addChild(member);
    tool->Handle = handle;
    auto weld = std::make_shared<Weld>(handle, member);
    weld->Name = "HandleMemberWeld";
    handle->addChild(weld);

    const int slotIndex = user->addToolToSlot(tool, 3);
    user->currentTool = tool;
    user->currentSlotIndex = slotIndex;
    tool->Equipped = true;
    oldCharacter->addChild(tool);

    workspace->initPhysics();
    workspace->getPhysicsEngine()->update(*workspace, 1.0f / 60.0f);

    auto expect = [](bool condition, const char* name, int& failures) {
        std::cout << "[ToolRespawnRegression] " << (condition ? "PASS" : "FAIL")
                  << ": " << name << "\n";
        if (!condition) ++failures;
    };

    int failures = 0;
    expect(workspace->getPhysicsEngine()->hasBody(*handle) &&
           workspace->getPhysicsEngine()->sharesBody(*handle, *member),
           "死亡前の装備Toolがcompound actorを持つ", failures);

    user->respawnCharacter();
    auto newCharacter = user->character;

    expect(newCharacter && newCharacter != oldCharacter,
           "respawnで新しいcharacterが生成される", failures);
    expect(tool->Parent.lock() == newCharacter &&
           newCharacter->children.contains(tool->Name),
           "装備Toolが新しいcharacterの子へ移る", failures);
    expect(tool->Equipped && user->currentTool == tool &&
           user->currentSlotIndex == slotIndex &&
           user->getToolInSlot(slotIndex) == tool,
           "装備状態・currentTool・スロット番号を維持する", failures);
    expect(!user->Inventory->children.contains(tool->Name),
           "再装備後のToolがInventoryに残らない", failures);
    expect(!oldCharacter->children.contains(tool->Name),
           "旧characterにToolが残らない", failures);

    workspace->getPhysicsEngine()->update(*workspace, 1.0f / 60.0f);
    expect(workspace->getPhysicsEngine()->hasBody(*handle) &&
           workspace->getPhysicsEngine()->sharesBody(*handle, *member),
           "respawn後の物理更新でToolのcompound actorを再構築する", failures);

    return failures == 0 ? 0 : 1;
}

int runInventoryToolSyncRegression() {
    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
    user->initializeInventory();

    auto nonToolFolder = std::make_shared<Folder>();
    nonToolFolder->Name = "NonToolFolder";
    auto nonToolCube = std::make_shared<BaseCube>(Vector3{}, Vector3{});
    nonToolCube->Name = "NonToolCube";
    user->Inventory->addChild(nonToolFolder);
    user->Inventory->addChild(nonToolCube);

    std::vector<std::shared_ptr<Tool>> tools;
    for (int i = 0; i < 12; ++i) {
        auto tool = std::make_shared<Tool>("InventoryTool" + std::to_string(i));
        user->Inventory->addChild(tool);
        tools.push_back(tool);
    }

    user->syncToolsFromInventory();

    auto expect = [](bool condition, const char* name, int& failures) {
        std::cout << "[InventoryToolSyncRegression] " << (condition ? "PASS" : "FAIL")
                  << ": " << name << "\n";
        if (!condition) ++failures;
    };

    int failures = 0;
    std::unordered_set<Tool*> inventoryTools;
    for (const auto& tool : tools) inventoryTools.insert(tool.get());

    size_t slottedToolCount = 0;
    bool slotsContainOnlyInventoryTools = true;
    for (int i = 0; i < 10; ++i) {
        auto tool = user->getToolInSlot(i);
        if (tool) ++slottedToolCount;
        slotsContainOnlyInventoryTools = slotsContainOnlyInventoryTools &&
                                         tool && inventoryTools.contains(tool.get());
    }
    expect(slottedToolCount == 10 && slotsContainOnlyInventoryTools,
           "Inventory直下のToolだけが最大10個ホットバーへ登録される", failures);
    expect(user->getToolInSlot(10) == nullptr,
           "ホットバー範囲外のスロットは取得できない", failures);
    expect(user->Inventory->children.contains("NonToolFolder") &&
           user->Inventory->children.contains("NonToolCube"),
           "非ToolのInventory子は保持される", failures);
    expect(user->Inventory->children.size() == tools.size() + 2,
           "11個目以降のToolもInventoryに残る", failures);

    auto workspace = std::make_shared<Workspace>();
    workspace->addChild(tools.front());
    bool movedToolStillSlotted = false;
    for (int i = 0; i < 10; ++i) {
        if (user->getToolInSlot(i) == tools.front()) movedToolStillSlotted = true;
    }
    expect(!movedToolStillSlotted,
           "InventoryからWorkspaceへ移したToolのスロット参照を即座に破棄する", failures);

    auto previousInventory = user->Inventory;
    user->resetInventory();
    bool anySlotOccupied = false;
    for (int i = 0; i < 10; ++i) {
        if (user->getToolInSlot(i)) anySlotOccupied = true;
    }
    expect(user->Inventory != previousInventory &&
           user->Inventory->children.empty() &&
           !user->Inventory->Parent.lock() &&
           !anySlotOccupied,
           "リロード時にInventory本体と全スロット参照を持ち越さない", failures);

    // SceneLoaderでは、新しいInventoryがUser配下へ接続された後に
    // user->Inventoryメンバへ採用される。この順序でもToolの所有Userが同期され、
    // 後からWorkspaceへ移した際にスロット参照が消えることを確認する。
    auto loadedInventory = std::make_shared<Folder>();
    loadedInventory->Name = "Inventory";
    user->addChild(loadedInventory);
    auto loadedTool = std::make_shared<Tool>("LoadedTool");
    loadedInventory->addChild(loadedTool);
    user->Inventory = loadedInventory;
    user->syncToolsFromInventory();
    workspace->addChild(loadedTool);
    bool loadedToolStillSlotted = false;
    for (int i = 0; i < 10; ++i) {
        if (user->getToolInSlot(i) == loadedTool) loadedToolStillSlotted = true;
    }
    expect(!loadedToolStillSlotted,
           "SceneLoaderのInventory採用順序でも移動後のスロット参照を破棄する", failures);

    return failures == 0 ? 0 : 1;
}

int runHumanoidPartRefRegression() {
    auto expect = [](bool condition, const char* name, int& failures) {
        std::cout << "[HumanoidPartRefRegression] " << (condition ? "PASS" : "FAIL")
                  << ": " << name << "\n";
        if (!condition) ++failures;
    };

    int failures = 0;
    auto model = std::make_shared<Model>();
    auto humanoid = std::make_shared<Humanoid>();
    auto root = std::make_shared<BaseCube>(Vector3{}, Vector3{2.0f, 2.0f, 1.0f});
    auto torso = std::make_shared<BaseCube>(Vector3{}, Vector3{2.0f, 2.0f, 1.0f});
    auto head = std::make_shared<BaseCube>(Vector3{}, Vector3{2.0f, 1.0f, 1.0f});
    auto leftArm = std::make_shared<BaseCube>(Vector3{}, Vector3{1.0f, 2.0f, 1.0f});
    auto rightArm = std::make_shared<BaseCube>(Vector3{}, Vector3{1.0f, 2.0f, 1.0f});
    auto leftLeg = std::make_shared<BaseCube>(Vector3{}, Vector3{1.0f, 2.0f, 1.0f});
    auto rightLeg = std::make_shared<BaseCube>(Vector3{}, Vector3{1.0f, 2.0f, 1.0f});

    root->Name = "Root";
    torso->Name = "Torso";
    head->Name = "Head";
    leftArm->Name = "LeftArm";
    rightArm->Name = "RightArm";
    leftLeg->Name = "LeftLeg";
    rightLeg->Name = "RightLeg";

    model->addChild(root);
    model->addChild(torso);
    model->addChild(head);
    model->addChild(leftArm);
    model->addChild(rightArm);
    model->addChild(leftLeg);
    model->addChild(rightLeg);
    model->addChild(humanoid);
    humanoid->resolveParts(model.get());

    expect(humanoid->getRootPart() == root &&
           humanoid->getTorsoPart() == torso &&
           humanoid->getHeadPart() == head &&
           humanoid->getLeftArmPart() == leftArm &&
           humanoid->getRightArmPart() == rightArm &&
           humanoid->getLeftLegPart() == leftLeg &&
           humanoid->getRightLegPart() == rightLeg,
           "resolveParts()が全兄弟パーツを正しく解決する", failures);

    std::weak_ptr<BaseCube> weakRoot = root;
    std::weak_ptr<BaseCube> weakTorso = torso;
    std::weak_ptr<BaseCube> weakHead = head;
    std::weak_ptr<BaseCube> weakLeftArm = leftArm;
    std::weak_ptr<BaseCube> weakRightArm = rightArm;
    std::weak_ptr<BaseCube> weakLeftLeg = leftLeg;
    std::weak_ptr<BaseCube> weakRightLeg = rightLeg;

    root.reset();
    torso.reset();
    head.reset();
    leftArm.reset();
    rightArm.reset();
    leftLeg.reset();
    rightLeg.reset();
    model.reset();

    expect(weakRoot.expired() && weakTorso.expired() && weakHead.expired() &&
           weakLeftArm.expired() && weakRightArm.expired() &&
           weakLeftLeg.expired() && weakRightLeg.expired(),
           "Humanoidだけを保持しても兄弟パーツの寿命を延長しない", failures);
    expect(!humanoid->getRootPart() &&
           !humanoid->getTorsoPart() &&
           !humanoid->getHeadPart() &&
           !humanoid->getLeftArmPart() &&
           !humanoid->getRightArmPart() &&
           !humanoid->getLeftLegPart() &&
           !humanoid->getRightLegPart(),
           "親Model破棄後は全getterがnullptrを返す", failures);

    const Vector3 fallbackRoot = humanoid->getRootWorldPosition();
    const Vector3 fallbackHead = humanoid->getHeadWorldPosition();
    humanoid->applyBodyAnimation(false, false);
    humanoid->updateFirstPersonState(true);
    humanoid->updateFirstPersonState(false);
    expect(fallbackRoot.x == 0.0f && fallbackRoot.y == 0.0f && fallbackRoot.z == 0.0f &&
           fallbackHead.x == 0.0f && fallbackHead.y == 0.0f && fallbackHead.z == 0.0f,
           "期限切れ後の座標取得・身体配置・一人称更新が安全にフォールバックする", failures);

    auto injectedRoot = std::make_shared<BaseCube>(Vector3{}, Vector3{2.0f, 2.0f, 1.0f});
    std::weak_ptr<BaseCube> weakInjectedRoot = injectedRoot;
    humanoid->setRootPart(injectedRoot);
    expect(humanoid->getRootPart() == injectedRoot,
           "setRootPart()が予測用Rootを参照できるようにする", failures);
    injectedRoot.reset();
    expect(weakInjectedRoot.expired() && !humanoid->getRootPart(),
           "setRootPart()は予測用Rootを所有しない", failures);

    return failures == 0 ? 0 : 1;
}

const char* physicsBackendName(PhysicsBackendType type) {
    return type == PhysicsBackendType::Box3D ? "box3d" : "physx";
}

float vectorLength(const Vector3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

bool finiteVector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finiteCFrame(const CFrame& value) {
    return finiteVector(value.Position) &&
           std::isfinite(value.Rotation.w) &&
           std::isfinite(value.Rotation.x) &&
           std::isfinite(value.Rotation.y) &&
           std::isfinite(value.Rotation.z);
}

void addTerrainBoxHull(
    PhysicsTerrainDescriptor& descriptor,
    const Vector3& center,
    const Vector3& halfSize) {
    PhysicsTerrainHullDescriptor hull;
    hull.localFrame = CFrame(center);
    hull.vertices = {
        {-halfSize.x, -halfSize.y, -halfSize.z},
        { halfSize.x, -halfSize.y, -halfSize.z},
        {-halfSize.x, -halfSize.y,  halfSize.z},
        { halfSize.x, -halfSize.y,  halfSize.z},
        {-halfSize.x,  halfSize.y, -halfSize.z},
        { halfSize.x,  halfSize.y, -halfSize.z},
        {-halfSize.x,  halfSize.y,  halfSize.z},
        { halfSize.x,  halfSize.y,  halfSize.z},
    };
    descriptor.hulls.push_back(std::move(hull));
}

PhysicsTerrainDescriptor makeMigrationTerrain(Instance* owner) {
    PhysicsTerrainDescriptor descriptor;
    descriptor.userData = owner;
    descriptor.staticFriction = 0.8f;
    descriptor.dynamicFriction = 0.7f;
    descriptor.restitution = 0.0f;

    // Two independently indexed patches share x=0. This intentionally models a
    // chunk seam instead of relying on welded triangle vertices.
    descriptor.vertices = {
        {-160, 0, -160}, {   0, 0, -160}, {   0, 0, 160}, {-160, 0, 160},
        {   0, 0, -160}, { 160, 0, -160}, { 160, 0, 160}, {   0, 0, 160},
    };
    descriptor.indices = {
        0, 2, 1, 0, 3, 2,
        4, 6, 5, 4, 7, 6,
    };

    PhysicsTerrainHullDescriptor ramp;
    ramp.localFrame = CFrame(Vector3(30.0f, 1.0f, 0.0f));
    ramp.vertices = {
        {-8, -1, -8}, {-8, -1, 8},
        { 8, -1, -8}, { 8, -1, 8},
        { 8,  7, -8}, { 8,  7, 8},
    };
    descriptor.hulls.push_back(std::move(ramp));
    addTerrainBoxHull(
        descriptor, Vector3(80.0f, 10.0f, 0.0f), Vector3(2.0f, 10.0f, 20.0f));
    return descriptor;
}

void setChunkPhysicsQuad(Chunk& chunk, float localHeight) {
    const float chunkSizeStuds =
        static_cast<float>(CHUNK_SIZE) * TerrainStreamer::BLOCK_STUD_SIZE;
    chunk.physVerts = {
        {0, localHeight, 0},
        {chunkSizeStuds, localHeight, 0},
        {chunkSizeStuds, localHeight, chunkSizeStuds},
        {0, localHeight, chunkSizeStuds},
    };
    chunk.physIndices = {0, 2, 1, 0, 3, 2};
    chunk.physConvexBlocks.clear();
}

const char* migrationBlockShapeName(BlockShape shape) {
    static constexpr const char* names[] = {
        "Empty",
        "Cube",
        "Wedge_TopNE", "Wedge_TopNW", "Wedge_TopSE", "Wedge_TopSW",
        "Wedge_BotNE", "Wedge_BotNW", "Wedge_BotSE", "Wedge_BotSW",
        "Tetra_TopNE", "Tetra_TopNW", "Tetra_TopSE", "Tetra_TopSW",
        "Tetra_BotNE", "Tetra_BotNW", "Tetra_BotSE", "Tetra_BotSW",
        "Ramp_N", "Ramp_S", "Ramp_E", "Ramp_W",
    };
    const size_t index = static_cast<size_t>(shape);
    return index < std::size(names) ? names[index] : "Unknown";
}

std::shared_ptr<BaseCube> addMigrationCube(
    const std::shared_ptr<Workspace>& workspace,
    const std::string& name,
    const Vector3& position,
    const Vector3& size,
    bool anchored = false) {
    auto cube = std::make_shared<BaseCube>(position, size);
    cube->Name = name;
    cube->Anchored = anchored;
    workspace->addChild(cube);
    return cube;
}

struct MotionSamples {
    std::vector<Vector3> positions;
    std::vector<Vector3> velocities;
};

int runPhysicsMigrationRegression() {
    auto workspace = std::make_shared<Workspace>();
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    const char* backend = physicsBackendName(physics->getBackendType());
    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        std::cout << "[PhysicsMigrationRegression] backend=" << backend << ' '
                  << (condition ? "PASS" : "FAIL") << ": " << name << "\n";
        if (!condition) ++failures;
    };

    expect(physics->isAvailable(), "backend initialization");
    if (!physics->isAvailable()) return 1;

    auto terrainOwner = std::make_shared<Instance>("MigrationTerrainOwner");
    const PhysicsTerrainHandle terrain =
        physics->createTerrain(makeMigrationTerrain(terrainOwner.get()));
    expect(static_cast<bool>(terrain), "triangle mesh + convex terrain creation");

    RaycastHit leftSeamHit;
    RaycastHit rightSeamHit;
    const bool hitLeftSeam = physics->raycast(
        Vector3(-0.01f, 30.0f, -40.0f), Vector3(0, -1, 0), 60.0f, leftSeamHit);
    const bool hitRightSeam = physics->raycast(
        Vector3(0.01f, 30.0f, -40.0f), Vector3(0, -1, 0), 60.0f, rightSeamHit);
    expect(hitLeftSeam && hitRightSeam &&
               leftSeamHit.instance == terrainOwner.get() &&
               rightSeamHit.instance == terrainOwner.get() &&
               std::abs(leftSeamHit.position.y) <= 0.02f &&
               std::abs(rightSeamHit.position.y) <= 0.02f,
           "chunk-equivalent seam raycast and Terrain userData");

    RaycastHit rampHit;
    const bool hitRamp = physics->raycast(
        Vector3(30.0f, 30.0f, 0.0f), Vector3(0, -1, 0), 60.0f, rampHit);
    expect(hitRamp && rampHit.instance == terrainOwner.get() &&
               rampHit.position.y > 3.0f,
           "convex ramp top raycast");

    RaycastHit sideHit;
    const bool hitSide = physics->raycast(
        Vector3(60.0f, 8.0f, 0.0f), Vector3(1, 0, 0), 40.0f, sideHit);
    expect(hitSide && sideHit.instance == terrainOwner.get() &&
               std::abs(sideHit.position.x - 78.0f) <= 0.02f,
           "convex terrain side raycast");

    auto fallingCube = addMigrationCube(
        workspace, "MigrationCube", {-70, 18, -20}, {2, 2, 2});
    auto fallingSphere = std::make_shared<Sphere>(
        Vector3(-45, 22, -20), Vector3(2, 2, 2));
    fallingSphere->Name = "MigrationSphere";
    workspace->addChild(fallingSphere);
    auto fallingConvex = std::make_shared<TriangularPrism>(
        Vector3(26, 20, 0), Vector3(3, 3, 3));
    fallingConvex->Name = "MigrationConvex";
    workspace->addChild(fallingConvex);

    auto verticalDefault = addMigrationCube(
        workspace, "VerticalDefault", {-95, 7, -55}, {1, 1, 1});
    auto verticalBullet = addMigrationCube(
        workspace, "VerticalBullet", {-75, 28, -55}, {1, 1, 1});
    verticalDefault->CollisionDetection = CCDMode::Default;
    verticalBullet->CollisionDetection = CCDMode::Bullet;

    auto horizontalDefault = addMigrationCube(
        workspace, "HorizontalDefault", {71.5f, 6, -10}, {1, 1, 1});
    auto horizontalBullet = addMigrationCube(
        workspace, "HorizontalBullet", {55, 10, 10}, {1, 1, 1});
    horizontalDefault->CollisionDetection = CCDMode::Default;
    horizontalBullet->CollisionDetection = CCDMode::Bullet;

    auto ropeAnchor = addMigrationCube(
        workspace, "RopeAnchor", {-80, 30, 45}, {2, 2, 2}, true);
    auto ropePayload = addMigrationCube(
        workspace, "RopePayload", {-80, 25, 45}, {2, 2, 2});
    auto rope = std::make_shared<Rope>(ropeAnchor, ropePayload);
    rope->Name = "DampedRope";
    rope->MaxDistance = 8.0f;
    rope->Stiffness = 60.0f;
    rope->Damping = 15.0f;
    workspace->addChild(rope);

    auto maxRopeAnchor = addMigrationCube(
        workspace, "MaxRopeAnchor", {-55, 30, 45}, {2, 2, 2}, true);
    auto maxRopePayload = addMigrationCube(
        workspace, "MaxRopePayload", {-55, 25, 45}, {2, 2, 2});
    auto maxOnlyRope = std::make_shared<Rope>(maxRopeAnchor, maxRopePayload);
    maxOnlyRope->Name = "MaxOnlyRope";
    maxOnlyRope->MaxDistance = 8.0f;
    maxOnlyRope->Stiffness = 0.0f;
    maxOnlyRope->Damping = 0.0f;
    workspace->addChild(maxOnlyRope);

    auto rodAnchor = addMigrationCube(
        workspace, "RodAnchor", {-25, 30, 45}, {2, 2, 2}, true);
    auto rodPayload = addMigrationCube(
        workspace, "RodPayload", {-19, 30, 45}, {2, 2, 2});
    auto rod = std::make_shared<Rod>(rodAnchor, rodPayload);
    rod->Name = "MigrationRod";
    workspace->addChild(rod);

    auto ballAnchor = addMigrationCube(
        workspace, "BallAnchor", {5, 30, 45}, {2, 2, 2}, true);
    auto ballPayload = addMigrationCube(
        workspace, "BallPayload", {5, 30, 45}, {1, 1, 1});
    auto ball = std::make_shared<BallSocket>(ballAnchor, ballPayload);
    ball->Name = "MigrationBallSocket";
    workspace->addChild(ball);

    auto weldRoot = addMigrationCube(
        workspace, "WeldRoot", {30, 30, 45}, {2, 2, 2}, true);
    auto weldMember = addMigrationCube(
        workspace, "WeldMember", {34, 30, 45}, {2, 2, 2});
    const CFrame weldRelative =
        weldRoot->getWorldCFrame().inverse() * weldMember->getWorldCFrame();
    auto weld = std::make_shared<Weld>(weldRoot, weldMember);
    weld->Name = "MigrationWeld";
    workspace->addChild(weld);

    auto motorAnchor = addMigrationCube(
        workspace, "MotorAnchor", {60, 30, 45}, {2, 2, 2}, true);
    auto motorRotor = addMigrationCube(
        workspace, "MotorRotor", {60, 30, 45}, {1, 4, 1});
    motorRotor->LockFlags =
        PhysicsLockFlags::AngularX |
        PhysicsLockFlags::AngularY |
        PhysicsLockFlags::AngularZ;
    auto motor = std::make_shared<Motor>(motorAnchor, motorRotor);
    motor->Name = "MigrationMotor";
    motor->Axis = Vector3(0, 1, 0);
    motor->DriveVelocity = 30.0f;
    motor->MaxForce = 25.0f;
    workspace->addChild(motor);

    auto noCollisionAnchor = addMigrationCube(
        workspace, "NoCollisionAnchor", {105, 12, 45}, {4, 4, 4}, true);
    auto noCollisionBody = addMigrationCube(
        workspace, "NoCollisionBody", {105, 12, 45}, {2, 2, 2});
    auto collisionControl = addMigrationCube(
        workspace, "CollisionControl", {106, 12, 45}, {2, 2, 2});
    auto noCollision =
        std::make_shared<NoCollision>(noCollisionAnchor, noCollisionBody);
    noCollision->Name = "MigrationNoCollision";
    workspace->addChild(noCollision);

    int filteredPairContacts = 0;
    int controlPairContacts = 0;
    Physics::s_contactCallback = [&](BaseCube* first, BaseCube* second) {
        auto isPair = [&](BaseCube* a, BaseCube* b) {
            return (first == a && second == b) || (first == b && second == a);
        };
        if (isPair(noCollisionAnchor.get(), noCollisionBody.get()))
            ++filteredPairContacts;
        if (isPair(noCollisionAnchor.get(), collisionControl.get()))
            ++controlPairContacts;
    };

    // First update registers bodies/constraints on both backends. PhysX processes
    // registrations after stepping, so velocity assignment follows this call.
    physics->update(*workspace, 0.0f);
    expect(physics->hasBody(*fallingCube) &&
               physics->hasBody(*fallingSphere) &&
               physics->hasBody(*fallingConvex),
           "cube/sphere/convex public Workspace registration");

    physics->setLinearVelocity(*verticalDefault, Vector3(0, -400, 0));
    physics->setLinearVelocity(*verticalBullet, Vector3(0, -400, 0));
    physics->setLinearVelocity(*horizontalDefault, Vector3(400, 0, 0));
    physics->setLinearVelocity(*horizontalBullet, Vector3(400, 0, 0));
    for (const auto& payload : {
             ropePayload, maxRopePayload, rodPayload, ballPayload, motorRotor}) {
        physics->setGravityEnabled(*payload, false);
    }
    physics->setLinearVelocity(*ropePayload, Vector3(0, -20, 0));
    physics->setLinearVelocity(*maxRopePayload, Vector3(0, -20, 0));
    physics->setLinearVelocity(*rodPayload, Vector3(0, -15, 0));

    const float ropeInitialDistance =
        positionDistance(ropeAnchor->getWorldPosition(), ropePayload->getWorldPosition());
    MotionSamples cubeSamples;
    MotionSamples sphereSamples;
    MotionSamples convexSamples;
    std::vector<float> ropeTailSpeeds;
    float verticalDefaultMinimumY = verticalDefault->getWorldPosition().y;
    float verticalBulletMinimumY = verticalBullet->getWorldPosition().y;
    float fallingCubeMinimumY = fallingCube->getWorldPosition().y;
    float fallingSphereMinimumY = fallingSphere->getWorldPosition().y;
    float fallingConvexMinimumY = fallingConvex->getWorldPosition().y;
    float horizontalDefaultMaximumX = horizontalDefault->getWorldPosition().x;
    float horizontalBulletMaximumX = horizontalBullet->getWorldPosition().x;
    bool finiteSimulation = true;

    for (int frame = 0; frame < 480; ++frame) {
        physics->update(*workspace, 1.0f / 60.0f);

        for (const auto& body : {
                 fallingCube, std::static_pointer_cast<BaseCube>(fallingSphere),
                 std::static_pointer_cast<BaseCube>(fallingConvex),
                 verticalDefault, verticalBullet,
                 horizontalDefault, horizontalBullet,
                 ropePayload, maxRopePayload, rodPayload, ballPayload,
                 weldMember, motorRotor, noCollisionBody, collisionControl}) {
            finiteSimulation =
                finiteSimulation &&
                finiteCFrame(body->getWorldCFrame()) &&
                finiteVector(physics->getLinearVelocity(*body));
        }

        verticalDefaultMinimumY = std::min(
            verticalDefaultMinimumY, verticalDefault->getWorldPosition().y);
        verticalBulletMinimumY = std::min(
            verticalBulletMinimumY, verticalBullet->getWorldPosition().y);
        fallingCubeMinimumY = std::min(
            fallingCubeMinimumY, fallingCube->getWorldPosition().y);
        fallingSphereMinimumY = std::min(
            fallingSphereMinimumY, fallingSphere->getWorldPosition().y);
        fallingConvexMinimumY = std::min(
            fallingConvexMinimumY, fallingConvex->getWorldPosition().y);
        horizontalDefaultMaximumX = std::max(
            horizontalDefaultMaximumX, horizontalDefault->getWorldPosition().x);
        horizontalBulletMaximumX = std::max(
            horizontalBulletMaximumX, horizontalBullet->getWorldPosition().x);

        if (frame >= 360) {
            cubeSamples.positions.push_back(fallingCube->getWorldPosition());
            cubeSamples.velocities.push_back(physics->getLinearVelocity(*fallingCube));
            sphereSamples.positions.push_back(fallingSphere->getWorldPosition());
            sphereSamples.velocities.push_back(physics->getLinearVelocity(*fallingSphere));
            convexSamples.positions.push_back(fallingConvex->getWorldPosition());
            convexSamples.velocities.push_back(physics->getLinearVelocity(*fallingConvex));
            ropeTailSpeeds.push_back(vectorLength(physics->getLinearVelocity(*ropePayload)));
        }
    }
    Physics::s_contactCallback = {};

    const float verticalDefaultFinalY = verticalDefault->getWorldPosition().y;
    const float verticalBulletFinalY = verticalBullet->getWorldPosition().y;
    const float cubeFinalY = fallingCube->getWorldPosition().y;
    const float sphereFinalY = fallingSphere->getWorldPosition().y;
    const float convexFinalY = fallingConvex->getWorldPosition().y;
    const float convexFinalX = fallingConvex->getWorldPosition().x;
    const float verticalDefaultFinalSpeed =
        vectorLength(physics->getLinearVelocity(*verticalDefault));
    const float verticalBulletFinalSpeed =
        vectorLength(physics->getLinearVelocity(*verticalBullet));
    const float maximumGroundPenetration = std::max({
        0.0f,
        0.5f - verticalDefaultFinalY,
        0.5f - verticalBulletFinalY,
        1.0f - cubeFinalY,
        1.0f - sphereFinalY});

    float convexMinimumVertexY = std::numeric_limits<float>::infinity();
    float convexTerrainPenetration = 0.0f;
    bool convexTouchesRampFootprint = false;
    const CFrame convexFinalFrame = fallingConvex->getWorldCFrame();
    for (const Vector3& unitVertex : fallingConvex->getConvexVertices()) {
        const Vector3 worldVertex = convexFinalFrame.pointToWorld(
            unitVertex * fallingConvex->Size);
        convexMinimumVertexY = std::min(convexMinimumVertexY, worldVertex.y);

        float terrainSurfaceY = 0.0f;
        if (worldVertex.x >= 22.0f && worldVertex.x <= 38.0f &&
            worldVertex.z >= -8.0f && worldVertex.z <= 8.0f) {
            terrainSurfaceY = std::max(
                terrainSurfaceY, (worldVertex.x - 22.0f) * 0.5f);
            convexTouchesRampFootprint = true;
        }
        convexTerrainPenetration = std::max(
            convexTerrainPenetration, terrainSurfaceY - worldVertex.y);
    }
    const bool verticalTunneling =
        verticalDefaultMinimumY < -0.5f ||
        verticalBulletMinimumY < -0.5f ||
        fallingCubeMinimumY < -1.0f ||
        fallingSphereMinimumY < -1.0f ||
        fallingConvexMinimumY < -1.5f;
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=terrain_vertical"
              << " max_final_penetration=" << maximumGroundPenetration
              << " default_min_y=" << verticalDefaultMinimumY
              << " default_final_y=" << verticalDefaultFinalY
              << " default_final_speed=" << verticalDefaultFinalSpeed
              << " bullet_min_y=" << verticalBulletMinimumY
              << " bullet_final_y=" << verticalBulletFinalY
              << " bullet_final_speed=" << verticalBulletFinalSpeed
              << " cube_min_y=" << fallingCubeMinimumY
              << " cube_final_y=" << cubeFinalY
              << " sphere_min_y=" << fallingSphereMinimumY
              << " sphere_final_y=" << sphereFinalY
              << " convex_min_y=" << fallingConvexMinimumY
              << " convex_final_y=" << convexFinalY
              << " convex_final_x=" << convexFinalX
              << " convex_min_vertex_y=" << convexMinimumVertexY
              << " convex_terrain_penetration=" << convexTerrainPenetration
              << " convex_support="
              << (convexTouchesRampFootprint ? "ramp_or_floor" : "floor")
              << " tunneled=" << (verticalTunneling ? "true" : "false")
              << "\n";
    expect(!verticalTunneling &&
               maximumGroundPenetration <= 0.02f &&
               convexTerrainPenetration <= 0.02f &&
               verticalDefaultFinalSpeed <= 0.5f &&
               verticalBulletFinalSpeed <= 0.5f,
           "Default and Bullet vertical 400 studs/s Terrain penetration <= 0.02");

    const float horizontalDefaultFinalX = horizontalDefault->getWorldPosition().x;
    const float horizontalBulletFinalX = horizontalBullet->getWorldPosition().x;
    const float horizontalDefaultFinalSpeed =
        vectorLength(physics->getLinearVelocity(*horizontalDefault));
    const float horizontalBulletFinalSpeed =
        vectorLength(physics->getLinearVelocity(*horizontalBullet));
    const float maximumWallPenetration = std::max({
        0.0f,
        horizontalDefaultFinalX + 0.5f - 78.0f,
        horizontalBulletFinalX + 0.5f - 78.0f});
    const bool horizontalTunneling =
        horizontalDefaultMaximumX > 82.5f ||
        horizontalBulletMaximumX > 82.5f;
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=terrain_horizontal"
              << " max_final_penetration=" << maximumWallPenetration
              << " default_max_x=" << horizontalDefaultMaximumX
              << " default_final_x=" << horizontalDefaultFinalX
              << " default_final_speed=" << horizontalDefaultFinalSpeed
              << " bullet_max_x=" << horizontalBulletMaximumX
              << " bullet_final_x=" << horizontalBulletFinalX
              << " bullet_final_speed=" << horizontalBulletFinalSpeed
              << " tunneled=" << (horizontalTunneling ? "true" : "false")
              << "\n";
    expect(!horizontalTunneling &&
               maximumWallPenetration <= 0.02f &&
               horizontalDefaultFinalSpeed <= 0.5f &&
               horizontalBulletFinalSpeed <= 0.5f,
           "Default and Bullet horizontal 400 studs/s Terrain penetration <= 0.02");
    expect(finiteSimulation, "no NaN/Inf during collision and constraint workload");

    auto evaluateSettled = [&](const MotionSamples& samples, const char* name) {
        if (samples.positions.empty()) {
            expect(false, name);
            return;
        }
        const float drift =
            positionDistance(samples.positions.front(), samples.positions.back());
        double sumSpeedSquared = 0.0;
        std::vector<double> energies;
        energies.reserve(samples.positions.size());
        for (size_t i = 0; i < samples.positions.size(); ++i) {
            const float speed = vectorLength(samples.velocities[i]);
            sumSpeedSquared += speed * speed;
            energies.push_back(
                0.5 * speed * speed +
                196.2 * std::max(0.0f, samples.positions[i].y - 1.0f));
        }
        const float velocityRms =
            static_cast<float>(std::sqrt(sumSpeedSquared / samples.velocities.size()));
        const size_t window = std::max<size_t>(1, energies.size() / 4);
        double earlyEnergy = 0.0;
        double lateEnergy = 0.0;
        for (size_t i = 0; i < window; ++i) {
            earlyEnergy += energies[i];
            lateEnergy += energies[energies.size() - window + i];
        }
        earlyEnergy /= window;
        lateEnergy /= window;
        const bool stable =
            drift <= 0.05f &&
            velocityRms <= 0.5f &&
            lateEnergy <= earlyEnergy * 1.10 + 0.5;
        std::cout << "[PhysicsMigrationRegression] backend=" << backend
                  << " metric=" << name
                  << " drift=" << drift
                  << " velocity_rms=" << velocityRms
                  << " early_energy=" << earlyEnergy
                  << " late_energy=" << lateEnergy << "\n";
        expect(stable, name);
    };
    evaluateSettled(cubeSamples, "settled cube drift/RMS/non-amplifying energy");
    evaluateSettled(sphereSamples, "settled sphere drift/RMS/non-amplifying energy");
    evaluateSettled(convexSamples, "settled convex drift/RMS/non-amplifying energy");

    const float ropeDistance =
        positionDistance(ropeAnchor->getWorldPosition(), ropePayload->getWorldPosition());
    const float maxOnlyRopeDistance =
        positionDistance(maxRopeAnchor->getWorldPosition(), maxRopePayload->getWorldPosition());
    double ropeSpeedSquared = 0.0;
    for (float speed : ropeTailSpeeds) ropeSpeedSquared += speed * speed;
    const float ropeVelocityRms = ropeTailSpeeds.empty()
        ? std::numeric_limits<float>::infinity()
        : static_cast<float>(std::sqrt(ropeSpeedSquared / ropeTailSpeeds.size()));
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=rope"
              << " initial_distance=" << ropeInitialDistance
              << " final_distance=" << ropeDistance
              << " max_distance=" << rope->MaxDistance
              << " tail_velocity_rms=" << ropeVelocityRms
              << " stiffness=" << rope->Stiffness
              << " damping=" << rope->Damping << "\n";
    expect(ropeInitialDistance < rope->MaxDistance &&
               ropeDistance <= rope->MaxDistance + 0.05f &&
               ropeVelocityRms <= 0.5f,
           "Rope slack/max-distance/damping steady state <= 0.05");
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=max_only_rope"
              << " final_distance=" << maxOnlyRopeDistance
              << " max_distance=" << maxOnlyRope->MaxDistance
              << " stiffness=" << maxOnlyRope->Stiffness << "\n";
    expect(maxOnlyRope->Stiffness == 0.0f &&
               maxOnlyRopeDistance <= maxOnlyRope->MaxDistance + 0.05f,
           "Rope Stiffness=0 max-only limit <= 0.05");

    const float rodDistance =
        positionDistance(rodAnchor->getWorldPosition(), rodPayload->getWorldPosition());
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=rod"
              << " initial_distance=6"
              << " final_distance=" << rodDistance
              << " error=" << std::abs(rodDistance - 6.0f) << "\n";
    expect(std::abs(rodDistance - 6.0f) <= 0.05f,
           "Rod rest length error <= 0.05");

    const float ballAnchorError =
        positionDistance(ballAnchor->getWorldPosition(), ballPayload->getWorldPosition());
    expect(ballAnchorError <= 0.05f, "BallSocket anchor error <= 0.05");

    const CFrame currentWeldRelative =
        weldRoot->getWorldCFrame().inverse() * weldMember->getWorldCFrame();
    const float weldPositionError =
        positionDistance(weldRelative.Position, currentWeldRelative.Position);
    expect(weldPositionError <= 0.001f &&
               sameCFrame(weldRelative, currentWeldRelative),
           "Weld relative CFrame error <= 0.001");

    const float motorAnchorError =
        positionDistance(motorAnchor->getWorldPosition(), motorRotor->getWorldPosition());
    const float motorSpeed = vectorLength(physics->getLinearVelocity(*motorRotor));
    expect(motor->Axis == Vector3(0, 1, 0) &&
               motor->DriveVelocity == 30.0f &&
               motor->MaxForce == 25.0f &&
               motorAnchorError <= 0.05f &&
               std::isfinite(motorSpeed) && motorSpeed < 50.0f,
           "Motor Axis/DriveVelocity/MaxForce finite overload stop");

    expect(filteredPairContacts == 0 && controlPairContacts > 0,
           "NoCollision suppresses specified pair only");

    auto chunkTerrainOwner = std::make_shared<Instance>("ChunkTerrainOwner");
    Chunk chunk0;
    chunk0.cx = 3;
    chunk0.cy = 1;
    chunk0.cz = 0;
    setChunkPhysicsQuad(chunk0, 0.0f);
    buildChunkPhysics(chunk0, *physics, chunkTerrainOwner.get());

    Chunk chunk1;
    chunk1.cx = 4;
    chunk1.cy = 1;
    chunk1.cz = 0;
    setChunkPhysicsQuad(chunk1, 0.0f);
    buildChunkPhysics(chunk1, *physics, chunkTerrainOwner.get());
    expect(chunk0.physicsHandle && chunk1.physicsHandle &&
               chunk0.physicsHandle != chunk1.physicsHandle,
           "two adjacent Chunk caches create independent terrain handles");

    RaycastHit chunkLeftSeamHit;
    RaycastHit chunkRightSeamHit;
    const bool chunkLeftSeam = physics->raycast(
        {255.99f, 90.0f, 20.0f}, {0, -1, 0}, 40.0f, chunkLeftSeamHit);
    const bool chunkRightSeam = physics->raycast(
        {256.01f, 90.0f, 20.0f}, {0, -1, 0}, 40.0f, chunkRightSeamHit);
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=chunk_seam"
              << " left_y=" << chunkLeftSeamHit.position.y
              << " right_y=" << chunkRightSeamHit.position.y << "\n";
    expect(chunkLeftSeam && chunkRightSeam &&
               chunkLeftSeamHit.instance == chunkTerrainOwner.get() &&
               chunkRightSeamHit.instance == chunkTerrainOwner.get() &&
               std::abs(chunkLeftSeamHit.position.y - 64.0f) <= 0.02f &&
               std::abs(chunkRightSeamHit.position.y - 64.0f) <= 0.02f,
           "actual adjacent Chunk origin conversion has no raycast seam");

    auto seamBody = addMigrationCube(
        workspace, "ChunkSeamBody", {256.0f, 72.0f, 20.0f},
        {0.5f, 0.5f, 0.5f});
    physics->update(*workspace, 0.0f);
    float seamBodyMinimumY = seamBody->getWorldPosition().y;
    for (int frame = 0; frame < 180; ++frame) {
        physics->update(*workspace, 1.0f / 60.0f);
        seamBodyMinimumY =
            std::min(seamBodyMinimumY, seamBody->getWorldPosition().y);
    }
    const float seamBodyFinalY = seamBody->getWorldPosition().y;
    const float seamBodyFinalSpeed =
        vectorLength(physics->getLinearVelocity(*seamBody));
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=chunk_seam_body"
              << " min_y=" << seamBodyMinimumY
              << " final_y=" << seamBodyFinalY
              << " final_speed=" << seamBodyFinalSpeed << "\n";
    expect(seamBodyMinimumY >= 63.75f &&
               seamBodyFinalY >= 64.23f &&
               seamBodyFinalSpeed <= 0.5f,
           "small dynamic body does not pass through actual Chunk boundary");

    const PhysicsTerrainHandle oldChunk0Handle = chunk0.physicsHandle;
    setChunkPhysicsQuad(chunk0, 4.0f);
    buildChunkPhysics(chunk0, *physics, chunkTerrainOwner.get());
    RaycastHit replacedChunkHit;
    RaycastHit unchangedNeighborHit;
    const bool hitReplacedChunk = physics->raycast(
        {220.0f, 90.0f, 20.0f}, {0, -1, 0}, 40.0f, replacedChunkHit);
    const bool hitUnchangedNeighbor = physics->raycast(
        {280.0f, 90.0f, 20.0f}, {0, -1, 0}, 40.0f, unchangedNeighborHit);
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=chunk_atomic_replace"
              << " old_handle=" << oldChunk0Handle.value
              << " new_handle=" << chunk0.physicsHandle.value
              << " replaced_y=" << replacedChunkHit.position.y
              << " neighbor_y=" << unchangedNeighborHit.position.y << "\n";
    expect(chunk0.physicsHandle &&
               chunk0.physicsHandle != oldChunk0Handle &&
               hitReplacedChunk && hitUnchangedNeighbor &&
               replacedChunkHit.instance == chunkTerrainOwner.get() &&
               unchangedNeighborHit.instance == chunkTerrainOwner.get() &&
               std::abs(replacedChunkHit.position.y - 68.0f) <= 0.02f &&
               std::abs(unchangedNeighborHit.position.y - 64.0f) <= 0.02f,
           "buildChunkPhysics atomically replaces one Chunk and preserves neighbor");

    auto shapeTerrainOwner = std::make_shared<Instance>("ShapeTerrainOwner");
    Chunk shapeChunk;
    shapeChunk.cx = 6;
    shapeChunk.cy = 1;
    shapeChunk.cz = 0;
    // Cube takes the triangle-mesh path. Its isolated top face is centered at
    // local (6,4,54); every other non-empty BlockShape takes ConvexBlock.
    shapeChunk.physVerts = {
        {4, 4, 52}, {8, 4, 52}, {8, 4, 56}, {4, 4, 56},
    };
    shapeChunk.physIndices = {0, 2, 1, 0, 3, 2};

    struct ShapeProbe {
        BlockShape shape;
        Vector3 localCenter;
    };
    std::vector<ShapeProbe> shapeProbes;
    for (int shapeValue = static_cast<int>(BlockShape::Wedge_TopNE);
         shapeValue <= static_cast<int>(BlockShape::Ramp_W);
         ++shapeValue) {
        const int probeIndex =
            shapeValue - static_cast<int>(BlockShape::Wedge_TopNE);
        const Vector3 center(
            6.0f + static_cast<float>(probeIndex % 5) * 12.0f,
            6.0f,
            6.0f + static_cast<float>(probeIndex / 5) * 12.0f);
        const BlockShape shape = static_cast<BlockShape>(shapeValue);
        shapeProbes.push_back({shape, center});
        shapeChunk.physConvexBlocks.push_back({
            static_cast<uint8_t>(shape), center});
    }
    buildChunkPhysics(shapeChunk, *physics, shapeTerrainOwner.get());
    expect(static_cast<bool>(shapeChunk.physicsHandle),
           "all BlockShape cache creates terrain handle");

    const Vector3 shapeChunkOrigin(
        static_cast<float>(shapeChunk.worldOriginX()) *
            TerrainStreamer::BLOCK_STUD_SIZE,
        static_cast<float>(shapeChunk.worldOriginY()) *
            TerrainStreamer::BLOCK_STUD_SIZE,
        static_cast<float>(shapeChunk.worldOriginZ()) *
            TerrainStreamer::BLOCK_STUD_SIZE);
    RaycastHit cubeShapeHit;
    const bool cubeShapeCollision = physics->raycast(
        shapeChunkOrigin + Vector3(6, 30, 54),
        {0, -1, 0}, 40.0f, cubeShapeHit);
    bool allShapeCollisions =
        cubeShapeCollision && cubeShapeHit.instance == shapeTerrainOwner.get();
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=block_shape"
              << " shape=Cube enum=" << static_cast<int>(BlockShape::Cube)
              << " hit=" << (cubeShapeCollision ? "true" : "false")
              << " hit_y=" << cubeShapeHit.position.y << "\n";

    std::vector<int> failedShapeValues;
    for (const ShapeProbe& probe : shapeProbes) {
        RaycastHit shapeHit;
        const bool hit = physics->raycast(
            shapeChunkOrigin + probe.localCenter + Vector3(0, 20, 0),
            {0, -1, 0}, 40.0f, shapeHit);
        const bool valid =
            hit && shapeHit.instance == shapeTerrainOwner.get();
        allShapeCollisions = allShapeCollisions && valid;
        if (!valid) failedShapeValues.push_back(static_cast<int>(probe.shape));
        std::cout << "[PhysicsMigrationRegression] backend=" << backend
                  << " metric=block_shape"
                  << " shape=" << migrationBlockShapeName(probe.shape)
                  << " enum=" << static_cast<int>(probe.shape)
                  << " hit=" << (valid ? "true" : "false")
                  << " hit_y=" << shapeHit.position.y << "\n";
    }
    if (!cubeShapeCollision)
        failedShapeValues.push_back(static_cast<int>(BlockShape::Cube));
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=block_shape_summary"
              << " tested=" << (shapeProbes.size() + 1)
              << " failed=" << failedShapeValues.size();
    for (int value : failedShapeValues) std::cout << " failed_enum=" << value;
    std::cout << "\n";
    expect(allShapeCollisions,
           "Cube/Wedge8/Tetra8/Ramp4 all produce backend collision");

    auto dynamicTarget = addMigrationCube(
        workspace, "DynamicCCDTarget", {-40, 140, -40}, {4, 4, 4});
    dynamicTarget->MassDensity = 50.0f;
    auto dynamicProjectile = addMigrationCube(
        workspace, "DynamicCCDProjectile", {-80, 140, -40}, {1, 1, 1});
    dynamicProjectile->CollisionDetection = CCDMode::Bullet;
    physics->update(*workspace, 0.0f);
    physics->setGravityEnabled(*dynamicTarget, false);
    physics->setGravityEnabled(*dynamicProjectile, false);

    int dynamicCcdContacts = 0;
    Physics::s_contactCallback = [&](BaseCube* first, BaseCube* second) {
        if ((first == dynamicTarget.get() && second == dynamicProjectile.get()) ||
            (first == dynamicProjectile.get() && second == dynamicTarget.get()))
            ++dynamicCcdContacts;
    };
    physics->setLinearVelocity(*dynamicProjectile, {400, 0, 0});
    bool dynamicCcdTunneledBeforeContact = false;
    for (int frame = 0; frame < 30; ++frame) {
        physics->update(*workspace, 1.0f / 60.0f);
        const float separation =
            dynamicProjectile->getWorldPosition().x -
            dynamicTarget->getWorldPosition().x;
        if (dynamicCcdContacts == 0 && separation > 2.5f)
            dynamicCcdTunneledBeforeContact = true;
    }
    Physics::s_contactCallback = {};
    const float dynamicCcdFinalSeparation =
        dynamicProjectile->getWorldPosition().x -
        dynamicTarget->getWorldPosition().x;
    const bool dynamicCcdCompletelyPassed =
        dynamicCcdFinalSeparation > 2.5f;
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=dynamic_dynamic_ccd"
              << " contacts=" << dynamicCcdContacts
              << " tunneled_before_contact="
              << (dynamicCcdTunneledBeforeContact ? "true" : "false")
              << " final_separation=" << dynamicCcdFinalSeparation
              << " completely_passed="
              << (dynamicCcdCompletelyPassed ? "true" : "false")
              << "\n";
    expect(dynamicCcdContacts > 0 &&
               !dynamicCcdTunneledBeforeContact &&
               !dynamicCcdCompletelyPassed,
           "Bullet 400 studs/s dynamic-dynamic CCD hits dynamic target");

    physics->destroyTerrain(shapeChunk.physicsHandle);
    shapeChunk.physicsHandle = {};
    physics->destroyTerrain(chunk0.physicsHandle);
    chunk0.physicsHandle = {};
    physics->destroyTerrain(chunk1.physicsHandle);
    chunk1.physicsHandle = {};
    physics->destroyTerrain(terrain);
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << "\n";
    return failures == 0 ? 0 : 1;
}

struct PhysicsPerformanceResult {
    bool available = false;
    bool guardPassed = false;
    PhysicsBackendType backend = PhysicsBackendType::PhysX;
    double medianMilliseconds = 0.0;
    double p95Milliseconds = 0.0;
    double maximumMilliseconds = 0.0;
};

PhysicsTerrainDescriptor makePerformanceTerrain(Instance* owner) {
    PhysicsTerrainDescriptor descriptor;
    descriptor.userData = owner;
    constexpr int tileCount = 16;
    constexpr float tileSize = 8.0f;
    descriptor.vertices.reserve(tileCount * tileCount * 4);
    descriptor.indices.reserve(tileCount * tileCount * 6);
    for (int x = 0; x < tileCount; ++x) {
        for (int z = 0; z < tileCount; ++z) {
            const float x0 = (x - tileCount / 2) * tileSize;
            const float z0 = (z - tileCount / 2) * tileSize;
            const uint32_t base = static_cast<uint32_t>(descriptor.vertices.size());
            descriptor.vertices.push_back({x0, 0, z0});
            descriptor.vertices.push_back({x0 + tileSize, 0, z0});
            descriptor.vertices.push_back({x0 + tileSize, 0, z0 + tileSize});
            descriptor.vertices.push_back({x0, 0, z0 + tileSize});
            descriptor.indices.insert(
                descriptor.indices.end(),
                {base, base + 2, base + 1, base, base + 3, base + 2});
        }
    }
    for (int i = 0; i < 32; ++i) {
        const float x = -60.0f + static_cast<float>(i % 8) * 16.0f;
        const float z = -60.0f + static_cast<float>(i / 8) * 40.0f;
        addTerrainBoxHull(descriptor, {x, 1.0f, z}, {2.0f, 1.0f, 2.0f});
    }
    return descriptor;
}

PhysicsPerformanceResult runPhysicsPerformanceWorkload() {
    PhysicsPerformanceResult result;
    auto workspace = std::make_shared<Workspace>();
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    result.backend = physics->getBackendType();
    result.available = physics->isAvailable();
    if (!result.available) return result;

    auto owner = std::make_shared<Instance>("PerformanceTerrainOwner");
    const PhysicsTerrainHandle terrain =
        physics->createTerrain(makePerformanceTerrain(owner.get()));
    if (!terrain) return result;

    std::vector<std::shared_ptr<BaseCube>> bodies;
    bodies.reserve(256);
    for (int i = 0; i < 256; ++i) {
        const int x = i % 16;
        const int z = (i / 16) % 16;
        auto body = addMigrationCube(
            workspace,
            "PerformanceBody" + std::to_string(i),
            Vector3(-45.0f + x * 6.0f, 5.0f + (i / 64) * 2.0f,
                    -45.0f + z * 6.0f),
            Vector3(0.75f, 0.75f, 0.75f));
        bodies.push_back(body);
    }
    for (int i = 0; i < 256; ++i) {
        auto rope = std::make_shared<Rope>(bodies[i], bodies[(i + 1) % bodies.size()]);
        rope->Name = "PerformanceConstraint" + std::to_string(i);
        rope->MaxDistance = (i + 1 == static_cast<int>(bodies.size())) ? 128.0f : 7.0f;
        rope->Stiffness = 0.0f;
        rope->Damping = 0.0f;
        workspace->addChild(rope);
    }

    physics->update(*workspace, 0.0f);
    auto stepPerformanceFrame = [&] {
        for (int substep = 0; substep < 4; ++substep)
            physics->update(*workspace, 1.0f / 240.0f);
    };
    for (int frame = 0; frame < 30; ++frame) stepPerformanceFrame();

    std::vector<double> frameMilliseconds;
    frameMilliseconds.reserve(120);
    for (int frame = 0; frame < 120; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        stepPerformanceFrame();
        const auto end = std::chrono::steady_clock::now();
        frameMilliseconds.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::vector<double> sorted = frameMilliseconds;
    std::sort(sorted.begin(), sorted.end());
    result.medianMilliseconds = sorted[sorted.size() / 2];
    result.p95Milliseconds = sorted[sorted.size() * 95 / 100];
    result.maximumMilliseconds = sorted.back();

    std::vector<double> firstHalf(
        frameMilliseconds.begin(),
        frameMilliseconds.begin() + frameMilliseconds.size() / 2);
    std::vector<double> secondHalf(
        frameMilliseconds.begin() + frameMilliseconds.size() / 2,
        frameMilliseconds.end());
    std::sort(firstHalf.begin(), firstHalf.end());
    std::sort(secondHalf.begin(), secondHalf.end());
    const double firstMedian = firstHalf[firstHalf.size() / 2];
    const double secondMedian = secondHalf[secondHalf.size() / 2];
    result.guardPassed =
        result.maximumMilliseconds <= 250.0 &&
        secondMedian <= firstMedian * 2.0 + 0.25;

    std::cout << "PHYSICS_PERF"
              << " backend=" << physicsBackendName(result.backend)
              << " median_ms=" << result.medianMilliseconds
              << " p95_ms=" << result.p95Milliseconds
              << " max_ms=" << result.maximumMilliseconds
              << " bodies=256 constraints=256"
              << " hz=60 substeps_equivalent=4"
              << " spiral_guard=" << (result.guardPassed ? "PASS" : "FAIL")
              << "\n";

    physics->destroyTerrain(terrain);
    return result;
}

bool configurePhysicsBackendForPerformance(const char* backend) {
    char program[] = "RecubinTest";
    char physxOption[] = "--physics=physx";
    char box3dOption[] = "--physics=box3d";
    char* arguments[] = {
        program,
        std::strcmp(backend, "box3d") == 0 ? box3dOption : physxOption
    };
    return Physics::configureBackendFromCommandLine(2, arguments);
}

int runPhysicsPerformanceGuard(int argc, char* argv[]) {
    const char* explicitBackend = nullptr;
    double referenceMilliseconds = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--physics=physx") explicitBackend = "physx";
        if (argument == "--physics=box3d") explicitBackend = "box3d";
        constexpr std::string_view prefix = "--physics-performance-reference-ms=";
        if (argument.starts_with(prefix)) {
            const std::string number(argument.substr(prefix.size()));
            char* end = nullptr;
            referenceMilliseconds = std::strtod(number.c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(referenceMilliseconds) ||
                referenceMilliseconds <= 0.0)
                referenceMilliseconds = 0.0;
        }
    }

    int failures = 0;
    auto reportResult = [&](const PhysicsPerformanceResult& result) {
        const char* backend = physicsBackendName(result.backend);
        const bool passed = result.available && result.guardPassed;
        std::cout << "[PhysicsPerformanceGuard] backend=" << backend << ' '
                  << (passed ? "PASS" : "FAIL")
                  << ": median and spiral-of-death guard\n";
        if (!passed) ++failures;
    };

    if (explicitBackend) {
        const PhysicsPerformanceResult selected = runPhysicsPerformanceWorkload();
        reportResult(selected);
        if (referenceMilliseconds > 0.0 &&
            selected.backend == PhysicsBackendType::Box3D) {
            const bool ratioPassed =
                selected.medianMilliseconds <= referenceMilliseconds * 1.5;
            std::cout << "[PhysicsPerformanceGuard] backend=box3d "
                      << (ratioPassed ? "PASS" : "FAIL")
                      << ": ratio=" << selected.medianMilliseconds / referenceMilliseconds
                      << " limit=1.5 reference_ms=" << referenceMilliseconds << "\n";
            if (!ratioPassed) ++failures;
        }
    } else {
        configurePhysicsBackendForPerformance("physx");
        const PhysicsPerformanceResult physxResult = runPhysicsPerformanceWorkload();
        reportResult(physxResult);
        configurePhysicsBackendForPerformance("box3d");
        const PhysicsPerformanceResult box3dResult = runPhysicsPerformanceWorkload();
        reportResult(box3dResult);
        const bool ratioPassed =
            physxResult.available && box3dResult.available &&
            box3dResult.medianMilliseconds <= physxResult.medianMilliseconds * 1.5;
        const double ratio = physxResult.medianMilliseconds > 0.0
            ? box3dResult.medianMilliseconds / physxResult.medianMilliseconds
            : std::numeric_limits<double>::infinity();
        std::cout << "[PhysicsPerformanceGuard] backend=box3d "
                  << (ratioPassed ? "PASS" : "FAIL")
                  << ": ratio=" << ratio << " limit=1.5\n";
        if (!ratioPassed) ++failures;
    }
    return failures == 0 ? 0 : 1;
}

float estimateFrequency(const std::vector<float>& samples, std::uint32_t sampleRate) {
    if (samples.size() < 4) return 0.0f;
    const std::size_t begin = samples.size() / 4;
    const std::size_t end = samples.size() * 3 / 4;
    std::size_t crossings = 0;
    for (std::size_t i = begin + 1; i < end; ++i) {
        if (samples[i - 1] <= 0.0f && samples[i] > 0.0f) ++crossings;
    }
    const double seconds = static_cast<double>(end - begin) / sampleRate;
    return seconds > 0.0 ? static_cast<float>(crossings / seconds) : 0.0f;
}

int runSoundStretchRegression() {
    constexpr std::uint32_t sampleRate = 48000;
    constexpr float frequency = 440.0f;
    constexpr std::size_t inputFrames = sampleRate * 2;
    constexpr double pi = 3.14159265358979323846;

    std::vector<float> input(inputFrames);
    for (std::size_t frame = 0; frame < inputFrames; ++frame) {
        input[frame] = std::sin(
            static_cast<float>(2.0 * pi * frequency * frame / sampleRate));
    }

    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        std::cout << "[SoundStretchRegression] "
                  << (condition ? "PASS" : "FAIL") << ": " << name << "\n";
        if (!condition) ++failures;
    };
    CommandHistory liveEditHistory;
    bool liveEditNotified = false;
    liveEditHistory.setOnChange([&liveEditNotified] { liveEditNotified = true; });
    liveEditHistory.notifyChanged();
    expect(liveEditNotified && !liveEditHistory.canUndo(),
           "ライブ編集通知はdirtyを中継してUndo履歴を増やさない");

    for (float speed : {2.0f, 0.5f}) {
        std::vector<float> output;
        const bool processed = TimeStretchNode::processOffline(
            input, 1, sampleRate, speed, output);
        const std::size_t expectedFrames = static_cast<std::size_t>(
            std::llround(static_cast<double>(inputFrames) / speed));
        const bool finite = std::all_of(output.begin(), output.end(),
                                        [](float value) { return std::isfinite(value); });
        const float estimated = estimateFrequency(output, sampleRate);

        expect(processed, speed == 2.0f
            ? "Speed=2.0を処理できる"
            : "Speed=0.5を処理できる");
        expect(output.size() == expectedFrames, speed == 2.0f
            ? "Speed=2.0で出力時間が半分になる"
            : "Speed=0.5で出力時間が倍になる");
        expect(finite, speed == 2.0f
            ? "Speed=2.0の全出力がfinite"
            : "Speed=0.5の全出力がfinite");
        expect(std::abs(estimated - frequency) <= 20.0f, speed == 2.0f
            ? "Speed=2.0で440Hz付近を維持する"
            : "Speed=0.5で440Hz付近を維持する");
        std::cout << "[SoundStretchRegression] Speed=" << speed
                  << " frames=" << output.size()
                  << " estimatedHz=" << estimated << "\n";
    }

    double remainder = 0.0;
    std::uint64_t consumedFrames = 0;
    constexpr double fractionalSpeed = 1.3;
    constexpr std::uint32_t blockFrames = 127;
    constexpr std::uint32_t blockCount = 10000;
    for (std::uint32_t block = 0; block < blockCount; ++block) {
        consumedFrames += TimeStretchNode::calculateInputFrameCount(
            blockFrames, static_cast<float>(fractionalSpeed), remainder);
    }
    const double expectedInput =
        static_cast<double>(blockFrames) * blockCount * fractionalSpeed;
    expect(std::abs(static_cast<double>(consumedFrames) - expectedInput) <= 1.0,
           "複数ブロックのフレーム比累積誤差が1フレーム以内");
    expect(TimeStretchNode::clampSpeed(-1.0f) == 0.25f &&
           TimeStretchNode::clampSpeed(10.0f) == 4.0f &&
           TimeStretchNode::clampSpeed(
               std::numeric_limits<float>::quiet_NaN()) == 1.0f,
           "Speedを0.25〜4.0へクランプする");
    static AudioService* uninitializedAudioService = new AudioService();
    Sound unloadedSound(*uninitializedAudioService);
    unloadedSound.setSpeed(-1.0f);
    const bool clampsLow = unloadedSound.getSpeed() == 0.25f;
    unloadedSound.setSpeed(10.0f);
    const bool clampsHigh = unloadedSound.getSpeed() == 4.0f;
    unloadedSound.setSpeed(std::numeric_limits<float>::quiet_NaN());
    const bool handlesNaN = unloadedSound.getSpeed() == 1.0f;
    expect(clampsLow && clampsHigh && handlesNaN,
           "Sound::setSpeed/getSpeed経路でもSpeedをクランプする");
    unloadedSound.setVolume(-1.0f);
    expect(unloadedSound.getVolume() == 0.0f,
           "Sound::setVolume/getVolume経路でVolumeの下限を0にクランプする");
    unloadedSound.setVolume(10.0f);
    expect(unloadedSound.getVolume() == 8.0f,
           "Sound::setVolume/getVolume経路でVolumeの上限を8にクランプする");
    unloadedSound.setVolume(std::numeric_limits<float>::quiet_NaN());
    expect(unloadedSound.getVolume() == 1.0f,
           "Sound::setVolume/getVolume経路で非finite値を1に戻す");

    std::cout << "[SoundStretchRegression] "
              << (failures == 0 ? "PASS" : "FAIL") << "\n";
    return failures == 0 ? 0 : 1;
}

int runNatCodecRegression() {
    using namespace NatProtocol;
    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        std::cout << "[NatCodecRegression] "
                  << (condition ? "PASS" : "FAIL") << ": " << name << "\n";
        if (!condition) ++failures;
    };

    const StunTransactionId transactionId{
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB
    };
    const auto request = encodeStunBindingRequest(transactionId);
    expect(request.size() == 20 && request[0] == 0x00 && request[1] == 0x01 &&
               request[4] == 0x21 && request[5] == 0x12 &&
               std::equal(transactionId.begin(), transactionId.end(), request.begin() + 8),
           "RFC8489 Binding requestをnetwork byte orderで生成する");

    constexpr uint16_t mappedPort = 54321;
    constexpr std::array<uint8_t, 4> mappedIp{203, 0, 113, 7};
    const uint16_t xorPort = mappedPort ^ 0x2112u;
    std::vector<uint8_t> response{
        0x01, 0x01, 0x00, 0x14, 0x21, 0x12, 0xA4, 0x42
    };
    response.insert(response.end(), transactionId.begin(), transactionId.end());
    response.insert(response.end(), {0x80, 0x22, 0x00, 0x01, 0x99, 0x00, 0x00, 0x00});
    response.insert(response.end(), {
        0x00, 0x20, 0x00, 0x08, 0x00, 0x01,
        static_cast<uint8_t>(xorPort >> 8), static_cast<uint8_t>(xorPort),
        static_cast<uint8_t>(mappedIp[0] ^ 0x21u),
        static_cast<uint8_t>(mappedIp[1] ^ 0x12u),
        static_cast<uint8_t>(mappedIp[2] ^ 0xA4u),
        static_cast<uint8_t>(mappedIp[3] ^ 0x42u)
    });

    NetworkCandidate mapped;
    const auto stunResult =
        decodeStunBindingResponse(response.data(), response.size(), transactionId, mapped);
    std::array<uint8_t, 4> storedIp{};
    std::memcpy(storedIp.data(), &mapped.host, storedIp.size());
    expect(stunResult == DecodeResult::Ok && mapped.port == mappedPort &&
               mapped.type == CandidateType::ServerReflexive && storedIp == mappedIp,
           "未知属性を読み飛ばしIPv4 XOR-MAPPED-ADDRESSをENet表現へ復号する");

    auto wrongTransaction = transactionId;
    wrongTransaction[0] ^= 0xFFu;
    expect(decodeStunBindingResponse(response.data(), response.size(), wrongTransaction, mapped) ==
               DecodeResult::WrongTransaction,
           "異なるSTUN transaction IDを拒否する");
    expect(decodeStunBindingResponse(response.data(), response.size() - 1, transactionId, mapped) ==
               DecodeResult::InvalidLength,
           "切り詰められたSTUN応答を拒否する");
    auto ipv6Response = response;
    ipv6Response[33] = 0x02;
    expect(decodeStunBindingResponse(
               ipv6Response.data(), ipv6Response.size(), transactionId, mapped) ==
               DecodeResult::UnsupportedAddressFamily,
           "IPv6 XOR-MAPPED-ADDRESSを明示的に拒否する");

    uint32_t candidateHost = 0;
    std::memcpy(&candidateHost, mappedIp.data(), mappedIp.size());
    const std::vector<NetworkCandidate> sourceCandidates{
        {CandidateType::Local, candidateHost, 40000},
        {CandidateType::ServerReflexive, candidateHost, mappedPort}
    };
    std::vector<uint8_t> candidateBytes;
    std::vector<NetworkCandidate> decodedCandidates;
    expect(encodeCandidates(sourceCandidates, candidateBytes) &&
               decodeCandidates(candidateBytes.data(), candidateBytes.size(), decodedCandidates) ==
                   DecodeResult::Ok &&
               decodedCandidates == sourceCandidates,
           "候補一覧を境界検証付きで往復する");
    std::vector<NetworkCandidate> excessiveCandidates(MAX_CANDIDATES + 1);
    expect(!encodeCandidates(excessiveCandidates, candidateBytes),
           "上限を超える候補一覧を拒否する");

    RendezvousPacket rendezvous;
    rendezvous.type = RendezvousMessageType::CookieRequest;
    rendezvous.transactionId = 0x12345678u;
    rendezvous.payload.assign(16, 0x5Au);
    std::vector<uint8_t> rendezvousBytes;
    RendezvousPacket decodedRendezvous;
    expect(encodeRendezvousPacket(rendezvous, rendezvousBytes) &&
               decodeRendezvousPacket(
                   rendezvousBytes.data(), rendezvousBytes.size(), decodedRendezvous) ==
                   DecodeResult::Ok &&
               decodedRendezvous.transactionId == rendezvous.transactionId &&
               decodedRendezvous.payload == rendezvous.payload,
           "ランデブーフレームをnetwork byte orderで往復する");
    rendezvousBytes[4] = RENDEZVOUS_VERSION + 1;
    expect(decodeRendezvousPacket(
               rendezvousBytes.data(), rendezvousBytes.size(), decodedRendezvous) ==
               DecodeResult::UnsupportedVersion,
           "未知のランデブープロトコルversionを拒否する");

    PunchPacket punch;
    punch.type = PunchMessageType::Probe;
    punch.nonce = 0x0102030405060708ull;
    punch.senderPeerId = 42;
    punch.roomEpoch = 7;
    for (size_t i = 0; i < punch.token.size(); ++i) {
        punch.token[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> punchBytes;
    PunchPacket decodedPunch;
    expect(encodePunchPacket(punch, punchBytes) &&
               decodePunchPacket(punchBytes.data(), punchBytes.size(), decodedPunch) ==
                   DecodeResult::Ok &&
               decodedPunch.nonce == punch.nonce && decodedPunch.token == punch.token &&
               decodedPunch.senderPeerId == punch.senderPeerId &&
               decodedPunch.roomEpoch == punch.roomEpoch,
           "参加token付きパンチpacketを境界検証付きで往復する");
    expect(decodePunchPacket(punchBytes.data(), punchBytes.size() - 1, decodedPunch) ==
               DecodeResult::Truncated,
           "切り詰められたパンチpacketを拒否する");

    return failures == 0 ? 0 : 1;
}
}

// ===================================================
//  RecubinTest: GUI操作不要のヘッドレスLuauテストランナー
//  GLFW/OpenGL/Renderer/PhysXを構築せず、シーンYAML内のScriptを実行して
//  print()の [PASS]/[FAIL]/[ERROR] 件数から終了コードを決める。
// ===================================================
int main(int argc, char* argv[]) {
    getPlatform().setupConsoleUtf8();
    getPlatform().setupDllSearchPath();
    // Physics test mode names also start with "--physics". Pass only the actual
    // selector through the existing parser so those mode names are not mistaken
    // for malformed backend options.
    std::vector<char*> physicsArguments{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--physics" || argument.starts_with("--physics="))
            physicsArguments.push_back(argv[i]);
    }
    if (!Physics::configureBackendFromCommandLine(
            static_cast<int>(physicsArguments.size()), physicsArguments.data()))
        return -1;

    const bool weldRegression = argc > 1 && std::string_view(argv[1]) == "--weld-regression";
    const bool toolWeldRegression = argc > 1 && std::string_view(argv[1]) == "--tool-weld-regression";
    const bool toolWeldReequipRegression = argc > 1 && std::string_view(argv[1]) == "--tool-weld-reequip-regression";
    const bool toolRespawnRegression = argc > 1 && std::string_view(argv[1]) == "--tool-respawn-regression";
    const bool inventoryToolSyncRegression = argc > 1 && std::string_view(argv[1]) == "--inventory-tool-sync-regression";
    const bool humanoidPartRefRegression = argc > 1 && std::string_view(argv[1]) == "--humanoid-part-ref-regression";
    const bool soundStretchRegression = argc > 1 && std::string_view(argv[1]) == "--sound-stretch-regression";
    const bool natCodecRegression = argc > 1 && std::string_view(argv[1]) == "--nat-codec-regression";
    bool physicsMigrationRegression = false;
    bool physicsPerformanceGuard = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        physicsMigrationRegression =
            physicsMigrationRegression || argument == "--physics-migration-regression";
        physicsPerformanceGuard =
            physicsPerformanceGuard || argument == "--physics-performance-guard";
    }
    if (physicsMigrationRegression) return runPhysicsMigrationRegression();
    if (physicsPerformanceGuard) return runPhysicsPerformanceGuard(argc, argv);
    if (toolWeldRegression) return runToolWeldRegression();
    if (toolWeldReequipRegression) return runToolWeldReequipRegression();
    if (toolRespawnRegression) return runToolRespawnRegression();
    if (inventoryToolSyncRegression) return runInventoryToolSyncRegression();
    if (humanoidPartRefRegression) return runHumanoidPartRefRegression();
    if (soundStretchRegression) return runSoundStretchRegression();
    if (natCodecRegression) return runNatCodecRegression();
    const char* sceneArgument = findSceneArgument(argc, argv, weldRegression ? 2 : 1);
    std::string scenePath = sceneArgument
        ? sceneArgument
        : (weldRegression ? "assets/scenes/_snapshot.yaml" : "assets/scenes/test_bindings.yaml");

    std::vector<std::string> expectedErrors;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--expect-error" && i + 1 < argc)
            expectedErrors.emplace_back(argv[++i]);
    }
    std::vector<bool> expectedErrorSeen(expectedErrors.size(), false);

    int passCount = 0;
    int failCount = 0;
    g_luauLogHook = [&](const std::string& msg) {
        if (msg.find("[FAIL]") != std::string::npos) {
            failCount++;
        } else if (msg.find("[ERROR]") != std::string::npos) {
            bool expected = false;
            for (size_t i = 0; i < expectedErrors.size(); ++i) {
                if (!expectedErrorSeen[i] && msg.find(expectedErrors[i]) != std::string::npos) {
                    expectedErrorSeen[i] = true;
                    expected = true;
                    break;
                }
            }
            if (!expected) failCount++;
        } else if (msg.find("[PASS]") != std::string::npos) {
            passCount++;
        }
    };

    auto audioService = std::make_unique<AudioService>();
    audioService->initialize();

    auto system    = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    auto lighting  = std::make_shared<Lighting>();
    lighting->Name = "Lighting";
    system->addChild(workspace);
    workspace->addChild(lighting);

    SceneLoader::registerSingleton("System", system);
    SceneLoader::registerSingleton("Workspace", workspace);
    SceneLoader::loadScene(scenePath);
    SceneLoader::clearSingletons();

    // 古い形式のYAML対応: System直下のLightingを見つけたら、WorkspaceのLightingにプロパティを移して削除
    for (auto it = system->children.begin(); it != system->children.end(); ) {
        if (it->second->IsA("Lighting")) {
            auto oldLighting = std::static_pointer_cast<Lighting>(it->second);
            lighting->lightDir   = oldLighting->lightDir;
            lighting->brightness = oldLighting->brightness;
            it = system->children.erase(it);
            break;
        } else {
            ++it;
        }
    }

    if (weldRegression) {
        audioService->stopAllSounds();
        return runWeldRegression(workspace);
    }

    auto luauEngine = std::make_unique<LuauEngine>();
    luauEngine->setGlobalInstance(workspace->Name, workspace);
    luauEngine->setGlobalInstance("workspace", workspace);
    luauEngine->setGlobalInstance("System", system);
    luauEngine->setWorkspace(workspace);
    luauEngine->setSystem(system.get());

    luauEngine->executeWorkspaceScripts(*workspace);
    luauEngine->executeSystemScripts(); // Workspace外(System配下)のスクリプトも実行対象

    // wait() で休止しているスクリプトの完了をタイムアウト付きで待つ
    const auto timeoutDuration = std::chrono::seconds(10);
    auto startTime = std::chrono::steady_clock::now();
    auto lastTime  = startTime;
    auto hasPending = [](const std::vector<std::shared_ptr<Instance>>& scripts) {
        for (auto& inst : scripts) {
            auto script = std::dynamic_pointer_cast<Script>(inst);
            if (script && script->Enabled && !script->Completed && !script->Aborted) {
                return true;
            }
        }
        return false;
    };
    while (true) {
        bool anyPending = hasPending(workspace->scripts) || hasPending(system->scripts);
        if (!anyPending) break;

        if (std::chrono::steady_clock::now() - startTime > timeoutDuration) {
            std::cout << "[RecubinTest] Timeout waiting for scripts to finish.\n";
            failCount++;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        luauEngine->update(dt);
    }

    audioService->stopAllSounds();

    for (size_t i = 0; i < expectedErrors.size(); ++i) {
        if (!expectedErrorSeen[i]) {
            std::cout << "[RecubinTest] Expected Luau error was not observed: " << expectedErrors[i] << "\n";
            failCount++;
        }
    }

    std::cout << "[RecubinTest] " << passCount << " passed, " << failCount << " failed.\n";
    return failCount > 0 ? 1 : 0;
}
