# Blenderでステージを作る

YanEngine Levelアドオンを使い、Blenderの配置から見た目のglTFとゲーム設定のJSONをまとめて出力できます。本番ゲームUIは追加していません。

## まず付属サンプルを開く

編集用ファイルは `resources/levels/stage01/stage01.blend`、アドオンは `tools/blender/yanengine_level_exporter.py` です。Blender 4.4以降を対象にしており、今回の自動検証環境はBlender 5.0.1です。

### 1. Add-onを導入する

1. Blenderを起動します。
2. **Edit → Preferences → Add-ons** を開きます。
3. 右上のメニューから **Install from Disk**（日本語表示ではディスクからインストール）を選びます。
4. `tools/blender/yanengine_level_exporter.py` を選び、インストールします。
5. **YanEngine Level** を有効にします。
6. `stage01.blend` を開きます。3D Viewportにマウスを置いて **N** キーを押し、右側の **YanEngine Level** タブを開きます。

Object Typeなどの設定は.blendへ保存されます。変更後は **Ctrl+S** で保存してください。Exportは.blend自体の保存を代行しません。

### 2. Stage IDとOutput Directoryを設定する

パネル上部はScene全体の設定です。

|項目|Stage01の設定|
|---|---|
|Stage ID|`stage01`|
|Project Root|`CG2_Setup.sln` と `resources` があるフォルダー|
|Output Directory|`resources/levels/stage01`|
|Fixed Seed|抽選を再現したい場合にON|

付属.blendのProject Rootは `//../../../` です。.blendから見た相対パスなので、プロジェクトごと別のPCへ移動できます。.blendだけ別の場所へ移す場合はProject Rootを実際のプロジェクトフォルダーに直してください。

出力先の末尾フォルダー名はStage IDと同じにします。Project Root設定時は `Project Root/resources/levels/Stage ID` へ出力します。Project Rootを空にして、絶対パスのOutput Directoryへ出すことも可能ですが、その場合はEnemy/Weapon IDの存在確認を省略します。

現在のGameSceneが起動時に読むのは `resources/levels/stage01/stage01.json` です。まずはStage IDをstage01のままで編集してください。別IDへ出したステージを起動するにはGameSceneのlevelPathを変更します。

### 3. FloorやWallをStatic Meshにする

1. **Shift+A → Mesh → Cube** などで形を作ります。
2. **G**で移動、**R**で回転、**S**で大きさを変更します。
3. 対象を選択し、パネルの **YanEngine Object Type** を **Static Mesh** にします。
4. Floor、Wallなど分かる名前を付けます。

Static MeshだけがglTFの見た目へ出力されます。Modifierを評価したメッシュと親を含む配置を一時メッシュへ焼き込みます。元のオブジェクト・選択状態・Sceneは保持します。Blender単位1をゲーム単位1として扱います。

### 4. Collisionを設定する

|Collision|動作|
|---|---|
|None|描画のみ。壁として射撃や移動を止めません。|
|Box|そのStatic MeshのローカルBounding Boxを使います。|
|Custom|別オブジェクトをCollider Object欄に指定します。|

複雑な岩や建物は、見た目と単純な当たり判定を分けられます。

1. Cubeを追加し、`COL_Building` などの名前にします。
2. Object Typeを **Collider** にします。Meshの場合はワイヤー表示になります。
3. 建物の当たり判定にしたい形へ移動・回転・拡縮します。
4. 建物側のStatic Meshを選び、CollisionをCustom、Collider ObjectをそのCubeにします。

Colliderはゲームでは見えません。Colliderを単独で配置することもでき、全Colliderが有効になります。Static Mesh側をNoneにして独立したColliderを置いても構いません。Customは二重の判定を作らず、参照したColliderを使用します。

Colliderは回転できます。ローカルAABBとWorld Transformを保持するので、斜めの壁にも対応します。サイズ0・負Scale・親の非均一Scaleによるシアーは検証エラーです。必要ならObject → Applyで変換を適用し、形を確定してください。

床の上面はPlayer Spawnの足元の高さへ合わせてください。現段階は水平移動を前提とし、階段・斜面歩行・落下・ジャンプは追加していません。マップ外へ出ないよう、外周には壁Colliderを置きます。

### 5. Player Spawnを置く

1. **Shift+A → Empty → Arrows** を追加します。
2. Object Typeを **Player Spawn** にします。
3. プレイヤーの足元を開始させたい場所に置きます。
4. 向きを変更したい場合はBlenderのZ軸回転を使います。

Player SpawnはScene内に必ず1つ置いてください。0個・複数個はExportエラーです。Blenderの **-Y方向がゲームの前方+Z**、**Zが高さ**です。プレイヤーの無回転時の視線はBlender -Y方向になります。Player Spawnを壁の中へ置かないでください。

ゲーム開始とRestart Stageで、この位置・回転を適用します。カメラは既存の目線高さを足した位置になります。

### 6. Enemy Spawnを置く

1. Emptyを追加し、Object Typeを **Enemy Spawn** にします。
2. IDを設定します。空欄ならObject名を使用します。
3. **Spawn Group** に `A` などを入力します。
4. 移動・回転でスポーン場所を設定します。

Enemy Spawnそのものはゲームに表示されません。IDは他の有効オブジェクトと重複させないでください。敵の中心が壁に重ならない位置を選びます。

### 7. Enemy Poolを設定する

**Add Entry** を押すとIDとWeightの行が増えます。右のマイナスボタンで行を削除できます。

例：normal=5、fast=2、ranged=2、tank=1、bomber=1。

IDは `resources/Data/enemies.json` のidです。任意のIDを文字入力できます。Project Root設定時はそのファイルと照合するので、入力間違いはExport時に検出されます。weightは0以上、合計は0より大きい値にします。空Poolはエラーです。確実に1種類だけ出すには、そのID・Weight=1の行だけを残します。

### 8. Spawn Triggerを置く

1. **Shift+A → Empty → Cube** を追加します。
2. Object Typeを **Spawn Trigger** にします。
3. **Spawn Group** を対応するEnemy Spawnと同じ `A` にします。
4. Cube Emptyを移動・拡縮し、プレイヤーが通る場所を囲みます。

同じGroupのEnemy Spawn IDをExporterが自動収集します。TriggerへSpawnPoint IDを列挙する必要はありません。同じGroupにEnemy Spawnが0個ならExportエラーです。

Trigger／Goalは既存の軸平行AABBです。斜め回転はエラーにします（90度単位の回転は対応）。Cube EmptyはDisplay SizeとScaleを含む実際の箱サイズを使用します。MeshのCubeも使用できます。

### 9. 敵数とタイミングを設定する

|項目|例|意味|
|---|---:|---|
|Spawn Count|8|このTriggerが出す総数|
|Spawn Interval|0.5|敵を出す間隔、秒|
|Initial Delay|0|Triggerへ入ってから最初の敵まで、秒|
|Max Alive|4|このTriggerの敵の同時生存上限|
|Selection|Random|スポーン地点の抽選|
|Selection|RoundRobin|ID順に並べたスポーン地点を順番に巡回|
|One Shot|ON|一度だけ発動|

地点の選択と、その地点のEnemy Pool抽選は別です。RoundRobinでも、1地点に複数IDのPoolがある場合は敵種類を抽選します。

サンプルの最初のAエリアは、5地点それぞれに1種類だけを設定し、RoundRobin・総数5・Max Alive=5にしてあります。Normal → Ranged → Fast → Tank → Bomberを必ず各1体確認できます。

### 10. Weapon Spawnを置く

1. Emptyを追加し、Object Typeを **Weapon Spawn** にします。
2. IDと位置・回転を設定します。
3. Add EntryからWeapon Poolを入力します。

例：pistol=3、smg=2、rifle=2、pump_shotgun=1。IDは `resources/Data/weapons.json` と照合します。Poolの合計Weightは0より大きくしてください。武器の見た目・取得方式・弾数は既存WeaponSystemが担当します。

### 11. Goalを置く

Cube Emptyを追加し、Object Typeを **Goal** にします。ゴールにしたい場所を箱で囲みます。拡縮がそのままゲームのGoal範囲になります。サイズ0はエラーです。Goalへ入ると既存StageProgressのStageClearになります。

### 12. Validate Levelを押す

パネル下部の **Validate Level** で確認します。エラー時はBlenderのメッセージを確認し、該当Objectを修正してください。

Player Spawn数、Duplicate ID、Group参照、空Pool・全Weight=0、不明ID、体積0、NaN/Infinity、Gameplay Transformのシアーなどを検証します。**Ignore** のObjectはゲーム用の出力・検証対象から除外します。

### 13. Export Levelを押す

Object Modeで **Export Level** を押します。Edit Modeでは実行しません。

出力：

- `resources/levels/stage01/stage01.gltf`：Static Meshの見た目。
- `resources/levels/stage01/stage01.bin`：glTFの頂点など。glTFと一緒に保管してください。
- `resources/levels/stage01/stage01.json`：PlayerSpawn・Collider・Enemy／Weapon Spawn・Trigger・Goal。
- 必要な場合はglTFが参照するテクスチャ。

一時フォルダーに全て出力・検証してから、フォルダーを差し替えます。置換失敗時は元のフォルダーへ戻します。出力先にある.blendなどの無関係なファイルは保持します。OS停止・電源断まで含む完全なファイルシステム取引ではありません。差し替えの瞬間に停止した場合は、隣の `.stage01-backup-*` が旧出力の復旧元になります。

Export後は.blendもCtrl+Sで保存します。**ゲームを終了して起動し直す**と、モデルキャッシュも更新され、変更したStageを読み込みます。起動中のExportによるホットリロードは実装していません。ゲーム内Restartは現在のステージの再開始に使用してください。

## ゲーム側の実装と範囲

GameSceneはStage01のJSONをStageLoader・EnemySpawnSystem・WeaponSystem・StageProgressへ共通で渡します。各設定がすべて読み込めた時点で採用するため、Stageだけ新しくSpawnだけ古い、という部分状態を避けます。正常時には旧巨大Cube床を描画しません。読み込みに失敗した場合はデバッガへ理由を出し、既存fps_spawns.jsonと旧Cube床へFallbackします。

fps_spawns.jsonは互換テスト用に残しています。普段の編集元はstage01.blendです。create_stage01.pyは初回サンプルの再生成用であり、通常のExportにfps_spawns.jsonを使用しません。独自編集後にこの生成スクリプトを実行するとサンプルへ戻るので、通常は使わないでください。

PlayerはXZ移動のCylinderをColliderのローカル軸へ投影してSweepし、壁で停止・Slideします。角では保守的な判定です。敵移動も同じ判定を使い、壁を自動で迂回するNavMeshやPathfindingはありません。壁に阻まれて止まることがあります。

PlayerのHitscanは最寄りStage Colliderまでに制限します。Ranged弾は壁で消え、BomberのBombは曲線を細分化して壁・床へSweepし、接触位置で停止してFuse後に爆発します。高度な物理・バウンドは実装していません。側面に当たったBombもその接触位置で停止します。爆発は従来どおり距離判定で、爆風の遮蔽物判定は追加していません。

Debug ImGuiのSpawn System画面で **Show Stage Colliders** をONにすると、Sceneへ黄色のCollider枠を重ねます。回転を含む8頂点を表示します。

## 座標変換の根拠

BlenderのglTF ExportをY-upで実行すると `(x,y,z) → (x,z,-y)`。既存Model.cppはglTFの頂点とNode TransformのXを反転するため、ゲーム側の点は `(-x,z,-y)` です。アドオンの `ENGINE_BASIS` と `engine_transform()` に変換を集約しています。

Geometryは評価済みWorld Transformをメッシュへ焼き込み、Colliderは `C × BlenderWorld × C⁻¹` と `C × LocalBounds` に分けて出します。C++はScale・XYZ Euler・PositionからWorldを再構築します。Parentを含む回転・非均一Scaleでも、この2経路の結果が一致することを自動テストしています。シアーは曖昧に分解せず拒否します。

参考：[Blender公式glTF Export API](https://docs.blender.org/api/main/bpy.ops.export_scene.html)。

## 自動検証と未確認項目

Debug / Release x64：両方とも警告0・エラー0。C++自動テストは既存7スイート＋StageLoaderTestsの計8スイートが成功しています。

実行用スクリプト：

- `tools/test-stage-loader.ps1`：Stage JSON、Transactional Load、資産、回転Box、表示／Collider座標、壁沿いSlide、Hitscan遮断、弾・爆弾、既存Spawn／Weapon／Goalとの互換性。
- `tools/test-blender-level.ps1`：Blenderをバックグラウンド起動。必要なら `-BlenderPath` で実行ファイルを指定。
- 既存のEnemyParts／EnemySpawn／EnemyTypes／StageProgress／Weapons／FPS／Raycastテストも実行。

Blender 5.0.1でアドオン登録・Export・Role別除外・保存プロパティの復元・不正値拒否・置換失敗時の復旧を確認しました。Python Syntax Checkも実行しています。Blender 4.4実機での確認は未実施です。

ゲーム画面・Blender画面を操作した目視確認は行っていません。以下は手元で確認してください。

- [ ] アドオンを導入し、NパネルのYanEngine Levelが表示される。
- [ ] Object Typeを切り替えると、対応する項目だけ表示される。
- [ ] Enemy／Weapon Poolを追加・削除でき、保存・再読込後も設定が残る。
- [ ] Blenderで変更した床・壁・装飾が、Export後のゲーム再起動で同じ位置に表示される。
- [ ] Collider／Spawn／Trigger／Goal用オブジェクトそのものはゲームに描画されない。
- [ ] Playerが指定の足元位置・向きから開始し、Restartでも戻る。
- [ ] 壁へ正面移動しても抜けず、斜め移動では壁に沿って進める。
- [ ] Collision Noneの装飾は移動・射撃を遮らない。
- [ ] 回転壁の表示とDebug Collider枠が重なる。
- [ ] 壁の向こうの敵に射撃ダメージが届かない。
- [ ] Ranged弾が壁で消え、Bombが壁・床で停止してFuse後に爆発する。
- [ ] 敵が壁を通り抜けない（迂回AIはありません）。
- [ ] TriggerのGroup・Spawn Count・Max Alive・間隔と、敵種類のPoolが反映される。
- [ ] 武器とGoalがBlenderで指定した位置にあり、取得・StageClearが動く。
- [ ] 既存の体格・Marker・被弾色・部位破壊が維持されている。
