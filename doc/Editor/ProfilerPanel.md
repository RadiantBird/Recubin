# ProfilerPanel

`View > Profiler`から開く、直近240フレームのCPU区間時間を表示するDockパネル。

## グラフ

| グラフ | `FrameProfiler` section | 計測範囲 |
|---|---|---|
| 描画 | `render` | 全Viewportの3D描画、Editor UI生成、ImGui描画。メインウィンドウの`glfwSwapBuffers`は含まない |
| 物理計算 | `physics` | Play中の各Workspaceに対する`Physics::update()`の合計 |
| スクリプト実行 | `luau` | Workspace/System Script、Heartbeat、Luau task更新の合計 |

各グラフは現在値、履歴平均、履歴最大をミリ秒で表示する。Playしていないフレームでは物理と
スクリプトは0msとして記録される。描画値はOpenGL GPU timerではなくCPU側の経過時間であり、
GPU timerの値ではない。通常のDock状態ではメインウィンドウのVSync待ちは表さないが、ImGuiパネルを
別OSウィンドウへ分離した場合は、そのplatform window描画・presentが区間内に含まれる。

## 描画処理の内訳

描画グラフの下に、次の区間の現在値、履歴平均、履歴最大を表示する。

- Shadow、Main Geometry、Surface Marks
- Extra Passes Total
- Highlights、Constraints、Debug Rendering
- Terrain、Weather、Particles、Selection Outline、Post Effects
- Editor UI、Swap / VSync

`Extra Passes Total`はHighlightsからPost Effectsまでの親区間で、`Editor UI`はEditor内のViewport描画も
含む親区間である。親子区間は重複するため、テーブルの全行を合算して総描画時間として扱わない。

描画数テーブルには、フレームごとのCubes Drawn、Cubes Culled、Instanced Cubes、Shadow Cubesを表示する。
複数Viewportが描画された場合は、そのフレームに描画された全Viewportの合計となる。

履歴は`FrameProfiler`内部の固定長リングバッファで保持し、フレームごとの動的確保を行わない。
