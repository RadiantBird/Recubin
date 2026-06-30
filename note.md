デストラクタが走るのが遅すぎて破棄前の処理ができない。
removeChildを改造するか
結論
->破棄前の処理(Parent == nullptr)はonAncestorChangeに書き込めばいい