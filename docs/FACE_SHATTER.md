# Face ShatterとPDB競合対策

## Face

標準はFace、ImGuiのBreak ModeでChunkへ切替可能です。各部位の事前分類済み三角形をresources/enemy/boss/faces/faces.json（1ファイル）へまとめました。head 202 / body 134 / arms各86 / legs各102、計712。既存の6部位Assetを読み取るため分類ルールは完全に同じです。

生成：
```powershell
blender --background --python-exit-code 1 --python tools/blender/split_boss_parts.py -- --faces-only
```

Blender 5.0.1で生成確認済み。4.4互換コード実装済み・実機未検証です。元bossと既存parts/fragmentsは変更していません。

各Faceは元頂点をEnemy World行列で変換した位置から開始。三角形の重心をPivotとして、既存DetachedPartMotionを共用します。ワールド空間に保持する頂点をEnemy専用Modelへまとめ、既存UpdateVertexPositionで更新します。Enemyあたり1つのObject3d・1回のDrawでFaceを描画し、Shaderや共通Model仕様は変更していません。逆向きTriangleも同じバッファへ入れるため両面表示です。材質は赤、ライティングなしです。

一括描画Object3dのWVPは毎Drawで現在のCameraから更新します。初版の初期化時だけの更新では視点変更後に画面へ貼り付く問題があり、ユーザー報告を受けて修正しました。

上限：Max Active Face Shardsは全Enemy合計256（1～1024）、Max Face Shards Per Breakは64（1～1024）、Face Shard Lifetimeは5秒。超過する部位は均等選択するため、初期値では全表面が残るわけではありません。選ばれたFaceの初期位置は元表面と一致します。全Faceを確認するにはPer Breakを202以上へ上げてください。全6部位を同時に残す場合はActiveも712以上にします。

Faceデータがない／読み込みに失敗した部位はChunkへ、Chunkもなければ単体部位へFallback。既存Chunk/単体部位の全Enemy合計128個制限とは別枠です。どちらも最古から削除します。上限合計の初期値は384個です。物理は各Faceごと、頂点バッファは最大1024枚分を固定確保して使い回します。

## PDB

当初のCG2_Setupは/MP有効、/FS恒久設定なし、既定vc143.pdbを使用。IntDirは他Projectとは分かれていました。実行中のcl.exeはなく、待機MSBuildノードとmspdbsrvが残っていました。これだけではC2471の単一原因は断定できませんが、過去のPDB競合も含めて対策しました。

- 待機中の確認済みBuild/PDBサーバーを停止。
- 正確に解決したgenerated/obj/CG2_Setup/DebugのみClean（ソース・Resourcesは削除なし）。
- IntDirをgenerated/obj/$(ProjectName)/$(Platform)/$(Configuration)/へ。
- Compiler PDBを$(IntDir)$(ProjectName).compiler.pdbへ明示。
- Debug / Development / Releaseすべてに/FSを追加し/MPを維持。

対象プロジェクトのCMake/Premake生成元は見つからず、既存vcxprojへ修正しました。Windows SDKは変更していません。ビルドログの実clコマンドで/FS、/MP、構成別/Fdを確認しました。同一構成のビルドを別プロセスから同時に実行することまでは保証しないため、通常はビルド完了後に次を実行します。

## 検証

既存Raycast/HP/物理テスト成功。三角形重心Pivotのテストを追加し成功。全712三角形が元の分類済みAssetと一致し、均等選択に重複がないことを確認しました。初版Debug/Releaseとも警告0・エラー0でCleanビルド成功。画面貼り付き修正版もDebug/Releaseとも警告0・エラー0。修正版を起動しましたが、物理ESCでComputer Useが停止したため、修正後の実操作再確認は未完了です。

修正版の実行ファイル：プロジェクトの一階層上のgenerated/verified-face-camera/{Debug,Release}/CG2_Setup.exe。
