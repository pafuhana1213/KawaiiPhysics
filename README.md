# Kawaii Physics

![UE Version](https://img.shields.io/badge/Unreal%20Engine-5.3--5.7-0e1128?logo=unrealengine&logoColor=white)
[![GitHub release (latest by tag)](https://img.shields.io/github/v/release/pafuhana1213/KawaiiPhysics)](https://github.com/pafuhana1213/KawaiiPhysics/releases)
[![Downloads](https://img.shields.io/github/downloads/pafuhana1213/KawaiiPhysics/total)](https://github.com/pafuhana1213/KawaiiPhysics/releases)
[![Discussions](https://img.shields.io/github/discussions/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/discussions)
[![GitHub contributors](https://img.shields.io/github/contributors/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/graphs/contributors)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/pafuhana1213/KawaiiPhysics)
[![X (formerly Twitter) Follow](https://img.shields.io/twitter/follow/pafuhana1213?style=social)](https://twitter.com/pafuhana1213)

[English README](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/README_en.md)

---

Kawaii Physicsは、Unreal Engine向けのシンプルな疑似物理プラグインです。  
髪、スカート、胸などの揺れものを「かんたんに」「かわいく」揺らすことができます。

<a href="https://youtu.be/0f-l-SP07Mo">
  <img src="https://github.com/user-attachments/assets/0bc33f5c-c7db-49b3-8e98-75dc062a4e2a" alt="Demo Video" width="640px">
</a>

[デモ動画 (Demo Video)](https://youtu.be/0f-l-SP07Mo)

## 🖼️ ギャラリー (Gallery)

<img src="https://github.com/user-attachments/assets/fda69859-b60f-4fde-a683-62da3e2839e4" alt="compare" width="640px">

*キャラクター：[Grayちゃん](http://rarihoma.xvs.jp/products/graychan)*

<img src="https://github.com/user-attachments/assets/28d72d0c-4423-41c7-bc52-c5c7c3886e02" alt="dance5" width="640px">

*キャラクター：[オリジナル3Dモデル『ルゼブル』-Lzebul-](https://booth.pm/ja/items/4887691) / モーション：[ミライ小町](https://www.miraikomachi.com/download/)*

<img src="https://github.com/user-attachments/assets/63faed3c-8aaa-4d4d-ae33-e98f9c8c15fd" alt="danceKano" width="640px">

*キャラクター：[TA式 鷺宮カノ](https://uzurig.com/ja/uzurig2-rigging-plugin-for-maya-jp/) / モーション：[しかのこのこのここしたんたん](https://booth.pm/ja/items/5975857) / セットアップ：[株式会社TA様](https://xta.co.jp/)*

## ✨ 特徴 (Features)

<table>
  <tr>
    <td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg?raw=true" width="320"></td>
    <td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics4.gif?raw=true" width="320"></td>
  </tr>
</table>

- アニメーションやキャラクターの動きに基づいた物理制御
- AnimGraph内の`KawaiiPhysics`ノード一つで簡単にセットアップ
- 球・カプセル・平面のコリジョンに対応し、ビューポートで直感的に調整可能
- 計算が破綻してもスケルトンが伸び縮みしない安定性
- PhysX非依存の軽量なアルゴリズム
- 風や重力だけでなく、演出目的の外力も適用可能
- `DataAsset`や`PhysicsAsset`を使ったパラメータの保存・共有が可能

### 参考資料
物理挙動を実装するにあたって、以下の資料を参考にしました。
- [次期アイドルマスター グラフィクス＆アニメーション プログラミング プレビュー](https://cedil.cesa.or.jp/cedil_sessions/view/416)

## 🚀 導入方法 (Getting Started)

### 1. ダウンロード

C++ビルド環境がない方は、以下のリンクからプラグインをダウンロードしてください。
- **[GitHub Releases](https://github.com/pafuhana1213/KawaiiPhysics/releases/)**
- **[FAB](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)** (内容はGitHub版と同じです）
- **[Booth](https://pafuhana1213.booth.pm/items/5943534)** (ストアをフォローすると更新通知が届きます)

### 2. 対応バージョン

- **UE 5.3 ~ 5.7**
- UE4.27: [v1.11.1](https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20230830-v1.11.1)

### 3. インストール

1. プロジェクトのルートに `Plugins` フォルダを作成します。
2. ダウンロードした `KawaiiPhysics` フォルダを `Plugins` フォルダにコピーします。

### 4. 使い方

- Animation BlueprintのAnimGraphで `KawaiiPhysics` ノードを追加して利用します。
- 詳細なパラメータ設定については、Wikiを参照してください。
  - **[各パラメータについて](https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6--About-each-parameters)**

> **Note:** Blueprintのみのプロジェクトでパッケージ化する際にエラーが出る場合があります。その際は、C++プロジェクトに変換するか、[こちらの記事](https://papersloth.hatenablog.com/entry/2024/02/14/201629)を参考にしてください。

## 📚 ドキュメント & コミュニティ

- **[Wiki](https://github.com/pafuhana1213/KawaiiPhysics/wiki)**: 公式ドキュメント（整備中）
- **[DeepWiki](https://deepwiki.com/pafuhana1213/KawaiiPhysics)**: AIによる自動生成ドキュメント
- **[Discussions](https://github.com/pafuhana1213/KawaiiPhysics/discussions)**: 質問、要望、雑談などはこちらへ
- **[Issues](https://github.com/pafuhana1213/KawaiiPhysics/issues)**: 不具合報告

## 🎓 解説記事 & 動画 (Tutorials)

### 公式

- [揺れ骨用自作AnimNode「Kawaii Physics」の内部実装解説的なもの その1](http.pafuhana1213.hatenablog.com/entry/2019/07/26/171046)
- [#ControlRig を使ったスカートの突き抜け対策と#KawaiiPhysics を併用してみるテスト](https://twitter.com/pafuhana1213/status/1300454762542817280)

### 非公式 (ありがとうございます！)

- [揺れモノ超ド素人がKawaiiPhysicsを使って、感覚を頼りに女性キャラクターモデルのKawaiiを増す方法まとめ](https://qiita.com/YuukiOgino/items/7f3198a90dab43019f26)
- [揺れものとコントロールリグ UEを使用したバーチャルライブでの実例](https://www.docswell.com/s/indieusgames/K4Q2XJ-2024-06-14-175045)
- [【UE】揺れ物設定（Kawaii Physics）](https://techblog.sumelagi.co.jp/unrealengine/147/)
- [【UE4】Kawaii Physicsチュートリアル#1](https://www.youtube.com/watch?v=hlgXuVML_is)
- [How To Setup Kawaii Physics in Unreal Engine 5](https://dev.epicgames.com/community/learning/tutorials/d1Z9/unreal-engine-how-to-setup-kawaii-physics-in-unreal)
- [５分でわかるKawaii Physicsの使い方！【UE5】【tutorial】](https://www.youtube.com/watch?v=TliP9vSxm4c)
- [The Hidden Physics “Engine” Behind Unreal's Most Stylish Games ...](https://www.youtube.com/watch?v=9ThmoMHnHhw)

## 🎮 サンプル (Sample)

<img src="https://github.com/user-attachments/assets/0d866ad2-f803-400b-bd23-2d46ab17b8ae" alt="sample2" width="640px">

プロジェクト内にサンプルレベルとキャラクターが含まれています。
- **サンプルレベル**: `Content/KawaiiPhysicsSample/L_KawaiiPhysicsSample`
- **使用キャラクター**:
  - **Grayちゃん**: http://rarihoma.xvs.jp/products/graychan
  - **TA式 鷺宮カノ**: [株式会社TA様](https://xta.co.jp/)よりご提供
    - Copyright (c) 2025 株式会社TA All rights reserved
    - 利用規約：https://uzurig.com/ja/terms_of_use_jp/

## 🌟 採用実績 (Showcase)

多くのプロジェクトで採用されています！
- **[採用実績一覧](https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E6%8E%A1%E7%94%A8%E5%AE%9F%E7%B8%BE-Adoption)**
- あなたの作品もぜひ [こちら](https://github.com/pafuhana1213/KawaiiPhysics/discussions/65) で教えてください！
- Twitter/Xで作品を共有する際は、ぜひハッシュタグ **[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)** をお使いください！

## 関連ツール(Related tools)
- [VRM SpringBoneをKawaiiPhysicsに変換するツール](https://yumetengu.booth.pm/items/7943387)

## 📜 ライセンス (License)

[MIT License](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/LICENSE)

## 👨‍💻 作者 (Author)

[おかず @pafuhana1213](https://twitter.com/pafuhana1213)

## 📅 更新履歴 (Changelog)

[Announcements - アナウンス](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/announcements-%E3%82%A2%E3%83%8A%E3%82%A6%E3%83%B3%E3%82%B9)

---

## ✨ よろしければご支援を！

このプラグインが、皆さんのUEプロジェクトに少しでも役立っていれば、とても嬉しいです。

開発は私個人が趣味と実益を兼ねて、開発時間の確保やコーヒー代を自腹でやりくりしながら進めています☕  
もし「なかなか良いじゃん！」と思っていただけたら、FABでの購入やGitHub Sponsorsで応援していただけると、  
開発を続ける上で大きな励みになります（[FABでの販売を開始に関して詳細な経緯はこちら](https://github.com/pafuhana1213/KawaiiPhysics/discussions/170)）。  

[💖**FABで購入する**](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)  
[💖 **GitHub Sponsorsで応援する**](https://github.com/sponsors/pafuhana1213)
