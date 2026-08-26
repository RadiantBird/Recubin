#include <Editor/CommandHistory.hpp>

SetPropertyCommand::SetPropertyCommand(std::shared_ptr<Instance> target, const PropertyDesc* desc,
                                       PropValue before, PropValue after)
    : m_target(std::move(target)), m_desc(desc), m_before(std::move(before)), m_after(std::move(after)) {}
void SetPropertyCommand::execute() { if (m_target && m_desc) PropertyRegistry::writeValue(m_target.get(), *m_desc, m_after); }
void SetPropertyCommand::undo() { if (m_target && m_desc) PropertyRegistry::writeValue(m_target.get(), *m_desc, m_before); }

SetSurfaceMarkFilterCommand::SetSurfaceMarkFilterCommand(std::shared_ptr<SurfaceMark> target,
    std::vector<std::shared_ptr<Instance>> beforeInstances, std::vector<std::string> beforePaths,
    std::vector<std::shared_ptr<Instance>> afterInstances, std::vector<std::string> afterPaths)
    : m_target(std::move(target)), m_beforeInstances(std::move(beforeInstances)),
      m_afterInstances(std::move(afterInstances)), m_beforePaths(std::move(beforePaths)),
      m_afterPaths(std::move(afterPaths)) {}
void SetSurfaceMarkFilterCommand::execute() { if (m_target) m_target->setFilterState(m_afterInstances, m_afterPaths); }
void SetSurfaceMarkFilterCommand::undo() { if (m_target) m_target->setFilterState(m_beforeInstances, m_beforePaths); }

SetVec3Command::SetVec3Command(std::shared_ptr<Spatial> t, std::string p, Vector3 b, Vector3 a)
    : m_target(std::move(t)), m_prop(std::move(p)), m_before(b), m_after(a) {}
void SetVec3Command::execute() { apply(m_after); }
void SetVec3Command::undo() { apply(m_before); }
void SetVec3Command::apply(const Vector3& v) {
    if (!m_target) return;
    if (m_target->IsA("BaseCube")) { auto* b=static_cast<BaseCube*>(m_target.get()); if(m_prop=="Position")b->teleportTo(v); else if(m_prop=="Size")b->setSize(v); }
    else { if(m_prop=="Position")m_target->cframe.Position=v; else if(m_prop=="Size")m_target->Size=v; }
}
SetColorCommand::SetColorCommand(std::shared_ptr<BaseCube> t, Color4 b, Color4 a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetColorCommand::execute(){if(m_target)m_target->Color=m_after;} void SetColorCommand::undo(){if(m_target)m_target->Color=m_before;}
SetMaterialCommand::SetMaterialCommand(std::shared_ptr<BaseCube> t, Material b, Material a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetMaterialCommand::execute(){if(m_target)m_target->setMaterial(m_after);} void SetMaterialCommand::undo(){if(m_target)m_target->setMaterial(m_before);}
SetMassDensityCommand::SetMassDensityCommand(std::shared_ptr<BaseCube> t,float b,float a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetMassDensityCommand::execute(){if(m_target)m_target->setMassDensity(m_after);} void SetMassDensityCommand::undo(){if(m_target)m_target->setMassDensity(m_before);}
SetBoolCommand::SetBoolCommand(std::shared_ptr<BaseCube> t,std::string p,bool b,bool a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetBoolCommand::execute(){apply(m_after);} void SetBoolCommand::undo(){apply(m_before);} void SetBoolCommand::apply(bool v){if(!m_target)return;if(m_prop=="Anchored")m_target->setAnchored(v);else if(m_prop=="CanCollide")m_target->CanCollide=v;}
RenameInstanceCommand::RenameInstanceCommand(std::shared_ptr<Instance> t,std::string b,std::string a):m_target(std::move(t)),m_before(std::move(b)),m_after(std::move(a)){}
void RenameInstanceCommand::execute(){if(m_target)m_target->renameTo(m_after);} void RenameInstanceCommand::undo(){if(m_target)m_target->renameTo(m_before);}
MultiRenameInstanceCommand::MultiRenameInstanceCommand(std::vector<Entry> e):m_entries(std::move(e)){}
void MultiRenameInstanceCommand::execute(){apply(true);}
void MultiRenameInstanceCommand::undo(){apply(false);}
void MultiRenameInstanceCommand::apply(bool after){
    // 先に全対象を親ごとに衝突しない一時名へ移してから確定名へ移す。
    for (size_t i=0;i<m_entries.size();++i) if (m_entries[i].target && !m_entries[i].target->isRuntimeNameLocked()) {
        auto p=m_entries[i].target->Parent.lock();
        if (p) {
            std::string tmp="__CodexRenameTmp"+std::to_string(i);
            int n=0; while (p->getChild(tmp)) tmp="__CodexRenameTmp"+std::to_string(i)+"_"+std::to_string(++n);
            m_entries[i].target->renameToAuthoritative(tmp);
        }
    }
    for (const auto& e:m_entries) if(e.target && !e.target->isRuntimeNameLocked())
        e.target->renameToAuthoritative(after ? e.after : e.before);
}
SetRotationCommand::SetRotationCommand(std::shared_ptr<Spatial> t,Quaternion b,Quaternion a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetRotationCommand::execute(){if(m_target)m_target->cframe.Rotation=m_after;} void SetRotationCommand::undo(){if(m_target)m_target->cframe.Rotation=m_before;}
SetToolPositionCommand::SetToolPositionCommand(std::shared_ptr<Tool> t,Vector3 b,Vector3 a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetToolPositionCommand::execute(){if(m_target)m_target->Position=m_after;} void SetToolPositionCommand::undo(){if(m_target)m_target->Position=m_before;}
SetToolRotationCommand::SetToolRotationCommand(std::shared_ptr<Tool> t,Quaternion b,Quaternion a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetToolRotationCommand::execute(){if(m_target)m_target->Rotation=m_after;} void SetToolRotationCommand::undo(){if(m_target)m_target->Rotation=m_before;}
SetSpatialCFrameCommand::SetSpatialCFrameCommand(std::shared_ptr<Spatial> t,CFrame b,CFrame a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetSpatialCFrameCommand::execute(){apply(m_after);} void SetSpatialCFrameCommand::undo(){apply(m_before);} void SetSpatialCFrameCommand::apply(const CFrame& v){if(!m_target)return;if(m_target->IsA("BaseCube")){auto*b=static_cast<BaseCube*>(m_target.get());b->teleportTo(v.Position);b->setRotation(v.Rotation);}else m_target->cframe=v;}
MultiSpatialTransformCommand::MultiSpatialTransformCommand(std::vector<Entry> e):m_entries(std::move(e)){}
void MultiSpatialTransformCommand::execute(){apply(true);} void MultiSpatialTransformCommand::undo(){apply(false);}
void MultiSpatialTransformCommand::apply(bool after){
    for (auto& e:m_entries) if(e.target){
        const CFrame& cf=after?e.afterCFrame:e.beforeCFrame;
        const Vector3& sz=after?e.afterSize:e.beforeSize;
        if(e.target->IsA("BaseCube")) {
            auto* b=static_cast<BaseCube*>(e.target.get());
            auto p=e.target->Parent.lock();
            CFrame local=cf;
            if(p&&p->IsA("Spatial")) local=static_cast<Spatial*>(p.get())->getWorldCFrame().inverse()*cf;
            b->teleportTo(local.Position); b->setRotation(local.Rotation); b->setSize(sz);
        }
        else e.target->Size=sz;
        if(!e.target->IsA("BaseCube")) e.target->setWorldCFrame(cf);
    }
}
GizmoCommand::GizmoCommand(std::shared_ptr<BaseCube> t,GizmoState b,GizmoState a):m_target(std::move(t)),m_before(b),m_after(a){}
void GizmoCommand::execute(){apply(m_after);} void GizmoCommand::undo(){apply(m_before);} void GizmoCommand::apply(const GizmoState&s){if(!m_target)return;m_target->teleportTo(s.position);m_target->setSize(s.size);m_target->setRotation(s.rotation);}
MultiGizmoCommand::MultiGizmoCommand(std::vector<Entry> e):m_entries(std::move(e)){}
void MultiGizmoCommand::execute(){for(auto&e:m_entries)applyState(e.target,e.after);} void MultiGizmoCommand::undo(){for(auto&e:m_entries)applyState(e.target,e.before);}
void MultiGizmoCommand::applyState(const std::shared_ptr<Spatial>& sp,const GizmoState&s){if(!sp||sp->Parent.expired())return;if(sp->IsA("BaseCube")){auto*b=static_cast<BaseCube*>(sp.get());b->teleportTo(s.position);b->setSize(s.size);b->setRotation(s.rotation);}else{sp->Position=s.position;sp->Size=s.size;sp->Rotation=s.rotation;}}

SetDecalColorCommand::SetDecalColorCommand(std::shared_ptr<Decal> t,Color4 b,Color4 a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetDecalColorCommand::execute(){if(m_target)m_target->Color=m_after;} void SetDecalColorCommand::undo(){if(m_target)m_target->Color=m_before;}
SetDecalFaceCommand::SetDecalFaceCommand(std::shared_ptr<Decal> t,Face b,Face a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetDecalFaceCommand::execute(){if(m_target)m_target->setFace(m_after);} void SetDecalFaceCommand::undo(){if(m_target)m_target->setFace(m_before);}
SetDecalModeCommand::SetDecalModeCommand(std::shared_ptr<Decal> t,DecalMode b,DecalMode a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetDecalModeCommand::execute(){if(m_target)m_target->Mode=m_after;} void SetDecalModeCommand::undo(){if(m_target)m_target->Mode=m_before;}
SetDecalTextureCommand::SetDecalTextureCommand(std::shared_ptr<Decal> t,std::string bp,unsigned int bi,std::string ap,unsigned int ai):m_target(std::move(t)),m_beforePath(std::move(bp)),m_afterPath(std::move(ap)),m_beforeID(bi),m_afterID(ai){}
void SetDecalTextureCommand::execute(){if(m_target){m_target->texturePath=m_afterPath;m_target->TextureID=m_afterID;}} void SetDecalTextureCommand::undo(){if(m_target){m_target->texturePath=m_beforePath;m_target->TextureID=m_beforeID;}}
SetDecalUVCommand::SetDecalUVCommand(std::shared_ptr<Decal> t,Vector2 bc,float br,Vector2 ac,float ar):m_target(std::move(t)),m_beforeCenter(bc),m_afterCenter(ac),m_beforeRadius(br),m_afterRadius(ar){}
void SetDecalUVCommand::execute(){if(m_target){m_target->UVCenter=m_afterCenter;m_target->UVRadius=m_afterRadius;}} void SetDecalUVCommand::undo(){if(m_target){m_target->UVCenter=m_beforeCenter;m_target->UVRadius=m_beforeRadius;}}
SetTextureFaceCommand::SetTextureFaceCommand(std::shared_ptr<Texture> t,Face b,Face a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetTextureFaceCommand::execute(){if(m_target)m_target->setFace(m_after);} void SetTextureFaceCommand::undo(){if(m_target)m_target->setFace(m_before);}
SetTextureTextureCommand::SetTextureTextureCommand(std::shared_ptr<Texture> t,std::string bp,unsigned int bi,std::string ap,unsigned int ai):m_target(std::move(t)),m_beforePath(std::move(bp)),m_afterPath(std::move(ap)),m_beforeID(bi),m_afterID(ai){}
void SetTextureTextureCommand::execute(){if(m_target){m_target->texturePath=m_afterPath;m_target->TextureID=m_afterID;}} void SetTextureTextureCommand::undo(){if(m_target){m_target->texturePath=m_beforePath;m_target->TextureID=m_beforeID;}}
SetTextureColorCommand::SetTextureColorCommand(std::shared_ptr<Texture> t,Color4 b,Color4 a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetTextureColorCommand::execute(){if(m_target)m_target->Color=m_after;} void SetTextureColorCommand::undo(){if(m_target)m_target->Color=m_before;}
SetTextureStudsCommand::SetTextureStudsCommand(std::shared_ptr<Texture> t,float bu,float bv,float au,float av):m_target(std::move(t)),m_beforeU(bu),m_afterU(au),m_beforeV(bv),m_afterV(av){}
void SetTextureStudsCommand::execute(){if(m_target){m_target->StudsPerTileU=m_afterU;m_target->StudsPerTileV=m_afterV;}} void SetTextureStudsCommand::undo(){if(m_target){m_target->StudsPerTileU=m_beforeU;m_target->StudsPerTileV=m_beforeV;}}
SetSoundBoolCommand::SetSoundBoolCommand(std::shared_ptr<Sound> t,std::string p,bool b,bool a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetSoundBoolCommand::execute(){apply(m_after);} void SetSoundBoolCommand::undo(){apply(m_before);} void SetSoundBoolCommand::apply(bool v){if(!m_target)return;if(m_prop=="AutoPlay")m_target->autoPlay=v;else if(m_prop=="Looped")m_target->setLooping(v);else if(m_prop=="PreservePitch")m_target->setPreservePitch(v);}
SetSoundFloatCommand::SetSoundFloatCommand(std::shared_ptr<Sound> t,std::string p,float b,float a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetSoundFloatCommand::execute(){apply(m_after);} void SetSoundFloatCommand::undo(){apply(m_before);} void SetSoundFloatCommand::apply(float v){if(!m_target)return;if(m_prop=="Volume")m_target->setVolume(v);else if(m_prop=="Speed")m_target->setSpeed(v);}
SetLightDirCommand::SetLightDirCommand(std::shared_ptr<Lighting> t,Vector3 b,Vector3 a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetLightDirCommand::execute(){if(m_target)m_target->lightDir=m_after;} void SetLightDirCommand::undo(){if(m_target)m_target->lightDir=m_before;}
SetLightBrightnessCommand::SetLightBrightnessCommand(std::shared_ptr<Lighting> t,float b,float a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetLightBrightnessCommand::execute(){if(m_target)m_target->brightness=m_after;} void SetLightBrightnessCommand::undo(){if(m_target)m_target->brightness=m_before;}
SetPostEffectBoolCommand::SetPostEffectBoolCommand(std::shared_ptr<PostEffect> t,std::string p,bool b,bool a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetPostEffectBoolCommand::execute(){apply(m_after);} void SetPostEffectBoolCommand::undo(){apply(m_before);} void SetPostEffectBoolCommand::apply(bool v){if(m_target&&m_prop=="Enabled")m_target->Enabled=v;}
SetPostEffectIntCommand::SetPostEffectIntCommand(std::shared_ptr<PostEffect> t,std::string p,int b,int a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetPostEffectIntCommand::execute(){apply(m_after);} void SetPostEffectIntCommand::undo(){apply(m_before);} void SetPostEffectIntCommand::apply(int v){if(m_target&&m_prop=="ZIndex")m_target->ZIndex=v;}
SetPostEffectTypeCommand::SetPostEffectTypeCommand(std::shared_ptr<PostEffect> t,PostEffectKind b,PostEffectKind a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetPostEffectTypeCommand::execute(){if(m_target)m_target->Type=m_after;} void SetPostEffectTypeCommand::undo(){if(m_target)m_target->Type=m_before;}
SetPostEffectFloatCommand::SetPostEffectFloatCommand(std::shared_ptr<PostEffect> t,std::string p,float b,float a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetPostEffectFloatCommand::execute(){apply(m_after);} void SetPostEffectFloatCommand::undo(){apply(m_before);} void SetPostEffectFloatCommand::apply(float v){if(!m_target)return;if(m_prop=="Intensity")m_target->Intensity=v;else if(m_prop=="Param1")m_target->Param1=v;else if(m_prop=="Param2")m_target->Param2=v;}
SetSkyboxFaceCommand::SetSkyboxFaceCommand(std::shared_ptr<Skybox> t,int i,std::string b,std::string a):m_target(std::move(t)),m_faceIndex(i),m_before(std::move(b)),m_after(std::move(a)){}
void SetSkyboxFaceCommand::execute(){if(m_target)m_target->setSkyboxPath(m_faceIndex,m_after);} void SetSkyboxFaceCommand::undo(){if(m_target)m_target->setSkyboxPath(m_faceIndex,m_before);}
SetConstraintCubeNameCommand::SetConstraintCubeNameCommand(std::shared_ptr<Instance> t,std::string p,std::string b,std::string a):m_target(std::move(t)),m_prop(std::move(p)),m_before(std::move(b)),m_after(std::move(a)){}
void SetConstraintCubeNameCommand::execute(){apply(m_after);} void SetConstraintCubeNameCommand::undo(){apply(m_before);} void SetConstraintCubeNameCommand::apply(const std::string&v){if(!m_target)return;YAML::Node n;n=v;m_target->setProperty(m_prop,n);}
SetNumberValueCommand::SetNumberValueCommand(std::shared_ptr<Instance> t,double b,double a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetNumberValueCommand::execute(){apply(m_after);} void SetNumberValueCommand::undo(){apply(m_before);} void SetNumberValueCommand::apply(double v){if(!m_target)return;YAML::Node n;n=v;m_target->setProperty("Value",n);}
SetQuaternionValueCommand::SetQuaternionValueCommand(std::shared_ptr<Instance> t,Quaternion b,Quaternion a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetQuaternionValueCommand::execute(){apply(m_after);} void SetQuaternionValueCommand::undo(){apply(m_before);} void SetQuaternionValueCommand::apply(const Quaternion&v){if(!m_target)return;YAML::Node n;n.push_back(v.x);n.push_back(v.y);n.push_back(v.z);n.push_back(v.w);m_target->setProperty("Value",n);}
SetCFrameValueCommand::SetCFrameValueCommand(std::shared_ptr<Instance> t,CFrame b,CFrame a):m_target(std::move(t)),m_before(b),m_after(a){}
void SetCFrameValueCommand::execute(){apply(m_after);} void SetCFrameValueCommand::undo(){apply(m_before);} void SetCFrameValueCommand::apply(const CFrame&v){if(!m_target)return;YAML::Node n,pos,rot;pos.push_back(v.Position.x);pos.push_back(v.Position.y);pos.push_back(v.Position.z);rot.push_back(v.Rotation.x);rot.push_back(v.Rotation.y);rot.push_back(v.Rotation.z);rot.push_back(v.Rotation.w);n["Position"]=pos;n["Rotation"]=rot;m_target->setProperty("Value",n);}
SetRopeFloatCommand::SetRopeFloatCommand(std::shared_ptr<Rope>t,std::string p,float b,float a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetRopeFloatCommand::execute(){apply(m_after);}void SetRopeFloatCommand::undo(){apply(m_before);}void SetRopeFloatCommand::apply(float v){if(!m_target)return;if(m_prop=="MaxDistance")m_target->setMaxDistance(v);else if(m_prop=="Stiffness")m_target->setStiffness(v);else if(m_prop=="Damping")m_target->setDamping(v);}
SetMotorFloatCommand::SetMotorFloatCommand(std::shared_ptr<Motor>t,std::string p,float b,float a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}
void SetMotorFloatCommand::execute(){apply(m_after);}void SetMotorFloatCommand::undo(){apply(m_before);}void SetMotorFloatCommand::apply(float v){if(!m_target)return;if(m_prop=="DriveVelocity")m_target->setDriveVelocity(v);else if(m_prop=="MaxForce")m_target->setMaxForce(v);}
SetRodColorCommand::SetRodColorCommand(std::shared_ptr<Rod>t,Color4 b,Color4 a):m_target(std::move(t)),m_before(b),m_after(a){}void SetRodColorCommand::execute(){if(m_target)m_target->Color=m_after;}void SetRodColorCommand::undo(){if(m_target)m_target->Color=m_before;}
SetRodLineWidthCommand::SetRodLineWidthCommand(std::shared_ptr<Rod>t,float b,float a):m_target(std::move(t)),m_before(b),m_after(a){}void SetRodLineWidthCommand::execute(){if(m_target)m_target->LineWidth=m_after;}void SetRodLineWidthCommand::undo(){if(m_target)m_target->LineWidth=m_before;}
SetRopeColorCommand::SetRopeColorCommand(std::shared_ptr<Rope>t,Color4 b,Color4 a):m_target(std::move(t)),m_before(b),m_after(a){}void SetRopeColorCommand::execute(){if(m_target)m_target->Color=m_after;}void SetRopeColorCommand::undo(){if(m_target)m_target->Color=m_before;}
SetRopeLineWidthCommand::SetRopeLineWidthCommand(std::shared_ptr<Rope>t,float b,float a):m_target(std::move(t)),m_before(b),m_after(a){}void SetRopeLineWidthCommand::execute(){if(m_target)m_target->LineWidth=m_after;}void SetRopeLineWidthCommand::undo(){if(m_target)m_target->LineWidth=m_before;}
SetMotorAxisCommand::SetMotorAxisCommand(std::shared_ptr<Motor>t,Vector3 b,Vector3 a):m_target(std::move(t)),m_before(b),m_after(a){}void SetMotorAxisCommand::execute(){if(m_target)m_target->setAxis(m_after);}void SetMotorAxisCommand::undo(){if(m_target)m_target->setAxis(m_before);}
SetScriptBoolCommand::SetScriptBoolCommand(std::shared_ptr<Script>t,std::string p,bool b,bool a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}void SetScriptBoolCommand::execute(){apply(m_after);}void SetScriptBoolCommand::undo(){apply(m_before);}void SetScriptBoolCommand::apply(bool v){if(m_target&&m_prop=="Enabled")m_target->Enabled=v;}
SetSystemIntCommand::SetSystemIntCommand(std::shared_ptr<System>t,std::string p,int b,int a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}void SetSystemIntCommand::execute(){apply(m_after);}void SetSystemIntCommand::undo(){apply(m_before);}void SetSystemIntCommand::apply(int v){if(!m_target)return;if(m_prop=="MaxClonesPerFrame")m_target->MaxClonesPerFrame=v;else if(m_prop=="MaxRestartsPerFrame")m_target->MaxRestartsPerFrame=v;else if(m_prop=="MaxTasksPerFrame")m_target->MaxTasksPerFrame=v;}
SetSystemFloatCommand::SetSystemFloatCommand(std::shared_ptr<System>t,std::string p,float b,float a):m_target(std::move(t)),m_prop(std::move(p)),m_before(b),m_after(a){}void SetSystemFloatCommand::execute(){apply(m_after);}void SetSystemFloatCommand::undo(){apply(m_before);}void SetSystemFloatCommand::apply(float v){if(m_target&&m_prop=="ScriptLoopTimeoutSeconds")m_target->ScriptLoopTimeoutSeconds=v;}
