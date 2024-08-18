# Kawaii Physics
<a href="https://youtu.be/0f-l-SP07Mo">
  <img src="https://github.com/user-attachments/assets/0bc33f5c-c7db-49b3-8e98-75dc062a4e2a" alt="画像" width="640px">
</a>  

Demo : https://youtu.be/0f-l-SP07Mo

English doc   
https://github.com/pafuhana1213/KawaiiPhysics/blob/master/README_en.md

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
- UE5.2 ~ 5.4  
https://github.com/pafuhana1213/KawaiiPhysics/releases/  
  
- UE4.27  
https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20230830-v1.11.1  
- UE4.26以前（Pluginのみ）  
https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20201202-v1.7.0

UE4.26以前でビルドする際は、GetSkeleton()をSkeletonに修正する必要があります。

UE4.21以前でビルドする場合は、KawaiiPhysicsEditMode.cpp における  
GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy () を  
GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy (false)　に修正する必要があります。 

UE4.23以前でビルドする場合は、awaiiPhysics.uplugin における  
"Type": "UncookedOnly",　を "Type": "DeveloperTool", に修正する必要があります（たぶん）。

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
![](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics3.jpg)  
- Content/KawaiiPhysicsSample/KawaiiPhysicsSample
- お借りしたキャラクタ：Gray ちゃん http://rarihoma.xvs.jp/products/graychan

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

## ライセンス
MIT

## 作者
[おかず@pafuhana1213](https://twitter.com/pafuhana1213)

## ハッシュタグ
[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)

## 履歴
- 2024/8/16 v1.17.0　[詳細はこちら](https://github.com/pafuhana1213/KawaiiPhysics/discussions/137)
  - Additional RootBones
  - Box Limit
  - PhysicsAsset for Limits
  - AnimNotify / AnimNotifyState for NewExternalForce
  - BP nodes for NewExternalForce
  - NewExternalForce Wind
  - Export BoneConstraint DataAsset
- 2024/7/19 v1.16.0　[詳細はこちら](https://github.com/pafuhana1213/KawaiiPhysics/discussions/128)
  - New External Force
  - 詳細パネルの整理・機能追加
  - コンソール変数を追加（レベル上でのデバッグ用）
  - DataAsset Update
- 2024/2/8 v1.15.0　[詳細はこちら](https://github.com/pafuhana1213/KawaiiPhysics/discussions/117)
  - UE5.4に対応しました
- 2024/2/8 v1.14.0
  - 骨間の距離拘束を行う BoneConstraint機能 を実験的に追加しました
    - ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/af5845e4-65aa-41c1-ba0e-ae466e90d19f)
    - 指定した骨間（BoneConstraints/Bone1, 2)の距離を維持しようとします（XPBDを使用）。なお、指定する骨はKawaiiPhysicsの制御対象である必要があります。
    - Bone Constrain Iteration Count Before(After) Collision で拘束処理の実行回数を指定できます（結果を収束させるため）。
    - DataAssetでも設定可能にしました。試験的にDataAssetに正規表現で拘束対象の骨セットを設定できるようにしています。
      ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/a43faf14-181e-4b95-8d16-5eb4b687dd12)
  - KawaiiPhysicsのAnimNodeが持つDataAssetをピンに公開できるようにしました
  - Planar Limitの処理に無駄があったので修正 https://github.com/pafuhana1213/KawaiiPhysics/pull/108
  - BoneConstraint対応に伴い、物理処理部分のコードをリファクタリング
- 2023/9/15 v1.13.0
  - 物理の空回し(Warm Up)機能・サンプルを追加しました
    - https://www.youtube.com/watch?v=stIOjZQh3Qw
    - <img width="525" alt="image" src="https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/e140f3b9-232a-4026-a0b0-c8e1e54a492f">
    - Need Warm Up が true かつ Warm Up Frame( 空回しするフレーム数 ) が 1以上だと Warm Up 処理が行われます
    - 実行後は Need Warm Upは false になります
    - Warm up Frameが大きいほど安定しますが、物理計算の回数がその分増えるので負荷が上がります
    - ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/ef55a7fd-699b-48ba-9bd8-13be62d23961)  
      v1.12.0から対応したAnimNode関数からWarm Upを有効にすることも可能です
- 2023/9/8 v1.12.0
  - UE5.3に対応
    - といっても、特に何もしていないです
  - AnimNode関数から各パラメータの変更・取得を可能に
    - ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/6ae6098a-64f3-4138-8822-704426dd70f4)
    - サンプルを追加（AnimBP_GrayChanKawaiiPhysicsAnimNodeFunction）
  - AnimNode関数への対応に伴い、UE4.27のサポートを終了
- 2023/8/30 v1.11.1
  - LevelSequence・ControlRigと併用した際のクラッシュを修正 https://github.com/pafuhana1213/KawaiiPhysics/commit/5a2fd868d9ccbd87b3727614faeb861cd7854d8a
  - AdjustByAngleLimitを正規化するように　https://github.com/pafuhana1213/KawaiiPhysics/commit/9a0576cfa06d37eb0c1b35f57757737ac61288b8
  - LimitAngleがピンに公開されてなかった不具合を修正　https://github.com/pafuhana1213/KawaiiPhysics/commit/937a1247f96190700fe06ca7559274059e62d111
- 2023/5/28 v1.11.0
  - UE5.2に対応
    - といっても、特に何もしていないです
  - KawaiiPhysicsとSequencerを併用するサンプルを追加
    - SkeletalMeshComponentのAnimClassをAnimBlueprintに設定することと、AnimGraphにSlotノードがあるかを要チェック
  - コンパイルしなくてもプロパティの変更を挙動に反映できるように
    - CopyNodeDataToPreviewNodeの対応が抜けていたので追加
  - UE5.1にてコライダー（～Limits系）のプレビューが上手く動作しない不具合（２つ目以降がコンパイルしないと出てこない）を修正 https://github.com/pafuhana1213/KawaiiPhysics/issues/84
  - コライダー（～Limits系）の回転を編集したときにクラッシュする事がある不具合を修正　https://github.com/pafuhana1213/KawaiiPhysics/issues/86
  - DataAssetからのLimitsをDeleteで消した際のクラッシュを修正　https://github.com/pafuhana1213/KawaiiPhysics/issues/72
  - コライダー（～Limits系）のDrivingBoneに指定しているボーンがLODによって除去された際にクラッシュする不具合を修正 https://github.com/pafuhana1213/KawaiiPhysics/issues/87
  - Control Rig, AnimationLayer と併用した際のクラッシュを（たぶん）修正 https://github.com/pafuhana1213/KawaiiPhysics/issues/56
  - サンプルのモデルにLOD1・2を追加しました（動作チェック用）
- 2022/12/03 v1.10.0
  - UE5.1に対応 
  - LODでボーン除去している場合に発生するクラッシュの修正
  - 一部コードをRider様によってリファクタリング
- 2022/4/11 v1.9.0
  -  カーブアセットを作らなくても、カーブでのパラメーター編集ができるようになりました　https://github.com/pafuhana1213/KawaiiPhysics/pull/32 
  -  ワールドコリジョン機能が入りました。サンプルにも追加してます　https://github.com/pafuhana1213/KawaiiPhysics/pull/26 
  -  LOD設定などにより末端のボーンが機能しない場合、DummyBoneが生成されない不具合を修正しました　https://github.com/pafuhana1213/KawaiiPhysics/pull/26 
  -  ResetPhysics時に物理設定がリセットされる不具合を修正しました　https://github.com/pafuhana1213/KawaiiPhysics/issues/38
  -  LOD切り替え時のあらぶりを抑える（かもしれない）処理を追加しました。ResetBoneTransformWhenBoneNotFoundを有効にしたら以前の処理に戻ります　https://github.com/pafuhana1213/KawaiiPhysics/issues/44 
- 2022/4/11 v1.8.0
  - UE5正式版に対応
  - UE4.26以下のサポートを一旦切りました。  
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
