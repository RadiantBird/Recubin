#pragma once

#include <include/Math/Matrix4.hpp>
#include <include/Math/Quaternion.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Model.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Folder.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/UserInput.hpp>
#include <Core/IInputBackend.hpp>
#include <Instances/Tool.hpp>

#include <cmath>
#include <array>
#include <memory>

struct camera {
    Quaternion Orientation;
    Vector3 Position = Vector3(0, -2, 5); // ただのダミーデータ
};

class User : public Instance {
public:
    float speed = 0.25f;
    float rotationSpeed = 1.0f;
    float cameraDistance = 10.0f;
    float zoomSpeed = 0.1f;
    float mouseZoomSpeed = 1.0f; // キーボードより早めに
    bool allowControlModeSwitch = true; // false のとき L キーをブロック
    camera current_camera;

    // 一人称視点
    float firstPersonThreshold = 1.5f; // この値以下のcameraDistanceで一人称視点に入る
    float minCameraDistance = 0.5f;    // processZoomの最小クランプ値（既存の2.0fから変更）

    // CtrlLock(Roblox ShiftLock相当)
    float ctrlLockOffsetDistance = 2.0f;

    camera &cam;
    Vector3 &cpos;
    std::shared_ptr<Model> character = nullptr;     // clone後のキャラクター本体(PlayerCharacter)
    std::shared_ptr<Humanoid> humanoid = nullptr;   // character内から名前解決される

    // User.Input (Roblox UserInputService 相当)。入力供給源を借用してポーリングする
    std::shared_ptr<UserInput> Input;

    bool processCameraRotation(bool viewportFocused);
    void processZoom(bool viewportZoomEnabled);
    void processMovement(bool viewportFocused, Physics* physics);
    void processHotkeys();
    void processToolkeys(bool viewportFocused, bool isGameplayInput, bool wantsTextInput);
    void processMouse(bool isGameplayInput);

    // TODO: インベントリにToolじゃないものがあったら無視するようにする
    // 多分このあたりprivateにしたほうが安全だよね。Tool追加/Tool取り除き、って感じ。
    std::shared_ptr<Folder> Inventory = std::make_shared<Folder>(); // ユーザーのインベントリ（アイテムを入れるためのフォルダ）
    std::array<std::shared_ptr<Tool>, 10> Slots = {};
    std::shared_ptr<Tool> currentTool = nullptr; // 現在手に持っているアイテム（スロットから参照）
    int currentSlotIndex = -1; // 現在選択されているスロットのインデックス（0-9、-1は未選択）

    enum class ControlMode {
        Free,
        Character
    } controlMode = ControlMode::Character;

    Vector3 forward;
    Vector3 right;
    Vector3 up;

    explicit User(std::unique_ptr<IInputBackend> input);
    ~User();

    std::string getClassName() override;
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    void updateVectors();
    void initializeInventory();  // Inventory を User の子として追加（コンストラクタ後に呼ぶ）
    void processInput(class Physics* physics, float deltaTime, bool viewportFocused, bool viewportZoomEnabled, bool isGameplayInput, bool wantsTextInput);
    // searchRoot: StarterCharacterを探す起点(通常はSystem)。Userは自身のParentに必ずしも
    // システムが居るとは限らない(パッケージ済みランタイムではUserがツリーに属さない)ため、
    // 呼び出し元(main.cpp/game_main.cpp)から明示的に渡す
    void spawnCharacter(Instance* searchRoot);
    void despawnCharacter();
    // 死亡後の再生成: 元の親(Workspace)を保持してキャラクターを作り直す
    void respawnCharacter();
    static User* getInstance() { return s_instance; }

    // イベントを"消費"するアクセサ（読み取りと同時に内部フラグをリセットする）
    bool consumeExitRequest();
    bool consumeWorkspaceSwitchRequest();

private:
    static User* s_instance;

    // 入力供給源（GLFW 等の具体実装を抽象化）
    std::unique_ptr<IInputBackend> m_input;

    // キャラクターの移動・カメラ追従(Humanoidに入力を渡し、結果を読んでカメラ位置を更新する)
    void processCharacterMovement(Physics* physics);

    // 外部からは参照されない内部状態（フレーム間のトグル判定）
    bool lastFKeyPressed = false; // トグル判定用

    // CtrlLock(Roblox ShiftLock相当)
    bool ctrlLockEnabled = false;
    bool lastCtrlKeyPressed = false;
    bool ctrlLockOffsetRight = true;      // true=右, false=左（Fキーで切替）
    bool lastCtrlLockFKeyPressed = false; // Fキートグル判定（lastFKeyPressedとは別物）

    std::array<bool, 10> lastToolKeyPressed = {};
    bool toolActivated = false; // Toolを持った状態で左クリックを押し続けている状態
    bool wannaExit = false;
    bool wantsSwitchWorkspace = false;
    bool isRightMouseRotating = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool m_altLookActive = false;  // Alt トグルによるフリールック中か
    bool m_altKeyWasDown = false;  // Alt 押下の立ち上がり検出用

    // 死亡 → respawn 管理
    bool      m_deathHandled  = false;
    float     m_respawnTimer  = 0.0f;
    Instance* m_lastSearchRoot = nullptr; // spawnCharacter の検索起点を保持（respawn 用）
};
