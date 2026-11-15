// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsRuntimeReinitTestPrivate
{
// root → spine → hair_a → hair_b の4本。LOD を模して spine を外すと、hair_a / hair_b の CompactPose の番号がずれる
TStrongObjectPtr<USkeleton> MakeRuntimeReinitTestSkeleton()
{
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>(GetTransientPackage()));
	FReferenceSkeletonModifier Modifier(Skeleton.Get());
	Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
	Modifier.Add(FMeshBoneInfo(TEXT("spine"), TEXT("spine"), 0), FTransform::Identity);
	Modifier.Add(FMeshBoneInfo(TEXT("hair_a"), TEXT("hair_a"), 0), FTransform::Identity);
	Modifier.Add(FMeshBoneInfo(TEXT("hair_b"), TEXT("hair_b"), 2), FTransform::Identity);
	return Skeleton;
}

void InitRuntimeReinitTestContainer(FBoneContainer& Container, USkeleton& Skeleton, TArray<FBoneIndexType> Indices)
{
	Container.InitializeTo(Indices, UE::Anim::FCurveFilterSettings(), Skeleton);
}

FSphericalLimit MakeRuntimeReinitTestLimit(const FName BoneName, const ECollisionSourceType SourceType)
{
	FSphericalLimit Limit;
	Limit.DrivingBone.BoneName = BoneName;
	Limit.SourceType = SourceType;
	Limit.Radius = 5.0f;
	return Limit;
}

// PAF-356：LOD 変更（CacheBones → InitializeBoneReferences）で、データアセット由来のコリジョンのボーン参照も更新する
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitLodLimitsDataTest,
                                 "KawaiiPhysics.RuntimeReinit.LodUpdatesDataAssetLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitLodLimitsDataTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<USkeleton> Skeleton = MakeRuntimeReinitTestSkeleton();
	FBoneContainer FullLod;
	InitRuntimeReinitTestContainer(FullLod, *Skeleton, {0, 1, 2, 3});
	FBoneContainer ReducedLod;
	InitRuntimeReinitTestContainer(ReducedLod, *Skeleton, {0, 2, 3});

	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> Asset(NewObject<UKawaiiPhysicsLimitsDataAsset>(GetTransientPackage()));
	Asset->SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("hair_b"), ECollisionSourceType::DataAsset));

	FKawaiiPhysicsTestAccessor A;
	A.Node.LimitsDataAsset = Asset.Get();
	A.ApplyLimitsDataAsset(FullLod);
	bool bOk = TestEqual(TEXT("data asset limit is applied"), A.Node.SphericalLimitsData.Num(), 1);
	if (!bOk)
	{
		return false;
	}
	const FCompactPoseBoneIndex FullIndex = A.Node.SphericalLimitsData[0].DrivingBone.GetCompactPoseIndex(FullLod);

	// LOD が下がると hair_b の CompactPose の番号は 3 から 2 になる
	A.CallInitializeBoneReferences(ReducedLod);
	const FCompactPoseBoneIndex ReducedIndex = A.Node.SphericalLimitsData[0].DrivingBone.GetCompactPoseIndex(ReducedLod);
	const FCompactPoseBoneIndex Expected = ReducedLod.MakeCompactPoseIndex(
		FMeshPoseBoneIndex(ReducedLod.GetPoseBoneIndexForBoneName(TEXT("hair_b"))));
	bOk &= TestNotEqual(TEXT("the LOD change moves the compact index"), FullIndex.GetInt(), Expected.GetInt());
	bOk &= TestEqual(TEXT("data asset limit follows the LOD change"), ReducedIndex.GetInt(), Expected.GetInt());
	return bOk;
}

// PAF-357：実行時に RootBone を名前だけで差し替えても、次の評価の冒頭でボーン参照が初期化される
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitRootBoneTest,
                                 "KawaiiPhysics.RuntimeReinit.RootBoneSetAtRuntime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitRootBoneTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<USkeleton> Skeleton = MakeRuntimeReinitTestSkeleton();
	FBoneContainer Container;
	InitRuntimeReinitTestContainer(Container, *Skeleton, {0, 1, 2, 3});

	FKawaiiPhysicsTestAccessor A;
	A.Node.RootBone = FBoneReference(TEXT("hair_a"));
	A.CallInitializeBoneReferences(Container);
	A.ClearPendingReinitForTest();

	// SetRootBoneName と同じ書き換え（名前だけの未初期化の参照になる）
	A.Node.RootBone = FBoneReference(TEXT("hair_b"));
	A.Node.RequestModifyBonesReinit();
	bool bOk = TestFalse(TEXT("a renamed bone reference is not valid yet"), A.Node.RootBone.IsValidToEvaluate(Container));
	bOk &= TestTrue(TEXT("rebuilding bones also re-initializes bone references"), A.IsBoneReferencesReinitPending());

	A.CallApplyPendingRuntimeReinit(Container);
	bOk &= TestTrue(TEXT("RootBone is valid after the pending re-initialization"), A.Node.RootBone.IsValidToEvaluate(Container));
	bOk &= TestFalse(TEXT("the request is consumed"), A.IsBoneReferencesReinitPending());
	return bOk;
}

// PAF-358：実行時に LimitsDataAsset を差し替えたら、次の評価の冒頭でコリジョンを作り直す（nullptr で古い形状が消える）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitLimitsDataAssetTest,
                                 "KawaiiPhysics.RuntimeReinit.LimitsDataAssetSetAtRuntime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitLimitsDataAssetTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<USkeleton> Skeleton = MakeRuntimeReinitTestSkeleton();
	FBoneContainer Container;
	InitRuntimeReinitTestContainer(Container, *Skeleton, {0, 1, 2, 3});

	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> AssetA(NewObject<UKawaiiPhysicsLimitsDataAsset>(GetTransientPackage()));
	AssetA->SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("hair_a"), ECollisionSourceType::DataAsset));
	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> AssetB(NewObject<UKawaiiPhysicsLimitsDataAsset>(GetTransientPackage()));
	AssetB->SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("hair_b"), ECollisionSourceType::DataAsset));
	AssetB->SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("spine"), ECollisionSourceType::DataAsset));

	FKawaiiPhysicsTestAccessor A;
	A.Node.LimitsDataAsset = AssetA.Get();
	A.ApplyLimitsDataAsset(Container);
	A.ClearPendingReinitForTest();

	// 汎用の setter（SetNodeWildcardProperty と同じ経路）で差し替える
	FObjectProperty* AssetProperty = CastField<FObjectProperty>(FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset)));
	UKawaiiPhysicsLimitsDataAsset* NewAsset = AssetB.Get();
	bool bOk = TestTrue(TEXT("setter succeeds"), UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(
		A.Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset), AssetProperty, &NewAsset));
	bOk &= TestTrue(TEXT("changing the asset requests a re-apply"), A.IsLimitAssetsReapplyPending());

	A.CallApplyPendingRuntimeReinit(Container);
	bOk &= TestEqual(TEXT("limits come from the new asset"), A.Node.SphericalLimitsData.Num(), 2);
	bOk &= TestTrue(TEXT("new limits are valid to evaluate"),
		A.Node.SphericalLimitsData.Num() == 2 && A.Node.SphericalLimitsData[0].DrivingBone.IsValidToEvaluate(Container));

	// 同じ値をもう一度設定しても作り直さない（毎フレーム設定する使い方でリセットを繰り返さない）
	A.ClearPendingReinitForTest();
	UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(
		A.Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset), AssetProperty, &NewAsset);
	bOk &= TestFalse(TEXT("setting the same asset does not re-apply"), A.IsLimitAssetsReapplyPending());
	bOk &= TestFalse(TEXT("setting the same asset does not rebuild bones"), A.IsModifyBonesReinitPending());

	// nullptr にしたら古いコリジョンを消す
	UKawaiiPhysicsLimitsDataAsset* NoAsset = nullptr;
	UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(
		A.Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset), AssetProperty, &NoAsset);
	A.CallApplyPendingRuntimeReinit(Container);
	bOk &= TestEqual(TEXT("clearing the asset removes its limits"), A.Node.SphericalLimitsData.Num(), 0);
	return bOk;
}

// PAF-357：ModifyBones を作り直さないボーン参照（ノードのコリジョンなど）も、文字列の setter で変えたら初期化し直す
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitCollisionFromStringTest,
                                 "KawaiiPhysics.RuntimeReinit.CollisionSetFromString",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitCollisionFromStringTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<USkeleton> Skeleton = MakeRuntimeReinitTestSkeleton();
	FBoneContainer Container;
	InitRuntimeReinitTestContainer(Container, *Skeleton, {0, 1, 2, 3});

	FKawaiiPhysicsTestAccessor A;
	A.Node.SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("hair_a"), ECollisionSourceType::AnimNode));
	A.CallInitializeBoneReferences(Container);
	A.ClearPendingReinitForTest();

	const FName PropertyName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimits);
	FString Text;
	UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(A.Node, PropertyName, Text);

	// 同じ値を書き戻しても何も要求しない
	bool bOk = TestTrue(TEXT("same value import succeeds"), UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(A.Node, PropertyName, Text));
	bOk &= TestFalse(TEXT("same value does not request bone reference reinit"), A.IsBoneReferencesReinitPending());
	// 配列は読み込みで要素が作り直されるので、同じ値でも書き込むと初期化済みのボーン番号が消える
	bOk &= TestTrue(TEXT("same value keeps the initialized bone reference"),
		A.Node.SphericalLimits[0].DrivingBone.IsValidToEvaluate(Container));

	// DrivingBone の名前だけを書き換える（古い BoneIndex が残る経路）
	Text = Text.Replace(TEXT("hair_a"), TEXT("hair_b"));
	bOk &= TestTrue(TEXT("import succeeds"), UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(A.Node, PropertyName, Text));
	bOk &= TestTrue(TEXT("collision change requests bone reference reinit"), A.IsBoneReferencesReinitPending());
	bOk &= TestTrue(TEXT("collision change requests a re-apply for mirroring"), A.IsLimitAssetsReapplyPending());

	A.CallApplyPendingRuntimeReinit(Container);
	const FCompactPoseBoneIndex Expected = Container.MakeCompactPoseIndex(
		FMeshPoseBoneIndex(Container.GetPoseBoneIndexForBoneName(TEXT("hair_b"))));
	bOk &= TestEqual(TEXT("collision uses the new bone"),
		A.Node.SphericalLimits[0].DrivingBone.GetCompactPoseIndex(Container).GetInt(), Expected.GetInt());
	return bOk;
}

// コリジョンの Location / Rotation / bEnable / Plane（とエディタの Guid / Type）は実行時にノードが書き込む欄で、設定ではない。
// 設定だけを書いた同じ値を毎フレーム渡しても「変更あり」にしない（ボーン参照の初期化とアセット由来のコリジョンの作り直しを
// 毎フレーム走らせない。パッケージ版では DataAsset 由来の平面の前ステップの履歴も毎回消えてしまう）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitCollisionRuntimeFieldsTest,
                                 "KawaiiPhysics.RuntimeReinit.CollisionRuntimeFieldsIgnored",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitCollisionRuntimeFieldsTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	const FName SphericalName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimits);
	const FName PlanarName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PlanarLimits);
	const TCHAR* SphericalText = TEXT("((DrivingBone=(BoneName=\"hair_a\"),Radius=5.000000))");
	const TCHAR* PlanarText = TEXT("((DrivingBone=(BoneName=\"spine\"),OffsetLocation=(X=0.000000,Y=0.000000,Z=10.000000)))");
	bool bOk = TestTrue(TEXT("spherical import succeeds"),
		UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(A.Node, SphericalName, SphericalText));
	bOk &= TestTrue(TEXT("planar import succeeds"),
		UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(A.Node, PlanarName, PlanarText));
	if (!bOk || A.Node.SphericalLimits.Num() != 1 || A.Node.PlanarLimits.Num() != 1)
	{
		return false;
	}

	// 評価でノードが書き込む欄を埋めた状態にする
	FSphericalLimit& Sphere = A.Node.SphericalLimits[0];
	Sphere.Location = FVector(1.0, 2.0, 3.0);
	Sphere.Rotation = FQuat(FVector::UpVector, 0.5);
	Sphere.bEnable = false;
	FPlanarLimit& Plane = A.Node.PlanarLimits[0];
	Plane.Location = FVector(0.0, 0.0, 10.0);
	Plane.Rotation = FQuat(FVector::ForwardVector, 0.25);
	Plane.Plane = FPlane(Plane.Location, Plane.Rotation.GetUpVector());
	A.ClearPendingReinitForTest();

	// 文字列の setter：設定だけを書いた同じ値（読み込みで作られる要素は Location などが初期値、エディタでは Guid も新しい）
	UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(A.Node, SphericalName, SphericalText);
	UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(A.Node, PlanarName, PlanarText);
	bOk &= TestFalse(TEXT("same collision settings do not re-apply the limit assets"), A.IsLimitAssetsReapplyPending());
	bOk &= TestFalse(TEXT("same collision settings do not re-initialize bone references"), A.IsBoneReferencesReinitPending());
	bOk &= TestEqual(TEXT("runtime location is kept"), A.Node.SphericalLimits[0].Location, FVector(1.0, 2.0, 3.0));

	// ワイルドカードの setter：新しく作った同じ設定の要素
	const FProperty* SphericalProperty = FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(SphericalName);
	TArray<FSphericalLimit> SameSettings;
	FSphericalLimit& SameSphere = SameSettings.AddDefaulted_GetRef();
	SameSphere.DrivingBone.BoneName = TEXT("hair_a");
	SameSphere.Radius = 5.0f;
	UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(A.Node, SphericalName, SphericalProperty, &SameSettings);
	bOk &= TestFalse(TEXT("same collision settings through the wildcard setter do not re-apply"),
		A.IsLimitAssetsReapplyPending());

	// 設定が変われば今まで通り作り直す
	SameSettings[0].Radius = 8.0f;
	UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(A.Node, SphericalName, SphericalProperty, &SameSettings);
	bOk &= TestTrue(TEXT("a changed radius re-applies the limit assets"), A.IsLimitAssetsReapplyPending());
	bOk &= TestEqual(TEXT("the changed radius is written"), A.Node.SphericalLimits[0].Radius, 8.0f);
	return bOk;
}

// 同じ名前の（未初期化の）ボーン参照を汎用 setter で渡しても、初期化済みのボーン番号を消さない。
// 消えると再初期化も要求されないまま RootBone の判定に失敗し、ノードが止まる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitSameBoneReferenceTest,
                                 "KawaiiPhysics.RuntimeReinit.SameBoneReferenceKeepsIndex",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitSameBoneReferenceTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<USkeleton> Skeleton = MakeRuntimeReinitTestSkeleton();
	FBoneContainer Container;
	InitRuntimeReinitTestContainer(Container, *Skeleton, {0, 1, 2, 3});

	FKawaiiPhysicsTestAccessor A;
	A.Node.RootBone = FBoneReference(TEXT("hair_a"));
	FKawaiiPhysicsRootBoneSetting& Additional = A.Node.AdditionalRootBones.AddDefaulted_GetRef();
	Additional.RootBone = FBoneReference(TEXT("hair_b"));
	A.CallInitializeBoneReferences(Container);
	A.ClearPendingReinitForTest();

	// ワイルドカード（構造体）：同じ名前の未初期化の参照
	const FName RootBoneName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RootBone);
	const FProperty* RootProperty = FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(RootBoneName);
	FBoneReference SameRoot(TEXT("hair_a"));
	bool bOk = TestTrue(TEXT("wildcard setter succeeds"),
		UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(A.Node, RootBoneName, RootProperty, &SameRoot));
	bOk &= TestTrue(TEXT("RootBone stays valid"), A.Node.RootBone.IsValidToEvaluate(Container));
	bOk &= TestFalse(TEXT("same RootBone does not rebuild bones"), A.IsModifyBonesReinitPending());

	// 文字列（配列）：同じ内容
	const FName AdditionalName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, AdditionalRootBones);
	FString Text;
	UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(A.Node, AdditionalName, Text);
	bOk &= TestTrue(TEXT("string setter succeeds"),
		UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(A.Node, AdditionalName, Text));
	bOk &= TestTrue(TEXT("additional root stays valid"),
		A.Node.AdditionalRootBones[0].RootBone.IsValidToEvaluate(Container));
	bOk &= TestFalse(TEXT("same additional roots do not rebuild bones"), A.IsModifyBonesReinitPending());
	return bOk;
}

// SyncBones を変えたら対象（子ボーン一覧など）だけを作り直す。ModifyBones は作り直さない（物理状態をリセットしない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitSyncBonesTest,
                                 "KawaiiPhysics.RuntimeReinit.SyncBonesReinitTargets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitSyncBonesTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	A.ClearPendingReinitForTest();
	const FName SyncName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SyncBones);
	const bool bSet = UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		A.Node, SyncName, TEXT("((Bone=(BoneName=\"spine\"),TargetRoots=((Bone=(BoneName=\"hair_a\")))))"));
	bool bOk = TestTrue(TEXT("SyncBones import succeeds"), bSet);
	bOk &= TestTrue(TEXT("changing SyncBones rebuilds the sync targets"), A.IsSyncBonesReinitPending());
	bOk &= TestFalse(TEXT("changing SyncBones does not rebuild bones"), A.IsModifyBonesReinitPending());
	bOk &= TestTrue(TEXT("changing SyncBones re-initializes bone references"), A.IsBoneReferencesReinitPending());

	// 作り直しで InitSyncBones が実行時のキャッシュ（UPROPERTY）を埋めた状態にする
	FKawaiiPhysicsSyncTargetRoot& Target = A.Node.SyncBones[0].TargetRoots[0];
	Target.ModifyBoneIndex = 3;
	Target.ChildTargets.Add(FKawaiiPhysicsSyncTarget(4));
	A.Node.SyncBones[0].InitialPoseLocation = FVector(1.0, 2.0, 3.0);
	A.ClearPendingReinitForTest();

	// 設定だけを組み立てた同じ値を毎フレーム渡しても、キャッシュの違いで「変更あり」にしない（物理をリセットし続けない）
	UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		A.Node, SyncName, TEXT("((Bone=(BoneName=\"spine\"),TargetRoots=((Bone=(BoneName=\"hair_a\")))))"));
	bOk &= TestFalse(TEXT("same SyncBones settings do not rebuild the sync targets"), A.IsSyncBonesReinitPending());
	bOk &= TestEqual(TEXT("runtime caches are kept"), A.Node.SyncBones[0].TargetRoots[0].ChildTargets.Num(), 1);

	FProperty* SyncProperty = FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(SyncName);
	TArray<FKawaiiPhysicsSyncBone> SameSettings;
	FKawaiiPhysicsSyncBone& SameSyncBone = SameSettings.AddDefaulted_GetRef();
	SameSyncBone.Bone.BoneName = TEXT("spine");
	SameSyncBone.TargetRoots.AddDefaulted_GetRef().Bone.BoneName = TEXT("hair_a");
	UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(A.Node, SyncName, SyncProperty, &SameSettings);
	bOk &= TestFalse(TEXT("same SyncBones settings through the wildcard setter do not rebuild the sync targets"),
		A.IsSyncBonesReinitPending());
	return bOk;
}

// アニメーショングラフのピンは setter を通らずにノードへ直接書き込む。ピンで LimitsDataAsset を差し替えても、
// 評価の冒頭（全ビルド構成で走る処理）でコリジョンが作り直される（パッケージ版ではここでしか反映されない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitPinAssetTest,
                                 "KawaiiPhysics.RuntimeReinit.LimitsDataAssetSetByPin",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitPinAssetTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<USkeleton> Skeleton = MakeRuntimeReinitTestSkeleton();
	FBoneContainer Container;
	InitRuntimeReinitTestContainer(Container, *Skeleton, {0, 1, 2, 3});

	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> AssetA(NewObject<UKawaiiPhysicsLimitsDataAsset>(GetTransientPackage()));
	AssetA->SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("hair_a"), ECollisionSourceType::DataAsset));
	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> AssetB(NewObject<UKawaiiPhysicsLimitsDataAsset>(GetTransientPackage()));
	AssetB->SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("hair_b"), ECollisionSourceType::DataAsset));
	AssetB->SphericalLimits.Add(MakeRuntimeReinitTestLimit(TEXT("spine"), ECollisionSourceType::DataAsset));

	FKawaiiPhysicsTestAccessor A;
	A.Node.LimitsDataAsset = AssetA.Get();
	A.CallApplyPendingRuntimeReinit(Container);
	A.ApplyLimitsDataAsset(Container);
	A.ClearPendingReinitForTest();
	A.CallApplyPendingRuntimeReinit(Container);

	// ピンと同じく、直接書き込む
	A.Node.LimitsDataAsset = AssetB.Get();
	A.CallApplyPendingRuntimeReinit(Container);
	bool bOk = TestEqual(TEXT("limits follow the asset set by the pin"), A.Node.SphericalLimitsData.Num(), 2);

	// ピンで外す
	A.Node.LimitsDataAsset = nullptr;
	A.CallApplyPendingRuntimeReinit(Container);
	bOk &= TestEqual(TEXT("clearing the asset by the pin removes its limits"), A.Node.SphericalLimitsData.Num(), 0);
	return bOk;
}

} // namespace KawaiiPhysicsRuntimeReinitTestPrivate

#endif
