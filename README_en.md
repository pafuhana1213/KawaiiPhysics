# Kawaii Physics
## Description
Kawaii Physics is a pseudo-physical plug-in created for Unreal Engine 4.  
You can sway your hair, skirt, chest, etc easily and n a Kawaii way.

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics1.gif)  
She is Gray-chan http://rarihoma.xvs.jp/products/graychan

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics0.gif)  
https://www.youtube.com/watch?v=UvpEIBGegvs  
She is MiraiKomachi https://www.bandainamcostudios.com/works/miraikomachi/

## Features
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg)  
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

## Requirement
- UE4.22  
(プラグインを再ビルドすれば過去のバージョンでも動く「かも」しれません)

- UE4.20, 4.21（Pluginのみ）  
https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20190702

## Usage
- プロジェクトのPluginsフォルダにKawaiiPhysicsフォルダを入れてください
- 各パラメータについて：https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6

## SampleMap
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics3.jpg)  
- Content/KawaiiPhysicsSample/KawaiiPhysicsSample
- She is Gray-chan http://rarihoma.xvs.jp/products/graychan

## License
MIT

## Author
[@pafuhana1213](https://twitter.com/pafuhana1213)

