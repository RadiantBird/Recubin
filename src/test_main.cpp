#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/Script.hpp>
#include <Instances/LocalScript.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Model.hpp>
#include <Instances/Folder.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/Force.hpp>
#include <Instances/Motor.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Tool.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Users.hpp>
#include <Instances/StarterCharacter.hpp>
#include <Instances/SpawnLocation.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Sun.hpp>
#include <Instances/FileRef.hpp>
#include <Instances/FontFile.hpp>
#include <Instances/TextFile.hpp>
#include <Instances/TextLabel.hpp>
#include <Instances/TextButton.hpp>
#include <Instances/ImageButton.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/SurfaceMark.hpp>
#include <Instances/Highlight.hpp>
#include <Instances/IntValue.hpp>
#include <Util/YamlLoadResult.hpp>
#include <stb_image.h>

#include <Core/LuauEngine.hpp>
#include <Core/FileLoader.hpp>
#include <Core/Packager.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/AnimationClip.hpp>
#include <Core/SceneRuntime.hpp>
#include <Core/AudioService.hpp>
#include <Core/CharacterRig.hpp>
#include <Core/BaseCubeFactory.hpp>
#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Core/TimeStretchNode.hpp>
#include <Core/NullInputBackend.hpp>
#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/Terrain.hpp>
#include <Core/TerrainStreamer.hpp>
#include <Core/User.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/GuiAutomationCommand.hpp>
#include <Editor/SceneHierarchyGrouping.hpp>
#include <Editor/SceneHierarchySelection.hpp>
#include <Editor/InstanceCatalog.hpp>
#include <Editor/EditorManager.hpp>
#include <Editor/ViewportGeometry.hpp>
#include <Editor/ViewportSceneQueries.hpp>
#include <Instances/ObjectValue.hpp>
#include <Network/ByteStream.hpp>
#include <Network/NatProtocol.hpp>
#include <Network/NetworkManager.hpp>
#include <Network/Replication.hpp>
#include <Util/Logger.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/PngWriter.hpp>
#include <Util/AssetPath.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <Util/MockPlatform.hpp>
#include <Util/RuntimeLaunchArgs.hpp>
#include <Util/RuntimeFileSystem.hpp>
#include <Util/UUID.hpp>
#include <Util/SystemExtensionPermissions.hpp>
#ifdef __APPLE__
#include <Util/MacPlatform.hpp>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

static int runAnimationClipRegression();
static int runSurfaceMarkRegression();

#ifdef near
#undef near
#endif

struct ReplicationTestAccess {
    static bool spawnRemoteAvatar(ReplicationManager& replication, PeerId id) {
        replication.spawnRemoteAvatar(id);
        return replication.m_remoteAvatars.contains(id);
    }

    static void setLatestPose(
        ReplicationManager& replication, PeerId id, const CFrame& pose) {
        replication.m_latestPoses[id] = pose;
    }

    static void applyAvatarPoses(ReplicationManager& replication, float dt) {
        replication.applyAvatarPoses(dt);
    }

    static std::shared_ptr<Model> model(
        ReplicationManager& replication, PeerId id) {
        auto it = replication.m_remoteAvatars.find(id);
        return it == replication.m_remoteAvatars.end() ? nullptr : it->second.model;
    }

    static std::shared_ptr<User> identity(
        ReplicationManager& replication, PeerId id) {
        auto it = replication.m_remoteAvatars.find(id);
        return it == replication.m_remoteAvatars.end() ? nullptr : it->second.identity;
    }

    static bool hasPendingPhysicsRegistration(
        ReplicationManager& replication, PeerId id) {
        auto it = replication.m_remoteAvatars.find(id);
        return it != replication.m_remoteAvatars.end() &&
            replication.hasPendingPhysicsRegistration(it->second);
    }
};

namespace {
class HullRegressionCube final : public BaseCube {
public:
    std::vector<Vector3> vertices;

    HullRegressionCube(
        Vector3 position, Vector3 size, std::vector<Vector3> source)
        : BaseCube(position, size), vertices(std::move(source)) {}

    PhysicsShape getPhysicsShape() const override {
        return PhysicsShape::ConvexMesh;
    }
    std::vector<Vector3> getConvexVertices() const override { return vertices; }
};

class FrameRateTestInputBackend final : public IInputBackend {
public:
    std::unordered_set<KeyCode> pressedKeys;
    std::unordered_set<MouseButton> pressedMouseButtons;
    double scrollDelta = 0.0;
    double cursorX = 0.0, cursorY = 0.0;
    bool mouseCaptured = false;

    bool isKeyDown(KeyCode key) const override { return pressedKeys.contains(key); }
    bool isMouseButtonDown(MouseButton button) const override {
        return pressedMouseButtons.contains(button);
    }
    void getCursorPos(double& x, double& y) const override { x = cursorX; y = cursorY; }
    void setCursorPos(double x, double y) override { cursorX = x; cursorY = y; }
    void setMouseCaptured(bool captured) override { mouseCaptured = captured; }
    double consumeScrollDelta() override {
        const double result = scrollDelta;
        scrollDelta = 0.0;
        return result;
    }
};

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

std::vector<Vector3> makeDenseHullVertices(int count = 96) {
    std::vector<Vector3> result;
    result.reserve(static_cast<std::size_t>(count));
    constexpr float GOLDEN_ANGLE = 2.39996323f;
    for (int index = 0; index < count; ++index) {
        const float y = 1.0f -
            2.0f * (static_cast<float>(index) + 0.5f) /
                static_cast<float>(count);
        const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float angle = GOLDEN_ANGLE * static_cast<float>(index);
        result.push_back({
            std::cos(angle) * radius * 0.5f,
            y * 0.5f,
            std::sin(angle) * radius * 0.5f,
        });
    }
    return result;
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

    user->spawnCharacter(system.get(), workspace.get());
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

int runStarterWeldRenameRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[StarterWeldRename] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };
    auto containsInstance = [](const std::vector<std::shared_ptr<BaseCube>>& values,
                               const BaseCube* expected) {
        return std::any_of(values.begin(), values.end(),
                           [expected](const auto& value) {
                               return value.get() == expected;
                           });
    };

    auto system = std::make_shared<System>();
    auto starter = std::make_shared<StarterCharacter>();
    auto hair = std::make_shared<Cube>(Vector3{}, Vector3(1, 1, 1), 0);
    auto head = std::make_shared<Sphere>(Vector3{}, Vector3(1, 1, 1));
    hair->Name = "Hair";
    head->Name = "Head";
    system->addChild(starter);
    starter->addChild(hair);
    starter->addChild(head);
    auto weld = std::make_shared<Weld>(hair, head);
    hair->addChild(weld);

    hair->renameTo("HairTemporary");
    head->renameTo("HeadTemporary");
    auto hairCollision = std::make_shared<Cube>(
        Vector3{}, Vector3(1, 1, 1), 0);
    auto headCollision = std::make_shared<Sphere>(
        Vector3{}, Vector3(1, 1, 1));
    hairCollision->Name = "Hair";
    headCollision->Name = "Head";
    starter->addChild(hairCollision);
    starter->addChild(headCollision);
    hair->renameTo("Hair");
    head->renameTo("Head");
    expect(hair->Name == "Hair1" && head->Name == "Head1" &&
               starter->getChild("Hair1") == hair.get() &&
               starter->getChild("Head1") == head.get(),
           "collision renames update instance names and parent lookup keys");

    hair->renameTo("Ponytail");
    head->renameTo("Face");
    hair->renameTo("Hair");
    head->renameTo("Head");
    weld->refreshRefNames();
    expect(hair->Name == "Hair1" && head->Name == "Head1" &&
               weld->m_cube0Name == "StarterCharacter\\Hair1" &&
               weld->m_cube1Name == "StarterCharacter\\Head1",
           "save-time refresh follows repeated and collision-renamed parts");

    const auto savePath = std::filesystem::temp_directory_path() /
        ("recubin_starter_weld_rename_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".yaml");
    SceneLoader::saveScene(system.get(), savePath.string());
    const std::string saved = FileLoader::readText(savePath.string());
    std::error_code removeError;
    std::filesystem::remove(savePath, removeError);
    expect(saved.find("Cube0: StarterCharacter\\Hair1") != std::string::npos &&
               saved.find("Cube1: StarterCharacter\\Head1") != std::string::npos,
           "scene serialization writes refreshed StarterCharacter paths");

    auto character = User::buildCharacterModel(system.get(), "PlayerCharacter");
    std::shared_ptr<BaseCube> clonedHair;
    std::shared_ptr<BaseCube> clonedHead;
    if (character) {
        if (auto it = character->children.find("Hair1");
            it != character->children.end())
            clonedHair = std::dynamic_pointer_cast<BaseCube>(it->second);
        if (auto it = character->children.find("Head1");
            it != character->children.end())
            clonedHead = std::dynamic_pointer_cast<BaseCube>(it->second);
    }
    const auto clonedAssembly = clonedHair
        ? Weld::collectAssembly(clonedHair, *character)
        : std::vector<std::shared_ptr<BaseCube>>{};
    expect(clonedHair && clonedHead &&
               containsInstance(clonedAssembly, clonedHair.get()) &&
               containsInstance(clonedAssembly, clonedHead.get()) &&
               !containsInstance(clonedAssembly, hair.get()) &&
               !containsInstance(clonedAssembly, head.get()),
           "character clone Weld targets renamed clone parts, not template parts");

    auto localHair = starter->children.contains("Hair1")
        ? std::dynamic_pointer_cast<BaseCube>(starter->children.at("Hair1"))
        : nullptr;
    auto localHead = starter->children.contains("Head1")
        ? std::dynamic_pointer_cast<BaseCube>(starter->children.at("Head1"))
        : nullptr;
    const auto localAssembly = localHair
        ? Weld::collectAssembly(localHair, *starter)
        : std::vector<std::shared_ptr<BaseCube>>{};
    expect(localHair && localHead && containsInstance(localAssembly, localHead.get()),
           "self-contained StarterCharacter Hair/Head paths resolve to a Weld assembly");

    std::cout << "[StarterWeldRename] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runStarterAccessoryWeldRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[StarterAccessoryWeld] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };
    auto cframeNear = [](const CFrame& first, const CFrame& second) {
        const float rotationDot = std::abs(
            first.Rotation.w * second.Rotation.w +
            first.Rotation.x * second.Rotation.x +
            first.Rotation.y * second.Rotation.y +
            first.Rotation.z * second.Rotation.z);
        return positionDistance(first.Position, second.Position) < 0.002f &&
               std::abs(1.0f - rotationDot) < 0.002f;
    };
    auto contains = [](const std::vector<std::shared_ptr<BaseCube>>& assembly,
                       const BaseCube* expected) {
        return std::any_of(assembly.begin(), assembly.end(),
            [expected](const auto& member) { return member.get() == expected; });
    };
    auto finiteVector = [](const Vector3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    auto finiteCFrame = [&](const CFrame& value) {
        const Quaternion& rotation = value.Rotation;
        return finiteVector(value.Position) && std::isfinite(rotation.w) &&
               std::isfinite(rotation.x) && std::isfinite(rotation.y) &&
               std::isfinite(rotation.z);
    };
    auto vectorLength = [](const Vector3& value) {
        return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    };

    auto system = std::make_shared<System>();
    auto starter = std::make_shared<StarterCharacter>();
    CharacterRig::buildDefaultRigParts(starter);
    auto templateHeadIt = starter->children.find("Head");
    auto templateHead = templateHeadIt != starter->children.end()
        ? std::dynamic_pointer_cast<BaseCube>(templateHeadIt->second)
        : nullptr;
    if (!templateHead) {
        expect(false, "default StarterCharacter contains Head");
        return 1;
    }
    auto templateHair = std::make_shared<Cube>(Vector3{}, Vector3(1.5f, 0.8f, 1.4f), 0);
    templateHair->Name = "Hair";
    templateHair->Anchored = true;
    templateHair->CanCollide = false;
    const CFrame initialHairRelative(
        Vector3(0.2f, 0.85f, 0.1f),
        Quaternion::fromAxisAngle(Vector3(0, 1, 0), 17.0f));
    templateHair->cframe = templateHead->cframe * initialHairRelative;
    starter->addChild(templateHair);
    auto templateWeld = std::make_shared<Weld>(templateHead, templateHair);
    templateWeld->Name = "HairWeld";
    templateHead->addChild(templateWeld);

    auto templateGlasses = std::make_shared<Cube>(Vector3{}, Vector3(1.7f, 0.3f, 0.3f), 0);
    templateGlasses->Name = "Glasses";
    templateGlasses->Anchored = true;
    templateGlasses->CanCollide = false;
    const CFrame initialGlassesRelative(
        Vector3(0.0f, 0.05f, -0.62f),
        Quaternion::fromAxisAngle(Vector3(0, 1, 0), -9.0f));
    templateGlasses->cframe = templateHead->cframe * initialGlassesRelative;
    starter->addChild(templateGlasses);
    auto glassesWeld = std::make_shared<Weld>(templateHead, templateGlasses);
    glassesWeld->Name = "GlassesWeld";
    templateHead->addChild(glassesWeld);
    system->addChild(starter);

    auto first = User::buildCharacterModel(system.get(), "CharacterA");
    auto second = User::buildCharacterModel(system.get(), "CharacterB");
    auto workspace = std::make_shared<Workspace>();
    workspace->Name = "StarterAccessoryWeldRegression";
    workspace->Gravity = Vector3(0, 0, 0);
    system->addChild(workspace);
    workspace->addChild(first);
    workspace->addChild(second);

    struct RigParts {
        std::shared_ptr<Humanoid> humanoid;
        std::shared_ptr<BaseCube> root;
        std::shared_ptr<BaseCube> head;
        std::shared_ptr<BaseCube> hair;
        std::shared_ptr<BaseCube> glasses;
        std::vector<std::shared_ptr<BaseCube>> bodyParts;
    };
    auto getParts = [](const std::shared_ptr<Model>& model) {
        RigParts parts;
        if (!model) return parts;
        auto find = [&](const char* name) -> std::shared_ptr<Instance> {
            auto it = model->children.find(name);
            return it != model->children.end() ? it->second : nullptr;
        };
        parts.humanoid = std::dynamic_pointer_cast<Humanoid>(find("Humanoid"));
        parts.root = std::dynamic_pointer_cast<BaseCube>(find("Root"));
        parts.head = std::dynamic_pointer_cast<BaseCube>(find("Head"));
        parts.hair = std::dynamic_pointer_cast<BaseCube>(find("Hair"));
        parts.glasses = std::dynamic_pointer_cast<BaseCube>(find("Glasses"));
        constexpr std::array<const char*, 9> partNames = {
            "Root", "Torso", "Head", "LeftArm", "RightArm",
            "LeftLeg", "RightLeg", "Hair", "Glasses"
        };
        for (const char* name : partNames) {
            if (auto part = std::dynamic_pointer_cast<BaseCube>(find(name)))
                parts.bodyParts.push_back(std::move(part));
        }
        return parts;
    };
    RigParts a = getParts(first);
    RigParts b = getParts(second);
    expect(first && second && a.humanoid && a.root && a.head && a.hair && a.glasses &&
               b.humanoid && b.root && b.head && b.hair && b.glasses &&
               a.bodyParts.size() == 9 && b.bodyParts.size() == 9,
           "two standard StarterCharacter clones contain independent accessory rigs");
    if (!a.humanoid || !a.root || !a.head || !a.hair || !a.glasses ||
        !b.humanoid || !b.root || !b.head || !b.hair || !b.glasses) {
        return 1;
    }

    const CFrame hairRelativeA = a.head->getWorldCFrame().inverse() * a.hair->getWorldCFrame();
    const CFrame hairRelativeB = b.head->getWorldCFrame().inverse() * b.hair->getWorldCFrame();
    const CFrame glassesRelativeA =
        a.head->getWorldCFrame().inverse() * a.glasses->getWorldCFrame();
    const CFrame glassesRelativeB =
        b.head->getWorldCFrame().inverse() * b.glasses->getWorldCFrame();

    const auto assemblyA = Weld::collectAssembly(a.head, *first);
    const auto assemblyB = Weld::collectAssembly(b.head, *second);
    expect(contains(assemblyA, a.head.get()) && contains(assemblyA, a.hair.get()) &&
               contains(assemblyA, a.glasses.get()) &&
               !contains(assemblyA, b.head.get()) && !contains(assemblyA, b.hair.get()) &&
               !contains(assemblyA, b.glasses.get()) &&
               contains(assemblyB, b.head.get()) && contains(assemblyB, b.hair.get()) &&
               contains(assemblyB, b.glasses.get()) &&
               !contains(assemblyB, a.head.get()) && !contains(assemblyB, a.hair.get()) &&
               !contains(assemblyB, a.glasses.get()),
           "each cloned Weld assembly excludes the other character");

    // Spawn the two complete rigs apart before native bodies are constructed.
    // Preserve both accessory offsets so compound-local poses start valid.
    a.root->cframe = CFrame(Vector3(12.0f, 5.0f, -3.0f));
    b.root->cframe = CFrame(Vector3(-14.0f, 8.0f, 6.0f));
    a.humanoid->applyBodyAnimation(false, false);
    b.humanoid->applyBodyAnimation(false, false);
    a.hair->setWorldCFrame(a.head->getWorldCFrame() * hairRelativeA);
    a.glasses->setWorldCFrame(a.head->getWorldCFrame() * glassesRelativeA);
    b.hair->setWorldCFrame(b.head->getWorldCFrame() * hairRelativeB);
    b.glasses->setWorldCFrame(b.head->getWorldCFrame() * glassesRelativeB);

    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    if (physics && physics->isAvailable()) {
        physics->setGravity(Vector3(0, 0, 0));
        physics->update(*workspace, 1.0f / 60.0f);
        expect(physics->sharesBody(*a.head, *a.hair) &&
                   physics->sharesBody(*a.head, *a.glasses) &&
                   physics->sharesBody(*b.head, *b.hair) &&
                   physics->sharesBody(*b.head, *b.glasses) &&
                   !physics->sharesBody(*a.head, *b.head),
               "multiple anchored accessories form one independent compound per rig");

        const CFrame secondHeadBefore = b.head->getWorldCFrame();
        const CFrame secondHairBefore = b.hair->getWorldCFrame();
        const CFrame secondGlassesBefore = b.glasses->getWorldCFrame();
        physics->setBodyWorldCFrame(*a.root, CFrame(
            Vector3(12.0f, 5.0f, -3.0f),
            Quaternion::fromAxisAngle(Vector3(0, 1, 0), 35.0f)));
        physics->syncCube(*a.root);
        a.humanoid->applyBodyAnimation(false, false);
        expect(!cframeNear(hairRelativeA,
                   a.head->getWorldCFrame().inverse() * a.hair->getWorldCFrame()) &&
                   cframeNear(secondHeadBefore, b.head->getWorldCFrame()) &&
                   cframeNear(secondHairBefore, b.hair->getWorldCFrame()) &&
                   cframeNear(secondGlassesBefore, b.glasses->getWorldCFrame()),
               "Humanoid updates only logical rig parts before compound synchronization");

        physics->setBodyWorldCFrame(*b.root, CFrame(
            Vector3(-14.0f, 8.0f, 6.0f),
            Quaternion::fromAxisAngle(Vector3(0, 1, 0), -50.0f)));
        physics->syncCube(*b.root);
        b.humanoid->applyBodyAnimation(false, false);
        physics->syncWeldKinematics();
        expect(cframeNear(hairRelativeA,
                   a.head->getWorldCFrame().inverse() * a.hair->getWorldCFrame()) &&
                   cframeNear(glassesRelativeA,
                   a.head->getWorldCFrame().inverse() * a.glasses->getWorldCFrame()) &&
                   cframeNear(hairRelativeB,
                   b.head->getWorldCFrame().inverse() * b.hair->getWorldCFrame()) &&
                   cframeNear(glassesRelativeB,
                   b.head->getWorldCFrame().inverse() * b.glasses->getWorldCFrame()),
               "one compound sync follows both anchored accessories from each Head driver");

        bool relativePosesPreserved = true;
        bool finiteAndNearRoots = true;
        bool rigsRemainSeparated = true;
        bool velocitiesRemainBounded = true;
        float maxPartDistance = 0.0f;
        float maxRootSpeed = 0.0f;
        std::string farthestPart;
        int farthestFrame = -1;
        int fastestFrame = -1;
        constexpr int frameCount = 180;
        for (int frame = 0; frame < frameCount; ++frame) {
            const float phase = static_cast<float>(frame);
            const CFrame rootTargetA(
                Vector3(16.0f + phase * 0.025f,
                        9.0f + std::sin(phase * 0.07f) * 0.6f,
                        -5.0f + std::cos(phase * 0.05f) * 0.8f),
                Quaternion::fromAxisAngle(Vector3(0, 1, 0), phase * 1.35f));
            const CFrame rootTargetB(
                Vector3(-18.0f - phase * 0.02f,
                        12.0f + std::cos(phase * 0.06f) * 0.7f,
                        6.0f + std::sin(phase * 0.04f) * 0.9f),
                Quaternion::fromAxisAngle(Vector3(0, 1, 0), -phase * 1.1f));

            physics->setBodyWorldCFrame(*a.root, rootTargetA);
            physics->setBodyWorldCFrame(*b.root, rootTargetB);
            physics->syncCube(*a.root);
            physics->syncCube(*b.root);
            a.humanoid->applyBodyAnimation(false, false);
            b.humanoid->applyBodyAnimation(false, false);
            physics->syncWeldKinematics();

            auto validate = [&]() {
                relativePosesPreserved = relativePosesPreserved &&
                    cframeNear(hairRelativeA,
                        a.head->getWorldCFrame().inverse() * a.hair->getWorldCFrame()) &&
                    cframeNear(glassesRelativeA,
                        a.head->getWorldCFrame().inverse() * a.glasses->getWorldCFrame()) &&
                    cframeNear(hairRelativeB,
                        b.head->getWorldCFrame().inverse() * b.hair->getWorldCFrame()) &&
                    cframeNear(glassesRelativeB,
                        b.head->getWorldCFrame().inverse() * b.glasses->getWorldCFrame());
                for (const auto& part : a.bodyParts) {
                    const float distance = part
                        ? positionDistance(part->getWorldPosition(), a.root->getWorldPosition())
                        : std::numeric_limits<float>::infinity();
                    if (distance > maxPartDistance) {
                        maxPartDistance = distance;
                        farthestPart = std::string("A/") + (part ? part->Name : "missing");
                        farthestFrame = frame;
                    }
                    finiteAndNearRoots = finiteAndNearRoots && part &&
                        finiteCFrame(part->getWorldCFrame()) &&
                        distance < 12.0f;
                }
                for (const auto& part : b.bodyParts) {
                    const float distance = part
                        ? positionDistance(part->getWorldPosition(), b.root->getWorldPosition())
                        : std::numeric_limits<float>::infinity();
                    if (distance > maxPartDistance) {
                        maxPartDistance = distance;
                        farthestPart = std::string("B/") + (part ? part->Name : "missing");
                        farthestFrame = frame;
                    }
                    finiteAndNearRoots = finiteAndNearRoots && part &&
                        finiteCFrame(part->getWorldCFrame()) &&
                        distance < 12.0f;
                }
                rigsRemainSeparated = rigsRemainSeparated &&
                    positionDistance(a.head->getWorldPosition(), b.head->getWorldPosition()) > 20.0f;
                const Vector3 velocityA = physics->getLinearVelocity(*a.root);
                const Vector3 velocityB = physics->getLinearVelocity(*b.root);
                const float speed = std::max(vectorLength(velocityA), vectorLength(velocityB));
                if (speed > maxRootSpeed) {
                    maxRootSpeed = speed;
                    fastestFrame = frame;
                }
                velocitiesRemainBounded = velocitiesRemainBounded &&
                    finiteVector(velocityA) && finiteVector(velocityB) &&
                    vectorLength(velocityA) < 100.0f && vectorLength(velocityB) < 100.0f;
            };

            validate();
            physics->update(*workspace, 1.0f / 60.0f);
            validate();
        }

        expect(relativePosesPreserved,
               "Head/Hair/Glasses relative poses survive 180 compound sync frames");
        expect(finiteAndNearRoots,
               "all body and accessory parts remain finite and near their own Root");
        expect(rigsRemainSeparated,
               "continuous translation and rotation never mixes the two rig assemblies");
        expect(velocitiesRemainBounded,
               "compound pose synchronization leaves Root velocities finite and bounded");
        if (!finiteAndNearRoots || !velocitiesRemainBounded) {
            std::cout << "[StarterAccessoryWeld] diagnostics maxPartDistance="
                      << maxPartDistance << " part=" << farthestPart
                      << " frame=" << farthestFrame << " maxRootSpeed="
                      << maxRootSpeed << " speedFrame=" << fastestFrame << '\n';
        }
    } else {
        std::cout << "[StarterAccessoryWeld] SKIP: physics backend unavailable\n";
    }

    std::cout << "[StarterAccessoryWeld] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runStarterRootSpawnRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[StarterRootSpawn] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    auto system = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    workspace->Name = "StarterRootSpawnRegression";
    system->addChild(workspace);

    auto starter = std::make_shared<StarterCharacter>();
    starter->Name = "StarterCharacter";
    CharacterRig::buildDefaultRigParts(starter);
    auto templateRoot = std::dynamic_pointer_cast<BaseCube>(
        starter->children.contains("Root") ? starter->children.at("Root") : nullptr);
    if (!templateRoot) {
        expect(false, "default StarterCharacter contains Root");
        return 1;
    }
    templateRoot->Anchored = true;
    templateRoot->CanCollide = false;
    system->addChild(starter);

    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
    system->addChild(user);
    user->initializeInventory();

    LuauEngine engine;
    engine.setWorkspace(workspace);
    engine.setSystem(system.get());
    engine.setGlobalInstance("User", user);
    bool eventSawNormalizedRoot = false;
    const auto oldLogHook = g_luauLogHook;
    g_luauLogHook = [&](const std::string& message) {
        if (message.find("[StarterRootSpawnEvent]") != std::string::npos)
            eventSawNormalizedRoot = true;
    };
    auto listener = std::make_shared<Script>();
    listener->Name = "StarterRootSpawnListener";
    listener->Source =
        "User.CharacterAdded:Connect(function(character) "
        "local root = character:WaitChild('Root') "
        "if root and root.Anchored == false and root.CanCollide == true then "
        "print('[StarterRootSpawnEvent]') end end)";
    workspace->addChild(listener);
    expect(engine.execute(*listener),
           "CharacterAdded Root normalization listener starts successfully");

    const Vector3 spawnPosition(0.0f, 16.0f, 0.0f);
    user->spawnCharacter(system.get(), workspace.get(), spawnPosition);
    g_luauLogHook = oldLogHook;
    auto root = user->humanoid ? user->humanoid->getRootPart() : nullptr;
    expect(root && !root->Anchored && root->CanCollide,
           "spawnCharacter normalizes only the cloned local Root physics flags");
    expect(eventSawNormalizedRoot,
           "CharacterAdded observes the normalized Root before firing completes");
    expect(templateRoot->Anchored && !templateRoot->CanCollide,
           "saved StarterCharacter Root flags remain unchanged for serialization and retry");
    if (!user->character || !root) return 1;

    auto floor = std::make_shared<BaseCube>(
        Vector3(0.0f, -1.0f, 0.0f), Vector3(64.0f, 2.0f, 64.0f));
    floor->Name = "Floor";
    floor->Anchored = true;
    workspace->addChild(floor);
    workspace->addChild(user->character);
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    expect(physics && physics->isAvailable(),
           "selected physics backend is available for spawned Root simulation");
    if (physics && physics->isAvailable()) {
        const float initialY = root->getWorldPosition().y;
        float minimumY = initialY;
        for (int frame = 0; frame < 300; ++frame) {
            physics->update(*workspace, 1.0f / 60.0f);
            minimumY = std::min(minimumY, root->getWorldPosition().y);
        }
        const float finalY = root->getWorldPosition().y;
        const Vector3 finalVelocity = physics->getLinearVelocity(*root);
        const bool finiteVelocity = std::isfinite(finalVelocity.x) &&
            std::isfinite(finalVelocity.y) && std::isfinite(finalVelocity.z);
        const bool settled = initialY > 10.0f && minimumY < 3.0f &&
            finalY > 1.5f && finalY < 2.5f && finiteVelocity &&
            std::abs(finalVelocity.y) < 1.0f;
        expect(settled,
               "normalized Root falls from the spawn height and settles on the floor");
        if (!settled) {
            std::cout << "[StarterRootSpawn] diagnostics backend="
                      << (physics->getBackendType() == PhysicsBackendType::Box3D
                              ? "box3d" : "physx")
                      << " initialY=" << initialY << " minimumY=" << minimumY
                      << " finalY=" << finalY << " velocity=["
                      << finalVelocity.x << ',' << finalVelocity.y << ','
                      << finalVelocity.z << "]\n";
        }
    }

    std::cout << "[StarterRootSpawn] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runSpawnLocationRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[SpawnLocation] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };
    auto cframeNear = [](const CFrame& first, const CFrame& second) {
        const float dot = std::abs(
            first.Rotation.w * second.Rotation.w +
            first.Rotation.x * second.Rotation.x +
            first.Rotation.y * second.Rotation.y +
            first.Rotation.z * second.Rotation.z);
        return positionDistance(first.Position, second.Position) < 0.002f &&
               std::abs(1.0f - dot) < 0.002f;
    };

    auto spawnDefaults = std::make_shared<SpawnLocation>();
    expect(spawnDefaults->Name == "SpawnLocation" &&
               spawnDefaults->Size == Vector3(8, 1, 8) &&
               spawnDefaults->Color.r == 1.0f && spawnDefaults->Color.g == 1.0f &&
               spawnDefaults->Color.b == 1.0f && spawnDefaults->Color.a == 1.0f &&
               spawnDefaults->Anchored && spawnDefaults->CanCollide &&
               spawnDefaults->Enabled,
           "default SpawnLocation state matches the editor/runtime contract");
    expect(spawnDefaults->IsA("SpawnLocation") && spawnDefaults->IsA("Cube") &&
               spawnDefaults->IsA("BaseCube") && spawnDefaults->IsA("Spatial") &&
               spawnDefaults->IsA("Instance"),
           "Named supplies the complete SpawnLocation IsA inheritance chain");

    constexpr std::array<const char*, 12> baseCubeClasses = {
        "Cube", "Cylinder", "TriangularPrism", "Truss", "Seat", "Sphere",
        "MeshCube", "LiquidCube", "SpawnLocation", "Skybox", "Sun", "Moon"
    };
    bool factoryComplete = true;
    for (const char* className : baseCubeClasses) {
        auto created = createBaseCubeInstance(className);
        factoryComplete = factoryComplete && created &&
            created->getClassName() == className && created->IsA("BaseCube");
    }
    expect(factoryComplete && !createBaseCubeInstance("Folder"),
           "shared BaseCube factory covers every concrete BaseCube class only");

    spawnDefaults->Enabled = false;
    spawnDefaults->Color = Color4(0.2f, 0.3f, 0.4f, 0.5f);
    spawnDefaults->Locked = true;
    spawnDefaults->MassDensity = 3.5f;
    spawnDefaults->CollisionDetection = CCDMode::Bullet;
    spawnDefaults->LockFlags = PhysicsLockFlags::LinearX | PhysicsLockFlags::AngularY;
    spawnDefaults->addChild(std::make_shared<Folder>());
    auto spawnClone = std::dynamic_pointer_cast<SpawnLocation>(spawnDefaults->clone());
    expect(spawnClone && !spawnClone->Enabled && spawnClone->Color.r == 0.2f &&
               spawnClone->Locked && spawnClone->MassDensity == 3.5f &&
               spawnClone->CollisionDetection == CCDMode::Bullet &&
               spawnClone->LockFlags == spawnDefaults->LockFlags &&
               spawnClone->children.size() == 1 && !spawnClone->lastWorkspace,
           "BaseCube clone helper preserves design/custom state and children without runtime ownership");

    auto liquid = std::make_shared<LiquidCube>(
        Vector3(0, 0, 0), Vector3(4, 2, 4));
    liquid->Density = 4.25f;
    auto liquidClone = std::dynamic_pointer_cast<LiquidCube>(liquid->clone());
    auto sun = std::make_shared<Sun>();
    sun->Angle = 123.0f;
    auto sunClone = std::dynamic_pointer_cast<Sun>(sun->clone());
    auto skybox = std::make_shared<Skybox>();
    skybox->skyboxPaths[2] = "custom/top.png";
    auto skyboxClone = std::dynamic_pointer_cast<Skybox>(skybox->clone());
    auto seat = std::make_shared<Seat>(
        Vector3(0, 0, 0), Vector3(1, 1, 1), Cube::defaultTextureID);
    seat->Steer = 1.0f;
    seat->Throttle = -1.0f;
    auto seatClone = std::dynamic_pointer_cast<Seat>(seat->clone());
    expect(liquidClone && liquidClone->Density == liquid->Density &&
               sunClone && sunClone->Angle == sun->Angle &&
               skyboxClone && skyboxClone->skyboxPaths[2] == skybox->skyboxPaths[2],
           "BaseCube clone helper keeps each derived class's persistent custom state");
    expect(seatClone && seatClone->Steer == 0.0f && seatClone->Throttle == 0.0f &&
               !seatClone->isOccupied(),
           "BaseCube clone helper excludes Seat live input and occupant runtime state");

    {
        LuauEngine engine;
        auto system = std::make_shared<System>();
        auto workspace = std::make_shared<Workspace>();
        system->addChild(workspace);
        engine.setWorkspace(workspace);
        engine.setSystem(system.get());
        bool luauCreated = false;
        const auto oldLogHook = g_luauLogHook;
        g_luauLogHook = [&](const std::string& message) {
            if (message.find("[SpawnLocationLuau]") != std::string::npos)
                luauCreated = true;
        };
        auto script = std::make_shared<Script>();
        script->Name = "SpawnLocationLuauFactory";
        script->Source =
            "local spawn = Instance.new('SpawnLocation') "
            "spawn.Enabled = false "
            "if spawn:IsA('Cube') and spawn.Enabled == false then "
            "print('[SpawnLocationLuau]') end";
        expect(engine.execute(*script),
               "Luau Instance.new accepts SpawnLocation through the shared factory");
        g_luauLogHook = oldLogHook;
        expect(luauCreated,
               "SpawnLocation Enabled is readable and writable from Luau");
    }

    auto system = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    system->addChild(workspace);
    auto starter = std::make_shared<StarterCharacter>();
    CharacterRig::buildDefaultRigParts(starter);
    auto templateRoot = std::dynamic_pointer_cast<BaseCube>(starter->children.at("Root"));
    auto templateHead = std::dynamic_pointer_cast<BaseCube>(starter->children.at("Head"));
    templateRoot->cframe = CFrame(
        Vector3(1.0f, 0.5f, -2.0f),
        Quaternion::fromAxisAngle(Vector3(0, 1, 0), 12.0f));
    templateHead->cframe = CFrame(Vector3(0, 3, 0));
    system->addChild(starter);

    auto folderA = std::make_shared<Folder>();
    folderA->Name = "A";
    auto folderZ = std::make_shared<Folder>();
    folderZ->Name = "Z";
    auto spawnA = std::make_shared<SpawnLocation>(Vector3(8, 2, -4));
    spawnA->cframe.Rotation = Quaternion::fromEuler(Vector3(20, 45, -15));
    auto spawnZ = std::make_shared<SpawnLocation>(Vector3(-12, 5, 7));
    spawnZ->cframe.Rotation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), -30.0f);
    auto disabled = std::make_shared<SpawnLocation>(Vector3(100, 100, 100));
    disabled->Name = "Disabled";
    disabled->Enabled = false;
    folderA->addChild(spawnA);
    folderA->addChild(disabled);
    folderZ->addChild(spawnZ);
    workspace->addChild(folderZ);
    workspace->addChild(folderA);

    auto spawnUser = [&](std::uint32_t id) {
        auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
        user->peerId = id;
        user->spawnCharacter(system.get(), workspace.get());
        return user;
    };

    auto user0 = std::make_shared<User>(std::make_unique<NullInputBackend>());
    LuauEngine spawnEventEngine;
    spawnEventEngine.setWorkspace(workspace);
    spawnEventEngine.setSystem(system.get());
    spawnEventEngine.setGlobalInstance("User", user0);
    bool eventSawFinalCFrame = false;
    const auto oldSpawnLogHook = g_luauLogHook;
    g_luauLogHook = [&](const std::string& message) {
        if (message.find("[SpawnLocationEvent]") != std::string::npos)
            eventSawFinalCFrame = true;
    };
    auto spawnListener = std::make_shared<Script>();
    spawnListener->Name = "SpawnLocationListener";
    spawnListener->Source =
        "User.CharacterAdded:Connect(function(character) "
        "local root = character:WaitChild('Root') "
        "local spawn = workspace:WaitChild('A'):WaitChild('SpawnLocation') "
        "local expected = spawn.WorldCFrame * CFrame.new(0, (spawn.Size.y + root.Size.y) * 0.5, 0) "
        "local p, ep, q, eq = root.WorldCFrame.Position, expected.Position, root.WorldCFrame.Rotation, expected.Rotation "
        "local pd = math.abs(p.x-ep.x)+math.abs(p.y-ep.y)+math.abs(p.z-ep.z) "
        "local dot = math.abs(q.w*eq.w+q.x*eq.x+q.y*eq.y+q.z*eq.z) "
        "if pd < 0.002 and math.abs(1-dot) < 0.002 then print('[SpawnLocationEvent]') end end)";
    workspace->addChild(spawnListener);
    expect(spawnEventEngine.execute(*spawnListener),
           "CharacterAdded final-CFrame listener starts successfully");
    user0->spawnCharacter(system.get(), workspace.get());
    g_luauLogHook = oldSpawnLogHook;
    expect(eventSawFinalCFrame,
           "CharacterAdded observes the selected SpawnLocation full CFrame");

    auto user1 = spawnUser(1);
    auto user2 = spawnUser(2);
    auto user3 = spawnUser(3);
    auto root0 = user0->humanoid->getRootPart();
    auto root1 = user1->humanoid->getRootPart();
    auto root2 = user2->humanoid->getRootPart();
    auto root3 = user3->humanoid->getRootPart();
    const CFrame expectedA = spawnA->getWorldCFrame() *
        CFrame(0, (spawnA->Size.y + root0->Size.y) * 0.5f, 0);
    const CFrame expectedZ = spawnZ->getWorldCFrame() *
        CFrame(0, (spawnZ->Size.y + root2->Size.y) * 0.5f, 0);
    expect(cframeNear(root0->getWorldCFrame(), expectedA) &&
               cframeNear(root1->getWorldCFrame(), expectedA) &&
               cframeNear(root2->getWorldCFrame(), expectedZ) &&
               cframeNear(root3->getWorldCFrame(), expectedA),
           "full-path sorting maps peer0/peer1/peer3 to first spawn and peer2 to second");
    const CFrame rootToHead = templateRoot->cframe.inverse() * templateHead->cframe;
    expect(cframeNear(root0->getWorldCFrame().inverse() *
                          user0->humanoid->getHeadPart()->getWorldCFrame(), rootToHead),
           "full CFrame spawn placement preserves the original Root-to-part assembly pose");

    auto playHereUser = std::make_shared<User>(std::make_unique<NullInputBackend>());
    const Vector3 playHere(33, 44, 55);
    playHereUser->spawnCharacter(system.get(), workspace.get(), playHere);
    expect(playHereUser->character->Position == playHere,
           "Play Here explicit Model.Position overrides SpawnLocation selection");

    workspace->addChild(user2->character);
    user2->respawnCharacter();
    root2 = user2->humanoid->getRootPart();
    expect(root2 && cframeNear(root2->getWorldCFrame(), expectedZ),
           "respawn reuses the active Workspace and selected SpawnLocation");

    auto emptyWorkspace = std::make_shared<Workspace>();
    auto originUser = std::make_shared<User>(std::make_unique<NullInputBackend>());
    originUser->spawnCharacter(system.get(), emptyWorkspace.get());
    expect(originUser->humanoid &&
               cframeNear(originUser->humanoid->getRootPart()->getWorldCFrame(), CFrame()),
           "absence of enabled SpawnLocations places Root at the world origin");

    std::error_code ec;
    const auto yamlPath = std::filesystem::temp_directory_path(ec) /
        "recubin_spawn_location_regression.yaml";
    if (!ec) {
        auto yamlSystem = std::make_shared<System>();
        auto yamlWorkspace = std::make_shared<Workspace>();
        yamlSystem->addChild(yamlWorkspace);
        auto yamlSpawn = std::make_shared<SpawnLocation>(Vector3(3, 4, 5));
        yamlSpawn->Enabled = false;
        yamlWorkspace->addChild(yamlSpawn);
        SceneLoader::saveScene(yamlSystem.get(), yamlPath.string());
        auto loaded = SceneLoader::loadScene(yamlPath.string());
        auto loadedSpawn = loaded
            ? dynamic_cast<SpawnLocation*>(loaded->getChildByPath(
                  "Workspace\\SpawnLocation"))
            : nullptr;
        expect(loadedSpawn && !loadedSpawn->Enabled &&
                   loadedSpawn->Size == Vector3(8, 1, 8),
               "SceneLoader shared factory round-trips SpawnLocation YAML and Enabled");
        std::filesystem::remove(yamlPath, ec);
    } else {
        expect(false, "temporary path is available for SpawnLocation YAML round-trip");
    }

    std::cout << "[SpawnLocation] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runRemoteAvatarSpawnTransformRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[RemoteAvatarSpawnTransform] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };
    auto cframeNear = [](const CFrame& first, const CFrame& second,
                         float tolerance = 0.003f) {
        const float dot = std::abs(
            first.Rotation.w * second.Rotation.w +
            first.Rotation.x * second.Rotation.x +
            first.Rotation.y * second.Rotation.y +
            first.Rotation.z * second.Rotation.z);
        return positionDistance(first.Position, second.Position) < tolerance &&
            std::abs(1.0f - dot) < tolerance;
    };

    auto system = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    workspace->Name = "RemoteAvatarSpawnTransformWorkspace";
    auto users = std::make_shared<Users>();
    auto starter = std::make_shared<StarterCharacter>();
    CharacterRig::buildDefaultRigParts(starter);
    auto templateHead = std::dynamic_pointer_cast<BaseCube>(
        starter->children.at("Head"));
    auto templateHair = std::make_shared<Cube>(
        Vector3(), Vector3(1.4f, 0.4f, 1.4f), Cube::defaultTextureID);
    templateHair->Name = "Hair";
    templateHair->cframe = templateHead->cframe * CFrame(
        Vector3(0.15f, 0.85f, -0.1f),
        Quaternion::fromEuler(Vector3(8.0f, -13.0f, 5.0f)));
    templateHair->Anchored = true;
    templateHair->CanCollide = false;
    starter->addChild(templateHair);
    auto templateHairWeld = std::make_shared<Weld>(templateHead, templateHair);
    templateHairWeld->Name = "HairWeld";
    starter->addChild(templateHairWeld);
    for (auto& [name, child] : starter->children) {
        (void)name;
        if (auto cube = std::dynamic_pointer_cast<BaseCube>(child))
            cube->Anchored = true;
    }
    system->addChild(workspace);
    system->addChild(users);
    system->addChild(starter);

    auto folderA = std::make_shared<Folder>();
    folderA->Name = "A";
    auto folderB = std::make_shared<Folder>();
    folderB->Name = "B";
    auto spawnA = std::make_shared<SpawnLocation>(Vector3(18, 3, -11));
    spawnA->cframe.Rotation = Quaternion::fromEuler(Vector3(19, -34, 12));
    auto spawnB = std::make_shared<SpawnLocation>(Vector3(-23, 7, 9));
    spawnB->cframe.Rotation = Quaternion::fromEuler(Vector3(-21, 47, -16));
    folderA->addChild(spawnA);
    folderB->addChild(spawnB);
    workspace->addChild(folderB);
    workspace->addChild(folderA);

    ReplicationManager replication(workspace, nullptr, system.get());
    const bool spawnedTwo =
        ReplicationTestAccess::spawnRemoteAvatar(replication, 2);
    const bool spawnedThree =
        ReplicationTestAccess::spawnRemoteAvatar(replication, 3);
    auto modelTwo = ReplicationTestAccess::model(replication, 2);
    auto modelThree = ReplicationTestAccess::model(replication, 3);
    auto identityTwo = ReplicationTestAccess::identity(replication, 2);
    auto identityThree = ReplicationTestAccess::identity(replication, 3);
    expect(spawnedTwo && spawnedThree && modelTwo && modelThree &&
               modelTwo != modelThree,
           "actual spawnRemoteAvatar path creates two independent avatar Models");
    if (!modelTwo || !modelThree) return 1;

    auto part = [](const std::shared_ptr<Model>& model, const char* name) {
        auto it = model->children.find(name);
        return it == model->children.end()
            ? std::shared_ptr<BaseCube>()
            : std::dynamic_pointer_cast<BaseCube>(it->second);
    };
    auto rootTwo = part(modelTwo, "Root");
    auto headTwo = part(modelTwo, "Head");
    auto torsoTwo = part(modelTwo, "Torso");
    auto hairTwo = part(modelTwo, "Hair");
    auto rootThree = part(modelThree, "Root");
    auto headThree = part(modelThree, "Head");
    auto hairThree = part(modelThree, "Hair");
    expect(rootTwo && headTwo && torsoTwo && hairTwo && rootThree &&
               headThree && hairThree,
           "spawned avatars contain standard body parts and cloned Weld accessory");
    if (!rootTwo || !headTwo || !torsoTwo || !hairTwo || !rootThree ||
        !headThree || !hairThree) return 1;

    const CFrame expectedSpawnTwo = spawnB->getWorldCFrame() *
        CFrame(0, (spawnB->Size.y + rootTwo->Size.y) * 0.5f, 0);
    const CFrame expectedSpawnThree = spawnA->getWorldCFrame() *
        CFrame(0, (spawnA->Size.y + rootThree->Size.y) * 0.5f, 0);
    expect(cframeNear(rootTwo->getWorldCFrame(), expectedSpawnTwo) &&
               cframeNear(rootThree->getWorldCFrame(), expectedSpawnThree),
           "PeerId mapping applies each nonidentity SpawnLocation full CFrame");

    const CFrame modelTwoSpawnFrame = modelTwo->getWorldCFrame();
    const CFrame modelThreeSpawnFrame = modelThree->getWorldCFrame();
    expect(!cframeNear(modelTwoSpawnFrame, CFrame()) &&
               !cframeNear(modelThreeSpawnFrame, CFrame()),
           "remote avatar Models retain nonidentity spawn transforms");

    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    for (int frame = 0; physics && frame < 8 &&
         (ReplicationTestAccess::hasPendingPhysicsRegistration(replication, 2) ||
          ReplicationTestAccess::hasPendingPhysicsRegistration(replication, 3));
         ++frame) {
        physics->update(*workspace, 1.0f / 60.0f);
    }
    expect(physics &&
               !ReplicationTestAccess::hasPendingPhysicsRegistration(replication, 2) &&
               !ReplicationTestAccess::hasPendingPhysicsRegistration(replication, 3),
           "runtime physics registration completes before remote pose application");

    const CFrame rootToTorso =
        rootTwo->getWorldCFrame().inverse() * torsoTwo->getWorldCFrame();
    const CFrame headToHair =
        headTwo->getWorldCFrame().inverse() * hairTwo->getWorldCFrame();
    const CFrame poseTwo(
        Vector3(-41, 13, 28),
        Quaternion::fromEuler(Vector3(24, -52, 17)));
    const CFrame poseThree(
        Vector3(36, 9, -31),
        Quaternion::fromEuler(Vector3(-18, 63, -22)));
    ReplicationTestAccess::setLatestPose(replication, 2, poseTwo);
    ReplicationTestAccess::setLatestPose(replication, 3, poseThree);
    ReplicationTestAccess::applyAvatarPoses(replication, 1.0f / 60.0f);

    expect(cframeNear(rootTwo->getWorldCFrame(), poseTwo) &&
               cframeNear(rootThree->getWorldCFrame(), poseThree),
           "first received poses snap both Roots to exact world CFrames");
    expect(cframeNear(rootTwo->getWorldCFrame().inverse() *
                          torsoTwo->getWorldCFrame(), rootToTorso) &&
               cframeNear(headTwo->getWorldCFrame().inverse() *
                          hairTwo->getWorldCFrame(), headToHair),
           "body and Weld accessory relative poses survive world-pose application");
    const auto assemblyTwo = Weld::collectAssembly(headTwo, *modelTwo);
    const auto assemblyThree = Weld::collectAssembly(headThree, *modelThree);
    const bool twoOwnsHair = std::find(
        assemblyTwo.begin(), assemblyTwo.end(), hairTwo) != assemblyTwo.end();
    const bool twoOwnsOtherHair = std::find(
        assemblyTwo.begin(), assemblyTwo.end(), hairThree) != assemblyTwo.end();
    const bool threeOwnsHair = std::find(
        assemblyThree.begin(), assemblyThree.end(), hairThree) != assemblyThree.end();
    expect(twoOwnsHair && threeOwnsHair && !twoOwnsOtherHair,
           "cloned Weld assemblies remain separated between peers");

    const CFrame nextPoseTwo(
        Vector3(52, 21, -44),
        Quaternion::fromEuler(Vector3(-31, 78, 26)));
    constexpr float interpolationDt = 0.1f;
    const float alpha = 1.0f - std::exp(-15.0f * interpolationDt);
    CFrame expectedInterpolated = poseTwo;
    expectedInterpolated.Position = poseTwo.Position +
        (nextPoseTwo.Position - poseTwo.Position) * alpha;
    expectedInterpolated.Rotation = Quaternion::Slerp(
        poseTwo.Rotation, nextPoseTwo.Rotation, alpha);
    ReplicationTestAccess::setLatestPose(replication, 2, nextPoseTwo);
    ReplicationTestAccess::applyAvatarPoses(replication, interpolationDt);
    expect(cframeNear(rootTwo->getWorldCFrame(), expectedInterpolated) &&
               cframeNear(rootThree->getWorldCFrame(), poseThree),
           "second pose interpolates only its target peer in world space");
    expect(cframeNear(modelTwo->getWorldCFrame(), modelTwoSpawnFrame) &&
               cframeNear(modelThree->getWorldCFrame(), modelThreeSpawnFrame),
           "pose application preserves each nonidentity Model CFrame");
    expect(identityTwo && identityThree && identityTwo != identityThree &&
               identityTwo->Name == "User_2" && identityThree->Name == "User_3" &&
               identityTwo->character == modelTwo &&
               identityThree->character == modelThree &&
               users->children.contains("User_2") &&
               users->children.contains("User_3"),
           "multiple peer identities retain their own canonical avatar Models");

    {
        auto legacySystem = std::make_shared<System>();
        auto legacyWorkspace = std::make_shared<Workspace>();
        auto legacyUsers = std::make_shared<Users>();
        auto legacyStarter = std::make_shared<StarterCharacter>();
        CharacterRig::buildDefaultRigParts(legacyStarter);
        auto legacyTemplateRoot = std::dynamic_pointer_cast<BaseCube>(
            legacyStarter->children.at("Root"));
        legacyTemplateRoot->Anchored = true;
        legacySystem->addChild(legacyWorkspace);
        legacySystem->addChild(legacyUsers);
        legacySystem->addChild(legacyStarter);

        ReplicationManager legacyReplication(
            legacyWorkspace, nullptr, legacySystem.get());
        const bool legacySpawned =
            ReplicationTestAccess::spawnRemoteAvatar(legacyReplication, 2);
        auto legacyModel =
            ReplicationTestAccess::model(legacyReplication, 2);
        auto legacyRoot = legacyModel ? part(legacyModel, "Root") : nullptr;
        legacyWorkspace->initPhysics();
        Physics* legacyPhysics = legacyWorkspace->getPhysicsEngine();
        for (int frame = 0; legacyPhysics && frame < 8 &&
             ReplicationTestAccess::hasPendingPhysicsRegistration(
                 legacyReplication, 2);
             ++frame) {
            legacyPhysics->update(*legacyWorkspace, 1.0f / 60.0f);
        }
        const CFrame legacyPose(
            Vector3(7, 12, -19),
            Quaternion::fromEuler(Vector3(13, -28, 9)));
        ReplicationTestAccess::setLatestPose(
            legacyReplication, 2, legacyPose);
        ReplicationTestAccess::applyAvatarPoses(
            legacyReplication, 1.0f / 60.0f);
        expect(legacySpawned && legacyModel && legacyRoot &&
                   cframeNear(legacyModel->getWorldCFrame(), CFrame()) &&
                   cframeNear(legacyRoot->getWorldCFrame(), legacyPose),
               "legacy no-SpawnLocation identity Model follows the same world-pose path");
    }

    std::cout << "[RemoteAvatarSpawnTransform] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runMeshCubeFallbackRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[MeshCubeFallback] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    const std::string missingPath =
        "assets/models/__recubin_missing_mesh_fallback__.glb";
    auto mesh = std::make_shared<MeshCube>(
        Vector3(1, 2, 3), Vector3(4, 5, 6));
    const bool loaded = mesh->loadFromGLB(missingPath);
    expect(!loaded && mesh->MeshFile == missingPath &&
               mesh->isUsingFallback() && mesh->hasGeometry() &&
               mesh->getIndexCount() == 36,
           "missing GLB preserves its path and activates box fallback geometry");

    const auto vertices = mesh->getConvexVertices();
    bool unitCorners = vertices.size() == 8;
    for (const Vector3& vertex : vertices) {
        unitCorners = unitCorners && near(std::abs(vertex.x), 0.5f) &&
            near(std::abs(vertex.y), 0.5f) &&
            near(std::abs(vertex.z), 0.5f);
    }
    expect(unitCorners,
           "fallback physics hull contains the eight unit-box corners");

    auto clone = std::dynamic_pointer_cast<MeshCube>(mesh->clone());
    expect(clone && clone->MeshFile == missingPath &&
               clone->isUsingFallback() && clone->hasGeometry() &&
               clone->getIndexCount() == 36 &&
               clone->getConvexVertices().size() == 8,
           "clone preserves fallback state without discarding the requested path");

    auto empty = std::make_shared<MeshCube>(Vector3{}, Vector3(1, 1, 1));
    expect(!empty->loadFromGLB("") && empty->MeshFile.empty() &&
               !empty->isUsingFallback() && !empty->hasGeometry() &&
               empty->getIndexCount() == 0 &&
               empty->getConvexVertices().empty(),
           "empty mesh request remains an explicit no-model state");

    std::cout << "[MeshCubeFallback] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runShadowModeRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[ShadowMode] " << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };
    auto cube = std::make_shared<Cube>(Vector3{}, Vector3(1, 1, 1), 0);
    expect(cube->ShadowMode == ShadowMode::Normal, "default mode is Normal");
    cube->Color.a = 0.0f;
    cube->CastShadow = true;
    for (const auto mode : {ShadowMode::Always, ShadowMode::Never, ShadowMode::Normal}) {
        cube->ShadowMode = mode;
        expect(cube->shouldCastShadow(false) == (mode == ShadowMode::Always),
               "transparent cube follows each ShadowMode");
        cube->Color.a = 1.0f;
        expect(cube->shouldCastShadow(false) == (mode != ShadowMode::Never),
               "opaque cube follows each ShadowMode");
        cube->Color.a = 0.0f;
    }
    cube->ShadowMode = ShadowMode::Always;
    cube->CastShadow = false;
    expect(!cube->shouldCastShadow(false), "CastShadow false overrides Always");
    cube->CastShadow = true;
    cube->setProperty("ShadowMode", YAML::Node("Always"));
    expect(cube->ShadowMode == ShadowMode::Always, "YAML setter accepts Always");
    cube->setProperty("ShadowMode", YAML::Node("Never"));
    expect(cube->ShadowMode == ShadowMode::Never, "YAML setter accepts Never");
    cube->setProperty("ShadowMode", YAML::Node("unknown"));
    expect(cube->ShadowMode == ShadowMode::Normal, "unknown YAML mode falls back to Normal");
    cube->CastShadow = false;
    cube->ShadowMode = ShadowMode::Always;
    auto clone = std::dynamic_pointer_cast<BaseCube>(cube->clone());
    expect(clone && clone->ShadowMode == ShadowMode::Always && !clone->CastShadow,
           "clone preserves ShadowMode and CastShadow");
    auto mesh = std::make_shared<MeshCube>(Vector3{}, Vector3(1, 1, 1));
    mesh->loadFromGLB("assets/models/__recubin_missing_shadow_mode__.glb");
    mesh->Color.a = 0.0f;
    mesh->ShadowMode = ShadowMode::Normal;
    expect(mesh->isUsingFallback() && mesh->shouldCastShadow(mesh->isUsingFallback()),
           "missing MeshCube fallback casts a Normal shadow");
    const auto path = std::filesystem::temp_directory_path() / "recubin_shadow_mode_regression.yaml";
    cube->CastShadow = true;
    cube->ShadowMode = ShadowMode::Always;
    cube->Name = "ShadowModeCube";
    auto saveRoot = std::make_shared<Instance>("ShadowModeSaveRoot");
    saveRoot->addChild(cube);
    SceneLoader::saveScene(saveRoot.get(), path.string());
    auto loadedRoot = SceneLoader::loadScene(path.string());
    Instance* loadedChild = loadedRoot ? loadedRoot->getChild("ShadowModeCube") : nullptr;
    auto loaded = loadedChild
        ? std::dynamic_pointer_cast<BaseCube>(loadedChild->shared_from_this())
        : nullptr;
    expect(loaded && loaded->ShadowMode == ShadowMode::Always,
           "Scene YAML round-trip preserves ShadowMode");
    std::ifstream saved(path);
    std::string yaml((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    const std::string key = "ShadowMode: Always\n";
    const std::size_t keyPos = yaml.find(key);
    expect(keyPos != std::string::npos, "Scene YAML writes ShadowMode");
    if (keyPos != std::string::npos) {
        const std::size_t lineStart = yaml.rfind('\n', keyPos);
        const std::size_t lineEnd = yaml.find('\n', keyPos);
        const std::size_t eraseStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        const std::size_t eraseEnd = lineEnd == std::string::npos ? yaml.size() : lineEnd + 1;
        yaml.erase(eraseStart, eraseEnd - eraseStart);
        std::ofstream legacy(path, std::ios::trunc);
        legacy << yaml;
        legacy.close();
        auto legacyRoot = SceneLoader::loadScene(path.string());
        Instance* legacyChild = legacyRoot ? legacyRoot->getChild("ShadowModeCube") : nullptr;
        auto legacyLoaded = legacyChild
            ? std::dynamic_pointer_cast<BaseCube>(legacyChild->shared_from_this())
            : nullptr;
        expect(legacyLoaded && legacyLoaded->ShadowMode == ShadowMode::Normal,
               "legacy YAML without ShadowMode defaults to Normal");
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::cout << "[ShadowMode] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
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

struct HumanoidRigCollisionCase {
    const char* name;
    bool includeTorso;
    bool torsoAnchored;
    bool torsoCanCollide;
    bool includeHair;
    bool hairAnchored;
    bool hairCanCollide;
    bool weldHair;
};

struct HumanoidRigCollisionResult {
    PhysicsBackendType backend = PhysicsBackendType::PhysX;
    float finalY = 0.0f;
    float minimumY = std::numeric_limits<float>::infinity();
    Vector3 finalVelocity;
    bool available = false;
    bool finite = true;
    int sameCharacterContacts = 0;
    int floorContacts = 0;
};

HumanoidRigCollisionResult simulateHumanoidRigCollision(
    const HumanoidRigCollisionCase& testCase) {
    constexpr float expectedRootY = 2.0f;
    constexpr float initialRootY = 16.25f;
    auto workspace = std::make_shared<Workspace>();
    workspace->Name = std::string("HumanoidRigCollision_") + testCase.name;
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    HumanoidRigCollisionResult result;
    result.backend = physics->getBackendType();
    result.available = physics->isAvailable();

    // 上面 Y=0 の床。巨大床や薄い床に依存しない寸法にして、リグ内部の
    // collider/kinematic drive だけを比較する。
    auto floor = std::make_shared<BaseCube>(Vector3(0, -1, 0), Vector3(32, 2, 32));
    floor->Name = "Floor";
    floor->Anchored = true;
    workspace->addChild(floor);

    auto character = std::make_shared<Model>();
    character->Name = "PlayerCharacter";
    workspace->addChild(character);
    std::unordered_set<BaseCube*> characterParts;

    auto humanoid = std::make_shared<Humanoid>();
    humanoid->Name = "Humanoid";
    auto root = std::make_shared<BaseCube>(
        Vector3(0, initialRootY, 0), Vector3(2, 4, 1));
    root->Name = "Root";
    root->Anchored = false;
    root->CanCollide = true;
    root->LockFlags = PhysicsLockFlags::AngularX | PhysicsLockFlags::AngularZ;
    characterParts.insert(root.get());

    // 標準部品は存在させるが、疑わしい部品以外はゲーム中の通常リグと同様に
    // anchored/non-colliding とし、診断対象へ余計な接触を加えない。
    auto makeBodyPart = [&](const char* name, const Vector3& position,
                            const Vector3& size) {
        auto part = std::make_shared<BaseCube>(position, size);
        part->Name = name;
        part->Anchored = true;
        part->CanCollide = false;
        character->addChild(part);
        characterParts.insert(part.get());
        return part;
    };

    character->addChild(root);
    auto head = makeBodyPart("Head", {0, initialRootY + 2.25f, 0},
                             {1.25f, 1.25f, 1.25f});
    makeBodyPart("LeftArm", {-1.5f, initialRootY + 0.75f, 0}, {1, 2, 1});
    makeBodyPart("RightArm", {1.5f, initialRootY + 0.75f, 0}, {1, 2, 1});
    makeBodyPart("LeftLeg", {-0.5f, initialRootY - 1.25f, 0}, {1, 2, 1});
    makeBodyPart("RightLeg", {0.5f, initialRootY - 1.25f, 0}, {1, 2, 1});

    if (testCase.includeTorso) {
        auto torso = std::make_shared<BaseCube>(Vector3(0, initialRootY + 0.75f, 0),
                                                Vector3(2, 2, 2));
        torso->Name = "Torso";
        torso->Anchored = testCase.torsoAnchored;
        torso->CanCollide = testCase.torsoCanCollide;
        character->addChild(torso);
        characterParts.insert(torso.get());
    }

    if (testCase.includeHair) {
        // chilly.yaml の Head 相対位置と Size を使い、Hair collider が
        // Root 上部と重なる実リグの配置を Cube proxy で再現する。
        auto hair = std::make_shared<BaseCube>(
            Vector3(0.035648704f, -0.35836554f, 0.08281714f),
                                               Vector3(2.5f, 2.5f, 2.5f));
        hair->Name = "Hair";
        hair->Anchored = testCase.hairAnchored;
        hair->CanCollide = testCase.hairCanCollide;
        head->addChild(hair);
        characterParts.insert(hair.get());
        if (testCase.weldHair) {
            auto weld = std::make_shared<Weld>(head, hair);
            weld->Name = "HairWeld";
            head->addChild(weld);
        }
    }

    character->addChild(humanoid);
    humanoid->resolveParts(character.get());

    const auto previousContactCallback = Physics::s_contactCallback;
    Physics::s_contactCallback = [&](BaseCube* first, BaseCube* second) {
        if (!first || !second) return;
        if (characterParts.contains(first) && characterParts.contains(second))
            ++result.sameCharacterContacts;
        if ((first == floor.get() && characterParts.contains(second)) ||
            (second == floor.get() && characterParts.contains(first)))
            ++result.floorContacts;
    };

    result.minimumY = root->getWorldPosition().y;
    for (int frame = 0; frame < 480; ++frame) {
        physics->update(*workspace, 1.0f / 60.0f);
        humanoid->applyBodyAnimation(false, false);
        physics->syncWeldKinematics();

        const Vector3 position = root->getWorldPosition();
        const Vector3 velocity = physics->getLinearVelocity(*root);
        result.minimumY = std::min(result.minimumY, position.y);
        result.finite = result.finite && finiteVector(position) &&
                        finiteVector(velocity) && finiteCFrame(root->getWorldCFrame());
    }
    result.finalY = root->getWorldPosition().y;
    result.finalVelocity = physics->getLinearVelocity(*root);
    Physics::s_contactCallback = previousContactCallback;
    return result;
}

struct CharacterCollisionFilterProbeResult {
    bool available = false;
    int initialSelfContacts = 0;
    int selfContactsAfterReparentOut = 0;
    int selfContactsAfterReparentBack = 0;
    int selfContactsAfterHumanoidRemove = 0;
    int selfContactsAfterHumanoidRestore = 0;
    int initialToolContacts = 0;
    int toolContactsAfterReparentOut = 0;
    int toolContactsAfterReparentBack = 0;
    int remoteNpcContacts = 0;
    int floorContacts = 0;
    int cloneContacts = 0;
    int nestedCharacterContacts = 0;
    int ragdollSelfContacts = 0;
    int ragdollContactsAfterReparentOut = 0;
    bool raycastDetectedSelfPart = false;
    bool overlapDetectedSelfBlock = false;
};

CharacterCollisionFilterProbeResult runCharacterCollisionFilterProbe() {
    CharacterCollisionFilterProbeResult result;
    auto workspace = std::make_shared<Workspace>();
    workspace->Name = "CharacterCollisionFilterProbe";
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    result.available = physics->isAvailable();
    physics->setGravity({0, 0, 0});

    struct CharacterParts {
        std::shared_ptr<Model> model;
        std::shared_ptr<Humanoid> humanoid;
        std::shared_ptr<BaseCube> root;
    };
    auto makeCharacter = [&](const char* name, float x, bool anchored) {
        CharacterParts parts;
        parts.model = std::make_shared<Model>();
        parts.model->Name = name;
        parts.root = std::make_shared<Cube>(
            Vector3(x, 2, 0), Vector3(2, 2, 2), 0);
        parts.root->Name = "Root";
        parts.root->Anchored = anchored;
        parts.humanoid = std::make_shared<Humanoid>();
        parts.humanoid->Name = "Humanoid";
        parts.model->addChild(parts.root);
        parts.model->addChild(parts.humanoid);
        workspace->addChild(parts.model);
        return parts;
    };

    CharacterParts selfCharacter = makeCharacter("SelfCharacter", -18.0f, false);
    auto selfBlock = std::make_shared<BaseCube>(
        Vector3(-18, 2, 0), Vector3(2, 2, 2));
    selfBlock->Name = "SelfBlock";
    selfBlock->Anchored = true;
    selfCharacter.model->addChild(selfBlock);

    CharacterParts toolCharacter = makeCharacter("ToolCharacter", -12.0f, false);
    auto tool = std::make_shared<Tool>("EquippedTool");
    auto handle = std::make_shared<BaseCube>(
        Vector3(-12, 2, 0), Vector3(2, 2, 2));
    handle->Name = "Handle";
    handle->Anchored = true;
    tool->addChild(handle);
    toolCharacter.model->addChild(tool);

    CharacterParts remoteCharacter = makeCharacter("RemoteCharacter", -6.0f, false);
    CharacterParts npcCharacter = makeCharacter("NPCCharacter", -6.0f, true);

    CharacterParts floorCharacter = makeCharacter("FloorCharacter", 0.0f, false);
    auto floor = std::make_shared<BaseCube>(Vector3(0, 2, 0), Vector3(2, 2, 2));
    floor->Name = "FloorProbe";
    floor->Anchored = true;
    workspace->addChild(floor);

    CharacterParts cloneSource = makeCharacter("CloneSource", 6.0f, false);
    auto clone = std::dynamic_pointer_cast<Model>(cloneSource.model->cloneTree());
    clone->Name = "CloneCharacter";
    auto cloneRoot = std::static_pointer_cast<BaseCube>(clone->children.at("Root"));
    cloneRoot->Anchored = true;
    workspace->addChild(clone);

    CharacterParts outerCharacter = makeCharacter("OuterCharacter", 12.0f, false);
    auto nestedCharacter = std::make_shared<Model>();
    nestedCharacter->Name = "NestedCharacter";
    auto nestedRoot = std::make_shared<BaseCube>(
        Vector3(12, 2, 0), Vector3(2, 2, 2));
    nestedRoot->Name = "Root";
    nestedRoot->Anchored = true;
    auto nestedHumanoid = std::make_shared<Humanoid>();
    nestedHumanoid->Name = "Humanoid";
    nestedCharacter->addChild(nestedRoot);
    nestedCharacter->addChild(nestedHumanoid);
    outerCharacter.model->addChild(nestedCharacter);

    CharacterParts ragdollCharacter = makeCharacter("RagdollCharacter", 18.0f, false);
    auto ragdollTorso = std::make_shared<BaseCube>(
        Vector3(18, 2, 0), Vector3(2, 2, 2));
    ragdollTorso->Name = "Torso";
    ragdollTorso->Anchored = true;
    ragdollTorso->CanCollide = false;
    auto ragdollHead = std::make_shared<BaseCube>(
        Vector3(18, 2, 0), Vector3(2, 2, 2));
    ragdollHead->Name = "Head";
    ragdollHead->Anchored = true;
    ragdollHead->CanCollide = false;
    ragdollCharacter.model->addChild(ragdollTorso);
    ragdollCharacter.model->addChild(ragdollHead);
    ragdollCharacter.humanoid->resolveParts(ragdollCharacter.model.get());

    auto isPair = [](BaseCube* first, BaseCube* second,
                     BaseCube* expectedFirst, BaseCube* expectedSecond) {
        return (first == expectedFirst && second == expectedSecond) ||
               (first == expectedSecond && second == expectedFirst);
    };
    int selfContacts = 0;
    int toolContacts = 0;
    int ragdollContacts = 0;
    const auto previousContactCallback = Physics::s_contactCallback;
    Physics::s_contactCallback = [&](BaseCube* first, BaseCube* second) {
        if (isPair(first, second, selfCharacter.root.get(), selfBlock.get()))
            ++selfContacts;
        if (isPair(first, second, toolCharacter.root.get(), handle.get()))
            ++toolContacts;
        if (isPair(first, second, remoteCharacter.root.get(), npcCharacter.root.get()))
            ++result.remoteNpcContacts;
        if (isPair(first, second, floorCharacter.root.get(), floor.get()))
            ++result.floorContacts;
        if (isPair(first, second, cloneSource.root.get(), cloneRoot.get()))
            ++result.cloneContacts;
        if (isPair(first, second, outerCharacter.root.get(), nestedRoot.get()))
            ++result.nestedCharacterContacts;
        if (isPair(first, second, ragdollTorso.get(), ragdollHead.get()))
            ++ragdollContacts;
    };

    auto step = [&] {
        for (int frame = 0; frame < 8; ++frame)
            physics->update(*workspace, 1.0f / 60.0f);
    };
    auto resetPair = [&](BaseCube& dynamicCube, BaseCube& anchoredCube,
                         const Vector3& position) {
        dynamicCube.teleportTo(position);
        anchoredCube.teleportTo(position);
        physics->setLinearVelocity(dynamicCube, {});
        physics->setAngularVelocity(dynamicCube, {});
        if (!anchoredCube.Anchored) {
            physics->setLinearVelocity(anchoredCube, {});
            physics->setAngularVelocity(anchoredCube, {});
        }
    };

    step();
    result.initialSelfContacts = selfContacts;
    result.initialToolContacts = toolContacts;
    RaycastHit selfPartHit;
    result.raycastDetectedSelfPart = physics->raycast(
        Vector3(-18, 10, 0), Vector3(0, -1, 0), 16.0f, selfPartHit) &&
        (selfPartHit.instance == selfCharacter.root.get() ||
         selfPartHit.instance == selfBlock.get());
    result.overlapDetectedSelfBlock =
        physics->findOverlapping(*selfCharacter.root, "BaseCube") ==
        selfBlock.get();

    ragdollCharacter.humanoid->enterRagdoll(physics);
    resetPair(*ragdollTorso, *ragdollHead, {18, 2, 0});
    step();
    result.ragdollSelfContacts = ragdollContacts;

    ragdollHead->setParent(workspace);
    resetPair(*ragdollTorso, *ragdollHead, {18, 2, 0});
    step();
    result.ragdollContactsAfterReparentOut = ragdollContacts;

    selfBlock->setParent(workspace);
    resetPair(*selfCharacter.root, *selfBlock, {-18, 2, 0});
    step();
    result.selfContactsAfterReparentOut = selfContacts;

    selfCharacter.model->addChild(selfBlock);
    resetPair(*selfCharacter.root, *selfBlock, {-18, 2, 0});
    step();
    result.selfContactsAfterReparentBack = selfContacts;

    selfCharacter.model->removeChild(selfCharacter.humanoid->Name);
    resetPair(*selfCharacter.root, *selfBlock, {-18, 2, 0});
    step();
    result.selfContactsAfterHumanoidRemove = selfContacts;

    selfCharacter.model->addChild(selfCharacter.humanoid);
    resetPair(*selfCharacter.root, *selfBlock, {-18, 2, 0});
    step();
    result.selfContactsAfterHumanoidRestore = selfContacts;

    tool->setParent(workspace);
    resetPair(*toolCharacter.root, *handle, {-12, 2, 0});
    step();
    result.toolContactsAfterReparentOut = toolContacts;

    toolCharacter.model->addChild(tool);
    resetPair(*toolCharacter.root, *handle, {-12, 2, 0});
    step();
    result.toolContactsAfterReparentBack = toolContacts;

    Physics::s_contactCallback = previousContactCallback;
    return result;
}

int runSeatNetworkRegression() {
    int failures = 0;
    auto expect = [&](bool ok, const char* msg) {
        std::cout << "[SeatNetworkRegression] " << (ok ? "PASS: " : "FAIL: ") << msg << '\n';
        if (!ok) ++failures;
    };
    auto workspace = std::make_shared<Workspace>();
    workspace->Gravity = {};
    workspace->initPhysics();
    auto character = std::make_shared<Model>();
    character->Name = "SeatRegressionCharacter";
    auto root = std::make_shared<Cube>(Vector3(0, 1.0f, 0), Vector3(2, 2, 2), Cube::defaultTextureID);
    root->Name = "Root";
    character->addChild(root);
    auto humanoid = std::make_shared<Humanoid>();
    character->addChild(humanoid);
    humanoid->resolveParts(character.get());
    workspace->addChild(character);
    auto seat = std::make_shared<Seat>(Vector3(0, 0, 0), Vector3(2, 1, 2), Cube::defaultTextureID);
    seat->Name = "RegressionSeat";
    workspace->addChild(seat);
    auto* physics = workspace->getPhysicsEngine();
    physics->update(*workspace, 1.0f / 60.0f);
    humanoid->move(Vector3(0, 0, -1), Vector3(1, 0, 0), false, Vector3{}, false,
                   physics, false, false, 0.0f, 0.0f, 0.15f, 1.0f / 60.0f);
    expect(humanoid->isSeated() && seat->isOccupied(), "Humanoid seats and Seat occupant is set");
    humanoid->standUp(physics);
    const Vector3 velocity = physics->getLinearVelocity(*root);
    const bool seatWeldRemoved = std::none_of(workspace->getChildren().begin(), workspace->getChildren().end(),
        [](const auto& entry) { return entry.second && entry.second->Name == "SeatWeld"; });
    expect(!humanoid->isSeated() && !seat->isOccupied(), "standUp clears SeatWeld and occupant");
    expect(seatWeldRemoved, "standUp removes SeatWeld instance");
    expect(velocity.y >= humanoid->JumpPower - 0.1f, "standUp applies upward hop velocity");
    return failures;
}

int runHumanoidRigCollisionRegression() {
    constexpr float expectedRootY = 2.0f;
    const std::array<HumanoidRigCollisionCase, 7> cases{{
        {"baseline_root_only", false, false, false, false, false, false, false},
        {"torso_original", true, false, true, false, false, false, false},
        {"hair_weld_original", false, false, false, true, false, true, true},
        {"torso_and_hair_original", true, false, true, true, false, true, true},
        {"torso_and_hair_no_weld", true, false, true, true, false, true, false},
        {"torso_and_hair_noncollide", true, false, false, true, false, false, true},
        {"torso_and_hair_anchored", true, true, true, true, true, true, true},
    }};

    int failures = 0;
    bool allCasesHealthy = true;
    for (size_t index = 0; index < cases.size(); ++index) {
        const auto& testCase = cases[index];
        const HumanoidRigCollisionResult result =
            simulateHumanoidRigCollision(testCase);
        const float penetration = std::max(0.0f, expectedRootY - result.minimumY);
        const float speed = vectorLength(result.finalVelocity);
        const bool healthy = result.available && result.finite &&
            std::abs(result.finalY - expectedRootY) <= 0.15f &&
            speed <= 0.5f && result.sameCharacterContacts == 0 &&
            result.floorContacts > 0;
        if (!healthy) {
            ++failures;
            allCasesHealthy = false;
        }

        std::cout << "[HumanoidRigCollisionRegression]"
                  << " backend=" << physicsBackendName(result.backend)
                  << " case=" << testCase.name
                  << " expected_y=" << expectedRootY
                  << " final_y=" << result.finalY
                  << " min_y=" << result.minimumY
                  << " penetration=" << penetration
                  << " velocity=[" << result.finalVelocity.x << ','
                  << result.finalVelocity.y << ',' << result.finalVelocity.z << ']'
                  << " speed=" << speed
                  << " same_character_contacts=" << result.sameCharacterContacts
                  << " floor_contacts=" << result.floorContacts
                  << " available=" << (result.available ? "true" : "false")
                  << " finite=" << (result.finite ? "true" : "false")
                  << " result=" << (healthy ? "PASS" : "FAIL")
                  << '\n';
    }

    const CharacterCollisionFilterProbeResult probe =
        runCharacterCollisionFilterProbe();
    const bool probeHealthy = probe.available &&
        probe.initialSelfContacts == 0 &&
        probe.selfContactsAfterReparentOut > probe.initialSelfContacts &&
        probe.selfContactsAfterReparentBack == probe.selfContactsAfterReparentOut &&
        probe.selfContactsAfterHumanoidRemove > probe.selfContactsAfterReparentBack &&
        probe.selfContactsAfterHumanoidRestore == probe.selfContactsAfterHumanoidRemove &&
        probe.initialToolContacts == 0 &&
        probe.toolContactsAfterReparentOut > probe.initialToolContacts &&
        probe.toolContactsAfterReparentBack == probe.toolContactsAfterReparentOut &&
        probe.remoteNpcContacts > 0 && probe.floorContacts > 0 &&
        probe.cloneContacts > 0 && probe.nestedCharacterContacts > 0 &&
        probe.ragdollSelfContacts == 0 &&
        probe.ragdollContactsAfterReparentOut > probe.ragdollSelfContacts &&
        probe.raycastDetectedSelfPart && probe.overlapDetectedSelfBlock;
    if (!probeHealthy) ++failures;
    std::cout << "[HumanoidRigCollisionRegression] filter_probe"
              << " initial_self=" << probe.initialSelfContacts
              << " self_out=" << probe.selfContactsAfterReparentOut
              << " self_back=" << probe.selfContactsAfterReparentBack
              << " humanoid_removed=" << probe.selfContactsAfterHumanoidRemove
              << " humanoid_restored=" << probe.selfContactsAfterHumanoidRestore
              << " initial_tool=" << probe.initialToolContacts
              << " tool_out=" << probe.toolContactsAfterReparentOut
              << " tool_back=" << probe.toolContactsAfterReparentBack
              << " remote_npc=" << probe.remoteNpcContacts
              << " floor=" << probe.floorContacts
              << " clone=" << probe.cloneContacts
              << " nested=" << probe.nestedCharacterContacts
              << " ragdoll_self=" << probe.ragdollSelfContacts
              << " ragdoll_out=" << probe.ragdollContactsAfterReparentOut
              << " raycast_self="
              << (probe.raycastDetectedSelfPart ? "true" : "false")
              << " overlap_self="
              << (probe.overlapDetectedSelfBlock ? "true" : "false")
              << " result=" << (probeHealthy ? "PASS" : "FAIL") << '\n';

    std::cout << "[HumanoidRigCollisionRegression] rig_cases="
              << (allCasesHealthy ? "PASS" : "FAIL")
              << " failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
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

    auto axisMotorAnchor = addMigrationCube(
        workspace, "AxisMotorAnchor", {0, 100, 120}, {2, 2, 2}, true);
    auto axisMotorCylinder = std::make_shared<Cylinder>(
        Vector3(0, 100, 120), Vector3(2, 6, 2));
    axisMotorCylinder->Name = "AxisMotorCylinder";
    axisMotorCylinder->setRotation(
        Quaternion::fromAxisAngle(Vector3(1, 0, 0), 90.0f));
    workspace->addChild(axisMotorCylinder);
    auto axisMotorAttachment0 = std::make_shared<Attachment>();
    axisMotorAttachment0->Name = "AxisMotorAttachment0";
    axisMotorAnchor->addChild(axisMotorAttachment0);
    auto axisMotorAttachment1 =
        std::make_shared<Attachment>(Vector3(0, 2, 0));
    axisMotorAttachment1->Name = "AxisMotorAttachment1";
    axisMotorCylinder->addChild(axisMotorAttachment1);
    auto axisMotor = std::make_shared<Motor>(
        axisMotorAnchor, axisMotorCylinder);
    axisMotor->Name = "AxisMotorDiagnostic";
    axisMotor->Axis = Vector3(0, 0, 1);
    axisMotor->DriveVelocity = 0.0f;
    axisMotor->MaxForce = 1000.0f;
    workspace->addChild(axisMotor);
    YAML::Node axisMotorAttachment0Name;
    axisMotorAttachment0Name = axisMotorAttachment0->Name;
    axisMotor->setProperty("Attachment0", axisMotorAttachment0Name);
    YAML::Node axisMotorAttachment1Name;
    axisMotorAttachment1Name = axisMotorAttachment1->Name;
    axisMotor->setProperty("Attachment1", axisMotorAttachment1Name);

    const CFrame axisMotorInitialFrame = axisMotorCylinder->getWorldCFrame();
    const Vector3 axisMotorInitialAxis =
        axisMotorInitialFrame.Rotation.rotate(Vector3(0, 1, 0)).normalize();
    const float axisMotorInitialWorldZDot =
        std::abs(Vector3::Dot(axisMotorInitialAxis, Vector3(0, 0, 1)));
    const bool axisMotorInitialFinite =
        finiteCFrame(axisMotorInitialFrame) && finiteVector(axisMotorInitialAxis);
    const float axisMotorInitialAttachmentSeparation = positionDistance(
        axisMotorAttachment0->getWorldCFrame().Position,
        axisMotorAttachment1->getWorldCFrame().Position);
    physics->update(*workspace, 0.0f);
    const PhysicsConstraintHandle axisMotorInitialHandle =
        axisMotor->getConstraintHandle();
    physics->setGravityEnabled(*axisMotorCylinder, false);
    auto axisMotorMarker = [&]() {
        return axisMotorCylinder->getWorldCFrame().Rotation
            .rotate(Vector3(1, 0, 0));
    };
    auto signedProjectedAngle = [](
        const Vector3& fromValue, const Vector3& toValue,
        const Vector3& axisValue) {
        const Vector3 axis = axisValue.normalize();
        const Vector3 from =
            (fromValue - axis * Vector3::Dot(fromValue, axis)).normalize();
        const Vector3 to =
            (toValue - axis * Vector3::Dot(toValue, axis)).normalize();
        return std::atan2(
            Vector3::Dot(axis, Vector3::Cross(from, to)),
            std::clamp(Vector3::Dot(from, to), -1.0f, 1.0f));
    };

    for (int step = 0; step < 180; ++step) {
        axisMotor->setDriveVelocity(0.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    const PhysicsConstraintHandle axisMotorSleepingHandle =
        axisMotor->getConstraintHandle();
    const Vector3 positiveReference = axisMotorMarker();
    for (int step = 0; step < 60; ++step) {
        axisMotor->setDriveVelocity(2.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    const Vector3 motorWorldAxis = axisMotorAnchor->getWorldCFrame().Rotation
        .rotate(axisMotor->Axis).normalize();
    const float positiveMotorAngle = signedProjectedAngle(
        positiveReference, axisMotorMarker(), motorWorldAxis);

    for (int step = 0; step < 60; ++step) {
        axisMotor->setDriveVelocity(0.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    const Vector3 negativeReference = axisMotorMarker();
    for (int step = 0; step < 60; ++step) {
        axisMotor->setDriveVelocity(-2.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    const float negativeMotorAngle = signedProjectedAngle(
        negativeReference, axisMotorMarker(), motorWorldAxis);

    for (int step = 0; step < 60; ++step) {
        axisMotor->setDriveVelocity(0.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    axisMotor->setMaxForce(0.0f);
    const Vector3 zeroTorqueReference = axisMotorMarker();
    axisMotor->setDriveVelocity(2.0f);
    for (int step = 0; step < 60; ++step) {
        axisMotor->setDriveVelocity(2.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    const float zeroTorqueMotorAngle = signedProjectedAngle(
        zeroTorqueReference, axisMotorMarker(), motorWorldAxis);

    const Vector3 recoveredReference = axisMotorMarker();
    axisMotor->setMaxForce(1000.0f);
    for (int step = 0; step < 60; ++step)
        physics->update(*workspace, 1.0f / 60.0f);
    const float recoveredMotorAngle = signedProjectedAngle(
        recoveredReference, axisMotorMarker(), motorWorldAxis);
    const PhysicsConstraintHandle axisMotorUpdatedHandle =
        axisMotor->getConstraintHandle();

    const CFrame axisMotorFinalFrame = axisMotorCylinder->getWorldCFrame();
    const Vector3 axisMotorFinalAxis =
        axisMotorFinalFrame.Rotation.rotate(Vector3(0, 1, 0)).normalize();
    const float axisMotorWorldZDot =
        std::abs(Vector3::Dot(axisMotorFinalAxis, Vector3(0, 0, 1)));
    const float axisMotorAttachmentSeparation = positionDistance(
        axisMotorAttachment0->getWorldCFrame().Position,
        axisMotorAttachment1->getWorldCFrame().Position);
    const bool axisMotorFinalFinite =
        finiteCFrame(axisMotorFinalFrame) && finiteVector(axisMotorFinalAxis);
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=motor_axis_diagnostic"
              << " initial_axis=[" << axisMotorInitialAxis.x << ','
              << axisMotorInitialAxis.y << ',' << axisMotorInitialAxis.z << ']'
              << " initial_abs_world_z_dot=" << axisMotorInitialWorldZDot
              << " final_axis=[" << axisMotorFinalAxis.x << ','
              << axisMotorFinalAxis.y << ',' << axisMotorFinalAxis.z << ']'
              << " final_quat=[" << axisMotorFinalFrame.Rotation.w << ','
              << axisMotorFinalFrame.Rotation.x << ','
              << axisMotorFinalFrame.Rotation.y << ','
              << axisMotorFinalFrame.Rotation.z << ']'
              << " abs_world_z_dot=" << axisMotorWorldZDot
              << " initial_attachment_separation="
              << axisMotorInitialAttachmentSeparation
              << " attachment_separation=" << axisMotorAttachmentSeparation
              << " initial_handle=" << axisMotorInitialHandle.value
              << " sleeping_handle=" << axisMotorSleepingHandle.value
              << " updated_handle=" << axisMotorUpdatedHandle.value
              << " positive_angle=" << positiveMotorAngle
              << " negative_angle=" << negativeMotorAngle
              << " zero_torque_angle=" << zeroTorqueMotorAngle
              << " recovered_angle=" << recoveredMotorAngle
              << "\n";
    expect(axisMotorInitialFinite && axisMotorFinalFinite &&
               axisMotorInitialHandle &&
               axisMotorSleepingHandle == axisMotorInitialHandle &&
               axisMotorUpdatedHandle == axisMotorInitialHandle &&
               axisMotorInitialWorldZDot >= 0.999f &&
               axisMotorWorldZDot >= 0.999f &&
               std::abs(axisMotorInitialAttachmentSeparation - 2.0f) <= 0.05f &&
               axisMotorAttachmentSeparation <= 0.05f &&
               std::isfinite(positiveMotorAngle) && positiveMotorAngle > 0.5f &&
               std::isfinite(negativeMotorAngle) && negativeMotorAngle < -0.5f &&
               std::isfinite(zeroTorqueMotorAngle) &&
               std::abs(zeroTorqueMotorAngle) <= 0.15f &&
               std::isfinite(recoveredMotorAngle) && recoveredMotorAngle > 0.5f,
           "Motor wakes, reverses, and resumes without replacing its handle");

    axisMotor->setAxis(Vector3(0, 0, 1));
    const PhysicsConstraintHandle axisMotorSameAxisHandle =
        axisMotor->getConstraintHandle();
    axisMotor->setAxis(Vector3(0, 0, -1));
    // frame/topology 変更は固定step前の binding reconciliation で反映する。
    physics->update(*workspace, 1.0f / 60.0f);
    const PhysicsConstraintHandle axisMotorChangedAxisHandle =
        axisMotor->getConstraintHandle();
    axisMotor->setDriveVelocity(0.0f);
    for (int step = 1; step < 30; ++step)
        physics->update(*workspace, 1.0f / 60.0f);
    const CFrame axisMotorReframedCylinder =
        axisMotorCylinder->getWorldCFrame();
    const Vector3 axisMotorReframedLocalY =
        axisMotorReframedCylinder.Rotation.rotate(Vector3(0, 1, 0)).normalize();
    const Vector3 axisMotorExpectedReframedAxis =
        axisMotorAnchor->getWorldCFrame().Rotation
            .rotate(axisMotor->Axis).normalize();
    const float axisMotorReframedAxisDot = std::abs(Vector3::Dot(
        axisMotorReframedLocalY, axisMotorExpectedReframedAxis));
    const float axisMotorReframedAnchorError = positionDistance(
        axisMotorAttachment0->getWorldCFrame().Position,
        axisMotorAttachment1->getWorldCFrame().Position);
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=motor_axis_setter"
              << " previous_handle=" << axisMotorUpdatedHandle.value
              << " same_axis_handle=" << axisMotorSameAxisHandle.value
              << " changed_axis_handle=" << axisMotorChangedAxisHandle.value
              << " expected_axis=[" << axisMotorExpectedReframedAxis.x << ','
              << axisMotorExpectedReframedAxis.y << ','
              << axisMotorExpectedReframedAxis.z << ']'
              << " cylinder_local_y=[" << axisMotorReframedLocalY.x << ','
              << axisMotorReframedLocalY.y << ','
              << axisMotorReframedLocalY.z << ']'
              << " abs_axis_dot=" << axisMotorReframedAxisDot
              << " anchor_error=" << axisMotorReframedAnchorError
              << "\n";
    expect(axisMotorSameAxisHandle == axisMotorUpdatedHandle &&
               axisMotorChangedAxisHandle &&
               axisMotorChangedAxisHandle != axisMotorUpdatedHandle &&
               finiteCFrame(axisMotorReframedCylinder) &&
               finiteVector(axisMotorExpectedReframedAxis) &&
               axisMotorReframedAxisDot >= 0.999f &&
               axisMotorReframedAnchorError <= 0.05f,
           "Motor Axis setter recreates frame only when axis changes");

    const Vector3 carStart(-120.0f, 3.25f, 100.0f);
    auto carChassis = addMigrationCube(
        workspace, "MotorWakeCarChassis", carStart, {8, 1, 4});
    carChassis->LockFlags =
        PhysicsLockFlags::AngularX |
        PhysicsLockFlags::AngularY |
        PhysicsLockFlags::AngularZ;
    struct MotorWakeWheel {
        std::shared_ptr<Cylinder> wheel;
        std::shared_ptr<Attachment> chassisAttachment;
        std::shared_ptr<Attachment> wheelAttachment;
        std::shared_ptr<Motor> motor;
        PhysicsConstraintHandle handle;
    };
    std::vector<MotorWakeWheel> carWheels;
    const std::array<Vector3, 4> carWheelOffsets = {
        Vector3(-3.0f, -1.2f, -2.5f),
        Vector3(-3.0f, -1.2f,  2.5f),
        Vector3( 3.0f, -1.2f, -2.5f),
        Vector3( 3.0f, -1.2f,  2.5f),
    };
    for (std::size_t index = 0; index < carWheelOffsets.size(); ++index) {
        const std::string suffix = std::to_string(index);
        auto chassisAttachment =
            std::make_shared<Attachment>(carWheelOffsets[index]);
        chassisAttachment->Name = "MotorWakeCarChassisAttachment" + suffix;
        carChassis->addChild(chassisAttachment);

        auto wheel = std::make_shared<Cylinder>(
            carStart + carWheelOffsets[index], Vector3(4, 1, 4));
        wheel->Name = "MotorWakeCarWheel" + suffix;
        wheel->setRotation(
            Quaternion::fromAxisAngle(Vector3(1, 0, 0), 90.0f));
        workspace->addChild(wheel);
        auto wheelAttachment = std::make_shared<Attachment>();
        wheelAttachment->Name = "MotorWakeCarWheelAttachment";
        wheel->addChild(wheelAttachment);

        auto wheelMotor = std::make_shared<Motor>(carChassis, wheel);
        wheelMotor->Name = "MotorWakeCarMotor" + suffix;
        wheelMotor->Axis = Vector3(0, 0, 1);
        wheelMotor->DriveVelocity = 0.0f;
        wheelMotor->MaxForce = 5000.0f;
        workspace->addChild(wheelMotor);
        YAML::Node chassisAttachmentName;
        chassisAttachmentName = chassisAttachment->Name;
        wheelMotor->setProperty("Attachment0", chassisAttachmentName);
        YAML::Node wheelAttachmentName;
        wheelAttachmentName = wheelAttachment->Name;
        wheelMotor->setProperty("Attachment1", wheelAttachmentName);
        carWheels.push_back({
            wheel, chassisAttachment, wheelAttachment, wheelMotor, {}});
    }

    physics->update(*workspace, 0.0f);
    for (MotorWakeWheel& value : carWheels)
        value.handle = value.motor->getConstraintHandle();
    for (int step = 0; step < 180; ++step) {
        for (MotorWakeWheel& value : carWheels)
            value.motor->setDriveVelocity(0.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    const Vector3 settledCarPosition = carChassis->getWorldPosition();
    for (int step = 0; step < 180; ++step) {
        for (MotorWakeWheel& value : carWheels)
            value.motor->setDriveVelocity(6.0f);
        physics->update(*workspace, 1.0f / 60.0f);
    }
    const Vector3 finalCarPosition = carChassis->getWorldPosition();
    const float carHorizontalDistance = std::sqrt(
        (finalCarPosition.x - settledCarPosition.x) *
            (finalCarPosition.x - settledCarPosition.x) +
        (finalCarPosition.z - settledCarPosition.z) *
            (finalCarPosition.z - settledCarPosition.z));
    bool carWheelsValid = finiteCFrame(carChassis->getWorldCFrame());
    float minimumCarWheelAxisDot = 1.0f;
    float maximumCarAttachmentSeparation = 0.0f;
    bool carHandlesStable = true;
    const Vector3 carExpectedAxis = carChassis->getWorldCFrame().Rotation
        .rotate(Vector3(0, 0, 1)).normalize();
    for (MotorWakeWheel& value : carWheels) {
        const CFrame wheelFrame = value.wheel->getWorldCFrame();
        const Vector3 wheelAxis = wheelFrame.Rotation
            .rotate(Vector3(0, 1, 0)).normalize();
        minimumCarWheelAxisDot = std::min(
            minimumCarWheelAxisDot,
            std::abs(Vector3::Dot(wheelAxis, carExpectedAxis)));
        maximumCarAttachmentSeparation = std::max(
            maximumCarAttachmentSeparation,
            positionDistance(
                value.chassisAttachment->getWorldCFrame().Position,
                value.wheelAttachment->getWorldCFrame().Position));
        carHandlesStable = carHandlesStable && value.handle &&
            value.motor->getConstraintHandle() == value.handle;
        carWheelsValid = carWheelsValid && finiteCFrame(wheelFrame) &&
            finiteVector(wheelAxis);
        value.motor->setDriveVelocity(0.0f);
    }
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=motor_wake_car"
              << " settled_position=[" << settledCarPosition.x << ','
              << settledCarPosition.y << ',' << settledCarPosition.z << ']'
              << " final_position=[" << finalCarPosition.x << ','
              << finalCarPosition.y << ',' << finalCarPosition.z << ']'
              << " horizontal_distance=" << carHorizontalDistance
              << " minimum_axis_dot=" << minimumCarWheelAxisDot
              << " maximum_attachment_separation="
              << maximumCarAttachmentSeparation
              << " handles_stable=" << (carHandlesStable ? "true" : "false")
              << " finite=" << (carWheelsValid ? "true" : "false")
              << "\n";
    expect(carWheelsValid && carHandlesStable &&
               std::isfinite(carHorizontalDistance) &&
               carHorizontalDistance > 1.0f &&
               minimumCarWheelAxisDot >= 0.999f &&
               maximumCarAttachmentSeparation <= 0.05f,
           "four-wheel Motor car wakes and travels after resting");

    struct MotorScaleResult {
        float horizontalDistance = 0.0f;
        float averageRelativeAngularVelocity = 0.0f;
        float maximumAttachmentSeparation = 0.0f;
        float minimumAxisDot = 1.0f;
        bool handlesStable = true;
        bool finite = true;
    };
    auto runMotorScaleDiagnostic = [&]
        (const std::string& caseName, const Vector3& origin,
         bool fourWheels, bool grounded, bool mismatchedAnchors,
         bool repeatVelocitySetter) {
        auto chassis = addMigrationCube(
            workspace, "MotorScaleChassis" + caseName,
            origin, Vector3(20, 2, 10), !grounded && !fourWheels);
        struct ScaleWheel {
            std::shared_ptr<Cylinder> wheel;
            std::shared_ptr<Attachment> chassisAttachment;
            std::shared_ptr<Attachment> wheelAttachment;
            std::shared_ptr<Motor> motor;
            PhysicsConstraintHandle handle;
            Vector3 previousMarker;
            float accumulatedAngle = 0.0f;
        };
        std::vector<ScaleWheel> wheels;
        const std::array<Vector3, 4> chassisOffsets = {
            Vector3(-8.0f, -0.5f, -5.0f),
            Vector3(-8.0f, -0.5f,  5.0f),
            Vector3( 8.0f, -0.5f, -5.0f),
            Vector3( 8.0f, -0.5f,  5.0f),
        };
        const std::size_t wheelCount = fourWheels ? 4u : 1u;
        for (std::size_t index = 0; index < wheelCount; ++index) {
            const std::string suffix = std::to_string(index);
            const float side = chassisOffsets[index].z < 0.0f ? -1.0f : 1.0f;
            const float mismatch = mismatchedAnchors
                ? (side < 0.0f ? 2.0f : 1.0f) : 0.0f;
            const Vector3 wheelCenter = origin + Vector3(
                chassisOffsets[index].x,
                chassisOffsets[index].y,
                side * (5.5f + mismatch));

            auto chassisAttachment =
                std::make_shared<Attachment>(chassisOffsets[index]);
            chassisAttachment->Name =
                "MotorScaleChassisAttachment" + caseName + suffix;
            chassis->addChild(chassisAttachment);
            auto wheel = std::make_shared<Cylinder>(
                wheelCenter, Vector3(4, 1, 4));
            wheel->Name = "MotorScaleWheel" + caseName + suffix;
            wheel->setRotation(
                Quaternion::fromAxisAngle(Vector3(1, 0, 0), 90.0f));
            workspace->addChild(wheel);
            auto wheelAttachment = std::make_shared<Attachment>(
                Vector3(0, side < 0.0f ? 0.5f : -0.5f, 0));
            wheelAttachment->Name = "MotorScaleWheelAttachment";
            wheel->addChild(wheelAttachment);
            auto wheelMotor = std::make_shared<Motor>(chassis, wheel);
            wheelMotor->Name =
                "MotorScaleDiagnostic" + caseName + suffix;
            wheelMotor->Axis = Vector3(0, 0, 1);
            wheelMotor->DriveVelocity = 0.0f;
            wheelMotor->MaxForce = 1000.0f;
            workspace->addChild(wheelMotor);
            YAML::Node chassisAttachmentName;
            chassisAttachmentName = chassisAttachment->Name;
            wheelMotor->setProperty("Attachment0", chassisAttachmentName);
            YAML::Node wheelAttachmentName;
            wheelAttachmentName = wheelAttachment->Name;
            wheelMotor->setProperty("Attachment1", wheelAttachmentName);
            wheels.push_back({
                wheel, chassisAttachment, wheelAttachment, wheelMotor,
                {}, {}, 0.0f});
        }

        physics->update(*workspace, 0.0f);
        if (!grounded) {
            physics->setGravityEnabled(*chassis, false);
            for (ScaleWheel& value : wheels)
                physics->setGravityEnabled(*value.wheel, false);
        }
        for (int step = 0; step < 180; ++step)
            physics->update(*workspace, 1.0f / 60.0f);
        for (ScaleWheel& value : wheels) {
            value.handle = value.motor->getConstraintHandle();
            const Quaternion chassisInverse =
                chassis->getWorldCFrame().Rotation.conjugate();
            value.previousMarker = chassisInverse.rotate(
                value.wheel->getWorldCFrame().Rotation.rotate(Vector3(1, 0, 0)));
        }
        const Vector3 settledPosition = chassis->getWorldPosition();
        for (ScaleWheel& value : wheels)
            value.motor->setDriveVelocity(15.0f);
        for (int step = 0; step < 180; ++step) {
            if (repeatVelocitySetter) {
                for (ScaleWheel& value : wheels)
                    value.motor->setDriveVelocity(15.0f);
            }
            physics->update(*workspace, 1.0f / 60.0f);
            const Quaternion chassisInverse =
                chassis->getWorldCFrame().Rotation.conjugate();
            for (ScaleWheel& value : wheels) {
                const Vector3 marker = chassisInverse.rotate(
                    value.wheel->getWorldCFrame().Rotation
                        .rotate(Vector3(1, 0, 0)));
                value.accumulatedAngle += signedProjectedAngle(
                    value.previousMarker, marker, Vector3(0, 0, 1));
                value.previousMarker = marker;
            }
        }

        MotorScaleResult result;
        const Vector3 finalPosition = chassis->getWorldPosition();
        result.horizontalDistance = std::sqrt(
            (finalPosition.x - settledPosition.x) *
                (finalPosition.x - settledPosition.x) +
            (finalPosition.z - settledPosition.z) *
                (finalPosition.z - settledPosition.z));
        result.finite = finiteCFrame(chassis->getWorldCFrame());
        const Vector3 expectedAxis = chassis->getWorldCFrame().Rotation
            .rotate(Vector3(0, 0, 1)).normalize();
        float totalAngularVelocity = 0.0f;
        for (ScaleWheel& value : wheels) {
            const CFrame wheelFrame = value.wheel->getWorldCFrame();
            const Vector3 wheelAxis = wheelFrame.Rotation
                .rotate(Vector3(0, 1, 0)).normalize();
            result.minimumAxisDot = std::min(
                result.minimumAxisDot,
                std::abs(Vector3::Dot(wheelAxis, expectedAxis)));
            result.maximumAttachmentSeparation = std::max(
                result.maximumAttachmentSeparation,
                positionDistance(
                    value.chassisAttachment->getWorldCFrame().Position,
                    value.wheelAttachment->getWorldCFrame().Position));
            result.handlesStable = result.handlesStable && value.handle &&
                value.motor->getConstraintHandle() == value.handle;
            result.finite = result.finite && finiteCFrame(wheelFrame) &&
                finiteVector(wheelAxis) &&
                std::isfinite(value.accumulatedAngle);
            totalAngularVelocity += value.accumulatedAngle / 3.0f;
            value.motor->setDriveVelocity(0.0f);
        }
        result.averageRelativeAngularVelocity =
            totalAngularVelocity / static_cast<float>(wheels.size());
        std::cout << "[PhysicsMigrationRegression] backend=" << backend
                  << " metric=motor_scale_diagnostic"
                  << " case=" << caseName
                  << " grounded=" << (grounded ? "true" : "false")
                  << " mismatch=" << (mismatchedAnchors ? "true" : "false")
                  << " repeat_setter=" << (repeatVelocitySetter ? "true" : "false")
                  << " horizontal_distance=" << result.horizontalDistance
                  << " relative_angular_velocity="
                  << result.averageRelativeAngularVelocity
                  << " max_anchor_error="
                  << result.maximumAttachmentSeparation
                  << " min_axis_dot=" << result.minimumAxisDot
                  << " handles_stable="
                  << (result.handlesStable ? "true" : "false")
                  << " finite=" << (result.finite ? "true" : "false")
                  << "\n";
        return result;
    };

    const MotorScaleResult scaleAir = runMotorScaleDiagnostic(
        "AirSingle", Vector3(100, 80, 145), false, false, false, true);
    const MotorScaleResult scaleGroundOnce = runMotorScaleDiagnostic(
        "GroundOnce", Vector3(0, 2.5f, 130), true, true, false, false);
    const MotorScaleResult scaleGroundRepeat = runMotorScaleDiagnostic(
        "GroundRepeat", Vector3(0, 2.5f, 70), true, true, false, true);
    const MotorScaleResult scaleGroundMismatch = runMotorScaleDiagnostic(
        "GroundMismatch", Vector3(0, 2.5f, -130), true, true, true, true);
    expect(scaleAir.finite && scaleAir.handlesStable &&
               scaleAir.averageRelativeAngularVelocity >= 13.5f &&
               scaleAir.averageRelativeAngularVelocity <= 15.5f &&
               scaleAir.maximumAttachmentSeparation <= 0.05f &&
               scaleAir.minimumAxisDot >= 0.999f,
           "isolated full-scale Motor reaches target speed and preserves its frame");
    expect(scaleGroundOnce.finite && scaleGroundRepeat.finite &&
               scaleGroundMismatch.finite &&
               scaleGroundOnce.handlesStable &&
               scaleGroundRepeat.handlesStable &&
               scaleGroundMismatch.handlesStable,
           "full-scale grounded Motor diagnostics remain finite");
    const auto fullScaleDriveInCompatibilityEnvelope =
        [](const MotorScaleResult& result) {
            return result.horizontalDistance >= 70.0f &&
                   result.horizontalDistance <= 105.0f &&
                   result.averageRelativeAngularVelocity >= 11.5f &&
                   result.averageRelativeAngularVelocity <= 17.5f &&
                   result.maximumAttachmentSeparation <= 0.1f &&
                   result.minimumAxisDot >= 0.999f;
        };
    expect(fullScaleDriveInCompatibilityEnvelope(scaleGroundOnce) &&
               fullScaleDriveInCompatibilityEnvelope(scaleGroundRepeat) &&
               fullScaleDriveInCompatibilityEnvelope(scaleGroundMismatch),
           "full-scale Motor drive matches the PhysX compatibility envelope");
    expect(std::abs(scaleGroundOnce.horizontalDistance -
                    scaleGroundRepeat.horizontalDistance) <= 1.0f &&
               std::abs(scaleGroundOnce.averageRelativeAngularVelocity -
                        scaleGroundRepeat.averageRelativeAngularVelocity) <= 0.2f,
           "repeating DriveVelocity each tick preserves Motor behavior");

    auto jumpLiquid = std::make_shared<LiquidCube>(
        Vector3(160, 120, -120), Vector3(20, 20, 20));
    jumpLiquid->Name = "MigrationJumpLiquid";
    jumpLiquid->Anchored = true;
    jumpLiquid->CanCollide = false;
    workspace->addChild(jumpLiquid);
    auto submergedJumpRoot = addMigrationCube(
        workspace, "SubmergedJumpRoot", {160, 120, -120}, {2, 2, 2});
    auto airborneJumpRoot = addMigrationCube(
        workspace, "AirborneJumpRoot", {190, 120, -120}, {2, 2, 2});
    auto submergedHumanoid = std::make_shared<Humanoid>();
    submergedHumanoid->Name = "SubmergedJumpHumanoid";
    submergedHumanoid->setRootPart(submergedJumpRoot);
    submergedHumanoid->setIsGroundedForReplication(false);
    workspace->addChild(submergedHumanoid);
    auto airborneHumanoid = std::make_shared<Humanoid>();
    airborneHumanoid->Name = "AirborneJumpHumanoid";
    airborneHumanoid->setRootPart(airborneJumpRoot);
    airborneHumanoid->setIsGroundedForReplication(false);
    workspace->addChild(airborneHumanoid);

    physics->update(*workspace, 0.0f);
    physics->setGravityEnabled(*submergedJumpRoot, false);
    physics->setGravityEnabled(*airborneJumpRoot, false);
    physics->setLinearVelocity(*submergedJumpRoot, Vector3());
    physics->setLinearVelocity(*airborneJumpRoot, Vector3());
    submergedHumanoid->jump(physics);
    airborneHumanoid->jump(physics);
    const float submergedJumpVelocity =
        physics->getLinearVelocity(*submergedJumpRoot).y;
    const float airborneJumpVelocity =
        physics->getLinearVelocity(*airborneJumpRoot).y;
    std::cout << "[PhysicsMigrationRegression] backend=" << backend
              << " metric=liquid_jump"
              << " submerged_velocity=" << submergedJumpVelocity
              << " airborne_velocity=" << airborneJumpVelocity << "\n";
    expect(std::abs(submergedJumpVelocity - submergedHumanoid->JumpPower) <=
               0.001f &&
               std::abs(airborneJumpVelocity) <= 0.001f,
           "submerged Humanoid can jump while airborne Humanoid cannot");

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

int runBox3DBuoyancyRegression() {
    auto workspace = std::make_shared<Workspace>();
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    if (physics->getBackendType() != PhysicsBackendType::Box3D) {
        std::cout << "[Box3DBuoyancyRegression] SKIP: this mode validates "
                     "Box3D buoyancy; run with --physics=box3d.\n";
        return 0;
    }

    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        std::cout << "[Box3DBuoyancyRegression] "
                  << (condition ? "PASS" : "FAIL") << ": " << name << "\n";
        if (!condition) ++failures;
    };
    expect(physics->isAvailable(), "Box3D backend initialization");
    if (!physics->isAvailable()) return 1;

    bool waveFormulaMatches = true;
    float maximumWaveFormulaError = 0.0f;
    for (float time : {0.0f, 0.5f, 2.0f}) {
        for (const Vector3& corner : {
                 Vector3(-0.5f, 0, -0.5f),
                 Vector3(-0.5f, 0,  0.5f),
                 Vector3( 0.5f, 0, -0.5f),
                 Vector3( 0.5f, 0,  0.5f)}) {
            const float expected = std::sin(
                time * LiquidCube::WAVE_ANGULAR_SPEED +
                corner.x * LiquidCube::WAVE_SPATIAL_FREQUENCY +
                corner.z * LiquidCube::WAVE_SPATIAL_FREQUENCY) *
                LiquidCube::WAVE_AMPLITUDE;
            const float actual = LiquidCube::waveHeight(corner.x, corner.z, time);
            const float error = std::abs(actual - expected);
            maximumWaveFormulaError = std::max(maximumWaveFormulaError, error);
            waveFormulaMatches = waveFormulaMatches && error <= 1.0e-6f;
        }
    }
    std::cout << "[Box3DBuoyancyRegression] metric=wave_formula"
              << " max_error=" << maximumWaveFormulaError << "\n";
    expect(waveFormulaMatches, "waveHeight matches deterministic formula at all corners/times");

    const std::uint64_t initialTick = physics->getSimulationTick();
    const float initialWaveTime = physics->getWaveTime();
    physics->update(*workspace, 1.0f / 60.0f);
    physics->update(*workspace, 1.0f / 60.0f);
    const std::uint64_t advancedTick = physics->getSimulationTick();
    const float advancedWaveTime = physics->getWaveTime();
    expect(advancedTick == initialTick + 2 &&
               std::abs(
                   advancedWaveTime - initialWaveTime - 2.0f / 60.0f) <= 1.0e-6f,
           "1/60 physics steps advance tick and wave time deterministically");

    workspace->PhysicsEnabled = false;
    const std::uint64_t pausedTick = physics->getSimulationTick();
    const float pausedWaveTime = physics->getWaveTime();
    for (int step = 0; step < 5; ++step)
        physics->update(*workspace, 1.0f / 60.0f);
    expect(physics->getSimulationTick() == pausedTick &&
               std::abs(physics->getWaveTime() - pausedWaveTime) <= 1.0e-7f,
           "PhysicsEnabled=false pauses tick and wave time");
    workspace->PhysicsEnabled = true;
    physics->update(*workspace, 1.0f / 60.0f);
    expect(physics->getSimulationTick() == pausedTick + 1,
           "physics clock resumes after PhysicsEnabled=true");

    physics->resetSimulationClockSynchronization();
    const std::uint32_t snapTick =
        static_cast<std::uint32_t>(physics->getSimulationTick() + 20);
    constexpr float snapAlpha = 0.25f;
    physics->synchronizeSimulationClock(snapTick, snapAlpha);
    std::uint32_t synchronizedTick = 0;
    float synchronizedAlpha = 0.0f;
    physics->getSynchronizedSimulationClock(
        synchronizedTick, synchronizedAlpha);
    std::cout << "[Box3DBuoyancyRegression] metric=clock_snap"
              << " requested_tick=" << snapTick
              << " actual_tick=" << synchronizedTick
              << " requested_alpha=" << snapAlpha
              << " actual_alpha=" << synchronizedAlpha << "\n";
    expect(synchronizedTick == snapTick &&
               std::abs(synchronizedAlpha - snapAlpha) <= 1.0e-5f,
           "synchronizeSimulationClock first sample snaps");

    physics->synchronizeSimulationClock(snapTick + 1, snapAlpha);
    const double phaseBeforeSlew = physics->getWavePhaseTicks();
    physics->update(*workspace, 1.0f / 60.0f);
    const double phaseAfterSlew = physics->getWavePhaseTicks();
    const double observedCorrection =
        phaseAfterSlew - phaseBeforeSlew - 1.0;
    std::cout << "[Box3DBuoyancyRegression] metric=clock_slew"
              << " correction_ticks=" << observedCorrection << "\n";
    expect(observedCorrection >= -1.0e-6 &&
               observedCorrection <= 0.10001 &&
               std::abs(observedCorrection - 0.1) <= 1.0e-4,
           "small clock difference slews by at most 0.1 tick per step");

    physics->resetSimulationClockSynchronization();
    constexpr std::uint32_t wrapTick = 0xFFFFFFFEu;
    constexpr float wrapAlpha = 0.75f;
    physics->synchronizeSimulationClock(wrapTick, wrapAlpha);
    physics->getSynchronizedSimulationClock(
        synchronizedTick, synchronizedAlpha);
    std::cout << "[Box3DBuoyancyRegression] metric=clock_wrap"
              << " requested_tick=" << wrapTick
              << " actual_tick=" << synchronizedTick
              << " requested_alpha=" << wrapAlpha
              << " actual_alpha=" << synchronizedAlpha << "\n";
    expect(synchronizedTick == wrapTick &&
               std::abs(synchronizedAlpha - wrapAlpha) <= 1.0e-5f,
           "synchronized simulation clock preserves U32 wrap-near phase");
    physics->resetSimulationClockSynchronization();

    auto primaryLiquid = std::make_shared<LiquidCube>(
        Vector3(0, 20, 0), Vector3(80, 40, 80));
    primaryLiquid->Name = "PrimaryLiquid";
    primaryLiquid->Anchored = true;
    primaryLiquid->Density = 2.0f;
    workspace->addChild(primaryLiquid);

    auto waterBox = addMigrationCube(
        workspace, "BuoyantBox", {0, 20, 0}, {2, 2, 2});
    auto dryBox = addMigrationCube(
        workspace, "DryBox", {100, 20, 0}, {2, 2, 2});

    auto waterSphere = std::make_shared<Sphere>(
        Vector3(-20, 20, 0), Vector3(2, 2, 2));
    waterSphere->Name = "BuoyantSphere";
    workspace->addChild(waterSphere);
    auto drySphere = std::make_shared<Sphere>(
        Vector3(120, 20, 0), Vector3(2, 2, 2));
    drySphere->Name = "DrySphere";
    workspace->addChild(drySphere);

    auto waterPrism = std::make_shared<TriangularPrism>(
        Vector3(20, 20, 0), Vector3(2, 2, 2));
    waterPrism->Name = "BuoyantPrism";
    workspace->addChild(waterPrism);
    auto dryPrism = std::make_shared<TriangularPrism>(
        Vector3(140, 20, 0), Vector3(2, 2, 2));
    dryPrism->Name = "DryPrism";
    workspace->addChild(dryPrism);

    auto zeroDensityLiquid = std::make_shared<LiquidCube>(
        Vector3(240, 20, 0), Vector3(40, 40, 40));
    zeroDensityLiquid->Name = "ZeroDensityLiquid";
    zeroDensityLiquid->Anchored = true;
    zeroDensityLiquid->Density = 0.0f;
    workspace->addChild(zeroDensityLiquid);
    auto zeroDensityBody = addMigrationCube(
        workspace, "ZeroDensityBody", {240, 20, 0}, {2, 2, 2});
    auto zeroDensityControl = addMigrationCube(
        workspace, "ZeroDensityControl", {300, 20, 0}, {2, 2, 2});

    auto rotatedLiquid = std::make_shared<LiquidCube>(
        Vector3(400, 20, 0), Vector3(40, 20, 10));
    rotatedLiquid->Name = "RotatedLiquid";
    rotatedLiquid->Anchored = true;
    rotatedLiquid->Density = 5.0f;
    rotatedLiquid->setRotation(
        Quaternion::fromAxisAngle(Vector3(0, 1, 0), 45.0f));
    workspace->addChild(rotatedLiquid);
    auto obbOutsideBody = addMigrationCube(
        workspace, "ObbOutsideBody", {418, 20, 4}, {1, 1, 1});
    auto obbOutsideControl = addMigrationCube(
        workspace, "ObbOutsideControl", {480, 20, 0}, {1, 1, 1});

    auto neutralLiquid = std::make_shared<LiquidCube>(
        Vector3(600, 20, 0), Vector3(40, 40, 40));
    neutralLiquid->Name = "NeutralDensityLiquid";
    neutralLiquid->Anchored = true;
    neutralLiquid->Density = 1.0f;
    workspace->addChild(neutralLiquid);
    auto neutralSphere = std::make_shared<Sphere>(
        Vector3(600, 20, 0), Vector3(2, 2, 2));
    neutralSphere->Name = "NeutralBuoyancySphere";
    workspace->addChild(neutralSphere);

    auto maintainVelocityLiquid = std::make_shared<LiquidCube>(
        Vector3(720, 20, 0), Vector3(40, 40, 40));
    maintainVelocityLiquid->Name = "MaintainVelocityLiquid";
    maintainVelocityLiquid->Anchored = true;
    maintainVelocityLiquid->Density = 1.0f;
    workspace->addChild(maintainVelocityLiquid);
    auto maintainVelocityCube = addMigrationCube(
        workspace, "MaintainVelocityBuoyantCube", {720, 20, 0}, {2, 2, 2});
    maintainVelocityCube->MassDensity = 0.25f;
    auto zeroVelocityForce = std::make_shared<Force>();
    zeroVelocityForce->Name = "MaintainZeroLinearVelocity";
    zeroVelocityForce->Enabled = true;
    zeroVelocityForce->Torque = false;
    zeroVelocityForce->MaintainVelocity = true;
    zeroVelocityForce->Value = Vector3(0, 0, 0);
    maintainVelocityCube->addChild(zeroVelocityForce);

    auto weldFirst = addMigrationCube(
        workspace, "BuoyantWeldFirst", {-8, 24, 20}, {2, 2, 2});
    auto weldSecond = addMigrationCube(
        workspace, "BuoyantWeldSecond", {-4, 24, 20}, {2, 2, 2});
    const CFrame weldInitialRelative =
        weldFirst->getWorldCFrame().inverse() * weldSecond->getWorldCFrame();
    auto buoyantWeld = std::make_shared<Weld>(weldFirst, weldSecond);
    buoyantWeld->Name = "BuoyantWeld";
    workspace->addChild(buoyantWeld);

    physics->update(*workspace, 0.0f);
    for (int step = 0; step < 8; ++step)
        physics->update(*workspace, 1.0f / 60.0f);

    struct BuoyancyComparison {
        const char* shape;
        std::shared_ptr<BaseCube> wet;
        std::shared_ptr<BaseCube> dry;
    };
    for (const BuoyancyComparison& comparison : {
             BuoyancyComparison{"Box", waterBox, dryBox},
             BuoyancyComparison{
                 "Sphere",
                 std::static_pointer_cast<BaseCube>(waterSphere),
                 std::static_pointer_cast<BaseCube>(drySphere)},
             BuoyancyComparison{
                 "TriangularPrism",
                 std::static_pointer_cast<BaseCube>(waterPrism),
                 std::static_pointer_cast<BaseCube>(dryPrism)}}) {
        const float wetVelocity =
            physics->getLinearVelocity(*comparison.wet).y;
        const float dryVelocity =
            physics->getLinearVelocity(*comparison.dry).y;
        std::cout << "[Box3DBuoyancyRegression] metric=buoyancy"
                  << " shape=" << comparison.shape
                  << " wet_vy=" << wetVelocity
                  << " dry_vy=" << dryVelocity
                  << " delta=" << wetVelocity - dryVelocity << "\n";
        expect(std::isfinite(wetVelocity) &&
                   std::isfinite(dryVelocity) &&
                   wetVelocity >= dryVelocity + 5.0f,
               comparison.shape);
    }

    const float zeroDensityVelocity =
        physics->getLinearVelocity(*zeroDensityBody).y;
    const float zeroDensityControlVelocity =
        physics->getLinearVelocity(*zeroDensityControl).y;
    const float positiveDensityBoxVelocity =
        physics->getLinearVelocity(*waterBox).y;
    std::cout << "[Box3DBuoyancyRegression] metric=zero_density"
              << " wet_vy=" << zeroDensityVelocity
              << " dry_vy=" << zeroDensityControlVelocity
              << " dry_difference_informational="
              << std::abs(zeroDensityVelocity - zeroDensityControlVelocity)
              << " density2_box_vy=" << positiveDensityBoxVelocity
              << " density_delta="
              << positiveDensityBoxVelocity - zeroDensityVelocity << "\n";
    expect(std::isfinite(zeroDensityVelocity) &&
               zeroDensityVelocity < 0.0f &&
               positiveDensityBoxVelocity >= zeroDensityVelocity + 5.0f,
           "Density=0 has no upward buoyancy while submerged damping remains active");

    const float neutralSphereVelocity =
        physics->getLinearVelocity(*neutralSphere).y;
    std::cout << "[Box3DBuoyancyRegression] metric=neutral_sphere"
              << " density=1"
              << " vy=" << neutralSphereVelocity
              << " tolerance=2\n";
    expect(std::isfinite(neutralSphereVelocity) &&
               std::abs(neutralSphereVelocity) <= 2.0f,
           "fully submerged Density=1 Sphere is neutrally buoyant within 2 studs/s");

    const Vector3 maintainedVelocity =
        physics->getLinearVelocity(*maintainVelocityCube);
    const Vector3 expectedMaintainedVelocity;
    const Vector3 maintainedVelocityError =
        maintainedVelocity - expectedMaintainedVelocity;
    std::cout << "[Box3DBuoyancyRegression] metric=maintain_velocity_buoyancy"
              << " mass_density=" << maintainVelocityCube->MassDensity
              << " liquid_density=" << maintainVelocityLiquid->Density
              << " actual_velocity=[" << maintainedVelocity.x << ','
              << maintainedVelocity.y << ',' << maintainedVelocity.z << ']'
              << " expected_velocity=[" << expectedMaintainedVelocity.x << ','
              << expectedMaintainedVelocity.y << ','
              << expectedMaintainedVelocity.z << ']'
              << " error=[" << maintainedVelocityError.x << ','
              << maintainedVelocityError.y << ','
              << maintainedVelocityError.z << ']'
              << " tolerance=0.05\n";
    expect(finiteVector(maintainedVelocity) &&
               std::abs(maintainedVelocityError.x) <= 0.05f &&
               std::abs(maintainedVelocityError.y) <= 0.05f &&
               std::abs(maintainedVelocityError.z) <= 0.05f,
           "MaintainVelocity zero overrides gravity and accumulated buoyancy");

    const float obbOutsideVelocity =
        physics->getLinearVelocity(*obbOutsideBody).y;
    const float obbOutsideControlVelocity =
        physics->getLinearVelocity(*obbOutsideControl).y;
    std::cout << "[Box3DBuoyancyRegression] metric=rotated_obb_control"
              << " outside_vy=" << obbOutsideVelocity
              << " control_vy=" << obbOutsideControlVelocity
              << " difference="
              << std::abs(obbOutsideVelocity - obbOutsideControlVelocity)
              << "\n";
    expect(std::abs(
               obbOutsideVelocity - obbOutsideControlVelocity) <= 1.0f,
           "rotated LiquidCube does not buoy old-AABB-only control body");

    workspace->removeChild(primaryLiquid->Name);
    physics->update(*workspace, 0.0f);
    physics->setLinearVelocity(*waterBox, Vector3());
    physics->setLinearVelocity(*dryBox, Vector3());
    for (int step = 0; step < 6; ++step)
        physics->update(*workspace, 1.0f / 60.0f);
    const float removedLiquidVelocity =
        physics->getLinearVelocity(*waterBox).y;
    const float removedLiquidControlVelocity =
        physics->getLinearVelocity(*dryBox).y;
    std::cout << "[Box3DBuoyancyRegression] metric=liquid_removal"
              << " former_wet_vy=" << removedLiquidVelocity
              << " control_vy=" << removedLiquidControlVelocity
              << " difference="
              << std::abs(
                     removedLiquidVelocity - removedLiquidControlVelocity)
              << "\n";
    expect(removedLiquidVelocity < -10.0f &&
               std::abs(
                   removedLiquidVelocity - removedLiquidControlVelocity) <= 1.0f,
           "Liquid removal clears damping and restores gravity acceleration");

    const CFrame weldFinalRelative =
        weldFirst->getWorldCFrame().inverse() * weldSecond->getWorldCFrame();
    const float buoyantWeldError = positionDistance(
        weldInitialRelative.Position, weldFinalRelative.Position);
    const bool buoyantWeldFinite =
        finiteCFrame(weldFirst->getWorldCFrame()) &&
        finiteCFrame(weldSecond->getWorldCFrame()) &&
        finiteVector(physics->getLinearVelocity(*weldFirst)) &&
        finiteVector(physics->getLinearVelocity(*weldSecond));
    std::cout << "[Box3DBuoyancyRegression] metric=buoyant_weld"
              << " relative_position_error=" << buoyantWeldError
              << " finite=" << (buoyantWeldFinite ? "true" : "false")
              << "\n";
    expect(buoyantWeldFinite &&
               buoyantWeldError <= 0.001f &&
               sameCFrame(weldInitialRelative, weldFinalRelative),
           "two-member buoyant Weld remains finite with <=0.001 stud error");

    auto performanceWorkspace = std::make_shared<Workspace>();
    performanceWorkspace->initPhysics();
    Physics* performancePhysics =
        performanceWorkspace->getPhysicsEngine();
    auto performanceLiquid = std::make_shared<LiquidCube>(
        Vector3(0, 20, 0), Vector3(100, 40, 100));
    performanceLiquid->Name = "PerformanceLiquid";
    performanceLiquid->Anchored = true;
    performanceLiquid->Density = 1.0f;
    performanceWorkspace->addChild(performanceLiquid);
    for (int index = 0; index < 128; ++index) {
        auto sphere = std::make_shared<Sphere>(
            Vector3(
                -45.0f + static_cast<float>(index % 16) * 6.0f,
                20.0f,
                -21.0f + static_cast<float>(index / 16) * 6.0f),
            Vector3(1, 1, 1));
        sphere->Name = "BuoyancyPerformanceSphere" + std::to_string(index);
        performanceWorkspace->addChild(sphere);
    }
    performancePhysics->update(*performanceWorkspace, 0.0f);
    std::vector<double> buoyancyStepMilliseconds;
    buoyancyStepMilliseconds.reserve(60);
    for (int step = 0; step < 60; ++step) {
        const auto start = std::chrono::steady_clock::now();
        performancePhysics->update(
            *performanceWorkspace, 1.0f / 60.0f);
        const auto end = std::chrono::steady_clock::now();
        buoyancyStepMilliseconds.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::vector<double> sortedBuoyancySteps = buoyancyStepMilliseconds;
    std::sort(sortedBuoyancySteps.begin(), sortedBuoyancySteps.end());
    const double buoyancyMedian =
        sortedBuoyancySteps[sortedBuoyancySteps.size() / 2];
    double buoyancyAverage = 0.0;
    for (double milliseconds : buoyancyStepMilliseconds)
        buoyancyAverage += milliseconds;
    buoyancyAverage /= buoyancyStepMilliseconds.size();
    std::cout << "BOX3D_BUOYANCY_PERF"
              << " spheres=128 steps=60"
              << " median_ms=" << buoyancyMedian
              << " average_ms=" << buoyancyAverage
              << " guard_ms=4\n";
    expect(buoyancyMedian <= 4.0, "128 Sphere buoyancy median stays within 4ms");

    std::cout << "[Box3DBuoyancyRegression] failures=" << failures
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

    AudioService spatialAudio;
    auto spatialRoot = std::make_shared<Model>(Vector3(10, 0, 0));
    spatialRoot->Rotation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f);
    auto spatialParent = std::make_shared<Model>(Vector3(0, 2, 0));
    spatialParent->Rotation = Quaternion::fromAxisAngle(Vector3(0, 0, 1), 90.0f);
    auto spatialSound = std::make_shared<Sound>(spatialAudio);
    spatialSound->Position = Vector3(3, 0, 0);
    spatialRoot->addChild(spatialParent);
    spatialParent->addChild(spatialSound);
    const Vector3 worldPosition = spatialSound->getWorldCFrame().Position;
    const Vector3 oldPosition = spatialParent->Position + spatialSound->Position;
    const Vector3 listenerPosition(0, 0, 0);
    const Vector3 listenerRight(1, 0, 0);
    const SoundSpatialMix spatialMix = Sound::calculateSpatialMix(
        worldPosition, listenerPosition, listenerRight, 0.8f);
    const float distance = worldPosition.length();
    const float expectedVolume = 0.8f / (1.0f + distance * 0.1f);
    const float expectedPan = Vector3::Dot(worldPosition.normalize(), listenerRight);
    expect(positionDistance(worldPosition, oldPosition) > 1e-4f &&
               std::abs(spatialMix.volume - expectedVolume) < 1e-5f &&
               std::abs(spatialMix.pan - expectedPan) < 1e-5f,
           "Sound uses rotated multi-level Spatial world position for volume and pan");

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
int runPhysicsLifecycleRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[PhysicsLifecycle] " << (condition ? "PASS: " : "FAIL: ")
                  << message << '\n';
        if (!condition) ++failures;
    };

    auto workspaceA = std::make_shared<Workspace>();
    auto workspaceB = std::make_shared<Workspace>();
    workspaceA->Name = "WorkspaceA";
    workspaceB->Name = "WorkspaceB";
    workspaceA->Gravity = {};
    workspaceB->Gravity = {};
    workspaceA->initPhysics();
    workspaceB->initPhysics();
    auto* physicsA = workspaceA->getPhysicsEngine();
    auto* physicsB = workspaceB->getPhysicsEngine();

    auto folder = std::make_shared<Model>();
    folder->Name = "Folder";
    workspaceA->addChild(folder);

    auto cube = std::make_shared<BaseCube>(Vector3(0, 0, 0), Vector3(2, 2, 2));
    cube->Name = "MovingCube";
    cube->Anchored = true;
    workspaceA->addChild(cube);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    expect(physicsA->hasBody(*cube), "Cube is created in Workspace A");
    const CFrame beforeReparent = physicsA->getBodyWorldCFrame(*cube);

    cube->setParent(folder);
    expect(physicsA->hasBody(*cube),
           "same-Workspace reparent keeps the existing body");
    expect(sameCFrame(beforeReparent, physicsA->getBodyWorldCFrame(*cube)),
           "same-Workspace reparent preserves the body pose");
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    expect(physicsA->hasBody(*cube),
           "same-Workspace reparent does not lose the body after flush");

    cube->setParent(workspaceB);
    expect(!physicsA->hasBody(*cube),
           "direct A-to-B move invalidates the old owner immediately");
    physicsB->update(*workspaceB, 1.0f / 60.0f);
    expect(physicsB->hasBody(*cube), "direct A-to-B move creates a body in B");
    RaycastHit hitA;
    RaycastHit hitB;
    expect(!physicsA->raycast(Vector3(0, 10, 0), Vector3(0, -1, 0), 20, hitA),
           "old world has no ghost shape after direct move");
    expect(physicsB->raycast(Vector3(0, 10, 0), Vector3(0, -1, 0), 20, hitB) &&
               hitB.instance == cube.get(),
           "new world raycast resolves the moved Cube");

    auto survivor = std::make_shared<BaseCube>(Vector3(20, 0, 0), Vector3(2, 2, 2));
    auto departing = std::make_shared<BaseCube>(Vector3(26, 0, 0), Vector3(2, 2, 2));
    survivor->Name = "WeldSurvivor";
    departing->Name = "WeldDeparting";
    survivor->Anchored = true;
    departing->Anchored = true;
    workspaceA->addChild(survivor);
    workspaceA->addChild(departing);
    auto weld = std::make_shared<Weld>(survivor, departing);
    weld->Name = "Weld";
    workspaceA->addChild(weld);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    expect(physicsA->sharesBody(*survivor, *departing),
           "Weld members initially share one body");

    departing->setParent(workspaceB);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    physicsB->update(*workspaceB, 1.0f / 60.0f);
    expect(physicsA->hasBody(*survivor),
           "remaining Weld component is rebuilt in the old world");
    expect(physicsB->hasBody(*departing),
           "departing Weld member receives a body in the new world");
    RaycastHit oldWeldHit;
    expect(!physicsA->raycast(Vector3(26, 10, 0), Vector3(0, -1, 0), 20,
                              oldWeldHit) ||
               oldWeldHit.instance != departing.get(),
           "old Weld compound contains no departing member ghost shape");

    std::cout << "[PhysicsLifecycle] "
              << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runFixedStepForceRegression() {
    auto backendWorkspace = std::make_shared<Workspace>();
    backendWorkspace->initPhysics();
    const char* backend = physicsBackendName(
        backendWorkspace->getPhysicsEngine()->getBackendType());
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[FixedStepForce] backend=" << backend << ' '
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    struct Result {
        Vector3 position;
        Vector3 velocity;
        Quaternion rotation;
    };
    auto simulate = [](float frameDt) {
        auto workspace = std::make_shared<Workspace>();
        workspace->Gravity = {};
        workspace->initPhysics();
        auto body = addMigrationCube(
            workspace, "FixedStepBody", {0, 0, 0}, {2, 2, 2});
        auto linear = std::make_shared<Force>();
        linear->Name = "Linear";
        linear->Value = {80, 0, 0};
        body->addChild(linear);
        auto torque = std::make_shared<Force>();
        torque->Name = "Torque";
        torque->Torque = true;
        torque->Value = {0, 0, 2};
        body->addChild(torque);

        Physics* physics = workspace->getPhysicsEngine();
        const int frames = static_cast<int>(std::lround(1.0f / frameDt));
        for (int frame = 0; frame < frames; ++frame)
            physics->update(*workspace, frameDt);
        return Result{body->getWorldPosition(), physics->getLinearVelocity(*body),
                      body->getWorldCFrame().Rotation};
    };

    const Result at30 = simulate(1.0f / 30.0f);
    const Result at60 = simulate(1.0f / 60.0f);
    const Result at120 = simulate(1.0f / 120.0f);
    auto closeVector = [](const Vector3& first, const Vector3& second,
                          float tolerance) {
        return positionDistance(first, second) <= tolerance;
    };
    auto rotationDot = [](const Quaternion& first, const Quaternion& second) {
        return std::abs(first.w * second.w + first.x * second.x +
                        first.y * second.y + first.z * second.z);
    };
    std::cout << "[FixedStepForce] backend=" << backend
              << " metric=dt_invariance"
              << " p30=" << at30.position.x << " p60=" << at60.position.x
              << " p120=" << at120.position.x
              << " v30=" << at30.velocity.x << " v60=" << at60.velocity.x
              << " v120=" << at120.velocity.x << '\n';
    expect(closeVector(at30.position, at60.position, 0.05f) &&
               closeVector(at120.position, at60.position, 0.05f) &&
               closeVector(at30.velocity, at60.velocity, 0.05f) &&
               closeVector(at120.velocity, at60.velocity, 0.05f),
           "Force integration is invariant at render dt 1/30, 1/60, and 1/120");
    expect(rotationDot(at30.rotation, at60.rotation) >= 0.999f &&
               rotationDot(at120.rotation, at60.rotation) >= 0.999f,
           "Torque integration is invariant at render dt 1/30, 1/60, and 1/120");

    auto workspace = std::make_shared<Workspace>();
    workspace->initPhysics();
    auto liquid = std::make_shared<LiquidCube>(
        Vector3(0, 0, 0), Vector3(20, 20, 20));
    liquid->Anchored = true;
    liquid->Density = 4.0f;
    workspace->addChild(liquid);
    auto maintained = addMigrationCube(
        workspace, "Maintained", {0, 0, 0}, {2, 2, 2});
    auto additive = std::make_shared<Force>();
    additive->Name = "Additive";
    additive->Value = {10000, 10000, 10000};
    maintained->addChild(additive);
    auto target = std::make_shared<Force>();
    target->Name = "Maintain";
    target->MaintainVelocity = true;
    target->Value = {3, 4, 5};
    maintained->addChild(target);
    Physics* physics = workspace->getPhysicsEngine();
    for (int step = 0; step < 60; ++step)
        physics->update(*workspace, 1.0f / 60.0f);
    const Vector3 actual = physics->getLinearVelocity(*maintained);
    std::cout << "[FixedStepForce] backend=" << backend
              << " metric=maintain_priority actual=[" << actual.x << ','
              << actual.y << ',' << actual.z << "]\n";
    expect(closeVector(actual, target->Value, 0.05f),
           "MaintainVelocity overrides gravity, buoyancy, and additive Force");

    std::cout << "[FixedStepForce] backend=" << backend << " failures="
              << failures << " result=" << (failures == 0 ? "PASS" : "FAIL")
              << '\n';
    return failures == 0 ? 0 : 1;
}

int runContactReentryRegression() {
    auto workspaceA = std::make_shared<Workspace>();
    auto workspaceB = std::make_shared<Workspace>();
    workspaceA->Name = "ContactWorkspaceA";
    workspaceB->Name = "ContactWorkspaceB";
    workspaceA->initPhysics();
    workspaceB->initPhysics();
    Physics* physicsA = workspaceA->getPhysicsEngine();
    Physics* physicsB = workspaceB->getPhysicsEngine();
    const char* backend = physicsBackendName(physicsA->getBackendType());
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[ContactReentry] backend=" << backend << ' '
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    auto floor = addMigrationCube(
        workspaceA, "ContactFloor", {0, 0, 0}, {20, 1, 20}, true);
    auto falling = addMigrationCube(
        workspaceA, "ContactFalling", {0, 5, 0}, {2, 2, 2});
    int callbacks = 0;
    bool poseWasSynchronized = false;
    Physics::s_contactCallback = [&](BaseCube* first, BaseCube* second) {
        const bool isPair =
            (first == floor.get() && second == falling.get()) ||
            (first == falling.get() && second == floor.get());
        if (!isPair) return;
        ++callbacks;
        poseWasSynchronized = positionDistance(
            falling->getWorldPosition(),
            physicsA->getBodyWorldCFrame(*falling).Position) <= 0.01f;
        if (callbacks == 1) {
            // callback中のreparentとshape再構築要求は、native step完了後の
            // 安全窓で処理されなければならない。
            falling->setParent(workspaceB);
            floor->setSize({22, 1, 22});
        }
    };
    for (int step = 0; step < 180 && callbacks == 0; ++step)
        physicsA->update(*workspaceA, 1.0f / 60.0f);
    Physics::s_contactCallback = {};
    expect(callbacks == 1, "contact is dispatched once without callback re-entry");
    expect(poseWasSynchronized,
           "contact dispatch occurs after native pose synchronization");
    expect(!physicsA->hasBody(*falling),
           "callback reparent immediately detaches the old-world body");
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    physicsB->update(*workspaceB, 1.0f / 60.0f);
    expect(physicsB->hasBody(*falling),
           "callback reparent registers the body in the destination world");
    expect(physicsA->hasBody(*floor),
           "callback resize is applied in the next safe update window");

    auto lower = addMigrationCube(
        workspaceA, "CompoundLower", {40, 1, 0}, {2, 2, 2}, true);
    auto upper = addMigrationCube(
        workspaceA, "CompoundUpper", {40, 4, 0}, {2, 2, 2}, true);
    auto weld = std::make_shared<Weld>(lower, upper);
    weld->Name = "CompoundRaycastWeld";
    workspaceA->addChild(weld);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    RaycastHit upperHit;
    RaycastHit lowerHit;
    const bool hitUpper = physicsA->raycast(
        {40, 10, 0}, {0, -1, 0}, 20, upperHit);
    const bool hitLowerWhenIgnoringUpper = physicsA->raycast(
        {40, 10, 0}, {0, -1, 0}, 20, lowerHit, upper.get());
    expect(hitUpper && upperHit.instance == upper.get(),
           "compound raycast reports the member shape that was hit");
    expect(hitLowerWhenIgnoringUpper && lowerHit.instance == lower.get(),
           "ignoreCube skips only its member shape, not the whole compound");

    std::cout << "[ContactReentry] backend=" << backend << " failures="
              << failures << " result=" << (failures == 0 ? "PASS" : "FAIL")
              << '\n';
    return failures == 0 ? 0 : 1;
}

int runNetworkCoreRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[NetworkCore] " << (condition ? "PASS: " : "FAIL: ")
                  << message << '\n' << std::flush;
        if (!condition) ++failures;
    };

    auto& network = NetworkManager::get();
    network.shutdown();
    const bool dedicatedStarted = network.startHost(0, false);
    const auto& dedicatedRoster = network.getRoster();
    expect(dedicatedStarted && network.getRole() == NetworkRole::Host &&
               network.getLocalPeerId() == 1 && !network.isLocalPlayer(),
           "dedicated host reserves PeerId 1 and is not a player");
    expect(dedicatedRoster.size() == 1 && dedicatedRoster.front().isHost &&
               !dedicatedRoster.front().isPlayer,
           "dedicated host roster entry preserves the non-player flag");

    auto connectRawClient = [&](ENetHost*& client, ENetPeer*& peer) {
        client = enet_host_create(
            nullptr, 1, static_cast<size_t>(NetworkChannel::Count), 0, 0);
        if (!client) return false;
        ENetAddress address{};
        if (enet_address_set_host(&address, "127.0.0.1") != 0) return false;
        address.port = network.getListenPort();
        peer = enet_host_connect(
            client, &address, static_cast<size_t>(NetworkChannel::Count), 0);
        if (!peer) return false;

        for (int attempt = 0; attempt < 300; ++attempt) {
            network.update(0.01f);
            ENetEvent event{};
            while (enet_host_service(client, &event, 1) > 0) {
                if (event.type == ENET_EVENT_TYPE_CONNECT) return true;
                if (event.type == ENET_EVENT_TYPE_RECEIVE)
                    enet_packet_destroy(event.packet);
                if (event.type == ENET_EVENT_TYPE_DISCONNECT) return false;
            }
        }
        return false;
    };
    auto makeHello = [](uint8_t version) {
        ByteWriter writer;
        writer.writeU8(static_cast<uint8_t>(MessageType::Hello));
        writer.writeU8(version);
        writer.writeU32(0);
        const AdmissionToken token{};
        writer.data.insert(writer.data.end(), token.begin(), token.end());
        writer.writeU16(0);
        writer.writeU32(0);
        std::vector<uint8_t> candidates;
        NatProtocol::encodeCandidates({}, candidates);
        writer.data.insert(writer.data.end(), candidates.begin(), candidates.end());
        return writer.data;
    };
    auto sendRawHello = [](ENetHost* client, ENetPeer* peer,
                           const std::vector<uint8_t>& hello) {
        ENetPacket* packet = enet_packet_create(
            hello.data(), hello.size(), ENET_PACKET_FLAG_RELIABLE);
        if (!packet) return false;
        if (enet_peer_send(peer,
                           static_cast<enet_uint8>(NetworkChannel::Reliable),
                           packet) != 0) {
            enet_packet_destroy(packet);
            return false;
        }
        enet_host_flush(client);
        return true;
    };
    auto destroyRawClient = [](ENetHost*& client, ENetPeer*& peer) {
        if (peer) enet_peer_reset(peer);
        if (client) enet_host_destroy(client);
        peer = nullptr;
        client = nullptr;
    };

    ENetHost* rawClient = nullptr;
    ENetPeer* rawPeer = nullptr;
    const bool oldClientConnected = connectRawClient(rawClient, rawPeer);
    bool oldClientWelcomed = false;
    bool oldClientDisconnected = false;
    if (oldClientConnected && sendRawHello(rawClient, rawPeer, makeHello(1))) {
        for (int attempt = 0; attempt < 300 && !oldClientDisconnected; ++attempt) {
            network.update(0.01f);
            ENetEvent event{};
            while (enet_host_service(rawClient, &event, 1) > 0) {
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    oldClientWelcomed = oldClientWelcomed ||
                        (event.packet->dataLength > 0 &&
                         event.packet->data[0] ==
                             static_cast<uint8_t>(MessageType::Welcome));
                    enet_packet_destroy(event.packet);
                } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                    oldClientDisconnected = true;
                }
            }
        }
    }
    expect(oldClientConnected && oldClientDisconnected && !oldClientWelcomed &&
               network.getRoster().size() == 1 && !network.hasPeers(),
           "direct host rejects an old game protocol version before admission");
    destroyRawClient(rawClient, rawPeer);

    const bool validClientConnected = connectRawClient(rawClient, rawPeer);
    bool sawWelcomePeerTwo = false;
    bool decodedRoster = false;
    bool rosterHostIsNonPlayer = false;
    bool rosterClientIsPlayer = false;
    if (validClientConnected && sendRawHello(rawClient, rawPeer, makeHello(2))) {
        for (int attempt = 0; attempt < 300 && !decodedRoster; ++attempt) {
            network.update(0.01f);
            ENetEvent event{};
            while (enet_host_service(rawClient, &event, 1) > 0) {
                if (event.type != ENET_EVENT_TYPE_RECEIVE) continue;
                if (event.packet->dataLength == 0) {
                    enet_packet_destroy(event.packet);
                    continue;
                }
                ByteReader reader{event.packet->data + 1,
                                  event.packet->dataLength - 1};
                const auto type =
                    static_cast<MessageType>(event.packet->data[0]);
                if (type == MessageType::Welcome) {
                    uint32_t assignedId = 0;
                    sawWelcomePeerTwo = reader.readU32(assignedId) && assignedId == 2;
                } else if (type == MessageType::Roster) {
                    uint32_t count = 0;
                    bool valid = reader.readU32(count) && count == 2;
                    for (uint32_t index = 0; index < count && valid; ++index) {
                        uint32_t id = 0, endpointHost = 0;
                        uint16_t listenPort = 0;
                        float cpuScore = 0.0f, latencyMs = 0.0f;
                        uint8_t isHost = 0, isPlayer = 0;
                        valid = reader.readU32(id) && reader.readU32(endpointHost) &&
                            reader.readU16(listenPort);
                        if (valid && reader.remaining >= 1) {
                            const size_t candidateBytes =
                                1 + static_cast<size_t>(reader.p[0]) * 7;
                            valid = candidateBytes <= reader.remaining;
                            if (valid) {
                                reader.p += candidateBytes;
                                reader.remaining -= candidateBytes;
                            }
                        } else {
                            valid = false;
                        }
                        if (valid && reader.remaining >= AdmissionToken{}.size()) {
                            reader.p += AdmissionToken{}.size();
                            reader.remaining -= AdmissionToken{}.size();
                        } else {
                            valid = false;
                        }
                        valid = valid && reader.readF32(cpuScore) &&
                            reader.readF32(latencyMs) && reader.readU8(isHost) &&
                            reader.readU8(isPlayer);
                        if (id == 1)
                            rosterHostIsNonPlayer = isHost != 0 && isPlayer == 0;
                        if (id == 2)
                            rosterClientIsPlayer = isHost == 0 && isPlayer != 0;
                    }
                    uint32_t nextPeerId = 0;
                    decodedRoster = valid && reader.readU32(nextPeerId) &&
                        nextPeerId == 3 && reader.remaining == 0;
                }
                enet_packet_destroy(event.packet);
            }
        }
    }
    expect(validClientConnected && sawWelcomePeerTwo && decodedRoster &&
               rosterHostIsNonPlayer && rosterClientIsPlayer,
           "roster round-trip preserves dedicated and player participation flags");
    {
        auto replicationSystem = std::make_shared<System>();
        auto replicationWorkspace = std::make_shared<Workspace>();
        auto users = std::make_shared<Users>();
        replicationSystem->addChild(replicationWorkspace);
        replicationSystem->addChild(users);
        ReplicationManager replication(
            replicationWorkspace, nullptr, replicationSystem.get());
        replication.update(0.01f, nullptr);
        expect(!users->children.contains("User_1") &&
                   !replicationWorkspace->children.contains("PlayerCharacter_1") &&
                   users->children.contains("User_2") &&
                   replicationWorkspace->children.contains("PlayerCharacter_2"),
               "replication excludes the dedicated peer and creates the player identity");
    }
    if (rawPeer) {
        enet_peer_disconnect(rawPeer, 0);
        enet_host_flush(rawClient);
        bool disconnected = false;
        for (int attempt = 0; attempt < 300 && !disconnected; ++attempt) {
            network.update(0.01f);
            ENetEvent event{};
            while (enet_host_service(rawClient, &event, 1) > 0) {
                if (event.type == ENET_EVENT_TYPE_RECEIVE)
                    enet_packet_destroy(event.packet);
                if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                    disconnected = true;
            }
        }
    }
    destroyRawClient(rawClient, rawPeer);

    {
        LuauEngine engine;
        auto system = std::make_shared<System>();
        auto workspace = std::make_shared<Workspace>();
        system->UseNetwork = true;
        system->addChild(workspace);

        auto serverScript = std::make_shared<Script>();
        serverScript->Name = "DedicatedServerScript";
        serverScript->Source = "local serverOnly = true";
        auto localScript = std::make_shared<LocalScript>();
        localScript->Name = "DedicatedLocalScript";
        localScript->Source = "local clientOnly = true";
        workspace->addChild(serverScript);
        workspace->addChild(localScript);

        engine.setWorkspace(workspace);
        engine.setSystem(system.get());
        engine.executeWorkspaceScripts(*workspace);
        expect(serverScript->Completed && !localScript->Completed &&
                   localScript->Coroutine == nullptr,
               "dedicated host executes Script and skips LocalScript");
    }
    network.shutdown();
    expect(network.getRole() == NetworkRole::Offline && !network.isActive(),
           "dedicated host shuts down cleanly after direct clients leave");

    {
        LuauEngine engine;
        auto system = std::make_shared<System>();
        auto workspace = std::make_shared<Workspace>();
        system->addChild(workspace);
        auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
        system->addChild(user);
        user->initializeInventory();

        engine.setWorkspace(workspace);
        engine.setSystem(system.get());
        bool eventSawInitialPosition = false;
        const auto oldLogHook = g_luauLogHook;
        g_luauLogHook = [&](const std::string& message) {
            if (message.find("[NetworkCoreSpawnEvent]") != std::string::npos)
                eventSawInitialPosition = true;
        };
        engine.setGlobalInstance("User", user);
        auto listener = std::make_shared<Script>();
        listener->Name = "InitialSpawnListener";
        listener->Source =
            "User.CharacterAdded:Connect(function(character) "
            "if character.Position.x == 12 and character.Position.y == 34 and "
            "character.Position.z == -56 then print('[NetworkCoreSpawnEvent]') end end)";
        workspace->addChild(listener);
        expect(engine.execute(*listener),
               "CharacterAdded position listener starts successfully");

        const Vector3 initialPosition(12.0f, 34.0f, -56.0f);
        user->spawnCharacter(system.get(), workspace.get(), initialPosition);
        expect(user->character &&
                   positionDistance(user->character->getWorldPosition(), initialPosition) < 1e-5f,
               "spawnCharacter applies the requested initial model position");
        expect(eventSawInitialPosition,
               "CharacterAdded observes the requested position before firing");
        g_luauLogHook = oldLogHook;
    }

    std::cout << "[NetworkCore] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runMultiWorkspaceRegression() {
    auto workspaceA = std::make_shared<Workspace>();
    auto workspaceB = std::make_shared<Workspace>();
    workspaceA->Name = "NetworkWorkspaceA";
    workspaceB->Name = "NetworkWorkspaceB";
    workspaceA->initPhysics();
    workspaceB->initPhysics();
    Physics* physicsA = workspaceA->getPhysicsEngine();
    Physics* physicsB = workspaceB->getPhysicsEngine();
    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
    ReplicationManager replication(workspaceA, user, nullptr);
    const char* backend = physicsBackendName(physicsA->getBackendType());
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[MultiWorkspace] backend=" << backend << ' '
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    const std::uint64_t firstGeneration =
        replication.getWorkspaceGeneration();
    replication.setWorkspace(workspaceB);
    expect(replication.getWorkspace() == workspaceB &&
               replication.getBoundPhysics() == physicsB,
           "Workspace and Physics binding switch atomically");
    expect(replication.getWorkspaceGeneration() == firstGeneration + 1,
           "Workspace switch advances the packet binding generation");

    replication.update(1.0f / 60.0f, physicsA);
    expect(replication.getWorkspace() == workspaceB &&
               replication.getBoundPhysics() == physicsB,
           "update rejects a Physics pointer from the previous Workspace");
    replication.update(1.0f / 60.0f, physicsB);
    expect(replication.getBoundPhysics() == physicsB,
           "update accepts the currently bound Workspace Physics");

    const std::uint64_t secondGeneration =
        replication.getWorkspaceGeneration();
    replication.setWorkspace(nullptr);
    expect(!replication.getWorkspace() && !replication.getBoundPhysics() &&
               replication.getWorkspaceGeneration() == secondGeneration + 1,
           "unbinding clears Workspace and Physics in one generation");

    std::cout << "[MultiWorkspace] backend=" << backend << " failures="
              << failures << " result=" << (failures == 0 ? "PASS" : "FAIL")
              << '\n';
    return failures == 0 ? 0 : 1;
}

int runPhysicsRollbackRegression() {
    auto workspace = std::make_shared<Workspace>();
    workspace->Gravity = {};
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    const char* backend = physicsBackendName(physics->getBackendType());
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[PhysicsRollback] backend=" << backend << ' '
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    auto cube = addMigrationCube(
        workspace, "RollbackCube", {0, 0, 0}, {2, 2, 2}, true);
    physics->update(*workspace, 1.0f / 60.0f);
    const PhysicsBodyHandle originalHandle = physics->getBodyHandle(*cube);
    cube->setSize(cube->Size);
    cube->setAnchored(cube->Anchored);
    cube->setCanCollide(cube->CanCollide);
    cube->setMaterial(cube->material);
    cube->setMassDensity(cube->MassDensity);
    expect(physics->getBodyHandle(*cube) == originalHandle,
           "same-value setters preserve the native body handle");

    const Vector3 originalSize = cube->Size;
    cube->setSize({std::numeric_limits<float>::quiet_NaN(), 2, 2});
    expect(cube->Size == originalSize &&
               physics->getBodyHandle(*cube) == originalHandle,
           "invalid Size setter leaves logical and native state unchanged");
    cube->Size = {std::numeric_limits<float>::quiet_NaN(), 2, 2};
    physics->recreateActor(cube);
    RaycastHit retainedHit;
    const bool retainedCollision = physics->raycast(
        {0, 10, 0}, {0, -1, 0}, 20, retainedHit);
    expect(physics->getBodyHandle(*cube) == originalHandle &&
               retainedCollision && retainedHit.instance == cube.get(),
           "invalid replacement descriptor retains the old collision body");
    cube->Size = originalSize;

    PhysicsTerrainDescriptor terrainDescriptor = makeMigrationTerrain(nullptr);
    const PhysicsTerrainHandle terrain =
        physics->createTerrain(terrainDescriptor);
    PhysicsTerrainDescriptor invalidTerrain = terrainDescriptor;
    invalidTerrain.indices = {0, 1, 99};
    const PhysicsTerrainHandle afterInvalid =
        physics->replaceTerrain(terrain, invalidTerrain);
    RaycastHit terrainHit;
    expect(terrain && afterInvalid == terrain && physics->raycast(
               {0, 10, 0}, {0, -1, 0}, 20, terrainHit),
           "invalid Terrain replacement retains the old handle and collision");
    physics->destroyTerrain(terrain);
    expect(!physics->createTerrain({}),
           "empty Terrain descriptor does not create a native handle");

    workspace->PhysicsEnabled = false;
    auto pending = addMigrationCube(
        workspace, "RemovedWhileDisabled", {50, 0, 0}, {2, 2, 2});
    pending->setParent(nullptr);
    workspace->PhysicsEnabled = true;
    physics->update(*workspace, 1.0f / 60.0f);
    expect(!physics->hasBody(*pending),
           "removed pending Cube is not resurrected when physics resumes");

    auto saveRoot = std::make_shared<Instance>("SaveRoot");
    auto locked = std::make_shared<Cube>(Vector3(), Vector3(1, 1, 1), 0);
    locked->Name = "LockedCube";
    locked->LockFlags = PhysicsLockFlags::LinearX |
        PhysicsLockFlags::AngularY | PhysicsLockFlags::AngularZ;
    locked->CollisionDetection = CCDMode::Bullet;
    locked->Locked = true;
    auto lockedClone = std::dynamic_pointer_cast<BaseCube>(locked->clone());
    expect(lockedClone && lockedClone->LockFlags == locked->LockFlags &&
               lockedClone->CollisionDetection == CCDMode::Bullet &&
               lockedClone->Locked,
           "clone preserves LockFlags, CCDMode, and Locked without native handles");
    saveRoot->addChild(locked);
    const auto savePath = std::filesystem::temp_directory_path() /
        ("recubin_lockflags_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".yaml");
    SceneLoader::saveScene(saveRoot.get(), savePath.string());
    auto loadedRoot = SceneLoader::loadScene(savePath.string());
    std::error_code removeError;
    std::filesystem::remove(savePath, removeError);
    Instance* loadedChild = loadedRoot
        ? loadedRoot->getChild("LockedCube") : nullptr;
    auto loaded = loadedChild
        ? std::dynamic_pointer_cast<BaseCube>(loadedChild->shared_from_this())
        : nullptr;
    expect(loaded && loaded->LockFlags == locked->LockFlags,
           "LockFlags string sequence survives YAML save/load");

    std::cout << "[PhysicsRollback] backend=" << backend << " failures="
              << failures << " result=" << (failures == 0 ? "PASS" : "FAIL")
              << '\n';
    return failures == 0 ? 0 : 1;
}

int runBox3DHullRegression() {
    auto workspace = std::make_shared<Workspace>();
    workspace->Gravity = {};
    workspace->initPhysics();
    Physics* physics = workspace->getPhysicsEngine();
    if (physics->getBackendType() != PhysicsBackendType::Box3D) {
        std::cout << "[Box3DHullRegression] SKIP: run with --physics=box3d.\n";
        return 0;
    }

    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[Box3DHullRegression] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    const std::vector<Vector3> denseVertices = makeDenseHullVertices();
    auto standalone = std::make_shared<HullRegressionCube>(
        Vector3(-20.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f),
        denseVertices);
    standalone->Name = "DenseStandalone";
    standalone->Anchored = true;
    workspace->addChild(standalone);

    auto weldFirst = std::make_shared<HullRegressionCube>(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f),
        denseVertices);
    auto weldSecond = std::make_shared<HullRegressionCube>(
        Vector3(6.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f),
        denseVertices);
    weldFirst->Name = "DenseWeldFirst";
    weldSecond->Name = "DenseWeldSecond";
    weldFirst->Anchored = true;
    workspace->addChild(weldFirst);
    workspace->addChild(weldSecond);

    physics->update(*workspace, 1.0f / 60.0f);
    RaycastHit standaloneHit;
    expect(physics->hasBody(*standalone) && physics->raycast(
               {-20.0f, 10.0f, 0.0f}, {0, -1, 0}, 20.0f, standaloneHit) &&
               standaloneHit.instance == standalone.get(),
           ">=64-point convex creates a standalone body and raycasts");

    auto weld = std::make_shared<Weld>(weldFirst, weldSecond);
    weld->Name = "DenseHullWeld";
    workspace->addChild(weld);
    physics->update(*workspace, 1.0f / 60.0f);
    RaycastHit weldedHit;
    expect(physics->hasBody(*weldFirst) && physics->hasBody(*weldSecond) &&
               physics->sharesBody(*weldFirst, *weldSecond) && physics->raycast(
                   {6.0f, 10.0f, 0.0f}, {0, -1, 0}, 20.0f, weldedHit) &&
               weldedHit.instance == weldSecond.get(),
           "multiple dense convexes build one welded compound");

    auto terrainOwner = std::make_shared<Instance>("DenseHullTerrainOwner");
    PhysicsTerrainDescriptor terrainDescriptor;
    terrainDescriptor.userData = terrainOwner.get();
    PhysicsTerrainHullDescriptor terrainHull;
    terrainHull.localFrame = CFrame(Vector3(40.0f, 0.0f, 0.0f));
    terrainHull.vertices = denseVertices;
    terrainDescriptor.hulls.push_back(std::move(terrainHull));
    const PhysicsTerrainHandle terrain =
        physics->createTerrain(terrainDescriptor);
    RaycastHit terrainHit;
    expect(terrain && physics->raycast(
               {40.0f, 10.0f, 0.0f}, {0, -1, 0}, 20.0f, terrainHit) &&
               terrainHit.instance == terrainOwner.get(),
           ">=64-point terrain convex is simplified and raycasts");

    const std::vector<Vector3> degenerateVertices = {
        {0.2f, 0.1f, -0.15f},
        {0.25f, 0.1f, -0.15f},
        {0.3f, 0.1f, -0.15f},
        {0.4f, 0.1f, -0.15f},
    };
    int fallbackWarnings = 0;
    const auto previousLogHook = g_logHook;
    g_logHook = [&](const std::string& message) {
        if (message.find("Box3D convex hull used local bounds fallback") !=
            std::string::npos)
            ++fallbackWarnings;
        if (previousLogHook) previousLogHook(message);
    };
    auto degenerate = std::make_shared<HullRegressionCube>(
        Vector3(20.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f),
        degenerateVertices);
    degenerate->Name = "DegenerateHull";
    degenerate->Anchored = true;
    workspace->addChild(degenerate);
    physics->update(*workspace, 1.0f / 60.0f);
    RaycastHit fallbackHit;
    const bool hitOffCenterBounds = physics->raycast(
        {21.2f, 5.0f, -0.6f}, {0, -1, 0}, 10.0f, fallbackHit);
    expect(physics->hasBody(*degenerate) && hitOffCenterBounds &&
               fallbackHit.instance == degenerate.get(),
           "degenerate convex uses its off-center local bounds for collision");

    physics->recreateActor(degenerate);
    physics->recreateActor(degenerate);
    expect(fallbackWarnings == 1,
           "repeated degenerate rebuilds emit one bounds fallback warning");
    degenerate->vertices = denseVertices;
    physics->recreateActor(degenerate);
    degenerate->vertices = degenerateVertices;
    physics->recreateActor(degenerate);
    expect(fallbackWarnings == 2,
           "normal hull success resets bounds fallback warning suppression");
    g_logHook = previousLogHook;
    physics->destroyTerrain(terrain);

    std::cout << "[Box3DHullRegression] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runConstraintRebindRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[ConstraintRebind] " << (condition ? "PASS: " : "FAIL: ")
                  << message << '\n';
        if (!condition) ++failures;
    };

    auto workspaceA = std::make_shared<Workspace>();
    auto workspaceB = std::make_shared<Workspace>();
    workspaceA->Name = "WorkspaceA";
    workspaceB->Name = "WorkspaceB";
    workspaceA->Gravity = {};
    workspaceB->Gravity = {};
    workspaceA->initPhysics();
    workspaceB->initPhysics();

    auto cube0 = std::make_shared<BaseCube>(Vector3(0, 0, 0), Vector3(2, 2, 2));
    auto cube1 = std::make_shared<BaseCube>(Vector3(4, 0, 0), Vector3(2, 2, 2));
    cube0->Name = "Cube0";
    cube1->Name = "Cube1";
    auto attachment0 = std::make_shared<Attachment>(Vector3(1, 0, 0));
    auto attachment1 = std::make_shared<Attachment>(Vector3(-1, 0, 0));
    attachment0->Name = "Attachment0";
    attachment1->Name = "Attachment1";
    cube0->addChild(attachment0);
    cube1->addChild(attachment1);
    workspaceA->addChild(cube0);
    workspaceA->addChild(cube1);

    auto motor = std::make_shared<Motor>(cube0, cube1);
    motor->Name = "Motor";
    workspaceA->addChild(motor);
    YAML::Node attachmentName;
    attachmentName = "Attachment0";
    motor->setProperty("Attachment0", attachmentName);
    attachmentName = "Attachment1";
    motor->setProperty("Attachment1", attachmentName);

    auto* physicsA = workspaceA->getPhysicsEngine();
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    const auto originalHandle = motor->getConstraintHandle();
    expect(static_cast<bool>(originalHandle), "Motor receives an initial native binding");

    attachment0->Position.x += 0.5f;
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    const auto movedAttachmentHandle = motor->getConstraintHandle();
    expect(movedAttachmentHandle && movedAttachmentHandle != originalHandle,
           "Attachment transform change rebuilds the native binding");

    YAML::Node invalidPath;
    invalidPath = "MissingCube";
    motor->setProperty("Cube1", invalidPath);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    expect(!motor->getConstraintHandle(),
           "invalid endpoint path clears the old weak binding and native joint");

    YAML::Node validPath;
    validPath = cube1->getWorkspaceRelativePath();
    motor->setProperty("Cube1", validPath);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    expect(static_cast<bool>(motor->getConstraintHandle()),
           "valid endpoint path reconnects automatically");

    cube1->setParent(workspaceB);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    expect(!motor->getConstraintHandle(),
           "cross-Workspace endpoint has no native binding");
    expect(motor->Parent.lock() == workspaceA,
           "endpoint departure preserves the logical Constraint Instance");

    cube1->setParent(workspaceA);
    physicsA->update(*workspaceA, 1.0f / 60.0f);
    expect(static_cast<bool>(motor->getConstraintHandle()),
           "returning endpoint reconnects during the next fixed-step flush");

    std::cout << "[ConstraintRebind] "
              << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runTerrainInstanceRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[TerrainInstance] " << (condition ? "PASS: " : "FAIL: ")
                  << message << '\n';
        if (!condition) ++failures;
    };

    auto workspaceA = std::make_shared<Workspace>();
    auto workspaceB = std::make_shared<Workspace>();
    workspaceA->Name = "WorkspaceA";
    workspaceB->Name = "WorkspaceB";
    workspaceA->initPhysics();
    workspaceB->initPhysics();
    auto folder = std::make_shared<Model>();
    folder->Name = "TerrainFolder";
    workspaceA->addChild(folder);

    auto terrain0 = std::make_shared<Terrain>();
    auto terrain1 = std::make_shared<Terrain>();
    terrain0->Name = "Terrain0";
    terrain1->Name = "Terrain1";
    folder->addChild(terrain0);
    workspaceA->addChild(terrain1);
    expect(SceneRuntime::collectTerrains(workspaceA.get()).size() == 2,
           "recursive enumeration finds multiple nested Terrain instances");
    expect(terrain0->DataPath.empty() && terrain1->DataPath.empty(),
           "new Terrain has no implicit current-directory DataPath");
    SceneRuntime::updateTerrains(workspaceA.get(), Vector3());
    expect(!terrain0->streamer && !terrain1->streamer,
           "empty DataPath does not create a streamer or terrain directory");

    terrain0->setDataPath("build/terrain_instance_regression_a");
    terrain1->setDataPath("build/terrain_instance_regression_b");
    // Headless runnerにはOpenGL contextが無いため、streamerの所有権遷移
    // だけを構築し、chunk mesh uploadを伴うupdateはGUI回帰に委ねる。
    terrain0->streamer = std::make_unique<TerrainStreamer>(
        workspaceA.get(), terrain0.get(), terrain0->DataPath);
    terrain1->streamer = std::make_unique<TerrainStreamer>(
        workspaceA.get(), terrain1.get(), terrain1->DataPath);
    expect(terrain0->streamer && terrain1->streamer,
           "all enabled Terrain instances own independent streamers");

    terrain0->setEnabled(false);
    expect(!terrain0->streamer && terrain1->streamer,
           "disabling one Terrain immediately releases only its streamer");
    terrain0->setEnabled(true);
    terrain0->streamer = std::make_unique<TerrainStreamer>(
        workspaceA.get(), terrain0.get(), terrain0->DataPath);
    expect(terrain0->streamer != nullptr,
           "re-enabling Terrain recreates its streamer on update");

    terrain0->setParent(workspaceB);
    expect(!terrain0->streamer,
           "Workspace move releases old-world Terrain resources immediately");
    terrain0->streamer = std::make_unique<TerrainStreamer>(
        workspaceB.get(), terrain0.get(), terrain0->DataPath);
    expect(terrain0->streamer != nullptr,
           "moved Terrain creates an independent streamer in the new Workspace");

    auto cloned = std::dynamic_pointer_cast<Terrain>(terrain0->clone());
    expect(cloned && cloned->Enabled == terrain0->Enabled &&
               cloned->DataPath == terrain0->DataPath &&
               cloned->Seed == terrain0->Seed && cloned->Flat == terrain0->Flat,
           "clone preserves Terrain type and settings");
    expect(cloned && !cloned->streamer,
           "clone does not duplicate streamer or native handles");

    PhysicsTerrainDescriptor descriptor;
    descriptor.vertices = {
        {-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, {-2, 0, 2},
    };
    descriptor.indices = {0, 2, 1, 0, 3, 2};
    descriptor.userData = terrain1.get();
    auto* physics = workspaceA->getPhysicsEngine();
    PhysicsTerrainHandle handle = physics->createTerrain(descriptor);
    RaycastHit beforeDelete;
    expect(handle && physics->raycast(
               Vector3(0, 5, 0), Vector3(0, -1, 0), 10, beforeDelete),
           "non-empty Terrain descriptor creates collision");
    PhysicsTerrainDescriptor empty;
    handle = physics->replaceTerrain(handle, empty);
    RaycastHit afterDelete;
    expect(!handle && !physics->raycast(
               Vector3(0, 5, 0), Vector3(0, -1, 0), 10, afterDelete),
           "empty replacement destroys the final Terrain collision handle");

    terrain0->releaseStreamer();
    terrain1->releaseStreamer();
    std::cout << "[TerrainInstance] "
              << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

struct HumanoidFrameRateSample {
    float walkCycle = 0.0f;
    Vector3 currentMoveDir;
    Quaternion rootRotation;
    bool valid = false;
};

HumanoidFrameRateSample sampleHumanoidSmoothing(float smoothing) {
    auto workspace = std::make_shared<Workspace>();
    auto root = std::make_shared<BaseCube>(Vector3(0, 100, 0), Vector3(2, 2, 2));
    root->Name = "CharacterSmoothingRoot";
    auto humanoid = std::make_shared<Humanoid>();
    humanoid->Name = "CharacterSmoothingHumanoid";
    humanoid->setRootPart(root);
    workspace->addChild(root);
    workspace->addChild(humanoid);
    workspace->initPhysics();

    Physics* physics = workspace->getPhysicsEngine();
    if (!physics) return {};
    physics->update(*workspace, 0.0f);
    if (!physics->hasBody(*root)) return {};

    humanoid->move(Vector3(0, 0, -1), Vector3(1, 0, 0), true, Vector3(1, 0, 0), false,
                   physics, false, false, 1.0f, 0.0f, smoothing, 1.0f / 60.0f);
    return { humanoid->getWalkCycle(), humanoid->getCurrentMoveDir(), root->Rotation, true };
}

int runUserCharacterSmoothingRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[UserCharacterSmoothing] " << (condition ? "PASS: " : "FAIL: ")
                  << message << '\n';
        if (!condition) ++failures;
    };

    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>());
    expect(std::abs(user->characterSmoothing - 0.15f) < 0.0001f,
           "default is the previous 0.15 smoothing rate");

    YAML::Node smoothing;
    smoothing = 2.0f;
    user->setProperty("CharacterSmoothing", smoothing);
    expect(user->characterSmoothing == 1.0f, "YAML setter clamps values above one");
    smoothing = -1.0f;
    user->setProperty("CharacterSmoothing", smoothing);
    expect(user->characterSmoothing == 0.0f, "YAML setter clamps values below zero");
    smoothing = std::numeric_limits<float>::quiet_NaN();
    user->setProperty("CharacterSmoothing", smoothing);
    expect(std::abs(user->characterSmoothing - 0.15f) < 0.0001f,
           "YAML NaN resets to the safe default");

    auto system = std::make_shared<System>();
    auto users = std::make_shared<Users>();
    system->addChild(users);
    user->initializeInventory();
    users->addChild(user);
    user->characterSmoothing = 0.75f;
    const auto yamlPath = std::filesystem::temp_directory_path() /
        ("recubin_user_character_smoothing_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
    SceneLoader::saveScene(system.get(), yamlPath.string());
    const std::string saved = FileLoader::readText(yamlPath.string());
    expect(saved.find("CharacterSmoothing: 0.75") != std::string::npos,
           "scene save persists CharacterSmoothing");
    std::error_code removeError;
    std::filesystem::remove(yamlPath, removeError);
    expect(!removeError, "temporary save fixture is removed");

    LuauEngine engine;
    engine.setGlobalInstance("User", user);
    auto script = std::make_shared<Script>();
    script->Source =
        "User.CharacterSmoothing = 2 assert(User.CharacterSmoothing == 1) "
        "User.CharacterSmoothing = -3 assert(User.CharacterSmoothing == 0) "
        "User.CharacterSmoothing = 0/0 assert(math.abs(User.CharacterSmoothing - 0.15) < 0.001)";
    expect(engine.execute(*script), "Luau getter/setter clamps and handles NaN");

    const auto normal = sampleHumanoidSmoothing(0.15f);
    const auto immediate = sampleHumanoidSmoothing(1.0f);
    const auto frozen = sampleHumanoidSmoothing(0.0f);
    expect(normal.valid && immediate.valid && frozen.valid,
           "Humanoid smoothing samples initialize physics bodies");
    expect(std::abs(normal.currentMoveDir.x - 0.15f) < 0.001f,
           "0.15 retains the former one-frame interpolation rate");
    expect(immediate.currentMoveDir.x > 0.999f,
           "one applies movement direction without interpolation");
    expect(frozen.currentMoveDir.length() < 0.001f,
           "zero does not follow the movement target");

    std::cout << "[UserCharacterSmoothing] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runUserInputControlsRegression() {
    int failures = 0;
    auto expect = [&](bool ok, const char* message) {
        std::cout << "[UserInputControls] " << (ok ? "PASS: " : "FAIL: ") << message << '\n';
        if (!ok) ++failures;
    };

    auto backend = std::make_unique<FrameRateTestInputBackend>();
    auto* input = backend.get();
    auto user = std::make_shared<User>(std::move(backend));
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    const std::array<KeyCode, 12> functionKeys = {
        KeyCode::F1, KeyCode::F2, KeyCode::F3, KeyCode::F4, KeyCode::F5, KeyCode::F6,
        KeyCode::F7, KeyCode::F8, KeyCode::F9, KeyCode::F10, KeyCode::F11, KeyCode::F12};
    bool allFunctionKeysRaw = true;
    for (size_t i = 0; i < functionKeys.size(); ++i) {
        input->pressedKeys.insert(functionKeys[i]);
        user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
        allFunctionKeysRaw = allFunctionKeysRaw && user->Input->isPressed("F" + std::to_string(i + 1));
        input->pressedKeys.erase(functionKeys[i]);
        user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    }
    expect(allFunctionKeysRaw, "User.Input exposes all raw F1-F12 key states");

    YAML::Node off; off = false;
    user->setProperty("MovementInputEnabled", off);
    user->setProperty("CameraInputEnabled", off);
    user->setProperty("HotkeyInputEnabled", off);
    user->setProperty("ToolInputEnabled", off);
    expect(!user->isMovementInputEnabled() && !user->isCameraInputEnabled() &&
           !user->isHotkeyInputEnabled() && !user->isToolInputEnabled(),
           "YAML controls persist all input categories");
    YAML::Node on; on = true;
    user->setProperty("MovementInputEnabled", on);
    user->setProperty("CameraInputEnabled", on);
    user->setProperty("HotkeyInputEnabled", on);
    user->setProperty("ToolInputEnabled", on);

    user->setGameViewport(10, 20, 100, 60, 1.0f, Vector3(), Vector3(0, 0, -1),
                          Vector3(1, 0, 0), Vector3(0, 1, 0), 60, 50, true);
    expect(user->setMouseLockEnabled(true) && input->mouseCaptured,
           "MouseLock captures through the fake backend when focused viewport is valid");
    expect(!user->setMouseLockEnabled(false) && !input->mouseCaptured,
           "MouseLock releases the fake backend");

    input->pressedKeys.insert(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->isMouseLockEnabled(), "F8 enables MouseLock only during focused gameplay input");
    input->pressedKeys.erase(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    input->pressedKeys.insert(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(!user->isMouseLockEnabled(), "F8 toggles MouseLock off on its next rising edge");
    input->pressedKeys.erase(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);

    input->pressedKeys.insert(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, false, false);
    expect(!user->isMouseLockEnabled(), "F8 is ignored outside gameplay input");
    input->pressedKeys.erase(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    input->pressedKeys.insert(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, false, false, true, false);
    expect(!user->isMouseLockEnabled(), "F8 is ignored without viewport focus");
    input->pressedKeys.erase(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);

    input->pressedKeys.insert(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, true);
    expect(!user->isMouseLockEnabled(), "F8 is ignored while text input owns the keyboard");
    input->pressedKeys.erase(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    user->setProperty("CameraInputEnabled", off);
    input->pressedKeys.insert(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(!user->isMouseLockEnabled(), "Camera category gates the F8 builtin only");
    input->pressedKeys.erase(KeyCode::F8);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->setMouseLockEnabled(true), "direct MouseLock bypasses the camera input category");
    input->pressedKeys.insert(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->isMouseLockEnabled(), "Escape requests exit without releasing MouseLock");
    input->pressedKeys.erase(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, false, false, true, false);
    expect(!user->isMouseLockEnabled() && !input->mouseCaptured,
           "viewport focus loss releases MouseLock capture");
    user->setProperty("CameraInputEnabled", on);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->setMouseLockEnabled(true), "MouseLock can be restored after focus returns");
    user->setGameViewport(20, 30, 120, 80, 1.0f, Vector3(), Vector3(0, 0, -1),
                          Vector3(1, 0, 0), Vector3(0, 1, 0), 80, 70, true);
    double anchorX = 0.0, anchorY = 0.0;
    user->getRotationAnchor(anchorX, anchorY);
    expect(anchorX == 80.0 && anchorY == 70.0 && input->cursorX == 80.0 && input->cursorY == 70.0,
           "MouseLock viewport changes recenter the camera anchor without a delta");
    const Quaternion beforeProgramRotation = user->cam.Orientation;
    user->controlMode = User::ControlMode::Program;
    input->cursorX = 140.0;
    input->cursorY = 110.0;
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(input->mouseCaptured && user->cam.Orientation.w == beforeProgramRotation.w &&
               user->cam.Orientation.x == beforeProgramRotation.x &&
               user->cam.Orientation.y == beforeProgramRotation.y &&
               user->cam.Orientation.z == beforeProgramRotation.z,
           "Program MouseLock retains capture but never applies cursor rotation");
    user->controlMode = User::ControlMode::Character;
    user->setMouseLockEnabled(false);

    user->setMoveDirection(Vector3(3, 0, 0));
    user->clearMoveDirection();
    user->queueJump();
    user->requestWorkspaceSwitch();
    expect(user->consumeWorkspaceSwitchRequest(), "direct workspace switch request bypasses categories");
    user->requestExit();
    expect(user->consumeExitRequest() && !user->isExitRequestPending(),
           "unhandled exit request is consumed immediately");

    auto character = std::make_shared<Model>();
    character->Name = "InputControlsCharacter";
    user->character = character;
    user->initializeInventory();
    auto tool = std::make_shared<Tool>("InputControlsTool");
    user->addToolToSlot(tool, 0);
    expect(user->selectToolSlot(1) && user->activateTool(),
           "direct one-based tool selection and activation work");

    auto inputRoot = std::make_shared<BaseCube>(Vector3(), Vector3(2, 2, 2));
    auto inputHumanoid = std::make_shared<Humanoid>();
    inputHumanoid->setRootPart(inputRoot);
    user->humanoid = inputHumanoid;
    user->controlMode = User::ControlMode::Character;
    user->setProperty("MovementInputEnabled", off);
    input->pressedKeys.insert(KeyCode::W);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->Input->isPressed("W") && !user->lastMovementInput.isPressingMove,
           "Movement category gates W builtin while raw input remains available");
    input->pressedKeys.erase(KeyCode::W);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);

    const Quaternion beforeArrowRotation = user->cam.Orientation;
    user->setProperty("CameraInputEnabled", off);
    input->pressedKeys.insert(KeyCode::Left);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->Input->isPressed("Left") && user->cam.Orientation.w == beforeArrowRotation.w &&
               user->cam.Orientation.x == beforeArrowRotation.x &&
               user->cam.Orientation.y == beforeArrowRotation.y &&
               user->cam.Orientation.z == beforeArrowRotation.z,
           "Camera category gates arrow rotation while raw input remains available");
    input->pressedKeys.erase(KeyCode::Left);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    user->setProperty("CameraInputEnabled", on);

    user->setProperty("HotkeyInputEnabled", off);
    input->pressedKeys.insert(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->Input->isPressed("Escape") && !user->consumeExitRequest(),
           "Hotkey category gates Escape builtin while raw input remains available");
    input->pressedKeys.erase(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    user->setProperty("HotkeyInputEnabled", on);

    user->setProperty("ToolInputEnabled", off);
    input->pressedKeys.insert(KeyCode::Num1);
    input->pressedMouseButtons.insert(MouseButton::Left);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    const bool toolSuppressed = user->Input->isPressed("1") && user->Input->isPressed("MouseButton1") &&
        user->currentTool == tool;
    user->setProperty("ToolInputEnabled", on);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(toolSuppressed && user->currentTool == tool,
           "Tool category gates Num/left-click and synchronizes held input without latent activation");
    input->pressedKeys.erase(KeyCode::Num1);
    input->pressedMouseButtons.erase(MouseButton::Left);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);

    user->setMoveDirection(Vector3(3, 0, 0));
    user->processInput(nullptr, 1.0f / 60.0f, false, false, true, false);
    const auto scriptedMovement = user->lastMovementInput;
    user->clearMoveDirection();
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(scriptedMovement.isPressingMove && scriptedMovement.targetMoveDir.x > 0.99f &&
               scriptedMovement.rightAxis > 0.99f &&
               !user->lastMovementInput.isPressingMove &&
               user->lastMovementInput.targetMoveDir.lengthSquared() < 0.0001f,
           "persistent world-space script movement works unfocused and clear sends zero Character input");
    user->queueJump();
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(user->lastMovementInput.jumpRequested,
           "direct Jump is queued and consumed by the next physics input frame");

    LuauEngine engine;
    engine.setGlobalInstance("User", user);
    auto script = std::make_shared<Script>();
    script->Source =
        "User.MovementInputEnabled=false User.CameraInputEnabled=false "
        "User.HotkeyInputEnabled=false User.ToolInputEnabled=false "
        "assert(not User.MovementInputEnabled and not User.CameraInputEnabled and "
        "not User.HotkeyInputEnabled and not User.ToolInputEnabled) "
        "User.Input.Pressed:Connect(function(key) if key == 'F2' then User.MovementInputEnabled=false end end) "
        "User.Input.Released:Connect(function(key) if key == 'F2' then User.CameraInputEnabled=false end end) "
        "User:SetMoveDirection(Vector3.new(0,0,-1)) User:ClearMoveDirection() "
        "assert(User:ToggleCtrlLock() == true) assert(User:SetCtrlLockOffset('Left') == 'Left')";
    expect(engine.execute(*script), "Luau exposes category properties and direct input APIs");
    user->setProperty("MovementInputEnabled", on);
    user->setProperty("CameraInputEnabled", on);
    input->pressedKeys.insert(KeyCode::F2);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    const bool pressedEdgeDelivered = !user->isMovementInputEnabled();
    input->pressedKeys.erase(KeyCode::F2);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(pressedEdgeDelivered && !user->isCameraInputEnabled(),
           "raw F-key Pressed and Released signals deliver exact F2 names while categories are disabled");

    auto exitScript = std::make_shared<Script>();
    exitScript->Source =
        "User.ToolInputEnabled=true "
        "User.ExitRequested:Connect(function() User.ToolInputEnabled=false end)";
    expect(engine.execute(*exitScript), "Luau can subscribe to User.ExitRequested");
    user->setProperty("HotkeyInputEnabled", on);
    input->pressedKeys.insert(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    const bool firstExitDelivered = user->isExitRequestPending() && !user->isToolInputEnabled();
    user->setProperty("ToolInputEnabled", on);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    expect(firstExitDelivered && user->isExitRequestPending() && user->isToolInputEnabled(),
           "Escape listener exit is pending and held Escape does not refire");
    input->pressedKeys.erase(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    user->cancelExit();
    input->pressedKeys.insert(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    input->pressedKeys.erase(KeyCode::Escape);
    user->processInput(nullptr, 1.0f / 60.0f, true, true, true, false);
    user->confirmExit();
    expect(!user->isExitRequestPending() && user->consumeExitRequest(),
           "CancelExit and ConfirmExit resolve the listener-aware exit request");

    const auto yamlSuffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto yamlPath = std::filesystem::temp_directory_path() /
        ("recubin_user_input_controls_" + yamlSuffix + ".yaml");
    const auto legacyYamlPath = std::filesystem::temp_directory_path() /
        ("recubin_user_input_controls_legacy_" + yamlSuffix + ".yaml");
    auto persistedSystem = std::make_shared<System>();
    auto persistedUsers = std::make_shared<Users>();
    persistedSystem->addChild(persistedUsers);
    persistedUsers->addChild(user);
    user->setProperty("MovementInputEnabled", off);
    user->setProperty("CameraInputEnabled", off);
    user->setProperty("HotkeyInputEnabled", off);
    user->setProperty("ToolInputEnabled", off);
    const bool savedControls = SceneLoader::saveSceneResult(persistedSystem.get(), yamlPath.string());
    auto loadSystem = std::make_shared<System>();
    auto loadUser = std::make_shared<User>(std::make_unique<NullInputBackend>(), true);
    auto loadedControls = SceneRuntime::stageSceneLoad(
        savedControls ? yamlPath.string() : std::string{}, loadSystem, loadUser);
    expect(savedControls && loadedControls && loadedControls.user && !loadedControls.user->isMovementInputEnabled() &&
               !loadedControls.user->isCameraInputEnabled() && !loadedControls.user->isHotkeyInputEnabled() &&
               !loadedControls.user->isToolInputEnabled(),
           "SceneLoader saves and loads all input category properties");
    {
        std::ofstream legacy(legacyYamlPath, std::ios::binary | std::ios::trunc);
        legacy << "Root:\n  ClassName: System\n  Children:\n    - ClassName: Users\n"
                  "      Children:\n        - ClassName: User\n          Name: User\n";
    }
    auto legacySystem = std::make_shared<System>();
    auto legacyUser = std::make_shared<User>(std::make_unique<NullInputBackend>(), true);
    auto legacyControls = SceneRuntime::stageSceneLoad(
        legacyYamlPath.string(), legacySystem, legacyUser);
    expect(legacyControls && legacyControls.user && legacyControls.user->isMovementInputEnabled() &&
               legacyControls.user->isCameraInputEnabled() && legacyControls.user->isHotkeyInputEnabled() &&
               legacyControls.user->isToolInputEnabled(),
           "legacy YAML without input category properties keeps true defaults");
    std::error_code yamlError;
    std::filesystem::remove(yamlPath, yamlError);
    std::filesystem::remove(legacyYamlPath, yamlError);

    std::cout << "[UserInputControls] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

HumanoidFrameRateSample sampleHumanoidAtFrameRate(int frameRate) {
    auto workspace = std::make_shared<Workspace>();
    auto root = std::make_shared<BaseCube>(Vector3(0, 100, 0), Vector3(2, 2, 2));
    root->Name = "FrameRateRoot";
    auto humanoid = std::make_shared<Humanoid>();
    humanoid->Name = "FrameRateHumanoid";
    humanoid->setRootPart(root);
    workspace->addChild(root);
    workspace->addChild(humanoid);
    workspace->initPhysics();

    Physics* physics = workspace->getPhysicsEngine();
    if (!physics) return {};
    physics->update(*workspace, 0.0f);
    if (!physics->hasBody(*root)) return {};

    const float dt = 1.0f / static_cast<float>(frameRate);
    const Vector3 flatForward(0, 0, -1);
    const Vector3 flatRight(1, 0, 0);
    const Vector3 targetMoveDir(1, 0, 0);
    for (int frame = 0; frame < frameRate; ++frame) {
        humanoid->move(flatForward, flatRight, true, targetMoveDir, false, physics,
                       false, false, 1.0f, 0.0f, 0.15f, dt);
    }

    return {
        humanoid->getWalkCycle(),
        humanoid->getCurrentMoveDir(),
        root->Rotation,
        true,
    };
}

struct CameraFrameRateSample {
    Vector3 position;
    Quaternion rotation;
};

CameraFrameRateSample sampleCameraAtFrameRate(int frameRate, KeyCode key) {
    auto backend = std::make_unique<FrameRateTestInputBackend>();
    backend->pressedKeys.insert(key);
    auto user = std::make_shared<User>(std::move(backend));
    user->controlMode = User::ControlMode::Free;

    const float dt = 1.0f / static_cast<float>(frameRate);
    for (int frame = 0; frame < frameRate; ++frame) {
        user->processInput(nullptr, dt, true, true, false, false);
    }
    return {user->cpos, user->cam.Orientation};
}

float quaternionDotMagnitude(const Quaternion& a, const Quaternion& b) {
    return std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
}

int runFrameRateInvarianceRegression() {
    const std::array<int, 3> frameRates{30, 60, 120};
    std::array<HumanoidFrameRateSample, 3> humanoidSamples;
    std::array<CameraFrameRateSample, 3> movementSamples;
    std::array<CameraFrameRateSample, 3> rotationSamples;
    std::array<CameraFrameRateSample, 3> zoomInSamples;
    std::array<CameraFrameRateSample, 3> zoomOutSamples;

    for (size_t i = 0; i < frameRates.size(); ++i) {
        humanoidSamples[i] = sampleHumanoidAtFrameRate(frameRates[i]);
        movementSamples[i] = sampleCameraAtFrameRate(frameRates[i], KeyCode::W);
        rotationSamples[i] = sampleCameraAtFrameRate(frameRates[i], KeyCode::Left);
        zoomInSamples[i] = sampleCameraAtFrameRate(frameRates[i], KeyCode::I);
        zoomOutSamples[i] = sampleCameraAtFrameRate(frameRates[i], KeyCode::O);
    }

    int failures = 0;
    auto expect = [&failures](bool condition, const char* name) {
        std::cout << "[FrameRateInvariance] " << (condition ? "PASS" : "FAIL")
                  << ": " << name << '\n';
        if (!condition) ++failures;
    };

    const HumanoidFrameRateSample& humanoidBaseline = humanoidSamples[1];
    for (size_t i = 0; i < frameRates.size(); ++i) {
        const auto& sample = humanoidSamples[i];
        expect(sample.valid, "Humanoid physics body is available");
        if (!sample.valid || !humanoidBaseline.valid) continue;
        expect(std::abs(sample.walkCycle - humanoidBaseline.walkCycle) <= 0.0001f,
               "Humanoid walkCycle is frame-rate invariant");
        expect(positionDistance(sample.currentMoveDir, humanoidBaseline.currentMoveDir) <= 0.0001f,
               "Humanoid currentMoveDir is frame-rate invariant");
        expect(quaternionDotMagnitude(sample.rootRotation, humanoidBaseline.rootRotation) >= 0.9999f,
               "Humanoid root rotation is frame-rate invariant");
    }

    auto cameraSamplesMatch = [](const std::array<CameraFrameRateSample, 3>& samples) {
        const auto& baseline = samples[1];
        for (const auto& sample : samples) {
            if (positionDistance(sample.position, baseline.position) > 0.001f ||
                quaternionDotMagnitude(sample.rotation, baseline.rotation) < 0.9999f) {
                return false;
            }
        }
        return true;
    };

    expect(cameraSamplesMatch(movementSamples), "Free camera W movement is frame-rate invariant");
    expect(cameraSamplesMatch(rotationSamples), "Free camera arrow rotation is frame-rate invariant");
    expect(cameraSamplesMatch(zoomInSamples), "Free camera I-key zoom is frame-rate invariant");
    expect(cameraSamplesMatch(zoomOutSamples), "Free camera O-key zoom is frame-rate invariant");
    expect(positionDistance(movementSamples[1].position, Vector3(0, -2, -10)) <= 0.001f,
           "Free camera W preserves the 60 Hz movement rate");
    expect(positionDistance(zoomInSamples[1].position, Vector3(0, -2, -1)) <= 0.001f &&
               positionDistance(zoomOutSamples[1].position, Vector3(0, -2, 11)) <= 0.001f,
           "Free camera I/O preserves the 60 Hz zoom rate");

    std::cout << "[FrameRateInvariance] " << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runViewportHelperRegression() {
    int failures = 0;
    auto expect = [&failures](bool condition, const char* name) {
        std::cout << "[ViewportHelperRegression] "
                  << (condition ? "PASS" : "FAIL") << ": " << name << '\n';
        if (!condition) ++failures;
    };

    const Quaternion identity;
    const CFrame identityBox(Vector3(0.0f, 0.0f, 0.0f), identity);
    const ViewportGeometry::ObbRayHit externalHit = ViewportGeometry::raycastObb(
        {Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, -1.0f)},
        identityBox,
        Vector3(2.0f, 2.0f, 2.0f));
    expect(externalHit.hit && near(externalHit.distance, 4.0f)
               && externalHit.axis == 2 && near(externalHit.sign, 1.0f),
           "external OBB ray hit preserves distance, axis, and face sign");

    const ViewportGeometry::ObbRayHit insideHit = ViewportGeometry::raycastObb(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f)},
        identityBox,
        Vector3(2.0f, 2.0f, 2.0f));
    expect(insideHit.hit && near(insideHit.distance, 1.0f)
               && insideHit.axis == 0 && near(insideHit.sign, -1.0f),
           "inside OBB ray exits with the established axis/sign semantics");

    const Quaternion rotatedBoxRotation =
        Quaternion::fromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), 90.0f);
    const ViewportGeometry::ObbRayHit rotatedHit = ViewportGeometry::raycastObb(
        {Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, -1.0f)},
        CFrame(Vector3(0.0f, 0.0f, 0.0f), rotatedBoxRotation),
        Vector3(2.0f, 2.0f, 4.0f));
    expect(rotatedHit.hit && near(rotatedHit.distance, 4.0f)
               && rotatedHit.axis == 0 && near(rotatedHit.sign, -1.0f),
           "rotated OBB ray hit reports the rotated local face");

    expect(!ViewportGeometry::obbIntersects(
               Vector3(0.0f, 0.0f, 0.0f), identity, Vector3(2.0f, 2.0f, 2.0f),
               Vector3(2.01f, 0.0f, 0.0f), identity, Vector3(2.0f, 2.0f, 2.0f)),
           "SAT rejects separated OBBs");
    expect(!ViewportGeometry::obbIntersects(
               Vector3(0.0f, 0.0f, 0.0f), identity, Vector3(2.0f, 2.0f, 2.0f),
               Vector3(2.0f, 0.0f, 0.0f), identity, Vector3(2.0f, 2.0f, 2.0f)),
           "SAT treats exact face contact as non-intersection");
    expect(ViewportGeometry::obbIntersects(
               Vector3(0.0f, 0.0f, 0.0f), identity, Vector3(2.0f, 2.0f, 2.0f),
               Vector3(1.99f, 0.0f, 0.0f), identity, Vector3(2.0f, 2.0f, 2.0f)),
           "SAT accepts positive-volume overlap");

    const ViewportGeometry::Ray centerRay = ViewportGeometry::makeScreenRay(
        Vector3(3.0f, 4.0f, 5.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector2(640.0f, 360.0f),
        Vector2(1280.0f, 720.0f));
    expect(positionDistance(centerRay.origin, Vector3(3.0f, 4.0f, 5.0f)) <= 0.001f
               && positionDistance(centerRay.direction, Vector3(0.0f, 0.0f, -1.0f)) <= 0.001f,
           "screen-center ray uses the camera origin and forward direction");

    auto transformParent = std::make_shared<Model>(Vector3(10.0f, 2.0f, -4.0f));
    transformParent->Rotation =
        Quaternion::fromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), 90.0f);
    auto transformChild = std::make_shared<BaseCube>(
        Vector3(1.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    transformChild->Name = "TransformChild";
    transformParent->addChild(transformChild);
    const Vector3 expectedLocalPosition(2.0f, -1.0f, 3.0f);
    const Vector3 worldPosition =
        transformParent->getWorldCFrame().pointToWorld(expectedLocalPosition);
    expect(positionDistance(
               ViewportGeometry::worldToLocalPosition(worldPosition, *transformChild),
               expectedLocalPosition) <= 0.001f,
           "world position converts through a rotated Spatial parent");

    const Quaternion expectedLocalRotation =
        Quaternion::fromAxisAngle(Vector3(1.0f, 0.0f, 0.0f), 30.0f);
    const Quaternion worldRotation =
        transformParent->getWorldCFrame().Rotation * expectedLocalRotation;
    expect(quaternionDotMagnitude(
               ViewportGeometry::worldToLocalRotation(worldRotation, *transformChild),
               expectedLocalRotation) >= 0.9999f,
           "world rotation converts through a rotated Spatial parent");

    const Matrix4 projection = Matrix4::Perspective(45.0f, 2.0f, 0.1f, 100.0f);
    const Matrix4 view = Matrix4::LookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    const ViewportGeometry::ProjectedPoint centerProjection =
        ViewportGeometry::projectWorldToScreen(
            projection * view,
            Vector3(0.0f, 0.0f, -5.0f),
            Vector2(10.0f, 20.0f),
            Vector2(200.0f, 100.0f));
    expect(centerProjection.visible && near(centerProjection.position.x, 110.0f)
               && near(centerProjection.position.y, 70.0f),
           "front-facing center point projects to the viewport center");
    expect(!ViewportGeometry::projectWorldToScreen(
                projection * view,
                Vector3(0.0f, 0.0f, 5.0f),
                Vector2(10.0f, 20.0f),
                Vector2(200.0f, 100.0f)).visible,
           "point behind the camera is not visible");

    // Retinaでも3D projectionはFramebufferのアスペクトを使うが、NDCの配置先は
    // ImGui論理座標のままにする。同じ16:9なら倍率1x/2xで表示位置は変わらない。
    const Vector3 guiCameraPosition(0.0f, 0.0f, 0.0f);
    const Vector3 guiCameraForward(0.0f, 0.0f, -1.0f);
    const Vector3 guiCameraRight(1.0f, 0.0f, 0.0f);
    const Vector3 guiCameraUp(0.0f, 1.0f, 0.0f);
    const GameGuiRenderContext normalDpiContext = Renderer::makeGameGuiRenderContext(
        0.0f, 0.0f, 1280.0f, 720.0f,
        guiCameraPosition, guiCameraForward, guiCameraRight, guiCameraUp,
        1280.0f / 720.0f);
    const GameGuiRenderContext retinaContext = Renderer::makeGameGuiRenderContext(
        0.0f, 0.0f, 1280.0f, 720.0f,
        guiCameraPosition, guiCameraForward, guiCameraRight, guiCameraUp,
        2560.0f / 1440.0f);
    const Vector3 billboardParentPosition(0.0f, 0.0f, -5.0f);
    const Vector3 billboardAnchor = CFrame(billboardParentPosition).pointToWorld(
        Vector3(0.0f, 1.25f, 0.0f));
    const auto normalDpiProjection = ViewportGeometry::projectWorldToScreen(
        normalDpiContext.projection * normalDpiContext.view,
        billboardAnchor,
        Vector2(normalDpiContext.viewportX, normalDpiContext.viewportY),
        Vector2(normalDpiContext.viewportWidth, normalDpiContext.viewportHeight));
    const auto retinaProjection = ViewportGeometry::projectWorldToScreen(
        retinaContext.projection * retinaContext.view,
        billboardAnchor,
        Vector2(retinaContext.viewportX, retinaContext.viewportY),
        Vector2(retinaContext.viewportWidth, retinaContext.viewportHeight));
    expect(normalDpiProjection.visible && retinaProjection.visible
               && positionDistance(
                      Vector3(normalDpiProjection.position.x, normalDpiProjection.position.y, 0.0f),
                      Vector3(retinaProjection.position.x, retinaProjection.position.y, 0.0f)) <= 0.001f,
           "Billboard projection is invariant between 1x and 2x framebuffer density");
    expect(near(normalDpiProjection.position.x, 640.0f)
               && normalDpiProjection.position.y < 360.0f,
           "positive local Y Billboard offset projects directly above its parent");

    const CFrame rotatedBillboardParent(
        Vector3(3.0f, 4.0f, -5.0f),
        Quaternion::fromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), 90.0f));
    const Vector3 rotatedAnchor = rotatedBillboardParent.pointToWorld(
        Vector3(0.0f, 1.25f, 0.0f));
    const Vector3 expectedRotatedAnchor = rotatedBillboardParent.Position
        + rotatedBillboardParent.Rotation.rotate(Vector3(0.0f, 1.25f, 0.0f));
    expect(positionDistance(rotatedAnchor, expectedRotatedAnchor) <= 0.001f,
           "Billboard offset follows the parent local rotation");

    auto workspace = std::make_shared<Workspace>();
    auto nestedModel = std::make_shared<Model>();
    nestedModel->Name = "NestedModel";
    auto innerModel = std::make_shared<Model>();
    innerModel->Name = "InnerModel";
    auto front = std::make_shared<BaseCube>(
        Vector3(0.0f, 0.0f, -3.0f), Vector3(1.0f, 1.0f, 1.0f));
    front->Name = "Front";
    auto lockedDescendant = std::make_shared<BaseCube>(
        Vector3(0.8f, 0.0f, 0.0f), Vector3(0.2f, 0.2f, 0.2f));
    lockedDescendant->Name = "LockedDescendant";
    auto behind = std::make_shared<BaseCube>(
        Vector3(0.0f, 0.0f, -6.0f), Vector3(1.0f, 1.0f, 1.0f));
    behind->Name = "Behind";
    workspace->addChild(nestedModel);
    nestedModel->addChild(innerModel);
    innerModel->addChild(front);
    front->addChild(lockedDescendant);
    workspace->addChild(behind);

    const ViewportGeometry::Ray sceneRay{
        Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f)};
    const ViewportSceneQueries::BaseCubeRayHit nearest =
        ViewportSceneQueries::findNearestBaseCube(*workspace, sceneRay);
    expect(nearest.hit && nearest.cube == front.get(),
           "nearest BaseCube search traverses nested instances");
    const ViewportSceneQueries::SelectionRayHit promotedSelection =
        ViewportSceneQueries::findSelectionTarget(*workspace, sceneRay);
    expect(promotedSelection.hit && !promotedSelection.locked
               && promotedSelection.cube == front.get()
               && promotedSelection.target == nestedModel.get(),
           "selection promotes the nearest cube to its topmost Model");
    const ViewportSceneQueries::BaseCubeRayHit excluded =
        ViewportSceneQueries::findNearestBaseCube(*workspace, sceneRay, front.get());
    expect(excluded.hit && excluded.cube == behind.get(),
           "nearest BaseCube exclusion skips the target subtree");

    front->Locked = true;
    const ViewportSceneQueries::BaseCubeRayHit lockedNearest =
        ViewportSceneQueries::findNearestBaseCube(*workspace, sceneRay);
    expect(lockedNearest.hit && lockedNearest.cube == front.get()
               && ViewportSceneQueries::isLockedBaseCube(lockedNearest.cube),
           "front Locked cube remains the hit ahead of an unlocked cube");
    const ViewportSceneQueries::SelectionRayHit lockedSelection =
        ViewportSceneQueries::findSelectionTarget(*workspace, sceneRay);
    expect(lockedSelection.hit && lockedSelection.locked
               && lockedSelection.cube == front.get()
               && lockedSelection.target == front.get(),
           "Locked cube blocks selection without promoting to its Model");

    auto priorityWorkspace = std::make_shared<Workspace>();
    auto looseFront = std::make_shared<BaseCube>(
        Vector3(0.0f, 0.0f, -3.0f), Vector3(1.0f, 1.0f, 1.0f));
    looseFront->Name = "LooseFront";
    auto rearModel = std::make_shared<Model>();
    rearModel->Name = "RearModel";
    auto rearChild = std::make_shared<BaseCube>(
        Vector3(0.0f, 0.0f, -6.0f), Vector3(1.0f, 1.0f, 1.0f));
    rearChild->Name = "RearChild";
    priorityWorkspace->addChild(looseFront);
    priorityWorkspace->addChild(rearModel);
    rearModel->addChild(rearChild);
    const ViewportSceneQueries::SelectionRayHit depthPriority =
        ViewportSceneQueries::findSelectionTarget(*priorityWorkspace, sceneRay);
    expect(depthPriority.hit && depthPriority.target == looseFront.get(),
           "front loose cube remains ahead of a rear Model target");

    auto gapWorkspace = std::make_shared<Workspace>();
    auto gapModel = std::make_shared<Model>();
    gapModel->Name = "GapModel";
    auto gapLeft = std::make_shared<BaseCube>(
        Vector3(-2.0f, 0.0f, -4.0f), Vector3(1.0f, 1.0f, 1.0f));
    auto gapRight = std::make_shared<BaseCube>(
        Vector3(2.0f, 0.0f, -4.0f), Vector3(1.0f, 1.0f, 1.0f));
    gapLeft->Name = "GapLeft";
    gapRight->Name = "GapRight";
    gapWorkspace->addChild(gapModel);
    gapModel->addChild(gapLeft);
    gapModel->addChild(gapRight);
    expect(!ViewportSceneQueries::findSelectionTarget(*gapWorkspace, sceneRay).hit,
           "empty space inside a Model AABB is not selectable");

    const ViewportSceneQueries::PickerRayHit cubePick =
        ViewportSceneQueries::findPickerTarget(
            *workspace, sceneRay, ViewportSceneQueries::PickerTargetType::BaseCube);
    expect(cubePick.hit && cubePick.target == front.get(),
           "BaseCube picker returns the nearest cube");

    auto attachment = std::make_shared<Attachment>(Vector3(3.0f, 0.0f, -2.0f));
    attachment->Name = "PickerAttachment";
    workspace->addChild(attachment);
    const ViewportSceneQueries::PickerRayHit attachmentPick =
        ViewportSceneQueries::findPickerTarget(
            *workspace,
            {Vector3(3.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f)},
            ViewportSceneQueries::PickerTargetType::Attachment);
    expect(attachmentPick.hit && attachmentPick.target == attachment.get()
               && near(attachmentPick.distance, 1.75f),
           "Attachment picker uses the fixed helper hit volume");

    const std::vector<Instance*> boxSelection =
        ViewportSceneQueries::collectBoxSelectableCubes(
            *workspace,
            Matrix4(),
            Vector2(0.0f, 0.0f),
            Vector2(100.0f, 100.0f),
            Vector2(0.0f, 0.0f),
            Vector2(100.0f, 100.0f));
    expect(std::find(boxSelection.begin(), boxSelection.end(), behind.get())
               != boxSelection.end(),
           "box selection collects an unlocked cube");
    expect(std::find(boxSelection.begin(), boxSelection.end(), front.get())
                   == boxSelection.end()
               && std::find(boxSelection.begin(), boxSelection.end(), lockedDescendant.get())
                   == boxSelection.end(),
           "box selection excludes a Locked cube and its descendants");

    auto boundsRoot = std::make_shared<Model>(Vector3(10.0f, 0.0f, 0.0f));
    auto boundsLeft = std::make_shared<BaseCube>(
        Vector3(-2.0f, 1.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    boundsLeft->Name = "BoundsLeft";
    auto boundsNested = std::make_shared<Model>(Vector3(3.0f, -1.0f, 2.0f));
    boundsNested->Name = "BoundsNested";
    auto boundsRight = std::make_shared<BaseCube>(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 2.0f, 2.0f));
    boundsRight->Name = "BoundsRight";
    boundsRoot->addChild(boundsLeft);
    boundsRoot->addChild(boundsNested);
    boundsNested->addChild(boundsRight);
    const ViewportGeometry::WorldAabb bounds =
        ViewportSceneQueries::computeDescendantWorldAabb(*boundsRoot);
    expect(bounds.valid
               && positionDistance(bounds.minimum, Vector3(7.0f, -2.0f, -1.0f)) <= 0.001f
               && positionDistance(bounds.maximum, Vector3(15.0f, 2.0f, 3.0f)) <= 0.001f,
           "descendant world AABB includes nested BaseCubes");
    const ViewportSceneQueries::MovementBounds modelBounds =
        ViewportSceneQueries::computeMovementBounds(*boundsRoot);
    expect(modelBounds.valid
               && positionDistance(modelBounds.center, Vector3(11.0f, 0.0f, 1.0f)) <= 0.001f
               && positionDistance(modelBounds.size, Vector3(8.0f, 4.0f, 4.0f)) <= 0.001f,
           "Model movement bounds use the descendant world AABB");
    const ViewportSceneQueries::MovementBounds rotatedCubeBounds =
        ViewportSceneQueries::computeMovementBounds(*transformChild);
    expect(rotatedCubeBounds.valid
               && positionDistance(rotatedCubeBounds.size, transformChild->Size) <= 0.001f
               && quaternionDotMagnitude(
                      rotatedCubeBounds.rotation,
                      transformChild->getWorldCFrame().Rotation) >= 0.9999f,
           "BaseCube movement bounds preserve its rotated world OBB");
    expect(ViewportSceneQueries::collectHighlightBaseCubes(*boundsRoot).size() == 2,
           "Model highlight query returns descendant BaseCubes only once");

    auto fitWorkspace = std::make_shared<Workspace>();
    auto obstacle = std::make_shared<BaseCube>(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    obstacle->Name = "Obstacle";
    fitWorkspace->addChild(obstacle);
    auto moving = std::make_shared<BaseCube>(
        Vector3(1.5f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    const ViewportSceneQueries::MovementBounds movingBounds =
        ViewportSceneQueries::computeMovementBounds(*moving);
    expect(near(ViewportSceneQueries::fitOnAxis(
                    *fitWorkspace, moving->Position, movingBounds, *moving, 0),
                2.0f),
           "axis collision fit resolves overlap along the requested axis");
    expect(positionDistance(
               ViewportSceneQueries::fitCollision(
                   *fitWorkspace, moving->Position, movingBounds, *moving),
               Vector3(2.0f, 0.0f, 0.0f)) <= 0.001f,
           "minimum-overlap collision fit resolves a stable axis-aligned overlap");

    auto movingModel = std::make_shared<Model>(Vector3(1.5f, 0.0f, 0.0f));
    movingModel->Name = "MovingModel";
    auto movingChild = std::make_shared<BaseCube>(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    movingChild->Name = "MovingChild";
    movingModel->addChild(movingChild);
    fitWorkspace->addChild(movingModel);
    const ViewportSceneQueries::MovementBounds movingModelBounds =
        ViewportSceneQueries::computeMovementBounds(*movingModel);
    expect(positionDistance(
               ViewportSceneQueries::fitCollision(
                   *fitWorkspace, movingModelBounds.center, movingModelBounds, *movingModel),
               Vector3(2.0f, 0.0f, 0.0f)) <= 0.001f,
           "Model collision fit uses its AABB and excludes its descendant subtree");

    const Vector3 smallResize = ViewportGeometry::additiveResize(
        Vector3(2.0f, 4.0f, 6.0f), Vector3(1.5f, 1.0f, 1.0f), false, 1.0f);
    const Vector3 largeResize = ViewportGeometry::additiveResize(
        Vector3(20.0f, 40.0f, 60.0f), Vector3(1.5f, 1.0f, 1.0f), false, 1.0f);
    expect(near(smallResize.x - 2.0f, 0.5f)
               && near(largeResize.x - 20.0f, 0.5f)
               && near(smallResize.y, 4.0f) && near(largeResize.y, 40.0f),
           "additive resize produces the same world delta for different initial sizes");
    const Vector3 snappedResize = ViewportGeometry::additiveResize(
        Vector3(2.2f, 2.0f, 0.1f), Vector3(1.4f, 1.0f, 0.0f), true, 1.0f);
    expect(near(snappedResize.x, 3.0f) && near(snappedResize.y, 2.0f)
               && near(snappedResize.z, 0.05f),
           "additive resize preserves unchanged axes, absolute snap, and minimum size");

    const Quaternion identityRotation;
    const Vector3 centeredOrigin = ViewportGeometry::fixedFaceResizeOrigin(
        Vector3(1.0f, 2.0f, 3.0f), identityRotation,
        Vector3(2.0f, 4.0f, 6.0f), Vector3(3.0f, 4.0f, 6.0f),
        Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
    expect(near(centeredOrigin.x, 1.5f) && near(centeredOrigin.y, 2.0f)
               && near(centeredOrigin.z, 3.0f),
           "fixed-face resize preserves centered Spatial behavior");

    const Vector3 surfaceFarOrigin = ViewportGeometry::fixedFaceResizeOrigin(
        Vector3(0.0f, 0.0f, 0.0f), identityRotation,
        Vector3(4.0f, 4.0f, 4.0f), Vector3(4.0f, 4.0f, 6.0f),
        Vector3(0.0f, 0.0f, -1.0f), Vector3(0.0f, 0.0f, -0.5f));
    const Vector3 surfaceNearOrigin = ViewportGeometry::fixedFaceResizeOrigin(
        Vector3(0.0f, 0.0f, 0.0f), identityRotation,
        Vector3(4.0f, 4.0f, 4.0f), Vector3(4.0f, 4.0f, 6.0f),
        Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 0.0f, -0.5f));
    expect(near(surfaceFarOrigin.z, 0.0f) && near(surfaceNearOrigin.z, 2.0f),
           "SurfaceMark resize keeps the near origin or far face fixed");

    const Quaternion quarterTurn = Quaternion::fromEuler(Vector3(0.0f, 90.0f, 0.0f));
    const Vector3 rotatedSurfaceOrigin = ViewportGeometry::fixedFaceResizeOrigin(
        Vector3(0.0f, 0.0f, 0.0f), quarterTurn,
        Vector3(4.0f, 4.0f, 4.0f), Vector3(4.0f, 4.0f, 6.0f),
        Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 0.0f, -0.5f));
    expect(near(rotatedSurfaceOrigin.x, 2.0f) && near(rotatedSurfaceOrigin.z, 0.0f),
           "SurfaceMark fixed-face resize follows world rotation");

    const std::vector<Vector3> groupSizes{
        Vector3(2.0f, 4.0f, 1.0f), Vector3(1.0f, 2.0f, 8.0f)};
    const Vector3 axisFactors = ViewportGeometry::effectiveGroupScaleFactors(
        groupSizes, Vector3(2.0f, 1.0f, 1.0f), false, 0.25f);
    const Vector3 axisScaled = ViewportGeometry::groupScaleSize(
        groupSizes[0], axisFactors, false, 0.25f);
    expect(axisFactors == Vector3(2.0f, 1.0f, 1.0f) &&
               axisScaled == Vector3(4.0f, 4.0f, 1.0f),
           "group scale applies an axis factor to every selected size");
    const Vector3 uniformFactors = ViewportGeometry::effectiveGroupScaleFactors(
        groupSizes, Vector3(0.1f, 0.1f, 0.1f), false, 0.25f, 0.5f);
    expect(uniformFactors == Vector3(0.5f, 0.5f, 0.5f) &&
               ViewportGeometry::groupScaleSize(groupSizes[1], uniformFactors, false, 0.25f)
                   == Vector3(0.5f, 1.0f, 4.0f),
           "uniform group scale clamps one common factor to every minimum size");
    expect(near(ViewportGeometry::snapScaleFactor(1.26f, true, 0.25f), 1.25f) &&
               near(ViewportGeometry::snapScaleFactor(0.63f, true, 0.25f), 0.75f),
           "group scale factor snap uses increments around one");
    const Vector3 clampedAxis = ViewportGeometry::effectiveGroupScaleFactors(
        groupSizes, Vector3(0.1f, 0.8f, 1.0f), false, 0.25f, 0.5f);
    expect(clampedAxis.x >= 0.5f && clampedAxis.y >= 0.5f && clampedAxis.z >= 0.5f,
           "axis group scale clamps every axis against all selected sizes");
    const Vector3 groupPivot(1.0f, 1.0f, 1.0f);
    const Vector3 groupFactors(2.0f, 0.5f, 3.0f);
    const Vector3 groupPosition = ViewportGeometry::groupScalePosition(
        Vector3(9.0f, 4.0f, -2.0f), groupPivot, groupFactors);
    const Vector3 pivotPosition = ViewportGeometry::groupScalePosition(
        groupPivot, groupPivot, groupFactors);
    expect(positionDistance(groupPosition, Vector3(17.0f, 2.5f, -8.0f)) <= 0.001f &&
               positionDistance(pivotPosition, groupPivot) <= 0.001f,
           "group scale applies world-axis position factors around a fixed pivot");
    const Vector3 rotatedOrigin = ViewportGeometry::fixedFaceResizeOrigin(
        Vector3(3.0f, 0.0f, 0.0f), quarterTurn,
        Vector3(2.0f, 2.0f, 2.0f), Vector3(4.0f, 2.0f, 2.0f),
        Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
    expect(positionDistance(rotatedOrigin, Vector3(3.0f, 0.0f, -1.0f)) <= 0.001f,
           "individual resize keeps the grabbed face fixed after rotation");

    auto zoomBackend = std::make_unique<FrameRateTestInputBackend>();
    FrameRateTestInputBackend* zoomInput = zoomBackend.get();
    auto zoomUser = std::make_shared<User>(std::move(zoomBackend));
    zoomUser->controlMode = User::ControlMode::Free;
    const Vector3 zoomStart = zoomUser->cpos;
    zoomInput->scrollDelta = 2.0;
    zoomUser->processInput(nullptr, 1.0f / 60.0f, true, false, false, false);
    expect(positionDistance(zoomUser->cpos, zoomStart) <= 0.001f
               && near(static_cast<float>(zoomInput->scrollDelta), 0.0f),
           "focused but non-hovered viewport consumes scroll without zooming");
    zoomInput->scrollDelta = 2.0;
    zoomUser->processInput(nullptr, 1.0f / 60.0f, true, true, false, false);
    expect(positionDistance(zoomUser->cpos, zoomStart) > 0.5f,
           "hovered viewport applies mouse-wheel zoom");
    zoomInput->pressedKeys.insert(KeyCode::I);
    const Vector3 beforeKeyboardZoom = zoomUser->cpos;
    zoomUser->processInput(nullptr, 1.0f / 60.0f, true, false, false, false);
    expect(positionDistance(zoomUser->cpos, beforeKeyboardZoom) > 0.01f,
           "focused viewport keeps keyboard zoom when the mouse is outside");

    std::cout << "[ViewportHelperRegression] "
              << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runAssetPathRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        std::cout << "[AssetPath] " << (condition ? "PASS: " : "FAIL: ")
                  << message << '\n';
        if (!condition) ++failures;
    };

    expect(AssetPath::normalize("assets\\image\\sample.png") ==
               "assets/image/sample.png",
           "Windows separators normalize to portable separators");
    expect(AssetPath::normalize("assets/image/sample.png") ==
               "assets/image/sample.png",
           "portable separators remain unchanged");
    expect(AssetPath::normalize("").empty(),
           "empty asset path remains unset");
    expect(AssetPath::toStored(std::filesystem::path("assets") / "scripts" / "sample.luauc") ==
               "assets/scripts/sample.luauc",
           "stored filesystem paths always use portable separators");
    const std::string unicodePath = "assets/日本語/フォント.ttf";
    expect(AssetPath::toStored(AssetPath::fromStored(unicodePath)) == unicodePath,
           "UTF-8 asset paths round-trip without using the Windows ANSI code page");

    const auto originalCwd = std::filesystem::current_path();
    const auto tempRoot = std::filesystem::temp_directory_path() /
        ("recubin_asset_path_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    std::filesystem::create_directories(tempRoot / "assets", ec);
    if (ec) {
        expect(false, "temporary asset directory can be created");
        return 1;
    }

    std::filesystem::create_directories(tempRoot / "assets/fonts", ec);
    for (const char* fontName : { "DotGothic16-Regular.ttf", "fa-solid-900.ttf" }) {
        std::filesystem::copy_file(
            originalCwd / "assets/fonts" / fontName,
            tempRoot / "assets/fonts" / fontName,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) break;
    }
    expect(!ec, "runtime test fonts can be staged for packaging");

    {
        std::ofstream textFile(tempRoot / "assets" / "sample.txt", std::ios::binary);
        textFile << "portable-text";
        const std::array<char, 4> binary{{'R', '\0', 'C', 'B'}};
        std::ofstream binaryFile(tempRoot / "assets" / "sample.bin", std::ios::binary);
        binaryFile.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        std::filesystem::create_directories(tempRoot / "assets/anims");
        std::ofstream animationFile(tempRoot / "assets/anims" / "r6_walk.rcanim", std::ios::binary);
        animationFile << "recubin:\n  type: animation\n  version: 1\n"
                         "animation:\n  name: R6Walk\n  rig: R6\n  space: joint_delta\n"
                         "  length: 1\n  speed: 1\n  looped: true\n  tracks:\n"
                         "    - joint: LeftShoulder\n      keyframes:\n"
                         "        - time: 0\n          position: [0, 0, 0]\n"
                         "          rotation: [0, 0, 0, 1]\n          easing: linear\n";
#ifndef __APPLE__
        std::ofstream editorFile(tempRoot / "Recubin.exe", std::ios::binary);
        editorFile << "editor";
        std::ofstream runtimeFile(tempRoot / "RecubinEngine.exe", std::ios::binary);
        runtimeFile << "runtime";
#endif
        std::ofstream sceneFile(tempRoot / "scene.yaml", std::ios::binary);
        sceneFile << "recubin:\n"
                  << "  type: scene\n"
                  << "  version: 0\n"
                  << "Root:\n"
                  << "  Children:\n"
                  << "    - ClassName: Animation\n"
                  << "      Name: R6Walk\n"
                  << "      Properties:\n"
                  << "        ContentPath: assets/anims/r6_walk.rcanim\n"
                  << "Properties:\n"
                  << "  Texture: assets\\sample.bin\n"
                  << "  MeshFile: assets\\models\\missing.glb\n";
    }

    std::filesystem::current_path(tempRoot, ec);
    expect(!ec, "temporary asset directory can become the working directory");
    if (!ec) {
        AssetGuard::enableSandbox(tempRoot);
        expect(FileLoader::readText("assets/sample.txt") == "portable-text" &&
                   FileLoader::readText("assets\\sample.txt") == "portable-text",
               "text loader accepts both separator styles");
        const auto forwardBytes = FileLoader::readBinary("assets/sample.bin");
        const auto windowsBytes = FileLoader::readBinary("assets\\sample.bin");
        expect(forwardBytes == windowsBytes && forwardBytes.size() == 4 &&
                   forwardBytes[0] == 'R' && forwardBytes[1] == '\0' &&
                   forwardBytes[2] == 'C' && forwardBytes[3] == 'B',
               "binary loader accepts both separator styles");
        expect(AssetGuard::allow("assets\\sample.txt"),
               "guard accepts an in-root Windows-style path");
        expect(!AssetGuard::allow("..\\outside.txt"),
               "guard rejects Windows-style parent traversal");

        Packager::Config packageConfig;
        packageConfig.gameName = "PortablePackage";
        packageConfig.applicationId = RecubinUUID::generate();
        packageConfig.outputDir = "package-output";
        packageConfig.scenePath = "scene.yaml";
        packageConfig.engineExePath =
#ifdef __APPLE__
            (originalCwd / "build-mac/Recubin").string();
#else
            "Recubin.exe";
#endif
        const bool packaged = Packager::package(packageConfig, [](const std::string& message) {
            std::cout << "[Packager] " << message << '\n';
        });
#ifdef __APPLE__
        const std::filesystem::path packageRoot = tempRoot / "package-output" / "PortablePackage.app";
#else
        const std::filesystem::path packageRoot = tempRoot / "package-output" / "PortablePackage";
#endif
#ifdef __APPLE__
        const std::filesystem::path packageContentRoot = packageRoot / "Contents/Resources";
#else
        const std::filesystem::path packageContentRoot = packageRoot;
#endif
        const std::string packagedScene = FileLoader::readText(
            std::filesystem::relative(packageContentRoot / "assets/scenes/PortablePackage.yaml",
                                       tempRoot).generic_string());
        const std::string packagedStartup = FileLoader::readText(
            std::filesystem::relative(packageContentRoot / "startup.yaml",
                                       tempRoot).generic_string());
        expect(packaged && std::filesystem::exists(packageContentRoot / "assets/sample.bin") &&
                   std::filesystem::exists(packageContentRoot / "assets/fonts/DotGothic16-Regular.ttf") &&
                   std::filesystem::exists(packageContentRoot / "assets/fonts/fa-solid-900.ttf") &&
                   packagedScene.find("assets/sample.bin") != std::string::npos &&
                   packagedScene.find("assets/models/missing.glb") != std::string::npos &&
                   std::filesystem::exists(packageContentRoot / "assets/anims/r6_walk.rcanim") &&
                   packagedScene.find("assets/anims/r6_walk.rcanim") != std::string::npos &&
                   packagedScene.find(packageConfig.applicationId) != std::string::npos &&
                   packagedStartup.find(packageConfig.applicationId) != std::string::npos &&
                   packagedScene.find('\\') == std::string::npos,
               "packager copies referenced assets, runtime fonts, and portable YAML paths");
#ifdef __APPLE__
        const std::string plist = FileLoader::readText(
            std::filesystem::relative(packageRoot / "Contents/Info.plist", tempRoot).generic_string());
        expect(std::filesystem::exists(packageRoot / "Contents/MacOS/RecubinEngine") &&
                   std::filesystem::exists(packageRoot / "Contents/Resources/startup.yaml") &&
                   plist.find("CFBundleExecutable") != std::string::npos &&
                   plist.find("RecubinEngine") != std::string::npos &&
                   plist.find("CFBundlePackageType") != std::string::npos &&
                   plist.find("APPL") != std::string::npos,
               "macOS packager writes an App Bundle with runtime and Info.plist");
        expect(std::system(("/usr/bin/codesign --verify --deep --strict " +
                            packageRoot.string()).c_str()) == 0,
               "macOS packager ad-hoc signs the finished App Bundle");

        auto readBinaryFile = [](const std::filesystem::path& path) {
            std::vector<unsigned char> bytes;
            std::ifstream file(path, std::ios::binary);
            if (!file) return bytes;
            file.seekg(0, std::ios::end);
            const auto length = file.tellg();
            if (length <= 0) return bytes;
            bytes.resize(static_cast<std::size_t>(length));
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file) bytes.clear();
            return bytes;
        };
        auto isValidIcns = [](const std::vector<unsigned char>& bytes) {
            if (bytes.size() < 8 || std::string(bytes.begin(), bytes.begin() + 4) != "icns") return false;
            const auto readBigEndian32 = [&bytes](std::size_t offset) -> std::uint32_t {
                return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                       (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                       (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
                       static_cast<std::uint32_t>(bytes[offset + 3]);
            };
            if (readBigEndian32(4) != bytes.size()) return false;
            constexpr unsigned char pngSignature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
            std::size_t offset = 8;
            int pngEntries = 0;
            while (offset + 8 <= bytes.size()) {
                const std::uint32_t entrySize = readBigEndian32(offset + 4);
                if (entrySize < 8 || offset + entrySize > bytes.size()) return false;
                for (std::size_t i = offset + 8; i + sizeof(pngSignature) <= offset + entrySize; ++i) {
                    if (std::equal(std::begin(pngSignature), std::end(pngSignature), bytes.begin() + i)) {
                        ++pngEntries;
                        break;
                    }
                }
                offset += entrySize;
            }
            return offset == bytes.size() && pngEntries > 0;
        };

        const auto packageIconScene = [&](const std::string& packageName,
                                          const std::filesystem::path& iconSource) {
            const auto scenePath = tempRoot / (packageName + "-scene.yaml");
            std::ofstream iconScene(scenePath, std::ios::binary);
            iconScene << "Root:\n"
                      << "  Children:\n"
                      << "    - ClassName: AppImage\n"
                      << "      Properties:\n"
                      << "        IconPath: " << iconSource.generic_string() << "\n";
            iconScene.close();

            Packager::Config iconConfig = packageConfig;
            iconConfig.gameName = packageName;
            iconConfig.scenePath = scenePath.filename().generic_string();
            std::vector<std::string> logs;
            const bool result = Packager::package(iconConfig, [&logs](const std::string& message) {
                logs.push_back(message);
            });
            const auto bundle = tempRoot / "package-output" / (packageName + ".app");
            const auto iconBytes = readBinaryFile(bundle / "Contents/Resources/AppIcon.icns");
            const std::string iconPlist = FileLoader::readText(
                std::filesystem::relative(bundle / "Contents/Info.plist", tempRoot).generic_string());
            return result && isValidIcns(iconBytes) &&
                   iconPlist.find("CFBundleIconFile") != std::string::npos &&
                   iconPlist.find("AppIcon.icns") != std::string::npos;
        };

        expect(packageIconScene("PortablePngIconPackage", originalCwd / "assets/image/the-cat.png"),
               "macOS packager creates a valid ICNS from PNG AppImage");
        expect(packageIconScene("PortableJpegIconPackage", originalCwd / "assets/image/salad-cat.jpg"),
               "macOS packager creates a valid ICNS from JPEG AppImage");

        const auto invalidScenePath = tempRoot / "invalid-icon-scene.yaml";
        std::ofstream invalidScene(invalidScenePath, std::ios::binary);
        invalidScene << "Root:\n"
                     << "  Children:\n"
                     << "    - ClassName: AppImage\n"
                     << "      Properties:\n"
                     << "        IconPath: " << (tempRoot / "missing-icon.png").generic_string() << "\n";
        invalidScene.close();
        Packager::Config invalidIconConfig = packageConfig;
        invalidIconConfig.gameName = "InvalidIconPackage";
        invalidIconConfig.scenePath = invalidScenePath.filename().generic_string();
        std::vector<std::string> invalidIconLogs;
        const bool invalidIconPackaged = Packager::package(
            invalidIconConfig, [&invalidIconLogs](const std::string& message) {
                invalidIconLogs.push_back(message);
            });
        const bool invalidIconLogged = std::any_of(
            invalidIconLogs.begin(), invalidIconLogs.end(), [](const std::string& message) {
                return message.find("[ERROR]") != std::string::npos;
            });
        expect(!invalidIconPackaged && invalidIconLogged,
               "macOS packager rejects a missing AppImage icon");

        const auto corruptIconPath = tempRoot / "corrupt-icon.png";
        std::ofstream corruptIcon(corruptIconPath, std::ios::binary);
        corruptIcon << "this is not an image";
        corruptIcon.close();
        const auto corruptScenePath = tempRoot / "corrupt-icon-scene.yaml";
        std::ofstream corruptScene(corruptScenePath, std::ios::binary);
        corruptScene << "Root:\n"
                     << "  Children:\n"
                     << "    - ClassName: AppImage\n"
                     << "      Properties:\n"
                     << "        IconPath: " << corruptIconPath.generic_string() << "\n";
        corruptScene.close();
        Packager::Config corruptIconConfig = packageConfig;
        corruptIconConfig.gameName = "CorruptIconPackage";
        corruptIconConfig.scenePath = corruptScenePath.filename().generic_string();
        std::vector<std::string> corruptIconLogs;
        const bool corruptIconPackaged = Packager::package(
            corruptIconConfig, [&corruptIconLogs](const std::string& message) {
                corruptIconLogs.push_back(message);
            });
        const bool corruptIconLogged = std::any_of(
            corruptIconLogs.begin(), corruptIconLogs.end(), [](const std::string& message) {
                return message.find("Cannot decode AppImage icon") != std::string::npos;
            });
        expect(!corruptIconPackaged && corruptIconLogged,
               "macOS packager rejects a corrupt AppImage icon");

#endif
    }

    std::filesystem::current_path(originalCwd, ec);
    expect(!ec, "original working directory is restored");
    std::filesystem::remove_all(tempRoot, ec);

    std::cout << "[AssetPath] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runAppImageRegression() {
    int failures = 0;
    auto expect = [&failures](bool condition, const char* name) {
        std::cout << "[AppImage] " << (condition ? "PASS" : "FAIL")
                  << ": " << name << '\n';
        if (!condition) ++failures;
    };

    MockPlatform mockPlatform;
    expect(mockPlatform.setApplicationIcon("") == ApplicationIconResult::Unsupported &&
               mockPlatform.setApplicationIcon("assets/image/hooo.png") ==
                   ApplicationIconResult::Unsupported,
           "unsupported platforms leave application icons to the GLFW fallback");

#ifdef __APPLE__
    MacPlatform macPlatform;
    const std::filesystem::path sourceRoot =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::string validIcon =
        AssetPath::normalize((sourceRoot / "assets/image/hooo.png").string());
    const std::string missingIcon =
        AssetPath::normalize((sourceRoot / "assets/image/app-image-missing.png").string());

    expect(macPlatform.setApplicationIcon("") == ApplicationIconResult::Applied,
           "an empty macOS icon path restores the default application icon");
    expect(macPlatform.setApplicationIcon(validIcon) == ApplicationIconResult::Applied,
           "macOS loads a valid image as the application icon");
    expect(macPlatform.setApplicationIcon(missingIcon) == ApplicationIconResult::Failed,
           "macOS reports a missing application icon");
    expect(macPlatform.setApplicationIcon("") == ApplicationIconResult::Applied,
           "macOS restores the default icon after the regression test");
#endif

    std::cout << "[AppImage] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

int runRuntimeLaunchArgsRegression() {
    int failures = 0;
    auto expect = [&failures](bool condition, const char* name) {
        std::cout << "[RuntimeLaunchArgs] " << (condition ? "PASS" : "FAIL")
                  << ": " << name << '\n';
        if (!condition) ++failures;
    };
    auto parse = [](std::vector<std::string> values) {
        std::vector<char*> arguments;
        arguments.reserve(values.size());
        for (std::string& value : values) arguments.push_back(value.data());
        return parseRuntimeLaunchArgs(
            static_cast<int>(arguments.size()), arguments.data());
    };

    const RuntimeLaunchArgs valid = parse({
        "RecubinEngine", "--direct-connect", "127.0.0.1:41000",
        "--scene", "assets/scenes/a snapshot.yaml", "--listen-port", "0",
        "--window-title", "Client 1",
    });
    expect(valid.valid && valid.scenePath == "assets/scenes/a snapshot.yaml" &&
               valid.windowTitle == "Client 1",
           "scene and title override while unrelated network arguments are ignored");
    const RuntimeLaunchArgs editorTest = parse({
        "RecubinEngine", "--scene", "assets/scenes/_snapshot.yaml",
        "--window-title", "Client 1", "--editor-test",
    });
    expect(editorTest.valid && editorTest.editorTest,
           "editor test flag is accepted without a value");
    expect(!parse({"RecubinEngine", "--editor-test", "--editor-test"}).valid,
           "duplicate editor test flag is rejected");
    expect(!parse({"RecubinEngine", "--editor-test=true"}).valid,
           "equals-form editor test flag is rejected");
    expect(!parse({"RecubinEngine", "--scene"}).valid,
           "missing scene value is rejected");
    expect(!parse({"RecubinEngine", "--window-title", "--listen-port", "0"}).valid,
           "missing window title value is rejected");
    expect(!parse({"RecubinEngine", "--scene", "a.yaml", "--scene", "b.yaml"}).valid,
           "duplicate scene override is rejected");
    expect(!parse({"RecubinEngine", "--scene=a.yaml"}).valid &&
               !parse({"RecubinEngine", "--window-title=Client"}).valid,
           "equals-form runtime options are rejected");
    const RuntimeLaunchArgs automationPair = parse({
        "RecubinEngine", "--ui-automation", "--ui-automation-scene", "fixture.yaml",
        "--ui-automation-settings", "settings.yaml"});
    expect(automationPair.valid && automationPair.uiAutomationScene == "fixture.yaml" &&
               automationPair.uiAutomationSettings == "settings.yaml",
           "GUI automation scene/settings pair is parsed");
    const RuntimeLaunchArgs missingAutomationSettings = parse({
        "RecubinEngine", "--ui-automation-scene", "fixture.yaml"});
    expect(missingAutomationSettings.valid && missingAutomationSettings.uiAutomationScene,
           "launch parser accepts individual values before main enforces the required pair");
    expect(!parse({"RecubinEngine", "--ui-automation-scene"}).valid &&
               !parse({"RecubinEngine", "--ui-automation-settings"}).valid,
           "missing GUI automation pair values are rejected");

    MockPlatform mockPlatform;
    ChildProcessLaunchOptions options;
    options.executable = "RecubinEngine";
    expect(!mockPlatform.launchChildProcess(options),
           "mock platform reports child process launch as unsupported");

    std::cout << "[RuntimeLaunchArgs] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

static int runSurfaceMarkRegression() {
    int failures = 0;
    const auto expect = [&](bool condition, const char* description) {
        if (!condition) { ++failures; std::cerr << "[FAIL] " << description << '\n'; }
        else std::cout << "[PASS] " << description << '\n';
    };
    SurfaceMark mark;
    expect(mark.IsA("SurfaceMark") && mark.IsA("Spatial") && !mark.IsA("BaseCube"),
           "SurfaceMark has the independent Spatial inheritance");
    expect(mark.Position == Vector3(0, 0, 0) && mark.Size == Vector3(4, 4, 4) &&
           mark.getForward() == Vector3(0, 0, -1), "SurfaceMark defaults are stable");
    mark.Rotation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f);
    expect(mark.getForward().x < -0.99f && std::fabs(mark.getForward().z) < 0.01f,
           "SurfaceMark forward follows rotation");
    auto parent = std::make_shared<Model>(Vector3(10, 0, 0), Vector3(1, 1, 1));
    auto child = std::make_shared<SurfaceMark>();
    child->Position = Vector3(2, 0, 0);
    parent->addChild(child);
    expect(std::fabs(child->getWorldPosition().x - 12.0f) < 0.001f,
           "SurfaceMark follows parent Spatial transform");
    expect(child->intersectsSphere(Vector3(12, 0, 0), 0.1f) &&
           child->intersectsSphere(Vector3(12, 0, -4), 0.1f) &&
           child->intersectsSphere(Vector3(12, 0, -2), 0.1f) &&
           !child->intersectsSphere(Vector3(12, 0, 0.1f), 0.01f) &&
           !child->intersectsSphere(Vector3(100, 0, 0), 0.1f),
           "SurfaceMark projection volume culls spheres");
    child->Rotation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f);
    expect(child->intersectsSphere(Vector3(8, 0, 2), 0.1f),
           "SurfaceMark projection volume follows rotation");
    auto filterWorkspace = std::make_shared<Workspace>();
    auto exactCube = std::make_shared<Cube>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0); exactCube->Name = "Exact";
    auto modelFilter = std::make_shared<Model>(); modelFilter->Name = "ModelFilter";
    auto modelCube = std::make_shared<Cube>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0); modelCube->Name = "ModelCube";
    auto folderFilter = std::make_shared<Folder>(); folderFilter->Name = "FolderFilter";
    auto folderCube = std::make_shared<Cube>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0); folderCube->Name = "FolderCube";
    auto unrelatedCube = std::make_shared<Cube>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0); unrelatedCube->Name = "Unrelated";
    auto filterMark = std::make_shared<SurfaceMark>(); filterMark->Name = "FilterMark";
    modelFilter->addChild(modelCube); filterWorkspace->addChild(exactCube);
    folderFilter->addChild(folderCube);
    filterWorkspace->addChild(modelFilter); filterWorkspace->addChild(unrelatedCube);
    filterWorkspace->addChild(folderFilter);
    filterWorkspace->addChild(filterMark);
    expect(filterMark->allowsSurfaceTarget(*exactCube) && filterMark->allowsSurfaceTarget(*unrelatedCube),
           "SurfaceMark Exclude empty filter allows all targets");
    filterMark->FilterMode = SurfaceMarkFilterMode::Include;
    expect(!filterMark->allowsSurfaceTarget(*exactCube), "SurfaceMark Include empty filter rejects all targets");
    filterMark->setFilterInstances({exactCube, exactCube, modelFilter});
    expect(filterMark->getFilterInstances().size() == 2 && filterMark->allowsSurfaceTarget(*exactCube) &&
           filterMark->allowsSurfaceTarget(*modelCube) && !filterMark->allowsSurfaceTarget(*unrelatedCube),
           "SurfaceMark filter matches exact and Model descendants with deduplication");
    filterMark->setFilterInstances({folderFilter});
    expect(filterMark->allowsSurfaceTarget(*folderCube) && !filterMark->allowsSurfaceTarget(*unrelatedCube),
           "SurfaceMark filter matches Folder descendants");
    filterMark->FilterMode = SurfaceMarkFilterMode::Exclude;
    filterMark->setFilterInstances({exactCube, modelFilter});
    expect(!filterMark->allowsSurfaceTarget(*exactCube) && !filterMark->allowsSurfaceTarget(*modelCube) &&
           filterMark->allowsSurfaceTarget(*unrelatedCube),
           "SurfaceMark Exclude filter rejects exact and descendant targets");
    {
        auto expiring = std::make_shared<Cube>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0);
        filterMark->setFilterInstances({expiring});
        expiring.reset();
        expect(filterMark->allowsSurfaceTarget(*unrelatedCube), "Expired SurfaceMark filter references are ignored");
    }
    filterMark->FilterMode = SurfaceMarkFilterMode::Include;
    filterMark->setFilterState({modelFilter, nullptr}, {"ModelFilter", "Missing\\Target"});
    auto clones = Instance::cloneForest({modelFilter, filterMark});
    auto clonedModelFilter = clones.size() > 0 ? clones[0] : nullptr;
    auto clonedFilterMark = clones.size() > 1 ? std::dynamic_pointer_cast<SurfaceMark>(clones[1]) : nullptr;
    bool cloneTargetRemapped = clonedFilterMark && clonedModelFilter && clonedFilterMark->getFilterInstances().size() == 2 &&
        clonedFilterMark->getFilterInstances()[0].lock().get() == clonedModelFilter.get() &&
        clonedFilterMark->getFilterInstances()[1].expired() && clonedFilterMark->getFilterPaths().size() == 2;
    expect(cloneTargetRemapped, "SurfaceMark cloneForest remaps live filters and preserves unresolved paths");
    auto serializedFilterMark = std::make_shared<SurfaceMark>(); serializedFilterMark->Name = "SerializedMark";
    serializedFilterMark->FilterMode = SurfaceMarkFilterMode::Include;
    serializedFilterMark->setFilterInstances({modelFilter});
    filterWorkspace->addChild(serializedFilterMark);
    auto workspaceFilterMark = std::make_shared<SurfaceMark>(); workspaceFilterMark->Name = "WorkspaceFilterMark";
    workspaceFilterMark->FilterMode = SurfaceMarkFilterMode::Include;
    workspaceFilterMark->setFilterInstances({filterWorkspace});
    filterWorkspace->addChild(workspaceFilterMark);
    const auto filterYaml = std::filesystem::temp_directory_path() / "recubin_surface_mark_filter.yaml";
    auto filterRoot = std::make_shared<System>(); filterRoot->addChild(filterWorkspace);
    expect(SceneLoader::saveSceneResult(filterRoot.get(), filterYaml.string()), "SurfaceMark filter YAML save succeeds");
    auto filterLoadedRoot = SceneLoader::loadScene(filterYaml.string());
    std::error_code filterRemoveError;
    auto loadedFilterMark = filterLoadedRoot ? dynamic_cast<SurfaceMark*>(filterLoadedRoot->getChildByPath("Workspace\\SerializedMark")) : nullptr;
    expect(loadedFilterMark && loadedFilterMark->FilterMode == SurfaceMarkFilterMode::Include &&
           loadedFilterMark->getFilterPaths().size() == 1 && loadedFilterMark->getFilterInstances().size() == 1,
           "SurfaceMark filter YAML round-trip resolves paths");
    auto loadedWorkspaceFilterMark = filterLoadedRoot ? dynamic_cast<SurfaceMark*>(filterLoadedRoot->getChildByPath("Workspace\\WorkspaceFilterMark")) : nullptr;
    auto loadedExactCube = filterLoadedRoot ? filterLoadedRoot->getChildByPath("Workspace\\Exact") : nullptr;
    auto loadedExactBaseCube = loadedExactCube ? dynamic_cast<BaseCube*>(loadedExactCube) : nullptr;
    expect(loadedWorkspaceFilterMark && loadedWorkspaceFilterMark->getFilterPaths().size() == 1 &&
           loadedWorkspaceFilterMark->getFilterInstances().size() == 1 &&
           !loadedWorkspaceFilterMark->getFilterInstances()[0].expired() && loadedExactBaseCube &&
           loadedWorkspaceFilterMark->allowsSurfaceTarget(*loadedExactBaseCube),
           "SurfaceMark Workspace filter resolves and matches descendants after YAML round-trip");
    if (loadedFilterMark) {
        auto loadedModel = filterLoadedRoot->getChildByPath("Workspace\\ModelFilter");
        if (loadedModel) loadedModel->renameTo("RenamedModel");
        loadedFilterMark->refreshFilterPaths();
        expect(!loadedFilterMark->getFilterPaths().empty() && loadedFilterMark->getFilterPaths()[0].find("RenamedModel") != std::string::npos,
               "SurfaceMark filter path refresh follows rename");
        const auto renamedFilterYaml = std::filesystem::temp_directory_path() / "recubin_surface_mark_filter_renamed.yaml";
        expect(SceneLoader::saveSceneResult(filterLoadedRoot.get(), renamedFilterYaml.string()),
               "SurfaceMark filter rename YAML save succeeds");
        auto renamedRoot = SceneLoader::loadScene(renamedFilterYaml.string());
        auto renamedMark = renamedRoot ? dynamic_cast<SurfaceMark*>(renamedRoot->getChildByPath("Workspace\\SerializedMark")) : nullptr;
        expect(renamedMark && renamedMark->getFilterInstances().size() == 1 &&
               !renamedMark->getFilterInstances()[0].expired() && renamedMark->getFilterPaths()[0].find("RenamedModel") != std::string::npos,
               "SurfaceMark renamed filter path resolves after save and reload");
        std::filesystem::remove(renamedFilterYaml, filterRemoveError);
        loadedFilterMark->setFilterPaths({"Missing\\Target"});
        loadedFilterMark->resolveFilterInstances(filterLoadedRoot.get());
        expect(loadedFilterMark->getFilterPaths().size() == 1 && loadedFilterMark->getFilterInstances().size() == 1 &&
               loadedFilterMark->getFilterInstances()[0].expired(), "Unresolved SurfaceMark filter paths are retained");
    }
    std::filesystem::remove(filterYaml, filterRemoveError);
    child->Color = Color4(0.2f, 0.3f, 0.4f, 0.5f);
    child->setTexturePath("assets/mark.png");
    auto copy = std::dynamic_pointer_cast<SurfaceMark>(child->clone());
    expect(copy && copy->Position == child->Position && copy->Size == child->Size &&
           copy->Color == child->Color && copy->texturePath == child->texturePath,
           "SurfaceMark clone preserves authoring properties");
    const auto& schema = PropertyRegistry::schemaFor("SurfaceMark");
    const bool hasColor = std::any_of(schema.begin(), schema.end(), [](const auto& p) { return p.name == "Color"; });
    const bool hasTexture = std::any_of(schema.begin(), schema.end(), [](const auto& p) { return p.name == "TexturePath"; });
    expect(hasColor && hasTexture, "SurfaceMark property schema exposes Color and TexturePath");
    expect(SceneLoader::createInstance("SurfaceMark") != nullptr,
           "SceneLoader creates SurfaceMark instances");
    const auto tempPath = std::filesystem::temp_directory_path() / "recubin_surface_mark_regression.yaml";
    auto root = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    auto saved = std::make_shared<SurfaceMark>();
    saved->Name = "Paint";
    saved->Position = Vector3(3, 4, 5);
    saved->Size = Vector3(2, 3, 6);
    saved->Rotation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), 30.0f);
    saved->Color = Color4(0.1f, 0.2f, 0.3f, 0.4f);
    saved->setTexturePath("assets/paint.png");
    workspace->addChild(saved); root->addChild(workspace);
    expect(SceneLoader::saveSceneResult(root.get(), tempPath.string()), "SurfaceMark YAML save succeeds");
    auto loadedRoot = SceneLoader::loadScene(tempPath.string());
    auto loaded = loadedRoot ? dynamic_cast<SurfaceMark*>(loadedRoot->getChildByPath("Workspace\\Paint")) : nullptr;
    expect(loaded && loaded->Position == saved->Position && loaded->Size == saved->Size &&
           loaded->Color == saved->Color && loaded->texturePath == saved->texturePath,
           "SurfaceMark YAML round-trip preserves transform, color and texture");
    expect(loaded && std::fabs(loaded->Rotation.w - saved->Rotation.w) < 1e-4f &&
           std::fabs(loaded->Rotation.x - saved->Rotation.x) < 1e-4f &&
           std::fabs(loaded->Rotation.y - saved->Rotation.y) < 1e-4f &&
           std::fabs(loaded->Rotation.z - saved->Rotation.z) < 1e-4f,
           "SurfaceMark YAML round-trip preserves rotation");
    std::error_code removeError; std::filesystem::remove(tempPath, removeError);

    {
        LuauEngine engine;
        auto luauSystem = std::make_shared<System>();
        auto luauWorkspace = std::make_shared<Workspace>();
        auto file = std::make_shared<FileRef>();
        file->Path = "assets/paint.png";
        luauSystem->addChild(luauWorkspace);
        luauWorkspace->addChild(file);
        engine.setWorkspace(luauWorkspace);
        engine.setSystem(luauSystem.get());
        bool luauMarker = false;
        const auto oldLogHook = g_luauLogHook;
        g_luauLogHook = [&](const std::string& message) {
            if (message.find("[SurfaceMarkLuau]") != std::string::npos) luauMarker = true;
        };
        auto script = std::make_shared<Script>();
        script->Source =
            "local mark = Instance.new('SurfaceMark') "
            "local f = workspace:WaitChild('FileRef') "
            "mark.Color = Color4.new(0.2, 0.4, 0.6, 0.8) "
            "mark.Source = f "
            "local expectedPath = mark.TexturePath "
            "local expectedId = mark.TextureID "
            "mark.TexturePath = 'bad.png' "
            "mark.TextureID = 4 "
            "mark.FilterMode = 'Include' "
            "mark.FilterInstances = { f } "
            "local filterSnapshot = mark.FilterInstances "
            "local filterOk = pcall(function() mark.FilterInstances = { 123 } end) "
            "local filterAtomic = #mark.FilterInstances == 1 and mark.FilterInstances[1].Name == f.Name "
            "mark.FilterInstances = {} "
            "if mark:IsA('Spatial') and not mark:IsA('BaseCube') and "
            "math.abs(mark.Color.r - 0.2) < 0.00001 and "
            "math.abs(mark.Color.g - 0.4) < 0.00001 and math.abs(mark.Color.b - 0.6) < 0.00001 and "
            "math.abs(mark.Color.a - 0.8) < 0.00001 and mark.TexturePath == expectedPath and "
            "mark.TexturePath == 'assets/paint.png' and mark.TextureID == expectedId and not filterOk and filterAtomic and "
            "#mark.FilterInstances == 0 and "
            "type(mark.TextureID) == 'number' then print('[SurfaceMarkLuau]') end";
        expect(engine.execute(*script), "SurfaceMark Luau factory script executes");
        g_luauLogHook = oldLogHook;
        expect(luauMarker, "SurfaceMark Luau exposes Color, Source, TexturePath and TextureID");
    }
    return failures == 0 ? 0 : 1;
}

static int runAnimationClipRegression() {
    int failures = 0;
    const auto expect = [&](bool condition, const char* description) {
        if (!condition) {
            ++failures;
            std::cerr << "[FAIL] " << description << '\n';
        } else {
            std::cout << "[PASS] " << description << '\n';
        }
    };
    const auto near = [](float a, float b, float epsilon = 1e-4f) {
        return std::fabs(a - b) <= epsilon;
    };

    const auto tempRoot = std::filesystem::temp_directory_path() /
        ("recubin_animation_clip_regression_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tempRoot);
    const auto clipPath = tempRoot / "walk.rcanim";
    const auto invalidPath = tempRoot / "invalid.rcanim";
    const auto wrongTypePath = tempRoot / "scene.rcanim";
    const auto newerPath = tempRoot / "newer.rcanim";
    const auto legacyPath = tempRoot / "legacy.yaml";
    const auto scenePath = tempRoot / "scene.yaml";
    const auto sceneRoundTripPath = tempRoot / "scene_roundtrip.yaml";

    const AnimationClip builtin = AnimationClip::defaultR6Walk();
    expect(builtin.name == "R6Walk" && builtin.rig == "R6" && builtin.space == "joint_delta",
           "built-in walk has the R6 joint-delta identity");
    expect(near(builtin.length, 2.0f / 3.0f) && builtin.looped,
           "built-in walk has the expected cycle length and looping");
    const auto* shoulder = builtin.findTrack("LeftShoulder");
    expect(shoulder && shoulder->keyframes.size() == 5,
           "built-in walk contains five shoulder keys");
    if (shoulder && shoulder->keyframes.size() == 5) {
        expect(near(shoulder->keyframes[1].delta.Rotation.w,
                    CFrame::fromAxisAngle(Vector3(1, 0, 0), 35.0f).Rotation.w),
               "walk shoulder key reaches the expected 35 degree pose");
        const auto midpoint = builtin.evaluate(*shoulder, builtin.length * 0.5f);
        expect(near(midpoint.Position.x, 0.0f) && near(midpoint.Position.y, 0.0f) &&
               near(midpoint.Position.z, 0.0f), "walk midpoint preserves joint translation");
        const CFrame root(Vector3(10.0f, 2.0f, -4.0f),
                          Quaternion::fromAxisAngle(Vector3(0, 1, 0), 0.5f));
        const CFrame delta(Vector3(1.0f, 0.0f, 0.0f), Quaternion());
        const CFrame world = root * delta;
        expect(near(world.Position.x, root.pointToWorld(delta.Position).x) &&
               near(world.Position.y, 2.0f) && near(world.Position.z, root.pointToWorld(delta.Position).z),
               "joint delta composes after the rig root transform");
        if (const auto* binding = CharacterRig::findR6Joint("LeftShoulder")) {
            const CFrame applied = CharacterRig::applyR6Joint(root, *binding, delta);
            const CFrame expected = root * binding->rootToJoint * delta * binding->jointToPartBind;
            expect(near(applied.Position.x, expected.Position.x) &&
                   near(applied.Position.y, expected.Position.y) &&
                   near(applied.Position.z, expected.Position.z) &&
                   near(applied.Rotation.x, expected.Rotation.x) &&
                   near(applied.Rotation.y, expected.Rotation.y) &&
                   near(applied.Rotation.z, expected.Rotation.z) &&
                   near(applied.Rotation.w, expected.Rotation.w),
                   "R6 joint binding composes translated and rotated roots correctly");
        } else {
            expect(false, "R6 shoulder binding is available");
        }
    }

    {
        AnimationClip eased = builtin;
        eased.tracks.clear();
        eased.addKey("LeftShoulder", 0.0f, CFrame(), EasingType::Sine);
        eased.addKey("LeftShoulder", eased.length, CFrame::fromAxisAngle(Vector3(1, 0, 0), 10.0f), EasingType::Exponential);
        const auto easedPath = tempRoot / "eased.rcanim";
        expect(AnimationClipIO::save(easedPath.string(), eased), "eased clip saves");
        const auto loaded = AnimationClipIO::load(easedPath.string());
        expect(loaded && loaded.clip.tracks.front().keyframes.front().easing == EasingType::Sine &&
               loaded.clip.tracks.front().keyframes.back().easing == EasingType::Exponential,
               "rcanim round-trip preserves easing modes");
        const auto zeroQuaternionPath = tempRoot / "zero_quaternion.rcanim";
        std::ofstream out(zeroQuaternionPath);
        out << "recubin:\n  type: animation\n  version: 1\nanimation:\n"
               "  name: bad\n  rig: R6\n  space: joint_delta\n  length: 1\n"
               "  speed: 1\n  looped: false\n  tracks:\n    - joint: LeftShoulder\n"
               "      keyframes:\n        - time: 0\n          position: [0,0,0]\n"
               "          rotation: [0,0,0,0]\n";
        out.close();
        const auto zeroQuaternion = AnimationClipIO::load(zeroQuaternionPath.string());
        expect(zeroQuaternion.status == AnimationClipLoadStatus::InvalidData &&
                   zeroQuaternion.message == "invalid quaternion",
               "zero quaternion is rejected for the quaternion reason");
        const auto duplicatePath = tempRoot / "duplicate_joint.rcanim";
        out.open(duplicatePath);
        out << "recubin:\n  type: animation\n  version: 1\nanimation:\n"
               "  name: bad\n  rig: R6\n  space: joint_delta\n  length: 1\n"
               "  speed: 1\n  looped: false\n  tracks:\n"
               "    - joint: LeftShoulder\n      keyframes:\n        - time: 0\n          position: [0,0,0]\n          rotation: [0,0,0,1]\n"
               "    - joint: LeftShoulder\n      keyframes:\n        - time: 1\n          position: [0,0,0]\n          rotation: [0,0,0,1]\n";
        out.close();
        const auto duplicateJoint = AnimationClipIO::load(duplicatePath.string());
        expect(duplicateJoint.status == AnimationClipLoadStatus::InvalidData &&
                   duplicateJoint.message == "duplicate joint or empty keyframes",
               "duplicate known-joint tracks are rejected for the duplicate reason");
    }

    expect(AnimationClipIO::save(clipPath.string(), builtin), "animation clip saves as rcanim");
    const auto roundTrip = AnimationClipIO::load(clipPath.string());
    expect(roundTrip && roundTrip.clip.tracks.size() == builtin.tracks.size(),
           "saved rcanim loads successfully");
    const auto* loadedShoulder = roundTrip.clip.findTrack("LeftShoulder");
    expect(loadedShoulder && shoulder && loadedShoulder->keyframes.size() == shoulder->keyframes.size(),
           "rcanim round-trip preserves joint key count");

    {
        std::ofstream out(invalidPath); out << "recubin: [broken";
        out.close();
        expect(AnimationClipIO::load(invalidPath.string()).status == AnimationClipLoadStatus::InvalidYaml,
               "malformed rcanim is rejected");
        out.open(wrongTypePath); out << "recubin:\n  type: scene\n  version: 1\n";
        out.close();
        expect(AnimationClipIO::load(wrongTypePath.string()).status == AnimationClipLoadStatus::TypeMismatch,
               "wrong document type is rejected");
        out.open(newerPath); out << "recubin:\n  type: animation\n  version: 99\n";
        out.close();
        expect(AnimationClipIO::load(newerPath.string()).status == AnimationClipLoadStatus::UnsupportedVersion,
               "newer animation version is rejected");
    }

    {
        std::ofstream out(legacyPath);
        out << "Animation:\n  Length: 1\n  Speed: 1\n  Looped: true\n"
               "  Tracks:\n    - PartName: Arm\n      Keyframes:\n"
               "        - Time: 0\n          Position: [0, 0, 0]\n"
               "          Rotation: [0, 0, 0, 1]\n          Easing: 0\n";
        out.close();
        Animation legacy;
        expect(legacy.importFromFile(legacyPath.string()) && legacy.getTracks().size() == 1,
               "legacy Animation YAML remains importable");
    }

    {
        auto legacySceneRoot = std::make_shared<Workspace>();
        auto legacyAnimation = std::make_shared<Animation>();
        legacyAnimation->Name = "LegacyTracksAnimation";
        legacyAnimation->Length = 1.0f;
        legacyAnimation->addOrReplaceKey("LeftArm", 0.0f, CFrame(), EasingType::Linear);
        auto followingCube = std::make_shared<Cube>(Vector3(3.0f, 2.0f, 1.0f), Vector3(1.0f, 1.0f, 1.0f), 0);
        followingCube->Name = "FollowingChild";
        legacySceneRoot->addChild(legacyAnimation);
        legacySceneRoot->addChild(followingCube);
        const auto legacyScenePath = tempRoot / "legacy_animation_scene.yaml";
        SceneLoader::saveScene(legacySceneRoot.get(), legacyScenePath.string());
        auto loadedLegacyScene = SceneLoader::loadScene(legacyScenePath.string());
        auto loadedLegacyAnimationNode = loadedLegacyScene
            ? loadedLegacyScene->getChild("LegacyTracksAnimation") : nullptr;
        auto loadedFollowingChild = loadedLegacyScene
            ? loadedLegacyScene->getChild("FollowingChild") : nullptr;
        expect(loadedLegacyAnimationNode && loadedFollowingChild,
               "legacy Animation and following Scene child both survive round-trip");
        auto loadedLegacyAnimation = loadedLegacyAnimationNode
            ? std::dynamic_pointer_cast<Animation>(loadedLegacyAnimationNode->shared_from_this()) : nullptr;
        expect(loadedLegacyAnimation && loadedLegacyAnimation->getTracks().size() == 1,
               "legacy Animation tracks remain present before following child");
    }

    {
        auto system = std::make_shared<System>();
        auto starter = std::make_shared<StarterCharacter>();
        CharacterRig::buildDefaultRigParts(starter);
        system->addChild(starter);
        auto injected = std::make_shared<AnimationClip>(builtin);
        injected->name = "InjectedWalk";
        auto starterWalk = std::dynamic_pointer_cast<Animation>(starter->getChildren().at("R6Walk"));
        auto starterHumanoid = std::dynamic_pointer_cast<Humanoid>(
            starter->getChildren().at("Humanoid"));
        auto starterJump = std::make_shared<Animation>();
        starterJump->Name = "R6Jump";
        starterJump->ContentPath = "assets/anims/r6_jump.rcanim";
        starter->addChild(starterJump);
        starterHumanoid->setJumpAnimation(starterJump);
        auto starterEquip = std::make_shared<Animation>();
        starterEquip->Name = "R6Equip";
        starterEquip->ContentPath = "assets/anims/r6_equip.rcanim";
        starter->addChild(starterEquip);
        starterHumanoid->setEquipAnimation(starterEquip);
        if (starterWalk) starterWalk->setClip(injected);
        expect(starterWalk && starterHumanoid &&
                   starterHumanoid->getWalkAnimation() == starterWalk &&
                   starterWalk->ContentPath == "assets/anims/r6_walk.rcanim",
               "default StarterCharacter exposes R6Walk and Humanoid references it");
        auto model = User::buildCharacterModel(system.get(), "ClipInjectionCharacter");
        std::shared_ptr<Humanoid> humanoid;
        if (model) {
            auto humanoidNode = model->getChildren().find("Humanoid");
            if (humanoidNode != model->getChildren().end())
                humanoid = std::dynamic_pointer_cast<Humanoid>(humanoidNode->second);
        }
        auto clonedWalk = humanoid ? humanoid->getWalkAnimation() : nullptr;
        auto clonedJump = humanoid ? humanoid->getJumpAnimation() : nullptr;
        auto clonedEquip = humanoid ? humanoid->getEquipAnimation() : nullptr;
        expect(humanoid && clonedWalk && clonedWalk->Parent.lock().get() == model.get() &&
               clonedWalk.get() != starterWalk.get() && clonedWalk->getClip() &&
               clonedWalk->getClip()->name == "InjectedWalk",
               "StarterCharacter Animation reference is remapped into generated User Humanoid");
        expect(clonedJump && clonedEquip &&
                   clonedJump->Parent.lock().get() == model.get() &&
                   clonedEquip->Parent.lock().get() == model.get() &&
                   clonedJump.get() != starterJump.get() &&
                   clonedEquip.get() != starterEquip.get(),
               "Walk, Jump, and Equip references all remap to PlayerCharacter clones");
        if (humanoid) {
            humanoid->resolveParts(model.get());
            const auto sameCFrame = [&](const CFrame& a, const CFrame& b) {
                return near(a.Position.x, b.Position.x) && near(a.Position.y, b.Position.y) &&
                       near(a.Position.z, b.Position.z) && near(a.Rotation.x, b.Rotation.x) &&
                       near(a.Rotation.y, b.Rotation.y) && near(a.Rotation.z, b.Rotation.z) &&
                       near(a.Rotation.w, b.Rotation.w);
            };
            auto rootPart = humanoid->getRootPart();
            const auto* shoulderBinding = CharacterRig::findR6Joint("LeftShoulder");
            const auto* hipBinding = CharacterRig::findR6Joint("LeftHip");
            humanoid->setIsGroundedForReplication(true);
            humanoid->setWalkCycle(0.0f);
            humanoid->applyBodyAnimation(true, false);
            auto toolLeft = humanoid->getLeftArmPart();
            expect(rootPart && shoulderBinding && toolLeft && sameCFrame(
                       toolLeft->getWorldCFrame(),
                       CharacterRig::applyR6Joint(rootPart->cframe, *shoulderBinding,
                           CFrame::fromAxisAngle(Vector3(1, 0, 0), 90.0f))),
                   "tool left-arm pose uses the R6 shoulder binding");
            humanoid->setSeatedForReplication(true);
            humanoid->applyBodyAnimation(false, false);
            auto seatedLeft = humanoid->getLeftArmPart();
            auto seatedLeg = humanoid->getLeftLegPart();
            expect(rootPart && shoulderBinding && hipBinding && seatedLeft && seatedLeg &&
                   sameCFrame(seatedLeft->getWorldCFrame(), CharacterRig::applyR6Joint(
                       rootPart->cframe, *shoulderBinding,
                       CFrame::fromAxisAngle(Vector3(1, 0, 0), 10.0f))) &&
                   sameCFrame(seatedLeg->getWorldCFrame(), CharacterRig::applyR6Joint(
                       rootPart->cframe, *hipBinding,
                       CFrame::fromAxisAngle(Vector3(1, 0, 0), 90.0f))),
                   "seated arm and leg poses use the R6 bindings");
            humanoid->setSeatedForReplication(false);
            humanoid->setWalkCycle(0.0f);
            humanoid->applyBodyAnimation(false, false);
            auto neutralLeft = humanoid->getLeftArmPart();
            auto neutralLeftCFrame = neutralLeft ? neutralLeft->getWorldCFrame() : CFrame();
            auto neutralLeg = humanoid->getLeftLegPart();
            auto neutralLegCFrame = neutralLeg ? neutralLeg->getWorldCFrame() : CFrame();
            humanoid->setWalkCycle(0.25f);
            humanoid->applyBodyAnimation(false, false);
            auto left = humanoid->getLeftArmPart();
            auto right = humanoid->getRightArmPart();
            expect(left && right && left->getWorldCFrame().Rotation.x *
                       right->getWorldCFrame().Rotation.x < -1e-4f,
                   "injected walk produces opposite left/right arm poses");
            const auto animatedLeftCFrame = left ? left->getWorldCFrame() : CFrame();
            const auto animatedLegCFrame = neutralLeg ? neutralLeg->getWorldCFrame() : CFrame();
            humanoid->setWalkCycle(0.0f);
            humanoid->applyBodyAnimation(false, false);
            const auto returnedLeftCFrame = left ? left->getWorldCFrame() : CFrame();
            const auto returnedLegCFrame = neutralLeg ? neutralLeg->getWorldCFrame() : CFrame();
            expect(!near(animatedLeftCFrame.Rotation.x, neutralLeftCFrame.Rotation.x, 1e-3f) &&
                   near(returnedLeftCFrame.Rotation.x, neutralLeftCFrame.Rotation.x, 1e-3f) &&
                   !near(animatedLegCFrame.Rotation.x, neutralLegCFrame.Rotation.x, 1e-3f) &&
                   near(returnedLegCFrame.Rotation.x, neutralLegCFrame.Rotation.x, 1e-3f) &&
                   near(returnedLeftCFrame.Position.x, neutralLeftCFrame.Position.x) &&
                   near(returnedLeftCFrame.Position.y, neutralLeftCFrame.Position.y) &&
                   near(returnedLeftCFrame.Position.z, neutralLeftCFrame.Position.z),
                   "grounded walk returns the shoulder to its neutral binding pose");
            humanoid->setIsGroundedForReplication(false);
            humanoid->applyBodyAnimation(false, false);
            expect(left && left->getWorldCFrame().Rotation.w < 0.99f,
                   "airborne Humanoid keeps its distinct fallback pose");
            humanoid->setIsGroundedForReplication(true);
            auto custom = std::make_shared<Animation>();
            custom->Length = 1.0f;
            custom->Speed = 1.0f;
            custom->Looped = true;
            auto customClip = std::make_shared<AnimationClip>();
            customClip->name = "CustomShoulder55";
            customClip->length = 1.0f;
            customClip->speed = 1.0f;
            customClip->looped = true;
            customClip->addKey("LeftShoulder", 0.0f,
                               CFrame::fromAxisAngle(Vector3(1, 0, 0), 55.0f));
            customClip->addKey("LeftShoulder", 1.0f,
                               CFrame::fromAxisAngle(Vector3(1, 0, 0), 55.0f));
            custom->setClip(customClip);
            humanoid->playAnimation(custom);
            humanoid->updateAnimation(0.1f);
            auto customLeft = humanoid->getLeftArmPart();
            expect(rootPart && shoulderBinding && customLeft && sameCFrame(
                       customLeft->getWorldCFrame(),
                       CharacterRig::applyR6Joint(rootPart->cframe, *shoulderBinding,
                           CFrame::fromAxisAngle(Vector3(1, 0, 0), 55.0f))),
                   "custom joint_delta Animation overrides the basic walk pose");
            humanoid->stopAnimation();
        }
    }

    {
        auto system = std::make_shared<System>();
        auto starter = std::make_shared<StarterCharacter>();
        CharacterRig::buildDefaultRigParts(starter);
        auto starterHumanoid = std::dynamic_pointer_cast<Humanoid>(starter->getChildren().at("Humanoid"));
        if (starterHumanoid) starterHumanoid->setWalkAnimationPath("MissingCustomWalk");
        system->addChild(starter);
        auto model = User::buildCharacterModel(system.get(), "UnresolvedWalkCharacter");
        std::shared_ptr<Humanoid> humanoid;
        if (model) {
            auto humanoidNode = model->getChildren().find("Humanoid");
            if (humanoidNode != model->getChildren().end())
                humanoid = std::dynamic_pointer_cast<Humanoid>(humanoidNode->second);
        }
        expect(humanoid && !humanoid->getWalkAnimation() &&
               humanoid->getWalkAnimationPath() == "MissingCustomWalk",
               "non-empty unresolved WalkAnimation path is preserved without runtime reference replacement");
    }

    {
        const auto clipScenePath = tempRoot / "clip_scene.yaml";
        const auto clipSceneRoundTripPath = tempRoot / "clip_scene_roundtrip.yaml";
        auto clipRoot = std::make_shared<Workspace>();
        auto clipAnimation = std::make_shared<Animation>();
        clipAnimation->Name = "JointDeltaAnimation";
        clipAnimation->setProperty("Length", YAML::Load("1.0"));
        clipAnimation->setProperty("Space", YAML::Load("joint_delta"));
        clipAnimation->setProperty("Rig", YAML::Load("R6"));
        clipAnimation->setProperty("Tracks", YAML::Load(
            "- PartName: LeftShoulder\n"
            "  Keyframes:\n"
            "    - Time: 0.0\n"
            "      Position: [0, 0, 0]\n"
            "      Rotation: [0, 0, 0, 1]\n"
            "      Easing: 0\n"));
        clipRoot->addChild(clipAnimation);
        SceneLoader::saveScene(clipRoot.get(), clipScenePath.string());
        auto loadedClipScene = SceneLoader::loadScene(clipScenePath.string());
        auto loadedAnimationNode = loadedClipScene ? loadedClipScene->getChild("JointDeltaAnimation") : nullptr;
        auto loadedAnimation = loadedAnimationNode
            ? std::dynamic_pointer_cast<Animation>(loadedAnimationNode->shared_from_this()) : nullptr;
        expect(loadedAnimation && loadedAnimation->getClip() &&
               loadedAnimation->getClip()->space == "joint_delta" &&
               loadedAnimation->getClip()->tracks.size() == 1,
               "joint_delta Animation clip survives Scene save/load");
        if (loadedAnimation && loadedAnimation->getClip()) {
            expect(loadedAnimation->getClip()->tracks.front().targetName == "LeftShoulder" &&
                   loadedAnimation->getClip()->tracks.front().keyframes.size() == 1,
                   "Scene round-trip preserves joint_delta track and keyframe");
            SceneLoader::saveScene(loadedClipScene.get(), clipSceneRoundTripPath.string());
            auto secondClipScene = SceneLoader::loadScene(clipSceneRoundTripPath.string());
            auto secondNode = secondClipScene ? secondClipScene->getChild("JointDeltaAnimation") : nullptr;
            auto secondAnimation = secondNode
                ? std::dynamic_pointer_cast<Animation>(secondNode->shared_from_this()) : nullptr;
            expect(secondAnimation && secondAnimation->getClip() &&
                   secondAnimation->getClip()->space == "joint_delta",
                   "joint_delta clip survives a second Scene round-trip");
        }
    }

    {
        const auto validAnim = tempRoot / "animations" / "walk.rcanim";
        std::filesystem::create_directories(validAnim.parent_path());
        expect(AnimationClipIO::save(validAnim.string(), builtin), "project walk clip is written");
        Animation project;
        project.ContentPath = validAnim.string();
        expect(project.loadContent() && project.resolveR6WalkClip().name == "R6Walk",
               "valid project walk Animation is selected");
        std::filesystem::remove(validAnim);
        Animation missing;
        missing.ContentPath = validAnim.string();
        expect(!missing.loadContent() && missing.resolveR6WalkClip().tracks.size() == builtin.tracks.size() &&
               missing.ContentPath == validAnim.string() && missing.isUsingBuiltInFallback(),
               "missing project walk Animation keeps its reference and falls back at runtime");
        std::filesystem::create_directories(validAnim.parent_path());
        std::ofstream out(validAnim); out << "recubin:\n  type: scene\n  version: 0\n"; out.close();
        Animation invalid;
        invalid.ContentPath = validAnim.string();
        expect(!invalid.loadContent() && invalid.resolveR6WalkClip().tracks.size() == builtin.tracks.size() &&
               invalid.getLoadStatus() == AnimationClipLoadStatus::TypeMismatch,
               "invalid project walk Animation preserves failure and falls back at runtime");
    }

    {
        const auto customPath = tempRoot / "assets" / "anims" / "my_walk.rcanim";
        std::filesystem::create_directories(customPath.parent_path());
        AnimationClip customClip = builtin;
        customClip.name = "MyWalk";
        expect(AnimationClipIO::save(customPath.string(), customClip),
               "custom Walk asset is written");

        auto makeStarterWithWalk = [&](const std::string& name,
                                       const std::string& contentPath) {
            auto system = std::make_shared<System>();
            auto starter = std::make_shared<StarterCharacter>();
            CharacterRig::buildDefaultRigParts(starter);
            system->addChild(starter);
            auto humanoid = std::dynamic_pointer_cast<Humanoid>(
                starter->getChildren().at("Humanoid"));
            auto animation = std::make_shared<Animation>();
            animation->Name = name;
            animation->ContentPath = contentPath;
            animation->loadContent();
            starter->addChild(animation);
            humanoid->setWalkAnimation(animation);
            return system;
        };

        auto customSystem = makeStarterWithWalk("MyCustomWalk", customPath.string());
        auto customModel = User::buildCharacterModel(customSystem.get(), "CustomCharacter");
        auto customHumanoid = customModel
            ? std::dynamic_pointer_cast<Humanoid>(customModel->getChildren().at("Humanoid")) : nullptr;
        auto clonedCustom = customHumanoid ? customHumanoid->getWalkAnimation() : nullptr;
        expect(clonedCustom && clonedCustom->Name == "MyCustomWalk" &&
                   clonedCustom->ContentPath == customPath.string() &&
                   clonedCustom->resolveR6WalkClip().name == "MyWalk" &&
                   !clonedCustom->isUsingBuiltInFallback(),
               "valid custom Walk remains the preferred PlayerCharacter reference");

        const auto missingPath = tempRoot / "assets" / "anims" / "missing_walk.rcanim";
        auto missingSystem = makeStarterWithWalk("MissingCustomWalk", missingPath.string());
        auto missingModel = User::buildCharacterModel(missingSystem.get(), "MissingCharacter");
        auto missingHumanoid = missingModel
            ? std::dynamic_pointer_cast<Humanoid>(missingModel->getChildren().at("Humanoid")) : nullptr;
        auto clonedMissing = missingHumanoid ? missingHumanoid->getWalkAnimation() : nullptr;
        expect(clonedMissing && clonedMissing->Name == "MissingCustomWalk" &&
                   clonedMissing->ContentPath == missingPath.string() &&
                   clonedMissing->resolveR6WalkClip().name == "R6Walk" &&
                   clonedMissing->isUsingBuiltInFallback(),
               "missing custom Walk keeps its Animation and ContentPath during runtime fallback");

        const auto brokenPath = tempRoot / "assets" / "anims" / "broken_walk.rcanim";
        std::ofstream brokenFile(brokenPath);
        brokenFile << "recubin:\n  type: scene\n  version: 0\n";
        brokenFile.close();
        auto brokenSystem = makeStarterWithWalk("BrokenCustomWalk", brokenPath.string());
        auto brokenModel = User::buildCharacterModel(brokenSystem.get(), "BrokenCharacter");
        auto brokenHumanoid = brokenModel
            ? std::dynamic_pointer_cast<Humanoid>(brokenModel->getChildren().at("Humanoid")) : nullptr;
        auto clonedBroken = brokenHumanoid ? brokenHumanoid->getWalkAnimation() : nullptr;
        expect(clonedBroken && clonedBroken->Name == "BrokenCustomWalk" &&
                   clonedBroken->ContentPath == brokenPath.string() &&
                   clonedBroken->resolveR6WalkClip().name == "R6Walk" &&
                   clonedBroken->getLoadStatus() == AnimationClipLoadStatus::TypeMismatch &&
                   clonedBroken->isUsingBuiltInFallback(),
               "broken custom Walk keeps its Animation and ContentPath during runtime fallback");

        auto legacySystem = std::make_shared<System>();
        auto legacyStarter = std::make_shared<StarterCharacter>();
        CharacterRig::buildDefaultRigParts(legacyStarter);
        legacySystem->addChild(legacyStarter);
        auto legacyHumanoid = std::dynamic_pointer_cast<Humanoid>(
            legacyStarter->getChildren().at("Humanoid"));
        legacyHumanoid->setWalkAnimation(nullptr);
        legacyStarter->removeChild("R6Walk");
        auto legacyModel = User::buildCharacterModel(legacySystem.get(), "LegacyCharacter");
        auto runtimeHumanoid = legacyModel
            ? std::dynamic_pointer_cast<Humanoid>(legacyModel->getChildren().at("Humanoid")) : nullptr;
        auto runtimeWalk = runtimeHumanoid ? runtimeHumanoid->getWalkAnimation() : nullptr;
        expect(!legacyStarter->getChild("R6Walk") && runtimeWalk &&
                   runtimeWalk->Parent.lock().get() == legacyModel.get() &&
                   runtimeWalk->getSource() == AnimationSource::BuiltIn,
               "unbound legacy character receives a visible runtime-only built-in Walk");
    }

    {
        const auto referenceScenePath = tempRoot / "animation_reference_scene.yaml";
        auto referenceRoot = std::make_shared<Workspace>();
        auto character = std::make_shared<Model>();
        character->Name = "Character";
        auto humanoid = std::make_shared<Humanoid>();
        humanoid->Name = "Humanoid";
        auto walk = std::make_shared<Animation>();
        walk->Name = "MyWalk";
        walk->ContentPath = "assets/anims/my_walk.rcanim";
        character->addChild(humanoid);
        character->addChild(walk);
        humanoid->setWalkAnimation(walk);
        referenceRoot->addChild(character);
        expect(SceneLoader::saveSceneResult(referenceRoot.get(), referenceScenePath.string()),
               "Scene with a Humanoid Animation reference saves");
        auto loadedReferenceRoot = SceneLoader::loadScene(referenceScenePath.string());
        auto loadedCharacter = loadedReferenceRoot
            ? loadedReferenceRoot->getChild("Character") : nullptr;
        auto loadedHumanoid = loadedCharacter
            ? dynamic_cast<Humanoid*>(loadedCharacter->getChild("Humanoid")) : nullptr;
        auto loadedWalk = loadedHumanoid ? loadedHumanoid->getWalkAnimation() : nullptr;
        expect(loadedWalk && loadedWalk->Name == "MyWalk" &&
                   loadedWalk->ContentPath == "assets/anims/my_walk.rcanim" &&
                   loadedWalk->Parent.lock().get() == loadedCharacter,
               "Humanoid Animation reference and ContentPath survive Scene round-trip");
    }

    {
        std::ofstream out(scenePath);
        out << "Root:\n"
                "  ClassName: System\n"
                "  Name: System\n"
                "  Children:\n"
                "    - ClassName: Workspace\n"
                "      Name: Workspace\n";
        out.close();
        auto old = SceneLoader::loadSceneResult(scenePath.string());
        expect(old && old.metadata.version == 0 &&
                   old.metadata.characterAnimationBindingsVersion == 0 &&
                   old.metadata.legacyDefaultR6AnimationDecision.empty(),
               "headerless scene loads as implicit version zero");

        const auto legacyHeaderPath = tempRoot / "legacy_header_scene.yaml";
        out.open(legacyHeaderPath);
        out << "recubin:\n  type: scene\n  version: 0\n"
               "  migrations:\n    default_r6_animations:\n      version: 1\n      decision: generated\n"
               "  animations:\n    r6_walk:\n      ContentPath: animations/walk.rcanim\n"
               "Root:\n  ClassName: System\n  Name: System\n  Children: []\n";
        out.close();
        auto legacyHeader = SceneLoader::loadSceneResult(legacyHeaderPath.string());
        expect(legacyHeader &&
                   legacyHeader.metadata.legacyDefaultR6AnimationDecision == "generated" &&
                   legacyHeader.metadata.legacyWalkContentPath == "animations/walk.rcanim",
               "retired R6 generation header remains readable as compatibility metadata");
        old.metadata.characterAnimationBindingsVersion = 1;
        expect(SceneLoader::saveSceneResult(old.root.get(), sceneRoundTripPath.string(), old.metadata),
               "scene metadata saves with character binding migration version");
        const std::string savedText = FileLoader::readText(sceneRoundTripPath.string());
        expect(savedText.find("character_animation_bindings") != std::string::npos &&
                   savedText.find("default_r6_animations") == std::string::npos &&
                   savedText.find("r6_walk") == std::string::npos,
               "new Scene saves emit only the character binding migration header");
        auto saved = SceneLoader::loadSceneResult(sceneRoundTripPath.string());
        expect(saved && saved.metadata.characterAnimationBindingsVersion == 1 &&
                   saved.metadata.legacyDefaultR6AnimationDecision.empty() &&
                   saved.metadata.legacyWalkContentPath.empty(),
               "character binding migration metadata round-trips without retired fields");
        const auto wrongScene = tempRoot / "wrong_scene.yaml";
        out.open(wrongScene); out << "recubin:\n  type: animation\n  version: 1\nRoot: {}\n"; out.close();
        expect(SceneLoader::loadSceneResult(wrongScene.string()).status == SceneLoader::LoadStatus::InvalidType,
               "scene loader rejects a mismatched document type");
        const auto newerScene = tempRoot / "newer_scene.yaml";
        out.open(newerScene); out << "recubin:\n  type: scene\n  version: 1\nRoot: {}\n"; out.close();
        expect(SceneLoader::loadSceneResult(newerScene.string()).status == SceneLoader::LoadStatus::UnsupportedVersion,
               "scene loader rejects a newer scene version");
    }

    {
        const auto makeR6System = [](bool withWalk) {
            auto system = std::make_shared<System>();
            auto starter = std::make_shared<StarterCharacter>();
            CharacterRig::buildDefaultRigParts(starter);
            system->addChild(starter);
            if (!withWalk) {
                auto humanoid = std::dynamic_pointer_cast<Humanoid>(
                    starter->getChildren().at("Humanoid"));
                humanoid->setWalkAnimation(nullptr);
                starter->removeChild("R6Walk");
            }
            return system;
        };
        const auto migrationScenePath = (tempRoot / "migration_scene.yaml").string();

        auto insertionSystem = makeR6System(false);
        SceneLoader::SceneDocumentMetadata insertionMetadata;
        auto insertionResult = EditorManager::migrateCharacterAnimationBindings(
            insertionSystem.get(), migrationScenePath, insertionMetadata);
        auto insertionStarter = insertionSystem->getChild("StarterCharacter");
        auto insertionHumanoid = insertionStarter
            ? dynamic_cast<Humanoid*>(insertionStarter->getChild("Humanoid")) : nullptr;
        auto insertedWalk = insertionHumanoid ? insertionHumanoid->getWalkAnimation() : nullptr;
        expect(insertionResult == EditorManager::CharacterAnimationMigrationResult::Inserted &&
                   insertionMetadata.characterAnimationBindingsVersion == 1 &&
                   insertedWalk && insertedWalk->ContentPath == "assets/anims/r6_walk.rcanim",
               "unmigrated empty R6 Walk binding is filled once with the standard asset");

        insertionHumanoid->setWalkAnimation(nullptr);
        insertionStarter->removeChild(insertedWalk->Name);
        auto repeatedResult = EditorManager::migrateCharacterAnimationBindings(
            insertionSystem.get(), migrationScenePath, insertionMetadata);
        expect(repeatedResult == EditorManager::CharacterAnimationMigrationResult::AlreadyMigrated &&
                   !insertionHumanoid->getWalkAnimation() &&
                   insertionHumanoid->getWalkAnimationPath().empty(),
               "migration marker prevents reinsertion after a user removes the standard binding");

        auto unresolvedSystem = makeR6System(false);
        auto unresolvedStarter = unresolvedSystem->getChild("StarterCharacter");
        auto unresolvedHumanoid = dynamic_cast<Humanoid*>(
            unresolvedStarter->getChild("Humanoid"));
        unresolvedHumanoid->setWalkAnimationPath("MissingCustomWalk");
        SceneLoader::SceneDocumentMetadata unresolvedMetadata;
        auto unresolvedResult = EditorManager::migrateCharacterAnimationBindings(
            unresolvedSystem.get(), migrationScenePath, unresolvedMetadata);
        expect(unresolvedResult == EditorManager::CharacterAnimationMigrationResult::RecordedOnly &&
                   unresolvedMetadata.characterAnimationBindingsVersion == 1 &&
                   unresolvedHumanoid->getWalkAnimationPath() == "MissingCustomWalk" &&
                   !unresolvedHumanoid->getWalkAnimation(),
               "migration preserves a non-empty unresolved custom Animation reference");

        auto legacyPathSystem = makeR6System(false);
        SceneLoader::SceneDocumentMetadata legacyPathMetadata;
        legacyPathMetadata.legacyWalkContentPath = "animations/walk.rcanim";
        const auto legacyAssetPath = tempRoot / "animations" / "walk.rcanim";
        std::filesystem::create_directories(legacyAssetPath.parent_path());
        expect(AnimationClipIO::save(legacyAssetPath.string(), builtin),
               "legacy scene-relative Walk asset is written");
        auto legacyPathResult = EditorManager::migrateCharacterAnimationBindings(
            legacyPathSystem.get(), migrationScenePath, legacyPathMetadata);
        auto legacyPathStarter = legacyPathSystem->getChild("StarterCharacter");
        auto legacyPathHumanoid = dynamic_cast<Humanoid*>(
            legacyPathStarter->getChild("Humanoid"));
        auto migratedLegacyWalk = legacyPathHumanoid->getWalkAnimation();
        const auto expectedLegacyPath = std::filesystem::absolute(legacyAssetPath).lexically_normal().string();
        expect(legacyPathResult == EditorManager::CharacterAnimationMigrationResult::Inserted &&
                   migratedLegacyWalk &&
                   std::filesystem::path(migratedLegacyWalk->ContentPath).lexically_normal() ==
                       std::filesystem::path(expectedLegacyPath),
               "legacy scene-relative Walk path is resolved before insertion");

        auto untitledSystem = makeR6System(false);
        SceneLoader::SceneDocumentMetadata untitledMetadata;
        expect(EditorManager::migrateCharacterAnimationBindings(
                   untitledSystem.get(), {}, untitledMetadata) ==
                   EditorManager::CharacterAnimationMigrationResult::NotApplicable &&
                   untitledMetadata.characterAnimationBindingsVersion == 0,
               "untitled Scene is not changed by animation migration");

        auto nonR6System = std::make_shared<System>();
        auto nonR6Starter = std::make_shared<StarterCharacter>();
        nonR6Starter->addChild(std::make_shared<Humanoid>());
        nonR6System->addChild(nonR6Starter);
        SceneLoader::SceneDocumentMetadata nonR6Metadata;
        expect(EditorManager::migrateCharacterAnimationBindings(
                   nonR6System.get(), migrationScenePath, nonR6Metadata) ==
                   EditorManager::CharacterAnimationMigrationResult::NotApplicable &&
                   nonR6Metadata.characterAnimationBindingsVersion == 0,
               "non-R6 StarterCharacter is not changed by animation migration");

        expect(EditorManager::restoreDefaultR6Bindings(insertionSystem.get()) &&
                   insertionHumanoid->getWalkAnimation() &&
                   insertionHumanoid->getWalkAnimation()->ContentPath ==
                       "assets/anims/r6_walk.rcanim",
               "explicit Restore Default Animations can reset a migrated empty binding");
    }

    std::error_code ignored;
    std::filesystem::remove_all(tempRoot, ignored);
    std::cout << "[AnimationClip] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

static int runSceneLoadTransactionRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const std::string& message) {
        std::cout << "[SceneLoadTransaction] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };
    auto writeScene = [&](const std::filesystem::path& path,
                          const std::string& yaml) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << yaml;
        return static_cast<bool>(out);
    };

    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto scenePath = std::filesystem::temp_directory_path() /
        ("recubin_scene_transaction_" + suffix + ".yaml");
    const auto invalidPath = std::filesystem::temp_directory_path() /
        ("recubin_scene_transaction_invalid_" + suffix + ".yaml");
    const auto missingUserPath = std::filesystem::temp_directory_path() /
        ("recubin_scene_transaction_missing_user_" + suffix + ".yaml");
    const auto contextOnlyPath = std::filesystem::temp_directory_path() /
        ("recubin_scene_transaction_context_only_" + suffix + ".yaml");

    expect(writeScene(scenePath,
        "recubin:\n"
        "  type: scene\n"
        "  version: 0\n"
        "Root:\n"
        "  ClassName: System\n"
        "  Properties:\n"
        "    MaxClonesPerFrame: 77\n"
        "    BaseResolution: [1280, 720]\n"
        "    UseNetwork: true\n"
        "  Children:\n"
        "    - ClassName: Users\n"
        "      Name: Users\n"
        "      Children:\n"
        "        - ClassName: User\n"
        "          Name: User\n"
        "          Properties:\n"
        "            ControlMode: Program\n"
        "            Speed: 0.75\n"
        "            RotationSpeed: 2.5\n"
        "            MouseRotationSpeed: 0.35\n"
        "            CharacterSmoothing: 0.75\n"
        "            MovementInputEnabled: false\n"
        "            CameraInputEnabled: false\n"
        "            HotkeyInputEnabled: false\n"
        "            ToolInputEnabled: false\n"
        "            CameraDistance: 14.0\n"
        "            ZoomSpeed: 0.25\n"
        "            MouseZoomSpeed: 1.5\n"
        "          Children:\n"
        "            - ClassName: Folder\n"
        "              Name: Inventory\n"
        "              Children:\n"
        "                - ClassName: Tool\n"
        "                  Name: Hammer\n"
        "    - ClassName: Workspace\n"
        "      Name: LoadedWorkspace\n"
        "      Children:\n"
        "        - ClassName: ObjectValue\n"
        "          Name: UserReference\n"
        "          Properties:\n"
        "            Value: 'Users\\User'\n"),
        "transaction scene fixture is written");
    expect(writeScene(invalidPath,
        "Root:\n"
        "  ClassName: System\n"
        "  Children:\n"
        "    - ClassName: Users\n"
        "      Children:\n"
        "        - ClassName: User\n"
        "          Properties:\n"
        "            Speed: [invalid]\n"),
        "invalid User property fixture is written");
    expect(writeScene(missingUserPath,
        "Root:\n"
        "  ClassName: System\n"
        "  Children:\n"
        "    - ClassName: Workspace\n"
        "      Name: EmptyWorkspace\n"),
        "missing User/Inventory fixture is written");
    expect(writeScene(contextOnlyPath,
        "Root:\n"
        "  ClassName: System\n"
        "  Properties:\n"
        "    MaxClonesPerFrame: 3\n"),
        "LoadContext isolation fixture is written");

    auto liveSystem = std::make_shared<System>();
    liveSystem->MaxClonesPerFrame = 321;
    liveSystem->BaseResolution = {1600.0f, 900.0f};
    auto oldWorkspace = std::make_shared<Workspace>();
    oldWorkspace->Name = "OldWorkspace";
    liveSystem->addChild(oldWorkspace);
    auto liveUsers = std::make_shared<Users>();
    liveSystem->addChild(liveUsers);
    auto liveUser = std::make_shared<User>(std::make_unique<NullInputBackend>());
    liveUser->speed = 0.5f;
    liveUser->cameraDistance = 9.0f;
    liveUser->initializeInventory();
    auto oldTool = std::make_shared<Tool>("OldTool");
    liveUser->Inventory->addChild(oldTool);
    liveUsers->addChild(liveUser);

    System* const liveSystemAddress = liveSystem.get();
    User* const liveUserAddress = liveUser.get();
    UserInput* const inputAddress = liveUser->Input.get();
    RCBNScriptSignal* const characterAddedAddress = liveUser->CharacterAdded.get();
    camera* const cameraAddress = &liveUser->current_camera;
    const camera cameraBefore = liveUser->current_camera;

    auto staged = SceneRuntime::stageSceneLoad(
        scenePath.string(), liveSystem, liveUser);
    expect(static_cast<bool>(staged),
           "User/Inventory/Tool scene stages successfully");
    expect(staged.system &&
               staged.system->getChildByPath("Users\\User\\Inventory\\Hammer"),
           "staged tree retains User/Inventory/Tool without skipping User");
    Instance* stagedReferenceNode = staged.system
        ? staged.system->getChildByPath("LoadedWorkspace\\UserReference")
        : nullptr;
    auto stagedReference = stagedReferenceNode
        ? std::dynamic_pointer_cast<ObjectValue>(stagedReferenceNode->shared_from_this())
        : nullptr;
    expect(stagedReference && stagedReference->getTarget() == staged.user,
           "staged ObjectValue resolves Users\\User to the staged User");
    expect(liveSystem.get() == liveSystemAddress &&
               liveUser.get() == liveUserAddress &&
               liveSystem->getChild("OldWorkspace") == oldWorkspace.get() &&
               liveUser->Inventory->getChild("OldTool") == oldTool.get() &&
               liveSystem->MaxClonesPerFrame == 321 && liveUser->speed == 0.5f,
           "staging leaves the live tree and scalar values unchanged");
    expect(User::getInstance() == liveUser.get(),
           "remote staged User does not replace the local User singleton");

    const auto invalid = SceneRuntime::stageSceneLoad(
        invalidPath.string(), liveSystem, liveUser);
    expect(!invalid && invalid.status == SceneLoader::LoadStatus::YamlError,
           "invalid User property fails staging with YamlError");
    expect(liveSystem->getChild("OldWorkspace") == oldWorkspace.get() &&
               liveUser->Inventory->getChild("OldTool") == oldTool.get(),
           "failed staging preserves the current live scene");

    LuauEngine engine;
    const auto bound = SceneRuntime::commitAndBind(
        std::move(staged), liveSystem, liveUser, engine, nullptr);
    const auto committedUsersIt = liveSystem->children.find("Users");
    auto committedUsers = committedUsersIt != liveSystem->children.end()
        ? std::dynamic_pointer_cast<Users>(committedUsersIt->second) : nullptr;
    expect(bound.workspace && bound.workspace->Name == "LoadedWorkspace" &&
               liveSystem->getChild("OldWorkspace") == nullptr,
           "commit replaces System children with the staged scene");
    expect(liveSystem.get() == liveSystemAddress && liveUser.get() == liveUserAddress &&
               committedUsers && committedUsers->getChild("User") == liveUser.get(),
           "commit preserves live System/User identities and replaces staged User");
    expect(stagedReference &&
               liveSystem->getChildByPath("LoadedWorkspace\\UserReference") ==
                   stagedReference.get() &&
               stagedReference->getTarget() == liveUser,
           "commit keeps the ObjectValue instance and re-resolves it to the live User");
    expect(liveUser->Input.get() == inputAddress &&
               liveUser->CharacterAdded.get() == characterAddedAddress &&
               &liveUser->current_camera == cameraAddress &&
               liveUser->current_camera.Position.x == cameraBefore.Position.x &&
               liveUser->current_camera.Position.y == cameraBefore.Position.y &&
               liveUser->current_camera.Position.z == cameraBefore.Position.z,
           "commit preserves input, signals, and camera runtime state");
    expect(liveSystem->MaxClonesPerFrame == 77 &&
               liveSystem->BaseResolution == Vector2(1280.0f, 720.0f) &&
               liveSystem->UseNetwork &&
               liveUser->controlMode == User::ControlMode::Program &&
               liveUser->speed == 0.75f && liveUser->characterSmoothing == 0.75f &&
               !liveUser->isMovementInputEnabled() && !liveUser->isCameraInputEnabled() &&
               !liveUser->isHotkeyInputEnabled() && !liveUser->isToolInputEnabled() &&
               liveUser->cameraDistance == 14.0f,
           "commit applies persisted System/User scalar values");
    expect(liveUser->Inventory && liveUser->Inventory->getChild("Hammer") &&
               liveUser->getToolInSlot(0) && liveUser->getToolInSlot(0)->Name == "Hammer",
           "commit adopts Inventory and synchronizes staged Tools");
    expect(liveSystem->getChild("PathfindingService") &&
               liveSystem->getChild("ChatService"),
           "commit supplies default runtime services");

    auto missingUserSystem = std::make_shared<System>();
    auto missingUser = std::make_shared<User>(
        std::make_unique<NullInputBackend>(), true);
    auto missingStage = SceneRuntime::stageSceneLoad(
        missingUserPath.string(), missingUserSystem, missingUser);
    LuauEngine missingEngine;
    const auto missingBound = SceneRuntime::commitAndBind(
        std::move(missingStage), missingUserSystem, missingUser,
        missingEngine, nullptr);
    const auto generatedUsersIt = missingUserSystem->children.find("Users");
    auto generatedUsers = generatedUsersIt != missingUserSystem->children.end()
        ? std::dynamic_pointer_cast<Users>(generatedUsersIt->second) : nullptr;
    expect(missingBound.workspace && generatedUsers &&
               generatedUsers->getChild("User") == missingUser.get() &&
               missingUser->Inventory &&
               missingUser->Inventory->Parent.lock().get() == missingUser.get(),
           "missing User and Inventory are supplied during commit");

    const auto notFoundPath = std::filesystem::temp_directory_path() /
        ("recubin_scene_transaction_not_found_" + suffix + ".yaml");
    const auto notFound = SceneRuntime::stageSceneLoad(
        notFoundPath.string(), liveSystem, liveUser);
    expect(!notFound && notFound.status == SceneLoader::LoadStatus::NotFound,
           "non-empty missing path is a staging failure");

    auto emptySystem = std::make_shared<System>();
    auto emptyUser = std::make_shared<User>(
        std::make_unique<NullInputBackend>(), true);
    auto emptyStage = SceneRuntime::stageSceneLoad({}, emptySystem, emptyUser);
    LuauEngine emptyEngine;
    const auto emptyBound = SceneRuntime::commitAndBind(
        std::move(emptyStage), emptySystem, emptyUser, emptyEngine, nullptr);
    expect(emptyBound.workspace && emptyBound.workspace->getChild("Lighting"),
           "empty path commits a new scene with default Workspace and Lighting");

    auto mappedSystem = std::make_shared<System>();
    SceneLoader::LoadContext firstContext;
    firstContext.registerMergeInstance("System", mappedSystem);
    const auto contextLoad = SceneLoader::loadSceneResult(
        contextOnlyPath.string(), firstContext);
    const auto contextFreeLoad = SceneLoader::loadSceneResult(contextOnlyPath.string());
    expect(contextLoad.root == mappedSystem && mappedSystem->MaxClonesPerFrame == 3,
           "LoadContext merges into its explicitly registered System");
    expect(contextFreeLoad && contextFreeLoad.root != mappedSystem,
           "LoadContext registrations do not leak into later loads");

    for (const auto& path : {scenePath, invalidPath, missingUserPath, contextOnlyPath}) {
        std::error_code error;
        std::filesystem::remove(path, error);
        expect(!error, "temporary fixture is removed: " + path.filename().string());
    }

    std::cout << "[SceneLoadTransaction] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

static int runGuiAutomationRegression() {
    int failures = 0;
    auto expect = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << "[GuiAutomation] FAIL: " << message << '\n'; }
    };
    const auto path = std::filesystem::temp_directory_path() / "recubin_gui_automation_test.png";
    std::vector<std::uint8_t> pixels = {255, 0, 0, 255, 0, 255, 0, 255,
                                        0, 0, 255, 255, 255, 255, 255, 255};
    const std::vector<std::uint8_t> expectedPngPixels = {
        0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255};
    std::string error;
    expect(PngWriter::writeRgba8(path.string(), 2, 2, pixels, &error), "small RGBA PNG is written");
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    expect(bytes.size() > 24 && bytes[0] == 137 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G',
           "PNG signature is valid");
    expect(bytes.size() > 24 && bytes[16] == 0 && bytes[17] == 0 && bytes[18] == 0 && bytes[19] == 2 &&
               bytes[20] == 0 && bytes[21] == 0 && bytes[22] == 0 && bytes[23] == 2,
           "PNG IHDR dimensions are 2x2");
    int decodedWidth = 0, decodedHeight = 0, decodedChannels = 0;
    unsigned char* decoded = stbi_load(path.string().c_str(), &decodedWidth,
                                       &decodedHeight, &decodedChannels, 4);
    bool decodedPixels = decoded && decodedWidth == 2 && decodedHeight == 2;
    if (decoded) {
        for (size_t index = 0; index < expectedPngPixels.size(); ++index)
            decodedPixels = decodedPixels && decoded[index] == expectedPngPixels[index];
        stbi_image_free(decoded);
    }
    expect(decodedPixels, "PNG decodes to the expected RGBA pixel content");
    expect(validateGuiAutomationCommand("click Picker") &&
               validateGuiAutomationCommand("right_click Picker") &&
               validateGuiAutomationCommand("capture output.png") &&
               validateGuiAutomationCommand("key ctrl+shift+a") &&
               validateGuiAutomationCommand("quit"),
           "valid GUI automation commands are accepted");
    expect(!validateGuiAutomationCommand("click Picker extra") &&
               !validateGuiAutomationCommand("key Ctrl+") &&
               !validateGuiAutomationCommand("quit extra") &&
               !validateGuiAutomationCommand("wait Picker nope") &&
               !validateGuiAutomationCommand("mouse nope 1") &&
               !validateGuiAutomationCommand("mouse_down 3"),
           "malformed GUI automation commands are rejected");
    expect(!PngWriter::writeRgba8(path.string(), 2, 2, {}, &error), "invalid RGBA buffer is rejected");
    std::error_code ec; std::filesystem::remove(path, ec);
    std::cout << "[GuiAutomation] failures=" << failures << " result="
              << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

static int runSceneHierarchyGroupingRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) { ++failures; std::cerr << "[Grouping] FAIL: " << message << '\n'; }
    };
    const auto& catalogEntries = InstanceCatalog::entries();
    std::unordered_set<std::string_view> catalogNames;
    bool catalogUnique = true;
    for (const auto& entry : catalogEntries)
        catalogUnique = catalogUnique && catalogNames.insert(entry.className).second;
    expect(catalogUnique, "catalog class names contain no duplicates");
    auto expectCategory = [&](std::string_view className, InstanceCategory category) {
        const auto it = std::find_if(catalogEntries.begin(), catalogEntries.end(),
            [&](const auto& entry) { return entry.className == className; });
        return it != catalogEntries.end() && it->category == category;
    };
    expect(expectCategory("Folder", InstanceCategory::Container) &&
               expectCategory("Model", InstanceCategory::Container) &&
               expectCategory("Tool", InstanceCategory::Container),
           "catalog classifies container instances");
    expect(expectCategory("TextFile", InstanceCategory::File) &&
               expectCategory("FileRef", InstanceCategory::File) &&
               expectCategory("FontFile", InstanceCategory::File),
           "catalog classifies file instances");
    expect(expectCategory("Script", InstanceCategory::Script) &&
               expectCategory("LocalScript", InstanceCategory::Script) &&
               expectCategory("ModuleScript", InstanceCategory::Script),
           "catalog classifies script instances");
    expect(std::none_of(catalogEntries.begin(), catalogEntries.end(), [](const auto& entry) {
               return entry.className == "System" || entry.className == "Users" || entry.className == "User";
           }), "catalog excludes system and user singleton classes");
    const auto scriptMatches = InstanceCatalog::search("script");
    expect(scriptMatches.size() == 3 && InstanceCatalog::search("SCRIPT").size() == 3,
           "catalog search is case-insensitive and finds all script classes");
    expect(InstanceCatalog::search("SYSTEM").empty(), "catalog excludes system classes");
    expect(InstanceCatalog::create("Workspace") != nullptr, "catalog creates Workspace");

    // Explorer range selection is based only on the expanded rows supplied in
    // visible display order. Reverse selection returns the same stable order.
    auto selectionRoot = std::make_shared<Folder>();
    auto selectionA = std::make_shared<Folder>();
    auto selectionB = std::make_shared<Folder>();
    auto selectionC = std::make_shared<Folder>();
    auto collapsedGrandchild = std::make_shared<Folder>();
    selectionRoot->Name = "SelectionRoot";
    selectionA->Name = "SelectionA";
    selectionB->Name = "SelectionB";
    selectionC->Name = "SelectionC";
    collapsedGrandchild->Name = "CollapsedGrandchild";
    selectionB->addChild(collapsedGrandchild);
    selectionRoot->addChild(selectionA);
    selectionRoot->addChild(selectionB);
    selectionRoot->addChild(selectionC);
    const std::vector<Instance*> visibleSelectionRows = {
        selectionRoot.get(), selectionA.get(), selectionB.get(), selectionC.get()};
    const std::vector<Instance*> expectedVisibleRange = {
        selectionA.get(), selectionB.get(), selectionC.get()};
    expect(SceneHierarchySelection::selectVisibleRange(
               visibleSelectionRows, selectionA.get(), selectionC.get(), {}, false)
               == expectedVisibleRange,
           "Explorer Shift range includes both endpoints in forward visible order");
    expect(SceneHierarchySelection::selectVisibleRange(
               visibleSelectionRows, selectionC.get(), selectionA.get(), {}, false)
               == expectedVisibleRange,
           "Explorer Shift range supports reverse clicks with stable visible order");
    const auto collapsedRange = SceneHierarchySelection::selectVisibleRange(
        visibleSelectionRows, selectionA.get(), selectionC.get(), {}, false);
    expect(std::find(collapsedRange.begin(), collapsedRange.end(), collapsedGrandchild.get())
               == collapsedRange.end(),
           "Explorer Shift range excludes nodes omitted by collapsed branches");

    const std::vector<Instance*> priorSelection = {
        selectionRoot.get(), selectionB.get(), selectionB.get()};
    expect(SceneHierarchySelection::selectVisibleRange(
               visibleSelectionRows, selectionA.get(), selectionC.get(), priorSelection, false)
               == expectedVisibleRange,
           "plain Shift replaces the existing selection");
    const std::vector<Instance*> expectedAppendedRange = {
        selectionRoot.get(), selectionB.get(), selectionA.get(), selectionC.get()};
    expect(SceneHierarchySelection::selectVisibleRange(
               visibleSelectionRows, selectionA.get(), selectionC.get(), priorSelection, true)
               == expectedAppendedRange,
           "Ctrl+Shift preserves existing order and deduplicates the visible range");

    const std::vector<Instance*> expectedFallback = {selectionC.get()};
    expect(SceneHierarchySelection::selectVisibleRange(
               visibleSelectionRows, collapsedGrandchild.get(), selectionC.get(), priorSelection, false)
               == expectedFallback,
           "non-visible Explorer anchor falls back to the clicked target");
    const std::vector<Instance*> expectedAppendedFallback = {
        selectionRoot.get(), selectionB.get(), selectionC.get()};
    expect(SceneHierarchySelection::selectVisibleRange(
               visibleSelectionRows, nullptr, selectionC.get(), priorSelection, true)
               == expectedAppendedFallback,
           "invalid Explorer anchor appends only the clicked target for Ctrl+Shift");

    const auto directChildren = SceneHierarchySelection::collectDirectChildren(*selectionRoot);
    std::vector<Instance*> expectedDirectChildren;
    for (const auto& [name, child] : selectionRoot->getChildren())
        if (child) expectedDirectChildren.push_back(child.get());
    expect(directChildren == expectedDirectChildren &&
               std::find(directChildren.begin(), directChildren.end(), selectionRoot.get())
                   == directChildren.end() &&
               std::find(directChildren.begin(), directChildren.end(), collapsedGrandchild.get())
                   == directChildren.end(),
           "select children returns direct children only in Explorer display order");
    Folder emptySelectionParent;
    expect(SceneHierarchySelection::collectDirectChildren(emptySelectionParent).empty(),
           "select children returns an empty selection for a leaf");

    auto workspace = std::make_shared<Workspace>();
    auto cube = std::make_shared<Cube>(Vector3(4, 2, -3), Vector3(1, 1, 1), Cube::defaultTextureID);
    auto folder = std::make_shared<Folder>();
    cube->Name = "Cube";
    folder->Name = "Folder";
    auto nested = std::make_shared<Cube>(Vector3(1, 2, 3), Vector3(1, 1, 1), Cube::defaultTextureID);
    folder->addChild(nested);
    workspace->addChild(cube);
    workspace->addChild(folder);
    const CFrame cubeWorld = cube->getWorldCFrame();
    const CFrame nestedWorld = nested->getWorldCFrame();
    auto model = std::make_shared<Model>();
    model->Name = "Model";
    CommandHistory history;
    history.execute(std::make_unique<GroupInstancesCommand>(
        workspace, model, std::vector<std::shared_ptr<Instance>>{cube, folder}));
    expect(model->getChild("Cube") == cube.get() && model->getChild("Folder") == folder.get(),
           "group moves selected roots");
    expect(cube->getWorldCFrame().Position == cubeWorld.Position &&
               nested->getWorldCFrame().Position == nestedWorld.Position,
           "group preserves descendant world positions");
    history.undo();
    expect(workspace->getChild("Cube") == cube.get() && workspace->getChild("Folder") == folder.get(),
           "undo restores original parents");
    history.redo();
    expect(model->getChild("Folder") == folder.get() &&
               nested->getWorldCFrame().Position == nestedWorld.Position,
               "redo restores group and descendant pose");

    // Multi-selection rename uses canonical names without disturbing an
    // unselected sibling, and runtime-locked instances are skipped.
    auto renameParent = std::make_shared<Folder>();
    auto renameA = std::make_shared<Cube>(Vector3(), Vector3(1, 1, 1), Cube::defaultTextureID);
    auto renameB = std::make_shared<Cube>(Vector3(), Vector3(1, 1, 1), Cube::defaultTextureID);
    auto occupied = std::make_shared<Cube>(Vector3(), Vector3(1, 1, 1), Cube::defaultTextureID);
    renameA->Name = "oldA"; renameB->Name = "oldB"; occupied->Name = "base";
    renameB->lockRuntimeName();
    renameParent->addChild(renameA); renameParent->addChild(renameB); renameParent->addChild(occupied);
    CommandHistory renameHistory;
    renameHistory.execute(std::make_unique<MultiRenameInstanceCommand>(
        std::vector<MultiRenameInstanceCommand::Entry>{
            {renameA, "oldA", "base1"}, {renameB, "oldB", "base2"}}));
    expect(renameParent->getChild("base1") == renameA.get() &&
               renameParent->getChild("base") == occupied.get() &&
               renameParent->getChild("oldB") == renameB.get(),
           "multi rename skips runtime lock and preserves unselected sibling keys");
    renameHistory.undo();
    expect(renameParent->getChild("oldA") == renameA.get() &&
               renameParent->getChild("oldB") == renameB.get() &&
               renameParent->getChild("base") == occupied.get(),
           "multi rename undo restores every children key");
    renameHistory.redo();
    expect(renameParent->getChild("base1") == renameA.get() &&
               renameParent->getChild("oldB") == renameB.get(),
           "multi rename redo restores canonical keys");

    // A single command applies world transforms to Spatial children with
    // different parents, while BaseCube physics setters receive local values.
    auto transformRoot = std::make_shared<Workspace>();
    auto parentA = std::make_shared<Model>(Vector3(10, 0, 0));
    auto parentB = std::make_shared<Model>(Vector3(-4, 2, 3));
    parentB->Rotation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f);
    auto transformA = std::make_shared<Cube>(Vector3(1, 0, 0), Vector3(1, 2, 3), Cube::defaultTextureID);
    auto transformB = std::make_shared<Cube>(Vector3(0, 1, 0), Vector3(2, 2, 2), Cube::defaultTextureID);
    parentA->addChild(transformA); parentB->addChild(transformB);
    transformRoot->addChild(parentA); transformRoot->addChild(parentB);
    const CFrame beforeA = transformA->getWorldCFrame();
    const CFrame beforeB = transformB->getWorldCFrame();
    const Vector3 beforeSizeA = transformA->Size;
    const Vector3 beforeSizeB = transformB->Size;
    const CFrame afterA(Vector3(3, 4, 5), Quaternion::fromEuler(Vector3(0, 20, 0)));
    const CFrame afterB(Vector3(-2, 6, 1), Quaternion::fromEuler(Vector3(10, 0, 30)));
    CommandHistory transformHistory;
    transformHistory.execute(std::make_unique<MultiSpatialTransformCommand>(
        std::vector<MultiSpatialTransformCommand::Entry>{
            {transformA, beforeA, afterA, beforeSizeA, Vector3(4, 5, 6)},
            {transformB, beforeB, afterB, beforeSizeB, Vector3(7, 8, 9)}}));
    expect(positionDistance(transformA->getWorldCFrame().Position, afterA.Position) <= 0.001f &&
               positionDistance(transformB->getWorldCFrame().Position, afterB.Position) <= 0.001f &&
               transformA->Size == Vector3(4, 5, 6) && transformB->Size == Vector3(7, 8, 9),
           "multi spatial transform applies world CFrame and size across different parents");
    transformHistory.undo();
    expect(positionDistance(transformA->getWorldCFrame().Position, beforeA.Position) <= 0.001f &&
               positionDistance(transformB->getWorldCFrame().Position, beforeB.Position) <= 0.001f &&
               transformA->Size == beforeSizeA && transformB->Size == beforeSizeB,
           "multi spatial transform undo restores world poses and sizes");
    transformHistory.redo();
    expect(positionDistance(transformA->getWorldCFrame().Position, afterA.Position) <= 0.001f &&
               positionDistance(transformB->getWorldCFrame().Position, afterB.Position) <= 0.001f,
           "multi spatial transform redo restores world poses");

    auto scriptParent = std::make_shared<Folder>();
    auto script = std::make_shared<Script>();
    script->Path = "old.luau";
    script->Name = "Logic"; script->Source = "return 1"; script->Enabled = false;
    auto child = std::make_shared<Folder>(); child->Name = "Child";
    script->addChild(child); scriptParent->addChild(script);
    CommandHistory replacementHistory;
    replacementHistory.execute(std::make_unique<ReplaceInstanceCommand>(
        scriptParent, script, "LocalScript"));
    auto local = scriptParent->getChild("Logic");
    expect(local && local->IsA("LocalScript") && static_cast<Script*>(local)->Path == "old.luau" &&
               static_cast<Script*>(local)->Source == "return 1" && local->getChild("Child") == child.get(),
           "replacement preserves script properties and child identity");
    replacementHistory.undo();
    expect(scriptParent->getChild("Logic") == script.get() && script->getChild("Child") == child.get(),
           "replacement undo restores original script and child");
    replacementHistory.redo();
    expect(scriptParent->getChild("Logic") == local && local->getChild("Child") == child.get(),
           "replacement redo restores replacement and child identity");

    // Replacement reference contract: generic ObjectValue references update
    // for every destination, while typed BaseCube references are cleared for
    // an incompatible Script destination and restored by undo.
    auto system = std::make_shared<System>();
    auto refWorkspace = std::make_shared<Workspace>();
    system->addChild(refWorkspace);
    auto target = std::make_shared<Cube>(Vector3(2, 3, 4), Vector3(5, 6, 7), Cube::defaultTextureID);
    target->Name = "Target";
    target->Anchored = true;
    target->Color = Color4(0.2f, 0.4f, 0.6f, 1.0f);
    auto targetChild = std::make_shared<Folder>();
    targetChild->Name = "Child";
    target->addChild(targetChild);
    auto other = std::make_shared<Cube>(Vector3(0, 0, 0), Vector3(1, 1, 1), Cube::defaultTextureID);
    other->Name = "Other";
    auto objectValue = std::make_shared<ObjectValue>();
    objectValue->Name = "TargetValue";
    objectValue->setTarget(target);
    auto weld = std::make_shared<Weld>();
    weld->Name = "TargetWeld";
    weld->setCubes(target, other);
    refWorkspace->addChild(target);
    refWorkspace->addChild(other);
    refWorkspace->addChild(objectValue);
    refWorkspace->addChild(weld);

    CommandHistory refHistory;
    refHistory.execute(std::make_unique<ReplaceInstanceCommand>(
        refWorkspace, target, "Sphere", std::function<void(Instance*)>{}, system));
    auto sphere = refWorkspace->getChild("Target");
    expect(sphere && sphere->IsA("Sphere") && sphere->IsA("BaseCube"),
           "Cube replacement creates the requested Sphere");
    expect(sphere && static_cast<Spatial*>(sphere)->Size == target->Size &&
               static_cast<BaseCube*>(sphere)->Anchored &&
               static_cast<BaseCube*>(sphere)->Color == target->Color,
           "Cube replacement preserves common properties");
    expect(sphere && sphere->getChild("Child") == targetChild.get() &&
               objectValue->getTarget().get() == sphere &&
               weld->getCube0().get() == sphere,
           "Cube replacement preserves child identity and compatible references");
    refHistory.undo();
    expect(refWorkspace->getChild("Target") == target.get() &&
               target->getChild("Child") == targetChild.get() &&
               objectValue->getTarget().get() == target.get() &&
               weld->getCube0().get() == target.get(),
           "Cube replacement undo restores exact node, child, and references");
    refHistory.redo();
    sphere = refWorkspace->getChild("Target");
    expect(sphere && objectValue->getTarget().get() == sphere &&
               weld->getCube0().get() == sphere && sphere->getChild("Child") == targetChild.get(),
           "Cube replacement redo reapplies references and child identity");

    auto scriptReplacement = std::make_unique<ReplaceInstanceCommand>(
        refWorkspace, sphere->shared_from_this(), "Script", std::function<void(Instance*)>{}, system);
    auto scriptPreview = InstanceCatalog::create("Script");
    scriptReplacement->analyzeReferences(scriptPreview);
    expect(std::find(scriptReplacement->incompatibleReferenceOwners().begin(),
                     scriptReplacement->incompatibleReferenceOwners().end(),
                     "Weld.Cube0") != scriptReplacement->incompatibleReferenceOwners().end(),
           "Cube to Script preview reports incompatible Weld reference");
    refHistory.execute(std::move(scriptReplacement));
    auto replacedScript = refWorkspace->getChild("Target");
    expect(replacedScript && replacedScript->IsA("Script") && !weld->getCube0() &&
               objectValue->getTarget().get() == replacedScript,
           "Cube to Script clears typed reference but updates ObjectValue");
    refHistory.undo();
    expect(weld->getCube0().get() == sphere && objectValue->getTarget().get() == sphere,
           "Cube to Script undo restores typed and generic references");

    {
        auto highlightSystem = std::make_shared<System>();
        auto highlightWorkspace = std::make_shared<Workspace>();
        auto highlight = std::make_shared<Highlight>();
        highlight->Name = "Highlight";
        highlightWorkspace->addChild(highlight);
        highlightSystem->addChild(highlightWorkspace);
        LuauEngine highlightEngine;
        highlightEngine.setWorkspace(highlightWorkspace);
        highlightEngine.setSystem(highlightSystem.get());
        highlightEngine.setGlobalInstance("workspace", highlightWorkspace);
        auto highlightScript = std::make_shared<Script>();
        highlightScript->Source =
            "local h = workspace:FindChild('Highlight') "
            "h.FillColor = Color4.new(1, 0, 0, 0.25) "
            "h.OutlineColor = Color4.new(0, 1, 0, 1) "
            "h.OutlineThickness = 99 "
            "if h.OutlineThickness == 10 then print('[HighlightPass]') end";
        highlightWorkspace->addChild(highlightScript);
        const bool executed = highlightEngine.execute(*highlightScript);
        expect(executed && highlight->FillColor == Color4(1, 0, 0, 0.25f) &&
                   highlight->OutlineColor == Color4(0, 1, 0, 1) &&
                   highlight->OutlineThickness == 10.0f,
               "Highlight Luau read/write and OutlineThickness clamp contract");
    }

    {
        auto fontSystem = std::make_shared<System>();
        auto fontWorkspace = std::make_shared<Workspace>();
        auto font = std::make_shared<FontFile>();
        auto label = std::make_shared<TextLabel>();
        font->Name = "FontFile";
        label->Name = "Label";
        label->FontFile = "Workspace\\FontFile";
        fontWorkspace->addChild(font);
        fontWorkspace->addChild(label);
        fontSystem->addChild(fontWorkspace);
        auto fontScriptPreview = InstanceCatalog::create("Script");
        ReplaceInstanceCommand fontReplacement(
            fontWorkspace, font, "Script", std::function<void(Instance*)>{}, fontSystem);
        fontReplacement.analyzeReferences(fontScriptPreview);
        const auto& incompatible = fontReplacement.incompatibleReferenceOwners();
        expect(std::find_if(incompatible.begin(), incompatible.end(), [](const std::string& owner) {
                   return owner.ends_with(".FontFile");
               }) != incompatible.end(),
               "FontFile to Script preview reports TextLabel.FontFile incompatibility");

        const auto fontScenePath = std::filesystem::temp_directory_path() /
            ("recubin_font_reference_replace_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
        {
            std::ofstream scene(fontScenePath);
            scene << "Root:\n"
                     "  Children:\n"
                     "    - ClassName: Workspace\n"
                     "      Name: Workspace\n"
                     "      Children:\n"
                     "        - ClassName: FontFile\n"
                     "          Name: FontFile\n"
                     "        - ClassName: TextLabel\n"
                     "          Name: Label\n"
                     "          Properties:\n"
                     "            FontFile: 'Workspace\\FontFile'\n";
        }
        auto fontContextSystem = std::make_shared<System>();
        SceneLoader::LoadContext fontContext;
        fontContext.registerMergeInstance("System", fontContextSystem);
        const auto loadedFontScene = SceneLoader::loadSceneResult(fontScenePath.string(), fontContext);
        auto loadedSystem = loadedFontScene.root
            ? std::dynamic_pointer_cast<System>(loadedFontScene.root)
            : nullptr;
        auto loadedFont = loadedSystem
            ? loadedSystem->getChildByPath("Workspace\\FontFile")
            : nullptr;
        auto loadedWorkspace = loadedSystem && loadedFont && loadedFont->Parent.lock()
            ? std::dynamic_pointer_cast<Workspace>(loadedFont->Parent.lock())
            : nullptr;
        auto loadedLabel = loadedSystem
            ? dynamic_cast<TextLabel*>(loadedSystem->getChildByPath("Workspace\\Label"))
            : nullptr;
        expect(loadedFontScene && loadedSystem && loadedWorkspace && loadedFont && loadedLabel,
               "FontFile reference fixture loads through SceneLoader");
        expect(loadedLabel && loadedLabel->FontFile == "Workspace\\FontFile" &&
                   loadedSystem->getChildByPath("Workspace\\FontFile") == loadedFont,
               "loaded TextLabel retains Workspace-relative FontFile reference");
        auto loadedPreview = InstanceCatalog::create("Script");
        ReplaceInstanceCommand loadedReplacement(
            loadedWorkspace, loadedFont->shared_from_this(), "Script",
            std::function<void(Instance*)>{}, loadedSystem);
        loadedReplacement.analyzeReferences(loadedPreview);
        const auto& loadedIncompatible = loadedReplacement.incompatibleReferenceOwners();
        expect(std::find_if(loadedIncompatible.begin(), loadedIncompatible.end(), [](const std::string& owner) {
                   return owner.ends_with(".FontFile");
               }) != loadedIncompatible.end(),
               "loaded FontFile to Script preview reports TextLabel.FontFile incompatibility");
        std::error_code fontSceneError;
        std::filesystem::remove(fontScenePath, fontSceneError);
    }

    {
        auto force = std::make_shared<Force>();
        auto forceHighlight = std::make_shared<Highlight>();
        force->Enabled = false;
        forceHighlight->Enabled = true;
        PropertyRegistry::copyCompatibleProperties(force.get(), forceHighlight.get());
        expect(!forceHighlight->Enabled,
               "unrelated Force to Highlight copies same-name same-type Enabled");
        auto vectorForce = std::make_shared<Force>();
        auto integerValue = std::make_shared<IntValue>();
        vectorForce->Value = Vector3(7, 8, 9);
        integerValue->Value = 42;
        PropertyRegistry::copyCompatibleProperties(vectorForce.get(), integerValue.get());
        expect(integerValue->Value == 42,
               "Force Vector3 Value does not overwrite IntValue integer Value");
    }
    std::cout << "[Grouping] failures=" << failures << " result="
              << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

static int runSystemExtensionRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const std::string& message) {
        std::cout << "[SystemExtension] " << (condition ? "PASS: " : "FAIL: ")
                  << message << '\n';
        if (!condition) ++failures;
    };
    const auto root = std::filesystem::temp_directory_path() /
        ("recubin_system_extension_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto applicationId = RecubinUUID::generate();
    RuntimeFileSystem fs(applicationId, RuntimeFileSystem::Namespace::Runtime, false, root);
    expect(static_cast<bool>(fs.write("a.bin", std::string("a\0b", 3))), "binary write succeeds");
    auto bytes = fs.read("a.bin");
    expect(static_cast<bool>(bytes) && bytes.value.size() == 3 && bytes.value[1] == '\0', "binary NUL round-trip");
    expect(static_cast<bool>(fs.append("a.bin", "c")), "append succeeds");
    expect(fs.isFile("a.bin").isFile && !fs.isDirectory("a.bin").isDirectory,
           "typed file/directory results");
    expect(static_cast<bool>(fs.createDirectory("dir")), "directory creation succeeds");
    expect(static_cast<bool>(fs.copy("a.bin", "dir/copy.bin")), "copy succeeds");
    std::vector<RuntimeFileEntry> entries;
    expect(static_cast<bool>(fs.list("dir", entries)) && entries.size() == 1 && entries.front().name == "copy.bin",
           "deterministic directory listing");
    expect(!static_cast<bool>(fs.read("../escape")), "parent traversal rejected");
    expect(!static_cast<bool>(fs.removeTree(".")), "runtime namespace root cannot be removed");
    TextFile file;
    file.Path = "seed.txt";
    expect(static_cast<bool>(fs.writeTextFile(file.StorageId, "persisted")), "TextFile overlay write succeeds");
    auto persisted = fs.readTextFile(file.Path, file.StorageId);
    expect(static_cast<bool>(persisted) && persisted.value == "persisted", "TextFile overlay read succeeds");
    RuntimeFileSystem editorFs(applicationId, RuntimeFileSystem::Namespace::Editor, false, root);
    expect(editorFs.namespaceRoot() != fs.namespaceRoot(), "runtime/editor namespaces are isolated");
    expect(!static_cast<bool>(fs.write("oversize.bin", std::string(128u * 1024u * 1024u + 1u, 'x'))),
           "files over 128 MiB are rejected");
    expect(!static_cast<bool>(fs.copy("a.bin", "dir/copy.bin")) &&
               static_cast<bool>(fs.copy("a.bin", "dir/copy.bin", true)),
           "copy overwrite policy is enforced");
    expect(static_cast<bool>(fs.removeTree("dir")), "recursive removal succeeds");
    RuntimeFileSystem externalFs(applicationId, RuntimeFileSystem::Namespace::Runtime,
                                 true, root / "external-base");
    const auto absoluteTarget = root / "absolute.bin";
    expect(static_cast<bool>(externalFs.write(absoluteTarget.string(), "external")) &&
               externalFs.isFile(absoluteTarget.string()).isFile,
           "external access permits absolute paths");
    auto system = std::make_shared<System>();
    system->ApplicationId = applicationId;
    expect(!system->EnableIOAPI && !system->EnableIPCAPI &&
               !system->EnableExternalFileAccess && !system->ApplicationId.empty(),
           "System extension defaults are disabled with an application id");
    system->EnableIOAPI = true;
    system->EnableIPCAPI = true;
    system->EnableExternalFileAccess = true;
    const auto systemYaml = root / "system.yaml";
    expect(SceneLoader::saveSceneResult(system.get(), systemYaml.string()),
           "System extension YAML save succeeds");
    auto mergedSystem = std::make_shared<System>();
    SceneLoader::LoadContext loadContext;
    loadContext.registerMergeInstance("System", mergedSystem);
    const auto loadedSystem = SceneLoader::loadSceneResult(systemYaml.string(), loadContext);
    expect(loadedSystem && mergedSystem->ApplicationId == system->ApplicationId &&
               mergedSystem->EnableIOAPI && mergedSystem->EnableIPCAPI &&
               mergedSystem->EnableExternalFileAccess,
           "System extension YAML round-trip preserves values");
    const auto consentRoot = fs.namespaceRoot().parent_path();
    SystemExtensionPermissions consentPermissions{true, false, false};
    expect(SystemExtensionConsent::shouldWarn(consentRoot, consentPermissions),
           "extension consent warns on first launch");
    expect(SystemExtensionConsent::write(consentRoot, consentPermissions) &&
               !SystemExtensionConsent::shouldWarn(consentRoot, consentPermissions),
           "same extension consent does not warn again");
    SystemExtensionPermissions changedPermissions{true, true, false};
    expect(SystemExtensionConsent::shouldWarn(consentRoot, changedPermissions),
           "extension consent warns when configuration changes");
    SystemExtensionPermissions readPermissions;
    expect(SystemExtensionConsent::read(consentRoot, readPermissions) &&
               readPermissions.io && !readPermissions.ipc && !readPermissions.external,
           "extension consent round-trip preserves exact set");

    // Exercise the Luau permission boundary and the read-only System fields.
    LuauEngine extensionEngine;
    system->EnableIOAPI = false;
    system->EnableIPCAPI = false;
    system->EnableExternalFileAccess = false;
    extensionEngine.setSystem(system.get());
    extensionEngine.setRuntimeFileSystem(std::make_shared<RuntimeFileSystem>(
        applicationId, RuntimeFileSystem::Namespace::Runtime, true, root));
    auto extensionWorkspace = std::make_shared<Workspace>();
    system->addChild(extensionWorkspace);
    extensionEngine.setWorkspace(extensionWorkspace);
    const auto seedPath = root / "seed.txt";
    { std::ofstream seed(seedPath, std::ios::binary); seed << "seed"; }
    auto textFile = std::make_shared<TextFile>();
    textFile->Path = seedPath.string();
    textFile->Name = "SaveData";
    extensionWorkspace->addChild(textFile);
    extensionEngine.setGlobalInstance("SaveData", textFile);
    auto permissionScript = std::make_shared<Script>();
    permissionScript->Source =
        "assert(pcall(function() IO.Exists('x') end) == false) "
        "assert(pcall(function() IPC.Connect('x') end) == false) "
        "System.EnableIOAPI = true System.EnableIPCAPI = true "
        "System.EnableExternalFileAccess = true System.ApplicationId = 'mutate'";
    extensionEngine.setGlobalInstance("System", system);
    expect(extensionEngine.execute(*permissionScript),
           "Luau IO permission and read-only System assignments are handled");
    expect(!system->EnableIOAPI && !system->EnableExternalFileAccess &&
               system->ApplicationId == applicationId,
           "System extension fields remain read-only from Luau");
    system->EnableIOAPI = true;
    auto ioScript = std::make_shared<Script>();
    ioScript->Source =
        "IO.WriteBytes('nul.bin', 'a\\0b') "
        "assert(IO.ReadBytes('nul.bin') == 'a\\0b')";
    expect(extensionEngine.execute(*ioScript), "Luau enabled IO binary round-trip succeeds");
    system->EnableIOAPI = false;
    system->EnableIPCAPI = true;
    auto ipcScript = std::make_shared<Script>();
    ipcScript->Source = "assert(pcall(function() IPC.Connect('x') end) == false)";
    expect(extensionEngine.execute(*ipcScript), "enabled IPC reports not implemented");
    auto contentScript = std::make_shared<Script>();
    contentScript->Source =
        "assert(SaveData.Content == 'seed') SaveData.Content = 'overlay' "
        "assert(SaveData.Content == 'overlay') assert(SaveData.StorageId == nil) "
        "assert(pcall(function() SaveData:Clone() end) == false) "
        "assert(pcall(function() Instance.new('TextFile') end) == false)";
    expect(extensionEngine.execute(*contentScript),
           "TextFile Content overlay works without exposing StorageId");
    std::filesystem::remove_all(root, ec);
    return failures;
}

static int runSystemExtensionSmokePackaging(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "usage: RecubinTest --package-system-extension-smoke <output-dir>\n";
        return 2;
    }
    Packager::Config config;
    config.gameName = "SystemExtensionSmokePackage";
    config.applicationId = "9b4d2c11-5e73-4a6f-8c20-1d9f7b3e6a42";
    config.scenePath = "TestCases/SystemExtensionSmoke/SystemExtensionSmoke.yaml";
    config.outputDir = argv[2];
#ifdef __APPLE__
    config.engineExePath =
        (std::filesystem::current_path() / "build-mac/Recubin").string();
#else
    config.engineExePath =
        (std::filesystem::current_path() / "build/Release/Recubin.exe").string();
#endif
    const bool packaged = Packager::package(config, [](const std::string& message) {
        std::cout << "[SystemExtensionPackage] " << message << '\n';
    });
    return packaged ? 0 : 1;
}

static int runPhysicalFileInstanceRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const std::string& message) {
        std::cout << "[PhysicalFileInstance] "
                  << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition) ++failures;
    };

    auto directFontFile = std::make_shared<FontFile>();
    YAML::Node directPath;
    directPath = "assets\\fonts\\direct.ttf";
    directFontFile->setProperty("ContentPath", directPath);
    auto directClone = std::dynamic_pointer_cast<PhysicalFileInstance>(
        directFontFile->clone());
    expect(directFontFile->Path == "assets/fonts/direct.ttf" && directClone &&
               directClone->Path == directFontFile->Path,
           "direct construction initializes the shared schema before setProperty/clone");

    const auto& types = PhysicalFileInstanceRegistry::types();
    expect(!types.empty(), "registry exposes at least one physical file type");
    const auto& screenGuiSchema = PropertyRegistry::schemaFor("ScreenGuiObject");
    const auto fontRef = std::find_if(screenGuiSchema.begin(), screenGuiSchema.end(),
        [](const PropertyDesc& desc) { return desc.name == "FontFile"; });
    expect(fontRef != screenGuiSchema.end() &&
               fontRef->instanceRefClass == "FontFile",
           "TextLabel FontFile uses the typed instance-reference metadata");

    const auto hasSignal = [](std::string_view className, std::string_view signalName) {
        const auto schema = PropertyRegistry::collectSchema(className);
        return std::any_of(schema.begin(), schema.end(), [&](const PropertyDesc* desc) {
            return desc->kind == PropKind::Signal && desc->name == signalName;
        });
    };
    expect(hasSignal("TextButton", "HoverEnded"),
           "TextButton exposes GuiButton HoverEnded signal schema");
    expect(hasSignal("ImageButton", "HoverEnded"),
           "ImageButton exposes GuiButton HoverEnded signal schema");

    bool hasFileRef = false;
    bool hasFontFile = false;
    auto system = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    system->addChild(workspace);
    std::vector<std::pair<std::string, std::string>> expectedPaths;

    for (const auto& type : types) {
        const std::string className(type.className);
        hasFileRef = hasFileRef || className == "FileRef";
        hasFontFile = hasFontFile || className == "FontFile";

        if (className == "TextFile")
            expect(!type.luaCreatable,
                   "TextFile is scene-owned and not Luau-creatable");

        auto created = PhysicalFileInstanceRegistry::create(type.className);
        expect(created && created->getClassName() == className &&
                   created->IsA(className) && created->IsA("PhysicalFileInstance") &&
                   created->IsA("Instance"),
               "registry create preserves class and IsA chain for " + className);

        auto sceneCreated = SceneLoader::createInstance(className);
        expect(sceneCreated && sceneCreated->getClassName() == className &&
                   sceneCreated->IsA("PhysicalFileInstance"),
               "SceneLoader factory delegates " + className + " to the registry");
        if (!created) continue;

        created->Name = className + "Asset";
        const std::string inputPath =
            "assets\\physical\\" + className + "\\日本語ファイル.dat";
        const std::string storedPath = AssetPath::normalize(inputPath);
        YAML::Node pathNode;
        pathNode = inputPath;
        created->setProperty("ContentPath", pathNode);
        expect(created->Path == storedPath,
               "ContentPath setter normalizes Path for " + className);

        auto clonedBase = created->clone();
        auto cloned = std::dynamic_pointer_cast<PhysicalFileInstance>(clonedBase);
        expect(cloned && cloned->getClassName() == className &&
                   cloned->Path == storedPath && cloned->Name == created->Name,
               "clone preserves type, Path, and Name for " + className);
        if (className == "TextFile") {
            auto* text = static_cast<TextFile*>(created.get());
            auto* textClone = dynamic_cast<TextFile*>(cloned.get());
            expect(textClone && textClone->StorageId != text->StorageId,
                   "TextFile clone receives a distinct StorageId");
        }

        expectedPaths.emplace_back(created->Name, storedPath);
        workspace->addChild(created);
    }
    expect(hasFileRef && hasFontFile,
           "registry retains the FileRef and FontFile compatibility class names");

    const auto scenePath = std::filesystem::temp_directory_path() /
        ("recubin_physical_file_instance_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".yaml");
    SceneLoader::saveScene(system.get(), scenePath.string());
    const std::string savedYaml = FileLoader::readText(scenePath.string());
    expect(savedYaml.find("ClassName: FileRef") != std::string::npos &&
               savedYaml.find("ClassName: FontFile") != std::string::npos &&
               savedYaml.find("ContentPath:") != std::string::npos,
           "YAML keeps FileRef/FontFile class names and the ContentPath key");

    auto loadedRoot = SceneLoader::loadScene(scenePath.string());
    for (const auto& [name, expectedPath] : expectedPaths) {
        Instance* loadedNode = loadedRoot
            ? loadedRoot->getChildByPath("Workspace\\" + name)
            : nullptr;
        auto* loadedFile = loadedNode && loadedNode->IsA("PhysicalFileInstance")
            ? static_cast<PhysicalFileInstance*>(loadedNode)
            : nullptr;
        const std::string expectedClass =
            name.ends_with("Asset") ? name.substr(0, name.size() - 5) : name;
        expect(loadedFile && loadedFile->getClassName() == expectedClass &&
                   loadedFile->Path == expectedPath,
               "Scene YAML round-trip preserves ContentPath for " + expectedClass);
    }
    std::error_code removeError;
    std::filesystem::remove(scenePath, removeError);
    expect(!removeError, "temporary physical-file Scene is removed");

    {
        LuauEngine engine;
        auto luauSystem = std::make_shared<System>();
        auto luauWorkspace = std::make_shared<Workspace>();
        luauSystem->addChild(luauWorkspace);
        engine.setWorkspace(luauWorkspace);
        engine.setSystem(luauSystem.get());

        std::unordered_set<std::string> luauCreated;
        const auto oldLogHook = g_luauLogHook;
        g_luauLogHook = [&](const std::string& message) {
            constexpr std::string_view PREFIX = "[PhysicalFileLuau:";
            const size_t begin = message.find(PREFIX);
            if (begin == std::string::npos) return;
            const size_t nameBegin = begin + PREFIX.size();
            const size_t end = message.find(']', nameBegin);
            if (end != std::string::npos)
                luauCreated.insert(message.substr(nameBegin, end - nameBegin));
        };

        auto script = std::make_shared<Script>();
        script->Name = "PhysicalFileInstanceLuauFactory";
        for (const auto& type : types) {
            const std::string className(type.className);
            if (!type.luaCreatable) continue;
            script->Source +=
                "do local file = Instance.new('" + className + "') "
                "if file and file:IsA('" + className +
                "') and file:IsA('PhysicalFileInstance') and type(file.Path) == 'string' "
                "then print('[PhysicalFileLuau:" + className + "]') end end\n";
        }
        const bool executed = engine.execute(*script);
        g_luauLogHook = oldLogHook;
        expect(executed, "Luau physical-file factory script executes");
        for (const auto& type : types) {
            const std::string className(type.className);
            if (!type.luaCreatable) continue;
            expect(luauCreated.contains(className),
                   "Luau Instance.new creates " + className +
                       " and exposes readable Path");
        }
    }

    std::cout << "[PhysicalFileInstance] failures=" << failures
              << " result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}

namespace {
int* g_audioEngineInit = nullptr;
int* g_audioGroupInit = nullptr;
int* g_audioGroupUninit = nullptr;
int* g_audioEngineUninit = nullptr;
int g_audioFailAt = 0;
bool g_audioEngineFails = false;
ma_result testAudioEngineInit(const ma_engine_config*, ma_engine*) {
    ++*g_audioEngineInit; return g_audioEngineFails ? MA_ERROR : MA_SUCCESS;
}
ma_result testAudioGroupInit(ma_engine*, ma_uint32, ma_sound_group*, ma_sound_group*) {
    ++*g_audioGroupInit; return g_audioFailAt == *g_audioGroupInit ? MA_ERROR : MA_SUCCESS;
}
void testAudioGroupUninit(ma_sound_group*) { ++*g_audioGroupUninit; }
void testAudioEngineUninit(ma_engine*) { ++*g_audioEngineUninit; }
}

int runAudioServiceInitializationRegression() {
    int failures = 0;
    int engineInit = 0, groupInit = 0, groupUninit = 0, engineUninit = 0;
    g_audioEngineInit = &engineInit; g_audioGroupInit = &groupInit;
    g_audioGroupUninit = &groupUninit; g_audioEngineUninit = &engineUninit;
    g_audioFailAt = 2;
    AudioService::InitializationOps ops{testAudioEngineInit, testAudioGroupInit,
                                        testAudioGroupUninit, testAudioEngineUninit};
    AudioService service(ops);
    const bool first = service.initialize();
    service.uninit(); service.uninit();
    if (first || engineInit != 1 || groupInit != 2 || groupUninit != 1 || engineUninit != 1 ||
        AudioService::instance != nullptr) ++failures;
    engineInit = groupInit = groupUninit = engineUninit = 0;
    g_audioFailAt = 1;
    AudioService bgmFailure(ops);
    if (bgmFailure.initialize() || engineInit != 1 || groupInit != 1 ||
        groupUninit != 0 || engineUninit != 1 || AudioService::instance != nullptr) ++failures;
    engineInit = groupInit = groupUninit = engineUninit = 0;
    g_audioFailAt = 0;
    AudioService success(ops);
    const bool initialized = success.initialize();
    const bool reinitialized = success.initialize();
    success.uninit(); success.uninit();
    if (!initialized || !reinitialized || engineInit != 1 || groupInit != 2 ||
        groupUninit != 2 || engineUninit != 1 || AudioService::instance != nullptr) ++failures;
    engineInit = groupInit = groupUninit = engineUninit = 0;
    g_audioFailAt = 0; g_audioEngineFails = true;
    AudioService engineFailure(ops);
    if (engineFailure.initialize() || engineInit != 1 || groupInit != 0 ||
        engineUninit != 0 || AudioService::instance != nullptr) ++failures;
    g_audioEngineFails = false; g_audioFailAt = -1;
    AudioService firstService(ops);
    AudioService secondService(ops);
    if (!firstService.initialize()) ++failures;
    const int engineAfterFirst = engineInit;
    const int groupsAfterFirst = groupInit;
    if (secondService.initialize() || engineInit != engineAfterFirst ||
        groupInit != groupsAfterFirst ||
        AudioService::instance != &firstService) ++failures;
    firstService.uninit(); secondService.uninit();
    std::cout << "[AudioServiceInitialization] " << (failures == 0 ? "PASS" : "FAIL")
              << ": initialization and idempotent teardown\n";
    return failures == 0 ? 0 : 1;
}

int runAudioServiceRegistrationRegression() {
    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        std::cout << "[AudioServiceRegistration] "
                  << (condition ? "PASS" : "FAIL") << ": " << name << "\n";
        if (!condition) ++failures;
    };

    AudioService* const previousInstance = AudioService::instance;
    AudioService::instance = nullptr;
    AudioService service;
    AudioService::instance = &service;
    auto workspace = std::make_shared<Workspace>();
    auto firstModel = std::make_shared<Model>(Vector3());
    auto secondModel = std::make_shared<Model>(Vector3());
    firstModel->Name = "FirstModel";
    secondModel->Name = "SecondModel";
    auto sound = std::make_shared<Sound>(service);
    workspace->addChild(firstModel);
    workspace->addChild(secondModel);
    firstModel->addChild(sound);
    expect(service.registeredSoundCount() == 1,
           "未初期化AudioServiceでもWorkspace内Soundを登録する");
    secondModel->addChild(sound);
    expect(service.registeredSoundCount() == 1,
           "同一SoundのWorkspace内reparentで登録数が1のままになる");
    sound->setParent(nullptr);
    expect(service.registeredSoundCount() == 0,
           "Workspace外detachでSound登録が解除される");

    {
        auto transient = std::make_shared<Sound>(service);
        service.addSound(transient);
        transient.reset();
        auto retained = std::make_shared<Sound>(service);
        service.addSound(retained);
        expect(service.registeredSoundCount() == 1,
               "次のSound登録時にexpired weak参照を掃除する");
        service.removeSound(retained);
        expect(service.registeredSoundCount() == 0,
               "Sound削除時にexpired weak参照も掃除する");
    }

    AudioService::instance = previousInstance;
    std::cout << "[AudioServiceRegistration] "
              << (failures == 0 ? "PASS" : "FAIL") << "\n";
    return failures == 0 ? 0 : 1;
}

int runYamlErrorRegression() {
    int failures = 0;
    std::error_code error;
    const auto path = std::filesystem::temp_directory_path() / "recubin_yaml_error_regression.yaml";
    const std::string original = "broken: [1, 2\n";
    { std::ofstream file(path, std::ios::binary); file << original; }
    const auto loaded = loadYamlFile(path.string());
    if (loaded.success || !loaded.loadFailed || loaded.error.empty()) ++failures;
    const auto before = FileLoader::readText(path.string());
    const auto saved = saveYamlFileGuarded(path.string(), YAML::Node(YAML::NodeType::Map), loaded.loadFailed);
    const auto after = FileLoader::readText(path.string());
    if (saved.success || saved.error.empty() || before != after) ++failures;
    const auto textLoaded = loadYamlText("broken: [1, 2\n", "startup.yaml");
    if (textLoaded.success || textLoaded.error.empty()) ++failures;
    const auto terrainDir = std::filesystem::temp_directory_path() / "recubin_yaml_error_terrain";
    std::filesystem::create_directories(terrainDir);
    const auto regionPath = terrainDir / "r_0_0.yaml";
    const std::string regionContent = "chunks: [broken\n";
    { std::ofstream region(regionPath, std::ios::binary); region << regionContent; }
    {
        auto terrainWorkspace = std::make_shared<Workspace>();
        TerrainStreamer streamer(terrainWorkspace.get(), nullptr, terrainDir.string(), 12345u, false);
        if (!streamer.validateRegionLoadFailure(0, 0)) ++failures;
    }
    if (FileLoader::readText(regionPath.string()) != regionContent) ++failures;
    std::filesystem::remove_all(terrainDir, error);
    std::filesystem::remove(path, error);
    std::cout << "[YamlErrorRegression] " << (failures == 0 ? "PASS" : "FAIL")
              << ": malformed YAML is reported and guarded save preserves content\n";
    return failures == 0 ? 0 : 1;
}

struct RegressionEntry {
    std::string_view name;
    int (*runner)(int, char**);
    bool sceneHarness;
};

int dedicatedReturn(int result);

const std::vector<RegressionEntry>& regressionRegistry() {
    static const std::vector<RegressionEntry> entries = {
#define REG(name, function) {name, [](int, char**) { return dedicatedReturn(function()); }, false}
        REG("--sound-stretch-regression", runSoundStretchRegression),
        REG("--audio-service-initialization-regression", runAudioServiceInitializationRegression),
        REG("--audio-service-registration-regression", runAudioServiceRegistrationRegression),
        REG("--yaml-error-regression", runYamlErrorRegression),
        REG("--nat-codec-regression", runNatCodecRegression),
        REG("--animation-clip-regression", runAnimationClipRegression),
        REG("--scene-load-transaction-regression", runSceneLoadTransactionRegression),
        REG("--system-extension-regression", runSystemExtensionRegression),
        REG("--scene-hierarchy-grouping-regression", runSceneHierarchyGroupingRegression),
        REG("--gui-automation-regression", runGuiAutomationRegression),
        REG("--physics-migration-regression", runPhysicsMigrationRegression),
        REG("--physics-lifecycle-regression", runPhysicsLifecycleRegression),
        REG("--constraint-rebind-regression", runConstraintRebindRegression),
        REG("--terrain-instance-regression", runTerrainInstanceRegression),
        REG("--fixed-step-force-regression", runFixedStepForceRegression),
        REG("--contact-reentry-regression", runContactReentryRegression),
        REG("--network-core-regression", runNetworkCoreRegression),
        REG("--multi-workspace-regression", runMultiWorkspaceRegression),
        REG("--physics-rollback-regression", runPhysicsRollbackRegression),
        REG("--box3d-hull-regression", runBox3DHullRegression),
        REG("--box3d-buoyancy-regression", runBox3DBuoyancyRegression),
        REG("--frame-rate-invariance-regression", runFrameRateInvarianceRegression),
        REG("--user-character-smoothing-regression", runUserCharacterSmoothingRegression),
        REG("--user-input-controls-regression", runUserInputControlsRegression),
        REG("--viewport-helper-regression", runViewportHelperRegression),
        REG("--asset-path-regression", runAssetPathRegression),
        REG("--app-image-regression", runAppImageRegression),
        REG("--runtime-launch-args-regression", runRuntimeLaunchArgsRegression),
        REG("--starter-weld-rename-regression", runStarterWeldRenameRegression),
        REG("--starter-accessory-weld-regression", runStarterAccessoryWeldRegression),
        REG("--starter-root-spawn-regression", runStarterRootSpawnRegression),
        REG("--spawn-location-regression", runSpawnLocationRegression),
        REG("--remote-avatar-spawn-transform-regression", runRemoteAvatarSpawnTransformRegression),
        REG("--meshcube-fallback-regression", runMeshCubeFallbackRegression),
        REG("--shadow-mode-regression", runShadowModeRegression),
        REG("--humanoid-rig-collision-regression", runHumanoidRigCollisionRegression),
        REG("--seat-network-regression", runSeatNetworkRegression),
        REG("--physical-file-instance-regression", runPhysicalFileInstanceRegression),
        REG("--surface-mark-regression", runSurfaceMarkRegression),
        REG("--tool-weld-regression", runToolWeldRegression),
        REG("--tool-weld-reequip-regression", runToolWeldReequipRegression),
        REG("--tool-respawn-regression", runToolRespawnRegression),
        REG("--inventory-tool-sync-regression", runInventoryToolSyncRegression),
        REG("--humanoid-part-ref-regression", runHumanoidPartRefRegression),
        {"--physics-performance-guard", [](int argc, char** argv) {
            return dedicatedReturn(runPhysicsPerformanceGuard(argc, argv));
        }, false},
        {"--weld-regression", nullptr, true},
#undef REG
    };
    return entries;
}

int dedicatedReturn(int result) {
    std::cout << "[RecubinTest] " << (result == 0 ? 1 : 0) << " passed, "
              << (result == 0 ? 0 : 1) << " failed.\n";
    return result;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string_view(argv[1]) == "--list-regressions") {
        for (const auto& entry : regressionRegistry()) std::cout << entry.name << '\n';
        return 0;
    }
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

    if (argc > 1 && std::string_view(argv[1]) == "--package-system-extension-smoke")
        return runSystemExtensionSmokePackaging(argc, argv);

    if (argc > 1) {
        const std::string_view requested(argv[1]);
        const auto& registry = regressionRegistry();
        const auto entry = std::find_if(registry.begin(), registry.end(),
            [requested](const RegressionEntry& candidate) { return candidate.name == requested; });
        if (entry != registry.end() && entry->runner && !entry->sceneHarness)
            return entry->runner(argc, argv);
        if (entry == registry.end() && requested.starts_with("--") &&
            requested.find("regression") != std::string_view::npos) {
            std::cerr << "[RecubinTest] Unknown regression mode: " << requested << '\n';
            return 2;
        }
    }

    const RegressionEntry* selectedEntry = nullptr;
    if (argc > 1) {
        const std::string_view requested(argv[1]);
        const auto& registry = regressionRegistry();
        const auto it = std::find_if(registry.begin(), registry.end(),
            [requested](const RegressionEntry& candidate) { return candidate.name == requested; });
        if (it != registry.end()) selectedEntry = &*it;
    }
    const bool weldRegression = selectedEntry && selectedEntry->sceneHarness;
    const char* sceneArgument = findSceneArgument(argc, argv, weldRegression ? 2 : 1);
    std::string scenePath = sceneArgument
        ? sceneArgument
        : (weldRegression ? "assets/scenes/test_weld_chain.yaml" : "assets/scenes/test_bindings.yaml");

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
    if (!audioService->initialize()) {
        std::cerr << "[RecubinTest] ERROR: AudioService initialization failed\n";
        return dedicatedReturn(1);
    }

    auto system    = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    auto lighting  = std::make_shared<Lighting>();
    lighting->Name = "Lighting";
    system->addChild(workspace);
    workspace->addChild(lighting);

    SceneLoader::LoadContext loadContext;
    loadContext.registerMergeInstance("System", system);
    loadContext.registerMergeInstance("Workspace", workspace);
    auto users = std::make_shared<Users>();
    users->Name = "Users";
    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>(), true);
    user->Name = "User";
    users->addChild(user);
    system->addChild(users);
    loadContext.registerMergeInstance("Users", users);
    loadContext.registerMergeInstance("User", user);
    loadContext.registerMergeInstance("Lighting", lighting);
    SceneLoader::loadScene(scenePath, loadContext);
    if (auto inventory = user->getChild("Inventory")) {
        if (auto folder = std::dynamic_pointer_cast<Folder>(inventory->shared_from_this()))
            user->Inventory = folder;
    }
    if (!user->Inventory || user->Inventory->Parent.lock().get() != user.get())
        user->initializeInventory();

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
        return dedicatedReturn(runWeldRegression(workspace));
    }

    auto luauEngine = std::make_unique<LuauEngine>();
    luauEngine->setGlobalInstance(workspace->Name, workspace);
    luauEngine->setGlobalInstance("workspace", workspace);
    luauEngine->setGlobalInstance("System", system);
    luauEngine->setGlobalInstance("User", user);
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
