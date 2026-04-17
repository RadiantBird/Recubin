#include <Core/User.hpp>
#include <include/Util/Logger.hpp>

User::User(GLFWwindow* win) 
    : window(win), 
      cam(current_camera), 
      cpos(current_camera.Position),
      forward(0, 0, -1), 
      right(1, 0, 0), 
      up(0, 1, 0),
      currentMoveDir(0, 0, 0),
      lastFKeyPressed(false)
{
    updateVectors();
}

// 文字化けなんとかしてくれ、アセンブリですか（笑）
User::~User() {
    if (character) {
        // Workspaceなどの親がいる場合は、親側のデストラクタで削除されるため二重解放を避ける
        if (character->Parent == nullptr) {
            RCBN_LOG("User Destructor: Cleaning up orphaned character");
            delete character;
        } else {
            RCBN_LOG("User Destructor: Character is owned by Workspace, skipping manual delete");
        }
        character = nullptr;
        root = nullptr;
        torso = nullptr;
        head = nullptr;
        leftArm = nullptr;
        rightArm = nullptr;
        leftLeg = nullptr;
        rightLeg = nullptr;
    }
}


void User::updateVectors() {
    // 外積を使わず、クォータニオンから直接ローカル軸を取り出す
    forward = cam.Orientation.getForward();
    right   = cam.Orientation.getRight();
    up      = cam.Orientation.getUp();
}

void User::processInput() {
    if (!window) return;

    // 1. カメラ回転の先行処理
    bool rotated = false;
    float rotationSpeed = 1.5f;

    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        cam.Orientation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), rotationSpeed) * cam.Orientation;
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        cam.Orientation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), -rotationSpeed) * cam.Orientation;
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1, 0, 0), rotationSpeed);
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1, 0, 0), -rotationSpeed);
        rotated = true;
    }

    if (rotated) {
        updateVectors();
    }

    // ズーム処理
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
        cameraDistance -= zoomSpeed; // ズームイン
        if (cameraDistance < 2.0f) cameraDistance = 2.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) {
        cameraDistance += zoomSpeed; // ズームアウト
    }

    if (ControlMode::Free == controlMode) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cpos = cpos + forward * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cpos = cpos - forward * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cpos = cpos - right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cpos = cpos + right * speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cpos = cpos - up * speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cpos = cpos + up * speed;
    } 
    else if (ControlMode::Character == controlMode && character && root) {
        if (root->actor) {
            physx::PxRigidDynamic* dynamicActor = root->actor->is<physx::PxRigidDynamic>();
            if (dynamicActor) {
                Vector3 targetMoveDir(0, 0, 0);
                bool isPressingMove = false;

                Vector3 flatForward = Vector3(forward.x, 0, forward.z).normalize();
                Vector3 flatRight = Vector3(right.x, 0, right.z).normalize();

                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { targetMoveDir = targetMoveDir + flatForward; isPressingMove = true; }
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { targetMoveDir = targetMoveDir - flatForward; isPressingMove = true; }
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { targetMoveDir = targetMoveDir - flatRight; isPressingMove = true; }
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { targetMoveDir = targetMoveDir + flatRight; isPressingMove = true; }

                if (isPressingMove) targetMoveDir = targetMoveDir.normalize();

                currentMoveDir = currentMoveDir + (targetMoveDir - currentMoveDir) * 0.15f;

                Quaternion targetRot = root->Rotation;
                if (isPressingMove) {
                    float targetAnglePos = atan2(targetMoveDir.x, targetMoveDir.z) * 180.0f / 3.14159265f;
                    targetRot = Quaternion::fromAxisAngle(Vector3(0, 1, 0), targetAnglePos);
                }

                // Slerpで向きを更新し、物理アクターに反映
                root->Rotation = Quaternion::Slerp(root->Rotation, targetRot, 0.15f);
                
                physx::PxTransform pose = dynamicActor->getGlobalPose();
                pose.q = physx::PxQuat(root->Rotation.x, root->Rotation.y, root->Rotation.z, root->Rotation.w);
                dynamicActor->setGlobalPose(pose);

                if (currentMoveDir.length() > 0.01f) {
                    Vector3 velocity = currentMoveDir * walkPower;
                    
                    const float PI = 3.14159265f;
                    if (isPressingMove) {
                        walkCycle += 0.15f;
                    } else {
                        // 慣性移動中：ニュートラルポーズ（PIの倍数）まで進める
                        float phase = fmod(walkCycle, PI);
                        if (phase > 0.15f) walkCycle += 0.15f;
                    }
                    
                    physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
                    dynamicActor->setLinearVelocity(physx::PxVec3(velocity.x, currentVel.y, velocity.z));
                } else {
                    // 速度が0になっても、アニメーションのキレが悪い場合は最後まで動かす
                    const float PI = 3.14159265f;
                    float phase = fmod(walkCycle, PI);
                    if (phase > 0.15f) walkCycle += 0.15f;

                    physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
                    dynamicActor->setLinearVelocity(physx::PxVec3(0, currentVel.y, 0));
                }
            }
            
            // カメラ位置もキャラクター位置に追従
            cpos = root->Position + Vector3(0, 2.0f, 0) - (forward * cameraDistance);
        }
    }
    
    // CFrameによる同期とアニメーション適用
    if (torso) {
        torso->cframe = root->cframe * CFrame(0, 1.0f, 0);
    }
    if (head) {
        head->cframe = root->cframe * CFrame(0, 2.5f, 0);
    }

    float swingAngle = sin(walkCycle) * 35.0f;

    if (leftArm) {
        // 肩の関節位置: x=-1.5, y=2.0 (Torsoの上角)
        CFrame jointCF = root->cframe * CFrame(-1.5f, 2.0f, 0);
        leftArm->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), swingAngle) * CFrame(0, -1.0f, 0);
    }
    if (rightArm) {
        // 肩の関節位置: x=1.5, y=2.0
        CFrame jointCF = root->cframe * CFrame(1.5f, 2.0f, 0);
        rightArm->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), -swingAngle) * CFrame(0, -1.0f, 0);
    }
    if (leftLeg) {
        // 股関節位置: x=-0.5, y=0.0 (Rootの中央高さ)
        CFrame jointCF = root->cframe * CFrame(-0.5f, 0.0f, 0);
        leftLeg->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), -swingAngle) * CFrame(0, -1.0f, 0);
    }
    if (rightLeg) {
        // 股関節位置: x=0.5, y=0.0
        CFrame jointCF = root->cframe * CFrame(0.5f, 0.0f, 0);
        rightLeg->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), swingAngle) * CFrame(0, -1.0f, 0);
    }
    
    // 回転があった場合のみベクトルを更新
    if (rotated) {
        updateVectors();
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        wannaExit = true;
    }
    // Fキーで制御モードを切り替え (トグル処理)
    bool fPressed = (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS);
    if (fPressed && !lastFKeyPressed) {
        // Free/Character モードの切り替え
        if (controlMode == ControlMode::Free) {
            controlMode = ControlMode::Character;
            RCBN_LOG("Control Mode: Character");
        } else {
            controlMode = ControlMode::Free;
            RCBN_LOG("Control Mode: Free");
        }
    }
    lastFKeyPressed = fPressed;

    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
        std::cout << "Camera Position: (" << cpos.x << ", " << cpos.y << ", " << cpos.z << ")\n";
        if (root) {
            std::cout << "Character (Root) Position: (" << root->Position.x << ", " 
                      << root->Position.y << ", " << root->Position.z << ")\n";
        }
    }
}

void User::spawnCharacter() {
    if (character) return;

    character = new Model(Vector3(5.0f, 10.0f, 5.0f), Vector3(1, 1, 1));
    Vector3 basePos = character->Position;
    
    // パーツ生成
    root      = new Cube(basePos, Vector3(2.0f, 4.0f, 1.0f), 0); 
    head      = new Cube(basePos, Vector3(1.0f, 1.0f, 1.0f), 0);
    torso     = new Cube(basePos, Vector3(2.0f, 2.0f, 1.0f), 0);
    leftArm   = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    rightArm  = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    leftLeg   = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    rightLeg  = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);

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
    root->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;

    root->Color = Color4(1.0f, 0.5f, 0.5f, 0.0f); // 透明（描画スキップ対象）
    torso->Color = Color4::FromRGB(0, 36, 81);
    Color4 skinColor = Color4(0.8, 1.0, 0.0, 1.0f);
    head->Color = skinColor;
    leftArm->Color = skinColor;
    rightArm->Color = skinColor;
    Color4 pantsColor = Color4::FromRGB(0, 255, 128);
    leftLeg->Color = pantsColor;
    rightLeg->Color = pantsColor;

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
