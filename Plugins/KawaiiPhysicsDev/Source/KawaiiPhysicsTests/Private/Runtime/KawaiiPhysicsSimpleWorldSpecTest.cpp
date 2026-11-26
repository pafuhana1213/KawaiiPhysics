// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsSimpleWorldSpecTestPrivate
{
	TArray<FPlane> MakeCubePlanes()
	{
		return {
			FPlane(1, 0, 0, 1), FPlane(-1, 0, 0, 1),
			FPlane(0, 1, 0, 1), FPlane(0, -1, 0, 1),
			FPlane(0, 0, 1, 1), FPlane(0, 0, -1, 1)};
	}

	TArray<FVector> MakeCubeVertices()
	{
		TArray<FVector> Vertices;
		for (const double X : {-1.0, 1.0})
		{
			for (const double Y : {-1.0, 1.0})
			{
				for (const double Z : {-1.0, 1.0})
				{
					Vertices.Add(FVector(X, Y, Z));
				}
			}
		}
		return Vertices;
	}

// SW-42 / SW-11、mutant 1027: ゼロ法線を上向きに戻さないと中心 Z が 40 でなく 50 になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecZeroNormalTest,
	"KawaiiPhysics.SimpleWorldSpec.ZeroNormalGroundBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecZeroNormalTest::RunTest(const FString& Parameters)
{
	FBoxLimit ZeroNormalBox;
	FBoxLimit UpNormalBox;
	const FVector ImpactPoint(100, 200, 50);
	const bool bZeroBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(ImpactPoint, FVector::ZeroVector, 300, ZeroNormalBox);
	const bool bUpBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(ImpactPoint, FVector::UpVector, 300, UpNormalBox);
	if (!TestTrue(TEXT("対照: 両方の地面 Box が生成された"), bZeroBuilt && bUpBuilt)) return false;
	// 厚さの半分 10 cm を上向き法線に沿って引く。整数入力なので許容差は不要。
	TestTrue(TEXT("ゼロ法線の中心は上向きと同じ"), ZeroNormalBox.Location == FVector(100, 200, 40));
	TestTrue(TEXT("ゼロ法線の寸法"), ZeroNormalBox.Extent == FVector(300, 300, 10));
	TestTrue(TEXT("上向き法線と同じ回転"), ZeroNormalBox.Rotation.Equals(UpNormalBox.Rotation));
	TestTrue(TEXT("上向き法線と同じ中心"), ZeroNormalBox.Location == UpNormalBox.Location);
	return true;
}

// SW-50 / SW-49、mutant 1028: 回転の合成順を逆にすると、往復後の傾斜が一致しない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecTiltedGroundFollowTest,
	"KawaiiPhysics.SimpleWorldSpec.TiltedGroundFollowsComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecTiltedGroundFollowTest::RunTest(const FString& Parameters)
{
	FBoxLimit WorldBox;
	const FVector TiltedNormal = FQuat(FVector::YAxisVector, PI / 6).RotateVector(FVector::UpVector);
	if (!TestTrue(TEXT("対照: 傾斜した地面 Box が生成された"),
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(FVector(10, 20, 50), TiltedNormal, 300, WorldBox))) return false;
	const FTransform ComponentTransform(FQuat(FVector::ZAxisVector, PI / 2), FVector(100, 50, 60));
	const FBoxLimit LocalBox = KawaiiPhysicsSimpleWorldCollision::MakeSimpleWorldGroundBoxLocal(WorldBox, ComponentTransform);
	const FBoxLimit RoundTripBox = KawaiiPhysicsSimpleWorldCollision::TransformSimpleWorldGroundBox(LocalBox, ComponentTransform);
	TestTrue(TEXT("対照: 地面は水平でない"), !WorldBox.Rotation.Equals(FQuat::Identity, 1e-3));
	// 四元数の合成と正規化の丸めを 1e-4 許す。
	TestTrue(TEXT("往復後の地面回転"), RoundTripBox.Rotation.Equals(WorldBox.Rotation, 1e-4));
	TestTrue(TEXT("往復後の地面中心"), RoundTripBox.Location.Equals(WorldBox.Location, 1e-4));
	const FQuat AdditionalRotation(FVector::XAxisVector, PI / 5);
	const FTransform MovedComponent(AdditionalRotation * ComponentTransform.GetRotation(), FVector(100, 50, 60));
	const FBoxLimit MovedBox = KawaiiPhysicsSimpleWorldCollision::TransformSimpleWorldGroundBox(LocalBox, MovedComponent);
	TestTrue(TEXT("Component の追加回転だけ床も回る"),
		MovedBox.Rotation.Equals(AdditionalRotation * WorldBox.Rotation, 1e-4));
	return true;
}

// SW-74、mutant 1032: 6 面で Max=6 を拒むと Convex 1 個にならない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecConvexMaxTest,
	"KawaiiPhysics.SimpleWorldSpec.ConvexExactPlaneLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecConvexMaxTest::RunTest(const FString& Parameters)
{
	const TArray<FPlane> Planes = MakeCubePlanes();
	const TArray<FVector> Vertices = MakeCubeVertices();
	const TArray<int32> Indices;
	FKawaiiPhysicsSharedCollisionData AtMaximum;
	const bool bAccepted = KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
		MakeArrayView(Planes), MakeArrayView(Vertices), MakeArrayView(Indices),
		FTransform::Identity, FVector::OneVector, 6, false, AtMaximum);
	TestTrue(TEXT("6 面ちょうどは受理"), bAccepted);
	TestEqual(TEXT("Convex は 1 個"), AtMaximum.ConvexLimits.Num(), 1);
	TestEqual(TEXT("Box への代用はない"), AtMaximum.BoxLimits.Num(), 0);
	FKawaiiPhysicsSharedCollisionData BelowMaximum;
	TestFalse(TEXT("対照: 上限 5 なら拒否"),
		KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			MakeArrayView(Planes), MakeArrayView(Vertices), MakeArrayView(Indices),
			FTransform::Identity, FVector::OneVector, 5, false, BelowMaximum));
	return true;
}

// SW-21 / TC-20 / SW-U9、mutant 1030: Tapered にスケールを掛けないと中心・半径・長さが Engine と一致しない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecTaperedEngineScaleTest,
	"KawaiiPhysics.SimpleWorldSpec.TaperedEngineScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecTaperedEngineScaleTest::RunTest(const FString& Parameters)
{
	FKTaperedCapsuleElem Elem;
	Elem.Center = FVector(10, 11, 12);
	Elem.Rotation = FRotator(10, 20, 30);
	Elem.Radius0 = 3;
	Elem.Radius1 = 1.5;
	Elem.Length = 6;
	FKAggregateGeom Aggregate;
	Aggregate.TaperedCapsuleElems.Add(Elem);
	for (const FVector Scale : {FVector(2, 2, 2), FVector(2, 1, 0.5)})
	{
		FKawaiiPhysicsSharedCollisionData Converted;
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(Aggregate, Scale,
			EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, 64, false, Converted);
		if (!TestEqual(TEXT("対照: Tapered が 1 個変換された"), Converted.TaperedCapsuleLimits.Num(), 1)) return false;
		const FKTaperedCapsuleElem EngineScaled = Elem.GetFinalScaled(Scale, FTransform::Identity);
		const FTaperedCapsuleLimit& Limit = Converted.TaperedCapsuleLimits[0];
		// Engine の公開契約と比較する。float の回転変換の丸めを 1e-3 cm 許す。
		TestTrue(TEXT("中心は Engine と一致"), Limit.Location.Equals(EngineScaled.Center, 1e-3));
		TestTrue(TEXT("回転は Engine と一致"), Limit.Rotation.Equals(EngineScaled.Rotation.Quaternion(), 1e-3));
		TestTrue(TEXT("両端半径と長さは Engine と一致"),
			FMath::IsNearlyEqual(Limit.Radius0, EngineScaled.Radius0, 1e-3)
			&& FMath::IsNearlyEqual(Limit.Radius1, EngineScaled.Radius1, 1e-3)
			&& FMath::IsNearlyEqual(Limit.Length, EngineScaled.Length, 1e-3));
		if (Scale == FVector(2, 2, 2))
		{
			// 一様 2 倍では中心・半径・長さもそれぞれ 2 倍。
			TestTrue(TEXT("一様スケールの導出値"),
				Limit.Location.Equals(FVector(20, 22, 24), 1e-3)
				&& FMath::IsNearlyEqual(Limit.Radius0, 6.0f, 1e-3)
				&& FMath::IsNearlyEqual(Limit.Radius1, 3.0f, 1e-3)
				&& FMath::IsNearlyEqual(Limit.Length, 12.0f, 1e-3));
		}
	}
	return true;
}

// SW-38、mutant 1018: 全列が 1e-5 の写像で下限を消すと半径が 1e-5 となり 1e-4 と違う。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecCollapsedScaleTest,
	"KawaiiPhysics.SimpleWorldSpec.CollapsedMappingFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecCollapsedScaleTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetComponentSpaceCollisionTransform(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(100000)));
	FSphericalLimit WorldSphere;
	WorldSphere.Radius = 1.0f;
	WorldSphere.bEnable = true;
	FKawaiiPhysicsSharedCollisionData WorldData;
	WorldData.SphericalLimits.Add(WorldSphere);
	const auto Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	Entry->Slot.Publish(WorldData);
	Accessor.SetSimpleWorldEntry(Entry);
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	Accessor.UpdateSimpleWorldCollisionLimits(Output);
	if (!TestEqual(TEXT("対照: 球が写像された"), Accessor.GetSimpleWorldSphericalLimits().Num(), 1)) return false;
	// world→simulation は 1/100000=1e-5。仕様の下限 KSN=1e-4 を掛ける。
	TestTrue(TEXT("潰れた写像でも半径に下限がある"),
		FMath::IsNearlyEqual(Accessor.GetSimpleWorldSphericalLimits()[0].Radius, 1e-4f, 1e-8f));
	// 係数計算と float への代入の丸めを 1e-8 cm 許す。
	return true;
}

// SW-30 / TC-20、mutant 1016: 純回転の最大軸像長が 1 から丸めで外れるケースで 1 へのスナップを消すと、double の Box Extent の == が崩れる（float の Capsule / Tapered の寸法は float へ代入した時点で丸めが消えるので、それらは回帰の確認だけ）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecRotatedExactDimensionsTest,
	"KawaiiPhysics.SimpleWorldSpec.RotatedExactDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecRotatedExactDimensionsTest::RunTest(const FString& Parameters)
{
	FBoxLimit WorldBox;
	WorldBox.Location = FVector(10, 20, 30);
	WorldBox.Extent = FVector(1000.125, 0.1, 1.25);
	WorldBox.bEnable = true;
	FCapsuleLimit WorldCapsule;
	WorldCapsule.Length = 50.75f;
	WorldCapsule.Radius = 4.25f;
	WorldCapsule.bEnable = true;
	FTaperedCapsuleLimit WorldTapered;
	WorldTapered.Radius0 = 6.25f;
	WorldTapered.Radius1 = 2.75f;
	WorldTapered.Length = 45.5f;
	WorldTapered.bEnable = true;
	FKawaiiPhysicsSharedCollisionData WorldData;
	WorldData.BoxLimits.Add(WorldBox);
	WorldData.CapsuleLimits.Add(WorldCapsule);
	WorldData.TaperedCapsuleLimits.Add(WorldTapered);
	int32 NonExactAxisCases = 0;
	const FVector RotationAxis = FVector(-0.503, -0.779, -0.066).GetSafeNormal();
	for (int32 CaseIndex = 0; CaseIndex < 16; ++CaseIndex)
	{
		// 10°〜355° を 23° 刻み。277.78° 付近の帯は逆変換の最大軸像長が 16 通りすべてちょうど 1 になり 1016 を判別できなかった。
		const double Angle = 10.0 + CaseIndex * 23.0;
		const FQuat Rotation(RotationAxis, FMath::DegreesToRadians(Angle));
		const FTransform ComponentTransform(Rotation, FVector(37, -19, 53), FVector::OneVector);
		const double ImageX = ComponentTransform.InverseTransformVector(FVector::XAxisVector).Size();
		const double ImageY = ComponentTransform.InverseTransformVector(FVector::YAxisVector).Size();
		const double ImageZ = ComponentTransform.InverseTransformVector(FVector::ZAxisVector).Size();
		const double MaximumImage = FMath::Max3(ImageX, ImageY, ImageZ);
		if (MaximumImage != 1.0) ++NonExactAxisCases;
		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetComponentSpaceCollisionTransform(ComponentTransform);
		const auto Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		// Publish は入力と中身を Swap するので、ケースごとにコピーを渡す（同じ WorldData を再利用すると 2 回目から空になる）。
		FKawaiiPhysicsSharedCollisionData PublishData = WorldData;
		Entry->Slot.Publish(PublishData);
		Accessor.SetSimpleWorldEntry(Entry);
		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);
		Accessor.UpdateSimpleWorldCollisionLimits(Output);
		if (!TestEqual(TEXT("対照: Box が読まれた"), Accessor.GetSimpleWorldBoxLimits().Num(), 1)
			|| !TestEqual(TEXT("対照: Capsule が読まれた"), Accessor.GetSimpleWorldCapsuleLimits().Num(), 1)
			|| !TestEqual(TEXT("対照: Tapered が読まれた"), Accessor.GetSimpleWorldTaperedCapsuleLimits().Num(), 1)) return false;
		TestTrue(FString::Printf(TEXT("回転 %d の Box Extent はビット一致"), CaseIndex),
			Accessor.GetSimpleWorldBoxLimits()[0].Extent == WorldBox.Extent);
		TestTrue(FString::Printf(TEXT("回転 %d の Capsule Length はビット一致"), CaseIndex),
			Accessor.GetSimpleWorldCapsuleLimits()[0].Length == WorldCapsule.Length);
		const FTaperedCapsuleLimit& SimTapered = Accessor.GetSimpleWorldTaperedCapsuleLimits()[0];
		TestTrue(FString::Printf(TEXT("回転 %d の Tapered 寸法はビット一致"), CaseIndex),
			SimTapered.Radius0 == WorldTapered.Radius0
			&& SimTapered.Radius1 == WorldTapered.Radius1 && SimTapered.Length == WorldTapered.Length);
	}
	TestTrue(TEXT("対照: 少なくとも一つの回転で最大軸像長が 1 と異なる"), NonExactAxisCases > 0);
	return true;
}

// SW-01 / SW-02、mutant なし。ソルバー側の無効ゲートを外すと、注入した球で OFF / CVar=0 の位置が空セルからずれる。
// 旧テストは Entry の直注入で登録ゲートの欠落を見逃した。Local 登録が OFF / CVar=0 で始まる壊れ方は Registry の不在検査で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecGatesTest,
	"KawaiiPhysics.SimpleWorldSpec.DisabledGatesBitExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecGatesTest::RunTest(const FString& Parameters)
{
	auto MakeOptions = []()
	{
		KawaiiPhysicsNodeFixture::FOptions GateOptions;
		GateOptions.Bones = {
			{TEXT("root"), INDEX_NONE, FTransform::Identity},
			{TEXT("chain_0"), 0, FTransform::Identity},
			{TEXT("chain_1"), 1, FTransform(FVector(12, 0, 0))},
			{TEXT("chain_2"), 2, FTransform(FVector(6, 0, 0))}};
		GateOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("chain_0");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.PhysicsSettings.Radius = 0.0f;
			PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
			PhysicsNode.bUseSimpleWorldCollision = false;
		};
		return GateOptions;
	};
	FKawaiiPhysicsSharedCollisionData WorldData;
	FSphericalLimit Sphere;
	Sphere.Location = FVector(12, -2, 0);
	Sphere.Radius = 3.0f;
	Sphere.bEnable = true;
	WorldData.SphericalLimits.Add(Sphere);
	FCapsuleLimit Capsule;
	Capsule.Location = FVector(1000, 0, 0);
	Capsule.Radius = 2.0f;
	Capsule.Length = 10.0f;
	Capsule.bEnable = true;
	WorldData.CapsuleLimits.Add(Capsule);
	FTaperedCapsuleLimit Tapered;
	Tapered.Location = FVector(1000, 0, 0);
	Tapered.Radius0 = 3.0f;
	Tapered.Radius1 = 1.0f;
	Tapered.Length = 10.0f;
	Tapered.bEnable = true;
	WorldData.TaperedCapsuleLimits.Add(Tapered);
	FBoxLimit Box;
	Box.Location = FVector(1000, 0, 0);
	Box.Extent = FVector(5, 5, 5);
	Box.bEnable = true;
	WorldData.BoxLimits.Add(Box);
	FKawaiiPhysicsConvexLimit Convex;
	Convex.Location = FVector(1000, 0, 0);
	Convex.LocalBounds = FBox(FVector(-1), FVector(1));
	Convex.LocalPlanes = MakeCubePlanes();
	Convex.bEnable = true;
	WorldData.ConvexLimits.Add(Convex);
	const auto Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	Entry->Slot.Publish(WorldData);
	FKawaiiPhysicsSharedCollisionData GroundData;
	GroundData.BoxLimits.Add(Box);
	Entry->GroundSlot.Publish(GroundData);
	auto HasSixRuntimeLimits = [](const FKawaiiPhysicsTestAccessor& GateAccessor)
	{
		return !GateAccessor.GetSimpleWorldSphericalLimits().IsEmpty()
			&& !GateAccessor.GetSimpleWorldCapsuleLimits().IsEmpty()
			&& !GateAccessor.GetSimpleWorldTaperedCapsuleLimits().IsEmpty()
			&& !GateAccessor.GetSimpleWorldBoxLimits().IsEmpty()
			&& !GateAccessor.GetSimpleWorldGroundBoxLimits().IsEmpty()
			&& !GateAccessor.GetSimpleWorldConvexLimits().IsEmpty();
	};
	bool bEnabledMoved = false;
	{
		KawaiiPhysicsNodeFixture::FScopedCVar SimpleWorldEnabled(
			TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("1"));
		KawaiiPhysicsNodeFixture::FFixture Disabled(MakeOptions());
		KawaiiPhysicsNodeFixture::FFixture Empty(MakeOptions());
		KawaiiPhysicsNodeFixture::FFixture Enabled(MakeOptions());
		Disabled.Accessor.SetSimpleWorldEntry(Entry);
		Disabled.Accessor.SetSimpleWorldLimits({Sphere}, {Capsule}, {Tapered}, {Box}, {Convex}, {Box});
		Disabled.Node.bUseSimpleWorldCollision = false;
		Enabled.Accessor.SetSimpleWorldEntry(Entry);
		Enabled.Accessor.SetSimpleWorldLimits({Sphere}, {Capsule}, {Tapered}, {Box}, {Convex}, {Box});
		if (!TestTrue(TEXT("対照: OFF と ON の評価前に 6 配列が埋まる"),
			HasSixRuntimeLimits(Disabled.Accessor) && HasSixRuntimeLimits(Enabled.Accessor))) return false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Disabled.EvaluateFrame();
			Empty.EvaluateFrame();
			Enabled.EvaluateFrame();
			if (!TestTrue(TEXT("対照: ModifyBones が作られた"), Disabled.Accessor.Num() > 1)) return false;
			if (!TestEqual(TEXT("比較セルのボーン数"), Disabled.Accessor.Num(), Empty.Accessor.Num())) return false;
			for (int32 BoneIndex = 0; BoneIndex < Disabled.Accessor.Num(); ++BoneIndex)
			{
				const FVector& OffLocation = Disabled.Accessor.Bone(BoneIndex).Location;
				const FVector& EmptyLocation = Empty.Accessor.Bone(BoneIndex).Location;
				TestTrue(FString::Printf(TEXT("OFF と空セルはビット一致 %d/%d"), Frame, BoneIndex),
					FMemory::Memcmp(&OffLocation, &EmptyLocation, sizeof(FVector)) == 0);
				if (BoneIndex < Enabled.Accessor.Num()
					&& FVector::Dist(Enabled.Accessor.Bone(BoneIndex).Location, EmptyLocation) > 0.1)
				{
					bEnabledMoved = true;
				}
			}
			TestTrue(TEXT("OFF では 6 配列が空"),
				Disabled.Accessor.GetSimpleWorldSphericalLimits().IsEmpty()
				&& Disabled.Accessor.GetSimpleWorldCapsuleLimits().IsEmpty()
				&& Disabled.Accessor.GetSimpleWorldTaperedCapsuleLimits().IsEmpty()
				&& Disabled.Accessor.GetSimpleWorldBoxLimits().IsEmpty()
				&& Disabled.Accessor.GetSimpleWorldGroundBoxLimits().IsEmpty()
				&& Disabled.Accessor.GetSimpleWorldConvexLimits().IsEmpty());
		}
	}
	TestTrue(TEXT("対照: Enable=1 で球がチェーンを押した"), bEnabledMoved);
	{
		KawaiiPhysicsNodeFixture::FScopedCVar SimpleWorldDisabled(
			TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("0"));
		KawaiiPhysicsNodeFixture::FFixture Gated(MakeOptions());
		KawaiiPhysicsNodeFixture::FFixture Empty(MakeOptions());
		Gated.Accessor.SetSimpleWorldEntry(Entry);
		Gated.Accessor.SetSimpleWorldLimits({Sphere}, {Capsule}, {Tapered}, {Box}, {Convex}, {Box});
		if (!TestTrue(TEXT("対照: CVar=0 の評価前に 6 配列が埋まる"), HasSixRuntimeLimits(Gated.Accessor))) return false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Gated.EvaluateFrame();
			Empty.EvaluateFrame();
			if (!TestEqual(TEXT("CVar 比較セルのボーン数"), Gated.Accessor.Num(), Empty.Accessor.Num())) return false;
			for (int32 BoneIndex = 0; BoneIndex < Gated.Accessor.Num(); ++BoneIndex)
			{
				const FVector& GatedLocation = Gated.Accessor.Bone(BoneIndex).Location;
				const FVector& EmptyLocation = Empty.Accessor.Bone(BoneIndex).Location;
				TestTrue(FString::Printf(TEXT("CVar=0 と空セルはビット一致 %d/%d"), Frame, BoneIndex),
					FMemory::Memcmp(&GatedLocation, &EmptyLocation, sizeof(FVector)) == 0);
			}
			TestTrue(TEXT("CVar=0 では 6 配列が空"),
				Gated.Accessor.GetSimpleWorldSphericalLimits().IsEmpty()
				&& Gated.Accessor.GetSimpleWorldCapsuleLimits().IsEmpty()
				&& Gated.Accessor.GetSimpleWorldTaperedCapsuleLimits().IsEmpty()
				&& Gated.Accessor.GetSimpleWorldBoxLimits().IsEmpty()
				&& Gated.Accessor.GetSimpleWorldGroundBoxLimits().IsEmpty()
				&& Gated.Accessor.GetSimpleWorldConvexLimits().IsEmpty());
		}
	}
	// 各セルで別の Subsystem と Component を使い、Evaluate から本番の Local 初期化を通す。
	auto CheckLocalRegistry = [this, &MakeOptions](bool bNodeEnabled, const TCHAR* CVarValue, bool bExpectProvider)
	{
		KawaiiPhysicsNodeFixture::FScopedCVar ScopedSimpleWorldEnable(
			TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), CVarValue);
		TStrongObjectPtr<UKawaiiPhysicsSharedCollisionSubsystem> LocalSubsystem(
			NewObject<UKawaiiPhysicsSharedCollisionSubsystem>());
		KawaiiPhysicsNodeFixture::FOptions LocalOptions = MakeOptions();
		LocalOptions.ConfigureNode = [bNodeEnabled](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("chain_0");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.PhysicsSettings.Radius = 0.0f;
			PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
			PhysicsNode.bUseSimpleWorldCollision = bNodeEnabled;
			PhysicsNode.SimpleWorldCollisionSource = EKawaiiPhysicsSimpleWorldCollisionSource::Local;
		};
		KawaiiPhysicsNodeFixture::FFixture LocalFixture(LocalOptions);
		LocalFixture.Accessor.SetSimpleWorldOwnSkelComp(LocalFixture.Component);
		LocalFixture.Accessor.SetSimpleWorldSubsystem(LocalSubsystem.Get());
		const auto LocalKey = FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(LocalFixture.Component);
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			LocalFixture.EvaluateFrame();
			if (!TestTrue(TEXT("対照: Local セルの ModifyBones が作られた"),
				LocalFixture.Accessor.Num() > 1)) return false;
		}
		const auto LocalEntry = LocalSubsystem->FindSimpleWorldEntry(LocalKey);
		if (bExpectProvider)
		{
			if (!TestTrue(TEXT("対照: ON かつ CVar=1 で Local key が登録された"), LocalEntry.IsValid())) return false;
			return TestTrue(TEXT("対照: Local provider の Desc が登録された"), LocalEntry->HasProviderDesc());
		}
		return TestFalse(FString::Printf(TEXT("OFF または CVar=%s では Local key が無い"), CVarValue),
			LocalEntry.IsValid());
	};
	if (!CheckLocalRegistry(true, TEXT("1"), true)) return false;
	if (!CheckLocalRegistry(false, TEXT("1"), false)) return false;
	if (!CheckLocalRegistry(true, TEXT("0"), false)) return false;
	// Evaluate は無効時に配列を消すため、ソルバー側のゲートは配列を残した直接ステップでも検査する。
	{
		KawaiiPhysicsNodeFixture::FScopedCVar SimpleWorldEnabled(
			TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("1"));
		FKawaiiPhysicsTestAccessor SolverOffAccessor;
		FKawaiiPhysicsTestAccessor SolverEmptyAccessor;
		FKawaiiPhysicsTestAccessor SolverOnAccessor;
		for (FKawaiiPhysicsTestAccessor* ChainAccessor : {&SolverOffAccessor, &SolverEmptyAccessor, &SolverOnAccessor})
		{
			ChainAccessor->BuildVerticalChain(3, 12.0f, FVector::ZeroVector, FVector(1, 0, 0));
			ChainAccessor->SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
			ChainAccessor->Bone(1).PhysicsSettings.Radius = 0.0f;
		}
		SolverOffAccessor.SetSimpleWorldLimits({Sphere}, {Capsule}, {Tapered}, {Box}, {Convex}, {Box});
		SolverOffAccessor.Node.bUseSimpleWorldCollision = false;
		SolverOnAccessor.SetSimpleWorldLimits({Sphere}, {Capsule}, {Tapered}, {Box}, {Convex}, {Box});
		if (!TestTrue(TEXT("対照: 直接ステップ前の 6 配列が埋まる"),
			HasSixRuntimeLimits(SolverOffAccessor) && HasSixRuntimeLimits(SolverOnAccessor))) return false;
		bool bSolverOnMoved = false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			SolverOffAccessor.StepFrame(1.0f / 60.0f);
			SolverEmptyAccessor.StepFrame(1.0f / 60.0f);
			SolverOnAccessor.StepFrame(1.0f / 60.0f);
			for (int32 BoneIndex = 0; BoneIndex < SolverOffAccessor.Num(); ++BoneIndex)
			{
				const FVector& OffLocation = SolverOffAccessor.Bone(BoneIndex).Location;
				const FVector& EmptyLocation = SolverEmptyAccessor.Bone(BoneIndex).Location;
				TestTrue(FString::Printf(TEXT("直接ステップの OFF と空セルはビット一致 %d/%d"), Frame, BoneIndex),
					FMemory::Memcmp(&OffLocation, &EmptyLocation, sizeof(FVector)) == 0);
				bSolverOnMoved |= FVector::Dist(SolverOnAccessor.Bone(BoneIndex).Location, EmptyLocation) > 0.1;
			}
		}
		TestTrue(TEXT("対照: 直接ステップの ON は球で動く"), bSolverOnMoved);
	}
	{
		KawaiiPhysicsNodeFixture::FScopedCVar SimpleWorldDisabled(
			TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("0"));
		FKawaiiPhysicsTestAccessor SolverGatedAccessor;
		FKawaiiPhysicsTestAccessor SolverEmptyAccessor;
		for (FKawaiiPhysicsTestAccessor* ChainAccessor : {&SolverGatedAccessor, &SolverEmptyAccessor})
		{
			ChainAccessor->BuildVerticalChain(3, 12.0f, FVector::ZeroVector, FVector(1, 0, 0));
			ChainAccessor->SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
			ChainAccessor->Bone(1).PhysicsSettings.Radius = 0.0f;
		}
		SolverGatedAccessor.SetSimpleWorldLimits({Sphere}, {Capsule}, {Tapered}, {Box}, {Convex}, {Box});
		if (!TestTrue(TEXT("対照: CVar=0 の直接ステップ前に 6 配列が埋まる"),
			HasSixRuntimeLimits(SolverGatedAccessor))) return false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			SolverGatedAccessor.StepFrame(1.0f / 60.0f);
			SolverEmptyAccessor.StepFrame(1.0f / 60.0f);
			for (int32 BoneIndex = 0; BoneIndex < SolverGatedAccessor.Num(); ++BoneIndex)
			{
				const FVector& GatedLocation = SolverGatedAccessor.Bone(BoneIndex).Location;
				const FVector& EmptyLocation = SolverEmptyAccessor.Bone(BoneIndex).Location;
				TestTrue(FString::Printf(TEXT("直接ステップの CVar=0 と空セルはビット一致 %d/%d"), Frame, BoneIndex),
					FMemory::Memcmp(&GatedLocation, &EmptyLocation, sizeof(FVector)) == 0);
			}
		}
	}
	// ビット一致に許容差はない。対照の 0.1 cm はコリジョンの実効性の閾値。
	return true;
}

// SW-37、mutant なし。Source の Capsule 断面と Box 頂点の像は publish→read 後の形状内に残る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSpecSharedContainmentTest,
	"KawaiiPhysics.SimpleWorldSpec.SharedRoundTripContainment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSpecSharedContainmentTest::RunTest(const FString& Parameters)
{
	FCapsuleLimit SourceCapsule;
	SourceCapsule.Location = FVector(10, 20, 30);
	SourceCapsule.Rotation = FRotator(10, 20, 30).Quaternion();
	SourceCapsule.Radius = 2.0f;
	SourceCapsule.Length = 12.0f;
	SourceCapsule.bEnable = true;
	FBoxLimit SourceBox;
	SourceBox.Location = FVector(-10, 4, 7);
	SourceBox.Rotation = FRotator(20, -10, 35).Quaternion();
	SourceBox.Extent = FVector(3, 4, 5);
	SourceBox.bEnable = true;
	for (const bool bUniformSource : {false, true})
	{
		const FVector SourceScale = bUniformSource ? FVector(2) : FVector(2, 1, 1);
		const FTransform SourceTransform(FQuat(FVector::ZAxisVector, PI / 6), FVector(30, -20, 10), SourceScale);
		const FTransform TargetTransform(FQuat(FVector::XAxisVector, PI / 9), FVector(-15, 40, 5), FVector(0.5));
		const auto Entry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
		const auto Slot = Entry->GetOrCreateSlot(bUniformSource ? 772 : 771);
		FKawaiiPhysicsTestAccessor SourceAccessor;
		SourceAccessor.SetComponentSpaceCollisionTransform(SourceTransform);
		SourceAccessor.SetSharedCollisionSourceSlot(Slot);
		SourceAccessor.Node.CapsuleLimits.Add(SourceCapsule);
		SourceAccessor.Node.BoxLimits.Add(SourceBox);
		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);
		SourceAccessor.WriteSharedCollisionToSubsystem(Output, SourceTransform);
		FKawaiiPhysicsSharedCollisionData Published;
		Slot->AppendTo(Published);
		if (!TestEqual(TEXT("対照: Capsule が publish された"), Published.CapsuleLimits.Num(), 1)
			|| !TestEqual(TEXT("対照: Box が publish された"), Published.BoxLimits.Num(), 1)) return false;
		FKawaiiPhysicsTestAccessor TargetAccessor;
		TargetAccessor.SetComponentSpaceCollisionTransform(TargetTransform);
		TargetAccessor.SetSharedCollisionEntry(Entry);
		TargetAccessor.UpdateSharedCollisionLimits(Output);
		if (!TestEqual(TEXT("対照: Capsule が read された"), TargetAccessor.Node.GetSharedCapsuleLimits().Num(), 1)
			|| !TestEqual(TEXT("対照: Box が read された"), TargetAccessor.Node.GetSharedBoxLimits().Num(), 1)) return false;
		const FCapsuleLimit& TargetCapsule = TargetAccessor.Node.GetSharedCapsuleLimits()[0];
		const FBoxLimit& TargetBox = TargetAccessor.Node.GetSharedBoxLimits()[0];
		const FVector SourceAxis = SourceCapsule.Rotation.GetAxisZ();
		const FVector TargetStart = TargetCapsule.Location + TargetCapsule.Rotation.GetAxisZ() * (TargetCapsule.Length * 0.5);
		const FVector TargetEnd = TargetCapsule.Location - TargetCapsule.Rotation.GetAxisZ() * (TargetCapsule.Length * 0.5);
		for (int32 Section = 0; Section <= 8; ++Section)
		{
			const double Alpha = static_cast<double>(Section) / 8.0;
			const FVector Center = SourceCapsule.Location + SourceAxis * (SourceCapsule.Length * (Alpha - 0.5));
			for (int32 Direction = 0; Direction < 16; ++Direction)
			{
				const double Angle = 2.0 * PI * Direction / 16.0;
				const FVector Radial = SourceCapsule.Rotation.RotateVector(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0));
				const FVector SourcePoint = Center + Radial * SourceCapsule.Radius;
				const FVector TargetPoint = TargetTransform.InverseTransformPosition(SourceTransform.TransformPosition(SourcePoint));
				const FVector ClosestAxisPoint = FMath::ClosestPointOnSegment(TargetPoint, TargetStart, TargetEnd);
				TestTrue(FString::Printf(TEXT("Capsule 断面 %d/%d は内側"), Section, Direction),
					FVector::Dist(TargetPoint, ClosestAxisPoint) <= TargetCapsule.Radius + 1e-3);
			}
		}
		for (const double X : {-1.0, 0.0, 1.0})
		{
			for (const double Y : {-1.0, 0.0, 1.0})
			{
				for (const double Z : {-1.0, 0.0, 1.0})
				{
					const int32 NonzeroAxes = static_cast<int32>(X != 0) + static_cast<int32>(Y != 0) + static_cast<int32>(Z != 0);
					if (NonzeroAxes != 3 && NonzeroAxes != 1) continue;
					const FVector LocalPoint(X * SourceBox.Extent.X, Y * SourceBox.Extent.Y, Z * SourceBox.Extent.Z);
					const FVector SourcePoint = SourceBox.Location + SourceBox.Rotation.RotateVector(LocalPoint);
					const FVector TargetPoint = TargetTransform.InverseTransformPosition(SourceTransform.TransformPosition(SourcePoint));
					const FVector Relative = TargetBox.Rotation.UnrotateVector(TargetPoint - TargetBox.Location).GetAbs();
					TestTrue(TEXT("Box 頂点または面中心は内側"),
						Relative.X <= TargetBox.Extent.X + 1e-3
						&& Relative.Y <= TargetBox.Extent.Y + 1e-3
						&& Relative.Z <= TargetBox.Extent.Z + 1e-3);
				}
			}
		}
		if (bUniformSource)
		{
			// Source 2 倍を Target 0.5 倍で読むので倍率は 2/0.5=4。
			TestTrue(TEXT("一様写像の Capsule 寸法は 4 倍"),
				FMath::IsNearlyEqual(TargetCapsule.Radius, 8.0f, 1e-4)
				&& FMath::IsNearlyEqual(TargetCapsule.Length, 48.0f, 1e-4));
			TestTrue(TEXT("一様写像の Box Extent は 4 倍"), TargetBox.Extent.Equals(FVector(12, 16, 20), 1e-4));
		}
	}
	// float の二回の回転・スケール・包含判定を 1e-3 cm 許す。一様寸法は 1e-4 cm。
	return true;
}

} // namespace KawaiiPhysicsSimpleWorldSpecTestPrivate

#endif
