# 複数Enemyと攻撃確認

GameSceneはvector<unique_ptr<Enemy>>で個体を管理します。固定5体配置は進行式Spawnへ置き換えました。設定とA→Bの確認手順は[SPAWN_SYSTEM.md](SPAWN_SYSTEM.md)を参照してください。各Enemyは個別の部位HP・AI・Cooldown・描画・Faceバッファを保持します。Modelリソースを共有しても、材質色や物理状態は共有しません。

射撃は各Enemy::Raycastの最寄り部位を比較し、最も近いEnemyポインター・部位・距離・交点を選択して一体だけに適用します。同距離では配列で先の個体を選びます。Last Hit Enemyは0始まりです。Destroyed部位はRaycastから除外します。消失した部位の位置を撃つと後方の未破壊部位へ射撃が通ります。Enemy全体がDeadでも残っている未破壊部位は判定を維持します。

## 分離

生存EnemyのXZ距離が1.1未満なら対称方向へ弱い補正を適用します。完全同位置でも決定的な方向を使って分離します。1組あたり各個体への補正は毎秒.6以下、追跡速度2.5より弱く、位置補正後にAIと描画Transformを更新します。完全なCrowd処理ではなくモデル同士の接触・腕の重なりまでは防ぎません。Dead個体は分離対象外です。

## 攻撃確認

AttackDamage=10、AttackInterval=1秒を維持。各個体のCooldownが独立しています。射撃後に生存している個体の攻撃だけを適用します。複数体が同時攻撃すれば合計ダメージが増えます。

FPS Controlsには総数・Alive・合計Attack Count・Last Enemy Attack HIT（.35秒）・直近の実HP減少を表示します。Selected Enemyの選択先に、個体のState・Distance・全6部位HP・Attack Count・Attack Cooldownを表示します。タイトルのEnemy Stateは選択個体の状態、Enemy Attacksは全個体合計です。

HP0後も攻撃回数は増えますが、実HP減少は0です。ESCで解除してReset Player HP (Debug)を押すと再確認できます。これは検証専用のボタンです。範囲内にいるだけで毎フレームダメージが発生する処理はありません。

## 既存処理

移動・分離後の各個体のTransformからRaycastとFace/Chunkを生成します。Face上限は全Enemy合計256、Chunk/単体部位は全Enemy合計128のままです。シーン終了時はEnemyを解放して共通上限管理から登録解除します。

## 検証

独立した2体の攻撃タイマーと回数、短い間隔の連続攻撃がないこと、手前の個体選択（列挙順を逆にしても一致）、個体間HP・死亡の独立性を自動テストで確認。既存FPS/部位Raycast/HP/Face Pivot/物理/移動後生成のテストも維持。

実機で複数個体の接近・Player HP減少とAttack Count増加を確認。ユーザー実操作で攻撃間隔・個体別ダメージ・他個体の継続動作を確認済み。

成果物：プロジェクトの一階層上のgenerated/verified-multi-enemy/{Debug,Release}/CG2_Setup.exe。

Debug / Release x64：両方とも警告0・エラー0でビルド成功。/FSと構成別PDBの設定は変更していません。

2026-09-21修正：Destroyed部位の不可視HitBoxによる遮蔽を解消。Debug枠も除外。全6部位・移動回転拡縮後の前後Enemyへの貫通と未破壊部位の遮蔽を自動テストで確認。
## 2026-09-21 個体間の色共有修正

Model::BindMaterialForMesh_がObject3dの色をModel共有のperMaterialResourcesへコピーしていました。GPUコマンドは値のコピーではなくCBVアドレスを記録するため、同じModelを後から描いた個体が上書きすると、先に描いた個体もその色になっていました。

Object3dが材質ごとのGPU定数バッファを所有し、個体色×AssetのbaseColorをその領域へ保持する方式へ修正。Modelは指定された個体専用CBVを直接バインドします。通常描画・スキニング描画・テクスチャ上書き描画の経路に適用し、共有Modelの頂点・テクスチャは維持します。DirectXの待機やEnemyのHP判定を変更する回避策は使用していません。

回帰テスト成功。修正版の実行ファイルはgenerated/verified-material-isolation/{Debug,Release}/CG2_Setup.exe（プロジェクトの一階層上）です。

ユーザー実操作で、撃った部位だけ色が変わり他個体への色移りが解消したことを確認済み。Debug / Release x64とも警告0・エラー0。
