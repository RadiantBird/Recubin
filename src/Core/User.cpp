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
        // Workspace縺ｪ縺ｩ縺ｮ隕ｪ縺後＞繧句ｴ蜷医・縲∬ｦｪ蛛ｴ縺ｮ繝・せ繝医Λ繧ｯ繧ｿ縺ｧ蜑企勁縺輔ｌ繧九◆繧∽ｺ碁㍾隗｣謾ｾ繧帝∩縺代ｋ
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
    // 螟也ｩ阪ｒ菴ｿ繧上★縲√け繧ｩ繝ｼ繧ｿ繝九が繝ｳ縺九ｉ逶ｴ謗･繝ｭ繝ｼ繧ｫ繝ｫ霆ｸ繧貞叙繧雁・縺・
    forward = cam.Orientation.getForward();
    right   = cam.Orientation.getRight();
    up      = cam.Orientation.getUp();
}

void User::processInput() {
    if (!window) return;

    // 1. 繧ｫ繝｡繝ｩ蝗櫁ｻ｢縺ｮ蜈郁｡悟・逅・
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

    // 繧ｺ繝ｼ繝蜃ｦ逅・
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
        cameraDistance -= zoomSpeed; // 繧ｺ繝ｼ繝繧､繝ｳ
        if (cameraDistance < 2.0f) cameraDistance = 2.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) {
        cameraDistance += zoomSpeed; // 繧ｺ繝ｼ繝繧｢繧ｦ繝・
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

                // Slerp縺ｧ蜷代″繧呈峩譁ｰ縺励∫黄逅・い繧ｯ繧ｿ繝ｼ縺ｫ蜿肴丐
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
                        // 諷｣諤ｧ遘ｻ蜍穂ｸｭ・壹ル繝･繝ｼ繝医Λ繝ｫ繝昴・繧ｺ・・I縺ｮ蛟肴焚・峨∪縺ｧ騾ｲ繧√ｋ
                        float phase = fmod(walkCycle, PI);
                        if (phase > 0.15f) walkCycle += 0.15f;
                    }
                    
                    physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
                    dynamicActor->setLinearVelocity(physx::PxVec3(velocity.x, currentVel.y, velocity.z));
                } else {
                    // 騾溷ｺｦ縺・縺ｫ縺ｪ縺｣縺ｦ繧ゅ√い繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ縺ｮ繧ｭ繝ｪ縺梧が縺・ｴ蜷医・譛蠕後∪縺ｧ蜍輔°縺・
                    const float PI = 3.14159265f;
                    float phase = fmod(walkCycle, PI);
                    if (phase > 0.15f) walkCycle += 0.15f;

                    physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
                    dynamicActor->setLinearVelocity(physx::PxVec3(0, currentVel.y, 0));
                }
            }
            
            // 繧ｫ繝｡繝ｩ菴咲ｽｮ繧ゅく繝｣繝ｩ繧ｯ繧ｿ繝ｼ菴咲ｽｮ縺ｫ霑ｽ蠕・
            cpos = root->Position + Vector3(0, 2.0f, 0) - (forward * cameraDistance);
        }
    }
    
    // CFrame縺ｫ繧医ｋ蜷梧悄縺ｨ繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ驕ｩ逕ｨ
    if (torso) {
        torso->cframe = root->cframe * CFrame(0, 1.0f, 0);
    }
    if (head) {
        head->cframe = root->cframe * CFrame(0, 2.5f, 0);
    }

    float swingAngle = sin(walkCycle) * 35.0f;

    if (leftArm) {
        // 閧ｩ縺ｮ髢｢遽菴咲ｽｮ: x=-1.5, y=2.0 (Torso縺ｮ荳願ｧ・
        CFrame jointCF = root->cframe * CFrame(-1.5f, 2.0f, 0);
        leftArm->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), swingAngle) * CFrame(0, -1.0f, 0);
    }
    if (rightArm) {
        // 閧ｩ縺ｮ髢｢遽菴咲ｽｮ: x=1.5, y=2.0
        CFrame jointCF = root->cframe * CFrame(1.5f, 2.0f, 0);
        rightArm->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), -swingAngle) * CFrame(0, -1.0f, 0);
    }
    if (leftLeg) {
        // 閧｡髢｢遽菴咲ｽｮ: x=-0.5, y=0.0 (Root縺ｮ荳ｭ螟ｮ鬮倥＆)
        CFrame jointCF = root->cframe * CFrame(-0.5f, 0.0f, 0);
        leftLeg->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), -swingAngle) * CFrame(0, -1.0f, 0);
    }
    if (rightLeg) {
        // 閧｡髢｢遽菴咲ｽｮ: x=0.5, y=0.0
        CFrame jointCF = root->cframe * CFrame(0.5f, 0.0f, 0);
        rightLeg->cframe = jointCF * CFrame::fromAxisAngle(Vector3(1,0,0), swingAngle) * CFrame(0, -1.0f, 0);
    }
    
    // 蝗櫁ｻ｢縺後≠縺｣縺溷ｴ蜷医・縺ｿ繝吶け繝医Ν繧呈峩譁ｰ
    if (rotated) {
        updateVectors();
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        wannaExit = true;
    }
    // F繧ｭ繝ｼ縺ｧ蛻ｶ蠕｡繝｢繝ｼ繝峨ｒ蛻・ｊ譖ｿ縺・(繝医げ繝ｫ蜃ｦ逅・
    bool fPressed = (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS);
    if (fPressed && !lastFKeyPressed) {
        // Free/Character 繝｢繝ｼ繝峨・蛻・ｊ譖ｿ縺・
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
    
    // 繝代・繝・函謌・
    root      = new Cube(basePos, Vector3(2.0f, 4.0f, 1.0f), 0); 
    head      = new Cube(basePos, Vector3(1.0f, 1.0f, 1.0f), 0);
    torso     = new Cube(basePos, Vector3(2.0f, 2.0f, 1.0f), 0);
    leftArm   = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    rightArm  = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    leftLeg   = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    rightLeg  = new Cube(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);

    // 1. 蜷榊燕繧呈怙蛻昴↓險ｭ螳夲ｼ磯㍾隕・ｼ啾ddChild縺ｮ蜑阪↓險ｭ螳壹＠縺ｦ驥崎､・く繝ｼ繧帝∩縺代ｋ・・
    root->Name     = "Root";
    head->Name     = "Head";
    torso->Name    = "Torso";
    leftArm->Name  = "LeftArm";
    rightArm->Name = "RightArm";
    leftLeg->Name  = "LeftLeg";
    rightLeg->Name = "RightLeg";

    // 2. 迚ｩ逅・・繧｢繝ｳ繧ｫ繝ｼ縺ｮ險ｭ螳・
    head->Anchored     = true;
    torso->Anchored    = true;
    leftArm->Anchored  = true;
    rightArm->Anchored = true;
    leftLeg->Anchored  = true;
    rightLeg->Anchored = true;
    root->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    root->Color = Color4(1.0f, 0.5f, 0.5f, 1.0f);

    // 3. 隕ｪ縺ｫ霑ｽ蜉・亥錐蜑咲｢ｺ螳壼ｾ後↓addChild縺吶ｋ縺薙→縺ｧ陦晉ｪ√ｒ髦ｲ縺撰ｼ・
    character->addChild(root);
    character->addChild(head);
    character->addChild(torso);
    character->addChild(leftArm);
    character->addChild(rightArm);
    character->addChild(leftLeg);
    character->addChild(rightLeg);

    RCBN_LOG("Spawning character...");
}
