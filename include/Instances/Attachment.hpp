#pragma once
#include <Instances/Spatial.hpp>
#include <memory>
#include <string>

// BaseCube の子として置く、位置と向き(CFrame)だけを持つアンカー。
// Motor/Rope/Rod は Attachment0/Attachment1 が設定されていればこの位置で制約を生成する。
// Size は使用しない（Spatial 基底の要求で保持するのみ）。
class Attachment : public Spatial {
public:
    Attachment(Vector3 Pos = {0, 0, 0}) : Spatial(Pos, {0, 0, 0}, "Attachment") {}
    std::string getClassName() override { return "Attachment"; }
    bool IsA(std::string name) override {
        if (name == "Attachment") return true;
        return Spatial::IsA(name);
    }
    std::shared_ptr<Instance> clone() const override;

    // root(通常は制約の対象Cube)配下の子孫パスから Attachment を解決する。
    // 見つからないか Attachment でなければ nullptr。
    static std::shared_ptr<Attachment> findUnder(Instance* root, const std::string& path);

    // 制約のAttachment参照を解決する。保存形式はcube基準の子孫パスだが、エディターの
    // ピッカーはWorkspace相対パスを代入するため、cube基準で見つからなければ
    // Workspace(無ければ最上位祖先)基準でも探す。cubeの子孫でなければnullptr。
    static std::shared_ptr<Attachment> resolveReference(Instance* cube, const std::string& path);

    // エディターで新規追加する用のAttachmentを作る。親(Spatialでなければ最寄りのSpatial祖先)の
    // ワールドCFrameに置くので、親へ追加した後(setParentはワールド姿勢を保つ)のlocalが単位になる。
    static std::shared_ptr<Attachment> createAtParent(const Instance& parent);

    // 祖先 ancestor に対する相対 CFrame を返す（直下の子でなくても親チェーンを合成する）。
    // ancestor が祖先に見つからない場合は自身のローカル cframe をそのまま返す。
    CFrame relativeToAncestor(const Instance* ancestor) const;
};
