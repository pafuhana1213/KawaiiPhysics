# Kawaii Physics
English doc :   
https://github.com/pafuhana1213/KawaiiPhysics/blob/master/README_en.md  

Forum :   
日本語  https://forums.unrealengine.com/international/japan/1679269  
英語  https://forums.unrealengine.com/community/released-projects/1638095

## はじめに
Kawaii Physicsは UnrealEngine4用に作成した疑似物理プラグインです。  
髪、スカート、胸などの揺れものを「かんたんに」「かわいく」揺らすことができます。

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics1.gif)  
お借りしたキャラクタ：Gray ちゃん http://rarihoma.xvs.jp/products/graychan

![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics0.gif)  
https://www.youtube.com/watch?v=UvpEIBGegvs  
お借りしたキャラクタ：ミライ小町 https://www.bandainamcostudios.com/works/miraikomachi/

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
- 4.25.4

- UE4.20~4.26（Pluginのみ）  
https://github.com/pafuhana1213/KawaiiPhysics/releases/

UE4.21以前でビルドする場合は、KawaiiPhysicsEditMode.cpp における  
GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy () を  
GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy (false)　に修正する必要があります。 

UE4.23以前でビルドする場合は、awaiiPhysics.uplugin における  
"Type": "UncookedOnly",　を "Type": "DeveloperTool", に修正する必要があります（たぶん）。

## 使い方
- プロジェクトのPluginsフォルダにKawaiiPhysicsフォルダを入れてください
- 各パラメータについて：https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6

## サンプル
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics3.jpg)  
- Content/KawaiiPhysicsSample/KawaiiPhysicsSample
- お借りしたキャラクタ：Gray ちゃん http://rarihoma.xvs.jp/products/graychan

## 内部実装について
その1 http://pafuhana1213.hatenablog.com/entry/2019/07/26/171046

## ライセンス
MIT

## 採用実績
https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E6%8E%A1%E7%94%A8%E5%AE%9F%E7%B8%BE-Adoption

## 作者
[おかず@pafuhana1213](https://twitter.com/pafuhana1213)

## ハッシュタグ
[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)

## 履歴
- 2020/12/2 v1.7.0
  - UAnimInstance::ResetDynamics(ETeleportType::ResetPhysics)呼び出しでのリセットに対応
    - 説明：https://twitter.com/pafuhana1213/status/1334141624666910722
    - https://github.com/pafuhana1213/KawaiiPhysics/pull/30
      - KazumasaOhashi様、ありがとうございました！
    - サンプルレベルの2キーでテストできるようにしました
- 2020/11/17 v.1.6.2
  - FKawaiiPhysicsEditMode::DrawHUDが正常に動作していない不具合を修正
- 2020/9/4 v1.6.1  
  - プロパティのカテゴリ指定忘れによるパッケージ作成エラーの修正
- 2020/8/29 v1.6.0
  - 専用のDataAsset(KawaiiPhysicsLimitsDataAsset)でコリジョン設定を管理・流用できるようになりました
    - 使い方：https://youtu.be/jnJqRu7zn3c
    - 動作実績があまりないのでExperimentalということでお願いします
  - UE4.25.3をデフォルトにしました
- 2020/7/28 v1.5.3
  - MeshとSkeletonのBoneIndexが違う場合に、正しい長さが取得できなくて、カーブでのパラメータが正常に動作しなくなる不具合修正
    - https://github.com/pafuhana1213/KawaiiPhysics/pull/21
    - nkinta 様ありがとうございました！
- 2020/5/29 v1.5.2  
  - v1.5.1 における対応がDummyBoneを考慮してなかった不具合の修正
- 2020/5/29 v1.5.1
  - bEnableDebugBoneLengthRate によるデバッグ表示の表示位置がズレていた不具合の修正
  - ↑のデバッグ表示のテキスト形式をエンジン標準のボーン名表示と同じに
    - https://twitter.com/pafuhana1213/status/1266264162788765696
- 2020/5/29 v1.5.0
  - ボーンの向きを設定するBoneForwardAxisプロパティを追加。これはDummyBoneの挙動にのみ影響します  
  (SAM-tak 様、ありがとうございました！)
    - https://twitter.com/pafuhana1213/status/1266224108657762307
- 2020/5/15 v1.4.6  
  - Rootと同じボーンをExcludeBoneに指定するとクラッシュする不具合を修正  
  - 不正な設定の場合はノードをWarning または Error表示にする処理を追加
    - 技術解説：http://pafuhana1213.hatenablog.com/entry/2020/05/16/173837
- 2020/5/15 v1.4.5
  - GravityをComponentSpaceで動作するように変更  
  (dxd39 様、ありがとうございました！)
- 2020/3/13 v1.4.4  
  - MacOSでビルドに失敗する不具合の修正  
  (melMass 様、ありがとうございました！)  
- 2020/3/13 v1.4.3  
  - Package作成に失敗する不具合を修正  
- 2020/2/16 v1.4.2  
  - Standalone実行時に正常に動作しない不具合を修正  
- 2020/1/31 v1.4.1  
  - **SphereLimitのLimitTypeがInnerの場合、Outer設定時に異なりBoneのRadius設定を考慮してなかった不具合の修正  
  1.3.1以前の挙動に戻したい方は CVarEnableOldPhysicsMethodSphereLimit を 1 に設定してください**    
- 2020/1/31 v1.4.0  
  - **重力変数の位置更新の計算がgt^2でなくgtになっていたので修正  
  1.3.1以前の重力計算を使用したい場合は　p.KawaiiPhysics.EnableOldPhysicsMethodGravity を 1 に設定してください**  
- 2020/1/31 v1.3.1  
  - TeleportRotationThresholdの計算にてラジアンと度が混在していたことでWorldRotationのテレポート対応がほぼ機能してなかった不具合を修正  
  (monguri 様、ありがとうございました！)  
- 2020/1/31 v1.3.0  
  - UE4.24.2 に対応  
  (TheHoodieGuy02 様、ありがとうございました！)  
- 2019/10/29 v1.2.1  
  - World Damping系を従来処理に差し戻し。30fps時の挙動が更に安定しました  
  (@seiko_dev 様、ありがとうございました！)  
- 2019/10/26 v1.2.0  
  - 簡易可変フレームレート対応。フレームレート低下時の挙動が少し安定するようになったはず  
  (@seiko_dev 様、ありがとうございました！)  
  - 基準フレームレートを設定できるようにしました(デフォルト：60)（正直テスト不足です。何かあったらissueへ  
  - カーブの調整用に各ボーンの始点からの長さの割合をデバッグ表示するようにしました  
- 2019/10/19 v1.1.2   
  - BoneのScaleが(1,1,1)以外の環境に対応できてなかったので修正  
  (shop-0761様、ありがとうございました！)  
- 2019/9/11 v1.1.1  
  - Bone.LengthFromRoot の計算が間違っていたのを修正  
  (KazumasaOhashi様、ありがとうございました！)  
- 2019/8/26 v1.1   
  - WindDirectionalSourceに対応しました(注意：従来のWind設定に対して破壊的変更が入ります)  
- 2019/7/20 v1.0.1   
  - CollisionのOffsetLocationがボーンのRotationを考慮していない不具合の修正  
  (v1.0 におけるOffsetLocationの挙動が大きく変化します)  
- 2019/7/2 v1.0 公開 v1.0
