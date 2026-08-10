// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"

namespace
{
constexpr float GProceduralWindTol = KINDA_SMALL_NUMBER;

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

void TestSampleNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected,
                    const float Tol = GProceduralWindTol)
{
	Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	              FMath::IsNearlyEqual(Actual, Expected, Tol));
}

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

struct FKawaiiPhysicsProceduralWindApplyTestForce : FKawaiiPhysics_ExternalForce_ProceduralWind
{
	void SetRandomizedForceScaleForTest(const float InScale)
	{
		RandomizedForceScale = InScale;
	}
};

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

	FKawaiiPhysics_ExternalForce_ProceduralWind OscillationWind;
	OscillationWind.OscillationForce = 1.0f;
	OscillationWind.OscillationPeriod = Period;
	TestSampleNear(*this, TEXT("Oscillation P/4"), OscillationWind.ComputeWindSample(Period * 0.25f).Oscillation,
	               1.0f);

	const float Time = 0.37f;
	TestSampleNear(*this, TEXT("Oscillation periodicity"),
	               OscillationWind.ComputeWindSample(Time).Oscillation,
	               OscillationWind.ComputeWindSample(Time + Period).Oscillation, 0.000001f);

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

	const float FixedTime = Wind.WavePeriod * 0.25f;
	const float RootWave = Wind.ComputeWindSample(FixedTime, 0.0f).Wave;
	const float TipWave = Wind.ComputeWindSample(FixedTime, 1.0f).Wave;
	TestTrue(FString::Printf(TEXT("Opposite sign: root=%.6f tip=%.6f"), RootWave, TipWave),
	         RootWave * TipWave < 0.0f);

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

	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const float Time = static_cast<float>(Index) * 0.013f;
		const float Envelope = Wind.ComputeWindSample(Time, 0.0f).Envelope;
		TestTrue(FString::Printf(TEXT("Envelope bounds %d: %.9f"), Index, Envelope),
		         Envelope >= Wind.EnvelopeMin - GProceduralWindTol &&
		         Envelope <= Wind.EnvelopeMax + GProceduralWindTol);
	}

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

	TestEqual(TEXT("StableHash(0, 0, 0)"), FWind::StableHash(0, 0, 0), 672839204u);
	TestEqual(TEXT("StableHash(123, 0, 0)"), FWind::StableHash(123, 0, 0), 961409981u);
	TestEqual(TEXT("StableHash(123, 1, 0)"), FWind::StableHash(123, 1, 0), 688218621u);
	TestEqual(TEXT("StableHash(123, -1, 0)"), FWind::StableHash(123, -1, 0), 2476066305u);
	TestEqual(TEXT("StableHash(-17, 42, 3)"), FWind::StableHash(-17, 42, 3), 1090092324u);

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

	TestSampleNear(*this, TEXT("Gust t=0.5"), Wind.ComputeWindSample(0.5f, 0.0f).Gust, 2.5f);
	TestSampleNear(*this, TEXT("Gust t=1.0"), Wind.ComputeWindSample(1.0f, 0.0f).Gust, 5.0f);
	TestSampleNear(*this, TEXT("Gust t=2.0"), Wind.ComputeWindSample(2.0f, 0.0f).Gust, 2.5f);
	TestSampleNear(*this, TEXT("Gust t=3.0"), Wind.ComputeWindSample(3.0f, 0.0f).Gust, 0.0f);
	TestSampleNear(*this, TEXT("Gust beyond"), Wind.ComputeWindSample(4.0f, 0.0f).Gust, 0.0f);

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
	Wind.ResetRuntimeState();
	Wind.RuntimeState->Time = 2.5f;
	Wind.RuntimeState->ActiveGust.StartTime = 0.0f;
	Wind.RuntimeState->ActiveGust.Strength = 4.0f;
	Wind.RuntimeState->ActiveGust.RiseTime = 0.5f;
	Wind.RuntimeState->ActiveGust.DecayTime = 3.0f;
	Wind.RuntimeState->ActiveGust.bIsActive = true;
	TestTrue(TEXT("Gust is active before reset"), Wind.ComputeWindSample(1.0f, 0.0f).Gust > 0.0f);

	Wind.ResetRuntimeState();
	TestSampleNear(*this, TEXT("Gust after reset"), Wind.ComputeWindSample(1.0f, 0.0f).Gust, 0.0f);
	TestSampleNear(*this, TEXT("Time after reset"), Wind.RuntimeState->Time, 0.0f);

	return true;
}

#endif
