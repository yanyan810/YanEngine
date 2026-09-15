# Boss分割Asset生成

## 実行

プロジェクトルートから、PATH上のBlenderまたは任意のBlender実行ファイルで実行します。スクリプトにBlenderのインストールパスは固定していません。

```powershell
blender --background --python-exit-code 1 --python tools/blender/split_boss_parts.py
python tools/blender/validate_boss_parts.py
```

Blender 4.4系のAPIを想定していますが、この環境には5.0.1のみ存在するため実行検証は5.0.1で行いました。4.4実機での実行は未確認です。5.0のMaterial.use_nodes非推奨警告は4.4互換性維持のため残しています。

任意の出力先は `-- --output PATH` で指定できます。既定はresources/enemy/boss/partsです。作業中の.blendは保存しません。バックグラウンド実行を推奨します。

## 分割方式・座標

元boss.gltfをimportし、Armature Modifierを持つ本体Meshだけを抽出します。骨の補助表示Meshは対象外。アニメーションを評価せず、元Meshの静止形状を使います。インポーターがSkinに対して適用した逆Bind変換をObject行列で戻し、元glTF POSITIONのBoundsと一致することを検証します。

EnemyParts.hのbox(...)の値を直接読み取り、Face中心で分類します。glTF Yが高さ、Zが左右（+Zが敵の左）。Blender座標(x,y,z)からglTFへは(x,z,-y)です。Boxが重なる場合はC++と同じ列挙順で決定、範囲外は最寄りBoxを採用します。現在は範囲外Faceが0です。

各Faceは一部位だけへ移し、切断・穴埋め・再中心化はしません。各部位のObject TransformはIdentityで、全体と同じ位置関係を頂点に保持します。Faceが境界をまたぐ場合の多少のHitBoxとの差は残ります。

白い単一Material、Skin/Animationなし、各glTFと同名binの組で出力します。6ファイルの検証後に出力先へコピーします。元boss.gltfは変更せずSHA256照合を行います。

## 生成結果

| 部位 | 頂点数（分割Mesh） | Face数 |
| --- | ---: | ---: |
| Head | 602 | 202 |
| Body | 354 | 134 |
| LeftArm | 116 | 86 |
| RightArm | 116 | 86 |
| LeftLeg | 244 | 102 |
| RightLeg | 248 | 102 |

各Boundsはresources/enemy/boss/parts/split_report.jsonへ記録。全712三角形について、別スクリプトで元Assetと座標・頂点の巡回順を照合しました。誤差1e-5以内で重複・欠落なし。glTFエクスポーターが法線等で頂点を複製する場合、ファイル内の頂点数は上表と異なる場合があります。

## ゲーム

GameSceneでuseSplitAssets=trueを指定しました。6Assetのいずれかが欠損した場合は従来Boss表示へ戻ります。依存binは必ずglTFと一緒に配置してください。既存の部位HP・専用Object3d材質・Destroyed時Draw省略を使用します。ColliderはDestroyed後も残ります。

Debug/Release x64：警告0・エラー0。既存Raycast/HPテスト成功。Debugゲームで6部位が合成された白いBossを確認しました。ライトにより暗い面が見えますが、材質の基準色は白です。

今回の実行ファイルは一階層上のgenerated/verified-split-assets/{Debug,Release}/CG2_Setup.exeです。作業ディレクトリをプロジェクトルートにして起動してください。
