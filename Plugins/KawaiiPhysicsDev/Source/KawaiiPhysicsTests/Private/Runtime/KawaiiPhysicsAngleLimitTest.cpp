// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsAngleLimit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace KawaiiPhysicsAngleLimitTestPrivate
{
using namespace KawaiiPhysics::AngleLimit;

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

	// +Z へ曲げる動きは Y まわりの回転なので、ロックされて平面へ戻る。面内の角度はそのまま保たれる
	FKawaiiPhysicsModifyBone OffPlane = MakePerAxisBone(DirectionFromDegrees(20.0f, 35.0f) * 10.0, Limit);
	A.CallAngleLimit(OffPlane, Parent);
	TestTrue(FString::Printf(TEXT("Hinge removes the locked axis: %s"), *OffPlane.Location.ToString()),
	         OffPlane.Location.Equals(DirectionFromDegrees(20.0f, 0.0f) * 10.0, GAngleLimitTolerance));
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
	FSwingLimit Limit;
	Limit.MinA = -180.0f;
	Limit.MaxA = 180.0f;
	Limit.MinB = -180.0f;
	Limit.MaxB = 180.0f;
	const FSwingFrame FrameX = MakeSwingFrame(FQuat::Identity, EBoneForwardAxis::X_Positive, FVector::XAxisVector);
	const FVector2f SwingZ = DecomposeSwing(FrameX, FVector::XAxisVector, FVector::YAxisVector, Limit);
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
//  軸すれすれの回転でも NaN にならず、最近傍点へ収束する
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitNearAxisTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.NearAxis",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitNearAxisTest::RunTest(const FString& Parameters)
{
	const auto MakeLimit = [](const float ExtentA, const float ExtentB)
	{
		FSwingLimit Limit;
		Limit.MinA = -ExtentA;
		Limit.MaxA = ExtentA;
		Limit.MinB = -ExtentB;
		Limit.MaxB = ExtentB;
		return Limit;
	};

	const FSwingLimit Limits[] = {
		MakeLimit(30.0f, 30.0f), MakeLimit(60.0f, 2.0f), MakeLimit(2.0f, 60.0f),
		MakeLimit(180.0f, 0.1f), MakeLimit(0.1f, 180.0f), MakeLimit(45.0f, 44.0f)
	};
	const float Offsets[] = {0.0f, 1.0e-12f, 1.0e-8f, 1.0e-7f, 1.0e-6f, 1.0e-5f, 1.0e-3f, 0.05f};

	for (const FSwingLimit& Limit : Limits)
	{
		for (const float Offset : Offsets)
		{
			// A 軸すれすれ（A が範囲外）と B 軸すれすれ（B が範囲外）の両方を見る
			const FVector2f Inputs[] = {
				{Limit.MaxA + 20.0f, Offset}, {-(Limit.MaxA + 20.0f), -Offset},
				{Offset, Limit.MaxB + 20.0f}, {-Offset, -(Limit.MaxB + 20.0f)}
			};
			for (const FVector2f& Input : Inputs)
			{
				const FVector2f Clamped = ClampSwing(Input, Limit);
				const float Boundary = FMath::Square(Clamped.X / Limit.MaxA) + FMath::Square(Clamped.Y / Limit.MaxB);
				if (!TestFalse(FString::Printf(TEXT("Clamp of %s is finite"), *Input.ToString()), Clamped.ContainsNaN()) ||
					!TestTrue(FString::Printf(TEXT("Clamp of %s is on the boundary: %s"), *Input.ToString(), *Clamped.ToString()),
					          FMath::IsNearlyEqual(Boundary, 1.0f, 0.001f)))
				{
					return false;
				}

				// 軸すれすれの点の最近傍は軸の端の近くにあり、符号も保たれる
				const bool bOnAxisA = FMath::Abs(Input.X) > FMath::Abs(Input.Y);
				const float MainValue = bOnAxisA ? Clamped.X : Clamped.Y;
				const float MainExtent = bOnAxisA ? Limit.MaxA : Limit.MaxB;
				const float MainInput = bOnAxisA ? Input.X : Input.Y;
				TestTrue(FString::Printf(TEXT("Clamp of %s stays at the axis end: %s"), *Input.ToString(), *Clamped.ToString()),
				         FMath::IsNearlyEqual(MainValue, FMath::Sign(MainInput) * MainExtent, MainExtent * 0.01f));
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
//  ポーズの真逆まで許容するヒンジは、真逆やその付近でも跳ね戻らない
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitFreeHingeTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.FreeHinge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitFreeHingeTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	const FKawaiiPhysicsModifyBone Parent = MakeParent();

	const auto RunHinge = [&](const FVector& Location, const float Min, const float Max)
	{
		FKawaiiPhysicsPerAxisLimitAngle Limit;
		Limit.Y = MakeRange(0.0f, 0.0f);
		Limit.Z = MakeRange(Min, Max);
		FKawaiiPhysicsModifyBone Bone = MakePerAxisBone(Location, Limit);
		A.CallAngleLimit(Bone, Parent);
		return Bone.Location;
	};

	const FVector Opposite(-10, 0, 0);
	TestTrue(TEXT("Free hinge keeps the opposite direction"),
	         RunHinge(Opposite, -180.0f, 180.0f).Equals(Opposite, GAngleLimitTolerance));
	TestTrue(TEXT("Positive hinge keeps the opposite direction"),
	         RunHinge(Opposite, 0.0f, 180.0f).Equals(Opposite, GAngleLimitTolerance));
	TestTrue(TEXT("Negative hinge keeps the opposite direction"),
	         RunHinge(Opposite, -180.0f, 0.0f).Equals(Opposite, GAngleLimitTolerance));

	// 範囲外は円周上で近いほうの端へ戻す。±180 の境目をまたいでも跳ばない
	TestTrue(TEXT("Negative hinge keeps its end across the wrap"),
	         RunHinge(DirectionFromDegrees(179.0f, 0.0f) * 10.0, -180.0f, 0.0f)
	         .Equals(Opposite, GAngleLimitTolerance));
	TestTrue(TEXT("Positive hinge keeps its end across the wrap"),
	         RunHinge(DirectionFromDegrees(-179.0f, 0.0f) * 10.0, 0.0f, 180.0f)
	         .Equals(Opposite, GAngleLimitTolerance));
	TestTrue(TEXT("Out of range returns to the nearer end"),
	         RunHinge(DirectionFromDegrees(-170.0f, 0.0f) * 10.0, 0.0f, 60.0f)
	         .Equals(DirectionFromDegrees(60.0f, 0.0f) * 10.0, GAngleLimitTolerance));

	// 真逆の直前で面外へわずかにずれても、面内の角度が保たれる
	for (const float Yaw : {170.0f, 179.0f, 179.5f, 179.9f, -179.9f})
	{
		const FVector Result = RunHinge(DirectionFromDegrees(Yaw, 0.3f) * 10.0, -180.0f, 180.0f);
		TestTrue(FString::Printf(TEXT("Hinge at yaw %f stays in place: %s"), Yaw, *Result.ToString()),
		         Result.Equals(DirectionFromDegrees(Yaw, 0.0f) * 10.0, GAngleLimitTolerance));
	}

	// ロックしていない形でも、真逆へ届く軸があれば維持する
	FKawaiiPhysicsPerAxisLimitAngle Wide;
	Wide.Y = MakeRange(-10.0f, 10.0f);
	Wide.Z = MakeRange(-180.0f, 180.0f);
	FKawaiiPhysicsModifyBone WideBone = MakePerAxisBone(Opposite, Wide);
	A.CallAngleLimit(WideBone, Parent);
	TestTrue(FString::Printf(TEXT("Widest axis keeps the opposite direction: %s"), *WideBone.Location.ToString()),
	         WideBone.Location.Equals(Opposite, GAngleLimitTolerance));

	// 届かない範囲なら、最も広い方向の端へ戻す
	FKawaiiPhysicsPerAxisLimitAngle Narrow;
	Narrow.Y = MakeRange(-10.0f, 10.0f);
	Narrow.Z = MakeRange(-60.0f, 30.0f);
	FKawaiiPhysicsModifyBone NarrowBone = MakePerAxisBone(Opposite, Narrow);
	A.CallAngleLimit(NarrowBone, Parent);
	TestTrue(FString::Printf(TEXT("Opposite direction returns to the widest end: %s"), *NarrowBone.Location.ToString()),
	         NarrowBone.Location.Equals(DirectionFromDegrees(-60.0f, 0.0f) * 10.0, GAngleLimitTolerance));

	// 全方向へ動ける設定の判定（プレビューの表示切り替えに使う）
	FSwingLimit Free;
	Free.MinA = -180.0f;
	Free.MaxA = 180.0f;
	Free.MinB = -180.0f;
	Free.MaxB = 180.0f;
	TestTrue(TEXT("Both axes free is unlimited"), IsUnlimited(Free));
	Free.MaxB = 170.0f;
	TestFalse(TEXT("A narrower side is limited"), IsUnlimited(Free));

	return true;
}

// ---------------------------------------------------------------------------
//  チェーン全体の経路: ボーン間ダミーを飛ばして実親を基準に制限する
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitChainTest,
                                 "KawaiiPhysics.AngleLimit.PerAxis.Chain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitChainTest::RunTest(const FString& Parameters)
{
	// Root(0) -> ボーン間ダミー(1) -> Child(2) -> ボーン間ダミー(3) -> 末端ダミー(4)、Root -> 末端ダミー(5)
	FKawaiiPhysicsTestAccessor A;
	A.BuildSyncBoneSubdivisionFixture();
	A.Node.bBoneSubdivisionCollisionOnly = true;

	FKawaiiPhysicsPerAxisLimitAngle Limit;
	Limit.Y = MakeRange(0.0f, 0.0f);
	Limit.Z = MakeRange(-60.0f, 60.0f);
	for (FKawaiiPhysicsModifyBone& Bone : A.Node.ModifyBones)
	{
		Bone.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
		Bone.PhysicsSettings.PerAxisLimitAngle = Limit;
	}
	A.Node.ModifyBones[0].bSkipSimulate = true;

	// 実親 Root から見て面外へ振れた Child。親がダミー(1)のままだと別の方向へ補正される
	A.Node.ModifyBones[2].Location = DirectionFromDegrees(30.0f, 40.0f) * 10.0;
	A.CallRestoreBoneLengthsAndLimits();

	const FVector ExpectedChild = DirectionFromDegrees(30.0f, 0.0f) * 10.0;
	const FVector Child = A.Node.ModifyBones[2].Location;
	TestTrue(FString::Printf(TEXT("Child is limited against the real parent: %s"), *Child.ToString()),
	         Child.Equals(ExpectedChild, GAngleLimitTolerance));

	// コリジョン専用ダミーは実端点の間へ置き直される
	TestTrue(TEXT("Collision-only dummy follows the limited endpoints"),
	         A.Node.ModifyBones[1].Location.Equals(ExpectedChild * 0.5, GAngleLimitTolerance));

	// 末端ダミーも同じ制限を受け、Child から見て面内に収まる
	const FVector TipOffset = A.Node.ModifyBones[4].Location - Child;
	TestTrue(FString::Printf(TEXT("Tip dummy stays in the hinge plane: %s"), *TipOffset.ToString()),
	         FMath::IsNearlyZero(static_cast<float>(TipOffset.Z), GAngleLimitTolerance));
	TestTrue(TEXT("Tip dummy keeps its length"),
	         FMath::IsNearlyEqual(static_cast<float>(TipOffset.Size()), 4.0f, GAngleLimitTolerance));

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
	// 回転 Identity のチェーンが -Z へ伸びるので、前方軸 X はずれ、Z なら揃う。
	// root 直下のボーンは放射状に伸びるのが普通なので数えない（4本のうち2本が対象）
	FKawaiiPhysicsTestAccessor A;
	A.BuildVerticalChain(4, 10.0f);

	A.Node.BoneForwardAxis = EBoneForwardAxis::X_Positive;
	TestEqual(TEXT("Cone mode never reports a mismatch"), A.Node.CountAngleLimitForwardAxisMismatches(), 0);

	A.Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
	TestEqual(TEXT("Mismatched bones are counted"), A.Node.CountAngleLimitForwardAxisMismatches(), 2);

	A.Node.BoneForwardAxis = EBoneForwardAxis::Z_Negative;
	TestEqual(TEXT("Aligned bones are not counted"), A.Node.CountAngleLimitForwardAxisMismatches(), 0);

	return true;
}

// ---------------------------------------------------------------------------
//  ヒンジの軸は親のポーズ回転に追従する
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

} // namespace KawaiiPhysicsAngleLimitTestPrivate

#endif // WITH_DEV_AUTOMATION_TESTS
