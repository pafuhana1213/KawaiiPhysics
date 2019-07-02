# Kawaii Physics
## はじめに
Kawaii Physicsは UnrealEngine4用に作成した疑似物理プラグインです。  
髪、スカート、胸などの揺れものを「かんたんに」「それっぽく」「かわいく」揺らすことができます。

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics1.gif)  
お借りしたキャラクタ：Gray ちゃん http://rarihoma.xvs.jp/products/graychan

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics0.gif)  
https://www.youtube.com/watch?v=UvpEIBGegvs  
お借りしたキャラクタ：ミライ小町 https://www.bandainamcostudios.com/works/miraikomachi/

## 特徴
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg)  
- 元の形状を尊重しつつ、アニメーションやSkeletalMeshComponentの移動・回転を元に物理制御を行います。
- プラグインに含まれる「KawaiiPhysicsノード」をAnimationBPのAnimGraphで使う形です。
- 指定したボーンとそれ以下のボーンをキャラクタの動きに合わせて揺らせます。
- 物理制御用のパラメータは2種類だけなので、エンジン標準の[AnimDynamics](https://docs.unrealengine.com/ja/Engine/Animation/NodeReference/SkeletalControls/AnimDynamics/index.html)に比べて簡単にセットアップできます。
- 球・カプセル・平面コリジョンを追加することができます
- アニメーションエディタのビューポート上で各コリジョンの位置・向き・大きさを調整できます
- 骨の長さを維持するため、仮に計算が破綻しても骨が伸び縮みすることがありません。
- シンプルなアルゴリズムを使用しているため、エンジン標準の物理システムに比べて負荷が低いです。

物理挙動を実装するにあたって参考にした資料  
[次期アイドルマスター グラフィクス＆アニメーション プログラミング プレビュー](https://cedil.cesa.or.jp/cedil_sessions/view/416)

## 動作環境
- UE4.22
(プラグインを再ビルドすれば過去のバージョンでも動く「かも」しれません)

## 使い方
- プロジェクトのPluginsフォルダにKawaiiPhysicsフォルダを入れてください
- 各パラメータについて：https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6

## サンプル
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics3.jpg)  
- Content/KawaiiPhysicsSample/KawaiiPhysicsSample
- お借りしたキャラクタ：Gray ちゃん http://rarihoma.xvs.jp/products/graychan

## ライセンス
MIT

## 作者
[おかず@pafuhana1213](https://twitter.com/pafuhana1213)

## 履歴
2019/7/2 公開

