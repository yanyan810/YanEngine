# 面ベースのEnemy Parts

## 移行範囲

通常敵 `normal` の `enemies.json` に `partAsset` を追加し、新方式を使用します。
残りの敵は従来の6部位方式を維持します。`partAsset` を省略すると従来方式です。
武器データ、stage01/stage02、Level Exporter、旧Boss・部位・Fragment資産は変更しません。

## Blenderで設定

1. Blender 4.4以降のPreferences > Add-ons > Install from Diskで
   `Tools/blender/yanengine_enemy_parts.py` をインストールして有効化します。
2. メッシュを選び、3DビューのNパネル > **Enemy Parts** を開きます。
   既存例は `resources/enemy/boss/normal-parts.blend`（単一メッシュ）です。
3. **Add Part**、Part Name、Roleを設定します。名前は重複不可、Roleはルール用です。
4. Edit Modeで面を選び、一覧のPartを選んで **Assign Selected Faces** を押します。
   1面につき1つの整数IDを保持し、再Assignは以前の所属を置換します。
5. **Select Part Faces**で所属面を選択、**Clear Assignment**で選択面の所属を解除します。
   **Remove Part**はそのPartの面を未割り当てにします。他のPartのIDは変更しません。
6. Local / Shared / LocalAndShared / Invincibleを選び、HPやBreakableを設定します。
7. 必要ならHP Groupsの **Add Group** でグループ名とMax HPを追加し、Partから選択します。
8. **Export Enemy Asset**でJSONを出力し、敵定義に次のように参照を設定します。

```json
"partAsset": "resources/enemy/boss/normal.enemy.json"
```

参照・Textureはゲームルート基準です。読み込みは設定ファイルの祖先ディレクトリも検索するため、
ツールやテストから同じ資産を開けます。変更した資産のGPUキャッシュ更新にはゲームの再起動が必要です。
このアドオンはLevel Editorから独立し、メッシュのカスタムプロパティとFACE/INT属性を`.blend`に保存します。

## 出力形式と制約

`version: 1`、`coordinateSystem: "yanengine"`、`texture`、`hpGroups`、`parts`を持ちます。
Partには自由名、Role、nullableな`localHp`、`sharedHpGroup`、`sharedDamageRate`、`breakable`、
`deathOnZero`、元ポリゴンIDの`faces`、UV・法線付きの`triangles`を出力します。
三角形の`face`は元ポリゴンIDです。n-gonを三角形化しても所属を保持します。

- `localHp: null` はLocal HPなしです。SharedもなければInvincibleです。
- `breakable: false` はLocal HPゼロでも描画・当たり判定・Sharedへのダメージ転送を維持します。
- Partの`deathOnZero`はLocal HPゼロで全体死亡。Head/BodyのRoleだけでは死亡を強制しません。
- Groupの`deathOnZero`はそのGroupのHPゼロで全体死亡。いずれかの死亡条件成立でAIが停止します。
- 複数の同Role部位を許可します。RoleはBomberのBody起爆や破片の重さにも使用します。
- 未割り当て面は`__Unassigned`という非破壊・無敵のGeneric部位として残し、弾を遮ります。
- 同じ面を複数Partへ登録したJSON、不明なGroup、重複名、不正数値、空Part等は読み込みを拒否します。
- 空PartはExport前に面をAssignするかRemoveしてください。削除したGroupへの参照はExport時にエラーになります。
- 現時点は静的／レスト姿勢です。Armatureの変形・アニメーションは適用しません。
  その他の有効なModifierはExportを拒否します。必要なModifierは面Assign前に適用してください。
- オブジェクトのワールド変換を焼き込み、Blender `(x,y,z)` → engine `(-x,z,-y)` に変換します。
  ピボットを意図した原点に置いてください。反転Transformの頂点順も処理します。
- Textureは1資産につき1枚です。UVと法線は保持しますが、複数マテリアル／アニメーションは今回の対象外です。

## 通常敵の例

`normal.enemy.json` は旧方式と同じ712三角形を、1メッシュの面属性からExportした資産です。

| Name | Role | Local HP | Breakable | Death on Local Zero |
|---|---|---:|---|---|
| Head | Head | 50 | true | true |
| Body | Body | 100 | true | true |
| LeftArm / RightArm | Arm | 60 | true | false |
| LeftLeg / RightLeg | Leg | 70 | true | false |

Shared Groupは不要です。`hpMultiplier`はLocal HPとShared Groupの最大HPへ1回だけ掛けます。
旧方式の箱判定から、割り当て三角形の実形状判定へ変わるため、面のない隙間は弾が通ります。

## ボスの例

`shared-boss.enemy.json`を敵定義の`partAsset`に指定できます。
BossMain = 5000、全PartはLocalAndShared、Shared Damage Rate = 1、PartのdeathOnZeroはfalseです。
Head/腕/脚は300、Bodyは800かつBreakable=falseです。
HeadとLeftArmへ各50ダメージ → 各Local HPが250、BossMainが4900になります。
腕が壊れてもBossMainが残っていればAIは継続し、BossMainが0になると停止します。

別のArmor Groupを作ってChestArmorから参照したり、Core Groupを死亡条件にすることもできます。
Local+Sharedでは元ダメージを独立に適用するため、Local残量10へ50命中してもSharedへは50×Rateが入ります。
破壊済みPartは以後命中対象外。Breakable=falseのPartはLocalゼロ後もSharedへ転送します。
命中診断のdamageはLocal/Sharedの減少量の大きい方で、二重加算しません。

## 実装・共有範囲

- `EnemyAsset.h`: immutableなCPU資産、読み込み検証、個体状態の生成。
- `EnemyParts.h`: vectorの可変部位状態、Role、部位インデックス、共有HP、実三角形Raycast。
  `EnemyPartType`は旧方式の互換タグとしてのみ残します。
- `EnemyDefinition.h`: 任意の`partAsset`。同じ設定ファイル内の参照はCPU資産を共有。
- `Enemy.cpp/.h`: 起動時GPUモデル／octant ChunkをModelManagerへ登録し、個体間で共有。
  Object3d、HP、AI、Transform、命中Timer、飛散中のFace/Chunk状態は個体ごとです。
- `Bullet.cpp/.h` / `BulletManager.cpp/.h`: 正確な部位インデックスをRaycastからDamageまで受け渡します。
- `GameScene.cpp/.h`: 自由な命中部位名を表示し、巻き戻しにも保存します。
- Face Shatterは割り当て面の三角形を直接使用します。既存の個数上限・寿命・物理を再利用します。
  Chunkは各Partを最大8個の空間グループに分割します。切断面の穴埋めは行いません。
- 新方式も旧方式も個体とデバッグ履歴が独立したHPを持ち、immutableな形状だけを共有します。

EnemyPoolは未実装です。次段階ではInitialize/ApplyDefinitionを資源準備とResetに分け、
Object3d・Face用動的バッファ・爆発表示・マーカーをプールで保持し、復帰時にHP/AI/Timer/破片をリセットします。
GPU形状・部位設定・Chunkは既に個体外で共有しているため、再利用対象は主に個体の描画インスタンスです。

## 検証コマンド

```powershell
./Tools/test-enemy-assets.ps1 # Blender生成 -> C++。BlenderPath引数を指定可能
./Tools/test-enemy-parts.ps1
./Tools/test-enemy-types.ps1
./Tools/test-bullets.ps1
./Tools/test-enemy-spawn.ps1
./Tools/test-showroom.ps1
./Tools/test-debug-tools.ps1
./Tools/build.ps1 -Configuration Debug
./Tools/build.ps1 -Configuration Release
```

通常のテストは生成物を`generated/enemy-asset-tests`へ出力し、制作中の資産を上書きしません。
配布サンプルの意図的な再生成だけはBlenderで
`--background --python Tools/blender/test_enemy_parts.py -- --write-examples`を指定します。

## 今回の検証結果

Blender 4.4.1の実行によるAssign/Select/Clear/Remove、保存・再読込、未割り当て、
四角面の三角形化、反転Transform、9部位の出力とC++への読込を確認しました。
新Asset統合テスト、既存部位・敵タイプ・弾・Spawn・DebugToolsテストは成功しています。
通常敵の712面の全頂点・頂点順を旧資産と比較し、Face/Chunkへの面保持と既存物理を検証しています。
Debug/Release x64ビルドは警告0・エラー0です。実機画面での色・飛散表現の目視確認は未実施です。

既存Showroomテストは、武器登録8件（追加済み`Desart_Eagle`を含む）に対して
Showroomの配置が7件のため、`ShowroomTests.cpp:35`の件数一致で失敗しました。
該当の武器JSON・Showroom JSON・テストは今回変更していません。

## 追加・変更ファイル一覧

| 分類 | ファイル |
|---|---|
| データとHP | `Game/enemy/EnemyAsset.h`（追加）、`EnemyParts.h`、`EnemyDefinition.h`、`EnemyAI.h` |
| 描画・部位破壊 | `Game/enemy/Enemy.h`、`Enemy.cpp` |
| 弾と命中名 | `Game/Bullet.h/.cpp`、`Game/BulletManager.h/.cpp`、`Game/scene/Main/GameScene.h/.cpp` |
| Blender | `Tools/blender/yanengine_enemy_parts.py`、`Tools/blender/test_enemy_parts.py`（追加） |
| 資産 | `resources/Data/enemies.json`、`resources/enemy/boss/normal.enemy.json`、`shared-boss.enemy.json`、`normal-parts.blend`（3サンプル追加） |
| テスト | `tests/EnemyPartsTests.cpp`、`tests/EnemyAssetTests.cpp`（追加）、`Tools/test-enemy-spawn.ps1`、`Tools/test-enemy-assets.ps1`、`Tools/test-blender-enemy-parts.ps1`（2 runner追加） |
| プロジェクト・説明 | `CG2_Setup.vcxproj`、`CG2_Setup.vcxproj.filters`、`README.md`、`docs/enemy-face-parts.md`（追加） |
