#include <Core/User.hpp>
#include <Core/SystemState.hpp>
#include <include/Util/Logger.hpp>
#include <include/Core/Physics.hpp>
#include <Instances/CharacterSetting.hpp>
#include <include/Util/Logger.hpp>

User* User::s_instance = nullptr;

void User::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (s_instance && s_instance->window == window) {
        s_instance->pendingScrollY += yoffset;
        if (s_instance->previousScrollCallback) {
            s_instance->previousScrollCallback(window, xoffset, yoffset);
        }
    }
}

User::User(GLFWwindow* win) 
    : Instance("User"),
      window(win), 
      cam(current_camera), 
      cpos(current_camera.Position),
      forward(0, 0, -1), 
      right(1, 0, 0), 
      up(0, 1, 0),
      currentMoveDir(0, 0, 0),
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
    root = nullptr;
    torso = nullptr;
    head = nullptr;
    leftArm = nullptr;
    rightArm = nullptr;
    leftLeg = nullptr;
    rightLeg = nullptr;
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
 
    if (viewportFocused) {
        const bool rightMousePressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        double currentMouseX = 0.0, currentMouseY = 0.0;
        glfwGetCursorPos(window, &currentMouseX, &currentMouseY);
 
        if (rightMousePressed) {
            if (!isRightMouseRotating) {
                isRightMouseRotating = true;
                lastMouseX = currentMouseX;
                lastMouseY = currentMouseY;
            } else {
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
                glfwSetCursorPos(window, lastMouseX, lastMouseY);
            }
        } else {
            isRightMouseRotating = false;
        }
    } else {
        isRightMouseRotating = false;
    }
 
    if (viewportFocused) {
        if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0),  rotationSpeed) * cam.Orientation; rotated = true; }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0), -rotationSpeed) * cam.Orientation; rotated = true; }
        if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0),  rotationSpeed); rotated = true; }
        if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0), -rotationSpeed); rotated = true; }
    }
 
    if (rotated) updateVectors();
    return rotated;
}
 
// ズーム（I/Oキー・スクロール）
void User::processZoom(bool viewportZoomEnabled) {
    if (!viewportZoomEnabled) return;
 
    if (controlMode == ControlMode::Free) {
        if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) cpos = cpos + forward * zoomSpeed;
        if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) cpos = cpos - forward * zoomSpeed;
        if (pendingScrollY != 0.0) {
            cpos = cpos + forward * (static_cast<float>(pendingScrollY) * mouseZoomSpeed);
            pendingScrollY = 0.0;
        }
    } else {
        if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) { cameraDistance -= zoomSpeed; if (cameraDistance < 2.0f) cameraDistance = 2.0f; }
        if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) cameraDistance += zoomSpeed;
        if (pendingScrollY != 0.0) {
            cameraDistance -= static_cast<float>(pendingScrollY) * mouseZoomSpeed;
            if (cameraDistance < 2.0f) cameraDistance = 2.0f;
            pendingScrollY = 0.0;
        }
    }
}
 
// 移動ディスパッチ（Free / Character を振り分け）
void User::processMovement(bool viewportFocused, Physics* physics) {
    if (!viewportFocused) return;
 
    if (controlMode == ControlMode::Free) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cpos = cpos + forward * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cpos = cpos - forward * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cpos = cpos - right   * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cpos = cpos + right   * speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cpos = cpos - up      * speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cpos = cpos + up      * speed;
    } else if (controlMode == ControlMode::Character && character && root) {
        processCharacterMovement(physics);
    }
}
 
// キャラクター移動・物理・アニメーションサイクル・地面判定・カメラ追従
void User::processCharacterMovement(Physics* physics) {
    if (!root || !root->actor) return;
 
    physx::PxRigidDynamic* dynamicActor = root->actor->is<physx::PxRigidDynamic>();
    if (!dynamicActor) return;
 
    // --- 入力方向の収集 ---
    Vector3 targetMoveDir(0, 0, 0);
    bool isPressingMove = false;
 
    Vector3 flatForward = Vector3(forward.x, 0, forward.z).normalize();
    Vector3 flatRight   = Vector3(right.x,   0, right.z  ).normalize();
 
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { targetMoveDir = targetMoveDir + flatForward; isPressingMove = true; }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { targetMoveDir = targetMoveDir - flatForward; isPressingMove = true; }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { targetMoveDir = targetMoveDir - flatRight;   isPressingMove = true; }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { targetMoveDir = targetMoveDir + flatRight;   isPressingMove = true; }
 
    if (isPressingMove) targetMoveDir = targetMoveDir.normalize();
 
    // --- 移動ベクトルの補間 ---
    currentMoveDir = currentMoveDir + (targetMoveDir - currentMoveDir) * 0.15f;
 
    // --- 向き(Rotation)の更新 ---
    Quaternion targetRot = root->Rotation;
    if (isPressingMove) targetRot = Quaternion::LookRotation(targetMoveDir, Vector3(0, 1, 0));
    root->Rotation = Quaternion::Slerp(root->Rotation, targetRot, 0.15f);
 
    physx::PxTransform pose = dynamicActor->getGlobalPose();
    pose.q = physx::PxQuat(root->Rotation.x, root->Rotation.y, root->Rotation.z, root->Rotation.w);
    dynamicActor->setGlobalPose(pose);
 
    // --- 物理速度の適用 ---
    if (currentMoveDir.length() > 0.01f) {
        Vector3 velocity = currentMoveDir * walkPower;
 
        RaycastHit wallHit;
        float checkDist = root->Size.x / 2.0f + 0.15f;
        if (physics && physics->raycast(root->getWorldPosition(), currentMoveDir, checkDist, wallHit, root->actor)) {
            Vector3 n(wallHit.normal.x, 0.0f, wallHit.normal.z);
            float nLen = n.length();
            if (nLen > 0.001f) {
                n = n * (1.0f / nLen);
                float dot = velocity.x * n.x + velocity.z * n.z;
                if (dot < 0.0f) {
                    velocity.x -= dot * n.x;
                    velocity.z -= dot * n.z;
                }
            }
        }
 
        physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
        dynamicActor->setLinearVelocity(physx::PxVec3(velocity.x, currentVel.y, velocity.z));
    } else {
        physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
        dynamicActor->setLinearVelocity(physx::PxVec3(0, currentVel.y, 0));
    }
 
    // --- アニメーションサイクル（0.0 ~ 1.0）の更新 ---
    const float animationSpeed = 0.025f;
    if (isPressingMove) {
        walkCycle += animationSpeed;
        if (walkCycle > 1.0f) walkCycle -= 1.0f;
    } else {
        if (walkCycle > 0.0f) {
            if (walkCycle > 0.5f) {
                walkCycle += animationSpeed;
                if (walkCycle >= 1.0f) walkCycle = 0.0f;
            } else {
                walkCycle -= animationSpeed;
                if (walkCycle < 0.0f) walkCycle = 0.0f;
            }
        }
    }
 
    // --- 地面判定 ---
    RaycastHit hit;
    float maxDist = (root->Size.y / 2.0f) + 0.2f;
    isGrounded = physics ? physics->raycast(root->getWorldPosition(), Vector3(0, -1, 0), maxDist, hit, root->actor) : false;
 
    // --- カメラ追従 ---
    cpos = root->getWorldPosition() + Vector3(0, 2.0f, 0) - (forward * cameraDistance);
}
 
// ============================================================
// Animation: Pose計算
// ============================================================

User::Pose User::computePose() {
    const float PI = 3.14159265f;
    float rad   = walkCycle * 2.0f * PI;
    float swing = std::sin(rad) * 35.0f;

    Pose p;

    p.leftArm = isGrounded ? swing : 180.0f;

    if (currentTool && currentTool->Equipped) {
        p.rightArm = 90.0f;
    } else {
        p.rightArm = isGrounded ? -swing : 180.0f;
    }

    p.leftLeg  = -swing;
    p.rightLeg =  swing;

    return p;
}


// ============================================================
// Animation: Limb組み立て（共通）
// ============================================================

static CFrame makeArm(
    const CFrame& root,
    const Vector3& jointPos,
    float angleDeg
) {
    const Vector3 pivotOffset = Vector3(0, -0.5f, 0); // 回転中心調整
    const Vector3 meshOffset  = Vector3(0, -1.0f, 0); // モデル補正

    return root *
           CFrame(jointPos.x, jointPos.y, jointPos.z) *
           CFrame(pivotOffset.x, pivotOffset.y, pivotOffset.z) *
           CFrame::fromAxisAngle(Vector3(1,0,0), angleDeg) *
           CFrame(-pivotOffset.x, -pivotOffset.y, -pivotOffset.z) *
           CFrame(meshOffset.x, meshOffset.y, meshOffset.z);
}


static CFrame makeLeg(
    const CFrame& root,
    const Vector3& jointPos,
    float angleDeg
) {
    // 脚は今のところpivot補正なし
    const Vector3 meshOffset = Vector3(0, -1.0f, 0);

    return root *
           CFrame(jointPos.x, jointPos.y, jointPos.z) *
           CFrame::fromAxisAngle(Vector3(1,0,0), angleDeg) *
           CFrame(meshOffset.x, meshOffset.y, meshOffset.z);
}

// ============================================================
// Animation: 適用本体（差し替え対象）
// ============================================================
void User::applyBodyAnimation() {
    if (!root) return;

    Pose pose = computePose();

    // --- リグ定義（全部ここに固定） ---
    const Vector3 torsoOffset      = Vector3(0, 1.0f, 0);
    const Vector3 headOffset       = Vector3(0, 2.5f, 0);

    const Vector3 leftShoulderPos  = Vector3(-1.5f, 2.0f, 0);
    const Vector3 rightShoulderPos = Vector3( 1.5f, 2.0f, 0);

    const Vector3 leftHipPos       = Vector3(-0.5f, 0.0f, 0);
    const Vector3 rightHipPos      = Vector3( 0.5f, 0.0f, 0);

    // --- torso / head ---
    if (torso) torso->cframe = root->cframe * CFrame(torsoOffset.x, torsoOffset.y, torsoOffset.z);
    if (head)  head->cframe  = root->cframe * CFrame(headOffset.x,  headOffset.y,  headOffset.z);

    // --- arms ---
    if (leftArm) {
        leftArm->cframe = makeArm(root->cframe, leftShoulderPos, pose.leftArm);
    }

    if (rightArm) {
        rightArm->cframe = makeArm(root->cframe, rightShoulderPos, pose.rightArm);
        if (currentTool && currentTool->Equipped && currentTool->Handle) {
            // FIXME: どうやらcframeをそのまま代入しても位置がおかしいので、なんとかする
            currentTool->Handle->cframe = rightArm->cframe * CFrame(0, 0, 0); // 手の位置にツールを配置
        }
    }

    if (leftLeg) {
        leftLeg->cframe = makeLeg(root->cframe, leftHipPos, pose.leftLeg);
    }

    if (rightLeg) {
        rightLeg->cframe = makeLeg(root->cframe, rightHipPos, pose.rightLeg);
    }
}
 
// ホットキー（ESC / L / P / Space）
void User::processHotkeys() {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        wannaExit = true;
    }
 
    // Lキー: Free/Character モード切り替え
    bool fPressed = (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS);
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
    bool pPressed = (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);
    if (pPressed && !lastPPressed) wantsSwitchWorkspace = true;
    lastPPressed = pPressed;
 
    // Space: ジャンプ（接地時のみ）
    bool spacePressed = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
    if (spacePressed && !lastSpacePressed && controlMode == ControlMode::Character && root && root->actor) {
        if (isGrounded) {
            physx::PxRigidDynamic* dynamicActor = root->actor->is<physx::PxRigidDynamic>();
            if (dynamicActor) {
                physx::PxVec3 vel = dynamicActor->getLinearVelocity();
                vel.y = jumpPower;
                dynamicActor->setLinearVelocity(vel);
            }
        }
    }
    lastSpacePressed = spacePressed;
}

void User::processToolkeys() {
    static const int keys[] = {
        GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
        GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9, GLFW_KEY_0
    };

    for (int i = 0; i < 10; i++) {
        const bool pressed = (glfwGetKey(window, keys[i]) == GLFW_PRESS);

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

void User::processMouse() {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (currentTool && currentTool->Equipped && SystemState::get().isPlaying) {
           currentTool->Activated->fire();
           RCBN_TRACE("Activated tool: " + currentTool->Name);
        }
    }
}

// ============================================================
// processInput（呼び出し口）
// ============================================================
 
void User::processInput(Physics* physics) {
#ifdef EDITOR_DISABLED
    const bool viewportFocused     = true;
    const bool viewportZoomEnabled = true;
#else
    const bool viewportFocused     = SystemState::get().viewportFocused;
    const bool viewportZoomEnabled = SystemState::get().viewportZoomEnabled;
#endif
    if (!window) return;
 
    if (!isScrollCallbackInstalled) {
        previousScrollCallback = glfwSetScrollCallback(window, User::scrollCallback);
        isScrollCallbackInstalled = true;
    }
 
    bool rotated = processCameraRotation(viewportFocused);
    processZoom(viewportZoomEnabled);
    processMovement(viewportFocused, physics);
    applyBodyAnimation();
    if (rotated) updateVectors();
    processHotkeys();
    processToolkeys();
    processMouse();
}


void User::despawnCharacter() {
    if (!character) return;
    auto parent = character->Parent.lock();
    if (parent) {
        parent->removeChild(character->Name);
    }
    character = nullptr;
    root      = nullptr;
    torso     = nullptr;
    head      = nullptr;
    leftArm   = nullptr;
    rightArm  = nullptr;
    leftLeg   = nullptr;
    rightLeg  = nullptr;
}

void User::spawnCharacter(CharacterSetting* cs) {
    if (character) {
        despawnCharacter();
    }

    // HACK: ただのテスト配列
    auto tool1 = std::make_shared<Tool>("TestTool1");
    auto tool2 = std::make_shared<Tool>("TestTool2");
    std::shared_ptr<Cube> testHandle = std::make_shared<Cube>(Vector3(0,0,0), Vector3(5.0f, 5.0f, 5.0f), 0);
    testHandle->Name = "Handle";
    testHandle->Anchored = true;
    testHandle->Color = Color4::FromRGB(255, 0, 0);
    testHandle->CanCollide = false;
    
    tool1->addChild(testHandle);
    tool1->Handle = testHandle;

    Slots[0] = tool1;
    Slots[1] = tool2;
    Inventory->addChild(tool1);
    Inventory->addChild(tool2);
    // end of HACK

    character = std::make_shared<Model>(Vector3(0.0f, 25.0f, 0.0f), Vector3(1, 1, 1));
    character->Name = "PlayerCharacter"; // NOTE: この名称は今後変更しないこと(ユーザーのスクリプトとの互換性を保つため)
    Vector3 basePos = character->Position;

    // パーツ生成
    root      = std::make_shared<Cube>(basePos, Vector3(2.0f, 4.0f, 1.0f), 0);
    head      = std::make_shared<Sphere>(basePos, Vector3(1.25f, 1.25f, 1.25f));
    torso     = std::make_shared<Cube>(basePos, Vector3(2.0f, 2.0f, 1.0f), 0);
    leftArm   = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    rightArm  = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    leftLeg   = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    rightLeg  = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);

    // headを90度回転させて顔が前を向くようにする
    head->setRotation(Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f));
    // 1. 名前を最初に設定（重要：addChildの前に設定して重複キーを避ける）
    root->Name     = "Root";
    head->Name     = "Head";
    torso->Name    = "Torso";
    leftArm->Name  = "LeftArm";
    rightArm->Name = "RightArm";
    leftLeg->Name  = "LeftLeg";
    rightLeg->Name = "RightLeg";

    // 2. 物理・アンカーの設定
    head->Anchored     = true;
    torso->Anchored    = true;
    leftArm->Anchored  = true;
    rightArm->Anchored = true;
    leftLeg->Anchored  = true;
    rightLeg->Anchored = true;

    head->CanCollide     = false;
    torso->CanCollide    = false;
    leftArm->CanCollide  = false;
    rightArm->CanCollide = false;
    leftLeg->CanCollide  = false;
    rightLeg->CanCollide = false;
    
    root->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;

    root->Color = Color4(1.0f, 0.5f, 0.5f, 0.0f); // NOTE: physics root は非表示 (alpha=0)
    if (cs) {
        jumpPower        = cs->jumpPower;
        walkPower        = cs->moveSpeed;
        head->Color      = cs->headColor;
        torso->Color     = cs->torsoColor;
        leftArm->Color   = cs->leftArmColor;
        rightArm->Color  = cs->rightArmColor;
        leftLeg->Color   = cs->leftLegColor;
        rightLeg->Color  = cs->rightLegColor;
    } else {
        torso->Color   = Color4::FromRGB(100, 12, 32);
        Color4 skin    = Color4(1.0f, 1.0f, 1.0f, 1.0f);
        head->Color    = skin;
        leftArm->Color = skin;
        rightArm->Color= skin;
        Color4 pants   = Color4::FromRGB(0, 36, 81);
        leftLeg->Color = pants;
        rightLeg->Color= pants;
    }

    // 3. 親に追加（名前確定後にaddChildすることで衝突を防ぐ）
    character->addChild(root);
    character->addChild(head);
    character->addChild(torso);
    character->addChild(leftArm);
    character->addChild(rightArm);
    character->addChild(leftLeg);
    character->addChild(rightLeg);

    RCBN_LOG("Spawning character...");
}

std::string User::GetClassName() {
    return "User";
}

bool User::IsA(std::string className) {
    if (className == "User") return true;
    return Instance::IsA(className);
}

void User::setProperty(const std::string& name, const YAML::Node& value) {
    // User specific properties can be handled here if needed
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> User::clone() const {
    // User is not cloneable due to its dependency on GLFWwindow*
    // Return nullptr or throw an error
    RCBN_LOG("[WARNING] User::clone() is not supported");
    return nullptr;
}
