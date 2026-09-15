# 事前生成Boss破片

## Blender

```powershell
blender --background --python-exit-code 1 --python tools/blender/split_boss_parts.py -- --fragments-only
```

追加モードは既存の6部位glTFを読み取り、resources/enemy/boss/fragmentsへ出力します。元bossおよびparts内のglTF/binを再生成・上書きしません。通常の6部位生成モードは維持しています。

破片数を変える場合は `--counts 4 6 4 4 4 4` を追加（Head, Body, LeftArm, RightArm, LeftLeg, RightLeg順）。manifest.jsonをゲーム側が読むのでC++側の数の修正は不要です。各部位1～32個に対応します。

Face中心の遠点選択＋クラスタリングで局所的な不規則な表面片へ分けます。固定seedで再現可能です。切断面の生成・面の細分化・Booleanは行いません。共通原点を維持し、全破片を同じEnemy Transformで描画すると元部位の面を再現します。薄い開いた表面片であり、閉じた体積や切断面はありません。

5.0.1で全26個（4/6/4/4/4/4）を生成・検証済み。全Faceの座標・面の向きを元部位と誤差1e-5以内で照合、重複・欠落なし。空Mesh、Skin/Animationなし、Identity Transformも確認。既存6Assetのハッシュ不変。Bounds・頂点数・Face数はfragments/generation_report.jsonに記録します。4.4互換APIを使用していますが4.4実機は未検証です。

## ゲーム

manifestの各部位リストを読み込み、glTFと依存binが揃った部位だけを初期化時に読み込みます。欠損・JSON読み取り失敗時はその部位だけ従来のDetachedEnemyPartへ戻ります。元BossへのFallbackも維持します。

HPが正から0に減った時だけ一式を生成し、本体描画を止めます。破片は共通のDetachedPartMotionを使用し、部位中心補正、重力、反発、摩擦、5秒寿命を共用します。独立Object3dは濃赤。射撃方向の初速にランダム成分（Fragment Spread）と部位中心から破片中心への成分（Fragment Outward）を加えます。各破片の角速度も別々に抽選します。

全Enemyで合計128個まで（単体部位Fallbackも含む）。追加で上限を超える場合は、全Enemyの中で最も古く生成されたものを先に削除します。Enemy破棄時は管理登録から外します。物理更新は所有Enemyで行うため二重更新しません。

ImGuiのDetached Partで速度、重力、寿命、反発、回転倍率、Spread、Outwardを調整できます。設定は新しく生成する破片に適用します。

## 検証

既存6部位Raycast・HP・物理の自動テスト成功。Debugゲーム起動・初期描画確認済み。全6部位の実操作確認はユーザー回答待ち。

最終ビルド出力：プロジェクトの一階層上のgenerated/verified-fragments-final/{Debug,Release}/CG2_Setup.exe。

最終Debug / Release x64：両方とも警告0・エラー0。再ビルド時のPDB競合にはコンパイラーの/FS指定で対処しました。
