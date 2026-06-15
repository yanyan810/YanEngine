# DebugAI Kit

DebugAI Kit は、ゲームと Bot / リプレイ / ログ / AI 操作を接続するための共通デバッグ基盤です。

ゲームごとに違う処理は `IGameDebugAdapter` に閉じ込めます。  
`Engine/DebugAI` 側は、敵の出現方法、プレイヤー攻撃、シーン復元などのゲーム固有処理を直接知らない方針です。

## 基本方針

```txt
ゲーム
↓
IGameDebugAdapter
↓
DebugAIManager
↓
Bot / Replay / OpenAI API
↓
DebugAction
↓
IGameDebugAdapter
↓
ゲーム内処理
```

AIやBotは、マウス座標やキー入力を直接操作しません。  
`AttackWeak`、`Jump`、`Move` のような、ゲーム側が定義した意味のある `DebugAction` を選びます。

## 主要ファイル

| ファイル | 役割 |
|---|---|
| `DebugAI.h` | DebugAI Kit のまとめ include |
| `DebugAIConfig.h` | ログ出力先や異常検出条件の設定 |
| `DebugAIManager.h/.cpp` | Bot、リプレイ、ログ、異常検出の中心 |
| `IGameDebugAdapter.h` | ゲーム固有処理との接続口 |
| `DebugTypes.h` | Action、State、Issue、Entity などの共通型 |
| `DebugJson.h/.cpp` | Action / State の JSON 変換 |
| `RandomDebugBot.h/.cpp` | ランダム操作 Bot |
| `ApiDebugBot.h/.cpp` | 外部APIのJSON返答をActionとして扱うBot |
| `GeminiDebugActionProvider.h/.cpp` | Gemini API 接続 |
| `OpenAIDebugActionProvider.h/.cpp` | OpenAI Responses API 接続 |
| `ImGui/DebugAIImGuiPanel.h/.cpp` | 任意の ImGui 操作パネル |

## 最小組み込み手順

別ゲームに移植する場合は、まず以下を行います。

1. `Engine/DebugAI` をコピーする
2. そのゲーム用の `IGameDebugAdapter` を作る
3. `DebugAIManager` を初期化する
4. Adapter を登録する
5. DebugAI有効中に、ゲーム更新前後で `InjectAction()` / `ProcessAfterUpdate(dt)` を呼ぶ

```cpp
DebugAIConfig config;
config.logDirectory = "generated/debug_ai";
config.playerLogDirectory = "generated/debug_ai/player";
config.aiLogDirectory = "generated/debug_ai/ai";

debugAI.Initialize(config);
debugAI.SetAdapter(&gameDebugAdapter);
```

## ログ出力先

現在のサンプルでは、手動プレイ記録とAI/Bot実行ログを分けています。

```txt
generated/debug_ai/player
  手動プレイで記録したActionログ

generated/debug_ai/ai
  Gemini / OpenAI / BasicCombatBot など、AIやBotが動かしたActionログ

generated/debug_ai
  共通の入口、互換用ログ、リプレイ一覧の検索ルート
```

`F7` の最新リプレイは、まず `player` 側、次に `ai` 側を探します。  
ImGuiのReplay一覧は `generated/debug_ai` 以下を再帰検索するため、両方のログを表示できます。

## Adapter が担当すること

Adapter は、共通の DebugAI データと実際のゲーム処理を変換します。

| メソッド | 内容 |
|---|---|
| `CaptureDebugState()` | 現在のゲーム状態を `DebugGameState` に変換する |
| `ExecuteDebugAction()` | `DebugAction` を実際のゲーム処理に変換して実行する |
| `RestoreDebugState()` | リプレイやSnapshot用にゲーム状態を復元する |
| `SetReplaySpawnOverrides()` | リプレイ時の敵出現情報などを反映する |

## Action の形式

AIやBotは、必ず `DebugGameState::availableActions` に含まれるActionから選びます。

例:

```json
{
  "action": {
    "name": "AttackWeak",
    "targetId": "",
    "intParam": 0,
    "floatParam": 0.0,
    "stringParam": "",
    "holdFrames": 1
  },
  "reason": "敵が近くにいるため弱攻撃を選択"
}
```

## Gemini API 接続

`GeminiDebugActionProvider` は、Gemini API の `generateContent` を使って次のActionを決めます。

現在の実装では、Gemini接続はデフォルトOFFです。  
環境変数を設定した場合だけ `ApiDebugBot` が有効になります。

API接続が無効、APIキー未設定、通信失敗、返答不正の場合は `RandomDebugBot` に戻ります。

PowerShellでゲームを起動する前に、以下を設定します。

```powershell
$env:GEMINI_API_KEY="自分のGemini APIキー"
$env:DEBUGAI_GEMINI_ENABLED="1"
$env:DEBUGAI_GEMINI_MODEL="gemini-3.5-flash"
$env:DEBUGAI_GEMINI_INTERVAL_FRAMES="30"
$env:DEBUGAI_GEMINI_TIMEOUT_MS="8000"
```

目的を変えたい場合は、以下も設定できます。

```powershell
$env:DEBUGAI_GEMINI_GOAL="ステージを探索して、敵を倒し、色々な行動を試す"
```

その後、同じPowerShellからゲームを起動します。

```powershell
.\generated\outputs\Debug\CG2_Setup.exe
```

Visual Studioの出力に以下が出ればGemini Botが有効です。

```txt
[DebugAI] Gemini ApiDebugBot enabled.
```

## OpenAI API 接続

`OpenAIDebugActionProvider` は、OpenAI Responses API を使って次のActionを決めます。

現在の実装では、API接続はデフォルトOFFです。  
環境変数を設定した場合だけ `ApiDebugBot` が有効になります。

API接続が無効、APIキー未設定、通信失敗、返答不正の場合は `RandomDebugBot` に戻ります。

## OpenAI API接続の設定方法

PowerShellでゲームを起動する前に、以下を設定します。

```powershell
$env:OPENAI_API_KEY="自分のOpenAI APIキー"
$env:DEBUGAI_OPENAI_ENABLED="1"
$env:DEBUGAI_OPENAI_MODEL="gpt-5.5"
$env:DEBUGAI_OPENAI_INTERVAL_FRAMES="30"
$env:DEBUGAI_OPENAI_TIMEOUT_MS="8000"
```

目的を変えたい場合は、以下も設定できます。

```powershell
$env:DEBUGAI_OPENAI_GOAL="ステージを探索して、敵を倒し、色々な行動を試す"
```

その後、同じPowerShellからゲームを起動します。

```powershell
.\generated\outputs\Debug\CG2_Setup.exe
```

Visual Studioから起動する場合は、Visual Studioを起動する前のPowerShellで環境変数を設定し、そのPowerShellから `devenv` を起動する必要があります。  
Visual Studioを普通にダブルクリックで起動した場合、PowerShellで設定した環境変数は渡りません。

## 現在使っている環境変数

| 変数名 | 内容 |
|---|---|
| `GEMINI_API_KEY` | Gemini APIキー |
| `DEBUGAI_GEMINI_ENABLED` | `1` / `true` / `on` でGemini接続を有効化 |
| `DEBUGAI_GEMINI_MODEL` | 使用モデル。未指定時は `gemini-3.5-flash` |
| `DEBUGAI_GEMINI_INTERVAL_FRAMES` | Gemini APIに問い合わせる間隔。未指定時は30フレーム |
| `DEBUGAI_GEMINI_TIMEOUT_MS` | Gemini API通信タイムアウト。未指定時は8000ms |
| `DEBUGAI_GEMINI_GOAL` | Geminiに渡すプレイ方針 |
| `OPENAI_API_KEY` | OpenAI APIキー |
| `DEBUGAI_OPENAI_ENABLED` | `1` / `true` / `on` でOpenAI接続を有効化 |
| `DEBUGAI_OPENAI_MODEL` | 使用モデル。未指定時は `gpt-5.5` |
| `DEBUGAI_OPENAI_INTERVAL_FRAMES` | APIに問い合わせる間隔。未指定時は30フレーム |
| `DEBUGAI_OPENAI_TIMEOUT_MS` | API通信タイムアウト。未指定時は8000ms |
| `DEBUGAI_OPENAI_GOAL` | AIに渡すプレイ方針 |

## 動作確認

1. 環境変数を設定する
2. ゲームを起動する
3. GameSceneに入る
4. `F8` でDebugAI Botを有効にする
5. プレイヤーがAIの選んだActionで動けば成功

Visual Studioの出力に以下が出ればOpenAI Botが有効です。

```txt
[DebugAI] Gemini ApiDebugBot enabled.
```

または

```txt
[DebugAI] OpenAI ApiDebugBot enabled.
```

以下が出る場合はOpenAI Botは無効で、RandomBotのままです。

```txt
[DebugAI] OpenAI ApiDebugBot disabled. Using RandomDebugBot.
```

## 注意点

- APIキーはコードに書かない
- APIキーをGitにコミットしない
- 毎フレームAPIを呼ぶと重くなり、料金も増える
- まずは `DEBUGAI_OPENAI_INTERVAL_FRAMES=30` 以上がおすすめ
- API返答が不正でも、登録されていないActionは実行されない
- 最終的には、外部ツールやローカルサーバー経由にすると管理しやすい

## ImGui パネル

ImGuiパネルは任意です。

ImGuiを使わないプロジェクトでは、以下をプロジェクトに追加しなければ大丈夫です。

```txt
Engine/DebugAI/ImGui/DebugAIImGuiPanel.cpp
```

DebugAI本体はImGuiに依存しません。

## 移植時の考え方

別ゲームへ移すとき、共通で持っていくのは以下です。

```txt
Engine/DebugAI/*
```

ゲームごとに作るものは以下のようなファイルです。

```txt
MyGameDebugAdapter.h/.cpp
MyGameDebugProfile.h/.cpp
```

Profile側には、最低限以下を説明できるようにしておくと、他の人が確認しやすくなります。

- 使用可能Action一覧
- Stateの各項目の意味
- Snapshotで復元できる対象
- マップ範囲
- StableKey / ProgressKey の作り方
- 未対応のリプレイ対象
