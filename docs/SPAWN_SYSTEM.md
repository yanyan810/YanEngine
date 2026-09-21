# 進行式Enemy Spawn

設定は `resources/levels/fps_spawns.json`。編集後はゲームを再起動してください。既存のnlohmann/jsonを利用し、C++の配置変更は不要です。実行時の作業ディレクトリは従来どおりプロジェクト直下です。

`EnemySpawnPoint` はID、ワールド座標、回転のみを持ちます。`EnemySpawnTrigger` がプレイヤーの足元座標とAABBの内外を判定し、IDで参照したPointから生成します。`position` はBox中心、`size` は全幅・全高・全奥行き、`rotation` はラジアンです。AIが追跡を開始すると従来どおりPlayerへ向き直ります。

| Trigger設定 | 意味 |
|---|---|
| spawnPointIds | 使用するPointのID配列（必須・1個以上） |
| spawnCount | 1回の起動で生成する合計数（必須） |
| maxAlive | このTrigger由来の最大生存数（省略時spawnCount） |
| spawnInterval | 生成間隔、秒。0は空き枠分を即時生成 |
| initialDelay | 起動から初回生成までの秒数。省略時0 |
| selection | Random（既定）またはRoundRobin。Sequentialも同義 |
| oneShot | 既定true。falseなら生成完了後に外へ出て再入場すると次の有限回を開始 |

数は1～10000の整数、時間は有限の0以上、Boxサイズは正数で指定します。存在しない参照ID、ID重複、不正な選択方式などはロードエラーになります。部分的な設定は採用せず、Debug出力とFPS Controlsに理由を表示します。

起動したフレームを0秒として計時します。領域から出ても生成は継続します。上限待ちによる時間の蓄積は捨て、空き枠ができると次のUpdateで補充します。生成総数に到達するとCompletedになります（全滅待ちではありません）。HeadまたはBodyの死亡で枠が空き、死体や残存Face/Chunkは生存数に含めません。oneShot=falseでも以前の回の生存個体を上限に含めます。実行中の再入場は予約されません。自動の無限Waveはありません。

Enemyは従来と同じ `Initialize(..., true)` で生成し、既存のAI、射撃判定、6部位HP、Face/Chunk描画のループで処理します。個体IDはEnemy_000から単調増加し、生成元Triggerも保持します。Pointを再使用しても新しいEnemyインスタンスです。

## ゲーム内で確認する手順

1. Debug x64を起動。開始位置は(3,0,-6)、正面は+Z。最初は敵0体です。
2. WでZ=2まで進むとTrigger_A起動。3地点からRandom、0.5秒間隔で最大3体。撃破すると補充し、累計5体で終了します。
3. さらにZ=24まで進むとTrigger_B起動。2秒後に開始し、4地点をRoundRobin、0.5秒間隔で最大4体。撃破・補充を繰り返し累計8体で終了します。AとBの上限は独立です。
4. ESCでマウスを解放し、Spawn Systemの各Triggerを開いてState、Spawned、Alive、Next Spawn、設定・Point IDを確認。水色=未起動、橙=実行中、灰=生成完了、緑Cube=Point。中心からPointへ線を表示します。チェックボックスで非表示にできます。ReleaseにはこのDebug描画・ウィンドウは出ません。
5. FPS ControlsでEnemy_000等を選び、生成元、追跡/攻撃、個別6部位HPとCooldownを確認。腕・脚を撃って別個体に影響しないこと、Head/Body撃破で補充されること、FaceとChunk両モード、破壊済み部位越しの射撃を確認してください。
6. A/BのBoxから出入りしても追加生成されないことを確認。試す場合はJSONのoneShot=falseで再起動し、生成完了後の再入場で新しい有限回が開始されることを確認できます。

完成例の「3地点・8体・最大4体・0.5秒」を試す場合はAのspawnCountを8、maxAliveを4へ変更してください。遅延をなくす場合はinitialDelayを0、同時生成を試す場合はspawnIntervalを0にします。

## 自動検証

`tools/test-enemy-spawn.ps1` は8/4の完了、上限待ちと補充、間隔、初期遅延、RoundRobin循環、Randomの選択範囲、OneShot、再利用時の既存生存数、独立Trigger、不正JSON設定拒否、実際のテスト配置A→Bの5+8体を検証します。`test-enemy-parts.ps1`、`test-raycast.ps1`、`test-fps.ps1` は既存挙動の回帰検証です。描画の見え方・実際の操作感はゲーム内で確認してください。
