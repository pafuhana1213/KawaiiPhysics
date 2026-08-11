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
	return A.Steady == B.Steady &&
		A.Oscillation == B.Oscillation &&
		A.Wave == B.Wave &&
		A.Envelope == B.Envelope &&
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

// Rate=[0,1]をNumSegments分割で走査し、Waveが最大となる区間インデックスを求める（WavePropagationテストでピーク位置の移動検出に使用）
int32 FindWavePeakIndex(const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind, const float Time,
                        const int32 NumSegments)
{
	int32 PeakIndex = 0;
	float PeakValue = -FLT_MAX;
	for (int32 Index = 0; Index <= NumSegments; ++Index)
	{
		const float Rate = static_cast<float>(Index) / static_cast<float>(NumSegments);
		const float Wave = Wind.ComputeWindSample(Time, Rate).Wave;
		if (Wave > PeakValue)
		{
			PeakValue = Wave;
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
};

// PreApplyが行うキャッシュ更新（合成波・envelope・random・gust・風向ベクトル）を、ポーズ評価なしで手動再現するヘルパー
void PrimeApplyCache(FKawaiiPhysicsProceduralWindApplyTestForce& Wind, const float Time)
{
	Wind.ResetRuntimeState();
	const FKawaiiPhysicsProceduralWindSample Sample = Wind.ComputeWindSample(Time, 0.0f);
	Wind.RuntimeState->Time = Time;
	Wind.RuntimeState->CachedSinesWithoutWave = Sample.Steady + Sample.Oscillation;
	Wind.RuntimeState->CachedEnvelope = Sample.Envelope;
	Wind.RuntimeState->CachedRandom = Sample.Random;
	Wind.RuntimeState->CachedGust = Sample.Gust;
	Wind.RuntimeState->CachedWindVector = Wind.WindDirection.Vector().GetSafeNormal();
	Wind.SetRandomizedForceScaleForTest(1.0f);
}

// 2ボーンチェーンへ同一フレーム分のWindを1回適用/NumSubsteps回に分割適用し、フレーム末の累積変位を返す
// （FramerateIndependenceテストでsubstep分割数への非依存性を検証するために使用）
FVector ApplyProceduralWindDisplacement(const int32 NumSubsteps)
{
	const float TotalDt = 1.0f / 30.0f;
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f, FVector::ZeroVector, FVector(0.0f, 0.0f, -1.0f));
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.Bone(1).LengthRateFromRoot = 0.75f;

	FKawaiiPhysicsProceduralWindApplyTestForce Wind;
	Wind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	Wind.WindDirection = FRotator::ZeroRotator;
	Wind.SteadyForce = 3.0f;
	Wind.OscillationForce = 2.0f;
	Wind.OscillationPeriod = 0.5f;
	Wind.WaveAmplitude = 4.0f;
	Wind.WavePeriod = 0.75f;
	Wind.WaveSpatialOffset = 120.0f;
	Wind.EnvelopeMin = 1.0f;
	Wind.EnvelopeMax = 1.0f;
	PrimeApplyCache(Wind, TotalDt);

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
	A.RandomPeriod = 0.37f;
	B.RandomPeriod = 0.37f;
	C.RandomPeriod = 0.37f;
	A.RandomSeed = 12345;
	B.RandomSeed = 12345;
	C.RandomSeed = 54321;

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindSteadyOnlyTest,
                                 "KawaiiPhysics.ProceduralWind.SteadyOnly",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindSteadyOnlyTest::RunTest(const FString& Parameters)
{
	// 定常風だけが有効な場合、時刻に関係なく合計が定常風と一致することを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.SteadyForce = 7.25f;

	for (const float Time : {0.0f, 0.1f, 0.5f, 1.0f, 3.75f})
	{
		const FKawaiiPhysicsProceduralWindSample Sample = Wind.ComputeWindSample(Time, 0.5f);
		TestSampleNear(*this, TEXT("Steady"), Sample.Steady, Wind.SteadyForce);
		TestSampleNear(*this, TEXT("Total"), Sample.Total, Wind.SteadyForce);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindSinePhaseTest,
                                 "KawaiiPhysics.ProceduralWind.SinePhase",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindSinePhaseTest::RunTest(const FString& Parameters)
{
	// 振動と波の位相、および周期性が実装式どおりになることを確認する。
	const float Period = 2.0f;

	// 振動成分: 1/4周期でsin位相がpi/2となり振幅そのものが出力される点、および1周期後の値が一致する周期性を確認
	FKawaiiPhysics_ExternalForce_ProceduralWind OscillationWind;
	OscillationWind.OscillationForce = 1.0f;
	OscillationWind.OscillationPeriod = Period;
	TestSampleNear(*this, TEXT("Oscillation P/4"), OscillationWind.ComputeWindSample(Period * 0.25f).Oscillation,
	               1.0f);

	const float Time = 0.37f;
	TestSampleNear(*this, TEXT("Oscillation periodicity"),
	               OscillationWind.ComputeWindSample(Time).Oscillation,
	               OscillationWind.ComputeWindSample(Time + Period).Oscillation, 0.000001f);

	// 波成分: WavePhase=90度によりt=0・Rate=0で位相pi/2（振幅最大）となる設定で、位相と周期性を確認
	FKawaiiPhysics_ExternalForce_ProceduralWind WaveWind;
	WaveWind.WaveAmplitude = 1.0f;
	WaveWind.WavePeriod = Period;
	WaveWind.WavePhase = 90.0f;
	TestSampleNear(*this, TEXT("Wave phase"), WaveWind.ComputeWindSample(0.0f, 0.0f).Wave, 1.0f);
	TestSampleNear(*this, TEXT("Wave periodicity"),
	               WaveWind.ComputeWindSample(Time, 0.35f).Wave,
	               WaveWind.ComputeWindSample(Time + Period, 0.35f).Wave, 0.000001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindWavePropagationTest,
                                 "KawaiiPhysics.ProceduralWind.WavePropagation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindWavePropagationTest::RunTest(const FString& Parameters)
{
	// 空間位相差で根元と毛先が逆相になり、時間経過でピークが毛先方向へ移動することを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.WaveAmplitude = 1.0f;
	Wind.WavePeriod = 2.0f;
	Wind.WaveSpatialOffset = 180.0f;

	// WaveSpatialOffset=180度により根元(Rate=0)と毛先(Rate=1)は空間位相差piとなり、符号が反転するはず
	const float FixedTime = Wind.WavePeriod * 0.25f;
	const float RootWave = Wind.ComputeWindSample(FixedTime, 0.0f).Wave;
	const float TipWave = Wind.ComputeWindSample(FixedTime, 1.0f).Wave;
	TestTrue(FString::Printf(TEXT("Opposite sign: root=%.6f tip=%.6f"), RootWave, TipWave),
	         RootWave * TipWave < 0.0f);

	// 時間を1/4周期進めるとピークが根元寄りから毛先寄りへ移動する（進行波としての空間伝播）ことを確認
	const int32 Peak0 = FindWavePeakIndex(Wind, Wind.WavePeriod * 0.25f, 1000);
	const int32 Peak1 = FindWavePeakIndex(Wind, Wind.WavePeriod * 0.50f, 1000);
	TestTrue(FString::Printf(TEXT("Peak moves root to tip: %d -> %d"), Peak0, Peak1),
	         Peak1 > Peak0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindEnvelopeBoundsTest,
                                 "KawaiiPhysics.ProceduralWind.EnvelopeBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindEnvelopeBoundsTest::RunTest(const FString& Parameters)
{
	// エンベロープが指定範囲内に収まり、最小最大が同値なら定数へ潰れることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.EnvelopeMin = 0.2f;
	Wind.EnvelopeMax = 1.5f;
	Wind.EnvelopeFrequency = 0.73f;
	Wind.EnvelopePhase = 17.0f;

	// 1000点サンプリングし、Envelopeが常に[EnvelopeMin, EnvelopeMax]の範囲内に収まることを確認
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const float Time = static_cast<float>(Index) * 0.013f;
		const float Envelope = Wind.ComputeWindSample(Time, 0.0f).Envelope;
		TestTrue(FString::Printf(TEXT("Envelope bounds %d: %.9f"), Index, Envelope),
		         Envelope >= Wind.EnvelopeMin - GProceduralWindTol &&
		         Envelope <= Wind.EnvelopeMax + GProceduralWindTol);
	}

	// MinとMaxを同値にした場合、Lerpの結果が時刻によらず定数へ潰れることを確認
	Wind.EnvelopeMin = 0.625f;
	Wind.EnvelopeMax = 0.625f;
	for (const float Time : {0.0f, 0.4f, 1.7f, 8.0f})
	{
		TestSampleNear(*this, TEXT("Envelope identity"), Wind.ComputeWindSample(Time, 0.0f).Envelope, 0.625f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindDynamicParamsTest,
                                 "KawaiiPhysics.ProceduralWind.DynamicParams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindDynamicParamsTest::RunTest(const FString& Parameters)
{
	// 上書き指定された項目だけが反映され、下限付き項目は安全化されることを確認する。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.WindDirection = FRotator(1.0f, 2.0f, 3.0f);
	Wind.SteadyForce = 2.0f;
	Wind.OscillationForce = 3.0f;
	Wind.OscillationPeriod = 1.0f;
	Wind.WaveAmplitude = 4.0f;
	Wind.WavePhase = 10.0f;
	Wind.DirectionNoiseAngle = 5.0f;
	Wind.TimeScale = 1.0f;

	// bOverride*をtrue/false交互に設定し、上書き対象と非対象の双方を1回のApplyDynamicParams呼び出しで検証する
	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideWindDirection = false;
	Params.WindDirection = FRotator(10.0f, 20.0f, 30.0f);
	Params.bOverrideSteadyForce = true;
	Params.SteadyForce = -5.0f;
	Params.bOverrideOscillationForce = false;
	Params.OscillationForce = 99.0f;
	Params.bOverrideOscillationPeriod = true;
	Params.OscillationPeriod = -10.0f;
	Params.bOverrideWaveAmplitude = false;
	Params.WaveAmplitude = 99.0f;
	Params.bOverrideWavePhase = true;
	Params.WavePhase = -45.0f;
	Params.bOverrideDirectionNoiseAngle = true;
	Params.DirectionNoiseAngle = -20.0f;
	Params.bOverrideTimeScale = true;
	Params.TimeScale = -1.0f;

	Wind.ApplyDynamicParams(Params);

	// 上書きされた項目は負値でも下限（0 または 0.01）へ安全化され、上書きされない項目は元値のまま残ることを確認
	TestTrue(TEXT("WindDirection unchanged"), Wind.WindDirection.Equals(FRotator(1.0f, 2.0f, 3.0f)));
	TestSampleNear(*this, TEXT("SteadyForce clamped"), Wind.SteadyForce, 0.0f);
	TestSampleNear(*this, TEXT("OscillationForce unchanged"), Wind.OscillationForce, 3.0f);
	TestSampleNear(*this, TEXT("OscillationPeriod clamped"), Wind.OscillationPeriod, 0.01f);
	TestSampleNear(*this, TEXT("WaveAmplitude unchanged"), Wind.WaveAmplitude, 4.0f);
	TestSampleNear(*this, TEXT("WavePhase unclamped"), Wind.WavePhase, -45.0f);
	TestSampleNear(*this, TEXT("DirectionNoiseAngle clamped"), Wind.DirectionNoiseAngle, 0.0f);
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
	Params.bOverrideSteadyForce = true;
	Params.SteadyForce = 8.0f;
	Params.bOverrideOscillationPeriod = true;
	Params.OscillationPeriod = 0.25f;

	// Mutex配下でPendingParams/PendingGustを設定し、Apply側スレッドからの非同期リクエストを模擬する
	{
		FScopeLock Lock(&Wind.RuntimeState->Mutex);
		Wind.RuntimeState->PendingParams = Params;
		Wind.RuntimeState->PendingGust = FKawaiiProceduralWindGustRequest{
			6.0f,
			0.1f,
			0.5f
		};
	}

	Wind.ConsumePendingRequests();

	TestSampleNear(*this, TEXT("Pending SteadyForce applied"), Wind.SteadyForce, 8.0f);
	TestSampleNear(*this, TEXT("Pending OscillationPeriod applied"), Wind.OscillationPeriod, 0.25f);
	TestSampleNear(*this, TEXT("ActiveGust StartTime"), Wind.RuntimeState->ActiveGust.StartTime, 2.25f);
	TestSampleNear(*this, TEXT("ActiveGust Strength"), Wind.RuntimeState->ActiveGust.Strength, 6.0f);
	TestSampleNear(*this, TEXT("ActiveGust RiseTime"), Wind.RuntimeState->ActiveGust.RiseTime, 0.1f);
	TestSampleNear(*this, TEXT("ActiveGust DecayTime"), Wind.RuntimeState->ActiveGust.DecayTime, 0.5f);
	TestTrue(TEXT("ActiveGust active"), Wind.RuntimeState->ActiveGust.bIsActive);
	TestFalse(TEXT("PendingParams reset"), Wind.RuntimeState->PendingParams.IsSet());
	TestFalse(TEXT("PendingGust reset"), Wind.RuntimeState->PendingGust.IsSet());

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
	TestTrue(TEXT("Wave-including displacement is non-zero"), !OneStep.IsNearlyZero());
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
	Wind.RuntimeState->ActiveGust.bIsActive = true;
	Wind.RuntimeState->CachedSinesWithoutWave = 1.0f;
	Wind.RuntimeState->CachedEnvelope = 2.0f;
	Wind.RuntimeState->CachedRandom = 3.0f;
	Wind.RuntimeState->CachedGust = 4.0f;
	Wind.RuntimeState->CachedWindVector = FVector(1.0f, 2.0f, 3.0f);
	Wind.RuntimeState->PendingParams = FKawaiiProceduralWindDynamicParams();
	Wind.RuntimeState->PendingGust = FKawaiiProceduralWindGustRequest{1.0f, 0.2f, 0.3f};
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
	TestSampleNear(*this, TEXT("CachedSinesWithoutWave after reset"), Wind.RuntimeState->CachedSinesWithoutWave, 0.0f);
	TestSampleNear(*this, TEXT("CachedEnvelope after reset"), Wind.RuntimeState->CachedEnvelope, 1.0f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindCopyRuntimeStateIndependenceTest,
                                 "KawaiiPhysics.ProceduralWind.CopyRuntimeStateIndependence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindCopyRuntimeStateIndependenceTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind Source;
	Source.SteadyForce = 12.0f;
	Source.RuntimeState->Time = 1.0f;

	FKawaiiPhysics_ExternalForce_ProceduralWind Copied(Source);
	TestTrue(TEXT("Copied RuntimeState is valid"), Copied.RuntimeState.IsValid());
	TestTrue(TEXT("Copy constructor does not share RuntimeState"),
	         Source.RuntimeState.Get() != Copied.RuntimeState.Get());
	TestSampleNear(*this, TEXT("Copied SteadyForce"), Copied.SteadyForce, Source.SteadyForce);

	Source.RuntimeState->Time = 2.0f;
	Copied.RuntimeState->Time = 3.0f;
	TestSampleNear(*this, TEXT("Source Time independent"), Source.RuntimeState->Time, 2.0f);
	TestSampleNear(*this, TEXT("Copied Time independent"), Copied.RuntimeState->Time, 3.0f);

	FKawaiiPhysics_ExternalForce_ProceduralWind Assigned;
	Assigned = Source;
	TestTrue(TEXT("Assigned RuntimeState is valid"), Assigned.RuntimeState.IsValid());
	TestTrue(TEXT("Copy assignment does not share RuntimeState"),
	         Source.RuntimeState.Get() != Assigned.RuntimeState.Get());
	TestSampleNear(*this, TEXT("Assigned SteadyForce"), Assigned.SteadyForce, Source.SteadyForce);

	Source.RuntimeState->Time = 4.0f;
	Assigned.RuntimeState->Time = 5.0f;
	TestSampleNear(*this, TEXT("Source Time independent after assign"), Source.RuntimeState->Time, 4.0f);
	TestSampleNear(*this, TEXT("Assigned Time independent"), Assigned.RuntimeState->Time, 5.0f);
	return true;
}

#endif
