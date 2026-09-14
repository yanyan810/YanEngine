# FPS Foundation

旧2.5Dバトルゲームを参照解除し、新しいFPS制作のための最小シーンへ整理した状態です。

- 固定カメラ
- Cubeの仮Player
- ResourcesのBossモデルを使った静止Enemy 1体
- 地面とライト

FPS移動・射撃・敵AI・部位破壊はまだ実装していません。
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
