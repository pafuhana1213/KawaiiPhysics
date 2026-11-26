// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsTestHarness.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/OutputDevice.h"

namespace KawaiiPhysicsWindSpecTestPrivate
{

void ConfigureMeasuredWind(FKawaiiPhysics_ExternalForce_ProceduralWind& MeasuredWind)
{
	MeasuredWind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	MeasuredWind.WindDirection = FVector::ForwardVector;
	MeasuredWind.ConstantForce = 200.0f;
	MeasuredWind.SwayForce = 3.0f;
	MeasuredWind.SwayPeriod = 0.7f;
	MeasuredWind.RippleForce = 4.0f;
	MeasuredWind.RipplePeriod = 0.75f;
	MeasuredWind.RippleTipPhaseDelay = 120.0f;
	MeasuredWind.StrengthCycleRange = FFloatInterval(0.5f, 1.5f);
	MeasuredWind.StrengthCyclePeriod = 4.0f;
	MeasuredWind.RandomForce = 2.0f;
	MeasuredWind.RandomForcePeriod = 0.3f;
	MeasuredWind.Seed = 291;
}

KawaiiPhysicsNodeFixture::FOptions MakeWindOptions(const float WindTimeScale = 1.0f)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
	NodeOptions.ConfigureNode = [WindTimeScale](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		auto& MeasuredWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
		ConfigureMeasuredWind(MeasuredWind);
		MeasuredWind.TimeScale = WindTimeScale;
	};
	return NodeOptions;
}

FKawaiiPhysics_ExternalForce_ProceduralWind& GetMeasuredWind(KawaiiPhysicsNodeFixture::FFixture& NodeFixture)
{
	return NodeFixture.Node.ExternalForces[0].GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>();
}

// TW-PW-07・TW-PW-09。3014: r=1、δ=90° の符号反転で Ripple(0,1) が -1 から +1 になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecRippleDirectionTest,
	"KawaiiPhysics.WindSpec.Wave.RippleDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecRippleDirectionTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind MeasuredWind;
	MeasuredWind.RippleForce = 1.0f;
	MeasuredWind.RipplePeriod = 2.0f;
	MeasuredWind.RippleTipPhaseDelay = 90.0f;
	// sin(0 - 1×π/2)=-1。float の三角関数の丸めを 1e-6 許す。
	bool bOk = TestTrue(TEXT("毛先は四分の一周期遅れる"),
		FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0.0f, 1.0f).Ripple, -1.0f, 1e-6f));
	const float RootSample = MeasuredWind.ComputeWindSample(0.3f, 0.0f).Ripple;
	// r=0.5 の遅れは 0.5×2×90/360=0.25 秒。
	bOk &= TestTrue(TEXT("進行波は四分の一秒後に届く"),
		FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0.55f, 0.5f).Ripple, RootSample, 1e-4f));
	MeasuredWind.RippleTipPhaseDelay = -90.0f;
	bOk &= TestTrue(TEXT("負の位相遅れは逆走する"),
		FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0.05f, 0.5f).Ripple, RootSample, 1e-4f));
	return bOk;
}

// TW-PW-07・TW-PW-09。3016: Apply の r の符号が反転すると、t=1/30・r=0.75 の Ripple は 4sin(2π/22.5−π/2)≈-3.85 から +3.85 へ変わる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecRippleApplyTest,
	"KawaiiPhysics.WindSpec.Wave.RippleApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecRippleApplyTest::RunTest(const FString& Parameters)
{
	constexpr float FrameDt = 1.0f / 30.0f;
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetTimeState(FrameDt, FrameDt);
	Accessor.Bone(1).LengthRateFromRoot = 0.75f;
	FAnimInstanceProxy AnimProxy;
	FComponentSpacePoseContext PoseContext(&AnimProxy);
	FKawaiiPhysics_ExternalForce_ProceduralWind MeasuredWind;
	MeasuredWind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	MeasuredWind.WindDirection = FVector::ForwardVector;
	MeasuredWind.ConstantForce = 3.0f;
	MeasuredWind.SwayForce = 2.0f;
	MeasuredWind.SwayPeriod = 0.5f;
	MeasuredWind.RippleForce = 4.0f;
	MeasuredWind.RipplePeriod = 0.75f;
	MeasuredWind.RippleTipPhaseDelay = 120.0f;
	MeasuredWind.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	MeasuredWind.PreApply(Accessor.Node, PoseContext);
	const FVector InitialLocation = Accessor.Bone(1).Location;
	MeasuredWind.Apply(Accessor.Bone(1), Accessor.Node, PoseContext);
	// t=dt。Apply は入力力に dt を一回掛けて位置へ加える。r×δ=90°。
	const float WindTime = FrameDt;
	const float ExpectedForce = 3.0f + 2.0f * FMath::Sin(2.0f * PI * WindTime / 0.5f) +
		4.0f * FMath::Sin(2.0f * PI * WindTime / 0.75f - PI / 2.0f);
	const FVector ExpectedDisplacement = FVector::ForwardVector * ExpectedForce * FrameDt;
	// sin と float の乗算順による丸めを 1e-5 cm 許す。
	bool bOk = TestTrue(TEXT("Apply の Ripple は毛先ほど遅れる"),
		(Accessor.Bone(1).Location - InitialLocation).Equals(ExpectedDisplacement, 1e-5f));
	bOk &= TestTrue(TEXT("対照: Ripple の寄与は零でない"),
		FMath::Abs(4.0f * FMath::Sin(2.0f * PI * WindTime / 0.75f - PI / 2.0f)) > 1.0f);
	return bOk;
}

// TW-PW-01・TW-PW-12。3017: Random にも StrengthCycle を掛けると、二条件の残差が異なる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecRandomOutsideCycleTest,
	"KawaiiPhysics.WindSpec.Wave.RandomOutsideCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecRandomOutsideCycleTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind LowCycleWind;
	FKawaiiPhysics_ExternalForce_ProceduralWind HighCycleWind;
	for (auto* MeasuredWind : {&LowCycleWind, &HighCycleWind})
	{
		MeasuredWind->ConstantForce = 2.0f;
		MeasuredWind->SwayForce = 1.0f;
		MeasuredWind->SwayPeriod = 0.7f;
		MeasuredWind->RippleForce = 0.8f;
		MeasuredWind->RipplePeriod = 0.4f;
		MeasuredWind->RandomForce = 3.0f;
		MeasuredWind->RandomForcePeriod = 0.3f;
		MeasuredWind->Seed = 291;
	}
	LowCycleWind.StrengthCycleRange = FFloatInterval(0.5f, 0.5f);
	HighCycleWind.StrengthCycleRange = FFloatInterval(2.0f, 2.0f);
	bool bOk = true;
	bool bSawRandom = false;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const float SampleTime = Index * 0.071f;
		const auto LowSample = LowCycleWind.ComputeWindSample(SampleTime, 0.4f);
		const auto HighSample = HighCycleWind.ComputeWindSample(SampleTime, 0.4f);
		// 基本波形だけが倍率の内側。二つの残差は同じ Random + Gust。
		const float LowResidual = LowSample.Total -
			(LowSample.Constant + LowSample.Sway + LowSample.Ripple) * 0.5f;
		const float HighResidual = HighSample.Total -
			(HighSample.Constant + HighSample.Sway + HighSample.Ripple) * 2.0f;
		bOk &= TestTrue(FString::Printf(TEXT("Random の残差 %d"), Index),
			FMath::IsNearlyEqual(LowResidual, HighResidual, 1e-5f));
		bSawRandom |= FMath::Abs(LowSample.Random) > 0.1f;
	}
	bOk &= TestTrue(TEXT("対照: Random が非零"), bSawRandom);
	return bOk;
}

// PW-2・TW-PW-10・TW-PW-11・TW-PW-13。生き残り mutant なし。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecStrengthCyclePhaseTest,
	"KawaiiPhysics.WindSpec.Wave.StrengthCyclePhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecStrengthCyclePhaseTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind MeasuredWind;
	MeasuredWind.ConstantForce = 2.0f;
	MeasuredWind.StrengthCycleRange = FFloatInterval(0.5f, 1.5f);
	MeasuredWind.StrengthCyclePeriod = 4.0f;
	// 0.5 + (1.5-0.5)×(1+sin(2πt/4+φ))/2。三角関数の丸めを 1e-6 許す。
	bool bOk = TestTrue(TEXT("原点は中央値"), FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0).StrengthCycle, 1.0f, 1e-6f));
	bOk &= TestTrue(TEXT("四分の一周期で最大"), FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(1).StrengthCycle, 1.5f, 1e-6f));
	bOk &= TestTrue(TEXT("四分の三周期で最小"), FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(3).StrengthCycle, 0.5f, 1e-6f));
	MeasuredWind.StrengthCyclePhaseOffset = 90.0f;
	bOk &= TestTrue(TEXT("正の位相は先へ進む"), FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0).StrengthCycle, 1.5f, 1e-6f));
	MeasuredWind.StrengthCyclePhaseOffset = 450.0f;
	bOk &= TestTrue(TEXT("360 度追加しても同じ"), FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0).StrengthCycle, 1.5f, 1e-6f));
	MeasuredWind.StrengthCycleRange = FFloatInterval(2.0f, 2.0f);
	bOk &= TestTrue(TEXT("一点の範囲は基本波形を二倍にする"),
		FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0).Total, 4.0f, 1e-6f));
	return bOk;
}

// PW-3・TW-PW-14・TW-PW-15。生き残り mutant なし。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecRandomRangeAndStretchTest,
	"KawaiiPhysics.WindSpec.Wave.RandomRangeAndStretch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecRandomRangeAndStretchTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind ShortPeriodWind;
	FKawaiiPhysics_ExternalForce_ProceduralWind LongPeriodWind;
	ShortPeriodWind.RandomForce = LongPeriodWind.RandomForce = 2.0f;
	ShortPeriodWind.RandomForcePeriod = 0.3f;
	LongPeriodWind.RandomForcePeriod = 0.6f;
	ShortPeriodWind.Seed = LongPeriodWind.Seed = 491;
	bool bOk = true;
	float Sum = 0.0f;
	bool bSawRandom = false;
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		const float SampleTime = Index * 0.103f;
		const float ShortRandom = ShortPeriodWind.ComputeWindSample(SampleTime).Random;
		const float LongRandom = LongPeriodWind.ComputeWindSample(2.0f * SampleTime).Random;
		bOk &= TestTrue(FString::Printf(TEXT("Random の範囲 %d"), Index), FMath::Abs(ShortRandom) <= 2.000001f);
		bOk &= TestTrue(FString::Printf(TEXT("時間軸の二倍 %d"), Index), FMath::IsNearlyEqual(ShortRandom, LongRandom, 1e-5f));
		Sum += ShortRandom;
		bSawRandom |= FMath::Abs(ShortRandom) > 0.1f;
	}
	// 約 1400 個の格子区間を走査するため、標本平均の許容は振幅の 10% とする。
	bOk &= TestTrue(TEXT("Random の平均は零付近"), FMath::Abs(Sum / 4096.0f) <= 0.2f);
	bOk &= TestTrue(TEXT("対照: Random が非零"), bSawRandom);
	return bOk;
}

// PW-5・TW-PW-28・TW-PW-29。3035: r を 1-r で引くと r=0 の変位が零でなく、r=1 の変位が零になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecBoneLengthCurveTest,
	"KawaiiPhysics.WindSpec.Bone.BoneLengthCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecBoneLengthCurveTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetTimeState(1.0f / 60.0f, 1.0f / 60.0f);
	FAnimInstanceProxy AnimProxy;
	FComponentSpacePoseContext PoseContext(&AnimProxy);
	FKawaiiPhysics_ExternalForce_ProceduralWind MeasuredWind;
	MeasuredWind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	MeasuredWind.WindDirection = FVector::ForwardVector;
	MeasuredWind.ConstantForce = 60.0f;
	MeasuredWind.PreApply(Accessor.Node, PoseContext);
	bool bOk = TestTrue(TEXT("対照: 対象ボーンがある"), Accessor.Node.ModifyBones.Num() > 1);
	const FVector InitialLocation = Accessor.Bone(1).Location;
	MeasuredWind.Apply(Accessor.Bone(1), Accessor.Node, PoseContext);
	// C=60、dt=1/60 なのでカーブ無しの変位は +X に 1 cm。
	const FVector EmptyCurveDisplacement = Accessor.Bone(1).Location - InitialLocation;
	bOk &= TestTrue(TEXT("キーなしの倍率は一"), EmptyCurveDisplacement.Equals(FVector::ForwardVector, 1e-5f));
	MeasuredWind.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(0.0f, 0.0f);
	MeasuredWind.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(1.0f, 1.0f);
	for (const float BoneRate : {0.0f, 0.5f, 1.0f})
	{
		Accessor.Bone(1).Location = InitialLocation;
		Accessor.Bone(1).LengthRateFromRoot = BoneRate;
		MeasuredWind.Apply(Accessor.Bone(1), Accessor.Node, PoseContext);
		// 線形キー (0,0)→(1,1) なので倍率は r、期待変位は r cm。
		bOk &= TestTrue(TEXT("ボーン長カーブの倍率は長さ比"),
			(Accessor.Bone(1).Location - InitialLocation).Equals(FVector::ForwardVector * BoneRate, 1e-5f));
	}
	return bOk;
}

// TW-GU-02。生き残り mutant なし。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecGustEnvelopeTest,
	"KawaiiPhysics.WindSpec.Gust.TriangleAndHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecGustEnvelopeTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind MeasuredWind;
	MeasuredWind.ResetRuntimeState();
	auto& ActiveGust = MeasuredWind.RuntimeState->Gusts.Items[0];
	ActiveGust.bIsActive = true;
	ActiveGust.StartTime = 0.0f;
	ActiveGust.Strength = 3.0f;
	ActiveGust.RiseTime = 0.4f;
	ActiveGust.HoldTime = 0.0f;
	ActiveGust.DecayTime = 0.6f;
	MeasuredWind.RuntimeState->Gusts.Num = 1;
	bool bOk = TestTrue(TEXT("対照: 突風が登録済み"), MeasuredWind.RuntimeState->Gusts.Num == 1);
	for (int32 Index = 0; Index <= 1200; ++Index)
	{
		const float Elapsed = Index * 0.001f;
		const float Expected = Elapsed < 0.4f ? 3.0f * Elapsed / 0.4f :
			FMath::Max(0.0f, 3.0f * (1.0f - (Elapsed - 0.4f) / 0.6f));
		// 入力の 0.001 秒刻みと float 除算の丸めを 1e-6 許す。
		bOk &= TestTrue(FString::Printf(TEXT("三角エンベロープ %d"), Index),
			FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(Elapsed).Gust, Expected, 1e-6f));
	}
	ActiveGust.Strength = 2.0f;
	ActiveGust.RiseTime = 0.2f;
	ActiveGust.HoldTime = 0.5f;
	ActiveGust.DecayTime = 0.3f;
	// τ=0.5 は Hold、τ=0.85 は Decay の中点。
	bOk &= TestTrue(TEXT("Hold 中は頂点の強さ"), FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0.5f).Gust, 2.0f, 1e-6f));
	bOk &= TestTrue(TEXT("Decay 中点は半分"), FMath::IsNearlyEqual(MeasuredWind.ComputeWindSample(0.85f).Gust, 1.0f, 1e-6f));
	return bOk;
}

// TW-PR-04・PR-6。3064: Strong の下限が 0.6 になると固定値 0.7 の比較で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecBuiltInPresetsTest,
	"KawaiiPhysics.WindSpec.Preset.DocumentedDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecBuiltInPresetsTest::RunTest(const FString& Parameters)
{
	const auto ActualPresets = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	if (!TestEqual(TEXT("組み込みプリセットは三件"), ActualPresets.Num(), 3)) return false;
	struct FExpectedPreset
	{
		float Constant, Sway, SwayPeriod, Ripple, RipplePeriod, Delay, CycleMin, CycleMax, CyclePeriod, Random, RandomPeriod, NoiseAngle;
	};
	// 仕様表をそのまま固定する。数値はデータ定義なので丸めを許さない。
	const FExpectedPreset Expected[] = {
		{2, 1, 3, 1, 1.2f, 120, 0.6f, 1, 20, 0.5f, 0.8f, 5},
		{8, 4, 1.6f, 3, 0.5f, 120, 0.7f, 1.3f, 12, 2, 0.5f, 10},
		{15, 10, 0.9f, 8, 0.3f, 180, 0.5f, 1.6f, 7, 6, 0.3f, 20}
	};
	bool bOk = true;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const auto& Actual = ActualPresets[Index];
		const auto& Reference = Expected[Index];
		bOk &= TestTrue(FString::Printf(TEXT("仕様表の全値 %d"), Index),
			Actual.ConstantForce == Reference.Constant && Actual.SwayForce == Reference.Sway &&
			Actual.SwayPeriod == Reference.SwayPeriod && Actual.RippleForce == Reference.Ripple &&
			Actual.RipplePeriod == Reference.RipplePeriod && Actual.RippleTipPhaseDelay == Reference.Delay &&
			Actual.StrengthCycleRange.Min == Reference.CycleMin && Actual.StrengthCycleRange.Max == Reference.CycleMax &&
			Actual.StrengthCyclePeriod == Reference.CyclePeriod && Actual.RandomForce == Reference.Random &&
			Actual.RandomForcePeriod == Reference.RandomPeriod && Actual.WindDirectionNoiseAngle == Reference.NoiseAngle);
		bOk &= TestTrue(TEXT("Sway と Ripple の周期比"), Actual.SwayPeriod / Actual.RipplePeriod >= 2.5f - 1e-5f &&
			Actual.SwayPeriod / Actual.RipplePeriod <= 3.5f);
	}
	return bOk;
}

// TW-PW-25・TW-PW-27。3029: TimeScale=0 でも UnscaledTime が進み、向きが変化する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecDirectionClockTest,
	"KawaiiPhysics.WindSpec.Direction.ScaledClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecDirectionClockTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture FrozenFixture(MakeWindOptions(0.0f));
	auto& FrozenWind = GetMeasuredWind(FrozenFixture);
	FrozenWind.WindDirectionNoiseAngle = 15.0f;
	FrozenWind.WindDirectionNoisePeriod = 0.3f;
	FrozenFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("対照: ModifyBones がある"), FrozenFixture.Node.ModifyBones.Num() > 0);
	const FVector FirstDirection = FrozenWind.RuntimeState->CachedWindVector;
	bOk &= TestTrue(TEXT("対照: 風向きが単位ベクトル"), FMath::IsNearlyEqual(FirstDirection.Size(), 1.0f, 1e-4f));
	for (int32 Index = 0; Index < 10; ++Index)
	{
		FrozenFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("停止中は風向きがビット一致"), FrozenWind.RuntimeState->CachedWindVector == FirstDirection);
	}
	KawaiiPhysicsNodeFixture::FFixture DoubleFixture(MakeWindOptions(2.0f));
	KawaiiPhysicsNodeFixture::FFixture NormalFixture(MakeWindOptions(1.0f));
	auto& DoubleWind = GetMeasuredWind(DoubleFixture);
	auto& NormalWind = GetMeasuredWind(NormalFixture);
	DoubleWind.WindDirectionNoiseAngle = NormalWind.WindDirectionNoiseAngle = 15.0f;
	DoubleWind.WindDirectionNoisePeriod = NormalWind.WindDirectionNoisePeriod = 0.3f;
	bool bChanged = false;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		DoubleFixture.EvaluateFrame();
		NormalFixture.EvaluateFrame();
		NormalFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("二倍の風時計は二倍のフレームに一致"),
			DoubleWind.RuntimeState->CachedWindVector.Equals(NormalWind.RuntimeState->CachedWindVector, 1e-4f));
		bChanged |= !DoubleWind.RuntimeState->CachedWindVector.Equals(FirstDirection, 1e-3f);
	}
	bOk &= TestTrue(TEXT("対照: 進む風時計では風向きが変わる"), bChanged);
	return bOk;
}

// TW-PW-20・TW-PW-22。3030: 円板長を切り詰めないと 15° を超える。3032: 非単位入力がそのままキャッシュされる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecDirectionConeTest,
	"KawaiiPhysics.WindSpec.Direction.ConeAndUnitLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecDirectionConeTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture UnitFixture(MakeWindOptions());
	auto& MeasuredWind = GetMeasuredWind(UnitFixture);
	MeasuredWind.WindDirection = FVector(3.0f, 4.0f, 0.0f);
	MeasuredWind.WindDirectionNoiseAngle = 0.0f;
	UnitFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("対照: ModifyBones がある"), UnitFixture.Node.ModifyBones.Num() > 0);
	// (3,4,0) の長さは 5。正規化した向きは (0.6,0.8,0)。
	bOk &= TestTrue(TEXT("非単位入力を正規化"),
		MeasuredWind.RuntimeState->CachedWindVector.Equals(FVector(0.6f, 0.8f, 0.0f), 1e-4f));
	bOk &= TestTrue(TEXT("向きのノルムは一"), FMath::IsNearlyEqual(MeasuredWind.RuntimeState->CachedWindVector.Size(), 1.0f, 1e-4f));
	MeasuredWind.WindDirection = FVector::ForwardVector;
	MeasuredWind.WindDirectionNoiseAngle = 15.0f;
	MeasuredWind.WindDirectionNoisePeriod = 0.05f;
	bool bOutsideHalfAngle = false;
	for (int32 Index = 0; Index < 2000; ++Index)
	{
		UnitFixture.EvaluateFrame();
		const FVector Direction = MeasuredWind.RuntimeState->CachedWindVector;
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Direction.X, -1.0, 1.0)));
		// acos と正規化の float 丸めとして 0.001° を許す。
		bOk &= TestTrue(FString::Printf(TEXT("円錐の半角 %d"), Index), Angle <= 15.001f);
		bOutsideHalfAngle |= Angle > 7.5f;
	}
	bOk &= TestTrue(TEXT("対照: ノイズは半角の半分を超えて効く"), bOutsideHalfAngle);
	return bOk;
}

// TW-RI-11。3152: authored 風の印が無いと初回評価で再初期化され、時刻 5 が消える。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecAuthoredForceInitializedOnceTest,
	"KawaiiPhysics.WindSpec.RuntimeInit.AuthoredWindClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecAuthoredForceInitializedOnceTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeWindOptions(1.25f));
	auto& MeasuredWind = GetMeasuredWind(NodeFixture);
	if (!TestTrue(TEXT("対照: authored 風の RuntimeState がある"), MeasuredWind.RuntimeState.IsValid())) return false;
	MeasuredWind.RuntimeState->Time = 5.0f;
	NodeFixture.EvaluateFrame();
	// 一フレーム 1/60 秒、TimeScale 1.25 なので 5 + 1/48 秒。
	bool bOk = TestTrue(TEXT("初回評価は authored 時計をリセットしない"),
		FMath::IsNearlyEqual(MeasuredWind.RuntimeState->Time, 5.0f + 1.0f / 48.0f, 1e-4f));
	bOk &= TestTrue(TEXT("対照: ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
	return bOk;
}

struct FSpaceConversionProbe : FKawaiiPhysics_ExternalForce_Gravity
{
	using FKawaiiPhysics_ExternalForce_Gravity::ConvertExternalForceToSimulationSpace;
};

// TW-WS-15。生き残り mutant なし。BoneSpace を変換すると各 SimulationSpace の入力 (1,2,3) と一致しなくなる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecForceSpaceConversionTest,
	"KawaiiPhysics.WindSpec.Space.BoneSpacePassThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecForceSpaceConversionTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	Accessor.SetWorldSpaceTransformForTest(FTransform(FRotator(0.0f, 90.0f, 0.0f)));
	FAnimInstanceProxy AnimProxy;
	FComponentSpacePoseContext PoseContext(&AnimProxy);
	FSpaceConversionProbe ConversionProbe;
	ConversionProbe.ExternalForceSpace = EExternalForceSpace::BoneSpace;
	bool bOk = TestTrue(TEXT("対照: 二ボーンの入力"), Accessor.Node.ModifyBones.Num() > 0);
	for (const auto SimulationSpace : {EKawaiiPhysicsSimulationSpace::ComponentSpace,
		EKawaiiPhysicsSimulationSpace::WorldSpace, EKawaiiPhysicsSimulationSpace::BaseBoneSpace})
	{
		Accessor.SetSimulationSpace(SimulationSpace);
		bOk &= TestTrue(TEXT("BoneSpace は変換せずに渡す"),
			ConversionProbe.ConvertExternalForceToSimulationSpace(Accessor.Node, PoseContext, FVector(1.0f, 2.0f, 3.0f)) ==
			FVector(1.0f, 2.0f, 3.0f));
	}
	ConversionProbe.ExternalForceSpace = EExternalForceSpace::WorldSpace;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	// Yaw +90° の逆回転では、ワールド +X はコンポーネント -Y。
	bOk &= TestTrue(TEXT("WorldSpace を ComponentSpace へ逆回転"),
		ConversionProbe.ConvertExternalForceToSimulationSpace(Accessor.Node, PoseContext, FVector::ForwardVector)
			.Equals(FVector(0.0f, -1.0f, 0.0f), 1e-4f));
	return bOk;
}

// TW-GR-04・GR-4。生き残り mutant なし。警告を毎フレーム出すと、互換符号で二回評価した際の警告数が上限一回を超える。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecGravityLegacySignTest,
	"KawaiiPhysics.WindSpec.Gravity.LegacySign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecGravityLegacySignTest::RunTest(const FString& Parameters)
{
	struct FScopedLegacyWarningCounter : FOutputDevice
	{
		FThreadSafeCounter WarningCount;
		// ログは非同期に出力先へ配られることがあるので、読む前に Flush して数え終わらせる。
		int32 ReadCount() { GLog->Flush(); return WarningCount.GetValue(); }
		FScopedLegacyWarningCounter() { GLog->AddOutputDevice(this); }
		~FScopedLegacyWarningCounter() override { GLog->RemoveOutputDevice(this); }
		bool CanBeUsedOnAnyThread() const override { return true; }
		void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			// テストの実行基盤がテスト中の警告を LogAutomationController でもう一度流すので、本番のカテゴリの行だけ数える。
			if (Category == FName(TEXT("LogKawaiiPhysics")) && Verbosity == ELogVerbosity::Warning &&
				FCString::Strstr(Message, TEXT("LegacyCharacterGravityScaleSign is enabled")))
			{
				WarningCount.Increment();
			}
		}
	};
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	NodeOptions.ActorKind = KawaiiPhysicsNodeFixture::EActorKind::Character;
	NodeOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::WorldSpace;
	NodeOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		auto& GravityForce = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Gravity>(AnimNode);
		GravityForce.bUseCharacterGravityScale = true;
	};
	KawaiiPhysicsNodeFixture::FScopedCVar LegacySign(
		TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("0"));
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	NodeFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("対照: ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
	const auto& GravityForce = NodeFixture.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_Gravity>();
	const FVector NewForce = FKawaiiPhysicsTestAccessor::GetExternalForceVector(GravityForce);
	bOk &= TestTrue(TEXT("対照: Character 重力が効く"), NewForce.Z < -100.0f);
	if (!TestNotNull(TEXT("互換 CVar がある"), LegacySign.Variable)) return false;
	LegacySign.Variable->Set(1, ECVF_SetByConsole);
	{
		FScopedLegacyWarningCounter WarningCounter;
		NodeFixture.EvaluateFrame();
		const FVector OldForce = FKawaiiPhysicsTestAccessor::GetExternalForceVector(GravityForce);
		// 同じ Character の重力ベクトルを符号だけ反転する。float の丸めを 1e-4 許す。
		bOk &= TestTrue(TEXT("旧符号は新符号の逆"), OldForce.Equals(-NewForce, 1e-4f));
		NodeFixture.EvaluateFrame();
		// プロセスで以前に警告済みなら零回、未警告なら一回を許す。
		bOk &= TestTrue(TEXT("互換符号の非推奨警告は二フレームで高々一回"),
			WarningCounter.ReadCount() <= 1);
	}
	return bOk;
}

// TW-GR-05。既定 CVar を 1 にする mutant は SetByConstructor の環境で 0 の比較に落ちる。上書き済みの環境では警告して既定値の主張を保留する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecGravityDefaultSignTest,
	"KawaiiPhysics.WindSpec.Gravity.DefaultSign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecGravityDefaultSignTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* LegacySign = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"));
	if (!TestNotNull(TEXT("重力符号の CVar がある"), LegacySign)) return false;
	if ((LegacySign->GetFlags() & ECVF_SetByMask) != ECVF_SetByConstructor)
	{
		AddWarning(TEXT("互換 CVar が上書きされているため、この構成では既定値を確かめられない"));
		return true;
	}
	return TestEqual(TEXT("互換 CVar の既定は 0"), LegacySign->GetInt(), 0);
}

} // namespace KawaiiPhysicsWindSpecTestPrivate

#endif
