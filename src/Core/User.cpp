#include <Core/User.hpp>
#include <Instances/StarterCharacter.hpp>
#include <include/Util/Logger.hpp>
#include <include/Core/Physics.hpp>
#include <include/Util/Logger.hpp>

User* User::s_instance = nullptr;

User::User(std::unique_ptr<IInputBackend> input)
    : Instance("User"),
      m_input(std::move(input)),
      cam(current_camera),
      cpos(current_camera.Position),
      forward(0, 0, -1),
      right(1, 0, 0),
      up(0, 1, 0),
      lastFKeyPressed(false)
{
    s_instance = this;
    updateVectors();
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

User::~User() {
    s_instance = nullptr;
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

// カメラ回転（マウス右ドラッグ＋矢印キー）
bool User::processCameraRotation(bool viewportFocused) {
    bool rotated = false;
    const float rotationSpeed = 1.5f;
    const double mouseRotationSpeed = 0.15;

    const bool rightMousePressed = viewportFocused && m_input->isMouseButtonDown(MouseButton::Right);

    if (rightMousePressed) {
        if (!isRightMouseRotating) {
            // ドラッグ開始: カーソルをロック(非表示)し、アンカー位置を取得する
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
        if (m_input->isKeyDown(KeyCode::Left))  { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0),  rotationSpeed) * cam.Orientation; rotated = true; }
        if (m_input->isKeyDown(KeyCode::Right)) { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0), -rotationSpeed) * cam.Orientation; rotated = true; }
        if (m_input->isKeyDown(KeyCode::Up))    { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0),  rotationSpeed); rotated = true; }
        if (m_input->isKeyDown(KeyCode::Down))  { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0), -rotationSpeed); rotated = true; }
    }

    if (rotated) updateVectors();
    return rotated;
}

// ズーム（I/Oキー・スクロール）
void User::processZoom(bool viewportZoomEnabled) {
    if (!viewportZoomEnabled) return;

    const double scrollDelta = m_input->consumeScrollDelta();

    if (controlMode == ControlMode::Free) {
        if (m_input->isKeyDown(KeyCode::I)) cpos = cpos + forward * zoomSpeed;
        if (m_input->isKeyDown(KeyCode::O)) cpos = cpos - forward * zoomSpeed;
        if (scrollDelta != 0.0) {
            cpos = cpos + forward * (static_cast<float>(scrollDelta) * mouseZoomSpeed);
        }
    } else {
        if (m_input->isKeyDown(KeyCode::I)) { cameraDistance -= zoomSpeed; if (cameraDistance < minCameraDistance) cameraDistance = minCameraDistance; }
        if (m_input->isKeyDown(KeyCode::O)) cameraDistance += zoomSpeed;
        if (scrollDelta != 0.0) {
            cameraDistance -= static_cast<float>(scrollDelta) * mouseZoomSpeed;
            if (cameraDistance < minCameraDistance) cameraDistance = minCameraDistance;
        }
    }
}

// 移動ディスパッチ（Free / Character を振り分け）
void User::processMovement(bool viewportFocused, Physics* physics) {
    if (!viewportFocused) return;

    if (controlMode == ControlMode::Free) {
        if (m_input->isKeyDown(KeyCode::W)) cpos = cpos + forward * speed;
        if (m_input->isKeyDown(KeyCode::S)) cpos = cpos - forward * speed;
        if (m_input->isKeyDown(KeyCode::A)) cpos = cpos - right   * speed;
        if (m_input->isKeyDown(KeyCode::D)) cpos = cpos + right   * speed;
        if (m_input->isKeyDown(KeyCode::Q)) cpos = cpos - up      * speed;
        if (m_input->isKeyDown(KeyCode::E)) cpos = cpos + up      * speed;
    } else if (controlMode == ControlMode::Character && character && humanoid) {
        processCharacterMovement(physics);
    }
}

static void attachToolHandle(
    const std::shared_ptr<Cube>& arm,
    const std::shared_ptr<Tool>& tool,
    const Quaternion& rootRotation
) {
    if (!arm || !tool || !tool->Handle) return;
    CFrame armCFrame = arm->getWorldCFrame();
    Vector3 charForward = rootRotation.getForward();
    const float TOOL_FORWARD_OFFSET = 1.0f;
    armCFrame.Position = armCFrame.Position + charForward * TOOL_FORWARD_OFFSET;
    tool->Handle->cframe = armCFrame; // 手の位置にツールを配置
}

// キャラクターの移動・カメラ追従（移動・回転・歩行アニメ・接地判定そのものはHumanoidが行う）
void User::processCharacterMovement(Physics* physics) {
    if (!humanoid || !humanoid->Root) return;

    // --- 入力方向の収集 ---
    Vector3 targetMoveDir(0, 0, 0);
    bool isPressingMove = false;

    Vector3 flatForward = Vector3(forward.x, 0, forward.z).normalize();
    Vector3 flatRight   = Vector3(right.x,   0, right.z  ).normalize();

    if (m_input->isKeyDown(KeyCode::W)) { targetMoveDir = targetMoveDir + flatForward; isPressingMove = true; }
    if (m_input->isKeyDown(KeyCode::S)) { targetMoveDir = targetMoveDir - flatForward; isPressingMove = true; }
    if (m_input->isKeyDown(KeyCode::A)) { targetMoveDir = targetMoveDir - flatRight;   isPressingMove = true; }
    if (m_input->isKeyDown(KeyCode::D)) { targetMoveDir = targetMoveDir + flatRight;   isPressingMove = true; }

    if (isPressingMove) targetMoveDir = targetMoveDir.normalize();

    bool toolEquipped   = currentTool && currentTool->Equipped;
    bool leftArmRaised  = toolEquipped && (currentTool->Hand == Tool::ToolHand::Left  || currentTool->Hand == Tool::ToolHand::Both);
    bool rightArmRaised = toolEquipped && (currentTool->Hand == Tool::ToolHand::Right || currentTool->Hand == Tool::ToolHand::Both);

    humanoid->move(flatForward, flatRight, isPressingMove, targetMoveDir, ctrlLockEnabled, physics, leftArmRaised, rightArmRaised);

    // --- 装備中のツールを手の位置に追従させる ---
    if (toolEquipped) {
        if (currentTool->Hand == Tool::ToolHand::Left) {
            attachToolHandle(humanoid->LeftArm, currentTool, humanoid->Root->Rotation);
        }
        if (currentTool->Hand != Tool::ToolHand::Left) {
            attachToolHandle(humanoid->RightArm, currentTool, humanoid->Root->Rotation);
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
void User::processHotkeys() {
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
        humanoid->jump();
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
            if (currentTool) {
                currentTool->Equipped = false;
                // character から削除して Inventory に戻す
                character->removeChild(currentTool->Name);
                Inventory->addChild(std::static_pointer_cast<Instance>(currentTool));
                RCBN_TRACE("Unequipped tool from slot " + std::to_string(currentSlotIndex + 1));
                currentTool = nullptr;
            }

            // 同じスロットを再度押した場合は解除のみ
            if (i == currentSlotIndex) {
                currentSlotIndex = -1;
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
           currentTool->Activated->fire();
           RCBN_TRACE("Activated tool: " + currentTool->Name);
           toolActivated = true;
        }
    } else {
        toolActivated = false;
    }
}

// ============================================================
// processInput（呼び出し口）
// ============================================================

void User::processInput(Physics* physics, bool viewportFocused, bool viewportZoomEnabled, bool isGameplayInput, bool wantsTextInput) {
    if (!m_input) return;

    bool rotated = processCameraRotation(viewportFocused);
    processZoom(viewportZoomEnabled);
    if (humanoid) humanoid->updateFirstPersonState(cameraDistance <= firstPersonThreshold);
    processMovement(viewportFocused, physics);
    if (rotated) updateVectors();
    processHotkeys();
    processToolkeys(viewportFocused, isGameplayInput, wantsTextInput);
    processMouse(isGameplayInput);
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

    auto humanoid = std::make_shared<Humanoid>();
    humanoid->Name = "Humanoid";

    Vector3 basePos(0.0f, 0.0f, 0.0f);
    auto root     = std::make_shared<Cube>(basePos, Vector3(2.0f, 4.0f, 1.0f), 0);
    auto head     = std::make_shared<Sphere>(basePos, Vector3(1.25f, 1.25f, 1.25f));
    auto torso    = std::make_shared<Cube>(basePos, Vector3(2.0f, 2.0f, 1.0f), 0);
    auto leftArm  = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    auto rightArm = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    auto leftLeg  = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    auto rightLeg = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);

    // headを90度回転させて顔が前を向くようにする
    head->setRotation(Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f));

    root->Name     = "Root";
    head->Name     = "Head";
    torso->Name    = "Torso";
    leftArm->Name  = "LeftArm";
    rightArm->Name = "RightArm";
    leftLeg->Name  = "LeftLeg";
    rightLeg->Name = "RightLeg";

    head->Anchored = torso->Anchored = leftArm->Anchored = rightArm->Anchored = leftLeg->Anchored = rightLeg->Anchored = true;
    head->CanCollide = torso->CanCollide = leftArm->CanCollide = rightArm->CanCollide = leftLeg->CanCollide = rightLeg->CanCollide = false;

    root->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    root->Color = Color4(1.0f, 0.5f, 0.5f, 0.0f); // NOTE: physics root は非表示 (alpha=0)

    torso->Color    = Color4::FromRGB(100, 12, 32);
    Color4 skin     = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    head->Color     = skin;
    leftArm->Color  = skin;
    rightArm->Color = skin;
    Color4 pants    = Color4::FromRGB(0, 36, 81);
    leftLeg->Color  = pants;
    rightLeg->Color = pants;

    starter->addChild(humanoid);
    starter->addChild(root);
    starter->addChild(head);
    starter->addChild(torso);
    starter->addChild(leftArm);
    starter->addChild(rightArm);
    starter->addChild(leftLeg);
    starter->addChild(rightLeg);

    return starter;
}

void User::spawnCharacter(Instance* searchRoot) {
    if (character) {
        despawnCharacter();
    }

    // HACK: ただのテスト配列
    auto tool1 = std::make_shared<Tool>("TestTool1");
    auto tool2 = std::make_shared<Tool>("TestTool2");
    std::shared_ptr<Cube> testHandle = std::make_shared<Cube>(Vector3(0,0,0), Vector3(0.5f, 0.5f, 5.0f), 0);
    testHandle->Name = "Handle";
    testHandle->Anchored = true;
    testHandle->Color = Color4::FromRGB(255, 0, 0);
    testHandle->CanCollide = false;

    tool1->addChild(testHandle);
    tool1->Hand = Tool::ToolHand::Left;
    tool1->Handle = testHandle;

    Slots[0] = tool1;
    Slots[1] = tool2;
    Inventory->addChild(tool1);
    Inventory->addChild(tool2);
    // end of HACK

    Instance* starter = findStarterCharacter(searchRoot);
    if (!starter && searchRoot) {
        auto defaultStarter = createDefaultStarterCharacter();
        searchRoot->addChild(defaultStarter);
        starter = defaultStarter.get();
        RCBN_LOG("StarterCharacter が見つからなかったため、既定のキャラクターを生成しました");
    }
    if (!starter) {
        RCBN_WARN("User::spawnCharacter: StarterCharacter を生成できないため、キャラクターは生成されません");
        return;
    }

    character = std::make_shared<Model>(Vector3(0.0f, 0.0f, 0.0f), Vector3(1, 1, 1));
    character->Name = "PlayerCharacter"; // NOTE: この名称は今後変更しないこと(ユーザーのスクリプトとの互換性を保つため)

    for (auto const& [name, child] : starter->children) {
        character->addChild(child->clone());
    }

    auto it = character->getChildren().find("Humanoid");
    humanoid = (it != character->getChildren().end()) ? std::dynamic_pointer_cast<Humanoid>(it->second) : nullptr;
    if (humanoid) humanoid->resolveParts(character.get());

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
        controlMode = (s == "Free") ? ControlMode::Free : ControlMode::Character;
        return;
    }
    if (name == "Speed")          { speed          = value.as<float>(); return; }
    if (name == "CameraDistance") { cameraDistance  = value.as<float>(); return; }
    if (name == "ZoomSpeed")      { zoomSpeed       = value.as<float>(); return; }
    if (name == "MouseZoomSpeed") { mouseZoomSpeed  = value.as<float>(); return; }
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> User::clone() const {
    // User is not cloneable due to its ownership of an IInputBackend (input source)
    // Return nullptr or throw an error
    RCBN_ERROR("User::clone() is not supported");
    return nullptr;
}
