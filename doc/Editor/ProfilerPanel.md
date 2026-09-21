# ProfilerPanel

`View > Profiler`から開く、直近240フレームのCPU区間時間を表示するDockパネル。

パネル上部には現在FPS、直近240フレームの平均FPS、平均フレーム時間を表示する。FPSはCPU区間の
合計から推定せず、swap後の`endFrame()`同士の実時間隔から算出するため、VSyncやidle待機を含む。
平均FPSは各フレームFPSの算術平均ではなく、`1000 / 平均フレーム時間(ms)`で算出する。
このFPSヘッダーは固定表示され、下の計測テーブルとグラフだけがスクロールする。

## GPU時間

OpenGL timestamp queryでGPU実行時間を計測する。

| 行 | 計測範囲 |
|---|---|
| GPU描画全体 | メインOpenGL contextの`Renderer::render()`開始からEditor/UIのGL command送信完了まで。`glfwSwapBuffers`は含まない |
| Shadow | 最初に描画された有効Viewportのshadow pass |
| Main Geometry | 同Viewportのmain geometry pass |
| Surface Marks | 同ViewportのSurfaceMark pass |
| Extra Passes Total | 同Viewportのhighlightからpost effectまで |

queryは4フレーム分の固定ringに発行し、最終timestampが利用可能になった後だけ結果を回収する。
GPUが遅れて全slotが使用中の場合は、CPUを待たせずそのフレームの計測を省略する。
このため値は数フレーム遅れて表示され、通常のCPU sectionとは別の履歴に保存される。
VSyncやpresent待機はGPU時間に含まれない。timestamp queryを使用できない環境では
`利用不可` / `Unavailable`と表示する。

複数Viewportがある場合、4つのpass内訳は最初のViewportのみを非重複区間として計測する。
GPU描画全体はメインcontextに送信された全ViewportとDock内UIを含むが、ImGuiが別OSウィンドウ用の
別contextへ送信するplatform window描画を厳密に含む値ではない。また、複数Viewport時に内訳の合計が
GPU描画全体と一致するとは限らない。

## グラフ

| グラフ | `FrameProfiler` section | 計測範囲 |
|---|---|---|
| 描画 | `render` | 全Viewportの3D描画、Editor UI生成、ImGui描画。メインウィンドウの`glfwSwapBuffers`は含まない |
| 物理計算 | `physics` | Play中の各Workspaceに対する`Physics::update()`の合計 |
| スクリプト実行 | `luau` | Workspace/System Script、Heartbeat、Luau task更新の合計 |

各グラフは現在値、履歴平均、履歴最大をミリ秒で表示する。Playしていないフレームでは物理と
スクリプトは0msとして記録される。これらのグラフはCPU側の経過時間であり、上記GPU時間とは別の値である。
通常のDock状態ではメインウィンドウのVSync待ちは表さないが、ImGuiパネルを
別OSウィンドウへ分離した場合は、そのplatform window描画・presentが区間内に含まれる。

## 描画処理の内訳

描画グラフの下に、次の区間の現在値、履歴平均、履歴最大を表示する。

- Shadow、Main Geometry、Surface Marks
- Extra Passes Total
- Highlights、Constraints、Debug Rendering
- Terrain、Weather、Particles、Selection Outline、Post Effects、SurfaceGui Bakes
- Editor UI、Swap / VSync

`Extra Passes Total`はHighlightsからPost Effectsまでの親区間で、`Editor UI`はEditor内のViewport描画も
含む親区間である。親子区間は重複するため、テーブルの全行を合算して総描画時間として扱わない。

描画数テーブルには、フレームごとのCubes Drawn、Cubes Culled、Instanced Cubes、Shadow Cubes、
Shadow Cubes Culled、SurfaceGui Baked、SurfaceGui Reusedを表示する。SurfaceGui Bakedは実際に
FBOを更新した数、SurfaceGui Reusedはサイズ・描画内容署名が一致して既存テクスチャを再利用した数である。
複数Viewportが描画された場合は、そのフレームに描画された全Viewportの合計となる。

履歴は`FrameProfiler`内部の固定長リングバッファで保持し、フレームごとの動的確保を行わない。

`Settings > Vertical Sync (diagnostic)`でメインウィンドウのVSyncを切り替えられる。既定値は有効で、
設定は`editor_settings.yaml`の`Preferences.VSync`へ保存される。無効化はGPU/CPUの上限性能を調べる
診断用途であり、ティアリングが発生しうる。
