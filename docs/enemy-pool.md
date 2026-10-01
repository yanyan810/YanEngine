# Enemy Pool

ステージ開始時にEnemyDefinitionごとの待機個体を生成します。`EnemyPool`が`unique_ptr<Enemy>`を所有し、`GameScene::enemies_`と`BulletManager`はActive個体への非所有ポインターだけを扱います。個体のアドレスはPool拡張後も変わりません。

## 初期化と再利用

- `PrepareForPool`：`Initialize`と`ApplyDefinition`を実行し、Object3d・全部位のVisual・Face Shatter描画リソース・該当するExplosion/Type Markerを事前準備。元のDefinition、Parts/Shared HP、AI、部位Modelを保存します。
- `Acquire`：指定Definitionの空き個体を選び、`ResetForSpawn`でSpawn ID・Trigger・座標を設定します。通常のSpawnで`Initialize`／`ApplyDefinition`は呼びません。
- `ResetForSpawn`：元のHPと設定をコピーし、Local/Shared/LocalAndShared/Invincible、破壊状態、非表示状態、AI/攻撃タイマー、色、爆発、破片、デバッグ個体設定を復元します。既存の描画リソースを再利用します。
- 死亡時：部位用Object3dを削除せず非表示にします。面の爆散演出を維持します。Chunkモードでも元の部位Rendererを保持します。
- `CanReturnToPool`：死亡済み、Face Shard/Detached Partが空、爆発表示終了のすべてを満たした時だけ回収できます。生存個体・演出中の個体の`Release`は拒否します。

通常回収はフレームの戦闘処理後です。Bulletの処理中にはActive配列を変更しません。回収前にStageProgressへ旧Spawn IDの死亡を通知し、再取得には新しいIDを付けます。選択中の敵と最終命中のインデックスも補正します。死亡直後からSpawnSystemの生存数には数えませんが、演出終了まではPoolのActiveに含めます。

## 設定と不足時

`resources/Data/enemy_pool.json`：

```json
{
  "perDefinition": 4,
  "overrides": { "normal": 8 }
}
```

初期設定は各Definition 4体、上書きなしです。数値は0～256の整数。変更は次回のシーン入場時に反映します。ファイルがない場合は4体、不正なファイルではデバッガーへ理由を出して既定値を使用します。Definitionをまたいで個体を転用せず、別のバケットから取得するため部位構成の異なるBlender AssetもSpawn時に組み直しません。EnemyAsset/Model/Chunkの共有は従来どおりです。

空きがない場合はそのDefinitionの個体を1体追加初期化します。この場合だけSpawn時に重い処理が残ります。追加個体は以後も再利用し、Debug出力へDefinition名と累計追加数を記録します。

## Showroomと履歴

F5のShowroomリセットはGPU完了待ちの後、Poolを解放して同じ個体を再取得します。HP/Shared HP、部位、AI、爆散状態を復元し、Pool自体は作り直しません。明示リセットでは演出をその場で打ち切ります。

履歴の復元もDefinition別Poolから取得します。保存したHP、AI、破壊部位、面の破片などを戻し、巻き戻し前後で所有権が混ざらないようにします。Chunk破片の履歴復元は従来どおり描画Object3dを作ります。通常Spawnの追加初期化とは別の処理です。

## 確認

DebugのFPS Controls、およびShowroomの編集画面・試射画面に`Enemy Pool | Active | Inactive | Capacity | Runtime Allocations`を表示します。Runtime Allocationsは事前準備後に追加生成した**Enemy個体の累計**で、一般のCPU割り当てや射撃用リソースの回数ではありません。履歴復元で容量不足になった場合も含みます。F5では累計を消しません。

`Tools/test-enemy-pool.ps1`はDebug実行ファイルの`--enemy-pool-test`を起動し、実際のD3Dリソースでテストします。再取得・同一アドレス・HP全モード・破壊部位・AI・全身爆散・Chunk・全敵タイプ・不足時の拡張・Bullet/Explosion・Trigger/Spawn ID・Showroomリセット・巻き戻しを検証します。Object3dの初期化カウンターを比較し、通常Acquire、死亡後の再取得、F5でRendererを再作成しないことも確認します。

結果は`generated/enemy-pool-tests/result.txt`。テスト用JSONはgenerated配下に作り、ゲームの設定ファイルは変更しません。Debug/Release x64のビルドには`Tools/build.ps1`を使用します。

## 今回の変更ファイル

- `Game/enemy/EnemyPool.h`、`resources/Data/enemy_pool.json`：所有・取得・回収・容量設定・追加生成の計数。
- `Game/enemy/Enemy.h/.cpp`：事前準備、Spawn時の状態復元、部位Renderer保持、回収可能判定。
- `Game/scene/Main/GameScene.h/.cpp`：Active配列、通常Spawn、演出後回収、F5、巻き戻し、統計表示。
- `Game/BulletManager.h/.cpp`：非所有のActive Enemy配列へ対応。
- `Engine/3D/Object3d.h/.cpp`、`Engine/Core/GameApp.cpp`：Debug限定の初期化回数計測とテスト起動。
- `tests/EnemyPoolTests.h`、`Tools/test-enemy-pool.ps1`：実D3Dリソースを使う回帰テスト。
- `tests/DebugToolsTests.cpp`、`tests/EnemyAssetTests.cpp`、`tests/StageLoaderTests.cpp`、`tests/WeaponTests.cpp`：現在のJSON設定に追随する期待値、固定カタログを必要とするFilterテストの分離、非対話のassert出力。
- `CG2_Setup.vcxproj`、`CG2_Setup.vcxproj.filters`：新規ヘッダーの登録。

## 検証結果（2026-10-01）

Debug / Release x64ビルド成功。新規EnemyPoolの実D3Dテスト成功。通常Spawn・再取得・F5のObject3d追加初期化は0回、意図的に容量を超えたケースではRuntime Allocationsが1増加し、追加枠の再利用時は増加しないことを確認しました。

既存の以下17スクリプトも成功しました：raycast、fps、enemy-parts、enemy-spawn、enemy-types、weapons、bullets、stage-loader、stage-progress、debug-tools、weapon-editor、showroom、compound-colliders、enemy-assets、blender-level、blender-showroom、blender-compound（いずれも`Tools/test-*.ps1`）。古い固定設定を前提としていた4テストは上記のように更新して再実行しています。人による見た目の確認やフレーム時間の比較測定は含みません。
