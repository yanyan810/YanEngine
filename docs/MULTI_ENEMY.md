# 固定5体Enemyと攻撃確認

GameSceneはvector<unique_ptr<Enemy>>で5体を管理します。配置は(3,0,18),(-1,0,10),(7,0,10),(-1,0,2),(7,0,2)。索敵範囲外の先頭個体はIdle、近い個体は追跡するため、異なる状態を確認できます。個別の部位HP・AI・Cooldown・描画・Faceバッファを保持します。Modelリソースを共有しても、材質色や物理状態は共有しません。

射撃は各Enemy::Raycastの最寄り部位を比較し、最も近いEnemyポインター・部位・距離・交点を選択して一体だけに適用します。同距離では配列で先の個体を選びます。Last Hit Enemyは0始まりです。従来同様、Destroyed部位のRaycastは残しています。

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
