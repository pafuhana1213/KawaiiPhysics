// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/AnimInstanceProxy.h"

// 物理計算の回帰テスト（Output 非依存の物理関数を直接呼ぶ）：パラメータ応答（重力方向・剛性単調性・減衰オーバーシュート）／フレームレート非依存性／数値安定性。

namespace
{
	// 標準の縦チェーン構成でシミュレーションし、最終 tip 位置を返す。
	// OutMaxAbsX != null のとき、シミュレーション中の |tip.X| のピークを返す（オーバーシュート計測用）。
	FVector SimulateChainTip(float Damping, float Stiffness, const FVector& Gravity,
	                         bool bFixedSubstep, int32 TargetFps, int32 NumFrames, float FrameDt,
	                         float* OutMaxAbsX = nullptr)
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(4, 10.0f); // root + 3 segments, tip at (0,0,-30)

		FKawaiiPhysicsSettings S;
		S.Damping = Damping;
		S.Stiffness = Stiffness;
		S.LimitAngle = 0.0f;
		S.Radius = 0.0f;
		A.SetAllPhysicsSettings(S);

		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(Gravity);
		A.SetFixedSubstepping(bFixedSubstep, TargetFps, 32);

		float MaxAbsX = 0.0f;
		for (int32 i = 0; i < NumFrames; ++i)
		{
			A.StepFrame(FrameDt);
			MaxAbsX = FMath::Max(MaxAbsX, static_cast<float>(FMath::Abs(A.TipLocation().X)));
		}
		if (OutMaxAbsX)
		{
			*OutMaxAbsX = MaxAbsX;
		}
		return A.TipLocation();
	}

}

// ---------------------------------------------------------------------------
//  抽出した物理計算関数の検証（解析的）
//  抽出した経路（速度寄与(wind)・legacy gravity・simple external force）を直接検証する。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsIntegrationCoreTest,
                                 "KawaiiPhysics.Simulation.IntegrationCore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsIntegrationCoreTest::RunTest(const FString& Parameters)
{
	const float Tol = 0.001f;

	// 共通の初期ボーン: Location=(0,0,0), PrevLocation=(-1,0,0), Damping=0.25。
	// DtOld=0.5 → 初速度 (2,0,0); damping 後 (1.5,0,0)。Dt=0.5, Gravity=(0,0,-10)。
	auto MakeBone = []()
	{
		FKawaiiPhysicsModifyBone B;
		B.Location = FVector(0, 0, 0);
		B.PrevLocation = FVector(-1, 0, 0);
		B.PhysicsSettings.Damping = 0.25f;
		return B;
	};

	// --- (a) 非legacy gravity + 速度寄与(wind) ---
	// V=(1.5,0,0)+wind(0,3,0)+g*dt(0,0,-5)=(1.5,3,-5); Loc=0+V*0.5=(0.75,1.5,-2.5)
	{
		FKawaiiPhysicsTestAccessor A;
		A.SetGravityInSimSpace(FVector(0, 0, -10));
		A.SetUseLegacyGravity(false);
		A.SetSimpleExternalForceInSimSpace(FVector::ZeroVector);
		A.SetTimeState(0.5f, 0.5f);
		FKawaiiPhysicsModifyBone B = MakeBone();
		const FVector Velocity = A.CallComputeVerletStepVelocity(B, FVector(0, 3, 0));
		// ApplyToVelocity に渡る「実速度」（減衰+wind+重力）の契約を固定（以前 wind だけが渡る退行があった）。
		// Simulate() の呼び出し配線自体は Output 依存のため headless 対象外。
		TestTrue(FString::Printf(TEXT("ApplyToVelocity input (non-legacy) = %s"), *Velocity.ToString()),
		         Velocity.Equals(FVector(1.5f, 3.0f, -5.0f), Tol));
		A.CallIntegrateVerletStepPosition(B, Velocity);
		A.CallSimpleExternalForce(B);
		TestTrue(FString::Printf(TEXT("Core non-legacy+wind: got %s"), *B.Location.ToString()),
		         B.Location.Equals(FVector(0.75f, 1.5f, -2.5f), Tol));
		TestTrue(TEXT("Core updates PrevLocation"), B.PrevLocation.Equals(FVector(0, 0, 0), Tol));
	}

	// --- (b) legacy gravity（位置へ 0.5*g*dt^2 = (0,0,-1.25)） ---
	// V=(1.5,3,0); Loc=(0,0,-1.25)+V*0.5=(0.75,1.5,-1.25)
	{
		FKawaiiPhysicsTestAccessor A;
		A.SetGravityInSimSpace(FVector(0, 0, -10));
		A.SetUseLegacyGravity(true);
		A.SetSimpleExternalForceInSimSpace(FVector::ZeroVector);
		A.SetTimeState(0.5f, 0.5f);
		FKawaiiPhysicsModifyBone B = MakeBone();
		const FVector Velocity = A.CallComputeVerletStepVelocity(B, FVector(0, 3, 0));
		// legacy では重力は位置へ入るので、フックに渡る速度は減衰+wind のみ（重力なし）。
		TestTrue(FString::Printf(TEXT("ApplyToVelocity input (legacy) = %s"), *Velocity.ToString()),
		         Velocity.Equals(FVector(1.5f, 3.0f, 0.0f), Tol));
		A.CallIntegrateVerletStepPosition(B, Velocity);
		A.CallSimpleExternalForce(B);
		TestTrue(FString::Printf(TEXT("Core legacy gravity: got %s"), *B.Location.ToString()),
		         B.Location.Equals(FVector(0.75f, 1.5f, -1.25f), Tol));
	}

	// --- (c) simple external force（位置へ SimpleExt*dt = (0,0,1)） ---
	// 非legacy と同じ (0.75,1.5,-2.5) に +（0,0,1）= (0.75,1.5,-1.5)
	{
		FKawaiiPhysicsTestAccessor A;
		A.SetGravityInSimSpace(FVector(0, 0, -10));
		A.SetUseLegacyGravity(false);
		A.SetSimpleExternalForceInSimSpace(FVector(0, 0, 2));
		A.SetTimeState(0.5f, 0.5f);
		FKawaiiPhysicsModifyBone B = MakeBone();
		const FVector Velocity = A.CallComputeVerletStepVelocity(B, FVector(0, 3, 0));
		A.CallIntegrateVerletStepPosition(B, Velocity);
		A.CallSimpleExternalForce(B);
		TestTrue(FString::Printf(TEXT("Core simple external force: got %s"), *B.Location.ToString()),
		         B.Location.Equals(FVector(0.75f, 1.5f, -1.5f), Tol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  BoneConstraint XPBD dt 正規化
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneConstraintStepDeltaTimeTest,
                                 "KawaiiPhysics.Simulation.BoneConstraintStepDeltaTime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneConstraintStepDeltaTimeTest::RunTest(const FString& Parameters)
{
	auto SolveOnceDistance = [](float FrameDt, bool bSubstep, float StepDt)
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(2, 10.0f, FVector::ZeroVector, FVector(1.0f, 0.0f, 0.0f));
		A.Bone(1).Location = FVector(20.0f, 0.0f, 0.0f);
		A.Bone(1).PrevLocation = A.Bone(1).Location;
		A.SetBoneConstraintGlobalComplianceType(EXPBDComplianceType::Fat);
		A.AddRuntimeBoneConstraint(0, 1, 10.0f);
		if (bSubstep)
		{
			A.SetSubstepTimeState(FrameDt, StepDt);
		}
		else
		{
			A.SetTimeState(FrameDt, FrameDt);
		}
		A.CallBoneConstraints();
		return static_cast<float>((A.Bone(1).Location - A.Bone(0).Location).Size());
	};

	const float FixedDt = 1.0f / 60.0f;
	const float SubstepDistance = SolveOnceDistance(1.0f / 30.0f, true, FixedDt);
	const float LegacySameStepDistance = SolveOnceDistance(FixedDt, false, FixedDt);

	const float Compliance = 0.0001f / (FixedDt * FixedDt); // EXPBDComplianceType::Fat
	const float ExpectedDistance = 20.0f - 2.0f * (10.0f / (2.0f + Compliance));
	TestTrue(FString::Printf(TEXT("Substep BoneConstraint uses StepDt: got %.6f expected %.6f"),
	                         SubstepDistance, ExpectedDistance),
	         FMath::IsNearlyEqual(SubstepDistance, ExpectedDistance, 0.0001f));
	TestTrue(FString::Printf(TEXT("Substep FrameDt=1/30 matches legacy StepDt=1/60: %.6f vs %.6f"),
	                         SubstepDistance, LegacySameStepDistance),
	         FMath::IsNearlyEqual(SubstepDistance, LegacySameStepDistance, 0.0001f));

	return true;
}

// ---------------------------------------------------------------------------
//  SyncBone + BoneSubdivision / 内部 dummy は SyncBone のターゲットに含めない
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBoneSubdivisionTargetTest,
                                 "KawaiiPhysics.Simulation.SyncBoneSubdivisionTargets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBoneSubdivisionTargetTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	A.BuildSyncBoneSubdivisionFixture();

	const FKawaiiPhysicsSyncTargetRoot TargetRoot = A.CollectSyncChildTargetsForRoot(0);
	auto HasTargetIndex = [&](int32 Index)
	{
		return TargetRoot.ChildTargets.ContainsByPredicate([&](const FKawaiiPhysicsSyncTarget& Target)
		{
			return Target.ModifyBoneIndex == Index;
		});
	};

	TestEqual(TEXT("Only real child + legacy direct tip dummy are exposed as SyncBone child targets"),
	          TargetRoot.ChildTargets.Num(), 2);
	TestTrue(TEXT("Real child remains a SyncBone child target"), HasTargetIndex(2));
	TestTrue(TEXT("Legacy direct tip dummy keeps existing SyncBone behavior"), HasTargetIndex(5));
	TestFalse(TEXT("Inter-bone dummy between real bones is hidden from SyncBone targets"), HasTargetIndex(1));
	TestFalse(TEXT("Terminal inter-bone dummy is hidden from SyncBone targets"), HasTargetIndex(3));
	TestFalse(TEXT("Subdivided tip dummy is hidden from SyncBone targets"), HasTargetIndex(4));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBoneSubdivisionPoseRefreshTest,
                                 "KawaiiPhysics.Simulation.SyncBoneSubdivisionPoseRefresh",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBoneSubdivisionPoseRefreshTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	A.BuildSyncBoneSubdivisionFixture();

	A.Bone(0).PoseLocation = FVector(0.0f, 0.0f, 0.0f);
	A.Bone(2).PoseLocation = FVector(12.0f, 0.0f, 0.0f);
	A.Bone(1).PoseLocation = FVector(100.0f, 0.0f, 0.0f);
	A.Bone(3).PoseLocation = FVector(100.0f, 0.0f, 0.0f);
	A.Bone(4).PoseLocation = FVector(100.0f, 0.0f, 0.0f);

	A.CallUpdateSubdivisionDummyPoseAfterSyncBones();

	const float Tol = 0.001f;
	TestTrue(FString::Printf(TEXT("Inter-bone dummy is re-lerped after SyncBone: %s"),
	                         *A.Bone(1).PoseLocation.ToString()),
	         A.Bone(1).PoseLocation.Equals(FVector(6.0f, 0.0f, 0.0f), Tol));
	TestTrue(FString::Printf(TEXT("Subdivided tip dummy follows its real ancestor: %s"),
	                         *A.Bone(4).PoseLocation.ToString()),
	         A.Bone(4).PoseLocation.Equals(FVector(16.0f, 0.0f, 0.0f), Tol));
	TestTrue(FString::Printf(TEXT("Terminal inter-bone dummy is re-lerped to refreshed tip: %s"),
	                         *A.Bone(3).PoseLocation.ToString()),
	         A.Bone(3).PoseLocation.Equals(FVector(14.0f, 0.0f, 0.0f), Tol));

	return true;
}

// ---------------------------------------------------------------------------
//  SyncBone + BoneSubdivision: 子は stale inter-bone dummy 基準で歪まず剛体並進する
//  (回帰: SyncBone+Subdivision の残留ストレッチ — 子が未更新の dummy 親基準で拘束されていた)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBoneSubdivisionApplyTest,
                                 "KawaiiPhysics.Simulation.SyncBoneSubdivisionApply",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBoneSubdivisionApplyTest::RunTest(const FString& Parameters)
{
	// 剛体並進時の child 位置と、非剛体移動時の segment 長を守る。
	FKawaiiPhysicsTestAccessor A;
	A.BuildSyncBoneSubdivisionFixture();

	// root(0) -> inter-bone dummy(1) -> real child(2)。root と child(2) を同一 delta で sync すると剛体並進になるはず。
	FKawaiiPhysicsSyncTargetRoot TargetRoot = A.CollectSyncChildTargetsForRoot(0);

	const FVector Delta(0.0f, 10.0f, 0.0f);
	A.ApplySyncTargetsForRoot(TargetRoot, Delta);

	const float Tol = 0.001f;
	// root は ParentIndex<0 なので素直に並進
	TestTrue(FString::Printf(TEXT("Root translates rigidly: %s"), *A.Bone(0).PoseLocation.ToString()),
	         A.Bone(0).PoseLocation.Equals(FVector(0.0f, 10.0f, 0.0f), Tol));

	// child(2) の親は inter-bone dummy(1)。修正前は stale dummy=(5,0,0) 基準の長さ拘束で約(7.236,4.472,0)へ歪む。
	// 修正後は剛体並進 (10,10,0)。
	TestTrue(FString::Printf(TEXT("Subdivided real child translates rigidly (no stale-dummy distortion): %s"),
	                         *A.Bone(2).PoseLocation.ToString()),
	         A.Bone(2).PoseLocation.Equals(FVector(10.0f, 10.0f, 0.0f), Tol));


	{
		FKawaiiPhysicsTestAccessor B;
		B.BuildSyncBoneSubdivisionFixture();
		FKawaiiPhysicsSyncTargetRoot SplitTargets = B.CollectSyncChildTargetsForRoot(0);
		B.ApplySyncTargetsForRootSplit(SplitTargets, FVector(0.0f, 10.0f, 0.0f), FVector(0.0f, 5.0f, 0.0f));
		const float RestLen = 10.0f;
		const float Len = FVector::Dist(B.Bone(2).PoseLocation, B.Bone(0).PoseLocation);
		TestTrue(FString::Printf(TEXT("Non-rigid sync preserves grandparent->child length: %.4f (expect %.1f) child=%s"),
		                         Len, RestLen, *B.Bone(2).PoseLocation.ToString()),
		         FMath::IsNearlyEqual(Len, RestLen, 0.001f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBoneSubdivisionLengthTest,
                                 "KawaiiPhysics.Simulation.SyncBoneSubdivisionLength",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBoneSubdivisionLengthTest::RunTest(const FString& Parameters)
{
	constexpr float SegmentLength = 12.0f;
	constexpr float TipLength = 6.0f;
	constexpr float Tol = 0.001f;

	for (int32 SubdivisionCount = 1; SubdivisionCount <= 2; ++SubdivisionCount)
	{
		FKawaiiPhysicsTestAccessor A;
		const int32 ChildIndex = A.BuildSyncBoneSubdivisionLengthFixture(
			SubdivisionCount, SegmentLength, TipLength);
		const float ExpectedSegmentLength = SegmentLength / (SubdivisionCount + 1);
		TestTrue(FString::Printf(TEXT("N=%d real child BoneLength is final segment length"), SubdivisionCount),
		         FMath::IsNearlyEqual(A.Bone(ChildIndex).BoneLength, ExpectedSegmentLength, Tol));
		TestTrue(FString::Printf(TEXT("N=%d real child LengthFromRoot counts each segment once"), SubdivisionCount),
		         FMath::IsNearlyEqual(A.Bone(ChildIndex).LengthFromRoot, SegmentLength, Tol));
		TestTrue(FString::Printf(TEXT("N=%d subdivided tip LengthFromRoot includes tip once"), SubdivisionCount),
		         FMath::IsNearlyEqual(A.Bone(A.Num() - 1).LengthFromRoot, SegmentLength + TipLength, Tol));
		TestTrue(FString::Printf(TEXT("N=%d real child LengthRateFromRoot uses physical chain length"), SubdivisionCount),
		         FMath::IsNearlyEqual(A.Bone(ChildIndex).LengthRateFromRoot,
		                              SegmentLength / (SegmentLength + TipLength), Tol));

		FKawaiiPhysicsSyncTargetRoot TargetRoot = A.CollectSyncChildTargetsForRoot(0);
		TestEqual(FString::Printf(TEXT("N=%d only real child is a SyncBone target"), SubdivisionCount),
		          TargetRoot.ChildTargets.Num(), 1);
		A.ApplySyncTargetsForRootSplit(TargetRoot, FVector(0.0f, 10.0f, 0.0f),
		                               FVector(0.0f, 5.0f, 0.0f));
		const float Distance = FVector::Dist(A.Bone(0).PoseLocation, A.Bone(ChildIndex).PoseLocation);
		TestTrue(FString::Printf(TEXT("N=%d non-rigid SyncBone keeps real segment length: %.4f"),
		                         SubdivisionCount, Distance),
		         FMath::IsNearlyEqual(Distance, SegmentLength, Tol));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBridgeDummyCollisionFeedbackTest,
                                 "KawaiiPhysics.Simulation.BridgeDummyCollisionFeedback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBridgeDummyCollisionFeedbackTest::RunTest(const FString& Parameters)
{
	constexpr float Tol = 0.001f;
	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
		const bool bCollisionOnly = (Mode == 0);
		FKawaiiPhysicsTestAccessor A;
		A.BuildSyncBoneSubdivisionFixture();
		A.Node.bBoneSubdivisionCollisionOnly = bCollisionOnly;
		A.Node.BoneConstraintSubdivisionCount = 1;
		A.Node.BoneConstraintSubdivisionFeedbackScale = 1.0f;

		auto AddBridge = [&A](int32 FirstEndpoint, int32 SecondEndpoint, float Alpha, float PushY)
		{
			FKawaiiPhysicsModifyBone Bridge;
			Bridge.bDummy = true;
			Bridge.bBridgeDummy = true;
			Bridge.InterBoneRealParentIndex = FirstEndpoint;
			Bridge.InterBoneRealChildIndex = SecondEndpoint;
			Bridge.InterBoneAlpha = Alpha;
			Bridge.PoseLocation = FVector::ZeroVector;
			Bridge.Location = FVector(0.0f, PushY, 0.0f);
			A.Node.ModifyBones.Add(Bridge);
		};

		// 縦ダミーを端点に持つ橋と、実rootへ重なる別の橋で重みの除数も検証する。
		AddBridge(1, 5, 0.25f, 4.0f);
		AddBridge(0, 5, 0.0f, 8.0f);
		A.CallApplyBridgeDummyCollisionFeedback();

		const float ExpectedRootY = bCollisionOnly ? 9.5f / 1.375f : 8.0f;
		TestTrue(TEXT("real root receives weighted feedback"),
		         FMath::IsNearlyEqual(A.Bone(0).Location.Y, ExpectedRootY, Tol));
		TestTrue(TEXT("real child receives routed feedback only in collision-only mode"),
		         FMath::IsNearlyEqual(A.Bone(2).Location.Y, bCollisionOnly ? 1.5f : 0.0f, Tol));
		TestTrue(TEXT("inter-bone dummy receives feedback only in simulated mode"),
		         FMath::IsNearlyEqual(A.Bone(1).Location.Y, bCollisionOnly ? 0.0f : 3.0f, Tol));
		TestTrue(TEXT("other bridge endpoint keeps its feedback"),
		         FMath::IsNearlyEqual(A.Bone(5).Location.Y, 5.0f, Tol));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSubdivisionCollisionOnlyRestoreTest,
                                 "KawaiiPhysics.Simulation.SubdivisionCollisionOnlyRestore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSubdivisionCollisionOnlyRestoreTest::RunTest(const FString& Parameters)
{
	constexpr float SegmentLength = 12.0f;
	constexpr float TipLength = 6.0f;
	constexpr float Tol = 0.001f;

	for (int32 SubdivisionCount = 1; SubdivisionCount <= 2; ++SubdivisionCount)
	{
		for (int32 Mode = 0; Mode < 2; ++Mode)
		{
			const bool bCollisionOnly = (Mode == 0);
			FKawaiiPhysicsTestAccessor A;
			const int32 ChildIndex = A.BuildSyncBoneSubdivisionLengthFixture(
				SubdivisionCount, SegmentLength, TipLength);
			const int32 TipIndex = A.Num() - 1;
			A.Node.bBoneSubdivisionCollisionOnly = bCollisionOnly;
			A.Bone(0).bSkipSimulate = true;
			A.Bone(ChildIndex).Location = FVector(SegmentLength * 0.1f, 0.0f, 0.0f);
			A.Bone(TipIndex).Location = FVector((SegmentLength + TipLength) * 0.1f, 0.0f, 0.0f);
			for (int32 BoneIndex = 0; BoneIndex < A.Num(); ++BoneIndex)
			{
				FKawaiiPhysicsModifyBone& Bone = A.Bone(BoneIndex);
				if (Bone.bInterBoneDummy)
				{
					Bone.Location = FMath::Lerp(A.Bone(Bone.InterBoneRealParentIndex).Location,
					                            A.Bone(Bone.InterBoneRealChildIndex).Location, Bone.InterBoneAlpha);
				}
			}

			A.CallRestoreBoneLengthsAndLimits();

			if (bCollisionOnly)
			{
				TestTrue(FString::Printf(TEXT("N=%d child returns to full root distance"), SubdivisionCount),
				         A.Bone(ChildIndex).Location.Equals(FVector(SegmentLength, 0.0f, 0.0f), Tol));
				TestTrue(FString::Printf(TEXT("N=%d tip returns to full child distance"), SubdivisionCount),
				         FMath::IsNearlyEqual(FVector::Dist(A.Bone(TipIndex).Location,
				                                          A.Bone(ChildIndex).Location), TipLength, Tol));
				for (int32 BoneIndex = 0; BoneIndex < A.Num(); ++BoneIndex)
				{
					const FKawaiiPhysicsModifyBone& Bone = A.Bone(BoneIndex);
					if (Bone.bInterBoneDummy)
					{
						const FVector Expected = FMath::Lerp(A.Bone(Bone.InterBoneRealParentIndex).Location,
						                                      A.Bone(Bone.InterBoneRealChildIndex).Location, Bone.InterBoneAlpha);
						TestTrue(FString::Printf(TEXT("N=%d dummy %d follows final endpoints"), SubdivisionCount, BoneIndex),
						         Bone.Location.Equals(Expected, Tol));
					}
				}
			}
			else
			{
				const float ExpectedChildX = (SubdivisionCount == 1) ? 0.0f : SegmentLength / 3.0f;
				TestTrue(FString::Printf(TEXT("N=%d simulated dummy retains per-parent child restore"), SubdivisionCount),
				         A.Bone(ChildIndex).Location.Equals(FVector(ExpectedChildX, 0.0f, 0.0f), Tol));
				TestTrue(FString::Printf(TEXT("N=%d simulated dummy retains per-parent tip restore"), SubdivisionCount),
				         A.Bone(TipIndex).Location.Equals(
					 FVector((SubdivisionCount == 1) ? 0.0f : TipLength / 3.0f, 0.0f, 0.0f), Tol));
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
//  パラメータ応答
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsParameterResponseTest,
                                 "KawaiiPhysics.Simulation.ParameterResponse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsParameterResponseTest::RunTest(const FString& Parameters)
{
	const FVector Gravity(-980, 0, 0); // 横方向重力（-X 方向へたわむ）
	const int32 Frames = 600;          // 10s @ 60fps、十分に整定
	const float Dt = 1.0f / 60.0f;

	// --- 重力方向: 横重力で tip は -X へ変位 ---
	const FVector TipSoft = SimulateChainTip(0.1f, 0.02f, Gravity, true, 60, Frames, Dt);
	TestTrue(FString::Printf(TEXT("Gravity deflects tip toward -X: tip=%s"), *TipSoft.ToString()),
	         TipSoft.X < -0.01f);

	// --- 剛性単調性: 剛性が高いほど tip はポーズ(X=0)に近い ---
	const FVector TipStiff = SimulateChainTip(0.1f, 0.5f, Gravity, true, 60, Frames, Dt);
	TestTrue(FString::Printf(TEXT("Higher stiffness => tip closer to pose: soft|X|=%.3f stiff|X|=%.3f"),
	                         FMath::Abs(TipSoft.X), FMath::Abs(TipStiff.X)),
	         FMath::Abs(TipStiff.X) < FMath::Abs(TipSoft.X));

	// --- 減衰オーバーシュート: 減衰が低いほど過渡のピーク変位が大きい ---
	float MaxLowDamp = 0.0f, MaxHighDamp = 0.0f;
	SimulateChainTip(0.02f, 0.05f, Gravity, true, 60, Frames, Dt, &MaxLowDamp);
	SimulateChainTip(0.40f, 0.05f, Gravity, true, 60, Frames, Dt, &MaxHighDamp);
	TestTrue(FString::Printf(TEXT("Lower damping overshoots more: lowDampPeak=%.3f highDampPeak=%.3f"),
	                         MaxLowDamp, MaxHighDamp),
	         MaxLowDamp > MaxHighDamp);

	return true;
}

// ---------------------------------------------------------------------------
//  フレームレート非依存性
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsFramerateIndependenceTest,
                                 "KawaiiPhysics.Simulation.FramerateIndependence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsFramerateIndependenceTest::RunTest(const FString& Parameters)
{
	const FVector Gravity(-980, 0, 0);
	const float SimTime = 2.0f;
	const int32 TargetFps = 60;

	// 同じシミュレーション時間を 30/60/120fps で実行（固定サブステップ ON）。
	const FVector Tip30 = SimulateChainTip(0.1f, 0.05f, Gravity, true, TargetFps,
	                                       FMath::RoundToInt(SimTime * 30.0f), 1.0f / 30.0f);
	const FVector Tip60 = SimulateChainTip(0.1f, 0.05f, Gravity, true, TargetFps,
	                                       FMath::RoundToInt(SimTime * 60.0f), 1.0f / 60.0f);
	const FVector Tip120 = SimulateChainTip(0.1f, 0.05f, Gravity, true, TargetFps,
	                                        FMath::RoundToInt(SimTime * 120.0f), 1.0f / 120.0f);

	const float SubstepTol = 0.5f; // cm。固定サブステップなので僅差に収束するはず。
	TestTrue(FString::Printf(TEXT("Substep 30 vs 60 fps: %s vs %s"), *Tip30.ToString(), *Tip60.ToString()),
	         Tip30.Equals(Tip60, SubstepTol));
	TestTrue(FString::Printf(TEXT("Substep 60 vs 120 fps: %s vs %s"), *Tip60.ToString(), *Tip120.ToString()),
	         Tip60.Equals(Tip120, SubstepTol));

	return true;
}

// ---------------------------------------------------------------------------
//  数値安定性（NaN や発散がないこと）
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsNumericalStabilityTest,
                                 "KawaiiPhysics.Simulation.NumericalStability",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsNumericalStabilityTest::RunTest(const FString& Parameters)
{
	auto RunScenario = [&](const TCHAR* Name, float Spacing, float Damping, float Stiffness,
	                       const FVector& Gravity, bool bFixedSubstep, float FrameDt, int32 Frames)
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(4, Spacing);
		FKawaiiPhysicsSettings S;
		S.Damping = Damping;
		S.Stiffness = Stiffness;
		A.SetAllPhysicsSettings(S);
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(Gravity);
		A.SetFixedSubstepping(bFixedSubstep, 60, 8);
		for (int32 i = 0; i < Frames; ++i)
		{
			A.StepFrame(FrameDt);
		}
		TestTrue(FString::Printf(TEXT("%s: finite"), Name), A.AllFinite());
	};

	// 巨大な dt（spiral of death クランプ確認）
	RunScenario(TEXT("HugeDt"), 10.0f, 0.1f, 0.05f, FVector(0, 0, -980), true, 5.0f, 20);
	// ゼロ長ボーン
	RunScenario(TEXT("ZeroLength"), 0.0f, 0.1f, 0.05f, FVector(0, 0, -980), true, 1.0f / 60.0f, 60);
	// 微小 dt（legacy）
	RunScenario(TEXT("TinyDtLegacy"), 10.0f, 0.1f, 0.05f, FVector(0, 0, -980), false, 1.0e-5f, 60);

	return true;
}

// ---------------------------------------------------------------------------
//  物理設定カーブ評価（全カーブ空の高速パスと per-bone 経路の等価性）
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPhysicsSettingsCurveTest,
                                 "KawaiiPhysics.Simulation.PhysicsSettingsCurvePaths",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPhysicsSettingsCurveTest::RunTest(const FString& Parameters)
{
	constexpr int32 NumBones = 8;

	FKawaiiPhysicsTestAccessor A;
	A.BuildVerticalChain(NumBones, 10.0f);
	for (int32 i = 0; i < NumBones; ++i)
	{
		// per-bone 経路のカーブ評価位置。ハーネスは設定しないためここで振る。
		A.Bone(i).LengthRateFromRoot = static_cast<float>(i) / (NumBones - 1);
	}

	// UpdatePhysicsSettingsOfModifyBones はノードの PhysicsSettings を基準値に使う
	A.Node.PhysicsSettings.Damping = 0.8f;
	A.Node.PhysicsSettings.WorldDampingLocation = 0.6f;
	A.Node.PhysicsSettings.WorldDampingRotation = 0.7f;
	A.Node.PhysicsSettings.Stiffness = 0.9f;
	A.Node.PhysicsSettings.Radius = 3.0f;
	A.Node.PhysicsSettings.LimitAngle = 30.0f;

	// --- 1. 高速パス（全カーブ空・DefaultValue あり）: DefaultValue の乗算とクランプが効く ---
	A.Node.DampingCurveData.EditorCurveData.SetDefaultValue(2.0f);              // 0.8*2.0=1.6 → 上限クランプで 1.0
	A.Node.WorldDampingLocationCurveData.EditorCurveData.SetDefaultValue(0.5f); // 0.6*0.5=0.3
	A.Node.RadiusCurveData.EditorCurveData.SetDefaultValue(-1.0f);              // 3.0*-1.0 → Max で 0.0
	A.CallUpdatePhysicsSettings();
	TArray<FKawaiiPhysicsSettings> FastPathResults;
	for (int32 i = 0; i < NumBones; ++i)
	{
		const FKawaiiPhysicsSettings& S = A.Bone(i).PhysicsSettings;
		TestTrue(FString::Printf(TEXT("FastPath DefaultValue: bone %d damping=%f wdl=%f radius=%f"),
		                         i, S.Damping, S.WorldDampingLocation, S.Radius),
		         S.Damping == 1.0f && S.WorldDampingLocation == 0.3f && S.Radius == 0.0f);
		FastPathResults.Add(S);
	}

	// --- 2. per-bone 経路との等価性: キー付きカーブを1本足して分岐を切り替え、
	//        空カーブ（DefaultValue 含む）の評価結果が高速パスとビット一致することを確認 ---
	A.Node.StiffnessCurveData.EditorCurveData.AddKey(0.0f, 1.0f);
	A.Node.StiffnessCurveData.EditorCurveData.AddKey(1.0f, 0.5f);
	A.CallUpdatePhysicsSettings();
	constexpr float ExpectedStiffness[NumBones] =
	{
		0.9f, 0.8357143f, 0.7714286f, 0.7071429f,
		0.6428571f, 0.5785714f, 0.5142857f, 0.45f
	};
	for (int32 i = 0; i < NumBones; ++i)
	{
		const FKawaiiPhysicsSettings& S = A.Bone(i).PhysicsSettings;
		const FKawaiiPhysicsSettings& F = FastPathResults[i];
		TestTrue(FString::Printf(TEXT("PerBone path matches fast path for empty curves: bone %d"), i),
		         S.Damping == F.Damping && S.WorldDampingLocation == F.WorldDampingLocation &&
		         S.WorldDampingRotation == F.WorldDampingRotation && S.Radius == F.Radius &&
		         S.LimitAngle == F.LimitAngle);

		// キー付きカーブの各ボーンでリテラルの期待値を確認する。
		TestTrue(FString::Printf(TEXT("PerBone stiffness curve: bone %d got=%f expected=%f"),
		                         i, S.Stiffness, ExpectedStiffness[i]),
		         FMath::IsNearlyEqual(S.Stiffness, ExpectedStiffness[i], 0.00001f));
	}
	// per-bone 経路で Stiffness が実際にボーン毎に変化していること（カーブが効いている証拠）
	TestTrue(TEXT("PerBone stiffness varies along the chain"),
	         A.Bone(0).PhysicsSettings.Stiffness != A.Bone(NumBones - 1).PhysicsSettings.Stiffness);

	return true;
}

// ---------------------------------------------------------------------------
//  テレポート時の Component 移動の破棄（固定サブステップの繰り越しで漏れないこと）
// ---------------------------------------------------------------------------
namespace
{
	struct FTeleportRunResult
	{
		// tip のポーズ位置からの最大ずれ（cm）
		float MaxTipDeviation = 0.0f;
		// 瞬間移動したフレーム直後の PreSkelCompTransform
		FTransform PreSkelAfterJump = FTransform::Identity;
	};

	// ComponentSpace の縦チェーン（約140cm）を静止させ、初フレームでコンポーネントを Jump へ瞬間移動させてから 60 フレーム進める。
	// WorldDamping=0 なので反映された移動はそのまま慣性（揺れ）になる。重力なしなので移動が破棄されればチェーンは直立したまま。
	FTeleportRunResult RunComponentJump(bool bFixedSubstep, const FTransform& Jump,
	                                    float DistanceThreshold, float RotationThreshold)
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(6, 28.0f);

		FKawaiiPhysicsSettings S;
		S.Damping = 0.1f;
		S.Stiffness = 0.05f;
		S.WorldDampingLocation = 0.0f;
		S.WorldDampingRotation = 0.0f;
		S.LimitAngle = 0.0f;
		S.Radius = 0.0f;
		A.SetAllPhysicsSettings(S);

		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector::ZeroVector);
		A.SetFixedSubstepping(bFixedSubstep, 60, 8);
		A.Node.TeleportDistanceThreshold = DistanceThreshold;
		A.Node.TeleportRotationThreshold = RotationThreshold;
		A.SetPreSkelCompTransform(FTransform::Identity);

		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);

		// 60Hz 固定ステップに対し 1.5 ステップ分の dt。初フレームは 1 ステップだけ消費し、移動の 1/3 を繰り越す。
		const float FrameDt = 1.0f / 40.0f;
		FTeleportRunResult Result;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			A.StepFrameWithComponentTransform(Output, FrameDt, Jump);
			if (Frame == 0)
			{
				Result.PreSkelAfterJump = A.GetPreSkelCompTransform();
			}
			const FKawaiiPhysicsModifyBone& Tip = A.Bone(A.Num() - 1);
			Result.MaxTipDeviation = FMath::Max(Result.MaxTipDeviation,
			                                    static_cast<float>((Tip.Location - Tip.PoseLocation).Size()));
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTeleportDiscardsComponentMoveTest,
                                 "KawaiiPhysics.Simulation.TeleportDiscardsComponentMove",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTeleportDiscardsComponentMoveTest::RunTest(const FString& Parameters)
{
	const FTransform Translate120(FVector(0.0f, 120.0f, 0.0f));
	const FTransform RotateX25(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(25.0f)));
	const float RigidTol = 0.01f;   // cm。破棄されていればポーズから動かない
	const float SwingMin = 5.0f;    // cm。反映されていれば明確に揺れる

	struct FCase
	{
		const TCHAR* Name;
		bool bFixedSubstep;
	};
	const FCase Cases[] = {{TEXT("FixedSubstep"), true}, {TEXT("Legacy"), false}};

	for (const FCase& Case : Cases)
	{
		// 距離テレポート（120cm > 閾値50cm）: 移動を全量破棄し、繰り越し分も漏れない
		{
			const FTeleportRunResult R = RunComponentJump(Case.bFixedSubstep, Translate120, 50.0f, 0.0f);
			TestTrue(FString::Printf(TEXT("[%s] distance teleport keeps chain rigid: maxDev=%.4f"),
			                         Case.Name, R.MaxTipDeviation),
			         R.MaxTipDeviation < RigidTol);
			TestTrue(FString::Printf(TEXT("[%s] distance teleport advances PreSkelCompTransform fully: %s"),
			                         Case.Name, *R.PreSkelAfterJump.GetLocation().ToString()),
			         R.PreSkelAfterJump.GetLocation().Equals(Translate120.GetLocation(), KINDA_SMALL_NUMBER));
		}

		// 回転テレポート（25° > 閾値10°、繰り越し分 8.3° は閾値未満）
		{
			const FTeleportRunResult R = RunComponentJump(Case.bFixedSubstep, RotateX25, 0.0f, 10.0f);
			TestTrue(FString::Printf(TEXT("[%s] rotation teleport keeps chain rigid: maxDev=%.4f"),
			                         Case.Name, R.MaxTipDeviation),
			         R.MaxTipDeviation < RigidTol);
			TestTrue(FString::Printf(TEXT("[%s] rotation teleport advances PreSkelCompTransform fully"), Case.Name),
			         R.PreSkelAfterJump.GetRotation().Equals(RotateX25.GetRotation(), KINDA_SMALL_NUMBER));
		}

		// 対照: 閾値未満の移動は従来どおり反映されて揺れる
		{
			const FTeleportRunResult R = RunComponentJump(Case.bFixedSubstep, Translate120, 300.0f, 0.0f);
			TestTrue(FString::Printf(TEXT("[%s] sub-threshold move still swings: maxDev=%.4f"),
			                         Case.Name, R.MaxTipDeviation),
			         R.MaxTipDeviation > SwingMin);
			// 非テレポート時の繰り越しは従来どおり（固定サブステップは 1/1.5 ステップ分だけ前進 → Y=80）
			const float ExpectedY = Case.bFixedSubstep ? 80.0f : 120.0f;
			TestTrue(FString::Printf(TEXT("[%s] sub-threshold move keeps carry-over: PreSkel.Y=%.4f expected=%.4f"),
			                         Case.Name, R.PreSkelAfterJump.GetLocation().Y, ExpectedY),
			         FMath::IsNearlyEqual(static_cast<float>(R.PreSkelAfterJump.GetLocation().Y), ExpectedY, 0.01f));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
