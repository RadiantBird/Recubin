#include <Core/User.hpp>
#include <include/Util/Logger.hpp>

User::User(GLFWwindow* win) 
    : window(win), 
      cam(current_camera), 
      cpos(current_camera.Position),
      forward(0, 0, -1), 
      right(1, 0, 0), 
      up(0, 1, 0) 
{
    updateVectors();
}

User::~User() {
    if (character) {
        // Workspaceなどの親がいる場合は、親側のデストラクタで削除されるため二重解放を避ける
        if (character->Parent == nullptr) {
            RCBN_LOG("User Destructor: Manually deleting orphan character");
            delete character;
        }
        character = nullptr;
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

    if (ControlMode::Free == controlMode) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cpos = cpos + forward * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cpos = cpos - forward * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cpos = cpos - right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cpos = cpos + right * speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cpos = cpos - up * speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cpos = cpos + up * speed;
    } 
    else if (ControlMode::Character == controlMode && character && root) {
        // キャラクターモードの入力処理：物理エンジンを使用して移動
        if (root->actor) {
            // 物理エンジンのダイナミックアクターに速度を設定
            physx::PxRigidDynamic* dynamicActor = root->actor->is<physx::PxRigidDynamic>();
            if (dynamicActor) {
                Vector3 velocity(0, 0, 0);
                
                // 水平移動速度の計算
                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
                    velocity = velocity + forward * walkPower;
                }
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
                    velocity = velocity - forward * walkPower;
                }
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                    velocity = velocity - right * walkPower;
                }
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                    velocity = velocity + right * walkPower;
                }
                
                // 現在の垂直速度を保持（重力の影響を保つ）
                physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
                velocity.y = currentVel.y;
                
                // 新しい速度を設定
                dynamicActor->setLinearVelocity(
                    physx::PxVec3(velocity.x, velocity.y, velocity.z)
                );
            }
            
            // カメラ位置もキャラクター位置に追従（向きと距離に基づいて計算）
            cpos = root->Position + Vector3(0, 2.0f, 0) - (forward * cameraDistance);
        }
    }
    // 回転とカメラの距離調整は常に実行する
    bool rotated = false;

    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        // Y軸（ワールドの上方向）を中心に回転
        cam.Orientation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), rotationSpeed) * cam.Orientation;
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        cam.Orientation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), -rotationSpeed) * cam.Orientation;
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        // X軸（カメラから見て右方向）を中心に回転
        cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1, 0, 0), rotationSpeed);
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1, 0, 0), -rotationSpeed);
        rotated = true;
    }

    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
        cameraDistance -= zoomSpeed; // ズームイン
        if (cameraDistance < 2.0f) cameraDistance = 2.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) {
        cameraDistance += zoomSpeed; // ズームアウト
    }
    
    // 常に同期（rootの回転に合わせてオフセットも回転させる）
    if (head) {
        head->Position = root->Position + root->Rotation.rotate(Vector3(0, 1.25f, 0));
        head->Rotation = root->Rotation;
    }
    if (leftArm) {
        leftArm->Position = root->Position + root->Rotation.rotate(Vector3(-0.75f, 0, 0));
        leftArm->Rotation = root->Rotation;
    }
    if (rightArm) {
        rightArm->Position = root->Position + root->Rotation.rotate(Vector3(0.75f, 0, 0));
        rightArm->Rotation = root->Rotation;
    }
    if (leftLeg) {
        leftLeg->Position = root->Position + root->Rotation.rotate(Vector3(-0.25f, -1.5f, 0));
        leftLeg->Rotation = root->Rotation;
    }
    if (rightLeg) {
        rightLeg->Position = root->Position + root->Rotation.rotate(Vector3(0.25f, -1.5f, 0));
        rightLeg->Rotation = root->Rotation;
    }
    
    // 回転があった場合のみベクトルを更新
    if (rotated) {
        updateVectors();
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        wannaExit = true;
    }
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
        // Free/Character モードの切り替え
        if (controlMode == ControlMode::Free) {
            controlMode = ControlMode::Character;
        } else {
            controlMode = ControlMode::Free;
        }
    }
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
        std::cout << "Camera Position: (" << cpos.x << ", " << cpos.y << ", " << cpos.z << ")\n";
        if (root) {
            std::cout << "Character (Root) Position: (" << root->Position.x << ", " 
                      << root->Position.y << ", " << root->Position.z << ")\n";
        }
    }
}

void User::spawnCharacter() {
    if (character) return; // 既にスポーンしている場合は何もしない

    // キャラクター群を格納するモデル（物理なし）
    character = new Model(Vector3(5.0f, 5.0f, 5.0f), Vector3(1, 1, 1));
    Vector3 basePos = character->Position;
    
    head      = new Cube(basePos + Vector3(0, 1.25f, 0), Vector3(1, 1, 1), 0);
    root     = new Cube(basePos + Vector3(0, -3.0f, 0), Vector3(1, 3.0f, 0.5f), 0); 
    leftArm   = new Cube(basePos + Vector3(-0.75f, 0, 0), Vector3(0.5f, 1.5f, 0.5f), 0);
    rightArm  = new Cube(basePos + Vector3(0.75f, 0, 0), Vector3(0.5f, 1.5f, 0.5f), 0);
    leftLeg   = new Cube(basePos + Vector3(-0.25f, -1.5f, 0), Vector3(0.5f, 1.5f, 0.5f), 0);
    rightLeg  = new Cube(basePos + Vector3(0.25f, -1.5f, 0), Vector3(0.5f, 1.5f, 0.5f), 0);
    
    // 回転をY軸のみに限定（X軸とZ軸をロック）
    root->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    // 同様にほかの部位も回転をY軸のみに限定
    head->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    leftArm->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    rightArm->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    leftLeg->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    rightLeg->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;

    head->Name = "Head";
    root->Name = "Root";
    leftArm->Name = "LeftArm";
    rightArm->Name = "RightArm";
    leftLeg->Name = "LeftLeg";
    rightLeg->Name = "RightLeg";
    
    // torsoのカラー
    // torso->Color = Color4(1.0f, 0.5f, 0.0f, 1.0f);

    root->Color = Color4(1.0f, 0.5f, 0.5f, 1.0f); // 確認用。リリース時はA = 0にして透明にする

    RCBN_LOG("Spawning character...");
    character->addChild(head);
    character->addChild(root);
    character->addChild(leftArm);
    character->addChild(rightArm);
    character->addChild(leftLeg);
    character->addChild(rightLeg);
}