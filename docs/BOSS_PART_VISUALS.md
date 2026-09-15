> 分割Asset生成・ゲーム有効化まで完了しました。最新の生成手順と検証は [BOSS_SPLIT_EXPORT.md](BOSS_SPLIT_EXPORT.md) を参照してください。以下は受け入れコード導入時点の記録です。

# Boss Asset調査と分割モデル受け入れ

## 現在のAsset

対象：resources/enemy/boss/boss.gltf（元ファイルは変更なし）。

- glTF Mesh：1（Cube）
- Primitive（SubMesh相当）：2。片方がmaterial 0、もう片方はmaterial指定なし（glTF既定材質）。
- 定義Material：1
- Node：23
- Skin：1、Joint：21（ボーンからボーン.020まで）
- Animation：13（Enemyでは停止）
- 両PrimitiveにJOINTS_0 / WEIGHTS_0属性があり、Skinning対応。

ルートのアーマチュアNode（22）の下に、描画Node Cube（21、mesh 0 / skin 0）と骨階層のルート（20）があります。残り21NodeがJoint階層です。頭・胴体・四肢ごとの描画Nodeはありません。Primitiveは2群であり6部位には分かれていません。骨の存在は独立した描画範囲を意味しません。現在のAssetでは6部位独立描画はできず、C++で頂点を分割・切断する処理は追加していません。

以前の「頭と胴体・四肢のメッシュ」という説明は、厳密には1 glTF Mesh内の2 Primitiveです。

## 受け入れコード

EnemyPartVisualが部位種別・専用Object3d・表示フラグを持ちます。Enemy内部で6部位のHPデータと対応させます。共通Model・Shader・読み込み仕様は変更していません。

Enemy::Initializeの第4引数useSplitAssets（既定false）をtrueにして使います。GameSceneの呼び出しは既定のままなので現在のBoss表示は変わりません。次の6ファイルを全て配置した場合だけ分割表示へ切り替えます。欠損時は元BossへフォールバックしDebug出力へ記録します。

- resources/enemy/boss/parts/boss_head.gltf
- resources/enemy/boss/parts/boss_body.gltf
- resources/enemy/boss/parts/boss_left_arm.gltf
- resources/enemy/boss/parts/boss_right_arm.gltf
- resources/enemy/boss/parts/boss_left_leg.gltf
- resources/enemy/boss/parts/boss_right_leg.gltf

ファイルの存在チェックは破損Assetや依存bin/textureの検証ではありません。インポート可能なAssetを用意してください。

全Object3dへ同一のEnemy Transformを適用します。独立したインスタンス材質でNormal白／Light薄赤／Heavy赤／Critical濃赤を設定し、DestroyedはそのObject3dだけDrawを省きます。SetPartVisibleまたは分割モード時のImGuiチェックボックスで部位ごとに表示切替できます。HP・Colliderは表示切替で変化しません。元Bossモードでは部分非表示・部分着色は行いません。

## Blender等での再Export条件

元Assetをコピーして作業し、元のboss.gltfは保持してください。静止バインドポーズを6オブジェクトへ分割し、各部位を個別ファイルとしてExportします。左右は敵自身の左右です。

6部位は元Bossと同じ座標系・単位・共通原点を維持し、部位ごとに中央へ移動しないでください。頂点に共通座標で位置を焼き込み、描画NodeとRootのTransformをIdentityにした静的MeshとしてExportしてください。今回の受け入れは骨アニメーションや別々の部位ピボットを同期しません。白い材質／テクスチャを用意すると状態色を確認しやすくなります。

既存Raycastは元Boss Boundsから作る簡易6分割のままです。分割モデルも同じ原点・形状なら追従しますが、造形や座標を変える場合は判定範囲も再調整が必要です。

## 検証範囲

分割Asset自体は未作成のため、分割モデルの描画・個別色・非表示の実画面確認は未実施です。元Bossのまま動作する既定経路を維持しています。今回のビルド出力は一階層上のgenerated/verified-part-visuals/{Debug,Release}です。

Debug x64 / Release x64とも警告0・エラー0でビルド成功。既存6部位Raycast・独立HPの自動テストも成功しています。
