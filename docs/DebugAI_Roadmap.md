# DebugAI 実装ロードマップ

## 目的

DebugAI は、ゲームと自動操作 AI / BOT を接続するための共通テスト基盤です。

目的は自動修正ではありません。

目的は以下です。

- 現在のゲーム状態を取得する
- 意味のあるゲーム内 Action を実行する
- 異常な挙動を検出する
- ログ、レポート、リプレイデータを保存する
- あとから記録した手順でバグを再現できるようにする

最終的な目標は、AI や BOT がゲーム内をくまなく探索し、想定外の挙動や不具合を見つけられるようにすることです。

## 現在できていること

実装済みのもの:

- `DebugAIManager`
- `DebugAction`
- `DebugGameState`
- `DebugIssue`
- `DebugLogger`
- `IGameDebugAdapter`
- `RandomDebugBot`
- `DebugAITestScene`
- `GameScene` 用の初期 Adapter
- `generated/debug_ai` へのログ出力
- 基本的な異常検出
- `GameScene` 内で `F8` による DebugAI の ON / OFF
- `TitleScene` から `F9` で `DebugAITestScene` に入る機能

現在の動作:

- ランダム BOT が登録済み Action から行動を選ぶ
- キーボードやマウス座標ではなく、意味のある Action として実行する
- HP が 0 未満、敵数が不正、座標が異常、マップ外、FPS低下、長時間停滞などをログに出せる
- `GameScene` は `Player::QueueDebugCommand` 経由で DebugAI の Action を受け取れる

## 重要な方向性

今後は「リプレイ駆動の自動テスト」に近づけていきます。

理想の流れ:

1. 人間が手動でゲームをプレイする
2. ゲーム側が意味のある Action として行動を記録する
3. Action 履歴をリプレイデータとして保存する
4. 必要に応じてリプレイデータを手直しする
5. リプレイデータを自動テストとして実行する
6. 自動テスト中も Action 履歴を再度記録する
7. バグを検出したら、その直前までの手順と状態を保存する
8. あとからそのリプレイデータを再生して、バグを再現する

重要なのは、バグが起きた座標だけを保存するのではなく、そこに至るまでの手順を保存することです。

ゲームのバグは、座標だけでは再現できないことがあります。

例えば以下のようなものが再現条件になることがあります。

- その前にどの敵と戦ったか
- どのイベントを通ったか
- どのアイテムを使ったか
- どの順番で移動したか
- どのシーン遷移を通ったか
- どのタイミングで攻撃やジャンプをしたか

そのため、DebugAI では Action の履歴を保存する仕組みが重要になります。

## 目標とする構成

```text
GameScene / 各ゲーム固有の処理
    |
    v
IGameDebugAdapter
    |
    +--> CaptureDebugState()
    +--> ExecuteDebugAction()
    |
    v
DebugAIManager
    |
    +--> BOT / リプレイ / AI の行動決定
    +--> 異常検出
    +--> Action 履歴
    +--> ログ出力
    |
    v
ログ / レポート / リプレイデータ
```

ゲーム固有の処理は、各 Adapter 側に閉じ込めます。

DebugAI 共通部分は、ボスフェーズ、カード効果、アイテム ID、マップオブジェクト名などのゲーム固有情報を直接知らないようにします。

ゲーム固有情報は `DebugGameState` と `DebugAction` を通して渡します。

## 今後必要になりそうなもの

### 1. DebugReplayRecorder

実行された Action と重要な状態を記録する仕組みです。

役割:

- 実行された `DebugAction` を記録する
- フレーム番号を記録する
- シーン名を記録する
- Action 実行前後の状態を必要に応じて保存する
- 直近の履歴をリングバッファとして保持する
- 手動でリプレイデータを保存できるようにする
- 異常検出時に直近のリプレイデータを自動保存する

出力例:

```jsonl
{ "frame": 120, "scene": "Game", "action": "MoveRight" }
{ "frame": 132, "scene": "Game", "action": "Jump" }
{ "frame": 180, "scene": "Game", "action": "AttackWeak" }
```

### 2. DebugReplayPlayer

保存したリプレイデータを読み込み、同じ Action を再実行する仕組みです。

役割:

- リプレイファイルを読み込む
- 記録されたフレーム、または時間に合わせて Action を実行する
- シーンが一致しない場合に停止できるようにする
- 多少ズレても続行するモードを用意する
- リプレイ成功 / 失敗をレポートに残す

想定する再生モード:

- フレーム基準の厳密な再生
- 時間基準の再生
- 1 Action ずつ進めるデバッグ再生
- フレームずれを許容する再生

### 3. 手動プレイの Action 記録

人間が普通にプレイした操作も、意味のある Action として記録したいです。

あまり良くない記録:

```text
KeyDown(DIK_A)
KeyDown(DIK_SPACE)
MouseClick(340, 220)
```

望ましい記録:

```text
MoveLeft
Jump
TalkTo(NPC_001)
UseItem(Potion)
AttackWeak
```

キー入力やマウス座標ではなく、ゲーム内の意味で記録する方が、あとから読みやすく、編集しやすく、UI変更にも強くなります。

### 4. 初期状態スナップショット

リプレイを安定して再現するには、開始状態も保存する必要があります。

最低限保存したい候補:

- 開始シーン名
- プレイヤー座標
- プレイヤー HP
- 敵リスト
- ボス HP
- 現在のフェーズ
- 乱数 seed
- 重要なイベントフラグ
- 難易度
- 所持アイテムの概要

出力例:

```json
{
  "scene": "Game",
  "player": {
    "hp": 100,
    "position": { "x": -12.0, "y": 0.0, "z": 5.0 }
  },
  "phase": "Battle",
  "rngSeed": 12345
}
```

### 5. 乱数 seed の管理

同じ Action を再生しても、敵の行動やドロップ、スポーンが毎回違うと再現性が落ちます。

必要になりそうなもの:

- 自動テスト中は seed を固定する
- リプレイデータに seed を保存する
- レポートに seed を表示する
- 探索用にはランダム seed モードも用意する

### 6. CoverageDebugBot

ランダム操作は最初の確認には便利ですが、ゲーム内をくまなく探索するには不足します。

CoverageDebugBot は、未探索エリアを優先して移動する BOT です。

やりたいこと:

- マップをグリッドに分ける
- 訪問済みセルを記録する
- 未訪問セルへ向かう
- 詰まったらジャンプ、ガード、攻撃、逆方向移動を試す
- 近くに調べられるものがあれば触る
- 敵が進行を邪魔していれば攻撃する
- 探索結果をカバレッジレポートとして保存する

追加したい状態:

- `visitedCells`
- `currentAreaId`
- `nearbyEnemies`
- `nearbyInteractables`
- `isGrounded`
- `isStuck`
- `canMove`
- `canAttack`
- `canInteract`

### 7. チェックポイントと成功ログ

自動テストでは、失敗ログだけではなく、成功した情報も必要です。

例:

- Battle フェーズに入った
- ボスが出現した
- ボス HP を 50% まで削った
- プレイヤーが特定エリアに到達した
- 特定 NPC に話しかけた
- GameClear に到達した

出力例:

```jsonl
{ "frame": 230, "checkpoint": "BattleStarted" }
{ "frame": 1800, "checkpoint": "BossHalfHp" }
{ "frame": 4200, "checkpoint": "GameClearReached" }
```

### 8. Issue Context Package

異常を検出したときに、調査に必要な情報をまとめて保存する仕組みです。

保存したいファイル:

- `issue_report.txt`
- `issue_context.json`
- `issue_replay.jsonl`
- `issue_frames.jsonl`
- スクリーンショット
- パフォーマンス情報

保存したい内容:

- 異常内容
- 発生フレーム
- シーン名
- 直前の Action
- 直近の Action 履歴
- 直近の GameState 履歴
- プレイヤー座標
- 敵の状態
- 乱数 seed
- 使用中のリプレイファイル

## 実装フェーズ案

### Phase 1: 現在の DebugAI を安定させる

目的:

現在のランダム DebugAI を `GameScene` 内で安全に動かせるようにする。

作業:

- `F8` で ON / OFF できる状態を維持する
- ImGui でログ保存先を確認できるようにする
- 同じ異常ログが大量に出すぎないようにする
- `GameScene` の Action 実行が自然に動くか確認する
- 現在のログに Action 履歴を少し追加する
- Scene 終了時に DebugAI が確実に無効になるようにする

完了条件:

- `GameScene` で DebugAI を数分動かせる
- ログが読める
- DebugAI OFF のとき通常プレイに影響しない

### Phase 2: Replay Recorder

目的:

テスト中に実行された Action の手順を保存できるようにする。

作業:

- `DebugReplayRecorder` を追加する
- BOT が選んだ Action を記録する
- リプレイ実行中の Action も記録する
- 直近 Action のリングバッファを持つ
- 異常検出時に直近リプレイを保存する
- レポートにリプレイファイルのパスを追加する

完了条件:

- 異常レポートにリプレイファイルが含まれる
- リプレイファイルに異常発生までの Action 手順が保存される

### Phase 3: Replay Player

目的:

保存したリプレイファイルを再生できるようにする。

作業:

- `DebugReplayPlayer` を追加する
- `jsonl` を読み込めるようにする
- フレームに合わせて Action を実行する
- ImGui から再生開始 / 停止 / 読み込みをできるようにする
- 厳密再生とゆるい再生の両方を用意する

完了条件:

- 保存したリプレイで、プレイヤーがだいたい同じ手順で動く
- 異常再現を手動操作なしで試せる

### Phase 4: 手動プレイ記録

目的:

人間のプレイをリプレイデータとして保存できるようにする。

作業:

- プレイヤー入力を `DebugAction` に変換する
- 手動プレイ中の Action を記録する
- シーン遷移を記録する
- 重要なインタラクトを記録する
- 記録開始 / 停止の操作を追加する

完了条件:

- 人間が一度プレイしたルートを保存できる
- 保存したルートを自動再生できる

### Phase 5: Coverage Bot

目的:

ランダム操作ではなく、探索目的の BOT にする。

作業:

- マップのグリッドカバレッジを追加する
- 訪問済みセルを記録する
- 移動目標を選ぶ
- 詰まり検出を追加する
- 簡単な敵対応を追加する
- カバレッジレポートを保存する

完了条件:

- ランダム操作より広い範囲を探索できる
- 訪問済み / 未訪問エリアを確認できる

### Phase 6: テスト実行とレポート

目的:

自動テストを開発のイテレーションに使える形にする。

作業:

- テストシナリオファイルを定義する
- BOT またはリプレイを指定時間実行する
- 成功 / 失敗のサマリーを保存する
- チェックポイントを保存する
- パフォーマンス情報を保存する
- Issue Context Package を保存する

完了条件:

- テスト実行結果を人間が読める
- 成功したテストと失敗したテストの両方を確認できる

## ファイル形式案

### リプレイのメタ情報

```json
{
  "version": 1,
  "gameBuild": "Debug",
  "createdAt": "2026-06-08T11:00:00+09:00",
  "startScene": "Game",
  "rngSeed": 12345,
  "mode": "ManualRecord"
}
```

### リプレイ Action

```json
{
  "frame": 120,
  "time": 2.0,
  "scene": "Game",
  "action": {
    "name": "AttackWeak",
    "targetId": "",
    "intParam": 0,
    "floatParam": 0.0
  }
}
```

### 異常発生時の情報

```json
{
  "message": "Enemy HP became negative.",
  "frame": 121,
  "scene": "Game",
  "lastAction": "AttackWeak",
  "replayFile": "issue_replay_0001.jsonl",
  "rngSeed": 12345
}
```

## 未決定事項

- リプレイはフレーム基準にするか、時間基準にするか
- シーン遷移をどう記録するか
- バトル前の動画スキップを Action として扱うか
- 再現に必要なゲーム固有フラグは何か
- 敵やボスの状態をどこまで復元するか
- テストシナリオファイルをどこに置くか
- 生成されたリプレイファイルを git 管理外にするか
- DebugAI を Debug ビルド限定にするか
- ImGui 上でリプレイ編集をできるようにするか

## 直近のおすすめ実装

次に実装するなら、以下の順番が良さそうです。

1. `DebugReplayRecorder`
2. 直近 Action のリングバッファ
3. 異常検出時に直近リプレイを保存
4. `debug_ai_report.txt` にリプレイファイルのパスを追加

これにより、現在のランダム BOT や今後の Coverage Bot に「再現可能な手順」が残せるようになります。

その後に `DebugReplayPlayer` を実装します。

