// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"

namespace
{
// 本テストは積分の蓄積誤差を持たない解析式（sin等）を直接評価するため、既定許容誤差はfloatの丸め程度で足りる
constexpr float GProceduralWindTol = KINDA_SMALL_NUMBER;

// Sample の全フィールドが完全一致するかを判定するヘルパー（同一seedのサンプル比較に使用）
bool SamplesExactlyEqual(const FKawaiiPhysicsProceduralWindSample& A,
                         const FKawaiiPhysicsProceduralWindSample& B)
{
	return A.Constant == B.Constant &&
		A.Sway == B.Sway &&
		A.Ripple == B.Ripple &&
		A.StrengthCycle == B.StrengthCycle &&
		A.Random == B.Random &&
		A.Gust == B.Gust &&
		A.Total == B.Total;
}

// 近似比較用アサーションヘルパー。失敗時に実測値と期待値を併記して原因調査しやすくする
void TestSampleNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected,
                    const float Tol = GProceduralWindTol)
{
	Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	              FMath::IsNearlyEqual(Actual, Expected, Tol));
}

// DynamicParams の bOverride フラグ数を数え、単一プロパティ/全項目スナップショットの検証に使う
int32 CountDynamicParamOverrideFlags(const FKawaiiProceduralWindDynamicParams& Params)
{
	return
		(Params.bOverrideIsEnabled ? 1 : 0) +
		(Params.bOverrideWindDirection ? 1 : 0) +
		(Params.bOverrideConstantForce ? 1 : 0) +
		(Params.bOverrideSwayForce ? 1 : 0) +
		(Params.bOverrideSwayPeriod ? 1 : 0) +
		(Params.bOverrideSwayPhaseOffset ? 1 : 0) +
		(Params.bOverrideRippleForce ? 1 : 0) +
		(Params.bOverrideRipplePeriod ? 1 : 0) +
		(Params.bOverrideRipplePhaseOffset ? 1 : 0) +
		(Params.bOverrideRippleTipPhaseDelay ? 1 : 0) +
		(Params.bOverrideStrengthCycleRange ? 1 : 0) +
		(Params.bOverrideStrengthCyclePeriod ? 1 : 0) +
		(Params.bOverrideStrengthCyclePhaseOffset ? 1 : 0) +
		(Params.bOverrideRandomForce ? 1 : 0) +
		(Params.bOverrideRandomForcePeriod ? 1 : 0) +
		(Params.bOverrideWindDirectionNoiseAngle ? 1 : 0) +
		(Params.bOverrideWindDirectionNoisePeriod ? 1 : 0) +
		(Params.bOverrideTimeScale ? 1 : 0);
}

// Rate=[0,1]をNumSegments分割で走査し、Rippleが最大となる区間インデックスを求める（RipplePropagationテストでピーク位置の移動検出に使用）
int32 FindRipplePeakIndex(const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind, const float Time,
                        const int32 NumSegments)
{
	int32 PeakIndex = 0;
	float PeakValue = -FLT_MAX;
	for (int32 Index = 0; Index <= NumSegments; ++Index)
	{
		const float Rate = static_cast<float>(Index) / static_cast<float>(NumSegments);
		const float Ripple = Wind.ComputeWindSample(Time, Rate).Ripple;
		if (Ripple > PeakValue)
		{
			PeakValue = Ripple;
			PeakIndex = Index;
		}
	}
	return PeakIndex;
}

// RandomizedForceScaleはprotectedかつ既定0.0fで、通常はPreApplyのランダム化処理でのみ設定される。
// Apply()を直接呼ぶテストではPreApplyを経由しないため、テスト用サブクラスで固定値を注入する
struct FKawaiiPhysicsProceduralWindApplyTestForce : FKawaiiPhysics_ExternalForce_ProceduralWind
{
	void SetRandomizedForceScaleForTest(const float InScale)
	{
		RandomizedForceScale = InScale;
	}

	float GetRandomizedForceScaleForTest() const
	{
		return RandomizedForceScale;
	}

	void SetSupportsRandomForceScaleRangeForTest(const bool bInSupports)
	{
		bSupportsRandomForceScaleRange = bInSupports;
	}
};

// PreApplyが行うキャッシュ更新（合成波・StrengthCycle・random・gust・風向ベクトル）を、ポーズ評価なしで手動再現するヘルパー
void PrimeApplyCache(FKawaiiPhysicsProceduralWindApplyTestForce& Wind, const float Time)
{
	Wind.ResetRuntimeState();
	const FKawaiiPhysicsProceduralWindSample Sample = Wind.ComputeWindSample(Time, 0.0f);
	Wind.RuntimeState->Time = Time;
	Wind.RuntimeState->CachedSinesWithoutRipple = Sample.Constant + Sample.Sway;
	Wind.RuntimeState->CachedStrengthCycle = Sample.StrengthCycle;
	Wind.RuntimeState->CachedRandom = Sample.Random;
	Wind.RuntimeState->CachedGust = Sample.Gust;
	Wind.RuntimeState->CachedWindVector = Wind.WindDirection.GetSafeNormal();
	Wind.SetRandomizedForceScaleForTest(1.0f);
}

// 2ボーンチェーンへ同一フレーム分のWindを1回適用/NumSubsteps回に分割適用し、フレーム末の累積変位を返す
// （FramerateIndependenceテストでsubstep分割数への非依存性を検証するために使用。
//  InRandomizedForceScale は基底ランダム倍率の無視検証用で、Apply が参照しないことを確認する）
FVector ApplyProceduralWindDisplacement(const int32 NumSubsteps, const float InRandomizedForceScale = 1.0f)
{
	const float TotalDt = 1.0f / 30.0f;
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f, FVector::ZeroVector, FVector(0.0f, 0.0f, -1.0f));
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.Bone(1).LengthRateFromRoot = 0.75f;

	FKawaiiPhysicsProceduralWindApplyTestForce Wind;
	Wind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	Wind.WindDirection = FVector::ForwardVector;
	Wind.ConstantForce = 3.0f;
	Wind.SwayForce = 2.0f;
	Wind.SwayPeriod = 0.5f;
	Wind.RippleForce = 4.0f;
	Wind.RipplePeriod = 0.75f;
	Wind.RippleTipPhaseDelay = 120.0f;
	Wind.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	PrimeApplyCache(Wind, TotalDt);
	Wind.SetRandomizedForceScaleForTest(InRandomizedForceScale);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	const FVector InitialLocation = Accessor.Bone(1).Location;
	if (NumSubsteps == 1)
	{
		Accessor.SetTimeState(TotalDt, TotalDt);
		Wind.Apply(Accessor.Bone(1), Accessor.Node, PoseContext);
	}
	else
	{
		const float StepDt = TotalDt / static_cast<float>(NumSubsteps);
		for (int32 Index = 0; Index < NumSubsteps; ++Index)
		{
			Accessor.SetSubstepTimeState(TotalDt, StepDt);
			Wind.Apply(Accessor.Bone(1), Accessor.Node, PoseContext);
		}
	}

	return Accessor.Bone(1).Location - InitialLocation;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindIgnoresBaseRandomScaleTest,
                                 "KawaiiPhysics.ProceduralWind.IgnoresBaseRandomForceScale",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindIgnoresBaseRandomScaleTest::RunTest(const FString& Parameters)
{
	// 基底の RandomizedForceScale（RandomForceScaleRange 由来）は ProceduralWind では無視される契約。
	// 倍率 1.0 と 2.0 で Apply の変位が完全一致することを確認する（ランダム性は Seed 系列に一本化）
	const FVector BaselineDisplacement = ApplyProceduralWindDisplacement(1, 1.0f);
	const FVector ScaledDisplacement = ApplyProceduralWindDisplacement(1, 2.0f);

	bool bOk = true;
	bOk &= TestEqual(TEXT("RandomizedForceScale=2.0 でも変位が不変（X）"),
	                 ScaledDisplacement.X, BaselineDisplacement.X);
	bOk &= TestEqual(TEXT("RandomizedForceScale=2.0 でも変位が不変（Y）"),
	                 ScaledDisplacement.Y, BaselineDisplacement.Y);
	bOk &= TestEqual(TEXT("RandomizedForceScale=2.0 でも変位が不変（Z）"),
	                 ScaledDisplacement.Z, BaselineDisplacement.Z);
	bOk &= TestTrue(TEXT("変位自体はゼロでない（風が適用されている）"),
	                !BaselineDisplacement.IsNearlyZero());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindPreApplyNoGlobalRandomTest,
                                 "KawaiiPhysics.ProceduralWind.PreApplyConsumesNoGlobalRandom",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindPreApplyNoGlobalRandomTest::RunTest(const FString& Parameters)
{
	// bSupportsRandomForceScaleRange=false（ProceduralWind既定）の PreApply はグローバル乱数を消費せず、
	// RandomizedForceScale は1固定になる契約。外力の追加/削除/並び替えが他外力の乱数列に影響しないことを保証する
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f, FVector::ZeroVector, FVector(0.0f, 0.0f, -1.0f));
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetTimeState(1.0f / 30.0f, 1.0f / 30.0f);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	// 基準: 固定シード直後に得られる乱数値
	FMath::RandInit(20260818);
	const int32 ExpectedNext = FMath::Rand();

	// 本命: PreApply（フラグfalse）を挟んでも乱数列が進まない
	FKawaiiPhysicsProceduralWindApplyTestForce Wind;
	Wind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	FMath::RandInit(20260818);
	Wind.PreApply(Accessor.Node, PoseContext);
	const int32 ActualNext = FMath::Rand();

	bool bOk = true;
	bOk &= TestEqual(TEXT("PreApply がグローバル乱数を消費しない"), ActualNext, ExpectedNext);
	bOk &= TestEqual(TEXT("RandomizedForceScale は1固定"), Wind.GetRandomizedForceScaleForTest(), 1.0f);

	// 対照: フラグtrueに戻すと従来どおり乱数列が進む（RandRangeが1回消費する）
	FKawaiiPhysicsProceduralWindApplyTestForce SupportedWind;
	SupportedWind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	SupportedWind.SetSupportsRandomForceScaleRangeForTest(true);
	FMath::RandInit(20260818);
	SupportedWind.PreApply(Accessor.Node, PoseContext);
	const int32 AdvancedNext = FMath::Rand();
	bOk &= TestNotEqual(TEXT("フラグtrueでは乱数列が進む（対照）"), AdvancedNext, ExpectedNext);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindDeterminismTest,
                                 "KawaiiPhysics.ProceduralWind.Determinism",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindDeterminismTest::RunTest(const FString& Parameters)
{
	// 同じ入力列では完全一致し、シード変更でランダム成分が変わることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind A;
	FKawaiiPhysics_ExternalForce_ProceduralWind B;
	FKawaiiPhysics_ExternalForce_ProceduralWind C;
	A.RandomForce = 3.0f;
	B.RandomForce = 3.0f;
	C.RandomForce = 3.0f;
	A.RandomForcePeriod = 0.37f;
	B.RandomForcePeriod = 0.37f;
	C.RandomForcePeriod = 0.37f;
	A.Seed = 12345;
	B.Seed = 12345;
	C.Seed = 54321;

	// 64点の時刻でサンプルを取り、同一seed(A/B)の完全一致と異seed(C)とのRandom成分の差分有無を集計する
	bool bDifferentSeedChangedRandom = false;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const float Time = static_cast<float>(Index) * 0.071f;
		const FKawaiiPhysicsProceduralWindSample SampleA = A.ComputeWindSample(Time, 0.25f);
		const FKawaiiPhysicsProceduralWindSample SampleB = B.ComputeWindSample(Time, 0.25f);
		const FKawaiiPhysicsProceduralWindSample SampleC = C.ComputeWindSample(Time, 0.25f);
		TestTrue(FString::Printf(TEXT("Same seed sample %d"), Index), SamplesExactlyEqual(SampleA, SampleB));
		bDifferentSeedChangedRandom |= SampleA.Random != SampleC.Random;
	}

	TestTrue(TEXT("Different seed changes at least one Random component"), bDifferentSeedChangedRandom);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindConstantOnlyTest,
                                 "KawaiiPhysics.ProceduralWind.ConstantOnly",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindConstantOnlyTest::RunTest(const FString& Parameters)
{
	// 定常風だけが有効な場合、時刻に関係なく合計が定常風と一致することを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.ConstantForce = 7.25f;

	for (const float Time : {0.0f, 0.1f, 0.5f, 1.0f, 3.75f})
	{
		const FKawaiiPhysicsProceduralWindSample Sample = Wind.ComputeWindSample(Time, 0.5f);
		TestSampleNear(*this, TEXT("Constant"), Sample.Constant, Wind.ConstantForce);
		TestSampleNear(*this, TEXT("Total"), Sample.Total, Wind.ConstantForce);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindSinePhaseTest,
                                 "KawaiiPhysics.ProceduralWind.SinePhase",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindSinePhaseTest::RunTest(const FString& Parameters)
{
	// 一斉揺れ(Sway)と波揺れ(Ripple)の位相、および周期性が実装式どおりになることを確認する。
	const float Period = 2.0f;

	// 一斉揺れ(Sway)成分: 1/4周期でsin位相がpi/2となり振幅そのものが出力される点、および1周期後の値が一致する周期性を確認
	FKawaiiPhysics_ExternalForce_ProceduralWind SwayWind;
	SwayWind.SwayForce = 1.0f;
	SwayWind.SwayPeriod = Period;
	TestSampleNear(*this, TEXT("Sway P/4"), SwayWind.ComputeWindSample(Period * 0.25f).Sway,
	               1.0f);

	const float Time = 0.37f;
	TestSampleNear(*this, TEXT("Sway periodicity"),
	               SwayWind.ComputeWindSample(Time).Sway,
	               SwayWind.ComputeWindSample(Time + Period).Sway, 0.000001f);

	// SwayPhaseOffset: 既定0では従来式（オフセット無し）と一致し、非0では位相がシフトすることを確認
	FKawaiiPhysics_ExternalForce_ProceduralWind SwayPhaseWind;
	SwayPhaseWind.SwayForce = 1.0f;
	SwayPhaseWind.SwayPeriod = Period;
	TestSampleNear(*this, TEXT("SwayPhaseOffset=0 matches legacy"),
	               SwayPhaseWind.ComputeWindSample(Time).Sway,
	               SwayWind.ComputeWindSample(Time).Sway);

	// 90度オフセットで t=0 に sin(pi/2)=1（振幅最大）となり、1/4周期の時間シフトと等価になることを確認
	SwayPhaseWind.SwayPhaseOffset = 90.0f;
	TestSampleNear(*this, TEXT("SwayPhaseOffset=90 at t=0"),
	               SwayPhaseWind.ComputeWindSample(0.0f).Sway, 1.0f);
	TestSampleNear(*this, TEXT("SwayPhaseOffset equals time shift"),
	               SwayPhaseWind.ComputeWindSample(Time).Sway,
	               SwayWind.ComputeWindSample(Time + Period * 0.25f).Sway, 0.000001f);

	// 波揺れ(Ripple)成分: RipplePhaseOffset=90度によりt=0・Rate=0で位相pi/2（振幅最大）となる設定で、位相と周期性を確認
	FKawaiiPhysics_ExternalForce_ProceduralWind RippleWind;
	RippleWind.RippleForce = 1.0f;
	RippleWind.RipplePeriod = Period;
	RippleWind.RipplePhaseOffset = 90.0f;
	TestSampleNear(*this, TEXT("Ripple phase"), RippleWind.ComputeWindSample(0.0f, 0.0f).Ripple, 1.0f);
	TestSampleNear(*this, TEXT("Ripple periodicity"),
	               RippleWind.ComputeWindSample(Time, 0.35f).Ripple,
	               RippleWind.ComputeWindSample(Time + Period, 0.35f).Ripple, 0.000001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindRipplePropagationTest,
                                 "KawaiiPhysics.ProceduralWind.RipplePropagation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindRipplePropagationTest::RunTest(const FString& Parameters)
{
	// 空間位相差で根元と毛先が逆相になり、時間経過でピークが毛先方向へ移動することを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.RippleForce = 1.0f;
	Wind.RipplePeriod = 2.0f;
	Wind.RippleTipPhaseDelay = 180.0f;

	// RippleTipPhaseDelay=180度により根元(Rate=0)と毛先(Rate=1)は空間位相差piとなり、符号が反転するはず
	const float FixedTime = Wind.RipplePeriod * 0.25f;
	const float RootRipple = Wind.ComputeWindSample(FixedTime, 0.0f).Ripple;
	const float TipRipple = Wind.ComputeWindSample(FixedTime, 1.0f).Ripple;
	TestTrue(FString::Printf(TEXT("Opposite sign: root=%.6f tip=%.6f"), RootRipple, TipRipple),
	         RootRipple * TipRipple < 0.0f);

	// 時間を1/4周期進めるとピークが根元寄りから毛先寄りへ移動する（進行波としての空間伝播）ことを確認
	const int32 Peak0 = FindRipplePeakIndex(Wind, Wind.RipplePeriod * 0.25f, 1000);
	const int32 Peak1 = FindRipplePeakIndex(Wind, Wind.RipplePeriod * 0.50f, 1000);
	TestTrue(FString::Printf(TEXT("Peak moves root to tip: %d -> %d"), Peak0, Peak1),
	         Peak1 > Peak0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindStrengthCycleBoundsTest,
                                 "KawaiiPhysics.ProceduralWind.StrengthCycleBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindStrengthCycleBoundsTest::RunTest(const FString& Parameters)
{
	// 強弱サイクル(StrengthCycle)変調が指定範囲内に収まり、最小最大が同値なら定数へ潰れることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.StrengthCycleRange = FFloatInterval(0.2f, 1.5f);
	Wind.StrengthCyclePeriod = 1.37f;
	Wind.StrengthCyclePhaseOffset = 17.0f;

	// 1000点サンプリングし、StrengthCycleが常に StrengthCycleRange 内に収まることを確認
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const float Time = static_cast<float>(Index) * 0.013f;
		const float StrengthCycle = Wind.ComputeWindSample(Time, 0.0f).StrengthCycle;
		TestTrue(FString::Printf(TEXT("StrengthCycle bounds %d: %.9f"), Index, StrengthCycle),
		         StrengthCycle >= Wind.StrengthCycleRange.Min - GProceduralWindTol &&
		         StrengthCycle <= Wind.StrengthCycleRange.Max + GProceduralWindTol);
	}

	// MinとMaxを同値にした場合、Lerpの結果が時刻によらず定数へ潰れることを確認
	Wind.StrengthCycleRange = FFloatInterval(0.625f, 0.625f);
	for (const float Time : {0.0f, 0.4f, 1.7f, 8.0f})
	{
		TestSampleNear(*this, TEXT("StrengthCycle identity"), Wind.ComputeWindSample(Time, 0.0f).StrengthCycle, 0.625f);
	}

	// Min>Max の逆転 Range でも Lerp としてそのまま評価され、指定端点の範囲内に収まることを確認
	Wind.StrengthCycleRange = FFloatInterval(1.5f, 0.25f);
	Wind.StrengthCyclePeriod = 2.0f;
	Wind.StrengthCyclePhaseOffset = 90.0f;
	TestSampleNear(*this, TEXT("StrengthCycle reversed range starts at max endpoint"),
	               Wind.ComputeWindSample(0.0f, 0.0f).StrengthCycle, 0.25f);
	for (int32 Index = 0; Index < 128; ++Index)
	{
		const float StrengthCycle = Wind.ComputeWindSample(static_cast<float>(Index) * 0.031f, 0.0f).StrengthCycle;
		TestTrue(FString::Printf(TEXT("StrengthCycle reversed range %d: %.9f"), Index, StrengthCycle),
		         StrengthCycle >= Wind.StrengthCycleRange.Max - GProceduralWindTol &&
		         StrengthCycle <= Wind.StrengthCycleRange.Min + GProceduralWindTol);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindNoisePropertiesTest,
                                 "KawaiiPhysics.ProceduralWind.NoiseProperties",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindNoisePropertiesTest::RunTest(const FString& Parameters)
{
	// ノイズ範囲、格子点一致、連続性、ハッシュのスナップショット値を確認する。
	using FWind = FKawaiiPhysics_ExternalForce_ProceduralWind;

	// StableHashの実装は仕様式を持たないFNV-1a派生のビット演算のため、期待値は現行実装から採取したスナップショット値（回帰検出用）
	TestEqual(TEXT("StableHash(0, 0, 0)"), FWind::StableHash(0, 0, 0), 672839204u);
	TestEqual(TEXT("StableHash(123, 0, 0)"), FWind::StableHash(123, 0, 0), 961409981u);
	TestEqual(TEXT("StableHash(123, 1, 0)"), FWind::StableHash(123, 1, 0), 688218621u);
	TestEqual(TEXT("StableHash(123, -1, 0)"), FWind::StableHash(123, -1, 0), 2476066305u);
	TestEqual(TEXT("StableHash(-17, 42, 3)"), FWind::StableHash(-17, 42, 3), 1090092324u);

	// Uを-8〜12の範囲で0.01刻みに走査し、値域[-1,1]と隣接差分が閾値以下に収まる連続性（滑らかな補間）を確認
	float Previous = FWind::SampleSmoothNoise(-8.0f, 2468, 0);
	for (int32 Index = 0; Index <= 2000; ++Index)
	{
		const float U = -8.0f + static_cast<float>(Index) * 0.01f;
		const float Value = FWind::SampleSmoothNoise(U, 2468, 0);
		TestTrue(FString::Printf(TEXT("Noise range U=%.3f Value=%.9f"), U, Value),
		         Value >= -1.0f - GProceduralWindTol && Value <= 1.0f + GProceduralWindTol);
		if (Index > 0)
		{
			TestTrue(FString::Printf(TEXT("Noise continuity U=%.3f Prev=%.9f Value=%.9f"), U, Previous, Value),
			         FMath::Abs(Value - Previous) <= 0.1f);
		}
		Previous = Value;
	}

	// 整数座標では滑らか補間の結果が格子点の生値（NoiseValueAt）と一致する（補間の境界条件）ことを確認
	for (int32 GridIndex = -8; GridIndex <= 8; ++GridIndex)
	{
		const float Smooth = FWind::SampleSmoothNoise(static_cast<float>(GridIndex), 2468, 1);
		const float Grid = FWind::NoiseValueAt(GridIndex, 2468, 1);
		TestTrue(FString::Printf(TEXT("Grid match %d: smooth=%.9f grid=%.9f"), GridIndex, Smooth, Grid),
		         Smooth == Grid);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindNoiseChannelsTest,
                                 "KawaiiPhysics.ProceduralWind.NoiseChannels",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindNoiseChannelsTest::RunTest(const FString& Parameters)
{
	// 同じシードと座標でもチャンネルごとに独立した系列になることを確認する。
	using FWind = FKawaiiPhysics_ExternalForce_ProceduralWind;

	bool bFoundDifferentChannel = false;
	for (int32 Index = 0; Index < 128; ++Index)
	{
		const float U = -3.0f + static_cast<float>(Index) * 0.071f;
		const float C0 = FWind::SampleSmoothNoise(U, 1357, 0);
		const float C1 = FWind::SampleSmoothNoise(U, 1357, 1);
		const float C2 = FWind::SampleSmoothNoise(U, 1357, 2);
		bFoundDifferentChannel |= !FMath::IsNearlyEqual(C0, C1, GProceduralWindTol) ||
			!FMath::IsNearlyEqual(C0, C2, GProceduralWindTol) ||
			!FMath::IsNearlyEqual(C1, C2, GProceduralWindTol);
	}

	TestTrue(TEXT("Channels differ for at least one sampled U"), bFoundDifferentChannel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindGustEnvelopeTest,
                                 "KawaiiPhysics.ProceduralWind.GustEnvelope",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindGustEnvelopeTest::RunTest(const FString& Parameters)
{
	// アクティブなガストが立ち上がりと減衰時間に従って評価されることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.ResetRuntimeState();
	Wind.RuntimeState->ActiveGust.StartTime = 0.0f;
	Wind.RuntimeState->ActiveGust.Strength = 5.0f;
	Wind.RuntimeState->ActiveGust.RiseTime = 1.0f;
	Wind.RuntimeState->ActiveGust.DecayTime = 2.0f;
	Wind.RuntimeState->ActiveGust.bIsActive = true;

	// RiseTime=1・DecayTime=2に対し、立ち上がり中間(t=0.5)・ピーク(t=1)・減衰中間(t=2)・終了(t=3)・終了後(t=4)の5点で線形補間の傾きを確認
	TestSampleNear(*this, TEXT("Gust t=0.5"), Wind.ComputeWindSample(0.5f, 0.0f).Gust, 2.5f);
	TestSampleNear(*this, TEXT("Gust t=1.0"), Wind.ComputeWindSample(1.0f, 0.0f).Gust, 5.0f);
	TestSampleNear(*this, TEXT("Gust t=2.0"), Wind.ComputeWindSample(2.0f, 0.0f).Gust, 2.5f);
	TestSampleNear(*this, TEXT("Gust t=3.0"), Wind.ComputeWindSample(3.0f, 0.0f).Gust, 0.0f);
	TestSampleNear(*this, TEXT("Gust beyond"), Wind.ComputeWindSample(4.0f, 0.0f).Gust, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindGustEnvelopeHoldTest,
                                 "KawaiiPhysics.ProceduralWind.GustEnvelopeHold",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindGustEnvelopeHoldTest::RunTest(const FString& Parameters)
{
	// HoldTime を含む台形ガストが立ち上がり、保持、減衰の各区間で評価されることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.ResetRuntimeState();
	Wind.RuntimeState->ActiveGust.StartTime = 0.0f;
	Wind.RuntimeState->ActiveGust.Strength = 5.0f;
	Wind.RuntimeState->ActiveGust.RiseTime = 1.0f;
	Wind.RuntimeState->ActiveGust.DecayTime = 2.0f;
	Wind.RuntimeState->ActiveGust.HoldTime = 2.0f;
	Wind.RuntimeState->ActiveGust.bIsActive = true;

	TestSampleNear(*this, TEXT("Hold Gust t=0.5"), Wind.ComputeWindSample(0.5f, 0.0f).Gust, 2.5f);
	TestSampleNear(*this, TEXT("Hold Gust t=1.0"), Wind.ComputeWindSample(1.0f, 0.0f).Gust, 5.0f);
	TestSampleNear(*this, TEXT("Hold Gust t=2.0"), Wind.ComputeWindSample(2.0f, 0.0f).Gust, 5.0f);
	TestSampleNear(*this, TEXT("Hold Gust t=3.0"), Wind.ComputeWindSample(3.0f, 0.0f).Gust, 5.0f);
	TestSampleNear(*this, TEXT("Hold Gust t=4.0"), Wind.ComputeWindSample(4.0f, 0.0f).Gust, 2.5f);
	TestSampleNear(*this, TEXT("Hold Gust t=5.0"), Wind.ComputeWindSample(5.0f, 0.0f).Gust, 0.0f);
	TestSampleNear(*this, TEXT("Hold Gust t=6.0"), Wind.ComputeWindSample(6.0f, 0.0f).Gust, 0.0f);

	FKawaiiPhysics_ExternalForce_ProceduralWind StepWind;
	StepWind.ResetRuntimeState();
	StepWind.RuntimeState->ActiveGust.StartTime = 0.0f;
	StepWind.RuntimeState->ActiveGust.Strength = 5.0f;
	StepWind.RuntimeState->ActiveGust.RiseTime = 0.0f;
	StepWind.RuntimeState->ActiveGust.DecayTime = 0.0f;
	StepWind.RuntimeState->ActiveGust.HoldTime = 1.0f;
	StepWind.RuntimeState->ActiveGust.bIsActive = true;

	TestSampleNear(*this, TEXT("Step Gust t=0.5"), StepWind.ComputeWindSample(0.5f, 0.0f).Gust, 5.0f);
	TestSampleNear(*this, TEXT("Step Gust t=1.001"), StepWind.ComputeWindSample(1.001f, 0.0f).Gust, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindRequestGustStopTest,
                                 "KawaiiPhysics.ProceduralWind.RequestGustStop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindRequestGustStopTest::RunTest(const FString& Parameters)
{
	// rise 途中の現在値から指定秒数で線形フェードアウトすることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind BlendOutWind;
	BlendOutWind.ResetRuntimeState();
	BlendOutWind.RuntimeState->Time = 1.0f;
	BlendOutWind.RuntimeState->ActiveGust.StartTime = 0.0f;
	BlendOutWind.RuntimeState->ActiveGust.Strength = 10.0f;
	BlendOutWind.RuntimeState->ActiveGust.RiseTime = 2.0f;
	BlendOutWind.RuntimeState->ActiveGust.DecayTime = 3.0f;
	BlendOutWind.RuntimeState->ActiveGust.bIsActive = true;

	BlendOutWind.RequestGustStop(1.0f);
	BlendOutWind.ConsumePendingRequests();

	TestSampleNear(*this, TEXT("Stop blend start"), BlendOutWind.ComputeWindSample(1.0f, 0.0f).Gust, 5.0f);
	TestSampleNear(*this, TEXT("Stop blend middle"), BlendOutWind.ComputeWindSample(1.5f, 0.0f).Gust, 2.5f);
	TestSampleNear(*this, TEXT("Stop blend end"), BlendOutWind.ComputeWindSample(2.0f, 0.0f).Gust, 0.0f);

	// BlendOutTime が0なら即時停止する。
	FKawaiiPhysics_ExternalForce_ProceduralWind ImmediateWind;
	ImmediateWind.ResetRuntimeState();
	ImmediateWind.RuntimeState->Time = 0.5f;
	ImmediateWind.RuntimeState->ActiveGust.StartTime = 0.0f;
	ImmediateWind.RuntimeState->ActiveGust.Strength = 10.0f;
	ImmediateWind.RuntimeState->ActiveGust.RiseTime = 1.0f;
	ImmediateWind.RuntimeState->ActiveGust.DecayTime = 1.0f;
	ImmediateWind.RuntimeState->ActiveGust.bIsActive = true;

	ImmediateWind.RequestGustStop(0.0f);
	ImmediateWind.ConsumePendingRequests();

	TestFalse(TEXT("Immediate stop deactivates gust"), ImmediateWind.RuntimeState->ActiveGust.bIsActive);
	TestSampleNear(*this, TEXT("Immediate stop gust"), ImmediateWind.ComputeWindSample(0.5f, 0.0f).Gust, 0.0f);

	// 非アクティブ時の停止要求は何もせず、クラッシュせずに消費される。
	FKawaiiPhysics_ExternalForce_ProceduralWind InactiveWind;
	InactiveWind.ResetRuntimeState();
	InactiveWind.RequestGustStop(1.0f);
	InactiveWind.ConsumePendingRequests();

	TestFalse(TEXT("Inactive stop keeps gust inactive"), InactiveWind.RuntimeState->ActiveGust.bIsActive);
	TestSampleNear(*this, TEXT("Inactive stop gust"), InactiveWind.ComputeWindSample(1.0f, 0.0f).Gust, 0.0f);
	TestFalse(TEXT("Inactive stop pending reset"), InactiveWind.RuntimeState->PendingGustStop.IsSet());

	// consume 前に複数停止要求が来た場合は最後の BlendOutTime が勝つ。
	FKawaiiPhysics_ExternalForce_ProceduralWind LastWinsWind;
	LastWinsWind.ResetRuntimeState();
	LastWinsWind.RuntimeState->Time = 1.0f;
	LastWinsWind.RuntimeState->ActiveGust.StartTime = 0.0f;
	LastWinsWind.RuntimeState->ActiveGust.Strength = 10.0f;
	LastWinsWind.RuntimeState->ActiveGust.RiseTime = 2.0f;
	LastWinsWind.RuntimeState->ActiveGust.DecayTime = 3.0f;
	LastWinsWind.RuntimeState->ActiveGust.bIsActive = true;

	LastWinsWind.RequestGustStop(0.25f);
	LastWinsWind.RequestGustStop(2.0f);
	LastWinsWind.ConsumePendingRequests();

	TestSampleNear(*this, TEXT("Last stop request wins"), LastWinsWind.ComputeWindSample(2.0f, 0.0f).Gust, 2.5f);

	// 同フレームに起動と即時停止が来た場合は停止を優先する。
	FKawaiiPhysics_ExternalForce_ProceduralWind SameFrameWind;
	SameFrameWind.ResetRuntimeState();
	SameFrameWind.RuntimeState->Time = 3.0f;

	SameFrameWind.RequestGust(10.0f, 0.0f, 2.0f, 1.0f);
	SameFrameWind.RequestGustStop(0.0f);
	SameFrameWind.ConsumePendingRequests();

	TestFalse(TEXT("Same-frame stop wins"), SameFrameWind.RuntimeState->ActiveGust.bIsActive);
	TestSampleNear(*this, TEXT("Same-frame stop gust"), SameFrameWind.ComputeWindSample(3.0f, 0.0f).Gust, 0.0f);
	TestFalse(TEXT("Same-frame stop pending reset"), SameFrameWind.RuntimeState->PendingGustStop.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindDynamicParamsTest,
                                 "KawaiiPhysics.ProceduralWind.DynamicParams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindDynamicParamsTest::RunTest(const FString& Parameters)
{
	// 上書き指定された項目だけが反映され、下限付き項目は安全化されることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.WindDirection = FVector(1.0f, 2.0f, 3.0f);
	Wind.ConstantForce = 2.0f;
	Wind.SwayForce = 3.0f;
	Wind.SwayPeriod = 1.0f;
	Wind.RippleForce = 4.0f;
	Wind.RipplePhaseOffset = 10.0f;
	Wind.StrengthCycleRange = FFloatInterval(0.5f, 1.5f);
	Wind.WindDirectionNoiseAngle = 5.0f;
	Wind.TimeScale = 1.0f;

	// bOverride*をtrue/false交互に設定し、上書き対象と非対象の双方を1回のApplyDynamicParams呼び出しで検証する
	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideWindDirection = false;
	Params.WindDirection = FVector(10.0f, 20.0f, 30.0f);
	Params.bOverrideConstantForce = true;
	Params.ConstantForce = -5.0f;
	Params.bOverrideSwayForce = false;
	Params.SwayForce = 99.0f;
	Params.bOverrideSwayPeriod = true;
	Params.SwayPeriod = -10.0f;
	Params.bOverrideRippleForce = false;
	Params.RippleForce = 99.0f;
	Params.bOverrideRipplePhaseOffset = true;
	Params.RipplePhaseOffset = -45.0f;
	Params.bOverrideStrengthCycleRange = true;
	Params.StrengthCycleRange = FFloatInterval(-0.25f, 2.25f);
	Params.bOverrideWindDirectionNoiseAngle = true;
	Params.WindDirectionNoiseAngle = -20.0f;
	Params.bOverrideTimeScale = true;
	Params.TimeScale = -1.0f;

	Wind.ApplyDynamicParams(Params);

	// 上書きされた項目は負値でも下限（0 または 0.01）へ安全化され、上書きされない項目は元値のまま残ることを確認
	TestTrue(TEXT("WindDirection unchanged"), Wind.WindDirection.Equals(FVector(1.0f, 2.0f, 3.0f)));
	TestSampleNear(*this, TEXT("ConstantForce clamped"), Wind.ConstantForce, 0.0f);
	TestSampleNear(*this, TEXT("SwayForce unchanged"), Wind.SwayForce, 3.0f);
	TestSampleNear(*this, TEXT("SwayPeriod clamped"), Wind.SwayPeriod, 0.01f);
	TestSampleNear(*this, TEXT("RippleForce unchanged"), Wind.RippleForce, 4.0f);
	TestSampleNear(*this, TEXT("RipplePhaseOffset unclamped"), Wind.RipplePhaseOffset, -45.0f);
	TestSampleNear(*this, TEXT("StrengthCycleRange Min clamped"), Wind.StrengthCycleRange.Min, 0.0f);
	TestSampleNear(*this, TEXT("StrengthCycleRange Max applied"), Wind.StrengthCycleRange.Max, 2.25f);
	TestSampleNear(*this, TEXT("WindDirectionNoiseAngle clamped"), Wind.WindDirectionNoiseAngle, 0.0f);
	TestSampleNear(*this, TEXT("TimeScale clamped"), Wind.TimeScale, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindPendingConsumptionTest,
                                 "KawaiiPhysics.ProceduralWind.PendingConsumption",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindPendingConsumptionTest::RunTest(const FString& Parameters)
{
	// 保留中のパラメータと突風が一度の消費で反映され、保留欄が空になることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.ResetRuntimeState();
	Wind.RuntimeState->Time = 2.25f;

	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideConstantForce = true;
	Params.ConstantForce = 8.0f;
	Params.bOverrideSwayPeriod = true;
	Params.SwayPeriod = 0.25f;

	// Mutex配下でPendingParams/PendingGustを設定し、Apply側スレッドからの非同期リクエストを模擬する
	{
		FScopeLock Lock(&Wind.RuntimeState->Mutex);
		Wind.RuntimeState->PendingParams = Params;
		Wind.RuntimeState->PendingGust = FKawaiiProceduralWindGustRequest{
			6.0f,
			0.1f,
			0.5f,
			0.75f
		};
	}

	Wind.ConsumePendingRequests();

	TestSampleNear(*this, TEXT("Pending ConstantForce applied"), Wind.ConstantForce, 8.0f);
	TestSampleNear(*this, TEXT("Pending SwayPeriod applied"), Wind.SwayPeriod, 0.25f);
	TestSampleNear(*this, TEXT("ActiveGust StartTime"), Wind.RuntimeState->ActiveGust.StartTime, 2.25f);
	TestSampleNear(*this, TEXT("ActiveGust Strength"), Wind.RuntimeState->ActiveGust.Strength, 6.0f);
	TestSampleNear(*this, TEXT("ActiveGust RiseTime"), Wind.RuntimeState->ActiveGust.RiseTime, 0.1f);
	TestSampleNear(*this, TEXT("ActiveGust DecayTime"), Wind.RuntimeState->ActiveGust.DecayTime, 0.5f);
	TestSampleNear(*this, TEXT("ActiveGust HoldTime"), Wind.RuntimeState->ActiveGust.HoldTime, 0.75f);
	TestTrue(TEXT("ActiveGust active"), Wind.RuntimeState->ActiveGust.bIsActive);
	TestFalse(TEXT("PendingParams reset"), Wind.RuntimeState->PendingParams.IsSet());
	TestFalse(TEXT("PendingGust reset"), Wind.RuntimeState->PendingGust.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindRequestDynamicParamsMergeTest,
                                 "KawaiiPhysics.ProceduralWind.RequestDynamicParamsMerge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindRequestDynamicParamsMergeTest::RunTest(const FString& Parameters)
{
	// consume 前に複数の動的パラメータ要求が届いても、別項目は保持され、同一項目は後勝ちになることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;

	FKawaiiProceduralWindDynamicParams DirectionParams;
	DirectionParams.bOverrideWindDirection = true;
	DirectionParams.WindDirection = FVector(0.0f, 20.0f, 30.0f);
	Wind.RequestDynamicParams(DirectionParams);

	FKawaiiProceduralWindDynamicParams ConstantParams;
	ConstantParams.bOverrideConstantForce = true;
	ConstantParams.ConstantForce = 9.0f;
	Wind.RequestDynamicParams(ConstantParams);

	TestTrue(TEXT("PendingParams merged"), Wind.RuntimeState->PendingParams.IsSet());
	if (!Wind.RuntimeState->PendingParams.IsSet())
	{
		return false;
	}

	const FKawaiiProceduralWindDynamicParams& PendingParams = Wind.RuntimeState->PendingParams.GetValue();
	TestTrue(TEXT("Pending WindDirection override merged"), PendingParams.bOverrideWindDirection);
	TestTrue(TEXT("Pending ConstantForce override merged"), PendingParams.bOverrideConstantForce);

	Wind.ConsumePendingRequests();

	TestTrue(TEXT("Merged WindDirection applied"), Wind.WindDirection.Equals(FVector(0.0f, 20.0f, 30.0f)));
	TestSampleNear(*this, TEXT("Merged ConstantForce applied"), Wind.ConstantForce, 9.0f);

	FKawaiiProceduralWindDynamicParams FirstDirectionParams;
	FirstDirectionParams.bOverrideWindDirection = true;
	FirstDirectionParams.WindDirection = FVector(1.0f, 2.0f, 3.0f);
	Wind.RequestDynamicParams(FirstDirectionParams);

	FKawaiiProceduralWindDynamicParams LastDirectionParams;
	LastDirectionParams.bOverrideWindDirection = true;
	LastDirectionParams.WindDirection = FVector(4.0f, 5.0f, 6.0f);
	Wind.RequestDynamicParams(LastDirectionParams);
	Wind.ConsumePendingRequests();

	TestTrue(TEXT("Last WindDirection request wins"), Wind.WindDirection.Equals(FVector(4.0f, 5.0f, 6.0f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindFramerateIndependenceTest,
                                 "KawaiiPhysics.ProceduralWind.FramerateIndependence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindFramerateIndependenceTest::RunTest(const FString& Parameters)
{
	// 同一フレーム内でキャッシュ済みの波を一括適用しても分割適用しても変位合計が一致することを確認する。
	const FVector OneStep = ApplyProceduralWindDisplacement(1);
	const FVector FourSteps = ApplyProceduralWindDisplacement(4);
	// 両者はfloat演算順序の違いによる誤差のみを含むはずなので、既定のGProceduralWindTolより厳しい許容誤差で比較する
	TestTrue(FString::Printf(TEXT("Apply displacement 1 vs 4: %s vs %s"), *OneStep.ToString(), *FourSteps.ToString()),
	         OneStep.Equals(FourSteps, 0.000001f));
	TestTrue(TEXT("Ripple-including displacement is non-zero"), !OneStep.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindResetRuntimeStateTest,
                                 "KawaiiPhysics.ProceduralWind.ResetRuntimeState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindResetRuntimeStateTest::RunTest(const FString& Parameters)
{
	// 時刻とガスト状態を進めたあと、リセットで初期状態へ戻ることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> InitialRuntimeState = Wind.RuntimeState;
	TestTrue(TEXT("RuntimeState starts valid"), InitialRuntimeState.IsValid());
	Wind.RuntimeState->Time = 2.5f;
	Wind.RuntimeState->ActiveGust.StartTime = 0.0f;
	Wind.RuntimeState->ActiveGust.Strength = 4.0f;
	Wind.RuntimeState->ActiveGust.RiseTime = 0.5f;
	Wind.RuntimeState->ActiveGust.DecayTime = 3.0f;
	Wind.RuntimeState->ActiveGust.HoldTime = 1.0f;
	Wind.RuntimeState->ActiveGust.bIsActive = true;
	Wind.RuntimeState->CachedSinesWithoutRipple = 1.0f;
	Wind.RuntimeState->CachedStrengthCycle = 2.0f;
	Wind.RuntimeState->CachedRandom = 3.0f;
	Wind.RuntimeState->CachedGust = 4.0f;
	Wind.RuntimeState->CachedWindVector = FVector(1.0f, 2.0f, 3.0f);
	Wind.RuntimeState->PendingParams = FKawaiiProceduralWindDynamicParams();
	Wind.RuntimeState->PendingGust = FKawaiiProceduralWindGustRequest{1.0f, 0.2f, 0.3f};
	Wind.RuntimeState->PendingGustStop = 0.4f;
#if WITH_EDITOR
	Wind.RuntimeState->ScopeWriteIndex = 10;
	Wind.RuntimeState->ScopeSampleCount = 20;
#endif
	TestTrue(TEXT("Gust is active before reset"), Wind.ComputeWindSample(1.0f, 0.0f).Gust > 0.0f);

	// ResetRuntimeStateはRuntimeStateを差し替えず、Mutex配下で中身だけを初期状態へ戻す
	Wind.ResetRuntimeState();
	TestTrue(TEXT("RuntimeState pointer is preserved"), Wind.RuntimeState.Get() == InitialRuntimeState.Get());
	TestSampleNear(*this, TEXT("Gust after reset"), Wind.ComputeWindSample(1.0f, 0.0f).Gust, 0.0f);
	TestSampleNear(*this, TEXT("Time after reset"), Wind.RuntimeState->Time, 0.0f);
	TestFalse(TEXT("ActiveGust inactive after reset"), Wind.RuntimeState->ActiveGust.bIsActive);
	TestFalse(TEXT("PendingParams reset"), Wind.RuntimeState->PendingParams.IsSet());
	TestFalse(TEXT("PendingGust reset"), Wind.RuntimeState->PendingGust.IsSet());
	TestFalse(TEXT("PendingGustStop reset"), Wind.RuntimeState->PendingGustStop.IsSet());
	TestSampleNear(*this, TEXT("CachedSinesWithoutRipple after reset"), Wind.RuntimeState->CachedSinesWithoutRipple, 0.0f);
	TestSampleNear(*this, TEXT("CachedStrengthCycle after reset"), Wind.RuntimeState->CachedStrengthCycle, 1.0f);
	TestSampleNear(*this, TEXT("CachedRandom after reset"), Wind.RuntimeState->CachedRandom, 0.0f);
	TestSampleNear(*this, TEXT("CachedGust after reset"), Wind.RuntimeState->CachedGust, 0.0f);
	TestTrue(TEXT("CachedWindVector after reset"), Wind.RuntimeState->CachedWindVector.IsNearlyZero());
#if WITH_EDITOR
	TestTrue(TEXT("ScopeBuffer allocated after reset"), Wind.RuntimeState->ScopeBuffer.Num() > 0);
	TestEqual(TEXT("ScopeWriteIndex after reset"), Wind.RuntimeState->ScopeWriteIndex, 0);
	TestEqual(TEXT("ScopeSampleCount after reset"), Wind.RuntimeState->ScopeSampleCount, static_cast<uint64>(0));
#endif

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindAssignmentPreservesDestinationRuntimeStateTest,
                                 "KawaiiPhysics.ProceduralWind.AssignmentPreservesDestinationRuntimeState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindAssignmentPreservesDestinationRuntimeStateTest::RunTest(const FString& Parameters)
{
	// 代入時にプロパティだけをコピーし、代入先の実行中状態（時刻・Pending）とポインタが維持されることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Destination;
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> DestinationRuntimeState =
		Destination.RuntimeState;
	Destination.RuntimeState->Time = 7.0f;

	FKawaiiProceduralWindDynamicParams PendingParams;
	PendingParams.bOverrideConstantForce = true;
	PendingParams.ConstantForce = 11.0f;
	Destination.RequestDynamicParams(PendingParams);

	FKawaiiPhysics_ExternalForce_ProceduralWind Source;
	Source.WindDirection = FVector(0.0f, 20.0f, 30.0f);
	Source.ConstantForce = 5.0f;
	Source.SwayForce = 6.0f;
	Source.TimeScale = 0.5f;
	Source.RuntimeState->Time = 3.0f;

	Destination = Source;

	TestTrue(TEXT("Destination RuntimeState pointer is preserved"),
	         Destination.RuntimeState.Get() == DestinationRuntimeState.Get());
	TestSampleNear(*this, TEXT("Destination Time preserved"), Destination.RuntimeState->Time, 7.0f);
	TestTrue(TEXT("Destination PendingParams preserved"), Destination.RuntimeState->PendingParams.IsSet());
	TestTrue(TEXT("PendingParams override preserved"),
	         Destination.RuntimeState->PendingParams.GetValue().bOverrideConstantForce);
	TestSampleNear(*this, TEXT("PendingParams value preserved"),
	               Destination.RuntimeState->PendingParams.GetValue().ConstantForce, 11.0f);
	TestTrue(TEXT("WindDirection copied"), Destination.WindDirection.Equals(Source.WindDirection));
	TestSampleNear(*this, TEXT("ConstantForce copied"), Destination.ConstantForce, Source.ConstantForce);
	TestSampleNear(*this, TEXT("SwayForce copied"), Destination.SwayForce, Source.SwayForce);
	TestSampleNear(*this, TEXT("TimeScale copied"), Destination.TimeScale, Source.TimeScale);
	TestTrue(TEXT("RuntimeState is not shared with source"),
	         Destination.RuntimeState.Get() != Source.RuntimeState.Get());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindInPlaceCopyScriptStructPreservesRuntimeStateTest,
                                 "KawaiiPhysics.ProceduralWind.InPlaceCopyScriptStructPreservesRuntimeState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindInPlaceCopyScriptStructPreservesRuntimeStateTest::RunTest(const FString& Parameters)
{
	// エディタの in-place 同期と同じ CopyScriptStruct 経路で、プロパティだけがコピーされ実行中状態が維持されることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Source;
	Source.WindDirection = FVector(0.0f, 20.0f, 30.0f);
	Source.ConstantForce = 5.0f;
	Source.RuntimeState->Time = 3.0f;

	FKawaiiPhysics_ExternalForce_ProceduralWind Destination;
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> DestinationRuntimeState =
		Destination.RuntimeState;
	Destination.RuntimeState->Time = 7.0f;

	FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct()->CopyScriptStruct(&Destination, &Source);

	TestTrue(TEXT("Destination RuntimeState pointer is preserved"),
	         Destination.RuntimeState.Get() == DestinationRuntimeState.Get());
	TestSampleNear(*this, TEXT("Destination Time preserved"), Destination.RuntimeState->Time, 7.0f);
	TestTrue(TEXT("WindDirection copied"), Destination.WindDirection.Equals(Source.WindDirection));
	TestSampleNear(*this, TEXT("ConstantForce copied"), Destination.ConstantForce, Source.ConstantForce);
	TestTrue(TEXT("RuntimeState is not shared with source"),
	         Destination.RuntimeState.Get() != Source.RuntimeState.Get());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindRequestCreatesRuntimeStateTest,
                                 "KawaiiPhysics.ProceduralWind.RequestCreatesRuntimeState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindRequestCreatesRuntimeStateTest::RunTest(const FString& Parameters)
{
	// 無効な RuntimeState に対する Request API が状態を遅延生成し、次回消費まで要求を保持することを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind ParamsWind;
	ParamsWind.RuntimeState.Reset();

	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideConstantForce = true;
	Params.ConstantForce = 9.0f;
	ParamsWind.RequestDynamicParams(Params);

	TestTrue(TEXT("RequestDynamicParams creates RuntimeState"), ParamsWind.RuntimeState.IsValid());
	TestTrue(TEXT("PendingParams set after request"), ParamsWind.RuntimeState->PendingParams.IsSet());
	ParamsWind.ConsumePendingRequests();
	TestSampleNear(*this, TEXT("PendingParams applied"), ParamsWind.ConstantForce, 9.0f);
	TestFalse(TEXT("PendingParams reset after consume"), ParamsWind.RuntimeState->PendingParams.IsSet());

	FKawaiiPhysics_ExternalForce_ProceduralWind GustWind;
	GustWind.RuntimeState.Reset();
	GustWind.RequestGust(4.0f, 0.2f, 0.6f);

	TestTrue(TEXT("RequestGust creates RuntimeState"), GustWind.RuntimeState.IsValid());
	TestTrue(TEXT("PendingGust set after request"), GustWind.RuntimeState->PendingGust.IsSet());
	GustWind.ConsumePendingRequests();
	TestTrue(TEXT("ActiveGust active after consume"), GustWind.RuntimeState->ActiveGust.bIsActive);
	TestSampleNear(*this, TEXT("ActiveGust Strength applied"), GustWind.RuntimeState->ActiveGust.Strength, 4.0f);
	TestSampleNear(*this, TEXT("ActiveGust RiseTime applied"), GustWind.RuntimeState->ActiveGust.RiseTime, 0.2f);
	TestSampleNear(*this, TEXT("ActiveGust DecayTime applied"), GustWind.RuntimeState->ActiveGust.DecayTime, 0.6f);
	TestFalse(TEXT("PendingGust reset after consume"), GustWind.RuntimeState->PendingGust.IsSet());

	FKawaiiPhysics_ExternalForce_ProceduralWind StopWind;
	StopWind.RuntimeState.Reset();
	StopWind.RequestGustStop(0.5f);

	TestTrue(TEXT("RequestGustStop creates RuntimeState"), StopWind.RuntimeState.IsValid());
	TestTrue(TEXT("PendingGustStop set after request"), StopWind.RuntimeState->PendingGustStop.IsSet());
	StopWind.ConsumePendingRequests();
	TestFalse(TEXT("PendingGustStop reset after consume"), StopWind.RuntimeState->PendingGustStop.IsSet());
	TestFalse(TEXT("Inactive gust remains inactive after stop consume"), StopWind.RuntimeState->ActiveGust.bIsActive);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindDynamicParamsForPropertyTest,
                                 "KawaiiPhysics.ProceduralWind.DynamicParamsForProperty",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindDynamicParamsForPropertyTest::RunTest(const FString& Parameters)
{
	// プロパティ名から、その項目だけを上書きする DynamicParams が作られることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.WindDirection = FVector(0.0f, 20.0f, 30.0f);

	FKawaiiProceduralWindDynamicParams Params;
	const bool bBuilt = Wind.BuildDynamicParamsForProperty(
		GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection), Params);

	TestTrue(TEXT("WindDirection params built"), bBuilt);
	TestTrue(TEXT("Only one override flag is set"), CountDynamicParamOverrideFlags(Params) == 1);
	TestTrue(TEXT("WindDirection override is set"), Params.bOverrideWindDirection);
	TestFalse(TEXT("ConstantForce override is not set"), Params.bOverrideConstantForce);
	TestFalse(TEXT("TimeScale override is not set"), Params.bOverrideTimeScale);
	TestFalse(TEXT("IsEnabled override is not set"), Params.bOverrideIsEnabled);
	TestTrue(TEXT("WindDirection value matches"), Params.WindDirection.Equals(Wind.WindDirection));

	FKawaiiPhysics_ExternalForce_ProceduralWind AppliedWind;
	AppliedWind.ConstantForce = 2.5f;
	AppliedWind.TimeScale = 0.75f;
	AppliedWind.ApplyDynamicParams(Params);

	TestTrue(TEXT("WindDirection applied"), AppliedWind.WindDirection.Equals(Wind.WindDirection));
	TestSampleNear(*this, TEXT("ConstantForce untouched"), AppliedWind.ConstantForce, 2.5f);
	TestSampleNear(*this, TEXT("TimeScale untouched"), AppliedWind.TimeScale, 0.75f);

	FKawaiiProceduralWindDynamicParams UnmappedParams;
	const bool bUnmappedBuilt = Wind.BuildDynamicParamsForProperty(FName(TEXT("Seed")), UnmappedParams);
	TestFalse(TEXT("Seed is unmapped"), bUnmappedBuilt);
	TestTrue(TEXT("Unmapped leaves no override flags"), CountDynamicParamOverrideFlags(UnmappedParams) == 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindDynamicParamsSnapshotTest,
                                 "KawaiiPhysics.ProceduralWind.DynamicParamsSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindDynamicParamsSnapshotTest::RunTest(const FString& Parameters)
{
	// スナップショットは DynamicParams 対応項目をすべて上書き対象にして現在値を保持する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.WindDirection = FVector(0.0f, 20.0f, 30.0f);
	Wind.ConstantForce = 5.0f;
	Wind.SwayPeriod = 0.25f;
	Wind.RipplePeriod = 0.5f;
	Wind.StrengthCycleRange = FFloatInterval(0.4f, 1.8f);
	Wind.RandomForcePeriod = 0.75f;
	Wind.WindDirectionNoisePeriod = 1.25f;
	Wind.TimeScale = 0.5f;
	Wind.bIsEnabled = false;

	const FKawaiiProceduralWindDynamicParams Params = Wind.BuildDynamicParamsSnapshot();

	TestTrue(TEXT("All override flags are set"), CountDynamicParamOverrideFlags(Params) == 18);
	TestTrue(TEXT("IsEnabled override is set"), Params.bOverrideIsEnabled);
	TestTrue(TEXT("WindDirection override is set"), Params.bOverrideWindDirection);
	TestTrue(TEXT("ConstantForce override is set"), Params.bOverrideConstantForce);
	TestTrue(TEXT("TimeScale override is set"), Params.bOverrideTimeScale);
	TestFalse(TEXT("Snapshot bIsEnabled matches"), Params.bIsEnabled);
	TestTrue(TEXT("Snapshot WindDirection matches"), Params.WindDirection.Equals(Wind.WindDirection));
	TestSampleNear(*this, TEXT("Snapshot ConstantForce matches"), Params.ConstantForce, 5.0f);
	TestSampleNear(*this, TEXT("Snapshot StrengthCycleRange Min matches"), Params.StrengthCycleRange.Min, 0.4f);
	TestSampleNear(*this, TEXT("Snapshot StrengthCycleRange Max matches"), Params.StrengthCycleRange.Max, 1.8f);
	TestSampleNear(*this, TEXT("Snapshot TimeScale matches"), Params.TimeScale, 0.5f);

	FKawaiiPhysics_ExternalForce_ProceduralWind AppliedWind;
	AppliedWind.ApplyDynamicParams(Params);

	TestTrue(TEXT("Snapshot WindDirection applied"), AppliedWind.WindDirection.Equals(Wind.WindDirection));
	TestSampleNear(*this, TEXT("Snapshot ConstantForce applied"), AppliedWind.ConstantForce, 5.0f);
	TestSampleNear(*this, TEXT("Snapshot StrengthCycleRange Min applied"), AppliedWind.StrengthCycleRange.Min, 0.4f);
	TestSampleNear(*this, TEXT("Snapshot StrengthCycleRange Max applied"), AppliedWind.StrengthCycleRange.Max, 1.8f);
	TestSampleNear(*this, TEXT("Snapshot TimeScale applied"), AppliedWind.TimeScale, 0.5f);
	TestFalse(TEXT("Snapshot bIsEnabled applied"), AppliedWind.bIsEnabled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindDynamicParamsSnapshotPendingMergeTest,
                                 "KawaiiPhysics.ProceduralWind.DynamicParamsSnapshotPendingMerge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindDynamicParamsSnapshotPendingMergeTest::RunTest(const FString& Parameters)
{
	// 1. デフォルト構築直後のスナップショットは全項目を上書き対象にし、各値がメンバのデフォルト値と一致することを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	const FKawaiiProceduralWindDynamicParams DefaultParams = Wind.BuildDynamicParamsSnapshot();

	TestTrue(TEXT("Default snapshot: all override flags set"), CountDynamicParamOverrideFlags(DefaultParams) == 18);
	TestTrue(TEXT("Default snapshot: bIsEnabled matches"), DefaultParams.bIsEnabled == Wind.bIsEnabled);
	TestTrue(TEXT("Default snapshot: WindDirection matches"), DefaultParams.WindDirection.Equals(Wind.WindDirection));
	TestSampleNear(*this, TEXT("Default snapshot: ConstantForce"), DefaultParams.ConstantForce, Wind.ConstantForce);
	TestSampleNear(*this, TEXT("Default snapshot: SwayForce"), DefaultParams.SwayForce, Wind.SwayForce);
	TestSampleNear(*this, TEXT("Default snapshot: SwayPeriod"), DefaultParams.SwayPeriod, Wind.SwayPeriod);
	TestSampleNear(*this, TEXT("Default snapshot: SwayPhaseOffset"), DefaultParams.SwayPhaseOffset, Wind.SwayPhaseOffset);
	TestSampleNear(*this, TEXT("Default snapshot: RippleForce"), DefaultParams.RippleForce, Wind.RippleForce);
	TestSampleNear(*this, TEXT("Default snapshot: RipplePeriod"), DefaultParams.RipplePeriod, Wind.RipplePeriod);
	TestSampleNear(*this, TEXT("Default snapshot: RipplePhaseOffset"), DefaultParams.RipplePhaseOffset, Wind.RipplePhaseOffset);
	TestSampleNear(*this, TEXT("Default snapshot: RippleTipPhaseDelay"), DefaultParams.RippleTipPhaseDelay, Wind.RippleTipPhaseDelay);
	TestSampleNear(*this, TEXT("Default snapshot: StrengthCycleRange Min"), DefaultParams.StrengthCycleRange.Min, Wind.StrengthCycleRange.Min);
	TestSampleNear(*this, TEXT("Default snapshot: StrengthCycleRange Max"), DefaultParams.StrengthCycleRange.Max, Wind.StrengthCycleRange.Max);
	TestSampleNear(*this, TEXT("Default snapshot: StrengthCyclePeriod"), DefaultParams.StrengthCyclePeriod, Wind.StrengthCyclePeriod);
	TestSampleNear(*this, TEXT("Default snapshot: StrengthCyclePhaseOffset"), DefaultParams.StrengthCyclePhaseOffset, Wind.StrengthCyclePhaseOffset);
	TestSampleNear(*this, TEXT("Default snapshot: RandomForce"), DefaultParams.RandomForce, Wind.RandomForce);
	TestSampleNear(*this, TEXT("Default snapshot: RandomForcePeriod"), DefaultParams.RandomForcePeriod, Wind.RandomForcePeriod);
	TestSampleNear(*this, TEXT("Default snapshot: WindDirectionNoiseAngle"), DefaultParams.WindDirectionNoiseAngle, Wind.WindDirectionNoiseAngle);
	TestSampleNear(*this, TEXT("Default snapshot: WindDirectionNoisePeriod"), DefaultParams.WindDirectionNoisePeriod, Wind.WindDirectionNoisePeriod);
	TestSampleNear(*this, TEXT("Default snapshot: TimeScale"), DefaultParams.TimeScale, Wind.TimeScale);

	// 2. ApplyDynamicParamsで数項目（WindDirection/ConstantForce/TimeScale）を変更すると、スナップショットに反映されることを確認する。
	FKawaiiProceduralWindDynamicParams ApplyParams;
	ApplyParams.bOverrideWindDirection = true;
	ApplyParams.WindDirection = FVector(1.0f, 0.0f, 0.0f);
	ApplyParams.bOverrideConstantForce = true;
	ApplyParams.ConstantForce = 12.5f;
	ApplyParams.bOverrideTimeScale = true;
	ApplyParams.TimeScale = 2.0f;
	Wind.ApplyDynamicParams(ApplyParams);

	const FKawaiiProceduralWindDynamicParams AppliedParams = Wind.BuildDynamicParamsSnapshot();
	TestTrue(TEXT("Applied snapshot: WindDirection reflects change"),
	        AppliedParams.WindDirection.Equals(FVector(1.0f, 0.0f, 0.0f)));
	TestSampleNear(*this, TEXT("Applied snapshot: ConstantForce reflects change"), AppliedParams.ConstantForce, 12.5f);
	TestSampleNear(*this, TEXT("Applied snapshot: TimeScale reflects change"), AppliedParams.TimeScale, 2.0f);

	// 3. RequestDynamicParamsでSwayForceのみをpendingとして積むと、consume前のスナップショットにもpending値が
	// 反映され（read-your-writes）、他項目は現在値のまま維持されることを確認する。
	FKawaiiProceduralWindDynamicParams PendingParams;
	PendingParams.bOverrideSwayForce = true;
	PendingParams.SwayForce = 8.5f;
	Wind.RequestDynamicParams(PendingParams);

	const FKawaiiProceduralWindDynamicParams PendingSnapshot = Wind.BuildDynamicParamsSnapshot();
	TestTrue(TEXT("Pending snapshot: all override flags remain set"),
	        CountDynamicParamOverrideFlags(PendingSnapshot) == 18);
	TestSampleNear(*this, TEXT("Pending snapshot: SwayForce reflects pending value"), PendingSnapshot.SwayForce, 8.5f);
	TestTrue(TEXT("Pending snapshot: WindDirection unaffected"),
	        PendingSnapshot.WindDirection.Equals(FVector(1.0f, 0.0f, 0.0f)));
	TestSampleNear(*this, TEXT("Pending snapshot: ConstantForce unaffected"), PendingSnapshot.ConstantForce, 12.5f);
	TestSampleNear(*this, TEXT("Pending snapshot: TimeScale unaffected"), PendingSnapshot.TimeScale, 2.0f);
	TestSampleNear(*this, TEXT("Pending snapshot: SwayPeriod unaffected"), PendingSnapshot.SwayPeriod, Wind.SwayPeriod);

	// pendingが消費されていないことを、実メンバへ未反映であること・PendingParamsが依然setであること・
	// もう一度スナップショットしても同じ値が返ることの3点で確認する
	TestSampleNear(*this, TEXT("SwayForce member not yet applied"), Wind.SwayForce, 0.0f);
	TestTrue(TEXT("PendingParams still set after snapshot"),
	        Wind.RuntimeState.IsValid() && Wind.RuntimeState->PendingParams.IsSet());

	const FKawaiiProceduralWindDynamicParams PendingSnapshotAgain = Wind.BuildDynamicParamsSnapshot();
	TestSampleNear(*this, TEXT("Repeated pending snapshot: SwayForce unchanged"), PendingSnapshotAgain.SwayForce, 8.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindCopyRuntimeStateIndependenceTest,
                                 "KawaiiPhysics.ProceduralWind.CopyRuntimeStateIndependence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindCopyRuntimeStateIndependenceTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind Source;
	Source.ConstantForce = 12.0f;
	Source.RuntimeState->Time = 1.0f;

	FKawaiiPhysics_ExternalForce_ProceduralWind Copied(Source);
	TestTrue(TEXT("Copied RuntimeState is valid"), Copied.RuntimeState.IsValid());
	TestTrue(TEXT("Copy constructor does not share RuntimeState"),
	         Source.RuntimeState.Get() != Copied.RuntimeState.Get());
	TestSampleNear(*this, TEXT("Copied ConstantForce"), Copied.ConstantForce, Source.ConstantForce);

	Source.RuntimeState->Time = 2.0f;
	Copied.RuntimeState->Time = 3.0f;
	TestSampleNear(*this, TEXT("Source Time independent"), Source.RuntimeState->Time, 2.0f);
	TestSampleNear(*this, TEXT("Copied Time independent"), Copied.RuntimeState->Time, 3.0f);

	FKawaiiPhysics_ExternalForce_ProceduralWind Assigned;
	Assigned = Source;
	TestTrue(TEXT("Assigned RuntimeState is valid"), Assigned.RuntimeState.IsValid());
	TestTrue(TEXT("Copy assignment does not share RuntimeState"),
	         Source.RuntimeState.Get() != Assigned.RuntimeState.Get());
	TestSampleNear(*this, TEXT("Assigned ConstantForce"), Assigned.ConstantForce, Source.ConstantForce);

	Source.RuntimeState->Time = 4.0f;
	Assigned.RuntimeState->Time = 5.0f;
	TestSampleNear(*this, TEXT("Source Time independent after assign"), Source.RuntimeState->Time, 4.0f);
	TestSampleNear(*this, TEXT("Assigned Time independent"), Assigned.RuntimeState->Time, 5.0f);
	return true;
}

#endif
