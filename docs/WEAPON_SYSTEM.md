# 武器定義・Pickup・射撃方式・Reload

性能は `resources/Data/weapons.json`、配置と候補は `resources/levels/fps_spawns.json` で設定します。変更はRestart Stageで再読込します。専用の銃モデルは追加せず、同梱Cubeを武器ごとのサイズ・色で表示しています。モデル、定義、Pickup描画リソースはStage初期化時に準備し、Pickup時や射撃時のモデルロードはありません。

初期装備はpistol。武器に2.2以内まで近づき、Eで最寄りの1個を取得します。同距離ならJSONの順で選びます。取得したPickupは消え、旧武器は破棄、新武器は満タンMagazineと定義のReserveになります。装備変更でReload・Burst・Cooldownをリセットします。

| ID | FireMode | Ammo/shot | Pellet | Magazine / Reserve | Reload |
|---|---|---:|---:|---|---|
| pistol | SemiAuto | 1 | 1 | 12 / 60 | Magazine |
| smg | FullAuto | 1 | 1 | 30 / 120 | Magazine |
| rifle | FullAuto | 1 | 1 | 30 / 90 | Magazine |
| shotgun（既存互換） | SemiAuto | 1 | 8 | 8 / 32 | Magazine |
| pump_shotgun | SemiAuto | 1 | 8 | 8 / 40 | PerRound |
| auto_shotgun | FullAuto | 1 | 6 | 20 / 80 | Magazine |
| burst_rifle | Burst（3発） | 1 | 1 | 30 / 90 | Magazine |

武器IDによる射撃・装填の特殊分岐はありません。既存shotgunの挙動は維持し、別定義としてpump_shotgunとauto_shotgunを追加しています。

## 性能フィールド

- `fireMode`: SemiAuto / FullAuto / Burst。省略時SemiAuto。
- `fireInterval`: 通常の発射間隔。Burstでは最終発射後から次Burstまでの待ち時間。DebugのRPMは60/fireInterval（Burst内部のレートではありません）。
- `burstCount`, `burstInterval`: 既定3発 / 0.07秒。クリック1回で開始し、マウスを離しても予約分は発射します。重複予約は不可。残弾不足なら打ち切ります。0秒間隔も使用可能です。
- `ammoPerShot`: 1回の発射で消費する弾薬。既定1。これ未満の残弾では発射できません。
- `pelletCount`: 発射1回で生成するRay数。既定1。Rayの本数で弾薬消費は変わりません。
- `damage`: Rayごとのダメージ。Shotgunの全Pelletが当たれば複数回加算されます。Shot Countは発射回数、Hit Countは命中Ray数です。
- `range`, `magazineSize`, `reserveAmmo`, `maxReserveAmmo`: 武器ごとの射程、容量、取得時Reserve、Reserve上限。
- `hipSpreadDegrees`, `adsSpreadDegrees`: Hip/ADSの拡散半角。既存のADS Blendで補間します。旧 `spreadDegrees` もHip値として読めます。
- `adsFov`, `adsTransitionTime`, `adsSensitivityMultiplier`: 既存のFOV・感度設定を維持。今回ADS補間処理は変更していません（既存のtarget一致時の修正を保持）。

Burstの同一フレーム内の期限を過ぎた弾も漏らさず発射し、実際の最終発射時刻からCooldownを計算します。FullAutoは既存同様フレームごとの入力評価です。ESC・フォーカス喪失・Scene操作再開クリックの抑制時は進行中Burstを取り消し、裏で発射しません。

## Reload

`reloadMode` はMagazine（既定）またはPerRound。

MagazineはRでStartingへ入り、`reloadTime` 後に不足分をReserveから一括移動してNoneへ戻ります。射撃による中断はできません。

PerRoundはRから `reloadStartTime` 待ち、最初の1発を装填します。続けて `reloadPerRoundTime` ごとに1発を装填し、満タンまたはReserve=0でFinishingへ移ります。`reloadEndTime` 後にNoneになります。既定は0.25 / 0.55 / 0.30秒。各時間は0も許可します。

`reloadCanInterrupt=true`（既定）のPerRoundは、ammoPerShot以上の残弾がありCooldownも終了していれば、射撃入力で中断して発射できます。装填済みの弾は残り、未完了の装填は取り消します。空Magazineなら装填前の入力で中断しません。Starting / InsertingRound / Finishingのいずれでも同じ条件です。falseなら終了まで射撃不可です。

Reload残り時間のDebug値は現在のフェーズの残り時間です。HUDはどのフェーズでもRELOADINGを表示します。RによるReload開始は残りBurstをキャンセルします。

## 配置と再抽選

- WeaponSpawn_01: (3,0.5,-2)、Pistol / SMG
- WeaponSpawn_02: (3,0.5,20)、SMG / Rifle / 既存Shotgun / Pump / Burst Rifle
- WeaponSpawn_03: (3,0.5,44)、Rifle / 既存Shotgun / Pump / Auto Shotgun / Burst Rifle

`weaponPool` はID文字列配列または `{ "id": "...", "weight": 1 }` の配列。抽選はStage初期化時だけです。weight=0は抽選対象外、全候補0はエラーです。

`weaponRandom: { "useFixedSeed": true, "seed": 12345 }` で同じ設定・同じ実行環境のRestart配置を再現できます。DebugのUse Fixed Seed / Seedは次のRestartから適用します。候補や順番を変えると同じSeedでも結果は変わります。非固定Seedでも偶然同じ武器が選ばれる場合があります。

StageClear後はPickup・射撃・Reload・Burstを含めWeaponRuntimeの更新自体を停止します。

## JSON検証

ammoPerShot、pelletCount、burstCount、magazineSizeは正整数。Burst間隔と各Reload時間は有限の0以上です。未知のFireMode/ReloadModeや不正値はLoad失敗となり、直前の正常な定義・配置・抽選結果を維持します。旧JSONで新しいフィールドがない場合は既定値を使用します。

## 手元で確認する項目（今回は画面操作未実施）

武器を確実に試すには、ESCでカーソルを解放し、Weapon Systemの **Equip Weapon (Debug)** から選択してください。JSONに定義された全武器を選べます。弾数・Reload・Burst・Cooldownは初期化され、配置済みPickupは消費しません。同じ武器の選び直しでも初期化できます。クリア後は変更できないためRestart Stageを使用してください。Restart時の初期武器はJSONの `initialWeapon` に従います。Pickupも試す場合は配置のweaponPoolを対象ID1つにします。

1. **SemiAuto**：Pistol/PumpでLMB長押ししても1発だけ。クリックを繰り返すとfireInterval以上の間隔で発射。
2. **FullAuto**：SMG/Auto Shotgunで長押し連射。離すと停止。Sceneを操作し直すためのクリックでは発射しない。
3. **Burst Rifle**：1クリックで3発、0.07秒間隔。その後0.45秒待ち。連打してもBurstが重ならない。残弾1～2発ではその分だけ発射して終了。
4. **Pump Reload**：数発撃ってR。0.25秒で最初の1発、以後0.55秒ごとに1発ずつMagazine増加/Reserve減少。満タン後0.30秒でRELOADING消失。
5. **Reload途中射撃**：Pump装填途中にLMBで中断して射撃。空のときは装填前に射撃しない。Magazine方式は中断不可。
6. **AmmoとPellet**：Pumpの1回射撃で弾数は1だけ減る。ammoPerShot=2に変えてRestartすると2減るが、Pellet数は8のまま。
7. **Pickup/Restart**：Eで最寄り武器だけが消え、装備・弾薬が切り替わる。Reload/Burst中の拾い替えで旧状態が残らない。固定Seedで再現し、Restartで取得状態と初期装備がリセットされる。
8. **ADS/部位/クリア**：RMB長押しで視点が揺れずFOV・感度・Spreadが変わる。各武器で部位HPとFace/Chunk破壊が動く。Goal後は弾数・Reload/Burstが進まない。

自動検証は `tools/test-weapons.ps1`。武器関連に加え、Stage/Spawn/Enemy部位・AI・破片/FPS/Raycastの既存テストを実行します。描画・操作感は自動テストの検証範囲に含めません。
