// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"

// 物理計算の回帰テスト（Output を引数に取らない物理計算関数を直接呼ぶ）。
// Regression tests for the physics math (calling the no-Output physics functions directly).
//   - 決定性 / determinism
//   - パラメータ応答（重力方向・剛性単調性・減衰オーバーシュート） / parameter response
//   - フレームレート非依存性 / frame-rate independence
//   - 数値安定性（NaN/発散なし） / numerical stability

namespace
{
	// 標準の縦チェーン構成でシミュレーションし、最終 tip 位置を返す。
	// Simulate a standard vertical chain and return the final tip location.
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
//  決定性 / Determinism
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
//  抽出した物理計算関数の検証（解析的） / Verify the extracted physics functions (analytic)
//  抽出した経路（速度寄与(wind)・legacy gravity・simple external force）を直接検証する。
//  Directly verifies the extracted code path: velocity contribution (wind), legacy gravity, simple external force.
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
		// この Velocity が Simulate() で ApplyToVelocity に渡る「実速度」（減衰+wind+重力）。公開フックの
		// 契約を固定する（以前ここに wind だけの値が渡る退行があった）。Simulate() の呼び出し配線自体は
		// Output 依存のため headless 対象外。
		// This Velocity is what Simulate() hands to ApplyToVelocity (damped+wind+gravity). Pin the public-hook
		// contract (a prior regression passed wind only). The Simulate() call wiring itself is Output-dependent
		// and out of scope for the headless harness.
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
		// In legacy mode gravity goes to position, so the hook receives damped+wind only (no gravity).
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
//  パラメータ応答 / Parameter response
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsParameterResponseTest,
                                 "KawaiiPhysics.Simulation.ParameterResponse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsParameterResponseTest::RunTest(const FString& Parameters)
{
	const FVector Gravity(-980, 0, 0); // 横方向重力 / sideways gravity (deflects toward -X)
	const int32 Frames = 600;          // 10s @ 60fps、十分に整定 / settle
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
	// 初回実行で出力された値を下のハード assert に固定する。まずは値をログ出力。
	// Snapshot baseline: capture the value from the first run, then harden into the assert below.
	const FVector Canonical = SimulateChainTip(0.1f, 0.05f, Gravity, true, 60, Frames, Dt);
	AddInfo(FString::Printf(TEXT("[SNAPSHOT] ParameterResponse canonical tip = %s"), *Canonical.ToString()));
	// スナップショット・基準値（2026-06-08, UE5.7 で捕捉）。物理挙動が変わるとここで検出される。
	// Snapshot baseline captured 2026-06-08 on UE5.7. Any change in core physics behavior trips this.
	const FVector CanonicalBaseline(-13.782f, 0.0f, -26.647f);
	TestTrue(FString::Printf(TEXT("Canonical tip snapshot: got %s expected %s"),
	                         *Canonical.ToString(), *CanonicalBaseline.ToString()),
	         Canonical.Equals(CanonicalBaseline, 0.1f));

	return true;
}

// ---------------------------------------------------------------------------
//  フレームレート非依存性 / Frame-rate independence
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
	// Same sim time at 30/60/120 fps with fixed substepping ON.
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

	// 対比: サブステップ OFF（legacy）では 30fps と 120fps の差が大きい（フレームレート依存の症状）。
	// Contrast: with substepping OFF, 30 vs 120 fps diverge much more (the frame-rate-dependence symptom).
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
//  数値安定性 / Numerical stability (no NaN / no blow-up)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsNumericalStabilityTest,
                                 "KawaiiPhysics.Simulation.NumericalStability",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsNumericalStabilityTest::RunTest(const FString& Parameters)
{
	// 縦チェーンは長さ復元により root から最大でも (segment*count) 内に収まる。
	// The chain stays bounded by total length thanks to length restoration.
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

	// 極端な重力 / extreme gravity
	RunScenario(TEXT("ExtremeGravity"), 10.0f, 0.1f, 0.05f, FVector(0, 0, -1.0e6f), true, 1.0f / 60.0f, 120);
	// 巨大な dt（spiral of death クランプ確認）/ huge dt
	RunScenario(TEXT("HugeDt"), 10.0f, 0.1f, 0.05f, FVector(0, 0, -980), true, 5.0f, 20);
	// ゼロ長ボーン / zero-length bones
	RunScenario(TEXT("ZeroLength"), 0.0f, 0.1f, 0.05f, FVector(0, 0, -980), true, 1.0f / 60.0f, 60);
	// 減衰=1, 剛性=1（境界値）/ full damping & stiffness
	RunScenario(TEXT("FullDampingStiffness"), 10.0f, 1.0f, 1.0f, FVector(0, -980, 0), true, 1.0f / 60.0f, 60);
	// 微小 dt（legacy）/ tiny dt (legacy path)
	RunScenario(TEXT("TinyDtLegacy"), 10.0f, 0.1f, 0.05f, FVector(0, 0, -980), false, 1.0e-5f, 60);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
