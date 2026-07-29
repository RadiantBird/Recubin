#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Script.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Model.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Tool.hpp>

#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/AudioService.hpp>
#include <Core/TimeStretchNode.hpp>
#include <Core/NullInputBackend.hpp>
#include <Core/Physics.hpp>
#include <Core/User.hpp>
#include <Editor/CommandHistory.hpp>
#include <Network/NatProtocol.hpp>
#include <Util/Logger.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
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
            if (worst->cube->actor) {
                const auto actorPose = worst->cube->actor->getGlobalPose();
                const auto& offset = worst->cube->m_compoundLocalOffset;
                std::cout << " actor=[" << actorPose.p.x << ',' << actorPose.p.y << ',' << actorPose.p.z << ']'
                          << " actorQ=[" << actorPose.q.x << ',' << actorPose.q.y << ',' << actorPose.q.z << ',' << actorPose.q.w << ']'
                          << " offset=[" << offset.p.x << ',' << offset.p.y << ',' << offset.p.z << ']'
                          << " offsetQ=[" << offset.q.x << ',' << offset.q.y << ',' << offset.q.z << ',' << offset.q.w << ']';
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
    expect(handle->actor && handle->actor == blade->actor && handle->actor == guard->actor,
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
        return handle->actor && handle->actor == member1->actor &&
               handle->actor == member2->actor && handle->actor == member3->actor;
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
    expect(!handle->actor && !member1->actor && !member2->actor && !member3->actor,
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
    expect(handle->actor && handle->actor == member->actor,
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
    expect(handle->actor && handle->actor == member->actor,
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

    const bool weldRegression = argc > 1 && std::string_view(argv[1]) == "--weld-regression";
    const bool toolWeldRegression = argc > 1 && std::string_view(argv[1]) == "--tool-weld-regression";
    const bool toolWeldReequipRegression = argc > 1 && std::string_view(argv[1]) == "--tool-weld-reequip-regression";
    const bool toolRespawnRegression = argc > 1 && std::string_view(argv[1]) == "--tool-respawn-regression";
    const bool inventoryToolSyncRegression = argc > 1 && std::string_view(argv[1]) == "--inventory-tool-sync-regression";
    const bool humanoidPartRefRegression = argc > 1 && std::string_view(argv[1]) == "--humanoid-part-ref-regression";
    const bool soundStretchRegression = argc > 1 && std::string_view(argv[1]) == "--sound-stretch-regression";
    const bool natCodecRegression = argc > 1 && std::string_view(argv[1]) == "--nat-codec-regression";
    if (toolWeldRegression) return runToolWeldRegression();
    if (toolWeldReequipRegression) return runToolWeldReequipRegression();
    if (toolRespawnRegression) return runToolRespawnRegression();
    if (inventoryToolSyncRegression) return runInventoryToolSyncRegression();
    if (humanoidPartRefRegression) return runHumanoidPartRefRegression();
    if (soundStretchRegression) return runSoundStretchRegression();
    if (natCodecRegression) return runNatCodecRegression();
    std::string scenePath = weldRegression
        ? ((argc > 2) ? argv[2] : "assets/scenes/_snapshot.yaml")
        : ((argc > 1) ? argv[1] : "assets/scenes/test_bindings.yaml");

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
