// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Animation/MirrorDataTable.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsCollisionSourceSpecTestPrivate
{

// BuildSimpleWorldCollisionDesc はノードの protected なので、派生型の名前で取ったメンバー関数ポインタで呼ぶ（フィクスチャとアクセサを変えないため）。
struct FSimpleWorldDescProbe : FAnimNode_KawaiiPhysics
{
	static FKawaiiPhysicsSimpleWorldCollisionDesc BuildFor(const FAnimNode_KawaiiPhysics& SourceNode)
	{
		return (SourceNode.*(&FSimpleWorldDescProbe::BuildSimpleWorldCollisionDesc))();
	}
};
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsCollisionSource, "KawaiiPhysics.Test.CollisionSource");

	KawaiiPhysicsNodeFixture::FOptions MakeBoxChainOptions()
	{
		KawaiiPhysicsNodeFixture::FOptions BoxOptions;
		BoxOptions.Bones = {
			{TEXT("root"), INDEX_NONE, FTransform::Identity},
			{TEXT("chain_0"), 0, FTransform(FVector(0, 0, 5))},
			{TEXT("chain_1"), 1, FTransform(FVector(1, 2, -1))}};
		BoxOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("chain_0");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
			PhysicsNode.PhysicsSettings.Radius = 1.0f;
		};
		return BoxOptions;
	}

	FBoxLimit MakeInteriorBox()
	{
		FBoxLimit InteriorBox;
		InteriorBox.Extent = FVector(6, 6, 5);
		InteriorBox.bEnable = true;
		return InteriorBox;
	}

	bool CheckBoxRoute(FAutomationTestBase& AutomationTest, KawaiiPhysicsNodeFixture::FFixture& BoxFixture,
		const TCHAR* RouteName, const FVector& ExpectedLocation)
	{
		BoxFixture.EvaluateFrame();
		if (!AutomationTest.TestTrue(FString::Printf(TEXT("%s: 対照の ModifyBones は親子二本"), RouteName),
			BoxFixture.Accessor.Num() >= 2)) return false;
		return AutomationTest.TestTrue(FString::Printf(TEXT("%s: 内部点は最浅 Z 面の外側へ"), RouteName),
			BoxFixture.Accessor.Bone(1).Location.Equals(ExpectedLocation, 1e-3));
	}

// BX-11、mutant なし。DataAsset と PhysicsAsset の Box も q=(1,2,4) を q'=(1,2,6) へ押す。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSourceSpecAssetBoxesTest,
	"KawaiiPhysics.CollisionSpec.Source.AssetBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSourceSpecAssetBoxesTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture EmptyFixture(MakeBoxChainOptions());
	if (!CheckBoxRoute(*this, EmptyFixture, TEXT("空セル"), FVector(1, 2, 4))) return false;

	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> BoxAsset(NewObject<UKawaiiPhysicsLimitsDataAsset>());
	FBoxLimit AssetBox = MakeInteriorBox();
	AssetBox.DrivingBone.BoneName = TEXT("chain_0");
	AssetBox.OffsetLocation = FVector(0, 0, -5);
	AssetBox.SourceType = ECollisionSourceType::DataAsset;
	BoxAsset->BoxLimits.Add(AssetBox);
	KawaiiPhysicsNodeFixture::FOptions AssetOptions = MakeBoxChainOptions();
	AssetOptions.ConfigureNode = [BoxAssetPointer = BoxAsset.Get()](FAnimNode_KawaiiPhysics& PhysicsNode)
	{
		PhysicsNode.RootBone.BoneName = TEXT("chain_0");
		PhysicsNode.DummyBoneLength = 0.0f;
		PhysicsNode.WarmUpFrames = 0;
		PhysicsNode.Gravity = FVector::ZeroVector;
		PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
		PhysicsNode.PhysicsSettings.Radius = 1.0f;
		PhysicsNode.LimitsDataAsset = BoxAssetPointer;
	};
	KawaiiPhysicsNodeFixture::FFixture AssetFixture(AssetOptions);
	if (!TestEqual(TEXT("対照: DataAsset の Box が統合された"), AssetFixture.Node.BoxLimitsData.Num(), 1)) return false;
	if (!TestTrue(TEXT("対照: DataAsset の出どころ"),
		AssetFixture.Node.BoxLimitsData[0].SourceType == ECollisionSourceType::DataAsset)) return false;
	if (!CheckBoxRoute(*this, AssetFixture, TEXT("DataAsset"), FVector(1, 2, 6))) return false;

	TStrongObjectPtr<UPhysicsAsset> BoxPhysicsAsset(NewObject<UPhysicsAsset>());
	USkeletalBodySetup* BodySetup = NewObject<USkeletalBodySetup>(BoxPhysicsAsset.Get());
	BodySetup->BoneName = TEXT("chain_0");
	FKBoxElem PhysicsBox;
	PhysicsBox.Center = FVector(0, 0, -5);
	PhysicsBox.X = 12.0f;
	PhysicsBox.Y = 12.0f;
	PhysicsBox.Z = 10.0f;
	BodySetup->AggGeom.BoxElems.Add(PhysicsBox);
	BoxPhysicsAsset->SkeletalBodySetups.Add(BodySetup);
	KawaiiPhysicsNodeFixture::FOptions PhysicsOptions = MakeBoxChainOptions();
	PhysicsOptions.ConfigureNode = [PhysicsAssetPointer = BoxPhysicsAsset.Get()](FAnimNode_KawaiiPhysics& PhysicsNode)
	{
		PhysicsNode.RootBone.BoneName = TEXT("chain_0");
		PhysicsNode.DummyBoneLength = 0.0f;
		PhysicsNode.WarmUpFrames = 0;
		PhysicsNode.Gravity = FVector::ZeroVector;
		PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
		PhysicsNode.PhysicsSettings.Radius = 1.0f;
		PhysicsNode.PhysicsAssetForLimits = PhysicsAssetPointer;
	};
	KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(PhysicsOptions);
	if (!TestEqual(TEXT("対照: PhysicsAsset の Box が統合された"), PhysicsFixture.Node.BoxLimitsData.Num(), 1)) return false;
	if (!TestTrue(TEXT("対照: PhysicsAsset の出どころ"),
		PhysicsFixture.Node.BoxLimitsData[0].SourceType == ECollisionSourceType::PhysicsAsset)) return false;
	if (!CheckBoxRoute(*this, PhysicsFixture, TEXT("PhysicsAsset"), FVector(1, 2, 6))) return false;
	// 親から入力と押し出し先までの距離は共に √6。float の復元を 1e-3 cm 許す。
	return true;
}

// BX-11 / MR-02、mutant なし。X 鏡映した Box は右側の q=(1,2,4) を q'=(1,2,6) へ押す。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSourceSpecMirrorBoxTest,
	"KawaiiPhysics.CollisionSpec.Source.MirrorBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSourceSpecMirrorBoxTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UMirrorDataTable> MirrorTable(NewObject<UMirrorDataTable>());
	MirrorTable->RowStruct = FMirrorTableRow::StaticStruct();
	MirrorTable->MirrorAxis = EAxis::X;
	FMirrorTableRow MirrorRow;
	MirrorRow.Name = TEXT("left_0");
	MirrorRow.MirroredName = TEXT("right_0");
	MirrorTable->AddRow(MirrorRow.Name, MirrorRow);
	KawaiiPhysicsNodeFixture::FOptions MirrorOptions;
	MirrorOptions.Bones = {
		{TEXT("root"), INDEX_NONE, FTransform::Identity},
		{TEXT("left_0"), 0, FTransform(FVector(10, 0, 5))},
		{TEXT("right_0"), 0, FTransform(FVector(-10, 0, 5))},
		{TEXT("right_1"), 2, FTransform(FVector(1, 2, -1))}};
	MirrorOptions.ConfigureNode = [MirrorTablePointer = MirrorTable.Get()](FAnimNode_KawaiiPhysics& PhysicsNode)
	{
		PhysicsNode.RootBone.BoneName = TEXT("right_0");
		PhysicsNode.DummyBoneLength = 0.0f;
		PhysicsNode.WarmUpFrames = 0;
		PhysicsNode.Gravity = FVector::ZeroVector;
		PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
		PhysicsNode.PhysicsSettings.Radius = 1.0f;
		PhysicsNode.MirrorDataTableForLimits = MirrorTablePointer;
		FBoxLimit SourceBox = MakeInteriorBox();
		SourceBox.DrivingBone.BoneName = TEXT("left_0");
		SourceBox.OffsetLocation = FVector(0, 0, -5);
		PhysicsNode.BoxLimits.Add(SourceBox);
	};
	KawaiiPhysicsNodeFixture::FFixture MirrorFixture(MirrorOptions);
	int32 MirroredBoxes = 0;
	for (const FBoxLimit& MirroredBox : MirrorFixture.Node.BoxLimitsData)
	{
		if (MirroredBox.SourceType == ECollisionSourceType::Mirror
			&& MirroredBox.DrivingBone.BoneName == TEXT("right_0")) ++MirroredBoxes;
	}
	if (!TestEqual(TEXT("対照: 右ボーンへ Mirror Box が生成された"), MirroredBoxes, 1)) return false;
	if (!CheckBoxRoute(*this, MirrorFixture, TEXT("Mirror"), FVector(-9, 2, 6))) return false;
	// 左の中心 (10,0,0) を X 鏡映すると c=(-10,0,0)、Q は軸に平行な Box の点集合。
	// 親 (-10,0,5) から前後の子までの距離は共に √6。float の復元を 1e-3 cm 許す。
	return true;
}

// BX-11、mutant なし。恒等 publish→read と地面専用 Slot の Box は同じ最浅 Z 面へ押す。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSourceSpecPublishedBoxesTest,
	"KawaiiPhysics.CollisionSpec.Source.PublishedAndGroundBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSourceSpecPublishedBoxesTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture EmptyFixture(MakeBoxChainOptions());
	if (!CheckBoxRoute(*this, EmptyFixture, TEXT("空セル"), FVector(1, 2, 4))) return false;
	KawaiiPhysicsNodeFixture::FScopedCVar SimpleWorldEnabled(
		TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("1"));
	FBoxLimit WorldBox = MakeInteriorBox();
	for (const bool bGround : {false, true})
	{
		KawaiiPhysicsNodeFixture::FOptions PublishedOptions = MakeBoxChainOptions();
		if (bGround)
		{
			// 地面 Box の厚み半分は固定 10。親を上げて q_z=9 とする。
			PublishedOptions.Bones[1].LocalTransform = FTransform(FVector(0, 0, 10));
		}
		if (!bGround)
		{
			PublishedOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
			{
				PhysicsNode.RootBone.BoneName = TEXT("chain_0");
				PhysicsNode.DummyBoneLength = 0.0f;
				PhysicsNode.WarmUpFrames = 0;
				PhysicsNode.Gravity = FVector::ZeroVector;
				PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
				PhysicsNode.PhysicsSettings.Radius = 1.0f;
				PhysicsNode.bUseSharedCollision = true;
				PhysicsNode.SharedCollisionGroupTag = TAG_KawaiiPhysicsCollisionSource;
			};
		}
		KawaiiPhysicsNodeFixture::FFixture PublishedFixture(PublishedOptions);
		FKawaiiPhysicsSharedCollisionData PublishedData;
		PublishedData.BoxLimits.Add(WorldBox);
		if (bGround)
		{
			FBoxLimit BuiltGroundBox;
			if (!TestTrue(TEXT("対照: 地面の規則で Box を生成"),
				KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
					FVector(0, 0, 10), FVector::UpVector, 6, BuiltGroundBox))) return false;
			if (!TestTrue(TEXT("地面 Box の寸法と回転"),
				BuiltGroundBox.Extent == FVector(6, 6, 10)
				&& BuiltGroundBox.Rotation.Equals(FQuat::Identity))) return false;
			PublishedData.BoxLimits[0] = BuiltGroundBox;
			const auto GroundEntry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
			GroundEntry->GroundSlot.Publish(PublishedData);
			PublishedFixture.Accessor.SetSimpleWorldEntry(GroundEntry);
			PublishedFixture.EvaluateFrame();
			if (!TestEqual(TEXT("対照: 地面 Box が読まれた"),
				PublishedFixture.Accessor.GetSimpleWorldGroundBoxLimits().Num(), 1)) return false;
		}
		else
		{
			const auto SharedEntry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
			SharedEntry->GetOrCreateSlot(701)->Publish(PublishedData);
			PublishedFixture.Accessor.SetSharedCollisionEntry(SharedEntry);
			PublishedFixture.EvaluateFrame();
			if (!TestEqual(TEXT("対照: Shared Box が読まれた"),
				PublishedFixture.Node.GetSharedBoxLimits().Num(), 1)) return false;
		}
		TestTrue(bGround ? TEXT("地面 Box の最浅面") : TEXT("Shared Box の最浅面"),
			PublishedFixture.Accessor.Bone(1).Location.Equals(
				bGround ? FVector(1, 2, 11) : FVector(1, 2, 6), 1e-3));
	}
	// 地面は固定厚みのため q=(1,2,9)、E=(6,6,10)、q'=(1,2,11)。
	// 親から前後の距離が共に √6 なので長さ復元は押し出し先を変えない。float を 1e-3 cm 許す。
	return true;
}

// SW-30 / SW-20 / TC-20、mutant 1016: 1 へのスナップを消すと、非軸回転で double の Box Extent・Convex 平面の W・地面 Extent が == から外れる（float の寸法は代入時の丸めで元に戻るので回帰の確認だけ）。角度は 10°〜355° を 23° 刻み（277.78° 付近は最大軸像長が全部 1 で判別できなかった）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSourceSpecExactPublishTest,
	"KawaiiPhysics.SimpleWorldSpec.Source.ExactConvexGroundAndPublish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSourceSpecExactPublishTest::RunTest(const FString& Parameters)
{
	FSphericalLimit WorldSphere;
	WorldSphere.Radius = 3.25f;
	WorldSphere.bEnable = true;
	FCapsuleLimit WorldCapsule;
	WorldCapsule.Radius = 4.25f;
	WorldCapsule.Length = 50.75f;
	WorldCapsule.bEnable = true;
	FTaperedCapsuleLimit WorldTapered;
	WorldTapered.Radius0 = 6.25f;
	WorldTapered.Radius1 = 2.75f;
	WorldTapered.Length = 45.5f;
	WorldTapered.bEnable = true;
	FBoxLimit WorldBox = MakeInteriorBox();
	WorldBox.Extent = FVector(1000.125, 0.1, 1.25);
	FKawaiiPhysicsConvexLimit WorldConvex;
	WorldConvex.Location = FVector(11, 13, 17);
	WorldConvex.LocalBounds = FBox(FVector(-2), FVector(2));
	WorldConvex.LocalPlanes = {FPlane(1, 0, 0, 2), FPlane(-1, 0, 0, 2),
		FPlane(0, 1, 0, 2), FPlane(0, -1, 0, 2), FPlane(0, 0, 1, 2), FPlane(0, 0, -1, 2)};
	WorldConvex.bEnable = true;
	FBoxLimit GroundBox;
	if (!TestTrue(TEXT("対照: 傾斜地面 Box が組めた"),
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
			FVector(101, 203, 50), FVector(1, 2, 3).GetSafeNormal(), 300, GroundBox))) return false;
	FKawaiiPhysicsSharedCollisionData WorldData;
	WorldData.SphericalLimits.Add(WorldSphere);
	WorldData.CapsuleLimits.Add(WorldCapsule);
	WorldData.TaperedCapsuleLimits.Add(WorldTapered);
	WorldData.BoxLimits.Add(WorldBox);
	WorldData.ConvexLimits.Add(WorldConvex);
	FKawaiiPhysicsSharedCollisionData GroundData;
	GroundData.BoxLimits.Add(GroundBox);
	const FVector RotationAxis = FVector(-0.503, -0.779, -0.066).GetSafeNormal();
	int32 NonExactAxisCases = 0;
	for (int32 CaseIndex = 0; CaseIndex < 16; ++CaseIndex)
	{
		const FQuat Rotation(RotationAxis, FMath::DegreesToRadians(10.0 + CaseIndex * 23.0));
		const FTransform ComponentTransform(Rotation, FVector(37, -19, 53), FVector::OneVector);
		const double MaximumImage = FMath::Max3(
			ComponentTransform.InverseTransformVector(FVector::XAxisVector).Size(),
			ComponentTransform.InverseTransformVector(FVector::YAxisVector).Size(),
			ComponentTransform.InverseTransformVector(FVector::ZAxisVector).Size());
		if (MaximumImage != 1.0) ++NonExactAxisCases;
		const auto WorldEntry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		// Publish は入力と中身を Swap するので、ケースごとにコピーを渡す（同じデータを再利用すると 2 回目から空になる）。
		FKawaiiPhysicsSharedCollisionData PublishWorldData = WorldData;
		FKawaiiPhysicsSharedCollisionData PublishGroundData = GroundData;
		WorldEntry->Slot.Publish(PublishWorldData);
		WorldEntry->GroundSlot.Publish(PublishGroundData);
		FKawaiiPhysicsTestAccessor ReaderAccessor;
		ReaderAccessor.SetComponentSpaceCollisionTransform(ComponentTransform);
		ReaderAccessor.SetSimpleWorldEntry(WorldEntry);
		FAnimInstanceProxy AnimProxy;
		FComponentSpacePoseContext PoseOutput(&AnimProxy);
		ReaderAccessor.UpdateSimpleWorldCollisionLimits(PoseOutput);
		if (!TestTrue(TEXT("対照: 全形状と地面が読まれた"),
			ReaderAccessor.GetSimpleWorldSphericalLimits().Num() == 1
			&& ReaderAccessor.GetSimpleWorldCapsuleLimits().Num() == 1
			&& ReaderAccessor.GetSimpleWorldTaperedCapsuleLimits().Num() == 1
			&& ReaderAccessor.GetSimpleWorldBoxLimits().Num() == 1
			&& ReaderAccessor.GetSimpleWorldConvexLimits().Num() == 1
			&& ReaderAccessor.GetSimpleWorldConvexLimits()[0].LocalPlanes.Num() == 6
			&& ReaderAccessor.GetSimpleWorldGroundBoxLimits().Num() == 1)) return false;
		TestTrue(TEXT("Sphere 半径は world 値と一致"),
			ReaderAccessor.GetSimpleWorldSphericalLimits()[0].Radius == WorldSphere.Radius);
		TestTrue(TEXT("Box Extent は world 値と一致"),
			ReaderAccessor.GetSimpleWorldBoxLimits()[0].Extent == WorldBox.Extent);
		TestTrue(TEXT("Capsule 寸法は world 値と一致"),
			ReaderAccessor.GetSimpleWorldCapsuleLimits()[0].Radius == WorldCapsule.Radius
			&& ReaderAccessor.GetSimpleWorldCapsuleLimits()[0].Length == WorldCapsule.Length);
		const FTaperedCapsuleLimit& SimTapered = ReaderAccessor.GetSimpleWorldTaperedCapsuleLimits()[0];
		TestTrue(TEXT("Tapered 寸法は world 値と一致"),
			SimTapered.Radius0 == WorldTapered.Radius0 && SimTapered.Radius1 == WorldTapered.Radius1
			&& SimTapered.Length == WorldTapered.Length);
		TestTrue(TEXT("Convex 平面の W は world 値と一致"),
			ReaderAccessor.GetSimpleWorldConvexLimits()[0].LocalPlanes[0].W == WorldConvex.LocalPlanes[0].W);
		TestTrue(TEXT("地面 Box Extent は world 値と一致"),
			ReaderAccessor.GetSimpleWorldGroundBoxLimits()[0].Extent == GroundBox.Extent);
		const auto SharedEntry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
		const auto SharedSlot = SharedEntry->GetOrCreateSlot(702);
		FKawaiiPhysicsTestAccessor PublisherAccessor;
		PublisherAccessor.SetComponentSpaceCollisionTransform(ComponentTransform);
		PublisherAccessor.SetSharedCollisionSourceSlot(SharedSlot);
		PublisherAccessor.Node.SphericalLimits.Add(WorldSphere);
		PublisherAccessor.Node.CapsuleLimits.Add(WorldCapsule);
		PublisherAccessor.Node.TaperedCapsuleLimits.Add(WorldTapered);
		PublisherAccessor.Node.BoxLimits.Add(WorldBox);
		PublisherAccessor.WriteSharedCollisionToSubsystem(PoseOutput, ComponentTransform);
		FKawaiiPhysicsSharedCollisionData PublishedData;
		SharedSlot->AppendTo(PublishedData);
		if (!TestTrue(TEXT("対照: Shared に四形状が publish された"),
			PublishedData.SphericalLimits.Num() == 1 && PublishedData.BoxLimits.Num() == 1
			&& PublishedData.CapsuleLimits.Num() == 1 && PublishedData.TaperedCapsuleLimits.Num() == 1)) return false;
		TestTrue(TEXT("publish Sphere / Box 寸法は一致"),
			PublishedData.SphericalLimits[0].Radius == WorldSphere.Radius
			&& PublishedData.BoxLimits[0].Extent == WorldBox.Extent);
		TestTrue(TEXT("publish Capsule / Tapered 寸法は一致"),
			PublishedData.CapsuleLimits[0].Radius == WorldCapsule.Radius
			&& PublishedData.CapsuleLimits[0].Length == WorldCapsule.Length
			&& PublishedData.TaperedCapsuleLimits[0].Radius0 == WorldTapered.Radius0
			&& PublishedData.TaperedCapsuleLimits[0].Radius1 == WorldTapered.Radius1
			&& PublishedData.TaperedCapsuleLimits[0].Length == WorldTapered.Length);
	}
	TestTrue(TEXT("対照: 丸めで最大軸像長が 1 と異なる回転がある"), NonExactAxisCases > 0);
	// ビット一致の要件なので寸法に許容差はない。
	return true;
}

// SW-123、mutant なし。Local 登録で収集設定を欠落させる壊れ方は全 Desc 比較と既定値でない入力のリテラル検査で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSourceSpecLocalProviderTest,
	"KawaiiPhysics.SimpleWorldSpec.Source.LocalProviderLegacyBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSourceSpecLocalProviderTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FScopedCVar SimpleWorldEnabled(
		TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("1"));
	TStrongObjectPtr<UKawaiiPhysicsSharedCollisionSubsystem> CollisionSubsystem(
		NewObject<UKawaiiPhysicsSharedCollisionSubsystem>());
	TStrongObjectPtr<USkeletalMeshComponent> SkeletalComponent(NewObject<USkeletalMeshComponent>());
	FKawaiiPhysicsTestAccessor LocalAccessor;
	LocalAccessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	LocalAccessor.SetSimpleWorldOwnSkelComp(SkeletalComponent.Get());
	LocalAccessor.SetSimpleWorldSubsystem(CollisionSubsystem.Get());
	LocalAccessor.SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource::Local);
	LocalAccessor.Node.bUseSimpleWorldCollision = true;
	LocalAccessor.Node.SimpleWorldCollisionGatherInterval = 0.37f;
	LocalAccessor.Node.bOverrideSimpleWorldCollisionGatherRadius = true;
	LocalAccessor.Node.SimpleWorldCollisionGatherRadius = 321.0f;
	LocalAccessor.Node.bOverrideCollisionParams = true;
	LocalAccessor.Node.CollisionChannelSettings.SetObjectType(ECC_WorldDynamic);
	LocalAccessor.Node.SimpleWorldCollisionObjectTypes = {
		UEngineTypes::ConvertToObjectType(ECC_WorldStatic), UEngineTypes::ConvertToObjectType(ECC_PhysicsBody)};
	LocalAccessor.Node.SimpleWorldCollisionConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox;
	LocalAccessor.Node.SimpleWorldCollisionSkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::PhysicsAsset;
	LocalAccessor.Node.bSimpleWorldCollisionGroundCollision = false;
	LocalAccessor.InitializeSimpleWorldCollision();
	const auto LocalKey = FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(SkeletalComponent.Get());
	const auto LocalEntry = CollisionSubsystem->FindSimpleWorldEntry(LocalKey);
	if (!TestTrue(TEXT("対照: Local key の Entry が作られた"), LocalEntry.IsValid())) return false;
	if (!TestTrue(TEXT("Local provider の Desc が登録された"), LocalEntry->HasProviderDesc())) return false;
	FKawaiiPhysicsSimpleWorldCollisionDesc RegisteredDesc;
	if (!TestTrue(TEXT("登録した Desc を読める"), LocalEntry->BuildMergedDesc(RegisteredDesc))) return false;
	const FKawaiiPhysicsSimpleWorldCollisionDesc NodeDesc = FSimpleWorldDescProbe::BuildFor(LocalAccessor.Node);
	FKawaiiPhysicsSimpleWorldCollisionDesc ExpectedMergedDesc = NodeDesc;
	// 1 provider の Merge は半径上書きの集約ビットだけ true にする。
	ExpectedMergedDesc.bGatherRadiusAllOverridden = true;
	TestTrue(TEXT("Local の登録済み Desc はノードの全設定と一致"), RegisteredDesc == ExpectedMergedDesc);
	TestTrue(TEXT("対照: 既定値でない収集設定が登録された"),
		RegisteredDesc.GatherIntervalSec == 0.37f
		&& RegisteredDesc.GatherRadiusOverride == 321.0f
		&& RegisteredDesc.bGatherRadiusAllOverridden
		&& RegisteredDesc.CollisionChannel == ECC_WorldDynamic
		&& RegisteredDesc.ObjectTypes == LocalAccessor.Node.SimpleWorldCollisionObjectTypes
		&& RegisteredDesc.ObjectTypes.Num() == 2
		&& RegisteredDesc.ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox
		&& RegisteredDesc.SkeletalMeshCollision == EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::PhysicsAsset
		&& !RegisteredDesc.bGroundCollision);
	FBoxLimit WorldBox = MakeInteriorBox();
	WorldBox.Location = FVector(12, 0, 0);
	FKawaiiPhysicsSharedCollisionData PublishedData;
	PublishedData.BoxLimits.Add(WorldBox);
	LocalEntry->Slot.Publish(PublishedData);
	FAnimInstanceProxy AnimProxy;
	FComponentSpacePoseContext PoseOutput(&AnimProxy);
	LocalAccessor.SetComponentSpaceCollisionTransform(FTransform::Identity);
	LocalAccessor.UpdateSimpleWorldCollisionLimits(PoseOutput);
	if (!TestEqual(TEXT("対照: Local provider の Box が読まれた"),
		LocalAccessor.GetSimpleWorldBoxLimits().Num(), 1)) return false;
	FKawaiiPhysicsTestAccessor LegacyAccessor;
	LegacyAccessor.SetSimpleWorldLimits({}, {}, {}, {WorldBox}, {});
	for (FKawaiiPhysicsTestAccessor* ChainAccessor : {&LocalAccessor, &LegacyAccessor})
	{
		ChainAccessor->BuildVerticalChain(2, 12.0f, FVector::ZeroVector, FVector(1, 0, 0));
		ChainAccessor->Bone(1).PhysicsSettings.Radius = 1.0f;
	}
	LocalAccessor.StepFrame(1.0f / 60.0f);
	LegacyAccessor.StepFrame(1.0f / 60.0f);
	TestTrue(TEXT("対照: Box が子を押した"),
		FVector::Dist(LocalAccessor.Bone(1).Location, FVector(12, 0, 0)) > 0.1);
	TestTrue(TEXT("Local と旧直接注入の位置はビット一致"),
		FMemory::Memcmp(&LocalAccessor.Bone(1).Location, &LegacyAccessor.Bone(1).Location, sizeof(FVector)) == 0);
	// ビット一致なので押し出し比較に許容差はない。対照の 0.1 cm は衝突の有無を分ける。
	return true;
}

} // namespace KawaiiPhysicsCollisionSourceSpecTestPrivate

#endif
