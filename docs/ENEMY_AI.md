# 平面Enemy AI

EnemyAI.hにIdle / Chase / Attack / Deadを追加しました。PlayerとEnemyの足元TransformのXZ距離を使います。索敵20、移動2.5、攻撃距離1.5。距離が索敵外ならIdle、範囲内ではYawをPlayer方向へ向けて直進し、攻撃距離を越えて進まないよう移動量を制限します。距離0で正規化しません。

BossのglTFは足先が+X方向、インポートでX反転するためエンジン内の正面を-XとしてYawを補正します。Pitch/Rollは0。物理やアニメーション、障害物回避はありません。

Player HPは100、敵の攻撃10、攻撃間隔1秒です。初回は攻撃範囲へ到達した時に攻撃します。範囲出入りでCooldownをリセットしないので連続接触による毎フレーム攻撃にはなりません。長いdtでも時間に応じた攻撃回数を計算します。HPは0下限、HP0でもFPS操作は維持しPlayer Deadを表示します。

HeadまたはBodyがDestroyedならEnemy::IsDead()がtrueになります。死亡条件はEnemyPartsDeadに集約し、IsDeadが参照します。脚・腕破壊ではAIを変えません。Deadでも本体の残部位、Face/Chunkは更新・描画を継続します。

更新順はPlayer移動→Enemy AI・本体/部位Transform更新→射撃/Raycast/破壊→Enemyが生存していればPlayerへ攻撃ダメージ適用。同フレームで致死射撃があればEnemy攻撃を抑止します。Chunkは現部位のTransformから、Faceは現部位のWorld変換から生成します。

初期Player z=-6、Enemy z=18で距離24のIdle開始。4m以上近づくと追跡します。テスト用の床をXZスケール32へ拡大しました。敵は1体のままです。

## Debug

FPS ControlsにEnemy State / Distance、Detection Range / Attack Range / Enemy Move Speed / Attack Damage / Attack Interval、Player HPを表示します。調整操作にはESCでキャプチャを解除してください（AI時間は継続）。タイトルにもPlayer HP / Enemy状態 / Shot・Hit・Last Hit Part・Last Damageを表示するのでReleaseでも確認できます。

## 検証

自動テスト：Idle、追跡、停止距離、正面Yaw、間隔攻撃、30/60/144fpsの一致、距離0、範囲再進入時のCooldown、Head/Body死亡・腕破壊では生存、AI移動回転後の全6部位RaycastとDetached生成位置を確認。既存部位HP・Face Pivot・簡易物理も成功。

Debugの起動と初期Idle・HP100表示を確認。追跡・攻撃・移動中の破壊・Deadの実操作確認はユーザー回答待ち。

実行ファイルはプロジェクトの一階層上のgenerated/verified-ai/{Debug,Release}/CG2_Setup.exe。作業ディレクトリはプロジェクトルートです。PDB対策(/FS、構成別/Fd)は維持しています。

Debug / Release x64：両方とも警告0・エラー0。既存FPS移動テストも成功。
