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

// SyncBones の対象（子ボーン一覧など）は ModifyBones の構築時に作られるので、変えたら作り直しを要求する
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeReinitSyncBonesTest,
                                 "KawaiiPhysics.RuntimeReinit.SyncBonesRebuildBones",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeReinitSyncBonesTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	A.ClearPendingReinitForTest();
	const FName SyncName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SyncBones);
	const bool bSet = UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		A.Node, SyncName, TEXT("((Bone=(BoneName=\"spine\"),TargetRoots=((Bone=(BoneName=\"hair_a\")))))"));
	bool bOk = TestTrue(TEXT("SyncBones import succeeds"), bSet);
	bOk &= TestTrue(TEXT("changing SyncBones rebuilds bones"), A.IsModifyBonesReinitPending());
	bOk &= TestTrue(TEXT("changing SyncBones re-initializes bone references"), A.IsBoneReferencesReinitPending());
	return bOk;
}

} // namespace KawaiiPhysicsRuntimeReinitTestPrivate

#endif
