# 敵タイプ拡張

共通EnemyにEnemyDefinitionを適用し、Normal / Fast / Tankは既存近接AI、Ranged / Bomberは距離維持AIを使用します。敵IDによる行動分岐はありません。部位破壊・ダメージ状態色・HeadまたはBody破壊による死亡を維持しています。本番UIは変更していません。

## 調整

- `resources/Data/enemies.json`：5種類の性能。hpMultiplierは全6部位のhp/maxHpに適用します。
- `resources/levels/fps_spawns.json`：各SpawnPointのenemyPool。初期重みはNormal 5 / Fast 2 / Ranged 2 / Tank 1 / Bomber 1です。pool省略時はnormal。明示された空poolや全weight=0は読み込みエラーです。
- 任意のルート設定 `"enemyRandom": { "useFixedSeed": true, "seed": 12345 }` で敵スポーン乱数を固定できます。同一の更新・スポーン順序で再現します。武器の乱数とは独立しています。
- 定義とスポーンのロードはそれぞれ検証完了後に置き換えます。失敗時は直前の有効状態を維持します。シーン初期化時に敵定義が不正なら敵スポーンを開始せず、デバッガへエラーを出力します。
- preferredRangeは保持し、現在の移動制御はminRange/maxRangeで行います。
- 通常弾は水色、爆弾は大きめのオレンジ色Cubeです。通常弾は発射時の方向へ直進し、移動区間とプレイヤーの簡易球で衝突判定します。
- 爆弾は発射時のプレイヤーXZ位置へ着地する初速を設定し、重力で落下します。地面Y=0で停止後、fuseTimeを消費して範囲ダメージを1回与え、消えます。直撃ダメージはありません。爆発パーティクル・SEは追加していません。
- 弾の寿命は発射時から数えます。爆弾も寿命到達時には削除されるため、調整時は飛行時間＋fuseTimeより長いprojectileLifetimeを設定してください。
- 発射元が死亡しても弾は残り、StageClearとシーン終了時には弾・描画オブジェクトを全消去します。

## 検証結果

Debug / Release x64ビルド：両方とも警告0・エラー0。

以下の自動テストが成功しています。ゲーム画面の操作による確認は実施していません。

- tools/test-enemy-types.ps1：5タイプの読み込み、不正値・重複・不明ID/type拒否とトランザクション、全部位HP倍率・死亡、距離制御・射撃間隔、弾の移動・寿命・連続衝突・一度だけのダメージ、爆弾の重力・着地・Fuse・範囲内外ダメージ、固定Seed・重み付き抽選・旧形式fallback、弾のクリア。
- tools/test-enemy-parts.ps1：既存の部位破壊・近接AI・破片・複数敵。
- tools/test-enemy-spawn.ps1：既存スポーン。
- tools/test-stage-progress.ps1：既存ステージ進行。
- tools/test-weapons.ps1：既存武器。
- tools/test-fps.ps1：既存移動。
- tools/test-raycast.ps1：既存射線。

## 手元確認チェックリスト

抽選なので1回のプレイで全種類が出る保証はありません。特定タイプを確実に確認するには、対象SpawnPointのenemyPoolをそのID・weight=1の1件に一時変更してください。Debugの敵一覧で実際のタイプを確認できます。

- [ ] Normalがプレイヤーに接近して近接攻撃する。
- [ ] FastがNormalより速く、HPが低い。
- [ ] TankがNormalより硬く、移動が遅く、近接攻撃が強い。
- [ ] Rangedが一定距離を取り、水色の見える弾を撃つ。
- [ ] Rangedへ近づくと後退する。
- [ ] Ranged Projectileに当たるとPlayer HPが減り、弾が消える。
- [ ] Bomberのオレンジ色のBombが山なりに飛ぶ。
- [ ] Bombが地面で停止する。
- [ ] 着地後のFuseが経過すると爆発し、Bombが消える。
- [ ] 爆発範囲内ならPlayer HPが減り、範囲外なら減らない。
- [ ] enemyPoolから複数種類がSpawnする。
- [ ] Head / Body破壊で各タイプとも死亡する。
- [ ] StageClear後にProjectileが残らない。
