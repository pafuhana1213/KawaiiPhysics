[日本語](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/README.md) / [English](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/README_en.md)

---
# Kawaii Physics
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/pafuhana1213/KawaiiPhysics) 
![UE Version](https://img.shields.io/badge/Unreal%20Engine-5.3--5.6-0e1128?logo=unrealengine&logoColor=white)
![GitHub release (latest by tag)](https://img.shields.io/github/v/release/pafuhana1213/KawaiiPhysics)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

<a href="https://youtu.be/0f-l-SP07Mo">
  <img src="https://github.com/user-attachments/assets/0bc33f5c-c7db-49b3-8e98-75dc062a4e2a" alt="画像" width="640px">
</a>  

Demo : https://youtu.be/0f-l-SP07Mo

ダウンロード（C++ビルド環境がない方向け）  
Github : https://github.com/pafuhana1213/KawaiiPhysics/releases/  
Booth : [https://pafuhana1213.booth.pm/items/5943534](https://pafuhana1213.booth.pm/items/5943534)  
（ストアアカウントをフォローするとアップデート通知あり）

ディスカッション  
- [アップデート情報](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/announcements-%E3%82%A2%E3%83%8A%E3%82%A6%E3%83%B3%E3%82%B9)
- [要望・アイデア](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/ideas-%E8%A6%81%E6%9C%9B-%E3%82%A2%E3%82%A4%E3%83%87%E3%82%A2)
- [質問・Q&A](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/q-a)
- [あなたの作品を教えてください！](https://github.com/pafuhana1213/KawaiiPhysics/discussions/65)  
- [設定方法などについての雑談・相談の場](https://github.com/pafuhana1213/KawaiiPhysics/discussions/66)  
- [機能やサンプルのリクエスト方法について](https://github.com/pafuhana1213/KawaiiPhysics/discussions/67)  

不具合報告  
- https://github.com/pafuhana1213/KawaiiPhysics/issues

採用実績  
- https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E6%8E%A1%E7%94%A8%E5%AE%9F%E7%B8%BE-Adoption

## はじめに
Kawaii Physicsは UnrealEngine4,5用に作成した疑似物理プラグインです。  
髪、スカート、胸などの揺れものを「かんたんに」「かわいく」揺らすことができます。

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics1.gif)  
キャラクタ：[Grayちゃん](http://rarihoma.xvs.jp/products/graychan)

![dance5](https://github.com/user-attachments/assets/28d72d0c-4423-41c7-bc52-c5c7c3886e02)  
キャラクタ：[オリジナル3Dモデル『ルゼブル』-Lzebul-](https://booth.pm/ja/items/4887691)  
モーション：[ミライ小町 テーマソング「ミライ」MV ダンスアニメーション](https://www.miraikomachi.com/download/)

## 特徴
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg)  
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics4.gif)  
- 元の形状を尊重しつつ、アニメーションやSkeletalMeshComponentの移動・回転を元に物理制御を行います。
- プラグインに含まれる「KawaiiPhysicsノード」をAnimationBPのAnimGraphで使う形です。
- 指定したボーンとそれ以下のボーンをキャラクタの動きに合わせて揺らせます。
- 物理制御用のパラメータは2種類だけなので、エンジン標準の[AnimDynamics](https://docs.unrealengine.com/ja/Engine/Animation/NodeReference/SkeletalControls/AnimDynamics/index.html)に比べて簡単にセットアップできます。
- 球・カプセル・平面コリジョンを追加することができます
- アニメーションエディタのビューポート上で各コリジョンの位置・向き・大きさを調整できます
- 骨の長さを維持するため、仮に計算が破綻しても骨が伸び縮みすることがありません。
- PhysXは使わずにシンプルなアルゴリズムを使用しているため、エンジン標準の物理システムに比べて負荷が低い（はず）です。

物理挙動を実装するにあたって参考にした資料  
[次期アイドルマスター グラフィクス＆アニメーション プログラミング プレビュー](https://cedil.cesa.or.jp/cedil_sessions/view/416)

## 動作環境
- UE5.3 ~ 5.6  
https://github.com/pafuhana1213/KawaiiPhysics/releases/    
- UE4.27  
https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20230830-v1.11.1  


## 使い方
- **プロジェクト**のPluginsフォルダにKawaiiPhysicsフォルダを入れてください
- 各パラメータについて：[https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6](https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6--About-each-parameters)

- BPプロジェクトの場合、パッケージ時にエラーが発生します。お手数ですが、C++プロジェクトに変換するか、下記記事で紹介されている方法をお試しください。  
[UE5 KawaiiPhysicsをBlueprintプロジェクトでもpkg化する方法 - PaperSloth’s diary -](https://papersloth.hatenablog.com/entry/2024/02/14/201629)

### BPプロジェクトをC++プロジェクトに変換するには
「C++ クラス ウィザード」からC++コードを追加することで C++プロジェクトに変換可能です。  
https://docs.unrealengine.com/5.0/ja/using-the-cplusplus-class-wizard-in-unreal-engine/  

ただし、Visual StudioなどのC++をビルドできる環境を準備する必要があります。  
https://docs.unrealengine.com/5.0/ja/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine/

## サンプル
![sample2](https://github.com/user-attachments/assets/0d866ad2-f803-400b-bd23-2d46ab17b8ae)
- サンプルレベル：Content/KawaiiPhysicsSample/L_KawaiiPhysicsSample
- お借りしたキャラクタ：
  - Grayちゃん
    - http://rarihoma.xvs.jp/products/graychan
  - TA式 鷺宮カノ
    - [株式会社TA](https://xta.co.jp/)様よりセットアップサンプル込みでご提供いただきました！
    - 利用規約：https://uzurig.com/ja/terms_of_use_jp/
    - Copyright (c) 2025 株式会社TA (https://xta.co.jp) All rights reserved

## 解説記事・動画
### 公式
- [揺れ骨用自作AnimNode「Kawaii Physics」の内部実装解説的なもの その1](http://pafuhana1213.hatenablog.com/entry/2019/07/26/171046)
- [#ControlRig を使ったスカートの突き抜け対策と#KawaiiPhysics を併用してみるテスト](https://twitter.com/pafuhana1213/status/1300454762542817280)

### 非公式( ありがとうございます！）
- [揺れモノ超ド素人がKawaiiPhysicsを使って、感覚を頼りに女性キャラクターモデルのKawaiiを増す方法まとめ](https://qiita.com/YuukiOgino/items/7f3198a90dab43019f26)
- [揺れものとコントロールリグ UEを使用したバーチャルライブでの実例](https://www.docswell.com/s/indieusgames/K4Q2XJ-2024-06-14-175045)
- [【UE】揺れ物設定（Kawaii Physics）](https://techblog.sumelagi.co.jp/unrealengine/147/)
  - 補足：骨ごとの影響度調整は「◯◯◯ by bone length rate」プロパティである程度実現可能です
- [【UE4】Kawaii Physicsチュートリアル#1](https://www.youtube.com/watch?v=hlgXuVML_is)
- [How To Setup Kawaii Physics in Unreal Engine 5](https://dev.epicgames.com/community/learning/tutorials/d1Z9/unreal-engine-how-to-setup-kawaii-physics-in-unreal)
- [５分でわかるKawaii Physicsの使い方！【UE5】【tutorial】](https://www.youtube.com/watch?v=TliP9vSxm4c)

## ライセンス
[MIT](https://github.com/pafuhana1213/KawaiiPhysics?tab=MIT-1-ov-file#readme)

## 作者
[おかず@pafuhana1213](https://twitter.com/pafuhana1213)

## ハッシュタグ
[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)

## 更新履歴  
[Announcements - アナウンス](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/announcements-%E3%82%A2%E3%83%8A%E3%82%A6%E3%83%B3%E3%82%B9)
