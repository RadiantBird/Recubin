#include <Core/User.hpp>
#include <Core/CharacterRig.hpp>
#include <Core/NullInputBackend.hpp>
#include <Network/NetworkIdentity.hpp>
#include <Instances/StarterCharacter.hpp>
#include <Instances/SpawnLocation.hpp>
#include <Instances/Workspace.hpp>
#include <include/Util/Logger.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/LuauEngine.hpp>
#include <include/Util/Logger.hpp>
#include <algorithm>

User* User::s_instance = nullptr;

User::User(std::unique_ptr<IInputBackend> input, bool isRemoteUser)
    : Instance("User"),
      m_input(std::move(input)),
      cam(current_camera),
      cpos(current_camera.Position),
      forward(0, 0, -1),
      right(1, 0, 0),
      up(0, 1, 0),
      lastFKeyPressed(false),
      isRemoteUser(isRemoteUser)
{
    if (!isRemoteUser) s_instance = this;
    updateVectors();

    // User.Input を生成し、入力供給源を借用させる
    Input = std::make_shared<UserInput>();
    Input->Name = "Input";
    Input->setBackend(m_input.get());

    CharacterAdded = std::make_shared<RCBNScriptSignal>();
}

std::shared_ptr<User> User::createRemoteUser(uint32_t peerId) {
    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>(), true);
    user->Name = NetworkIdentity::userName(peerId);
    user->peerId = peerId;
    user->lockRuntimeName();
    user->initializeInventory();
    return user;
}

bool User::applyNetworkIdentity(uint32_t newPeerId) {
    if (newPeerId == 0) return false;
    const std::string oldName = Name;
    if (!renameToAuthoritative(NetworkIdentity::userName(newPeerId))) return false;
    if (character && !character->renameToAuthoritative(NetworkIdentity::characterName(newPeerId))) {
        renameToAuthoritative(oldName);
        return false;
    }
    peerId = newPeerId;
    lockRuntimeName();
    if (character) character->lockRuntimeName();
    RCBN_LOG("User: applied authoritative identity " << Name
             << (character ? " / " + character->Name : ""));
    return true;
}

void User::initializeInventory() {
    // Inventory を User の子として追加（コンストラクタ後に呼ぶ）
    if (Inventory) {
        Inventory->Name = "Inventory";
        // すでに Parent が設定されていなければ addChild
        if (!Inventory->Parent.lock()) {
            this->addChild(Inventory);
        }
    }
}

void User::resetToolState() {
    for (auto& slot : Slots) slot = nullptr;
    currentTool      = nullptr;
    currentSlotIndex = -1;
}

void User::resetInventory() {
    // 先に強参照を切り、旧 Inventory 以下の Tool がスロット経由で生存しないようにする。
    resetToolState();
    if (Inventory) {
        if (auto parent = Inventory->Parent.lock()) {
            parent->removeChild(Inventory->Name);
        }
    }
    Inventory = std::make_shared<Folder>();
    Inventory->Name = "Inventory";
}

void User::syncToolsFromInventory() {
    resetToolState();
    if (!Inventory) return;

    for (const auto& [name, child] : Inventory->children) {
        (void)name;
        auto tool = std::dynamic_pointer_cast<Tool>(child);
        if (!tool) continue;

        if (addToolToSlot(tool) < 0) {
            RCBN_WARN("User::syncToolsFromInventory: hotbar is full; ignoring Tool " + tool->Name);
        }
    }
}

int User::addToolToSlot(std::shared_ptr<Tool> tool, int slotIndex) {
    if (!tool) return -1;

    // SceneLoader が新Inventoryを user->Inventory に採用する前にToolをツリーへ
    // 接続する場合があるため、親変更通知だけに依存せず、同期時にも所有Userを記録する。
    tool->m_inventoryOwner =
        std::static_pointer_cast<User>(shared_from_this());

    // Inventory への再収納や、スクリプトからの重複追加で同じ Tool が
    // 複数スロットに入らないよう、既存スロットをそのまま返す。
    for (int i = 0; i < static_cast<int>(Slots.size()); ++i) {
        if (Slots[i] == tool) return i;
    }

    // スロット番号の決定（負なら先頭の空きを探す）
    if (slotIndex < 0) {
        slotIndex = -1;
        for (int i = 0; i < static_cast<int>(Slots.size()); ++i) {
            if (!Slots[i]) { slotIndex = i; break; }
        }
        if (slotIndex < 0) return -1; // 空きなし
    } else if (slotIndex >= static_cast<int>(Slots.size())) {
        return -1; // 範囲外
    }

    // まだ Inventory 配下でなければ Inventory に入れる（装備ロジックと整合）
    if (Inventory && tool->Parent.lock().get() != Inventory.get()) {
        Inventory->addChild(std::static_pointer_cast<Instance>(tool));
    }

    Slots[slotIndex] = tool;
    return slotIndex;
}

void User::removeToolReferences(const std::shared_ptr<Tool>& tool) {
    if (!tool) return;
    for (auto& slot : Slots) {
        if (slot == tool) slot = nullptr;
    }
    if (currentTool == tool) {
        currentTool = nullptr;
        currentSlotIndex = -1;
    }
}

int User::findSlotByName(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(Slots.size()); ++i) {
        if (Slots[i] && Slots[i]->Name == name) return i;
    }
    return -1;
}

std::shared_ptr<Tool> User::getToolInSlot(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(Slots.size())) return nullptr;
    return Slots[slotIndex];
}

std::shared_ptr<Tool> User::removeToolFromSlot(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(Slots.size())) return nullptr;
    auto tool = Slots[slotIndex];
    if (!tool) return nullptr;

    // 装備中なら解除する（character から外れる前に状態を整える）
    if (currentTool == tool) {
        currentTool->Equipped = false;
        currentTool = nullptr;
        currentSlotIndex = -1;
    }

    // ツリー（Inventory もしくは character）からデタッチする
    if (auto parent = tool->Parent.lock()) {
        parent->removeChild(tool->Name);
    }

    Slots[slotIndex] = nullptr;
    return tool;
}

User::~User() {
    if (s_instance == this) s_instance = nullptr;
    // shared_ptr なので参照カウントが 0 になれば自動解放される
    character = nullptr;
    humanoid  = nullptr;
}

void User::updateVectors() {
    // 外積を使わず、クォータニオンから直接ローカル軸を取り出す
    forward = cam.Orientation.getForward();
    right   = cam.Orientation.getRight();
    up      = cam.Orientation.getUp();
}

// ControlMode::Program 用: Luauからカメラを直接設定する
void User::setCameraCFrame(const CFrame& cf) {
    cpos            = cf.Position;
    cam.Orientation = cf.Rotation;
    updateVectors();
}

CFrame User::getCameraCFrame() const {
    return CFrame(cpos, cam.Orientation);
}

// カメラ回転（マウス右ドラッグ＋矢印キー）
bool User::processCameraRotation(bool viewportFocused, float deltaTime) {
    if (controlMode == ControlMode::Program) return false; // Luauがカメラを直接制御するため入力は無視する

    bool rotated = false;
    const float frameScale = std::max(deltaTime, 0.0f) * 60.0f;

    // Alt トグル: ビューポートにフォーカスがあるとき、Alt 押下の立ち上がりで
    // フリールック(マウスを動かすだけでカメラが回る)を ON/OFF する
    const bool altDown = viewportFocused &&
        (m_input->isKeyDown(KeyCode::LeftAlt) || m_input->isKeyDown(KeyCode::RightAlt));
    if (altDown && !m_altKeyWasDown) m_altLookActive = !m_altLookActive;
    m_altKeyWasDown = altDown;
    // フォーカスを失ったらフリールックは解除（カーソルが隠れたままになるのを防ぐ）
    if (!viewportFocused) m_altLookActive = false;

    const bool rightMousePressed = viewportFocused && m_input->isMouseButtonDown(MouseButton::Right);
    const bool looking = rightMousePressed || m_altLookActive;

    if (looking) {
        if (!isRightMouseRotating) {
            // 開始: カーソルをロック(非表示)し、アンカー位置を取得する
            isRightMouseRotating = true;
            m_input->setMouseCaptured(true);
            m_input->getCursorPos(lastMouseX, lastMouseY);
        } else {
            // ロック中(DISABLED+raw)なので画面端クランプ・加速が無く滑らかな差分が得られる。
            // アンカーからの差分で回転したのち、仮想カーソルをアンカーへ戻す。
            // これにより ImGui へは固定位置を見せ、他エディター要素の誤反応を防ぐ。
            double currentMouseX = 0.0, currentMouseY = 0.0;
            m_input->getCursorPos(currentMouseX, currentMouseY);
            const double deltaX = currentMouseX - lastMouseX;
            const double deltaY = currentMouseY - lastMouseY;
            if (deltaX != 0.0 || deltaY != 0.0) {
                cam.Orientation =
                    Quaternion::fromAxisAngle(Vector3(0, 1, 0), static_cast<float>(-deltaX * mouseRotationSpeed)) *
                    cam.Orientation;
                cam.Orientation =
                    cam.Orientation *
                    Quaternion::fromAxisAngle(Vector3(1, 0, 0), static_cast<float>(-deltaY * mouseRotationSpeed));
                rotated = true;
            }
            m_input->setCursorPos(lastMouseX, lastMouseY);
        }
    } else {
        // ドラッグ終了(ボタン解除 or フォーカス喪失): カーソルロックを解放する
        if (isRightMouseRotating) {
            m_input->setMouseCaptured(false);
            isRightMouseRotating = false;
        }
    }

    if (viewportFocused) {
        const float keyboardRotation = rotationSpeed * frameScale;
        if (m_input->isKeyDown(KeyCode::Left))  { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0),  keyboardRotation) * cam.Orientation; rotated = true; }
        if (m_input->isKeyDown(KeyCode::Right)) { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0), -keyboardRotation) * cam.Orientation; rotated = true; }
        if (m_input->isKeyDown(KeyCode::Up))    { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0),  keyboardRotation); rotated = true; }
        if (m_input->isKeyDown(KeyCode::Down))  { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0), -keyboardRotation); rotated = true; }
    }

    if (rotated) updateVectors();
    return rotated;
}

void User::beginExternalCameraDrag() {
    if (m_externalDragActive) return;
    m_externalDragActive = true;
    m_input->setMouseCaptured(true);
    m_input->getCursorPos(lastMouseX, lastMouseY);
}
void User::sampleExternalCameraDrag(double& dx, double& dy) {
    dx = 0.0; dy = 0.0;
    if (!m_externalDragActive) return;
    double curX = 0.0, curY = 0.0;
    m_input->getCursorPos(curX, curY);
    dx = curX - lastMouseX;
    dy = curY - lastMouseY;
    m_input->setCursorPos(lastMouseX, lastMouseY);
}
void User::endExternalCameraDrag() {
    if (!m_externalDragActive) return;
    m_externalDragActive = false;
    m_input->setMouseCaptured(false);
}

// ズーム（I/Oキー・スクロール）
void User::processZoom(bool keyboardZoomEnabled, bool mouseZoomEnabled, float deltaTime) {
    // 無効時も毎フレーム破棄しておかないと、ビューポート外でのスクロールが
    // 消費されずに溜まり、後でhoverしただけの瞬間にまとめて適用されてしまう
    const double scrollDelta = m_input->consumeScrollDelta();
    if (controlMode == ControlMode::Program) return; // Luauがカメラを直接制御するため入力は無視する

    const float keyboardZoom = zoomSpeed * std::max(deltaTime, 0.0f) * 60.0f;
    if (controlMode == ControlMode::Free) {
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::I)) cpos = cpos + forward * keyboardZoom;
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::O)) cpos = cpos - forward * keyboardZoom;
        if (mouseZoomEnabled && scrollDelta != 0.0) {
            cpos = cpos + forward * (static_cast<float>(scrollDelta) * mouseZoomSpeed);
        }
    } else {
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::I)) { cameraDistance -= keyboardZoom; if (cameraDistance < minCameraDistance) cameraDistance = minCameraDistance; }
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::O)) cameraDistance += keyboardZoom;
        if (mouseZoomEnabled && scrollDelta != 0.0) {
            cameraDistance -= static_cast<float>(scrollDelta) * mouseZoomSpeed;
            if (cameraDistance < minCameraDistance) cameraDistance = minCameraDistance;
        }
    }
}

// 移動ディスパッチ（Free / Character を振り分け）
void User::processMovement(bool viewportFocused, Physics* physics, float deltaTime) {
    if (viewportFocused) {
        if (controlMode == ControlMode::Free) {
            Vector3 moveDirection{};

            if (m_input->isKeyDown(KeyCode::W)) moveDirection += forward;
            if (m_input->isKeyDown(KeyCode::S)) moveDirection -= forward;
            if (m_input->isKeyDown(KeyCode::A)) moveDirection -= right;
            if (m_input->isKeyDown(KeyCode::D)) moveDirection += right;
            if (m_input->isKeyDown(KeyCode::Q)) moveDirection -= up;
            if (m_input->isKeyDown(KeyCode::E)) moveDirection += up;

            const bool isMoving = moveDirection.lengthSquared() > 0.0f;

            if (isMoving) {
                movingTime += deltaTime;

                if (movingTime > accelerationDelay) {
                    accelerationMultiplier += accelerationRate * deltaTime;
                    if (maxAccelerationMultiplier < accelerationMultiplier) {
                        accelerationMultiplier = maxAccelerationMultiplier;
                    }
                }

                moveDirection = moveDirection.normalize();

                const float movementSpeed =
                    speed * std::max(deltaTime, 0.0f) * 60.0f * accelerationMultiplier;

                cpos += moveDirection * movementSpeed;
            }
            else {
                movingTime = 0.0f;
                accelerationMultiplier = 1.0f;
            }

            // Free モードでもボディパーツを Root に追従させる
            // （Character モードでは humanoid->move() 内で呼ばれる）
            bool leftArmRaised = false, rightArmRaised = false;
            getToolArmRaiseState(leftArmRaised, rightArmRaised);
            if (humanoid) humanoid->applyBodyAnimation(leftArmRaised, rightArmRaised);
        } else if (controlMode == ControlMode::Character && character && humanoid) {
            processCharacterMovement(physics, deltaTime);
        } else if (controlMode == ControlMode::Program) {
            // Program モードでもボディパーツを Root に追従させる
            // （カメラはLuauが制御するが、キャラクター自体の同期は他モードと同様に必要）
            bool leftArmRaised = false, rightArmRaised = false;
            getToolArmRaiseState(leftArmRaised, rightArmRaised);
            if (humanoid) humanoid->applyBodyAnimation(leftArmRaised, rightArmRaised);
        }
    }
    else {
        // Focusがどうであれ、ボディパーツはRootに追従させるべきであるため
        bool leftArmRaised = false, rightArmRaised = false;
        getToolArmRaiseState(leftArmRaised, rightArmRaised);
        if (humanoid) humanoid->applyBodyAnimation(leftArmRaised, rightArmRaised);
    }
}

void User::getToolArmRaiseState(bool& leftArmRaised, bool& rightArmRaised) const {
    bool toolEquipped = currentTool && currentTool->Equipped;
    leftArmRaised  = toolEquipped && (currentTool->Hand == Tool::ToolHand::Left  || currentTool->Hand == Tool::ToolHand::Both);
    rightArmRaised = toolEquipped && (currentTool->Hand == Tool::ToolHand::Right || currentTool->Hand == Tool::ToolHand::Both);
}

static void attachToolHandle(
    const std::shared_ptr<BaseCube>& arm,
    const std::shared_ptr<Tool>& tool,
    const Quaternion& rootRotation,
    Physics* physics
) {
    if (!arm || !tool || !tool->Handle) return;
    CFrame armCFrame = arm->getWorldCFrame();
    Vector3 charForward = rootRotation.getForward();
    const float TOOL_FORWARD_OFFSET = 1.0f;
    armCFrame.Position = armCFrame.Position + charForward * TOOL_FORWARD_OFFSET;
    const CFrame handleCFrame = armCFrame * CFrame(tool->Position, tool->Rotation);
    if (physics) {
        physics->moveWeldAssembly(tool->Handle, handleCFrame);
    } else {
        tool->Handle->setWorldCFrame(handleCFrame);
    }
}

// キャラクターの移動・カメラ追従（移動・回転・歩行アニメ・接地判定そのものはHumanoidが行う）
void User::processCharacterMovement(Physics* physics, float deltaTime) {
    if (!humanoid) return;
    auto root = humanoid->getRootPart();
    if (!root) return;

    // --- 入力方向の収集 ---
    Vector3 targetMoveDir(0, 0, 0);
    bool isPressingMove = false;

    Vector3 flatForward = Vector3(forward.x, 0, forward.z).normalize();
    Vector3 flatRight   = Vector3(right.x,   0, right.z  ).normalize();

    bool wDown = m_input->isKeyDown(KeyCode::W);
    bool sDown = m_input->isKeyDown(KeyCode::S);
    bool aDown = m_input->isKeyDown(KeyCode::A);
    bool dDown = m_input->isKeyDown(KeyCode::D);

    if (wDown) { targetMoveDir = targetMoveDir + flatForward; isPressingMove = true; }
    if (sDown) { targetMoveDir = targetMoveDir - flatForward; isPressingMove = true; }
    if (aDown) { targetMoveDir = targetMoveDir - flatRight;   isPressingMove = true; }
    if (dDown) { targetMoveDir = targetMoveDir + flatRight;   isPressingMove = true; }

    if (isPressingMove) targetMoveDir = targetMoveDir.normalize();

    // Truss登坂・Seat操作用の独立した2軸(-1..1)。斜め入力でも減衰しない生の値
    float forwardAxis = (wDown ? 1.0f : 0.0f) - (sDown ? 1.0f : 0.0f);
    float rightAxis   = (dDown ? 1.0f : 0.0f) - (aDown ? 1.0f : 0.0f);

    bool toolEquipped   = currentTool && currentTool->Equipped;
    bool leftArmRaised = false, rightArmRaised = false;
    getToolArmRaiseState(leftArmRaised, rightArmRaised);

    lastMovementInput.flatForward     = flatForward;
    lastMovementInput.flatRight       = flatRight;
    lastMovementInput.targetMoveDir   = targetMoveDir;
    lastMovementInput.isPressingMove  = isPressingMove;
    lastMovementInput.ctrlLockEnabled = ctrlLockEnabled;
    lastMovementInput.forwardAxis     = forwardAxis;
    lastMovementInput.rightAxis       = rightAxis;

    humanoid->move(flatForward, flatRight, isPressingMove, targetMoveDir, ctrlLockEnabled, physics,
                   leftArmRaised, rightArmRaised, forwardAxis, rightAxis, deltaTime);

    // --- 装備中のツールを手の位置に追従させる ---
    if (toolEquipped) {
        if (currentTool->Hand == Tool::ToolHand::Left) {
            auto leftArm = humanoid->getLeftArmPart();
            attachToolHandle(leftArm, currentTool, root->Rotation, physics);
        }
        if (currentTool->Hand != Tool::ToolHand::Left) {
            auto rightArm = humanoid->getRightArmPart();
            attachToolHandle(rightArm, currentTool, root->Rotation, physics);
        }
    }

    // --- カメラ追従 ---
    const Vector3 headOffset = Vector3(0, 2.5f, 0); // Humanoid::applyBodyAnimation()のheadOffsetと一致させる
    if (humanoid->isInFirstPerson()) {
        cpos = humanoid->getRootWorldPosition() + headOffset;
    } else {
        Vector3 basePos = humanoid->getRootWorldPosition() + Vector3(0, 2.0f, 0) - (forward * cameraDistance);
        if (ctrlLockEnabled) {
            float offsetSign = ctrlLockOffsetRight ? 1.0f : -1.0f;
            basePos = basePos + right * (ctrlLockOffsetDistance * offsetSign);
        }
        cpos = basePos;
    }
}

// ホットキー（ESC / L / P / Space）
void User::processHotkeys(Physics* physics) {
    if (m_input->isKeyDown(KeyCode::Escape)) {
        wannaExit = true;
    }

    // Lキー: Free/Character モード切り替え
    bool fPressed = m_input->isKeyDown(KeyCode::L);
    if (fPressed && !lastFKeyPressed && allowControlModeSwitch) {
        if (controlMode == ControlMode::Free) {
            controlMode = ControlMode::Character;
            RCBN_LOG("Control Mode: Character");
        } else {
            controlMode = ControlMode::Free;
            RCBN_LOG("Control Mode: Free");
        }
    }
    lastFKeyPressed = fPressed;

    // Pキー: ワークスペース切り替え
    static bool lastPPressed = false;
    bool pPressed = m_input->isKeyDown(KeyCode::P);
    if (pPressed && !lastPPressed) wantsSwitchWorkspace = true;
    lastPPressed = pPressed;

    // Space: ジャンプ（押し続けている間は接地するたびに連続でジャンプする。
    // 接地判定自体はHumanoid内部で行うため、ここでは押下中であれば毎フレーム要求するだけでよい）
    bool spacePressed = m_input->isKeyDown(KeyCode::Space);
    if (spacePressed && controlMode == ControlMode::Character && humanoid) {
        if (humanoid->isSeated()) {
            humanoid->standUp(physics);
            lastMovementInput.standUpRequested = true;
        }
        else {
            humanoid->jump(physics);
            lastMovementInput.jumpRequested = true;
        }
    }

    // 左Ctrlキー: CtrlLock ON/OFFトグル
    bool ctrlKeyPressed = m_input->isKeyDown(KeyCode::LeftControl);
    if (ctrlKeyPressed && !lastCtrlKeyPressed && controlMode == ControlMode::Character) {
        ctrlLockEnabled = !ctrlLockEnabled;
        RCBN_LOG(ctrlLockEnabled ? "CtrlLock: ON" : "CtrlLock: OFF");
    }
    lastCtrlKeyPressed = ctrlKeyPressed;

    // Fキー: CtrlLockのオフセット方向（左右）切り替え（CtrlLockのON/OFFに関わらず状態は保持される）
    bool ctrlLockFKeyPressed = m_input->isKeyDown(KeyCode::F);
    if (ctrlLockFKeyPressed && !lastCtrlLockFKeyPressed) {
        ctrlLockOffsetRight = !ctrlLockOffsetRight;
        RCBN_LOG(ctrlLockOffsetRight ? "CtrlLock offset: Right" : "CtrlLock offset: Left");
    }
    lastCtrlLockFKeyPressed = ctrlLockFKeyPressed;
}

bool User::consumeExitRequest() {
    bool v = wannaExit;
    wannaExit = false;
    return v;
}

bool User::consumeWorkspaceSwitchRequest() {
    bool v = wantsSwitchWorkspace;
    wantsSwitchWorkspace = false;
    return v;
}

void User::processToolkeys(bool viewportFocused, bool isGameplayInput, bool wantsTextInput) {
    if (!isGameplayInput) return;
    if (!character) return;
    if (!viewportFocused || controlMode != ControlMode::Character) return;
    if (wantsTextInput) return; // テキストボックス入力中はツール切り替えキーを無視

    static const KeyCode keys[] = {
        KeyCode::Num1, KeyCode::Num2, KeyCode::Num3, KeyCode::Num4, KeyCode::Num5,
        KeyCode::Num6, KeyCode::Num7, KeyCode::Num8, KeyCode::Num9, KeyCode::Num0
    };

    for (int i = 0; i < 10; i++) {
        const bool pressed = m_input->isKeyDown(keys[i]);

        if (pressed && !lastToolKeyPressed[i]) {
            RCBN_TRACE("Tool key pressed: " + std::to_string(i + 1));

            // 現在装備中のツールを外す
            int prevSlotIndex = currentSlotIndex;
            if (currentTool) {
                currentTool->Equipped = false;
                // character から削除して Inventory に戻す
                character->removeChild(currentTool->Name);
                Inventory->addChild(std::static_pointer_cast<Instance>(currentTool));
                RCBN_TRACE("Unequipped tool from slot " + std::to_string(currentSlotIndex + 1));
                currentTool      = nullptr;
                currentSlotIndex = -1;
            }

            // 同じスロットを再度押した場合は解除のみ（unequip前の番号で判定）
            if (i == prevSlotIndex) {
                // currentSlotIndex は既に -1 → 解除のみで完了
            } else if (Slots[i]) {
                // 新しいスロットを装備
                currentTool           = Slots[i];
                currentTool->Equipped = true;
                // Inventory から削除して character に追加
                Inventory->removeChild(currentTool->Name);
                character->addChild(std::static_pointer_cast<Instance>(currentTool));
                currentSlotIndex      = i;
                RCBN_TRACE("Equipped tool from slot " + std::to_string(i + 1));
            }
        }

        lastToolKeyPressed[i] = pressed;
    }
}

void User::processMouse(bool isGameplayInput) {
    if (m_input->isMouseButtonDown(MouseButton::Left)) {
        if (!toolActivated && currentTool && currentTool->Equipped && isGameplayInput) {
            RCBN_TRACE("Activated tool: " + currentTool->Name);
            currentTool->Activated->fire();
            // NOTE: currentTool will be nullptr if Luau removed the tool
            toolActivated = true;
        }
    } else {
        toolActivated = false;
    }
}

// ============================================================
// processInput（呼び出し口）
// ============================================================

void User::processInput(Physics* physics, float deltaTime, bool viewportFocused,
                        bool viewportHovered, bool isGameplayInput, bool wantsTextInput) {
    if (!m_input) return;

    // ジャンプ要求は毎フレームクリアし、processHotkeys()内でSpace押下時にのみセットする
    // (ネットワークレプリケーション用: このフレームでジャンプ要求があったかをlastMovementInputに残す)
    lastMovementInput.jumpRequested = false;
    lastMovementInput.standUpRequested = false;

    // User.Input: 前フレームとの差分で Pressed/Released を発火する
    if (Input) Input->poll();

    // 死亡後の経過時間はHumanoid側で管理し、再生成だけUserが行う
    if (humanoid && humanoid->isRespawnReady()) respawnCharacter();

    bool rotated = processCameraRotation(viewportFocused && !wantsTextInput, deltaTime);
    processZoom(viewportFocused && !wantsTextInput,
                viewportHovered && !wantsTextInput,
                deltaTime);
    if (humanoid) humanoid->updateFirstPersonState(cameraDistance <= firstPersonThreshold);
    // 死亡中はキャラクター移動を駆動しない（ばらしたパーツを上書きしないため）
    if (!humanoid || !humanoid->isDead()) {
        processMovement(viewportFocused && !wantsTextInput, physics, deltaTime);
    }
    if (rotated) updateVectors();
    if (!wantsTextInput) processHotkeys(physics);
    processToolkeys(viewportFocused, isGameplayInput, wantsTextInput);
    processMouse(isGameplayInput && !wantsTextInput);
}


void User::despawnCharacter() {
    if (!character) return;
    auto parent = character->Parent.lock();
    if (parent) {
        parent->removeChild(character->Name);
    }
    character = nullptr;
    humanoid  = nullptr;
}

void User::respawnCharacter() {
    // 死亡したキャラクターが属していた親(Workspace)を保持してから作り直す
    std::shared_ptr<Instance> parent = character ? character->Parent.lock() : nullptr;
    auto equippedTool = currentTool;
    const int equippedSlotIndex = currentSlotIndex;
    const bool shouldRestoreTool =
        equippedTool && equippedTool->Equipped && character && Inventory &&
        equippedSlotIndex >= 0 &&
        equippedSlotIndex < static_cast<int>(Slots.size()) &&
        Slots[equippedSlotIndex] == equippedTool &&
        equippedTool->Parent.lock().get() == character.get();

    // 装備中Toolを旧characterに残したままdespawnすると、Slots/currentToolの
    // 強参照によって死亡地点の物理状態ごと生存する。先にWorkspace外へ退避し、
    // actorを解放してから新characterを生成する。
    if (shouldRestoreTool) {
        equippedTool->Equipped = false;
        character->removeChild(equippedTool->Name);
        Inventory->addChild(std::static_pointer_cast<Instance>(equippedTool));
    }

    despawnCharacter();
    spawnCharacter(m_lastSearchRoot, m_lastSpawnWorkspace);
    if (parent && character) {
        parent->addChild(character);
    }

    if (!shouldRestoreTool) return;

    if (character) {
        Inventory->removeChild(equippedTool->Name);
        character->addChild(std::static_pointer_cast<Instance>(equippedTool));
        if (equippedTool->Parent.lock().get() == character.get()) {
            equippedTool->Equipped = true;
            currentTool = equippedTool;
            currentSlotIndex = equippedSlotIndex;
            return;
        }

        // 名前衝突等で再装備に失敗した場合もToolを失わない。
        Inventory->addChild(std::static_pointer_cast<Instance>(equippedTool));
    }

    equippedTool->Equipped = false;
    currentTool = nullptr;
    currentSlotIndex = -1;
}

void User::setCharacterFromScript(std::shared_ptr<Model> newCharacter) {
    if (!newCharacter) {
        character = nullptr;
        humanoid = nullptr;
        controlMode = ControlMode::Free;
        return;
    }

    character = newCharacter;

    auto it = character->getChildren().find("Humanoid");
    humanoid = (it != character->getChildren().end()) ? std::dynamic_pointer_cast<Humanoid>(it->second) : nullptr;
    if (humanoid) humanoid->resolveParts(character.get());

    if (controlMode == ControlMode::Free) controlMode = ControlMode::Character;

    if (CharacterAdded) {
        auto self = character;
        CharacterAdded->fire([self](lua_State* L) -> int {
            LuauEngine::pushInstance(L, std::static_pointer_cast<Instance>(self));
            return 1;
        });
    }
}

// System配下を再帰探索してStarterCharacterを見つける
static Instance* findStarterCharacter(Instance* inst) {
    if (!inst) return nullptr;
    if (inst->getClassName() == "StarterCharacter") return inst;
    for (auto& [name, child] : inst->children) {
        if (auto* found = findStarterCharacter(child.get())) return found;
    }
    return nullptr;
}

// StarterCharacterが一つも無いプロジェクトのためのフォールバック。
// 以前ハードコードされていた既定のリグ(Humanoid+Root+Torso+Head+両腕+両脚)を
// そのままStarterCharacterとして生成する(初回のみ。以後はsearchRootの子として見つかる)。
static std::shared_ptr<StarterCharacter> createDefaultStarterCharacter() {
    auto starter = std::make_shared<StarterCharacter>();
    starter->Name = "StarterCharacter";
    CharacterRig::buildDefaultRigParts(starter);
    return starter;
}

std::shared_ptr<Model> User::buildCharacterModel(Instance* searchRoot, const std::string& name) {
    Instance* starter = findStarterCharacter(searchRoot);
    if (!starter && searchRoot) {
        auto defaultStarter = createDefaultStarterCharacter();
        searchRoot->addChild(defaultStarter);
        starter = defaultStarter.get();
        RCBN_LOG("StarterCharacter が見つからなかったため、既定のキャラクターを生成しました");
    }
    if (!starter) return nullptr;

    auto model = std::make_shared<Model>(Vector3(0.0f, 0.0f, 0.0f), Vector3(1, 1, 1));
    model->Name = name;

    for (auto const& [childName, child] : starter->children) {
        model->addChild(child->clone());
    }
    // 制約（Weld/Rope 等）の Cube 参照をクローン側（PlayerCharacter 内）へ張り替える。
    // 髪の Weld は兄弟 Head を参照するため、キャラ全体で一括して張り替える必要がある。
    Instance::rebindClonedConstraints(*starter, *model);
    return model;
}

void User::placeCharacterAtSpawn(
    const std::shared_ptr<Model>& model,
    const std::shared_ptr<Humanoid>& humanoid,
    Workspace* workspace,
    std::uint32_t spawnPeerId) {
    if (!model || !humanoid) return;
    auto root = humanoid->getRootPart();
    if (!root) return;

    std::vector<std::shared_ptr<SpawnLocation>> candidates;
    auto collect = [&](auto& self, const std::shared_ptr<Instance>& node) -> void {
        if (!node) return;
        if (auto spawn = std::dynamic_pointer_cast<SpawnLocation>(node);
            spawn && spawn->Enabled) {
            candidates.push_back(std::move(spawn));
        }
        for (const auto& [name, child] : node->children) {
            (void)name;
            self(self, child);
        }
    };
    if (workspace) {
        for (const auto& [name, child] : workspace->children) {
            (void)name;
            collect(collect, child);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& first, const auto& second) {
            return first->getFullPath() < second->getFullPath();
        });

    CFrame targetRoot;
    if (!candidates.empty()) {
        const std::size_t index = spawnPeerId == 0
            ? 0
            : static_cast<std::size_t>(spawnPeerId - 1) % candidates.size();
        const auto& spawn = candidates[index];
        targetRoot = spawn->getWorldCFrame() *
            CFrame(0.0f, (spawn->Size.y + root->Size.y) * 0.5f, 0.0f);
    }
    model->cframe = targetRoot * root->cframe.inverse();
}

void User::spawnCharacter(Instance* searchRoot, Workspace* workspace,
                          const std::optional<Vector3>& initialPosition) {
    m_lastSearchRoot = searchRoot; // respawn 用に保持
    m_lastSpawnWorkspace = workspace;
    if (character) {
        despawnCharacter();
    }

    // HACK: ただのテスト配列
    // auto tool1 = std::make_shared<Tool>("TestTool1");
    // auto tool2 = std::make_shared<Tool>("TestTool2");
    // std::shared_ptr<Cube> testHandle = std::make_shared<Cube>(Vector3(0,0,0), Vector3(0.5f, 0.5f, 5.0f), 0);
    // testHandle->Name = "Handle";
    // testHandle->Anchored = true;
    // testHandle->Color = Color4::FromRGB(255, 0, 0);
    // testHandle->CanCollide = false;

    // tool1->addChild(testHandle);
    // tool1->Hand = Tool::ToolHand::Left;
    // tool1->Handle = testHandle;

    // Slots[0] = tool1;
    // Slots[1] = tool2;
    // Inventory->addChild(tool1);
    // Inventory->addChild(tool2);
    // end of HACK

    const std::string characterName = peerId != 0 ? NetworkIdentity::characterName(peerId) : "PlayerCharacter";
    character = buildCharacterModel(searchRoot, characterName);
    if (!character) {
        RCBN_WARN("User::spawnCharacter: StarterCharacter を生成できないため、キャラクターは生成されません");
        return;
    }
    if (peerId != 0) character->lockRuntimeName();
    auto it = character->getChildren().find("Humanoid");
    humanoid = (it != character->getChildren().end()) ? std::dynamic_pointer_cast<Humanoid>(it->second) : nullptr;
    if (humanoid) {
        humanoid->resolveParts(character.get());
        if (auto root = humanoid->getRootPart()) {
            root->Anchored = false;
            root->CanCollide = true;
        }
    }
    if (initialPosition) {
        // Play HereはSpawnLocationより明示Model.Positionを優先する。
        character->Position = *initialPosition;
    } else {
        placeCharacterAtSpawn(character, humanoid, workspace, peerId);
    }

    // NOTE: この時点ではcharacterはまだWorkspaceに追加されていない(addChildは呼び出し元が行う)。
    // それでもRoot等のパーツ解決は完了しているため、Luau側はcharacterを直接受け取れば
    // workspace:WaitChild()に頼らずrespawn後の新しいキャラクターを取得できる
    if (CharacterAdded) {
        auto self = character;
        CharacterAdded->fire([self](lua_State* L) -> int {
            LuauEngine::pushInstance(L, std::static_pointer_cast<Instance>(self));
            return 1;
        });
    }
    RCBN_LOG("Spawning character...");
}

std::string User::getClassName() {
    return "User";
}

bool User::IsA(std::string className) {
    if (className == "User") return true;
    return Instance::IsA(className);
}

void User::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "ControlMode") {
        std::string s = value.as<std::string>();
        if (s == "Free")         controlMode = ControlMode::Free;
        else if (s == "Program") controlMode = ControlMode::Program;
        else                     controlMode = ControlMode::Character;
        return;
    }
    if (name == "Speed")             { speed             = value.as<float>(); return; }
    if (name == "RotationSpeed")     { rotationSpeed      = value.as<float>(); return; }
    if (name == "MouseRotationSpeed"){ mouseRotationSpeed = value.as<float>(); return; }
    if (name == "CameraDistance")    { cameraDistance     = value.as<float>(); return; }
    if (name == "ZoomSpeed")         { zoomSpeed          = value.as<float>(); return; }
    if (name == "MouseZoomSpeed")    { mouseZoomSpeed     = value.as<float>(); return; }
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> User::clone() const {
    // User is not cloneable due to its ownership of an IInputBackend (input source)
    // Return nullptr or throw an error
    RCBN_ERROR("User::clone() is not supported");
    return nullptr;
}
