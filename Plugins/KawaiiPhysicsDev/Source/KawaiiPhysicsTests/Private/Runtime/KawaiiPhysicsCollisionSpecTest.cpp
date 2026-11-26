// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"

namespace KawaiiPhysicsCollisionSpecTestPrivate
{
	FKawaiiPhysicsModifyBone MakeCollisionBone(const FVector& Start, const float Radius)
	{
		FKawaiiPhysicsModifyBone CollisionBone;
		CollisionBone.Location = Start;
		CollisionBone.PrevLocation = Start;
		CollisionBone.PoseLocation = Start;
		CollisionBone.PhysicsSettings.Radius = Radius;
		return CollisionBone;
	}

	int32 BuildFeedbackSegment(FKawaiiPhysicsTestAccessor& Accessor, const bool bChildFixed = false)
	{
		const int32 ChildIndex = Accessor.BuildSyncBoneSubdivisionLengthFixture(1, 12.0f, 6.0f);
		Accessor.Node.bBoneSubdivisionCollisionOnly = true;
		// 実親を根元から独立させる。実親 (0,0,0) と子 (12,0,0) の区間長と α=0.5 は維持する。
		FKawaiiPhysicsModifyBone AnchorBone;
		const int32 AnchorIndex = Accessor.Node.ModifyBones.Num();
		AnchorBone.Index = AnchorIndex;
		AnchorBone.ParentIndex = INDEX_NONE;
		AnchorBone.Location = FVector(-12, 0, 0);
		AnchorBone.PrevLocation = AnchorBone.Location;
		AnchorBone.PoseLocation = AnchorBone.Location;
		AnchorBone.bSkipSimulate = true;
		AnchorBone.ChildIndices.Add(0);
		Accessor.Node.ModifyBones.Add(AnchorBone);
		Accessor.Bone(0).ParentIndex = AnchorIndex;
		Accessor.Bone(0).BoneLength = 12.0f;
		Accessor.Bone(0).bSkipSimulate = false;
		Accessor.Bone(ChildIndex).bSkipSimulate = bChildFixed;
		for (int32 BoneIndex = 0; BoneIndex < Accessor.Num(); ++BoneIndex)
		{
			Accessor.Bone(BoneIndex).PhysicsSettings.Radius = 0.0f;
		}
		return ChildIndex;
	}

	int32 FindFeedbackDummy(const FKawaiiPhysicsTestAccessor& Accessor, const int32 ChildIndex)
	{
		for (int32 BoneIndex = 0; BoneIndex < Accessor.Num(); ++BoneIndex)
		{
			const FKawaiiPhysicsModifyBone& Candidate = Accessor.Bone(BoneIndex);
			if (Candidate.bInterBoneDummy && Candidate.InterBoneRealParentIndex == 0 && Candidate.InterBoneRealChildIndex == ChildIndex)
			{
				return BoneIndex;
			}
		}
		return INDEX_NONE;
	}

// BX-01、mutant なし。外側では最近点から半径だけ離れ、辺と角の法線を区別する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecBoxExteriorTest,
	"KawaiiPhysics.CollisionSpec.Box.ExteriorEdgeAndCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecBoxExteriorTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	FBoxLimit BoxLimit;
	BoxLimit.Extent = FVector(5, 5, 5);
	BoxLimit.bEnable = true;
	TArray<FBoxLimit> BoxLimits{BoxLimit};
	FKawaiiPhysicsModifyBone EdgeBone = MakeCollisionBone(FVector(5.3, 5.4, 0), 1.0f);
	FKawaiiPhysicsModifyBone CornerBone = MakeCollisionBone(FVector(5.2, 5.2, 5.2), 1.0f);
	Accessor.CallBoxCollision(EdgeBone, BoxLimits);
	Accessor.CallBoxCollision(CornerBone, BoxLimits);
	// 辺は距離 0.5 なので法線 (0.6,0.8,0)。角は三軸同量で 1/√3。
	const double CornerOffset = 1.0 / FMath::Sqrt(3.0);
	TestTrue(TEXT("辺の外側から半径 1 の位置"), EdgeBone.Location.Equals(FVector(5.6, 5.8, 0), 1e-4));
	TestTrue(TEXT("角の外側から半径 1 の位置"),
		CornerBone.Location.Equals(FVector(5 + CornerOffset, 5 + CornerOffset, 5 + CornerOffset), 1e-4));
	TestTrue(TEXT("対照: どちらも押し出された"),
		!EdgeBone.Location.Equals(FVector(5.3, 5.4, 0)) && !CornerBone.Location.Equals(FVector(5.2, 5.2, 5.2)));
	return true;
}

// BX-03 / BX-06 / BX-U1、mutant なし。負のゼロは + 側、同じ深さの軸は X を選ぶ。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecBoxTieTest,
	"KawaiiPhysics.CollisionSpec.Box.SignedZeroAndAxisTie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecBoxTieTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	FBoxLimit ThinLimit;
	ThinLimit.Extent = FVector(100, 100, 10);
	ThinLimit.bEnable = true;
	TArray<FBoxLimit> ThinLimits{ThinLimit};
	FKawaiiPhysicsModifyBone NegativeZeroBone = MakeCollisionBone(FVector(0, 0, -0.0), 2.0f);
	Accessor.CallBoxCollision(NegativeZeroBone, ThinLimits);
	TestTrue(TEXT("負のゼロも上面へ"), NegativeZeroBone.Location.Equals(FVector(0, 0, 12), 1e-4));
	FKawaiiPhysicsModifyBone UpperBone = MakeCollisionBone(FVector(0, 0, 5), 2.0f);
	Accessor.CallBoxCollision(UpperBone, ThinLimits);
	TestTrue(TEXT("薄い Box の上半分は上面へ"), UpperBone.Location.Equals(FVector(0, 0, 12), 1e-4));
	FBoxLimit CubeLimit = ThinLimit;
	CubeLimit.Extent = FVector(7, 7, 7);
	TArray<FBoxLimit> CubeLimits{CubeLimit};
	FKawaiiPhysicsModifyBone CenterBone = MakeCollisionBone(FVector::ZeroVector, 2.0f);
	Accessor.CallBoxCollision(CenterBone, CubeLimits);
	TestTrue(TEXT("立方体の同深度では X が先"), CenterBone.Location.Equals(FVector(9, 0, 0), 1e-4));
	TestTrue(TEXT("対照: 入力点から移動"), NegativeZeroBone.Location.Z > 0 && CenterBone.Location.X > 0);
	// float の Box 変換の丸めだけを 1e-4 cm 許す。
	return true;
}

// TC-U1 / TC-03 / TC-12、mutant なし。+Z 端から R=6,4,2 と線形になり、再適用で動かない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecTaperedSideTest,
	"KawaiiPhysics.CollisionSpec.Tapered.SideInterpolationAndIdempotence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecTaperedSideTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	FTaperedCapsuleLimit TaperedLimit;
	TaperedLimit.Length = 10.0f;
	TaperedLimit.Radius0 = 6.0f;
	TaperedLimit.Radius1 = 2.0f;
	TaperedLimit.bEnable = true;
	TArray<FTaperedCapsuleLimit> TaperedLimits{TaperedLimit};
	const double Heights[] = {5.0, 0.0, -5.0};
	const double ExpectedX[] = {7.0, 5.0, 3.0};
	for (int32 CaseIndex = 0; CaseIndex < 3; ++CaseIndex)
	{
		FKawaiiPhysicsModifyBone SideBone = MakeCollisionBone(FVector(0.5, 0, Heights[CaseIndex]), 1.0f);
		Accessor.CallTaperedCapsuleCollision(SideBone, TaperedLimits);
		TestTrue(FString::Printf(TEXT("軸上の高さ %d の線形半径"), CaseIndex),
			SideBone.Location.Equals(FVector(ExpectedX[CaseIndex], 0, Heights[CaseIndex]), 1e-3));
		const FVector Once = SideBone.Location;
		Accessor.CallTaperedCapsuleCollision(SideBone, TaperedLimits);
		TestTrue(FString::Printf(TEXT("再適用で動かない %d"), CaseIndex), SideBone.Location.Equals(Once, 1e-3));
		TestTrue(TEXT("対照: 側面との重なりが解消"), SideBone.Location.X > 0.5);
	}
	// float の線形補間と再適用の丸めを 1e-3 cm 許す。
	return true;
}

// TC-05 / TC-08、mutant 1067: 軸上 Capsule の代替方向を消すと (0,7,0) にならない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecEqualRadiusAxisTest,
	"KawaiiPhysics.CollisionSpec.Tapered.EqualRadiusAxisFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecEqualRadiusAxisTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	const FQuat QuarterTurn(FVector::ZAxisVector, PI / 2);
	FCapsuleLimit CapsuleLimit;
	CapsuleLimit.Rotation = QuarterTurn;
	CapsuleLimit.Radius = 4.0f;
	CapsuleLimit.Length = 10.0f;
	CapsuleLimit.bEnable = true;
	FTaperedCapsuleLimit TaperedLimit;
	TaperedLimit.Rotation = QuarterTurn;
	TaperedLimit.Radius0 = 4.0f;
	TaperedLimit.Radius1 = 4.0f;
	TaperedLimit.Length = 10.0f;
	TaperedLimit.bEnable = true;
	TArray<FCapsuleLimit> CapsuleLimits{CapsuleLimit};
	TArray<FTaperedCapsuleLimit> TaperedLimits{TaperedLimit};
	FKawaiiPhysicsModifyBone CapsuleBone = MakeCollisionBone(FVector::ZeroVector, 3.0f);
	FKawaiiPhysicsModifyBone TaperedBone = MakeCollisionBone(FVector::ZeroVector, 3.0f);
	Accessor.CallCapsuleCollision(CapsuleBone, CapsuleLimits);
	Accessor.CallTaperedCapsuleCollision(TaperedBone, TaperedLimits);
	// 軸からの距離は R+r=4+3=7。float の 90 度回転を 1e-4 cm 許す。
	TestTrue(TEXT("通常 Capsule の軸上は回転後の +X"), CapsuleBone.Location.Equals(FVector(0, 7, 0), 1e-4));
	TestTrue(TEXT("等半径 Tapered と Capsule は一致"), CapsuleBone.Location.Equals(TaperedBone.Location, 1e-4));
	TestTrue(TEXT("対照: 軸上の球は移動"), CapsuleBone.Location.Size() > 6.9);
	return true;
}

// BS-03 / BS-12 / BS-U5、mutant なし。可動実親へ補正を誤配分する壊れ方は実親のビット比較で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecDummyFeedbackTest,
	"KawaiiPhysics.CollisionSpec.Subdivision.DummyFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecDummyFeedbackTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	const int32 ChildIndex = BuildFeedbackSegment(Accessor);
	const int32 DummyIndex = FindFeedbackDummy(Accessor, ChildIndex);
	if (!TestTrue(TEXT("対照: 中央ダミーがある"), DummyIndex != INDEX_NONE)) return false;
	const FVector ParentBefore = Accessor.Bone(0).Location;
	if (!TestTrue(TEXT("対照: 実親は固定されていない中間ボーン"),
		!Accessor.Bone(0).bSkipSimulate && Accessor.Bone(0).ParentIndex != INDEX_NONE)) return false;
	Accessor.CallCaptureCollisionOnlyDummyPreCollision();
	Accessor.Bone(DummyIndex).Location += FVector(0, 3, 0);
	Accessor.CallApplyCollisionOnlyDummyFeedback();
	TestTrue(TEXT("子は 3/0.5=6 動く"), Accessor.Bone(ChildIndex).Location.Equals(FVector(12, 6, 0), 1e-3));
	TestTrue(TEXT("可動実親はビット一致で不変"),
		FMemory::Memcmp(&Accessor.Bone(0).Location, &ParentBefore, sizeof(FVector)) == 0);
	TestTrue(TEXT("新しい区間は押されたダミーを通る"),
		FMath::Lerp(Accessor.Bone(0).Location, Accessor.Bone(ChildIndex).Location, 0.5f).Equals(Accessor.Bone(DummyIndex).Location, 1e-3));
	// float の補正と Lerp の丸めを 1e-3 cm 許す。
	return true;
}

// BS-04 / BS-12、mutant なし。可動実親へ復元前の補正を誤配分する壊れ方は実親のビット比較で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecDummyRestoreTest,
	"KawaiiPhysics.CollisionSpec.Subdivision.RestoreLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecDummyRestoreTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	const int32 ChildIndex = BuildFeedbackSegment(Accessor);
	const int32 DummyIndex = FindFeedbackDummy(Accessor, ChildIndex);
	if (!TestTrue(TEXT("対照: 中央ダミーがある"), DummyIndex != INDEX_NONE)) return false;
	const FVector ParentBefore = Accessor.Bone(0).Location;
	if (!TestTrue(TEXT("対照: 実親は固定されていない中間ボーン"),
		!Accessor.Bone(0).bSkipSimulate && Accessor.Bone(0).ParentIndex != INDEX_NONE)) return false;
	Accessor.CallCaptureCollisionOnlyDummyPreCollision();
	Accessor.Bone(DummyIndex).Location += FVector(0, 3, 0);
	Accessor.CallApplyCollisionOnlyDummyFeedback();
	TestTrue(TEXT("対照: 復元前の子は上がる"), Accessor.Bone(ChildIndex).Location.Y > 5.9);
	TestTrue(TEXT("復元前の可動実親はビット一致で不変"),
		FMemory::Memcmp(&Accessor.Bone(0).Location, &ParentBefore, sizeof(FVector)) == 0);
	Accessor.CallRestoreBoneLengthsAndLimits();
	TestTrue(TEXT("復元後の可動実親もビット一致で不変"),
		FMemory::Memcmp(&Accessor.Bone(0).Location, &ParentBefore, sizeof(FVector)) == 0);
	// 子の復元長は 12、押し戻し前の子の偏位は +6。中央の高さは 6/2 * 12/√(12²+6²)。
	TestTrue(TEXT("復元後も中央の押し出しが残る"),
		FMath::IsNearlyEqual(Accessor.Bone(DummyIndex).Location.Y, 36.0 / FMath::Sqrt(180.0), 1e-3));
	// float の長さ復元と平方根の丸めを 1e-3 cm 許す。
	return true;
}

// BS-U3 / BS-12、mutant なし。固定子の補正を可動実親へ逃がす壊れ方は実親のビット比較で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecFixedChildTest,
	"KawaiiPhysics.CollisionSpec.Subdivision.FixedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecFixedChildTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor FixedAccessor;
	const int32 FixedChildIndex = BuildFeedbackSegment(FixedAccessor, true);
	const int32 FixedDummyIndex = FindFeedbackDummy(FixedAccessor, FixedChildIndex);
	FKawaiiPhysicsTestAccessor MovableAccessor;
	const int32 MovableChildIndex = BuildFeedbackSegment(MovableAccessor);
	const int32 MovableDummyIndex = FindFeedbackDummy(MovableAccessor, MovableChildIndex);
	if (!TestTrue(TEXT("対照: 両区間にダミーがある"), FixedDummyIndex != INDEX_NONE && MovableDummyIndex != INDEX_NONE)) return false;
	if (!TestTrue(TEXT("対照: 固定子の実親は可動"),
		!FixedAccessor.Bone(0).bSkipSimulate && FixedAccessor.Bone(FixedChildIndex).bSkipSimulate)) return false;
	const FVector FixedParentBefore = FixedAccessor.Bone(0).Location;
	const FVector FixedChildBefore = FixedAccessor.Bone(FixedChildIndex).Location;
	const FVector MovableParentBefore = MovableAccessor.Bone(0).Location;
	FixedAccessor.CallCaptureCollisionOnlyDummyPreCollision();
	MovableAccessor.CallCaptureCollisionOnlyDummyPreCollision();
	FixedAccessor.Bone(FixedDummyIndex).Location += FVector(0, 3, 0);
	MovableAccessor.Bone(MovableDummyIndex).Location += FVector(0, 3, 0);
	FixedAccessor.CallApplyCollisionOnlyDummyFeedback();
	MovableAccessor.CallApplyCollisionOnlyDummyFeedback();
	TestTrue(TEXT("固定子はビット一致で不変"),
		FMemory::Memcmp(&FixedAccessor.Bone(FixedChildIndex).Location, &FixedChildBefore, sizeof(FVector)) == 0);
	TestTrue(TEXT("固定子の可動実親はビット一致で不変"),
		FMemory::Memcmp(&FixedAccessor.Bone(0).Location, &FixedParentBefore, sizeof(FVector)) == 0);
	TestTrue(TEXT("対照区間の可動実親もビット一致で不変"),
		FMemory::Memcmp(&MovableAccessor.Bone(0).Location, &MovableParentBefore, sizeof(FVector)) == 0);
	TestTrue(TEXT("対照: 可動子は同じ入力で動く"), MovableAccessor.Bone(MovableChildIndex).Location.Y > 5.9);
	return true;
}

// BS-05 / BS-12、mutant 1052: Pose からの +1 を Push と誤認すると子がずれる。可動実親への誤配分はビット比較で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecEndpointOnlyTest,
	"KawaiiPhysics.CollisionSpec.Subdivision.EndpointOnlyAfterPoseOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecEndpointOnlyTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	const int32 ChildIndex = BuildFeedbackSegment(Accessor);
	const int32 DummyIndex = FindFeedbackDummy(Accessor, ChildIndex);
	if (!TestTrue(TEXT("対照: 中央ダミーがある"), DummyIndex != INDEX_NONE)) return false;
	const FVector ParentBefore = Accessor.Bone(0).Location;
	if (!TestTrue(TEXT("対照: 実親は固定されていない中間ボーン"),
		!Accessor.Bone(0).bSkipSimulate && Accessor.Bone(0).ParentIndex != INDEX_NONE)) return false;
	Accessor.Bone(DummyIndex).Location += FVector(0, 1, 0);
	TestTrue(TEXT("対照: ダミーは Pose からずれている"),
		Accessor.Bone(DummyIndex).Location != Accessor.Bone(DummyIndex).PoseLocation);
	Accessor.CallCaptureCollisionOnlyDummyPreCollision();
	Accessor.Bone(ChildIndex).Location += FVector(0, 1, 0);
	Accessor.CallApplyCollisionOnlyDummyFeedback();
	TestTrue(TEXT("端点だけの押し出しは維持"), Accessor.Bone(ChildIndex).Location.Equals(FVector(12, 1, 0), 1e-3));
	TestTrue(TEXT("可動実親はビット一致で不変"),
		FMemory::Memcmp(&Accessor.Bone(0).Location, &ParentBefore, sizeof(FVector)) == 0);
	// float の補正判定の丸めを 1e-3 cm 許す。
	return true;
}

// BS-01、mutant なし。分割数 0 ならフラグを変えても 60 フレームの位置はビット一致。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecZeroSubdivisionTest,
	"KawaiiPhysics.CollisionSpec.Subdivision.ZeroCountBitExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecZeroSubdivisionTest::RunTest(const FString& Parameters)
{
	auto MakeOptions = [](const bool bFeedback, const bool bWithSphere)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions;
		ChainOptions.Bones = {
			{TEXT("root"), INDEX_NONE, FTransform::Identity},
			{TEXT("chain_0"), 0, FTransform::Identity},
			{TEXT("chain_1"), 1, FTransform(FVector(12, 0, 0))},
			{TEXT("chain_2"), 2, FTransform(FVector(6, 0, 0))}};
		ChainOptions.ConfigureNode = [bFeedback, bWithSphere](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("chain_0");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.PhysicsSettings.Radius = 0.0f;
			PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
			PhysicsNode.BoneSubdivisionCount = 0;
			PhysicsNode.bBoneSubdivisionCollisionOnly = bFeedback;
			if (bWithSphere)
			{
				FSphericalLimit Sphere;
				Sphere.DrivingBone.BoneName = TEXT("root");
				Sphere.OffsetLocation = FVector(12, -2, 0);
				Sphere.Radius = 3.0f;
				Sphere.bEnable = true;
				PhysicsNode.SphericalLimits.Add(Sphere);
			}
		};
		return ChainOptions;
	};
	KawaiiPhysicsNodeFixture::FFixture FeedbackEnabled(MakeOptions(true, true));
	KawaiiPhysicsNodeFixture::FFixture FeedbackDisabled(MakeOptions(false, true));
	KawaiiPhysicsNodeFixture::FFixture NoSphere(MakeOptions(false, false));
	bool bCollisionAffectedChain = false;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		FeedbackEnabled.EvaluateFrame();
		FeedbackDisabled.EvaluateFrame();
		NoSphere.EvaluateFrame();
		if (!TestTrue(TEXT("対照: ModifyBones が作られた"), FeedbackEnabled.Accessor.Num() > 1)) return false;
		if (!TestEqual(TEXT("全セルで ModifyBones 数が同じ"), FeedbackEnabled.Accessor.Num(), FeedbackDisabled.Accessor.Num())) return false;
		for (int32 BoneIndex = 0; BoneIndex < FeedbackEnabled.Accessor.Num(); ++BoneIndex)
		{
			const FVector& EnabledLocation = FeedbackEnabled.Accessor.Bone(BoneIndex).Location;
			const FVector& DisabledLocation = FeedbackDisabled.Accessor.Bone(BoneIndex).Location;
			TestTrue(FString::Printf(TEXT("フレーム %d、ボーン %d はビット一致"), Frame, BoneIndex),
				FMemory::Memcmp(&EnabledLocation, &DisabledLocation, sizeof(FVector)) == 0);
			if (BoneIndex < NoSphere.Accessor.Num()
				&& FVector::Dist(EnabledLocation, NoSphere.Accessor.Bone(BoneIndex).Location) > 0.1)
			{
				bCollisionAffectedChain = true;
			}
		}
	}
	TestTrue(TEXT("対照: 球がチェーンの位置を変えた"), bCollisionAffectedChain);
	// ビット一致を要求するため許容差はない。対照の 0.1 cm は衝突効果の有無を分ける閾値。
	return true;
}

// BX-11、mutant なし。authored と Simple World の内部 Box は同じ最浅 Z 面へ押し出す。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecBoxNodeRoutesTest,
	"KawaiiPhysics.CollisionSpec.Box.AuthoredAndSimpleWorldRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecBoxNodeRoutesTest::RunTest(const FString& Parameters)
{
	auto MakeOptions = [](const bool bAuthored)
	{
		KawaiiPhysicsNodeFixture::FOptions BoxOptions;
		BoxOptions.Bones = {
			{TEXT("root"), INDEX_NONE, FTransform::Identity},
			{TEXT("chain_0"), 0, FTransform(FVector(0, 0, 5))},
			{TEXT("chain_1"), 1, FTransform(FVector(1, 2, -1))}};
		BoxOptions.ConfigureNode = [bAuthored](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("chain_0");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
			PhysicsNode.PhysicsSettings.Radius = 1.0f;
			if (bAuthored)
			{
				FBoxLimit AuthoredBox;
				AuthoredBox.DrivingBone.BoneName = TEXT("chain_0");
				AuthoredBox.OffsetLocation = FVector(0, 0, -5);
				AuthoredBox.Extent = FVector(6, 6, 5);
				AuthoredBox.bEnable = true;
				PhysicsNode.BoxLimits.Add(AuthoredBox);
			}
		};
		return BoxOptions;
	};
	KawaiiPhysicsNodeFixture::FFixture Authored(MakeOptions(true));
	KawaiiPhysicsNodeFixture::FFixture NoBox(MakeOptions(false));
	Authored.EvaluateFrame();
	NoBox.EvaluateFrame();
	if (!TestTrue(TEXT("対照: ModifyBones は親子二本"), Authored.Accessor.Num() >= 2)) return false;
	const FVector Expected(1, 2, 6);
	// 親 (0,0,5) から入力 (1,2,4) と押し出し先 (1,2,6) までの距離は共に √6。
	// 剛性 1・重力 0 で Verlet の入力をポーズに保ち、長さ復元も押し出し先を動かさない。
	TestTrue(TEXT("対照: Box なしの子は入力位置にある"),
		NoBox.Accessor.Bone(1).Location.Equals(FVector(1, 2, 4), 1e-3));
	TestTrue(TEXT("authored Box の内部押し出し"), Authored.Accessor.Bone(1).Location.Equals(Expected, 1e-3));
	{
		KawaiiPhysicsNodeFixture::FScopedCVar SimpleWorldEnabled(
			TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("1"));
		KawaiiPhysicsNodeFixture::FFixture SimpleWorld(MakeOptions(false));
		FBoxLimit WorldBox;
		WorldBox.Extent = FVector(6, 6, 5);
		WorldBox.bEnable = true;
		FKawaiiPhysicsSharedCollisionData WorldData;
		WorldData.BoxLimits.Add(WorldBox);
		const auto Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		Entry->Slot.Publish(WorldData);
		SimpleWorld.Accessor.SetSimpleWorldEntry(Entry);
		SimpleWorld.EvaluateFrame();
		if (!TestTrue(TEXT("対照: Simple World の Box が読まれた"),
			SimpleWorld.Accessor.GetSimpleWorldBoxLimits().Num() == 1)) return false;
		TestTrue(TEXT("Simple World Box の内部押し出し"),
			SimpleWorld.Accessor.Bone(1).Location.Equals(Expected, 1e-3));
	}
	// float のボーン長復元と押し出しの丸めを 1e-3 cm 許す。
	return true;
}

// BS-09 / BS-10 / BS-U4、mutant なし。Densify の α は等間隔で、補正の分母は最低 0.1、変位は区間長まで。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecDensifiedFeedbackTest,
	"KawaiiPhysics.CollisionSpec.Subdivision.DensifiedFeedbackLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecDensifiedFeedbackTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions DenseOptions;
	DenseOptions.Bones = {
		{TEXT("root"), INDEX_NONE, FTransform::Identity},
		{TEXT("chain_0"), 0, FTransform::Identity},
		{TEXT("chain_1"), 1, FTransform(FVector(12, 0, 0))}};
	DenseOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
	{
		PhysicsNode.RootBone.BoneName = TEXT("chain_0");
		PhysicsNode.DummyBoneLength = 0.0f;
		PhysicsNode.WarmUpFrames = 0;
		PhysicsNode.Gravity = FVector::ZeroVector;
		PhysicsNode.PhysicsSettings.Radius = 0.1f;
		PhysicsNode.BoneSubdivisionCount = 1;
		PhysicsNode.bBoneSubdivisionDensifyByRadius = true;
		PhysicsNode.bBoneSubdivisionCollisionOnly = true;
	};
	KawaiiPhysicsNodeFixture::FFixture DenseFixture(DenseOptions);
	DenseFixture.EvaluateFrame();
	int32 ParentIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	for (int32 BoneIndex = 0; BoneIndex < DenseFixture.Accessor.Num(); ++BoneIndex)
	{
		const FName BoneName = DenseFixture.Accessor.Bone(BoneIndex).BoneRef.BoneName;
		if (BoneName == TEXT("chain_0")) ParentIndex = BoneIndex;
		if (BoneName == TEXT("chain_1")) ChildIndex = BoneIndex;
	}
	if (!TestTrue(TEXT("対照: 実親子が作られた"), ParentIndex != INDEX_NONE && ChildIndex != INDEX_NONE)) return false;
	TArray<int32> DummyIndices;
	for (int32 BoneIndex = 0; BoneIndex < DenseFixture.Accessor.Num(); ++BoneIndex)
	{
		const FKawaiiPhysicsModifyBone& Candidate = DenseFixture.Accessor.Bone(BoneIndex);
		if (Candidate.bInterBoneDummy && Candidate.InterBoneRealParentIndex == ParentIndex
			&& Candidate.InterBoneRealChildIndex == ChildIndex) DummyIndices.Add(BoneIndex);
	}
	DummyIndices.Sort([&DenseFixture](const int32 Left, const int32 Right)
	{
		return DenseFixture.Accessor.Bone(Left).InterBoneAlpha < DenseFixture.Accessor.Bone(Right).InterBoneAlpha;
	});
	if (!TestTrue(TEXT("対照: 半径による追加で 10 個以上"), DummyIndices.Num() >= 10)) return false;
	for (int32 DummyOrder = 0; DummyOrder < DummyIndices.Num(); ++DummyOrder)
	{
		const double ExpectedAlpha = static_cast<double>(DummyOrder + 1) / (DummyIndices.Num() + 1);
		TestTrue(FString::Printf(TEXT("等間隔の α %d"), DummyOrder),
			FMath::IsNearlyEqual(DenseFixture.Accessor.Bone(DummyIndices[DummyOrder]).InterBoneAlpha, ExpectedAlpha, 1e-6));
	}
	FKawaiiPhysicsTestAccessor& Accessor = DenseFixture.Accessor;
	Accessor.Bone(ParentIndex).Location = FVector::ZeroVector;
	Accessor.Bone(ChildIndex).Location = FVector(12, 0, 0);
	for (const int32 DummyIndex : DummyIndices)
	{
		Accessor.Bone(DummyIndex).Location = FVector(12 * Accessor.Bone(DummyIndex).InterBoneAlpha, 0, 0);
	}
	Accessor.CallCaptureCollisionOnlyDummyPreCollision();
	Accessor.Bone(DummyIndices[0]).Location += FVector(0, 0.1, 0);
	Accessor.CallApplyCollisionOnlyDummyFeedback();
	// α<0.1 でも 0.1/0.1=1。float の補正を 1e-3 cm 許す。
	TestTrue(TEXT("先頭ダミーの補正は下限 0.1 で 1"),
		Accessor.Bone(ChildIndex).Location.Equals(FVector(12, 1, 0), 1e-3));
	TestTrue(TEXT("実親は不変"), Accessor.Bone(ParentIndex).Location == FVector::ZeroVector);
	Accessor.Bone(ChildIndex).Location = FVector(12, 0, 0);
	Accessor.CallCaptureCollisionOnlyDummyPreCollision();
	Accessor.Bone(DummyIndices[0]).Location += FVector(0, 100, 0);
	Accessor.CallApplyCollisionOnlyDummyFeedback();
	const FVector ChildCorrection = Accessor.Bone(ChildIndex).Location - FVector(12, 0, 0);
	TestTrue(TEXT("過大な補正は区間長 12 が上限"), ChildCorrection.Size() <= 12.001);
	TestTrue(TEXT("対照: 上限に達する押し方"), ChildCorrection.Size() >= 11.999);
	return true;
}

// BX-13、mutant なし。薄い Box の有無を並走させ、各フレームの差が pen_z+r=5+2=7 を超えない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionSpecThinGroundDisplacementTest,
	"KawaiiPhysics.CollisionSpec.Box.ThinGroundDisplacementBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionSpecThinGroundDisplacementTest::RunTest(const FString& Parameters)
{
	auto MakeOptions = [](const bool bUseBox)
	{
		KawaiiPhysicsNodeFixture::FOptions GroundOptions;
		GroundOptions.Bones = {
			{TEXT("root"), INDEX_NONE, FTransform::Identity},
			{TEXT("chain_0"), 0, FTransform(FVector(0, 0, 8.5))},
			{TEXT("chain_1"), 1, FTransform(FVector(0, 0, -3.5))}};
		GroundOptions.ConfigureNode = [bUseBox](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("chain_0");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
			PhysicsNode.PhysicsSettings.Radius = 2.0f;
			if (bUseBox)
			{
				FBoxLimit GroundBox;
				GroundBox.DrivingBone.BoneName = TEXT("root");
				GroundBox.Extent = FVector(100, 100, 10);
				GroundBox.bEnable = true;
				PhysicsNode.BoxLimits.Add(GroundBox);
			}
		};
		return GroundOptions;
	};
	KawaiiPhysicsNodeFixture::FFixture WithBox(MakeOptions(true));
	KawaiiPhysicsNodeFixture::FFixture WithoutBox(MakeOptions(false));
	bool bBoxChangedOutput = false;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		WithBox.EvaluateFrame();
		WithoutBox.EvaluateFrame();
		if (!TestTrue(TEXT("対照: ModifyBones の親子がある"), WithBox.Accessor.Num() >= 2)) return false;
		const FVector WithLocation = WithBox.Accessor.Bone(1).Location;
		const FVector WithoutLocation = WithoutBox.Accessor.Bone(1).Location;
		const double Difference = FVector::Dist(WithLocation, WithoutLocation);
		if (Difference > 1.0) bBoxChangedOutput = true;
		// 親 (0,0,8.5) から入力 z=5 と押し出し先 z=12 の距離は共に 3.5。
		// 剛性 1・重力 0 で各フレームのコリジョン前入力を一致させ、長さ復元も同一距離なので、
		// 二本の最終出力差を一回の Box 変位として測れる。
		TestTrue(FString::Printf(TEXT("フレーム %d の変位は 7 cm 以下"), Frame), Difference <= 7.001);
		TestTrue(TEXT("上面へ抜ける"), WithLocation.Z >= 11.999);
	}
	TestTrue(TEXT("対照: Box が出力を変えた"), bBoxChangedOutput);
	// float の Verlet と長さ復元の丸めを 1e-3 cm 許す。
	return true;
}

} // namespace KawaiiPhysicsCollisionSpecTestPrivate

#endif
