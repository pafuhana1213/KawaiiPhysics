// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"

namespace KawaiiPhysicsCollisionOnlyDummyFeedbackTestPrivate
{
constexpr float GSegmentLength = 12.0f;
constexpr float GTipLength = 6.0f;
constexpr float GFeedbackTol = 0.001f;

// Root(0,0,0) → ダミー → Child(12,0,0) → ダミー → Tip(18,0,0) の直線チェーン。Root は固定
int32 BuildFeedbackChain(FKawaiiPhysicsTestAccessor& A, const int32 SubdivisionCount, const bool bCollisionOnly = true)
{
	const int32 ChildIndex = A.BuildSyncBoneSubdivisionLengthFixture(SubdivisionCount, GSegmentLength, GTipLength);
	A.Node.bBoneSubdivisionCollisionOnly = bCollisionOnly;
	A.Bone(0).bSkipSimulate = true;
	return ChildIndex;
}

// Root と Child の間のダミーを、α の小さい順に返す
TArray<int32> RootSegmentDummies(FKawaiiPhysicsTestAccessor& A, const int32 ChildIndex)
{
	TArray<int32> Result;
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		const FKawaiiPhysicsModifyBone& Bone = A.Bone(Index);
		if (Bone.bInterBoneDummy && Bone.InterBoneRealParentIndex == 0 && Bone.InterBoneRealChildIndex == ChildIndex)
		{
			Result.Add(Index);
		}
	}
	Result.Sort([&A](const int32 L, const int32 R) { return A.Bone(L).InterBoneAlpha < A.Bone(R).InterBoneAlpha; });
	return Result;
}

// 押されたダミーの変位が子へ R/α で返り、長さを戻した後も押し出しの一部が残る（PAF-364）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionOnlyDummyFeedbackPushTest,
                                 "KawaiiPhysics.Simulation.CollisionOnlyDummyFeedback.Push",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionOnlyDummyFeedbackPushTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	const int32 ChildIndex = BuildFeedbackChain(A, 1);
	const TArray<int32> Dummies = RootSegmentDummies(A, ChildIndex);
	if (!TestEqual(TEXT("one dummy in the root segment"), Dummies.Num(), 1))
	{
		return false;
	}
	FKawaiiPhysicsModifyBone& Dummy = A.Bone(Dummies[0]);
	bool bOk = TestTrue(TEXT("dummy is at the middle"), FMath::IsNearlyEqual(Dummy.InterBoneAlpha, 0.5f, GFeedbackTol));

	// コリジョンが中央のダミーだけを +Y へ 3 押し出した
	constexpr float Push = 3.0f;
	Dummy.Location += FVector(0.0, Push, 0.0);

	A.CallApplyCollisionOnlyDummyFeedback();
	// 親を固定して子を R/α = 6 動かすと、区間が押されたダミーを通る
	bOk &= TestTrue(FString::Printf(TEXT("child moves by R/alpha: %s"), *A.Bone(ChildIndex).Location.ToString()),
		A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, Push / 0.5f, 0.0), GFeedbackTol));

	A.CallRestoreBoneLengthsAndLimits();
	// 長さを戻すと押し出しは一部弱まり、中央の高さは hL / sqrt(L^2 + 4h^2) になる
	const float Expected = Push * GSegmentLength / FMath::Sqrt(GSegmentLength * GSegmentLength + 4.0f * Push * Push);
	bOk &= TestTrue(FString::Printf(TEXT("dummy keeps the push after the length restore: %.4f (expected %.4f)"),
		Dummy.Location.Y, Expected), FMath::IsNearlyEqual(Dummy.Location.Y, Expected, GFeedbackTol));
	bOk &= TestTrue(TEXT("segment length is kept"),
		FMath::IsNearlyEqual(A.Bone(ChildIndex).Location.Size(), GSegmentLength, GFeedbackTol));
	return bOk;
}

// 1 区間の複数のダミーが押されたら、子への補正が最も大きいものを使う。平均はしない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionOnlyDummyFeedbackLargestTest,
                                 "KawaiiPhysics.Simulation.CollisionOnlyDummyFeedback.Largest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionOnlyDummyFeedbackLargestTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	const int32 ChildIndex = BuildFeedbackChain(A, 2);
	const TArray<int32> Dummies = RootSegmentDummies(A, ChildIndex);
	if (!TestEqual(TEXT("two dummies in the root segment"), Dummies.Num(), 2))
	{
		return false;
	}

	// α=1/3 を +Y へ 1（子への補正 3）、α=2/3 を -Y へ 1（子への補正 -1.5）
	A.Bone(Dummies[0]).Location += FVector(0.0, 1.0, 0.0);
	A.Bone(Dummies[1]).Location += FVector(0.0, -1.0, 0.0);
	A.CallApplyCollisionOnlyDummyFeedback();

	return TestTrue(FString::Printf(TEXT("largest correction wins: %s"), *A.Bone(ChildIndex).Location.ToString()),
		A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, 3.0, 0.0), GFeedbackTol));
}

// 補正は区間の長さで上限をかける。押されていないとき・コリジョン専用モードでないときは何もしない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionOnlyDummyFeedbackLimitsTest,
                                 "KawaiiPhysics.Simulation.CollisionOnlyDummyFeedback.Limits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionOnlyDummyFeedbackLimitsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	{
		FKawaiiPhysicsTestAccessor A;
		const int32 ChildIndex = BuildFeedbackChain(A, 1);
		A.Bone(RootSegmentDummies(A, ChildIndex)[0]).Location += FVector(0.0, 100.0, 0.0);
		A.CallApplyCollisionOnlyDummyFeedback();
		const FVector Correction = A.Bone(ChildIndex).Location - FVector(GSegmentLength, 0.0, 0.0);
		bOk &= TestTrue(FString::Printf(TEXT("correction is capped at the segment length: %.3f"), Correction.Size()),
			FMath::IsNearlyEqual(Correction.Size(), GSegmentLength, GFeedbackTol));
	}
	{
		FKawaiiPhysicsTestAccessor A;
		const int32 ChildIndex = BuildFeedbackChain(A, 2);
		A.CallApplyCollisionOnlyDummyFeedback();
		bOk &= TestTrue(TEXT("no push, no correction"),
			A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, 0.0, 0.0), GFeedbackTol));
	}
	{
		FKawaiiPhysicsTestAccessor A;
		const int32 ChildIndex = BuildFeedbackChain(A, 1, false);
		A.Bone(RootSegmentDummies(A, ChildIndex)[0]).Location += FVector(0.0, 3.0, 0.0);
		A.CallApplyCollisionOnlyDummyFeedback();
		bOk &= TestTrue(TEXT("simulated dummies are not fed back here"),
			A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, 0.0, 0.0), GFeedbackTol));
	}
	return bOk;
}

} // namespace KawaiiPhysicsCollisionOnlyDummyFeedbackTestPrivate

#endif
