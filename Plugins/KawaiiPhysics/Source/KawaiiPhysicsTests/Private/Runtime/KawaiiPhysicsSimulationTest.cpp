// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"

// 物理計算の回帰テスト（Output 非依存の物理関数を直接呼ぶ）：決定性／パラメータ応答（重力方向・剛性単調性・減衰オーバーシュート）／フレームレート非依存性／数値安定性。

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

	// 横に並んだ2本のチェーンの tip 間に BoneConstraint を張り、最終距離を返す。
	float SimulateLateralConstraintDistance(int32 NumFrames, float FrameDt)
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildTwoVerticalChains(2, 10.0f, 20.0f);

		FKawaiiPhysicsSettings S;
		S.Damping = 1.0f;
		S.Stiffness = 0.0f;
		S.LimitAngle = 0.0f;
		S.Radius = 0.0f;
		A.SetAllPhysicsSettings(S);

		const int32 LeftTipIndex = 1;
		const int32 RightTipIndex = 3;
		const FVector StretchedRightTip = A.Bone(RightTipIndex).Location + FVector(8.0f, 0.0f, 0.0f);
		A.Bone(RightTipIndex).Location = StretchedRightTip;
		A.Bone(RightTipIndex).PrevLocation = StretchedRightTip;

		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector::ZeroVector);
		A.SetFixedSubstepping(true, 60, 8);
		A.SetBoneConstraintIterations(1, 1);
		A.SetBoneConstraintGlobalComplianceType(EXPBDComplianceType::Fat);
		A.AddRuntimeBoneConstraint(LeftTipIndex, RightTipIndex, 20.0f);

		A.StepFrames(NumFrames, FrameDt);
		return static_cast<float>((A.Bone(RightTipIndex).Location - A.Bone(LeftTipIndex).Location).Size());
	}
}

// ---------------------------------------------------------------------------
//  決定性
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsDeterminismTest,
                                 "KawaiiPhysics.Simulation.Determinism",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsDeterminismTest::RunTest(const FString& Parameters)
{
	const FVector Gravity(-980, 0, 0);
	const FVector A = SimulateChainTip(0.1f, 0.05f, Gravity, true, 60, 200, 1.0f / 60.0f);
	const FVector B = SimulateChainTip(0.1f, 0.05f, Gravity, true, 60, 200, 1.0f / 60.0f);

	TestTrue(FString::Printf(TEXT("Determinism: A=%s B=%s"), *A.ToString(), *B.ToString()),
	         A.Equals(B, KINDA_SMALL_NUMBER));
	return true;
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
	const float LegacyFrameDtDistance = SolveOnceDistance(1.0f / 30.0f, false, 1.0f / 30.0f);

	const float Compliance = 0.0001f / (FixedDt * FixedDt); // EXPBDComplianceType::Fat
	const float ExpectedDistance = 20.0f - 2.0f * (10.0f / (2.0f + Compliance));
	TestTrue(FString::Printf(TEXT("Substep BoneConstraint uses StepDt: got %.6f expected %.6f"),
	                         SubstepDistance, ExpectedDistance),
	         FMath::IsNearlyEqual(SubstepDistance, ExpectedDistance, 0.0001f));
	TestTrue(FString::Printf(TEXT("Substep FrameDt=1/30 matches legacy StepDt=1/60: %.6f vs %.6f"),
	                         SubstepDistance, LegacySameStepDistance),
	         FMath::IsNearlyEqual(SubstepDistance, LegacySameStepDistance, 0.0001f));
	TestTrue(FString::Printf(TEXT("Regression guard: FrameDt-normalized solve would differ: %.6f vs %.6f"),
	                         SubstepDistance, LegacyFrameDtDistance),
	         FMath::Abs(SubstepDistance - LegacyFrameDtDistance) > 0.5f);

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBoneSubdivisionRigidTranslationTest,
                                 "KawaiiPhysics.Simulation.SyncBoneSubdivisionRigidTranslation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBoneSubdivisionRigidTranslationTest::RunTest(const FString& Parameters)
{
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

	// 後段の dummy 再補間で inter-bone dummy も剛体並進位置へ戻る
	A.CallUpdateSubdivisionDummyPoseAfterSyncBones();
	TestTrue(FString::Printf(TEXT("Inter-bone dummy re-lerps to rigid position: %s"),
	                         *A.Bone(1).PoseLocation.ToString()),
	         A.Bone(1).PoseLocation.Equals(FVector(5.0f, 10.0f, 0.0f), Tol));

	return true;
}

// ---------------------------------------------------------------------------
//  SyncBone + BoneSubdivision: 非剛体（root/child で delta が異なる）でも segment 長を保存
//  (stale inter-bone dummy ではなく実祖父基準・距離 BoneLength/(1-alpha) で拘束する)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBoneSubdivisionLengthPreservedTest,
                                 "KawaiiPhysics.Simulation.SyncBoneSubdivisionLengthPreserved",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBoneSubdivisionLengthPreservedTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	A.BuildSyncBoneSubdivisionFixture();

	FKawaiiPhysicsSyncTargetRoot TargetRoot = A.CollectSyncChildTargetsForRoot(0);

	// root と child(2) に異なる delta（attenuation/curve 相当）。stale dummy 基準だと segment が伸縮するが、
	// 実祖父 gp(=root) 基準・距離 BoneLength/(1-alpha)=10 の拘束で gp→child 長は保たれるはず。
	A.ApplySyncTargetsForRootSplit(TargetRoot, FVector(0.0f, 10.0f, 0.0f), FVector(0.0f, 5.0f, 0.0f));

	const float RestLen = 10.0f; // |child(10,0,0) - root(0,0,0)| = BoneLength(5) / (1 - alpha 0.5)
	const float Len = FVector::Dist(A.Bone(2).PoseLocation, A.Bone(0).PoseLocation);
	TestTrue(FString::Printf(TEXT("Non-rigid sync preserves grandparent->child length: %.4f (expect %.1f) child=%s"),
	                         Len, RestLen, *A.Bone(2).PoseLocation.ToString()),
	         FMath::IsNearlyEqual(Len, RestLen, 0.001f));

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

	// --- スナップショット基準値（現状の挙動を固定） ---
	const FVector Canonical = SimulateChainTip(0.1f, 0.05f, Gravity, true, 60, Frames, Dt);
	AddInfo(FString::Printf(TEXT("[SNAPSHOT] ParameterResponse canonical tip = %s"), *Canonical.ToString()));
	// 基準値（2026-06-08, UE5.7 で捕捉）。物理挙動が変わるとここで検出される。
	const FVector CanonicalBaseline(-13.782f, 0.0f, -26.647f);
	TestTrue(FString::Printf(TEXT("Canonical tip snapshot: got %s expected %s"),
	                         *Canonical.ToString(), *CanonicalBaseline.ToString()),
	         Canonical.Equals(CanonicalBaseline, 0.1f));

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

	const float Constraint30 = SimulateLateralConstraintDistance(FMath::RoundToInt(SimTime * 30.0f), 1.0f / 30.0f);
	const float Constraint60 = SimulateLateralConstraintDistance(FMath::RoundToInt(SimTime * 60.0f), 1.0f / 60.0f);
	const float Constraint120 = SimulateLateralConstraintDistance(FMath::RoundToInt(SimTime * 120.0f), 1.0f / 120.0f);
	const float ConstraintTol = 0.01f;
	TestTrue(FString::Printf(TEXT("BoneConstraint substep 30 vs 60 fps: %.6f vs %.6f"),
	                         Constraint30, Constraint60),
	         FMath::IsNearlyEqual(Constraint30, Constraint60, ConstraintTol));
	TestTrue(FString::Printf(TEXT("BoneConstraint substep 60 vs 120 fps: %.6f vs %.6f"),
	                         Constraint60, Constraint120),
	         FMath::IsNearlyEqual(Constraint60, Constraint120, ConstraintTol));

	// 対比: サブステップ OFF（legacy）では 30fps と 120fps の差が大きい（フレームレート依存の症状）。
	const FVector Legacy30 = SimulateChainTip(0.1f, 0.05f, Gravity, false, TargetFps,
	                                          FMath::RoundToInt(SimTime * 30.0f), 1.0f / 30.0f);
	const FVector Legacy120 = SimulateChainTip(0.1f, 0.05f, Gravity, false, TargetFps,
	                                           FMath::RoundToInt(SimTime * 120.0f), 1.0f / 120.0f);

	const float SubstepSpread = static_cast<float>((Tip30 - Tip120).Size());
	const float LegacySpread = static_cast<float>((Legacy30 - Legacy120).Size());
	AddInfo(FString::Printf(TEXT("[framerate] substepSpread=%.4f legacySpread=%.4f"), SubstepSpread, LegacySpread));
	TestTrue(FString::Printf(TEXT("Substepping reduces frame-rate dependence: substep=%.4f < legacy=%.4f"),
	                         SubstepSpread, LegacySpread),
	         SubstepSpread < LegacySpread);

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
	// 縦チェーンは長さ復元により root から最大でも (segment*count) 内に収まる。
	const float Bound = 1000.0f;

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
		TestTrue(FString::Printf(TEXT("%s: bounded (|loc| <= %.0f)"), Name, Bound), A.AllWithin(Bound));
	};

	// 極端な重力
	RunScenario(TEXT("ExtremeGravity"), 10.0f, 0.1f, 0.05f, FVector(0, 0, -1.0e6f), true, 1.0f / 60.0f, 120);
	// 巨大な dt（spiral of death クランプ確認）
	RunScenario(TEXT("HugeDt"), 10.0f, 0.1f, 0.05f, FVector(0, 0, -980), true, 5.0f, 20);
	// ゼロ長ボーン
	RunScenario(TEXT("ZeroLength"), 0.0f, 0.1f, 0.05f, FVector(0, 0, -980), true, 1.0f / 60.0f, 60);
	// 減衰=1, 剛性=1（境界値）
	RunScenario(TEXT("FullDampingStiffness"), 10.0f, 1.0f, 1.0f, FVector(0, -980, 0), true, 1.0f / 60.0f, 60);
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

	// --- 1. 高速パス（全カーブ空・DefaultValue なし）: 基準値がそのまま入る ---
	A.CallUpdatePhysicsSettings();
	for (int32 i = 0; i < NumBones; ++i)
	{
		const FKawaiiPhysicsSettings& S = A.Bone(i).PhysicsSettings;
		TestTrue(FString::Printf(TEXT("FastPath base: bone %d"), i),
		         S.Damping == 0.8f && S.WorldDampingLocation == 0.6f && S.WorldDampingRotation == 0.7f &&
		         S.Stiffness == 0.9f && S.Radius == 3.0f && S.LimitAngle == 30.0f);
	}

	// --- 2. 高速パス（全カーブ空・DefaultValue あり）: DefaultValue の乗算とクランプが効く ---
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

	// --- 3. per-bone 経路との等価性: キー付きカーブを1本足して分岐を切り替え、
	//        空カーブ（DefaultValue 含む）の評価結果が高速パスとビット一致することを確認 ---
	A.Node.StiffnessCurveData.EditorCurveData.AddKey(0.0f, 1.0f);
	A.Node.StiffnessCurveData.EditorCurveData.AddKey(1.0f, 0.5f);
	A.CallUpdatePhysicsSettings();
	for (int32 i = 0; i < NumBones; ++i)
	{
		const FKawaiiPhysicsSettings& S = A.Bone(i).PhysicsSettings;
		const FKawaiiPhysicsSettings& F = FastPathResults[i];
		TestTrue(FString::Printf(TEXT("PerBone path matches fast path for empty curves: bone %d"), i),
		         S.Damping == F.Damping && S.WorldDampingLocation == F.WorldDampingLocation &&
		         S.WorldDampingRotation == F.WorldDampingRotation && S.Radius == F.Radius &&
		         S.LimitAngle == F.LimitAngle);

		// キー付きカーブは LengthRateFromRoot 位置の評価値が乗る
		const float ExpectedStiffness = FMath::Clamp(
			0.9f * A.Node.StiffnessCurveData.EditorCurveData.Eval(A.Bone(i).LengthRateFromRoot, 1.0f), 0.0f, 1.0f);
		TestTrue(FString::Printf(TEXT("PerBone stiffness curve: bone %d got=%f expected=%f"),
		                         i, S.Stiffness, ExpectedStiffness),
		         S.Stiffness == ExpectedStiffness);
	}
	// per-bone 経路で Stiffness が実際にボーン毎に変化していること（カーブが効いている証拠）
	TestTrue(TEXT("PerBone stiffness varies along the chain"),
	         A.Bone(0).PhysicsSettings.Stiffness != A.Bone(NumBones - 1).PhysicsSettings.Stiffness);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
