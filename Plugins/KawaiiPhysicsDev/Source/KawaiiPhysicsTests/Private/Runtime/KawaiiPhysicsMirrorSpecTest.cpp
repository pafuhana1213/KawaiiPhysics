// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/Skeleton.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsMirrorSpecTestPrivate
{
	int32 CountMirroredSpheres(const TArray<FSphericalLimit>& Limits)
	{
		int32 Count = 0;
		for (const FSphericalLimit& Limit : Limits)
		{
			if (Limit.SourceType == ECollisionSourceType::Mirror) ++Count;
		}
		return Count;
	}

// MR-06、mutant 1061: spine→spine の除外を消すと Mirror が 1 個増える（spine 由来の Mirror ができる）。
// 対照: 同じテーブルの thigh_l→thigh_r からは Mirror が 1 個できる（ミラー自体が動いていて、spine 由来が 0 個なのは自己ミラーの除外によることを示す）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorSpecSelfMirrorTest,
	"KawaiiPhysics.MirrorSpec.SelfMirrorIsSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorSpecSelfMirrorTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>());
	{
		FReferenceSkeletonModifier Modifier(Skeleton.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("spine"), TEXT("spine"), 0), FTransform(FVector(0, 0, 10)));
		Modifier.Add(FMeshBoneInfo(TEXT("thigh_l"), TEXT("thigh_l"), 0), FTransform(FVector(10, 0, 0)));
		Modifier.Add(FMeshBoneInfo(TEXT("thigh_r"), TEXT("thigh_r"), 0), FTransform(FVector(-10, 0, 0)));
	}
	FBoneContainer Container;
	Container.InitializeTo(TArray<FBoneIndexType>{0, 1, 2, 3}, UE::Anim::FCurveFilterSettings(), *Skeleton);
	TStrongObjectPtr<UMirrorDataTable> Table(NewObject<UMirrorDataTable>());
	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->MirrorAxis = EAxis::X;
	FMirrorTableRow Row;
	Row.Name = TEXT("spine");
	Row.MirroredName = TEXT("spine");
	Table->AddRow(Row.Name, Row);
	FMirrorTableRow ThighRow;
	ThighRow.Name = TEXT("thigh_l");
	ThighRow.MirroredName = TEXT("thigh_r");
	Table->AddRow(ThighRow.Name, ThighRow);
	for (const bool bSkipExisting : {true, false})
	{
		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.Node.MirrorDataTableForLimits = Table.Get();
		Accessor.Node.bSkipMirroredBoneWithExistingCollision = bSkipExisting;
		FSphericalLimit Sphere;
		Sphere.DrivingBone = FBoneReference(TEXT("spine"));
		Sphere.Radius = 3.0f;
		Sphere.bEnable = true;
		Accessor.Node.SphericalLimits.Add(Sphere);
		FSphericalLimit ThighSphere = Sphere;
		ThighSphere.DrivingBone = FBoneReference(TEXT("thigh_l"));
		Accessor.Node.SphericalLimits.Add(ThighSphere);
		Accessor.ApplyMirrorLimits(Container);
		TestEqual(TEXT("対照: 元の Sphere は 2 個ある"), Accessor.Node.SphericalLimits.Num(), 2);
		// Mirror は thigh_l 由来の 1 個だけ。spine の自己ミラーが除外されなければ 2 個になる。
		TestEqual(TEXT("自己ミラーは設定にかかわらず生成しない"),
			CountMirroredSpheres(Accessor.Node.SphericalLimitsData), 1);
		bool bSpineMirrored = false;
		bool bThighMirrored = false;
		for (const FSphericalLimit& Generated : Accessor.Node.SphericalLimitsData)
		{
			if (Generated.SourceType != ECollisionSourceType::Mirror) continue;
			bSpineMirrored |= Generated.DrivingBone.BoneName == TEXT("spine");
			bThighMirrored |= Generated.DrivingBone.BoneName == TEXT("thigh_r");
		}
		TestTrue(TEXT("対照: thigh_l→thigh_r のミラーはできる"), bThighMirrored);
		TestFalse(TEXT("spine を元にしたミラーは無い"), bSpineMirrored);
	}
	return true;
}

// MR-02 / MR-03、mutant 1063: MergedLimits を空にすると、統合済みの DataAsset と PhysicsAsset の球 2 個の Mirror が欠ける。
// 旧テストは球を統合先へ直置きしたため、両 Asset の取り込みを消しても通った。今は取り込み直後の元球検査で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorSpecMergedSourcesTest,
	"KawaiiPhysics.MirrorSpec.MergedSourcesProduceMirrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorSpecMergedSourcesTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>());
	{
		FReferenceSkeletonModifier Modifier(Skeleton.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("thigh_l"), TEXT("thigh_l"), 0), FTransform(FVector(5, 0, 0)));
		Modifier.Add(FMeshBoneInfo(TEXT("thigh_r"), TEXT("thigh_r"), 0), FTransform(FVector(-5, 0, 0)));
	}
	FBoneContainer Container;
	Container.InitializeTo(TArray<FBoneIndexType>{0, 1, 2}, UE::Anim::FCurveFilterSettings(), *Skeleton);
	TStrongObjectPtr<UMirrorDataTable> Table(NewObject<UMirrorDataTable>());
	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->MirrorAxis = EAxis::X;
	FMirrorTableRow Row;
	Row.Name = TEXT("thigh_l");
	Row.MirroredName = TEXT("thigh_r");
	Table->AddRow(Row.Name, Row);
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.MirrorDataTableForLimits = Table.Get();
	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> SphereAsset(NewObject<UKawaiiPhysicsLimitsDataAsset>());
	FSphericalLimit AssetSphere;
	AssetSphere.DrivingBone = FBoneReference(TEXT("thigh_l"));
	AssetSphere.Radius = 3.0f;
	AssetSphere.bEnable = true;
	AssetSphere.SourceType = ECollisionSourceType::DataAsset;
	SphereAsset->SphericalLimits.Add(AssetSphere);
	Accessor.Node.LimitsDataAsset = SphereAsset.Get();
	TStrongObjectPtr<UPhysicsAsset> SpherePhysicsAsset(NewObject<UPhysicsAsset>());
	USkeletalBodySetup* SphereBodySetup = NewObject<USkeletalBodySetup>(SpherePhysicsAsset.Get());
	SphereBodySetup->BoneName = TEXT("thigh_l");
	FKSphereElem PhysicsSphere;
	PhysicsSphere.Radius = 5.0f;
	SphereBodySetup->AggGeom.SphereElems.Add(PhysicsSphere);
	SpherePhysicsAsset->SkeletalBodySetups.Add(SphereBodySetup);
	Accessor.Node.PhysicsAssetForLimits = SpherePhysicsAsset.Get();
	FSphericalLimit NodeSphere = AssetSphere;
	NodeSphere.Radius = 2.0f;
	NodeSphere.SourceType = ECollisionSourceType::AnimNode;
	Accessor.Node.SphericalLimits.Add(NodeSphere);
	Accessor.ApplyLimitsDataAsset(Container);
	Accessor.ApplyPhysicsAsset(Container);
	TestEqual(TEXT("対照: Asset 二経路の元球だけが統合された"), Accessor.Node.SphericalLimitsData.Num(), 2);
	int32 OriginalAssetSpheres = 0;
	int32 OriginalPhysicsSpheres = 0;
	for (const FSphericalLimit& Limit : Accessor.Node.SphericalLimitsData)
	{
		if (Limit.DrivingBone.BoneName != TEXT("thigh_l")) continue;
		if (Limit.SourceType == ECollisionSourceType::DataAsset && Limit.Radius == 3.0f) ++OriginalAssetSpheres;
		if (Limit.SourceType == ECollisionSourceType::PhysicsAsset && Limit.Radius == 5.0f) ++OriginalPhysicsSpheres;
	}
	TestEqual(TEXT("対照: DataAsset の元球"), OriginalAssetSpheres, 1);
	TestEqual(TEXT("対照: PhysicsAsset の元球"), OriginalPhysicsSpheres, 1);
	TestEqual(TEXT("対照: AnimNode の元球"), Accessor.Node.SphericalLimits.Num(), 1);
	Accessor.ApplyMirrorLimits(Container);
	TestEqual(TEXT("Asset の元球 2 個と三経路の Mirror が統合先にある"), Accessor.Node.SphericalLimitsData.Num(), 5);
	TestEqual(TEXT("三経路から Mirror が一個ずつ"), CountMirroredSpheres(Accessor.Node.SphericalLimitsData), 3);
	int32 MirroredRadius2 = 0;
	int32 MirroredRadius3 = 0;
	int32 MirroredRadius5 = 0;
	for (const FSphericalLimit& Limit : Accessor.Node.SphericalLimitsData)
	{
		if (Limit.SourceType != ECollisionSourceType::Mirror) continue;
		TestTrue(TEXT("鏡像の DrivingBone は右側"), Limit.DrivingBone.BoneName == TEXT("thigh_r"));
		if (Limit.Radius == 2.0f) ++MirroredRadius2;
		if (Limit.Radius == 3.0f) ++MirroredRadius3;
		if (Limit.Radius == 5.0f) ++MirroredRadius5;
	}
	TestEqual(TEXT("AnimNode の半径"), MirroredRadius2, 1);
	TestEqual(TEXT("DataAsset の半径"), MirroredRadius3, 1);
	TestEqual(TEXT("PhysicsAsset の半径"), MirroredRadius5, 1);
	return true;
}

// MR-01、mutant なし。テーブルを外した後は Mirror が 0 個で、未設定セルと 60 フレームの位置がビット一致。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorSpecNullTableTest,
	"KawaiiPhysics.MirrorSpec.NullTableBitExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorSpecNullTableTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions MirrorOptions;
	MirrorOptions.Bones = {
		{TEXT("root"), INDEX_NONE, FTransform::Identity},
		{TEXT("chain_0"), 0, FTransform::Identity},
		{TEXT("chain_1"), 1, FTransform(FVector(12, 0, 0))},
		{TEXT("source_l"), 0, FTransform(FVector(0, -10, 0))},
		{TEXT("source_r"), 0, FTransform(FVector(0, 10, 0))}};
	MirrorOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
	{
		PhysicsNode.RootBone.BoneName = TEXT("chain_0");
		PhysicsNode.DummyBoneLength = 0.0f;
		PhysicsNode.WarmUpFrames = 0;
		PhysicsNode.Gravity = FVector::ZeroVector;
		PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
		PhysicsNode.PhysicsSettings.Radius = 0.0f;
		FSphericalLimit ActiveSphere;
		ActiveSphere.DrivingBone.BoneName = TEXT("root");
		ActiveSphere.OffsetLocation = FVector(12, -2, 0);
		ActiveSphere.Radius = 3.0f;
		ActiveSphere.bEnable = true;
		PhysicsNode.SphericalLimits.Add(ActiveSphere);
		FSphericalLimit MirroredSource;
		MirroredSource.DrivingBone.BoneName = TEXT("source_l");
		MirroredSource.OffsetLocation = FVector(1000, 0, 0);
		MirroredSource.Radius = 3.0f;
		MirroredSource.bEnable = true;
		PhysicsNode.SphericalLimits.Add(MirroredSource);
	};
	KawaiiPhysicsNodeFixture::FFixture NeverConfigured(MirrorOptions);
	KawaiiPhysicsNodeFixture::FFixture TemporarilyConfigured(MirrorOptions);
	TStrongObjectPtr<UMirrorDataTable> Table(NewObject<UMirrorDataTable>());
	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->MirrorAxis = EAxis::X;
	FMirrorTableRow Row;
	Row.Name = TEXT("source_l");
	Row.MirroredName = TEXT("source_r");
	Table->AddRow(Row.Name, Row);
	TemporarilyConfigured.Node.MirrorDataTableForLimits = Table.Get();
	TemporarilyConfigured.Accessor.ApplyMirrorLimits(TemporarilyConfigured.Proxy->GetRequiredBones());
	TestEqual(TEXT("対照: テーブルがあると Mirror が生成される"),
		CountMirroredSpheres(TemporarilyConfigured.Node.SphericalLimitsData), 1);
	TemporarilyConfigured.Node.MirrorDataTableForLimits = nullptr;
	TemporarilyConfigured.Accessor.ApplyMirrorLimits(TemporarilyConfigured.Proxy->GetRequiredBones());
	bool bCollisionActive = false;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		NeverConfigured.EvaluateFrame();
		TemporarilyConfigured.EvaluateFrame();
		if (!TestTrue(TEXT("対照: ModifyBones が作られた"), NeverConfigured.Accessor.Num() > 1)) return false;
		if (!TestEqual(TEXT("比較セルのボーン数"), NeverConfigured.Accessor.Num(), TemporarilyConfigured.Accessor.Num())) return false;
		TestEqual(TEXT("テーブルなしでは Mirror は 0 個"),
			CountMirroredSpheres(NeverConfigured.Node.SphericalLimitsData), 0);
		TestEqual(TEXT("テーブルを外すと Mirror は 0 個"),
			CountMirroredSpheres(TemporarilyConfigured.Node.SphericalLimitsData), 0);
		for (int32 BoneIndex = 0; BoneIndex < NeverConfigured.Accessor.Num(); ++BoneIndex)
		{
			const FVector& ReferenceLocation = NeverConfigured.Accessor.Bone(BoneIndex).Location;
			const FVector& ResetLocation = TemporarilyConfigured.Accessor.Bone(BoneIndex).Location;
			TestTrue(FString::Printf(TEXT("ミラー未設定と復元後の位置はビット一致 %d/%d"), Frame, BoneIndex),
				FMemory::Memcmp(&ReferenceLocation, &ResetLocation, sizeof(FVector)) == 0);
			if (NeverConfigured.Accessor.Bone(BoneIndex).BoneRef.BoneName == TEXT("chain_1")
				&& FVector::Dist(ReferenceLocation, FVector(12, 0, 0)) > 0.1) bCollisionActive = true;
		}
	}
	TestTrue(TEXT("対照: Sphere がチェーンに効いた"), bCollisionActive);
	return true;
}

} // namespace KawaiiPhysicsMirrorSpecTestPrivate

#endif
