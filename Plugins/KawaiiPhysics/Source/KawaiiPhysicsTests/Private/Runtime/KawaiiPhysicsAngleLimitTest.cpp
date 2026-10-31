// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsAngleLimit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace KawaiiPhysics::AngleLimit;

namespace
{
	constexpr float GAngleLimitTolerance = 0.01f;

	FKawaiiPhysicsAxisAngleRange MakeRange(const float Min, const float Max)
	{
		FKawaiiPhysicsAxisAngleRange Range;
		Range.Min = Min;
		Range.Max = Max;
		return Range;
	}

	FKawaiiPhysicsModifyBone MakeParent(const FQuat& PoseRotation = FQuat::Identity)
	{
		FKawaiiPhysicsModifyBone Parent;
		Parent.Location = FVector::ZeroVector;
		Parent.PoseLocation = FVector::ZeroVector;
		Parent.PoseRotation = PoseRotation;
		return Parent;
	}

	// ポーズ方向 +X、長さ 10 のボーンを Location へ置く
	FKawaiiPhysicsModifyBone MakePerAxisBone(const FVector& Location, const FKawaiiPhysicsPerAxisLimitAngle& Limit)
	{
		FKawaiiPhysicsModifyBone Bone;
		Bone.Location = Location;
		Bone.PoseLocation = FVector(10, 0, 0);
		Bone.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
		Bone.PhysicsSettings.PerAxisLimitAngle = Limit;
		return Bone;
	}

	FVector DirectionFromDegrees(const float YawDegrees, const float PitchDegrees)
	{
		return FRotator(PitchDegrees, YawDegrees, 0.0f).Vector();
	}
}

// ---------------------------------------------------------------------------
//  ヒンジ: Z まわりだけ曲がり、Y まわりの成分は消える
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitHingeTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.Hinge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitHingeTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	FKawaiiPhysicsPerAxisLimitAngle Limit;
	Limit.Y = MakeRange(0.0f, 0.0f);
	Limit.Z = MakeRange(-60.0f, 60.0f);

	const FKawaiiPhysicsModifyBone Parent = MakeParent();

	// +Y へ曲げる動きは Z まわりの回転なので、範囲内なら維持される
	const FVector InPlane = DirectionFromDegrees(40.0f, 0.0f) * 10.0;
	FKawaiiPhysicsModifyBone Within = MakePerAxisBone(InPlane, Limit);
	A.CallAngleLimit(Within, Parent);
	TestTrue(TEXT("Hinge keeps in-plane bending"), Within.Location.Equals(InPlane, GAngleLimitTolerance));

	// 範囲を超えた分は 60° に戻る
	FKawaiiPhysicsModifyBone Over = MakePerAxisBone(DirectionFromDegrees(85.0f, 0.0f) * 10.0, Limit);
	A.CallAngleLimit(Over, Parent);
	TestTrue(FString::Printf(TEXT("Hinge clamps to its range: %s"), *Over.Location.ToString()),
	         Over.Location.Equals(DirectionFromDegrees(60.0f, 0.0f) * 10.0, GAngleLimitTolerance));

	// +Z へ曲げる動きは Y まわりの回転なので、ロックされて平面へ戻る
	FKawaiiPhysicsModifyBone OffPlane = MakePerAxisBone(DirectionFromDegrees(20.0f, 35.0f) * 10.0, Limit);
	A.CallAngleLimit(OffPlane, Parent);
	TestTrue(FString::Printf(TEXT("Hinge removes the locked axis: %s"), *OffPlane.Location.ToString()),
	         FMath::IsNearlyZero(OffPlane.Location.Z, GAngleLimitTolerance));
	TestTrue(TEXT("Hinge preserves bone length"),
	         FMath::IsNearlyEqual(static_cast<float>(OffPlane.Location.Size()), 10.0f, GAngleLimitTolerance));

	return true;
}

// ---------------------------------------------------------------------------
//  片側制限: 正方向だけ曲がる
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitOneSidedTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.OneSided",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitOneSidedTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	FKawaiiPhysicsPerAxisLimitAngle Limit;
	Limit.Y = MakeRange(0.0f, 0.0f);
	Limit.Z = MakeRange(0.0f, 90.0f);

	const FKawaiiPhysicsModifyBone Parent = MakeParent();

	const FVector Positive = DirectionFromDegrees(70.0f, 0.0f) * 10.0;
	FKawaiiPhysicsModifyBone Allowed = MakePerAxisBone(Positive, Limit);
	A.CallAngleLimit(Allowed, Parent);
	TestTrue(TEXT("Positive side is free"), Allowed.Location.Equals(Positive, GAngleLimitTolerance));

	FKawaiiPhysicsModifyBone Blocked = MakePerAxisBone(DirectionFromDegrees(-30.0f, 0.0f) * 10.0, Limit);
	A.CallAngleLimit(Blocked, Parent);
	TestTrue(FString::Printf(TEXT("Negative side returns to the pose: %s"), *Blocked.Location.ToString()),
	         Blocked.Location.Equals(FVector(10, 0, 0), GAngleLimitTolerance));

	return true;
}

// ---------------------------------------------------------------------------
//  等角の PerAxis は円錐と一致する
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitMatchesConeTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.MatchesCone",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitMatchesConeTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	FKawaiiPhysicsPerAxisLimitAngle Limit;
	Limit.Y = MakeRange(-30.0f, 30.0f);
	Limit.Z = MakeRange(-30.0f, 30.0f);

	const FKawaiiPhysicsModifyBone Parent = MakeParent();

	FRandomStream Random(1234);
	for (int32 Index = 0; Index < 256; ++Index)
	{
		const FVector Location = Random.GetUnitVector() * 10.0;

		FKawaiiPhysicsModifyBone PerAxis = MakePerAxisBone(Location, Limit);
		A.CallAngleLimit(PerAxis, Parent);

		FKawaiiPhysicsModifyBone Cone;
		Cone.Location = Location;
		Cone.PoseLocation = FVector(10, 0, 0);
		Cone.PhysicsSettings.LimitAngle = 30.0f;
		A.CallAngleLimit(Cone, Parent);

		if (!TestTrue(FString::Printf(TEXT("Per axis %s equals cone %s"),
		                              *PerAxis.Location.ToString(), *Cone.Location.ToString()),
		              PerAxis.Location.Equals(Cone.Location, GAngleLimitTolerance)))
		{
			break;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
//  細い形状で大きく引き戻されない
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitThinShapeTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.ThinShape",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitThinShapeTest::RunTest(const FString& Parameters)
{
	FSwingLimit Limit;
	Limit.MinA = -60.0f;
	Limit.MaxA = 60.0f;
	Limit.MinB = -2.0f;
	Limit.MaxB = 2.0f;

	// 中心へ縮める方式だと (11.8, 2.0) まで飛ぶ入力
	const FVector2f Clamped = ClampSwing(FVector2f(30.0f, 5.0f), Limit);
	TestTrue(FString::Printf(TEXT("Clamped A stays near the input: %f"), Clamped.X), Clamped.X > 29.0f);
	TestTrue(FString::Printf(TEXT("Clamped B reaches the boundary: %f"), Clamped.Y), Clamped.Y > 1.5f);

	const float Boundary = FMath::Square(Clamped.X / 60.0f) + FMath::Square(Clamped.Y / 2.0f);
	TestTrue(FString::Printf(TEXT("Clamped point is on the boundary: %f"), Boundary),
	         FMath::IsNearlyEqual(Boundary, 1.0f, 0.001f));

	// 極端に細くても有限で境界上に収まる
	Limit.MinA = -180.0f;
	Limit.MaxA = 180.0f;
	Limit.MinB = -0.1f;
	Limit.MaxB = 0.1f;
	const FVector2f Extreme = ClampSwing(FVector2f(170.0f, 170.0f), Limit);
	TestFalse(TEXT("Extreme shape stays finite"), Extreme.ContainsNaN());
	TestTrue(FString::Printf(TEXT("Extreme B is inside: %f"), Extreme.Y), FMath::Abs(Extreme.Y) <= 0.1001f);

	return true;
}

// ---------------------------------------------------------------------------
//  内側は変更されず、象限ごとの範囲が使われる
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitClampSwingTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.ClampSwing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitClampSwingTest::RunTest(const FString& Parameters)
{
	FSwingLimit Limit;
	Limit.MinA = -10.0f;
	Limit.MaxA = 40.0f;
	Limit.MinB = -20.0f;
	Limit.MaxB = 30.0f;

	const FVector2f Inside(5.0f, -3.0f);
	TestTrue(TEXT("Inside is unchanged"), ClampSwing(Inside, Limit) == Inside);

	TestTrue(TEXT("Positive A uses MaxA"),
	         ClampSwing(FVector2f(90.0f, 0.0f), Limit).Equals(FVector2f(40.0f, 0.0f), GAngleLimitTolerance));
	TestTrue(TEXT("Negative A uses MinA"),
	         ClampSwing(FVector2f(-90.0f, 0.0f), Limit).Equals(FVector2f(-10.0f, 0.0f), GAngleLimitTolerance));
	TestTrue(TEXT("Positive B uses MaxB"),
	         ClampSwing(FVector2f(0.0f, 90.0f), Limit).Equals(FVector2f(0.0f, 30.0f), GAngleLimitTolerance));
	TestTrue(TEXT("Negative B uses MinB"),
	         ClampSwing(FVector2f(0.0f, -90.0f), Limit).Equals(FVector2f(0.0f, -20.0f), GAngleLimitTolerance));

	// プレビューが描く境界はソルバーから見ても境界上にある
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const FVector2f Point = EvaluateLimitBoundary(Limit, Index / 64.0f);
		TestTrue(FString::Printf(TEXT("Boundary point %s stays on the shape"), *Point.ToString()),
		         ClampSwing(Point * 1.01f, Limit).Equals(Point, 0.5f));
		TestTrue(FString::Printf(TEXT("Boundary point %s is not moved inward"), *Point.ToString()),
		         ClampSwing(Point * 0.99f, Limit).Equals(Point * 0.99f, GAngleLimitTolerance));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  回転の符号と BoneForwardAxis の対応
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitFrameTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.Frame",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitFrameTest::RunTest(const FString& Parameters)
{
	// +X から +Y への回転は +Z まわり
	const FSwingFrame FrameX = MakeSwingFrame(FQuat::Identity, EBoneForwardAxis::X_Positive, FVector::XAxisVector);
	const FVector2f SwingZ = DecomposeSwing(FrameX, FVector::XAxisVector, FVector::YAxisVector);
	TestTrue(FString::Printf(TEXT("Rotation sense about local Z: %s"), *SwingZ.ToString()),
	         SwingZ.Equals(FVector2f(0.0f, 90.0f), GAngleLimitTolerance));
	TestTrue(TEXT("Compose inverts decompose"),
	         ComposeSwing(FrameX, FVector::XAxisVector, SwingZ).Equals(FVector::YAxisVector, GAngleLimitTolerance));

	// 骨が前方軸の負方向へ伸びるリグでも軸は反転しない
	const FSwingFrame FrameNegative = MakeSwingFrame(FQuat::Identity, EBoneForwardAxis::X_Positive, -FVector::XAxisVector);
	TestTrue(TEXT("AxisA keeps local Y on a negative bone"),
	         FrameNegative.AxisA.Equals(FVector::YAxisVector, GAngleLimitTolerance));
	TestTrue(TEXT("AxisB keeps local Z on a negative bone"),
	         FrameNegative.AxisB.Equals(FVector::ZAxisVector, GAngleLimitTolerance));

	// 符号付きの前方軸は同じ結果になる
	const FSwingFrame FrameXNegative =
		MakeSwingFrame(FQuat::Identity, EBoneForwardAxis::X_Negative, FVector::XAxisVector);
	TestTrue(TEXT("X_Negative matches X_Positive"), FrameXNegative.AxisA.Equals(FrameX.AxisA, GAngleLimitTolerance));

	// 前方軸ごとの有効軸
	const auto ExpectAxes = [this](const EBoneForwardAxis Forward, const FVector& Pose,
	                               const FVector& ExpectedA, const FVector& ExpectedB)
	{
		const FSwingFrame Frame = MakeSwingFrame(FQuat::Identity, Forward, Pose);
		TestTrue(TEXT("AxisA"), Frame.AxisA.Equals(ExpectedA, GAngleLimitTolerance));
		TestTrue(TEXT("AxisB"), Frame.AxisB.Equals(ExpectedB, GAngleLimitTolerance));
	};
	ExpectAxes(EBoneForwardAxis::Y_Positive, FVector::YAxisVector, FVector::XAxisVector, FVector::ZAxisVector);
	ExpectAxes(EBoneForwardAxis::Z_Negative, -FVector::ZAxisVector, FVector::XAxisVector, FVector::YAxisVector);

	// 親の回転に追従する
	const FQuat ParentRotation(FVector::ZAxisVector, FMath::DegreesToRadians(90.0f));
	const FSwingFrame FrameRotated =
		MakeSwingFrame(ParentRotation, EBoneForwardAxis::X_Positive, ParentRotation.GetAxisX());
	TestTrue(TEXT("Frame follows the parent rotation"),
	         FrameRotated.AxisA.Equals(ParentRotation.GetAxisY(), GAngleLimitTolerance));

	// ポーズ方向が前方軸から少しずれても、軸はポーズ方向に直交する
	const FVector TiltedPose = FVector(1.0, 0.3, -0.2).GetSafeNormal();
	const FSwingFrame FrameTilted = MakeSwingFrame(FQuat::Identity, EBoneForwardAxis::X_Positive, TiltedPose);
	TestTrue(TEXT("AxisA is orthogonal to the pose"),
	         FMath::IsNearlyZero(static_cast<float>(FVector::DotProduct(FrameTilted.AxisA, TiltedPose)), 0.001f));
	TestTrue(TEXT("AxisB is orthogonal to the pose"),
	         FMath::IsNearlyZero(static_cast<float>(FVector::DotProduct(FrameTilted.AxisB, TiltedPose)), 0.001f));

	// 不一致の検出
	TestTrue(TEXT("Aligned forward axis"),
	         IsForwardAxisAligned(FQuat::Identity, EBoneForwardAxis::X_Positive, -FVector::XAxisVector));
	TestFalse(TEXT("Mismatched forward axis"),
	          IsForwardAxisAligned(FQuat::Identity, EBoneForwardAxis::X_Positive, FVector::YAxisVector));

	return true;
}

// ---------------------------------------------------------------------------
//  反平行でも破綻しない
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitAntiParallelTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.AntiParallel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitAntiParallelTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	FKawaiiPhysicsPerAxisLimitAngle Limit;
	Limit.Y = MakeRange(-30.0f, 30.0f);
	Limit.Z = MakeRange(-30.0f, 30.0f);

	const FKawaiiPhysicsModifyBone Parent = MakeParent();
	FKawaiiPhysicsModifyBone Bone = MakePerAxisBone(FVector(-10, 0, 0), Limit);
	A.CallAngleLimit(Bone, Parent);

	TestFalse(TEXT("Anti-parallel stays finite"), Bone.Location.ContainsNaN());
	TestTrue(TEXT("Anti-parallel preserves bone length"),
	         FMath::IsNearlyEqual(static_cast<float>(Bone.Location.Size()), 10.0f, GAngleLimitTolerance));
	const FVector Direction = Bone.Location.GetSafeNormal();
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Direction.X, -1.0, 1.0)));
	TestTrue(FString::Printf(TEXT("Anti-parallel returns to the limit: %f"), Angle),
	         FMath::IsNearlyEqual(Angle, 30.0f, GAngleLimitTolerance));

	return true;
}

// ---------------------------------------------------------------------------
//  カーブと倍率
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitScaleTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.Scale",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitScaleTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsPerAxisLimitAngle Limit;
	Limit.X = MakeRange(-10.0f, 20.0f);
	Limit.Y = MakeRange(-30.0f, 40.0f);
	Limit.Z = MakeRange(-100.0f, 120.0f);

	const FKawaiiPhysicsPerAxisLimitAngle Half = ScalePerAxisLimitAngle(Limit, 0.5f);
	TestEqual(TEXT("Scale applies to Min"), Half.Y.Min, -15.0f);
	TestEqual(TEXT("Scale applies to Max"), Half.Y.Max, 20.0f);

	const FKawaiiPhysicsPerAxisLimitAngle Locked = ScalePerAxisLimitAngle(Limit, 0.0f);
	TestEqual(TEXT("Scale 0 locks Min"), Locked.Z.Min, 0.0f);
	TestEqual(TEXT("Scale 0 locks Max"), Locked.Z.Max, 0.0f);

	const FKawaiiPhysicsPerAxisLimitAngle Over = ScalePerAxisLimitAngle(Limit, 3.0f);
	TestEqual(TEXT("Max is clamped to 180"), Over.Z.Max, 180.0f);
	TestEqual(TEXT("Min is clamped to -180"), Over.Z.Min, -180.0f);

	const FKawaiiPhysicsPerAxisLimitAngle Negative = ScalePerAxisLimitAngle(Limit, -1.0f);
	TestEqual(TEXT("Negative scale locks"), Negative.Y.Max, 0.0f);

	// ボーンへの反映（カーブなしの高速経路）
	FKawaiiPhysicsTestAccessor A;
	A.BuildVerticalChain(3, 10.0f);
	A.Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
	A.Node.PhysicsSettings.PerAxisLimitAngle = Limit;
	A.CallUpdatePhysicsSettings();
	TestTrue(TEXT("Mode reaches the bones"),
	         A.Node.ModifyBones.Last().PhysicsSettings.LimitAngleMode == EKawaiiPhysicsLimitAngleMode::PerAxis);
	TestEqual(TEXT("Limits reach the bones"),
	          A.Node.ModifyBones.Last().PhysicsSettings.PerAxisLimitAngle.Y.Max, 40.0f);

	// カーブありの経路
	A.Node.LimitAngleCurveData.GetRichCurve()->AddKey(0.0f, 0.5f);
	A.Node.LimitAngleCurveData.GetRichCurve()->AddKey(1.0f, 0.5f);
	A.CallUpdatePhysicsSettings();
	TestEqual(TEXT("Curve scales the limits"),
	          A.Node.ModifyBones.Last().PhysicsSettings.PerAxisLimitAngle.Y.Max, 20.0f);

	return true;
}

// ---------------------------------------------------------------------------
//  BoneForwardAxis の不一致検出
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitForwardMismatchTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.ForwardMismatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitForwardMismatchTest::RunTest(const FString& Parameters)
{
	// 回転 Identity のチェーンが -Z へ伸びるので、前方軸 X はずれ、Z なら揃う
	FKawaiiPhysicsTestAccessor A;
	A.BuildVerticalChain(3, 10.0f);

	A.Node.BoneForwardAxis = EBoneForwardAxis::X_Positive;
	TestEqual(TEXT("Cone mode never reports a mismatch"), A.Node.CountAngleLimitForwardAxisMismatches(), 0);

	A.Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
	TestEqual(TEXT("Mismatched bones are counted"), A.Node.CountAngleLimitForwardAxisMismatches(), 2);

	A.Node.BoneForwardAxis = EBoneForwardAxis::Z_Negative;
	TestEqual(TEXT("Aligned bones are not counted"), A.Node.CountAngleLimitForwardAxisMismatches(), 0);

	return true;
}

// ---------------------------------------------------------------------------
//  親のポーズ回転に追従したチェーン全体のヒンジ（解法の経路）
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitParentRotationTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.ParentRotation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitParentRotationTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	FKawaiiPhysicsPerAxisLimitAngle Limit;
	Limit.Y = MakeRange(0.0f, 0.0f);
	Limit.Z = MakeRange(-60.0f, 60.0f);

	// 親が X まわりに 90° 回ると、ローカル Z は +Z から -Y へ移る。ヒンジ面も一緒に回る
	const FQuat ParentRotation(FVector::XAxisVector, FMath::DegreesToRadians(90.0f));
	const FKawaiiPhysicsModifyBone Parent = MakeParent(ParentRotation);

	FKawaiiPhysicsModifyBone Bone = MakePerAxisBone(DirectionFromDegrees(0.0f, 30.0f) * 10.0, Limit);
	A.CallAngleLimit(Bone, Parent);

	TestTrue(FString::Printf(TEXT("Hinge plane follows the parent: %s"), *Bone.Location.ToString()),
	         Bone.Location.Equals(DirectionFromDegrees(0.0f, 30.0f) * 10.0, GAngleLimitTolerance));

	FKawaiiPhysicsModifyBone OffPlane = MakePerAxisBone(DirectionFromDegrees(30.0f, 0.0f) * 10.0, Limit);
	A.CallAngleLimit(OffPlane, Parent);
	TestTrue(FString::Printf(TEXT("Motion out of the rotated plane is removed: %s"), *OffPlane.Location.ToString()),
	         OffPlane.Location.Equals(FVector(10, 0, 0), GAngleLimitTolerance));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
