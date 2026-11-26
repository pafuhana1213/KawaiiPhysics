// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsLibrary.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "AnimNode_KawaiiPhysicsSharedPublisherInternal.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "KawaiiPhysicsTypes.h"

#include <limits>

namespace KawaiiPhysicsWindInputRobustnessTestPrivate
{

static bool IsFiniteSample(const FKawaiiPhysicsProceduralWindSample& Sample)
{
	return FMath::IsFinite(Sample.Constant) && FMath::IsFinite(Sample.Sway) &&
		FMath::IsFinite(Sample.Ripple) && FMath::IsFinite(Sample.StrengthCycle) &&
		FMath::IsFinite(Sample.Random) && FMath::IsFinite(Sample.Gust) && FMath::IsFinite(Sample.Total);
}

enum class EWindCase : uint8
{
	Normal, Disabled, ZeroDirection, InvalidDirection, InfiniteRandom, ZeroRandom,
	InvalidRipple, ZeroRipple, InvalidCycle, UnitCycle, ZeroSwayPeriod, MinimumSwayPeriod,
	InfiniteSwayPeriod, InvalidTimeScale, ZeroTimeScale, InvalidForceRate, InfiniteNoiseAngle
};

static void ConfigureWind(FKawaiiPhysics_ExternalForce_ProceduralWind& Force, const EWindCase Case)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	Force.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	Force.WindDirection = FVector::ForwardVector;
	Force.ConstantForce = 200.0f;
	Force.SwayForce = 25.0f;
	Force.SwayPeriod = 0.3f;
	Force.RippleForce = 20.0f;
	Force.RipplePeriod = 0.4f;
	Force.RandomForce = 10.0f;
	Force.Seed = 42;
	Force.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	switch (Case)
	{
	case EWindCase::Disabled: Force.bIsEnabled = false; break;
	case EWindCase::ZeroDirection: Force.WindDirection = FVector::ZeroVector; break;
	case EWindCase::InvalidDirection: Force.WindDirection = FVector(NaN, 0.0, 0.0); break;
	case EWindCase::InfiniteRandom: Force.RandomForce = Infinity; break;
	case EWindCase::ZeroRandom: Force.RandomForce = 0.0f; break;
	case EWindCase::InvalidRipple: Force.RippleForce = NaN; break;
	case EWindCase::ZeroRipple: Force.RippleForce = 0.0f; break;
	case EWindCase::InvalidCycle: Force.StrengthCycleRange.Min = NaN; break;
	case EWindCase::UnitCycle: Force.StrengthCycleRange = FFloatInterval(1.0f, 1.0f); break;
	case EWindCase::ZeroSwayPeriod: Force.SwayPeriod = 0.0f; break;
	case EWindCase::MinimumSwayPeriod: Force.SwayPeriod = 0.01f; break;
	case EWindCase::InfiniteSwayPeriod: Force.SwayPeriod = Infinity; break;
	case EWindCase::InvalidTimeScale: Force.TimeScale = NaN; break;
	case EWindCase::ZeroTimeScale: Force.TimeScale = 0.0f; break;
	case EWindCase::InvalidForceRate:
		Force.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(0.0f, NaN);
		Force.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(1.0f, NaN);
		break;
	case EWindCase::InfiniteNoiseAngle: Force.WindDirectionNoiseAngle = Infinity; break;
	default: break;
	}
}

struct FWindResult
{
	FVector Location = FVector::ZeroVector;
	int32 ModifyBoneCount = 0;
	bool bFinite = false;
};

static FWindResult EvaluateWindCase(const EWindCase Case)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	NodeOptions.ConfigureNode = [Case](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		ConfigureWind(KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode), Case);
	};
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		NodeFixture.EvaluateFrame();
	}
	FWindResult Result;
	Result.ModifyBoneCount = NodeFixture.Node.ModifyBones.Num();
	if (!NodeFixture.OutTransforms.IsEmpty())
	{
		Result.Location = NodeFixture.OutTransforms.Last().Transform.GetLocation();
		Result.bFinite = !Result.Location.ContainsNaN();
	}
	return Result;
}

// PW-7 / PW-7 補足 / PW-7 補足 2 / TW-PW-34: 修正前は方向 0 が +X になり、非有限系列とカーブが位置へ伝播し、非有限周期と時刻は最小周期・停止時刻と一致しない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindInputNodeCasesTest,
	"KawaiiPhysics.ProceduralWind.InputRobustness.NodeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindInputNodeCasesTest::RunTest(const FString& Parameters)
{
	const FWindResult Normal = EvaluateWindCase(EWindCase::Normal);
	const FWindResult Disabled = EvaluateWindCase(EWindCase::Disabled);
	bool bOk = TestTrue(TEXT("風を受けるボーンがある"), Normal.ModifyBoneCount > 0);
	bOk &= TestTrue(TEXT("正常な風でボーンが動く"), Normal.Location != Disabled.Location);
	struct FRow { EWindCase Actual; EWindCase Expected; const TCHAR* Name; };
	const FRow Rows[] = {
		{EWindCase::ZeroDirection, EWindCase::Disabled, TEXT("方向 0")},
		{EWindCase::InvalidDirection, EWindCase::Disabled, TEXT("方向 NaN")},
		{EWindCase::InfiniteRandom, EWindCase::ZeroRandom, TEXT("Random Inf")},
		{EWindCase::InvalidRipple, EWindCase::ZeroRipple, TEXT("Ripple NaN")},
		{EWindCase::InvalidCycle, EWindCase::UnitCycle, TEXT("StrengthCycle NaN")},
		{EWindCase::ZeroSwayPeriod, EWindCase::MinimumSwayPeriod, TEXT("SwayPeriod 0")},
		{EWindCase::InfiniteSwayPeriod, EWindCase::MinimumSwayPeriod, TEXT("SwayPeriod Inf")},
		{EWindCase::InvalidTimeScale, EWindCase::ZeroTimeScale, TEXT("TimeScale NaN")},
		{EWindCase::InvalidForceRate, EWindCase::Disabled, TEXT("ForceRate NaN")},
		// 風向きノイズの系列だけを捨てる（Normal は NoiseAngle 0）。修正前は cos(Inf) で風向きが NaN になった
		{EWindCase::InfiniteNoiseAngle, EWindCase::Normal, TEXT("NoiseAngle Inf")}
	};
	for (const FRow& Row : Rows)
	{
		const FWindResult Actual = EvaluateWindCase(Row.Actual);
		const FWindResult Expected = EvaluateWindCase(Row.Expected);
		bOk &= TestTrue(Row.Name, Actual.ModifyBoneCount > 0 && Actual.bFinite &&
			Expected.bFinite && Actual.Location == Expected.Location);
	}
	return bOk;
}

// PW-7 補足 / TW-PW-38: 修正前は NaN 倍率で基底風が消え、有限な積の溢れで Total が Inf になった。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindInputSampleValuesTest,
	"KawaiiPhysics.ProceduralWind.InputRobustness.SampleValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindInputSampleValuesTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind Force;
	Force.ConstantForce = 5.0f;
	Force.StrengthCycleRange = FFloatInterval(std::numeric_limits<float>::quiet_NaN(), 1.0f);
	const auto Cycle = Force.ComputeWindSample(0.25f);
	bool bOk = TestEqual(TEXT("無効な倍率は 1"), Cycle.StrengthCycle, 1.0f);
	bOk &= TestEqual(TEXT("基底風を保持"), Cycle.Total, 5.0f);
	Force.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	Force.RandomForce = std::numeric_limits<float>::infinity();
	const auto Random = Force.ComputeWindSample(0.25f);
	bOk &= TestTrue(TEXT("Random のみ無効"), Random.Random == 0.0f && Random.Total == 5.0f && IsFiniteSample(Random));
	Force.RandomForce = 0.0f;
	Force.ConstantForce = 3.0e38f;
	Force.StrengthCycleRange = FFloatInterval(10.0f, 10.0f);
	const auto Overflow = Force.ComputeWindSample(0.25f);
	bOk &= TestTrue(TEXT("有限値の積が溢れても Total は 0"), IsFiniteSample(Overflow) && Overflow.Total == 0.0f);
	Force.ConstantForce = -5.0f;
	Force.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	bOk &= TestEqual(TEXT("負の合成値を保持"), Force.ComputeWindSample(0.25f).Total, -5.0f);

	// PW-7（Gust の系列）: Strength が NaN の突風は Gust だけ 0 で、Constant は残る（Total = 5）。
	// 修正前は NaN の Gust がそのまま Total に足されていた
	FKawaiiPhysics_ExternalForce_ProceduralWind GustForce;
	GustForce.ConstantForce = 5.0f;
	GustForce.RequestLocalGust(std::numeric_limits<float>::quiet_NaN(), 0.1f, 0.1f, 0.5f);
	GustForce.ConsumePendingRequests();
	const auto Gust = GustForce.ComputeWindSample(0.3f);
	bOk &= TestTrue(TEXT("NaN の突風は Gust だけ 0"), Gust.Gust == 0.0f && Gust.Total == 5.0f && IsFiniteSample(Gust));
	return bOk;
}

// TW-PW-34: 修正前は NaN / Inf を含む周期や入力がサンプル値へ伝播した。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindInputFiniteMatrixTest,
	"KawaiiPhysics.ProceduralWind.InputRobustness.FiniteMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindInputFiniteMatrixTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(20261126);
	bool bOk = true;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		FKawaiiPhysics_ExternalForce_ProceduralWind Force;
		Force.ConstantForce = Stream.FRandRange(-100.0f, 100.0f);
		Force.SwayForce = Stream.FRandRange(-100.0f, 100.0f);
		Force.RippleForce = Stream.FRandRange(-100.0f, 100.0f);
		Force.RandomForce = Stream.FRandRange(-100.0f, 100.0f);
		Force.SwayPeriod = Index % 5 == 0 ? 0.0f : Stream.FRandRange(0.01f, 10.0f);
		Force.RipplePeriod = Index % 7 == 0 ? -1.0f : Stream.FRandRange(0.01f, 10.0f);
		Force.RandomForcePeriod = Stream.FRandRange(0.01f, 10.0f);
		Force.StrengthCyclePeriod = Stream.FRandRange(0.01f, 10.0f);
		Force.WindDirection = Index % 11 == 0 ? FVector::ZeroVector : FVector::ForwardVector;
		bOk &= TestTrue(TEXT("全サンプル成分が有限"), IsFiniteSample(Force.ComputeWindSample(Stream.FRandRange(0.0f, 10.0f), 0.5f)));
	}
	for (const EWindCase Case : {EWindCase::ZeroDirection, EWindCase::ZeroSwayPeriod, EWindCase::InvalidRipple})
	{
		const FWindResult Result = EvaluateWindCase(Case);
		bOk &= TestTrue(TEXT("ピン相当の入力で評価位置が有限"), Result.ModifyBoneCount > 0 && Result.bFinite);
	}
	return bOk;
}

KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsWindInputRobustness, "KawaiiPhysics.Test.Regression.A");

// GU-6: 修正前は Duration 0 と負値でも共有 Entry に要求が積まれ、消費側で寿命の短い突風ができた。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindInputSharedDurationTest,
	"KawaiiPhysics.ProceduralWind.InputRobustness.SharedDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindInputSharedDurationTest::RunTest(const FString& Parameters)
{
	UWorld* TestWorld = KawaiiPhysicsNodeFixture::MakeWorld();
	AActor* TestActor = TestWorld->SpawnActor<AActor>();
	USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(TestActor);
	TestActor->AddInstanceComponent(MeshComponent);
	TestActor->SetRootComponent(MeshComponent);
	UAnimInstance* AnimInstance = NewObject<UAnimInstance>(MeshComponent);
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	Publisher.SharedGroupTag = TAG_KawaiiPhysicsWindInputRobustness;
	Publisher.SharedWind.EnsureRuntimeState();
	TestWorld->TimeSeconds = 0.0;
	Publisher.PreUpdate(AnimInstance);
	FAnimInstanceProxy PublisherProxy(AnimInstance);
	FAnimationUpdateContext UpdateContext(&PublisherProxy, 0.0f);
	Publisher.Update_AnyThread(UpdateContext);
	KawaiiPhysics::ResetInvalidGustDurationWarningForTests();
	AddExpectedMessage(TEXT("Invalid gust Duration"), EAutomationExpectedMessageFlags::Contains, 1);
	bool bOk = true;
	for (const float Duration : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		bOk &= TestFalse(TEXT("無効な共有突風を拒否"), UKawaiiPhysicsLibrary::StartProceduralWindGustOnSharedPublisher(
			TestActor, TAG_KawaiiPhysicsWindInputRobustness, 100.0f, Duration, 0.0f, 0.0f));
	}
	Publisher.PreUpdate(AnimInstance);
	Publisher.Update_AnyThread(UpdateContext);
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.PreUpdate(AnimInstance);
	Accessor.SetTimeState(0.1f, 0.1f);
	FKawaiiPhysics_ExternalForce_ProceduralWind Consumer;
	Consumer.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
	Consumer.SharedWindTag = TAG_KawaiiPhysicsWindInputRobustness;
	FAnimInstanceProxy ConsumerProxy(AnimInstance);
	FComponentSpacePoseContext PoseContext(&ConsumerProxy);
	Consumer.PreApply(Accessor.Node, PoseContext);
	bOk &= TestEqual(TEXT("共有消費側に突風の寄与なし"), Consumer.RuntimeState->CachedGust, 0.0f);
	bOk &= TestEqual(TEXT("共有側に突風スロットなし"), Publisher.SharedWind.RuntimeState->Gusts.Num, 0);
	// 対照: Publisher の解決が成り立っていること（有効な Duration は受け付ける）。これが無いと解決失敗でも素通りする
	bOk &= TestTrue(TEXT("有効な共有突風は受け付ける"), UKawaiiPhysicsLibrary::StartProceduralWindGustOnSharedPublisher(
		TestActor, TAG_KawaiiPhysicsWindInputRobustness, 100.0f, 1.0f, 0.0f, 0.0f));
	KawaiiPhysics::ResetInvalidGustDurationWarningForTests();
	TestWorld->DestroyWorld(false);
	return bOk;
}

struct FSharedClockResult
{
	float PublisherTime = 0.0f;
	float PublisherUnscaledTime = 0.0f;
	float ConsumerTime = 0.0f;
	bool bConsumerResolved = false;
};

// Publisher の TimeScale だけを変えて、World 時刻を 0.1 秒ずつ 4 フレーム進め、最後に消費側を 1 回評価する
static FSharedClockResult EvaluateSharedClock(const float PublisherTimeScale)
{
	UWorld* TestWorld = KawaiiPhysicsNodeFixture::MakeWorld();
	AActor* TestActor = TestWorld->SpawnActor<AActor>();
	USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(TestActor);
	TestActor->AddInstanceComponent(MeshComponent);
	TestActor->SetRootComponent(MeshComponent);
	UAnimInstance* AnimInstance = NewObject<UAnimInstance>(MeshComponent);
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	Publisher.SharedGroupTag = TAG_KawaiiPhysicsWindInputRobustness;
	Publisher.SharedWind.EnsureRuntimeState();
	Publisher.SharedWind.TimeScale = PublisherTimeScale;
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		TestWorld->TimeSeconds = 0.1 * Frame;
		Publisher.PreUpdate(AnimInstance);
		FAnimInstanceProxy PublisherProxy(AnimInstance);
		FAnimationUpdateContext UpdateContext(&PublisherProxy, 0.1f);
		Publisher.Update_AnyThread(UpdateContext);
	}
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.PreUpdate(AnimInstance);
	Accessor.SetTimeState(0.1f, 0.1f);
	FKawaiiPhysics_ExternalForce_ProceduralWind Consumer;
	Consumer.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
	Consumer.SharedWindTag = TAG_KawaiiPhysicsWindInputRobustness;
	FAnimInstanceProxy ConsumerProxy(AnimInstance);
	FComponentSpacePoseContext PoseContext(&ConsumerProxy);
	Consumer.PreApply(Accessor.Node, PoseContext);

	FSharedClockResult Result;
	Result.PublisherTime = Publisher.SharedWind.RuntimeState->Time;
	Result.PublisherUnscaledTime = Publisher.SharedWind.RuntimeState->UnscaledTime;
	Result.ConsumerTime = Consumer.RuntimeState->Time;
	Result.bConsumerResolved = Consumer.RuntimeState->SharedPublisherEntry.IsValid();
	TestWorld->DestroyWorld(false);
	return Result;
}

// PW-7（時刻の増分）/ Codex レビュー F1: Publisher の TimeScale が NaN / Inf なら、その増分は 0（TimeScale = 0 の対照と同じ）。
// 修正前は Publisher が Time += dt × NaN をそのまま進め、公開された NaN の時刻を消費側がそのまま写していた。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindInputSharedClockTest,
	"KawaiiPhysics.ProceduralWind.InputRobustness.SharedClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindInputSharedClockTest::RunTest(const FString& Parameters)
{
	// 対照: TimeScale = 1 なら 4 フレームで時計が進み、消費側が Publisher を解決できている（素通りしないことの確認）
	const FSharedClockResult Normal = EvaluateSharedClock(1.0f);
	const FSharedClockResult Frozen = EvaluateSharedClock(0.0f);
	bool bOk = TestTrue(TEXT("対照: 時計が進む"), Normal.PublisherTime > 0.0f);
	bOk &= TestTrue(TEXT("対照: 消費側が Publisher を解決"), Normal.bConsumerResolved);
	bOk &= TestEqual(TEXT("対照: TimeScale 0 では止まる"), Frozen.PublisherTime, 0.0f);
	for (const float TimeScale : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		const FSharedClockResult Invalid = EvaluateSharedClock(TimeScale);
		bOk &= TestTrue(TEXT("Publisher の時刻は有限で TimeScale 0 と一致"),
			FMath::IsFinite(Invalid.PublisherTime) && Invalid.PublisherTime == Frozen.PublisherTime);
		// 実秒の時計（UnscaledTime）は TimeScale に依らないので対照と同じだけ進む
		bOk &= TestEqual(TEXT("UnscaledTime は TimeScale 0 と一致"), Invalid.PublisherUnscaledTime,
			Frozen.PublisherUnscaledTime);
		bOk &= TestTrue(TEXT("消費側の時刻は有限で TimeScale 0 と一致"),
			Invalid.bConsumerResolved && FMath::IsFinite(Invalid.ConsumerTime) &&
			Invalid.ConsumerTime == Frozen.ConsumerTime);
	}
	return bOk;
}

}

#endif
