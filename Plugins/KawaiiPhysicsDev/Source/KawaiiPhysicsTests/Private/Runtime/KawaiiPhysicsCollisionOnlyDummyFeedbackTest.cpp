// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"

namespace KawaiiPhysicsCollisionOnlyDummyFeedbackTestPrivate
{
constexpr float GSegmentLength = 12.0f;
constexpr float GTipLength = 6.0f;
constexpr float GFeedbackTol = 0.001f;

// Root(0,0,0) → ダミー → Child(12,0,0) → ダミー → Tip(18,0,0) の直線チェーン。Root は固定、ボーンの半径は 0
int32 BuildFeedbackChain(FKawaiiPhysicsTestAccessor& A, const int32 SubdivisionCount, const bool bCollisionOnly = true)
{
	const int32 ChildIndex = A.BuildSyncBoneSubdivisionLengthFixture(SubdivisionCount, GSegmentLength, GTipLength);
	A.Node.bBoneSubdivisionCollisionOnly = bCollisionOnly;
	A.Bone(0).bSkipSimulate = true;
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		A.Bone(Index).PhysicsSettings.Radius = 0.0f;
	}
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

// 本番と同じ順序で、球のコリジョン → ダミーのフィードバック → 長さ復元を 1 回通す
void RunSphereCollisionStep(FKawaiiPhysicsTestAccessor& A, TArray<FSphericalLimit>& Spheres)
{
	A.CallCaptureCollisionOnlyDummyPreCollision();
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (!A.Bone(Index).bSkipSimulate)
		{
			A.CallSphereCollision(A.Bone(Index), Spheres);
		}
	}
	A.CallApplyCollisionOnlyDummyFeedback();
	A.CallRestoreBoneLengthsAndLimits();
}

FSphericalLimit MakeSphere(const FVector& Location, const float Radius)
{
	FSphericalLimit Sphere;
	Sphere.Location = Location;
	Sphere.Radius = Radius;
	Sphere.bEnable = true;
	return Sphere;
}

float Penetration(const FVector& Point, const FSphericalLimit& Sphere)
{
	return FMath::Max(0.0f, Sphere.Radius - static_cast<float>(FVector::Dist(Point, Sphere.Location)));
}

// ダミーだけが当たる球：押し出しが子へ返り、長さを戻した後もダミーがほぼ球の外に残る（PAF-364）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionOnlyDummyFeedbackDummyHitTest,
                                 "KawaiiPhysics.Simulation.CollisionOnlyDummyFeedback.DummyHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionOnlyDummyFeedbackDummyHitTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	const int32 ChildIndex = BuildFeedbackChain(A, 1);
	const int32 DummyIndex = RootSegmentDummies(A, ChildIndex)[0];
	// 中央のダミー(6,0,0)だけに 1 めり込む球
	TArray<FSphericalLimit> Spheres{MakeSphere(FVector(6.0, -2.0, 0.0), 3.0f)};

	RunSphereCollisionStep(A, Spheres);

	const float DummyPenetration = Penetration(A.Bone(DummyIndex).Location, Spheres[0]);
	bool bOk = TestTrue(FString::Printf(TEXT("dummy stays almost outside the sphere: %.4f"), DummyPenetration),
		DummyPenetration < 0.05f);
	bOk &= TestTrue(FString::Printf(TEXT("child moved away from the sphere: %s"), *A.Bone(ChildIndex).Location.ToString()),
		A.Bone(ChildIndex).Location.Y > 1.0);
	bOk &= TestTrue(TEXT("segment length is kept"),
		FMath::IsNearlyEqual(A.Bone(ChildIndex).Location.Size(), GSegmentLength, GFeedbackTol));
	return bOk;
}

// 子だけが当たる球：押されていないダミーが子の押し出しを打ち消さない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionOnlyDummyFeedbackEndpointHitTest,
                                 "KawaiiPhysics.Simulation.CollisionOnlyDummyFeedback.EndpointHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionOnlyDummyFeedbackEndpointHitTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	const int32 ChildIndex = BuildFeedbackChain(A, 1);
	// 子(12,0,0)だけに 1 めり込む球。ダミー(6,0,0)からは離れている
	TArray<FSphericalLimit> Spheres{MakeSphere(FVector(12.0, -2.0, 0.0), 3.0f)};

	A.CallCaptureCollisionOnlyDummyPreCollision();
	A.CallSphereCollision(A.Bone(ChildIndex), Spheres);
	const FVector PushedChild = A.Bone(ChildIndex).Location;
	A.CallApplyCollisionOnlyDummyFeedback();

	return TestTrue(FString::Printf(TEXT("child keeps its own push: %s"), *A.Bone(ChildIndex).Location.ToString()),
		A.Bone(ChildIndex).Location.Equals(PushedChild, GFeedbackTol));
}

// ダミーと子が同じ向きに押され、子の押し出しでダミーの位置まで解消済みなら、子を引き戻さない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionOnlyDummyFeedbackResolvedTest,
                                 "KawaiiPhysics.Simulation.CollisionOnlyDummyFeedback.AlreadyResolved",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionOnlyDummyFeedbackResolvedTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	const int32 ChildIndex = BuildFeedbackChain(A, 1);
	const int32 DummyIndex = RootSegmentDummies(A, ChildIndex)[0];

	A.CallCaptureCollisionOnlyDummyPreCollision();
	A.Bone(DummyIndex).Location += FVector(0.0, 1.0, 0.0);
	A.Bone(ChildIndex).Location += FVector(0.0, 4.0, 0.0);
	A.CallApplyCollisionOnlyDummyFeedback();

	return TestTrue(FString::Printf(TEXT("child is not pulled back: %s"), *A.Bone(ChildIndex).Location.ToString()),
		A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, 4.0, 0.0), GFeedbackTol));
}

// 押されたダミーの変位が子へ R/α で返る。1 区間に複数あれば子への補正が最も大きいものを使う
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
	A.CallCaptureCollisionOnlyDummyPreCollision();
	A.Bone(Dummies[0]).Location += FVector(0.0, 1.0, 0.0);
	A.Bone(Dummies[1]).Location += FVector(0.0, -1.0, 0.0);
	A.CallApplyCollisionOnlyDummyFeedback();

	return TestTrue(FString::Printf(TEXT("largest correction wins: %s"), *A.Bone(ChildIndex).Location.ToString()),
		A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, 3.0, 0.0), GFeedbackTol));
}

// 親の近く（α が小さい）のダミーも除外しない。増幅は α の下限 0.1 で抑え、補正は区間の長さで上限をかける
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionOnlyDummyFeedbackLimitsTest,
                                 "KawaiiPhysics.Simulation.CollisionOnlyDummyFeedback.Limits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionOnlyDummyFeedbackLimitsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	{
		// 分割数 10：先頭のダミーは α = 1/11
		FKawaiiPhysicsTestAccessor A;
		const int32 ChildIndex = BuildFeedbackChain(A, 10);
		const TArray<int32> Dummies = RootSegmentDummies(A, ChildIndex);
		A.CallCaptureCollisionOnlyDummyPreCollision();
		A.Bone(Dummies[0]).Location += FVector(0.0, 0.1, 0.0);
		A.CallApplyCollisionOnlyDummyFeedback();
		// 未解消 0.1 を α の下限 0.1 で割った 1.0
		bOk &= TestTrue(FString::Printf(TEXT("a dummy near the parent still feeds back: %s"),
			*A.Bone(ChildIndex).Location.ToString()),
			A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, 1.0, 0.0), GFeedbackTol));
	}
	{
		FKawaiiPhysicsTestAccessor A;
		const int32 ChildIndex = BuildFeedbackChain(A, 1);
		A.CallCaptureCollisionOnlyDummyPreCollision();
		A.Bone(RootSegmentDummies(A, ChildIndex)[0]).Location += FVector(0.0, 100.0, 0.0);
		A.CallApplyCollisionOnlyDummyFeedback();
		const FVector Correction = A.Bone(ChildIndex).Location - FVector(GSegmentLength, 0.0, 0.0);
		bOk &= TestTrue(FString::Printf(TEXT("correction is capped at the segment length: %.3f"), Correction.Size()),
			FMath::IsNearlyEqual(Correction.Size(), GSegmentLength, GFeedbackTol));
	}
	{
		FKawaiiPhysicsTestAccessor A;
		const int32 ChildIndex = BuildFeedbackChain(A, 1, false);
		A.CallCaptureCollisionOnlyDummyPreCollision();
		A.Bone(RootSegmentDummies(A, ChildIndex)[0]).Location += FVector(0.0, 3.0, 0.0);
		A.CallApplyCollisionOnlyDummyFeedback();
		bOk &= TestTrue(TEXT("simulated dummies are not fed back here"),
			A.Bone(ChildIndex).Location.Equals(FVector(GSegmentLength, 0.0, 0.0), GFeedbackTol));
	}
	return bOk;
}

} // namespace KawaiiPhysicsCollisionOnlyDummyFeedbackTestPrivate

#endif
