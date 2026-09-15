# FPS Foundation

旧2.5Dバトルゲームを参照解除し、新しいFPS制作のための最小シーンへ整理した状態です。

- 一人称カメラ（WASD移動・マウス視点）
- Transformを持つPlayer（仮Cubeは一人称では非表示）
- ResourcesのBossモデルを使った静止Enemy 1体
- 地面とライト

左クリック1回につき1発のHitscan射撃を実装しています。Bossを6部位のAABBで判定し、命中部位名を表示します。各部位に独立HPを持ち、1発25ダメージを適用します。Debugでは判定枠がダメージ段階別の色になります（モデル表面の部位着色は未対応）。敵AI・切断は未実装です。
描画、Object3d、Model、Animation、Input、Collision、Sound、Particle、Sprite、ImGui、SceneManager等のエンジン機能は保持しています。

## ビルド

Visual Studioで `CG2_Setup.sln` を開き、x64のDebugまたはReleaseをビルドしてください。
v143 C++ツールセットとWindows SDKが必要です。
スタートアッププロジェクトは `CG2_Setup` です。

PowerShellでは `./tools/build.ps1 -Configuration Debug` も利用できます。
実行ファイルは一階層上の `generated/outputs/Debug/CG2_Setup.exe` に出力されます。

## 整理内容

旧コードは `archive/legacy-2.5d` に退避しており、ビルド・起動経路から外しています。
退避ファイル一覧は `archive/legacy-2.5d/files.txt`、詳細は `docs/FPS_FOUNDATION.md` を参照してください。
Resourcesは削除していません。

## FPS操作と射撃

WASDで移動、マウスで視点操作、左クリックで射撃、ESCで操作を解除します。
Scene画像（Releaseはゲーム画面）をクリックすると復帰します。この復帰クリックでは発射しません。
Shot Count / Hit CountはウィンドウタイトルとDebugのFPS Controlsに表示します。
現在の部位判定と制約は docs/FPS_ENEMY_PARTS.md、射撃の基礎は docs/FPS_SHOOTING.md を参照してください。

Bossの構造調査と分割Assetの受け入れ条件は [BOSS_PART_VISUALS.md](docs/BOSS_PART_VISUALS.md) を参照してください。
