# Kawaii Physics
## はじめに
Kawaii Physicsは UnrealEngine4用に作成した疑似物理プラグインです。  
髪、スカート、胸などの揺れものを「それっぽく」「かわいく」揺らすことができます。

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics1.gif)  
お借りしたキャラクタ：Gray ちゃん http://rarihoma.xvs.jp/products/graychan

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics0.gif)  
https://www.youtube.com/watch?v=UvpEIBGegvs  
お借りしたキャラクタ：ミライ小町 https://www.bandainamcostudios.com/works/miraikomachi/

## 特徴
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg)  
- プラグインに含まれる「KawaiiPhysicsノード」をAnimationBPのAnimGraphで使う形です。
- 指定したボーンとそれ以下のボーンをキャラクタの動きに合わせて揺らせます。
- 物理制御用のパラメータは2種類だけなので、エンジン標準の[AnimDynamics](https://docs.unrealengine.com/ja/Engine/Animation/NodeReference/SkeletalControls/AnimDynamics/index.html)に比べて簡単にセットアップできます。
- 球・カプセル・平面コリジョンを追加することができます
- アニメーションエディタのビューポート上で各コリジョンの位置・向き・大きさを調整できます
- シンプルなアルゴリズムを使用しているため、エンジン標準の物理システムに比べて負荷が低いです。

物理挙動を実装するにあたって参考にした資料  
[次期アイドルマスター グラフィクス＆アニメーション プログラミング プレビュー](https://cedil.cesa.or.jp/cedil_sessions/view/416)
