# Kawaii Physics

![UE Version](https://img.shields.io/badge/Unreal%20Engine-5.3--5.8-0e1128?logo=unrealengine&logoColor=white) [![Docs](https://img.shields.io/badge/Docs-Portal-blue)](https://pafuhana1213.github.io/KawaiiPhysics-Portal/docs) [![GitHub release (latest by tag)](https://img.shields.io/github/v/release/pafuhana1213/KawaiiPhysics)](https://github.com/pafuhana1213/KawaiiPhysics/releases) [![Downloads](https://img.shields.io/github/downloads/pafuhana1213/KawaiiPhysics/total)](https://github.com/pafuhana1213/KawaiiPhysics/releases) [![Discussions](https://img.shields.io/github/discussions/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/discussions) [![GitHub contributors](https://img.shields.io/github/contributors/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/graphs/contributors) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) [![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/pafuhana1213/KawaiiPhysics) [![X (formerly Twitter) Follow](https://img.shields.io/twitter/follow/pafuhana1213?style=social)](https://twitter.com/pafuhana1213)

[English README](README.md) | [简体中文 README](README_zh-Hans.md)

* * *

Kawaii Physicsは、Unreal Engine向けのシンプルな疑似物理プラグインです。  
髪、スカート、胸などの揺れものを「かんたんに」「かわいく」揺らすことができます。

[![Demo Video](https://github.com/user-attachments/assets/0bc33f5c-c7db-49b3-8e98-75dc062a4e2a)](https://youtu.be/0f-l-SP07Mo)

[デモ動画 (Demo Video)](https://youtu.be/0f-l-SP07Mo)

目次 (Contents)

-   [ギャラリー](#%E3%82%AE%E3%83%A3%E3%83%A9%E3%83%AA%E3%83%BC)
-   [特徴](#%E7%89%B9%E5%BE%B4)
-   [導入方法](#%E5%B0%8E%E5%85%A5%E6%96%B9%E6%B3%95)
-   [ドキュメント＆コミュニティ](#%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88%E3%82%B3%E3%83%9F%E3%83%A5%E3%83%8B%E3%83%86%E3%82%A3)
-   [解説記事・動画](#%E8%A7%A3%E8%AA%AC%E8%A8%98%E4%BA%8B%E5%8B%95%E7%94%BB)
-   [サンプル](#%E3%82%B5%E3%83%B3%E3%83%97%E3%83%AB)
-   [採用実績](#%E6%8E%A1%E7%94%A8%E5%AE%9F%E7%B8%BE)
-   [関連ツール](#%E9%96%A2%E9%80%A3%E3%83%84%E3%83%BC%E3%83%AB)
-   [ライセンス](#%E3%83%A9%E3%82%A4%E3%82%BB%E3%83%B3%E3%82%B9)
-   [作者](#%E4%BD%9C%E8%80%85)
-   [更新履歴](#%E6%9B%B4%E6%96%B0%E5%B1%A5%E6%AD%B4)

## ギャラリー

![compare](https://github.com/user-attachments/assets/fda69859-b60f-4fde-a683-62da3e2839e4)

*キャラクター：[Grayちゃん](http://rarihoma.xvs.jp/products/graychan)*

![dance5](https://github.com/user-attachments/assets/28d72d0c-4423-41c7-bc52-c5c7c3886e02)

*キャラクター：[オリジナル3Dモデル『ルゼブル』-Lzebul-](https://booth.pm/ja/items/4887691) / モーション：[ミライ小町](https://www.miraikomachi.com/download/)*

![danceKano](https://github.com/user-attachments/assets/63faed3c-8aaa-4d4d-ae33-e98f9c8c15fd)

*キャラクター：[TA式 鷺宮カノ](https://uzurig.com/ja/uzurig2-rigging-plugin-for-maya-jp/) / モーション：[しかのこのこのここしたんたん](https://booth.pm/ja/items/5975857) / セットアップ：[株式会社TA様](https://xta.co.jp/)*

## 特徴

<table><tbody><tr><td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg?raw=true" width="320"></td><td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics4.gif?raw=true" width="320"></td></tr></tbody></table>

-   アニメーションやキャラクターの動きに合わせて、揺れものを物理制御します
-   AnimGraphに`KawaiiPhysics`ノードをひとつ追加するだけで使えます
-   球・カプセル・平面のコリジョンに対応。ビューポート上で直感的に調整できます
-   ビューポートでのコリジョンの色・不透明度・境界線は、Editor Preferences でコリジョンの種類ごとに変更できます
-   骨の長さを保つので、計算が破綻してもボーンが伸び縮みしません
-   PhysXに頼らないシンプルな計算なので、動作が軽いのが特長です
-   風や重力に加えて、演出目的の外力も加えられます
-   `DataAsset`や`PhysicsAsset`にパラメータを保存して使い回せます
-   曲がる角度は円錐で制限できるほか、親ボーンのローカル軸ごとに制限してヒンジや片側だけの曲げも作れます（Per Axis）
-   ボーン間の距離制約で、スカートの裾が足を貫通するのを抑えられます（BoneConstraint)
-   アニメ制御下のボーン（足など）の動きを揺れものに同期させ、追従や貫通対策ができます（SyncBone）
-   Blueprint・C++から、実行中にパラメータや外力を動的に制御できます
-   その他の最新機能は [更新履歴（KawaiiPhysics Portal）](https://pafuhana1213.github.io/KawaiiPhysics-Portal/docs/changelog) を参照

### 参考資料

物理挙動を実装するにあたって、以下の資料を参考にしました。

-   [次期アイドルマスター グラフィクス＆アニメーション プログラミング プレビュー](https://cedil.cesa.or.jp/cedil_sessions/view/416)

## 導入方法

### 1\. ダウンロード

C++ビルド環境がない方は、以下のリンクからプラグインをダウンロードしてください。

-   **[GitHub Releases](https://github.com/pafuhana1213/KawaiiPhysics/releases/)**
-   **[FAB](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)** (内容はGitHub版と同じです）
-   **[Booth](https://pafuhana1213.booth.pm/items/5943534)** (ストアをフォローすると更新通知が届きます)

### 2\. 対応バージョン

-   **UE 5.3 ~ 5.8**
-   UE4.27: [v1.11.1](https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20230830-v1.11.1)

### 3\. インストール

1.  プロジェクトのルートに `Plugins` フォルダを作成します。
2.  ダウンロードした `KawaiiPhysics` フォルダを `Plugins` フォルダにコピーします。

### 4\. 使い方

-   Animation BlueprintのAnimGraphで `KawaiiPhysics` ノードを追加して利用します。
-   詳細なパラメータ設定については、[ドキュメント](https://pafuhana1213.github.io/KawaiiPhysics-Portal/docs/parameters/physics)を参照してください。

> **Note:** Blueprintのみのプロジェクトでパッケージ化する際にエラーが出る場合があります。その際は、C++プロジェクトに変換するか、[こちらの記事](https://papersloth.hatenablog.com/entry/2024/02/14/201629)を参考にしてください。

## ドキュメント＆コミュニティ

-   **[Portal](https://pafuhana1213.github.io/KawaiiPhysics-Portal/docs)**: 公式ドキュメント（最新）
-   **[Wiki](https://github.com/pafuhana1213/KawaiiPhysics/wiki)**: 旧ドキュメント（最新情報はポータルへ移動しました）
-   **[DeepWiki](https://deepwiki.com/pafuhana1213/KawaiiPhysics)**: AIによる自動生成ドキュメント
-   **[Discussions](https://github.com/pafuhana1213/KawaiiPhysics/discussions)**: 質問、要望、雑談などはこちらへ
-   **[Issues](https://github.com/pafuhana1213/KawaiiPhysics/issues)**: 不具合報告

## 解説記事・動画

### 公式

-   [揺れ骨用自作AnimNode「Kawaii Physics」の内部実装解説的なもの その1](http://pafuhana1213.hatenablog.com/entry/2019/07/26/171046)
-   [#ControlRig を使ったスカートの突き抜け対策と#KawaiiPhysics を併用してみるテスト](https://twitter.com/pafuhana1213/status/1300454762542817280)

### 非公式 (ありがとうございます！)

-   [揺れモノ超ド素人がKawaiiPhysicsを使って、感覚を頼りに女性キャラクターモデルのKawaiiを増す方法まとめ](https://qiita.com/YuukiOgino/items/7f3198a90dab43019f26)
-   [揺れものとコントロールリグ UEを使用したバーチャルライブでの実例](https://www.docswell.com/s/indieusgames/K4Q2XJ-2024-06-14-175045)
-   [【UE】揺れ物設定（Kawaii Physics）](https://techblog.sumelagi.co.jp/unrealengine/147/)
-   [【UE4】Kawaii Physicsチュートリアル#1](https://www.youtube.com/watch?v=hlgXuVML_is)
-   [How To Setup Kawaii Physics in Unreal Engine 5](https://dev.epicgames.com/community/learning/tutorials/d1Z9/unreal-engine-how-to-setup-kawaii-physics-in-unreal)
-   [５分でわかるKawaii Physicsの使い方！【UE5】【tutorial】](https://www.youtube.com/watch?v=TliP9vSxm4c)
-   [The Hidden Physics “Engine” Behind Unreal's Most Stylish Games ...](https://www.youtube.com/watch?v=9ThmoMHnHhw)

## サンプル

![sample2](https://github.com/user-attachments/assets/0d866ad2-f803-400b-bd23-2d46ab17b8ae)

プロジェクト内にサンプルレベルとキャラクターが含まれています。

-   **エンジンバージョン**: UE 5.8
-   **機能別サンプル**: `Content/KawaiiPhysicsSample/Examples/` にテーマごとのレベルがあります。各展示は 1 つの機能だけを専用の AnimBlueprint で紹介し、段階的に学べるようにしています。`Content/KawaiiPhysicsSample/L_KPS_AllExamples` ではテーマごとのレベルをすべて並べて読み込み、全機能を 1 か所で見て回れます。PIE 中は Z / X で同じテーマ内の前 / 次の展示へ、PageUp / PageDown で前 / 次のテーマへジャンプできます。H で、いま見ている展示の説明ページを [KawaiiPhysics Portal](https://pafuhana1213.github.io/KawaiiPhysics-Portal/docs) で開きます。B では、その展示で使っている AnimBlueprint をエディタで開きます。エディタでは看板アクタの Details にある **Open Docs** / **Open AnimBP** ボタンからも開けます。看板の説明文は現在の言語に合わせて切り替わります（日本語のときは日本語、それ以外は英語）。
    -   `01_BoneChain`（RootBone / DummyBoneLength / ExcludeBones / AdditionalRootBones / 複数ノードとタグ）
    -   `02_PhysicsSettings`（Damping / Stiffness / WorldDamping / LimitAngle / カーブ / テレポートしきい値 / ウォームアップ / SkelCompMoveScale / Per Axis LimitAngle / TimeScale）
    -   `03_Collision`（球 / カプセル / テーパードカプセル / 箱 / 平面 / LimitsDataAsset / PhysicsAsset / ワールドコリジョン）
    -   `04_Forces`（重力 / SimpleExternalForce / 外力プリセット / AnimNotify / 外力の座標系 / ボーン長による外力の倍率 / 従来方式の重力）
    -   `05_Advanced`（ボーン拘束 / ボーン細分化 / SyncBone / プリセット / 共有コリジョン）
    -   `06_RuntimeControl`（Blueprint からの Alpha・設定倍率・突風 / 設定倍率の AnimNotifyState / Sequencer トラック / Sequencer 用の PostProcess ABP / AnimNode Function / 外力ボリューム）
    -   `07_SimpleWorldCollision`（レベルのコリジョンの自動収集 / 対応形状 / スケルタルメッシュとの当たり / 収集半径）
    -   `08_Wind`（ProceduralWind の成分 / 風プリセット DataAsset / 突風 AnimNotify / Shared Publisher）
    -   `09_Showcase`（TA式 鷺宮カノでの完成形のセットアップを部位ごとに紹介）
-   **使用キャラクター**:
    -   **Grayちゃん**: [http://rarihoma.xvs.jp/products/graychan](http://rarihoma.xvs.jp/products/graychan)
    -   **TA式 鷺宮カノ**: [株式会社TA様](https://xta.co.jp/)よりご提供
        -   Copyright (c) 2025 株式会社TA All rights reserved
        -   利用規約：[https://uzurig.com/ja/terms\_of\_use\_jp/](https://uzurig.com/ja/terms_of_use_jp/)

### MCP（Experimental）

サンプルプロジェクトでは UE 5.8 の Experimental プラグイン **Unreal MCP** を有効にしており、MCP クライアントからエディタを操作できます。Kawaii Physics は専用の Toolset（`KawaiiPhysicsToolset`）を同梱しており、ノードの作成・編集（KawaiiPhysics / Shared Publisher ノード、コリジョン、外力）、プリセットの適用と監査、PIE での検証（ランタイム情報、ボーンとコリジョンの確認、ボーン／貫通サンプラー、一時的な設定倍率とウィンドガスト）、スカートの確認・調整（リング状のボーン拘束、深さ別の半径、モーションの記録と解析）を行えます。

1.  次のどちらかで MCP サーバーを起動します
    -   コンソールコマンド `ModelContextProtocol.StartServer` を実行する（すぐに起動）
    -   **Editor Preferences > General > Model Context Protocol** で **Auto Start Server** を有効にする（次回起動から有効。設定はユーザーごとに保存）
2.  MCP クライアントから `http://127.0.0.1:8000/mcp` に接続します。プロジェクト直下の `.mcp.json` にこの設定が入っています。

Toolset は Agent Skill（`KawaiiPhysicsSetupSkill`）も登録し、MCP で KawaiiPhysics ノードを設定・調整・確認するときの注意点を提供します。

サーバーはクライアントからのエディタ操作を受け付けます。必要なときだけ起動してください。

## 採用実績

多くのプロジェクトで採用されています！

-   **[採用実績一覧](https://pafuhana1213.github.io/KawaiiPhysics-Portal/adoption)**
-   あなたの作品もぜひ [こちら](https://github.com/pafuhana1213/KawaiiPhysics/discussions/65) で教えてください！
-   Twitter/Xで作品を共有する際は、ぜひハッシュタグ **[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)** をお使いください！

## 関連ツール

-   [VRM SpringBoneをKawaiiPhysicsに変換するツール](https://yumetengu.booth.pm/items/7943387)

## ライセンス

[MIT License](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/LICENSE)

## 作者

[おかず @pafuhana1213](https://twitter.com/pafuhana1213)

## 更新履歴

-   [Announcements - アナウンス](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/announcements-%E3%82%A2%E3%83%8A%E3%82%A6%E3%83%B3%E3%82%B9)
-   [更新履歴 - Changelog (KawaiiPhysics Portal)](https://pafuhana1213.github.io/KawaiiPhysics-Portal/docs/changelog)

* * *

## よろしければご支援を！

このプラグインが、皆さんのUEプロジェクトに少しでも役立っていれば、とても嬉しいです。

開発は私個人が趣味と実益を兼ねて、開発時間の確保やコーヒー代を自腹でやりくりしながら進めています☕  
もし「なかなか良いじゃん！」と思っていただけたら、FABでの購入やGitHub Sponsorsで応援していただけると、  
開発を続ける上で大きな励みになります（[FABでの販売を開始に関して詳細な経緯はこちら](https://github.com/pafuhana1213/KawaiiPhysics/discussions/170)）。

[💖**FABで購入する**](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)  
[💖 **GitHub Sponsorsで応援する**](https://github.com/sponsors/pafuhana1213)