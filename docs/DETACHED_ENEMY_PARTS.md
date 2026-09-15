# Detached Enemy Parts

6部位のHPが正の値から0へ減った呼び出しでのみ、元のObject3dをDetachedEnemyPartへ移します。HP0への再射撃は実ダメージ0となるため再生成しません。本体側の所有権を空にして二重描画を防ぎ、射撃と同フレームに濃赤で描画します。分割AssetがないFallbackでは分離を行いません。

生成時は元部位の位置・回転・Scaleを保持します。モデルは共通原点のままですが、物理位置を部位Boundsの中心で管理し、毎フレームの描画Translationを補正することで部位中心の回転にします。生成時に頂点は移動しません。分離後はEnemy Transformに追従しません。

初速は正規化した射撃Ray方向×6＋上方向2。全体にHead 1.1、Arm 1.0、Leg .8、Body .5の倍率を適用します。角速度は各軸2～6rad/sのランダムな正負です。重力-9.8、寿命5秒。寿命終了時はvectorから削除します。

最大1/120秒のサブステップで落下と接地を計算します。回転後のモデルBounds下端が高さ0を下回ったら押し戻し、垂直速度へ反発係数.25、水平速度と回転速度へ.7を掛けます。小さい反発速度で停止します。これは地面高さの無限平面との簡易衝突で、床Meshの外周・壁・部位同士は判定しません。

ESCで解放後、FPS Controls > Detached PartからLaunch Power / Upward Power / Gravity / Life Time / Bounce / Angular Velocity Scaleを調整できます。設定は新しく分離する部位へ適用します。

## 検証

- 自動テスト：全6部位の生成Transform連続性、初速倍率、30/144fpsでの自由落下一致、地面下へ落ちないこと、接地停止、寿命終了。
- 既存Raycast・HPテストも成功。
- Debugゲーム起動と初期6部位描画確認済み。ユーザー実操作で吹き飛び・落下・消失を確認済み。全6部位の数値挙動は自動テストで確認。
- 成果物：プロジェクトの一階層上のgenerated/verified-detached/{Debug,Release}/CG2_Setup.exe。

## Blender

スクリプトはbpy.app.versionを参照し、4.4/5.0に存在するMaterial.use_nodesを使用する互換分岐を追加しました。両版で必要なAPIは共通なので別の生成経路は追加していません。4.4互換コード実装済み・実機未検証です。既存の5.0.1生成済み6Assetには変更を加えていません。

Debug / Release x64：ともに警告0・エラー0でビルド成功。
