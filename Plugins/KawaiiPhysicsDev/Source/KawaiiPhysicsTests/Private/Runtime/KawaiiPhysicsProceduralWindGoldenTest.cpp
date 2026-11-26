// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/Parse.h"

namespace KawaiiPhysicsProceduralWindGoldenTestPrivate
{
// C1（PR-5a 最終検収）: 正常値での出力が変更前とビット一致であることを守る。物理式や風の式を意図的に変えたら -KPGoldenCapture で採り直し、PR 本文に理由を書く。
// 採取環境が空、または実行環境と異なる場合、通常実行では比較を成立させない。
static const TCHAR* const Golden_Environment = TEXT("5.8.4-58902052+++UE5+Release-5.8|MSVC 194435222|AMD Ryzen 9 3950X 16-Core Processor            ");
static const TArray<uint64> Golden_LocalComponentSpace = { 0xd7d04cc21ddbe4f7ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4011c1d1737e9788ULL, 0x40003c8cff7190abULL, 0xc021749c3648deb6ULL, 0x402276bbdd1107a9ULL, 0x4010f3f209d62184ULL, 0xc03138d646a3da06ULL, 0x4030228857086976ULL, 0x401d93cc780540beULL, 0xc037bb8bedc46646ULL, 0x403939148e507f78ULL, 0x4026fddef2f4020eULL, 0xc036f7d415059dc1ULL, 0x40412741609d292eULL, 0x402f5242ec2c4508ULL, 0xc03697e8542ac47cULL, };
static const TArray<uint64> Golden_LocalBoneSpace = { 0x49de6c2f408024adULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0xbfc6f8997a5a448fULL, 0xbfdcc7b728fc0bb8ULL, 0xc023f9fec183bd8bULL, 0xc0062c53f4fe5889ULL, 0xc00e61195ebb4cffULL, 0xc0330c3672c1563aULL, 0xc00f1469527f3e7bULL, 0xc0168dd1f4acb194ULL, 0xc03cd04228b94a09ULL, 0xbff04c960eeb02f4ULL, 0xc0077131c136a3eaULL, 0xc04300682805b8bcULL, 0x4008649b60235ac0ULL, 0x40037169f2fb6836ULL, 0xc046b33a83b57869ULL, };
static const TArray<uint64> Golden_SharedClock = { 0xf3da9f411c09c6ccULL, 0x000000003f3ffffdULL, 0x000000003f000003ULL, 0x0000000041ca774bULL, 0x000000003f902f04ULL, 0x00000000bf6c6d23ULL, 0x0000000000000000ULL, 0x3fea5cd428de6887ULL, 0x3fd8109ffe1cd0bcULL, 0x3fdb257d325ca7d6ULL, };
static const TArray<uint64> Golden_Sample = { 0x0000000042280000ULL, 0x00000000418bd0beULL, 0x000000003e2fe593ULL, 0x000000003fa5d22aULL, 0x0000000040b341e5ULL, 0x00000000418c0000ULL, 0x0000000042c8c023ULL, 0x0000000042280000ULL, 0x00000000418bd0beULL, 0x00000000c150b3ddULL, 0x000000003fa5d22aULL, 0x0000000040b341e5ULL, 0x00000000418c0000ULL, 0x0000000042a68262ULL, 0x0000000042280000ULL, 0x00000000418bd0beULL, 0x00000000c13b810cULL, 0x000000003fa5d22aULL, 0x0000000040b341e5ULL, 0x00000000418c0000ULL, 0x0000000042a9f12cULL, 0x0000000042280000ULL, 0x00000000409659d5ULL, 0x000000004181121eULL, 0x000000003fac4c34ULL, 0x00000000beb37226ULL, 0x0000000041e00000ULL, 0x0000000042e07400ULL, 0x0000000042280000ULL, 0x00000000409659d5ULL, 0x0000000040c26f27ULL, 0x000000003fac4c34ULL, 0x00000000beb37226ULL, 0x0000000041e00000ULL, 0x0000000042c56046ULL, 0x0000000042280000ULL, 0x00000000409659d5ULL, 0x00000000c1773ff1ULL, 0x000000003fac4c34ULL, 0x00000000beb37226ULL, 0x0000000041e00000ULL, 0x00000000428b6a90ULL, 0x0000000042280000ULL, 0x00000000c1461227ULL, 0x00000000bf23de6eULL, 0x000000003fa761caULL, 0x00000000c08b519dULL, 0x0000000041794a54ULL, 0x0000000042447ed4ULL, 0x0000000042280000ULL, 0x00000000c1461227ULL, 0x00000000414bd11fULL, 0x000000003fa761caULL, 0x00000000c08b519dULL, 0x0000000041794a54ULL, 0x0000000042853cceULL, 0x0000000042280000ULL, 0x00000000c1461227ULL, 0x000000004140dcc6ULL, 0x000000003fa761caULL, 0x00000000c08b519dULL, 0x0000000041794a54ULL, 0x000000004283726aULL, 0x0000000042280000ULL, 0x00000000415724b0ULL, 0x00000000c154f272ULL, 0x000000003f8eb742ULL, 0x00000000409fd6daULL, 0x0000000000000000ULL, 0x00000000424fe81bULL, 0x0000000042280000ULL, 0x00000000415724b0ULL, 0x00000000c184eb9eULL, 0x000000003f8eb742ULL, 0x00000000409fd6daULL, 0x0000000000000000ULL, 0x00000000424129b7ULL, 0x0000000042280000ULL, 0x00000000415724b0ULL, 0x0000000040214f7eULL, 0x000000003f8eb742ULL, 0x00000000409fd6daULL, 0x0000000000000000ULL, 0x00000000428b40a2ULL, 0x0000000042280000ULL, 0x00000000c10c5055ULL, 0x000000004187fe39ULL, 0x000000003f554978ULL, 0x00000000bfc860deULL, 0x0000000000000000ULL, 0x0000000042212230ULL, 0x0000000042280000ULL, 0x00000000c10c5055ULL, 0x00000000412e6f29ULL, 0x000000003f554978ULL, 0x00000000bfc860deULL, 0x0000000000000000ULL, 0x00000000420cd08cULL, 0x0000000042280000ULL, 0x00000000c10c5055ULL, 0x00000000c1450b1fULL, 0x000000003f554978ULL, 0x00000000bfc860deULL, 0x0000000000000000ULL, 0x00000000417dc2d6ULL, 0x0000000042280000ULL, 0x00000000c197f837ULL, 0x00000000c16231baULL, 0x000000003f27beccULL, 0x00000000c09449c3ULL, 0x0000000000000000ULL, 0x000000003f968490ULL, 0x0000000042280000ULL, 0x00000000c197f837ULL, 0x00000000c1821707ULL, 0x000000003f27beccULL, 0x00000000c09449c3ULL, 0x0000000000000000ULL, 0x00000000be5d1740ULL, 0x0000000042280000ULL, 0x00000000c197f837ULL, 0x0000000040799c9cULL, 0x000000003f27beccULL, 0x00000000c09449c3ULL, 0x0000000000000000ULL, 0x00000000414feb64ULL, 0x0000000042280000ULL, 0x00000000c15edc51ULL, 0x00000000400a8b41ULL, 0x000000003f7ad9faULL, 0x00000000c11274c3ULL, 0x0000000000000000ULL, 0x0000000041a3cb80ULL, 0x0000000042280000ULL, 0x00000000c15edc51ULL, 0x000000004166af06ULL, 0x000000003f7ad9faULL, 0x00000000c11274c3ULL, 0x0000000000000000ULL, 0x000000004201ec77ULL, 0x0000000042280000ULL, 0x00000000c15edc51ULL, 0x00000000411ea98eULL, 0x000000003f7ad9faULL, 0x00000000c11274c3ULL, 0x0000000000000000ULL, 0x0000000041e08f9aULL, 0x0000000042280000ULL, 0x00000000c1969e41ULL, 0x0000000041870438ULL, 0x000000003facc5d0ULL, 0x00000000c0014b00ULL, 0x0000000000000000ULL, 0x0000000042502765ULL, 0x0000000042280000ULL, 0x00000000c1969e41ULL, 0x000000004144559cULL, 0x000000003facc5d0ULL, 0x00000000c0014b00ULL, 0x0000000000000000ULL, 0x00000000423748d8ULL, 0x0000000042280000ULL, 0x00000000c1969e41ULL, 0x00000000c12f387aULL, 0x000000003facc5d0ULL, 0x00000000c0014b00ULL, 0x0000000000000000ULL, 0x0000000041679e40ULL, };
constexpr int32 GBoneCount = 6;
constexpr int32 GSharedComponents = 9;
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsProceduralWindGolden, "KawaiiPhysics.Test.Regression.B");

FString BuildEnvironment()
{
	FString Compiler;
#if defined(_MSC_FULL_VER)
	Compiler = FString::Printf(TEXT("MSVC %d"), _MSC_FULL_VER);
#elif defined(__clang__)
	Compiler = FString::Printf(TEXT("Clang %d.%d.%d"), __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
	Compiler = FString::Printf(TEXT("GCC %d.%d.%d"), __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
	Compiler = TEXT("unknown");
#endif
	return FString::Printf(TEXT("%s|%s|%s"), *FEngineVersion::Current().ToString(),
		*Compiler, *FPlatformMisc::GetCPUBrand());
}

uint64 GetDoubleBits(const double Value)
{
	static_assert(sizeof(double) == sizeof(uint64));
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	return Bits;
}

uint64 GetFloatBits(const float Value)
{
	static_assert(sizeof(float) == sizeof(uint32));
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	return Bits;
}

template <typename T>
uint64 GetVectorBits(const T Value)
{
	if constexpr (sizeof(T) == sizeof(float))
	{
		return GetFloatBits(Value);
	}
	else
	{
		return GetDoubleBits(Value);
	}
}

uint64 ComputeHash(const TArray<uint64>& Values)
{
	uint64 Hash = 0xcbf29ce484222325ULL;
	for (const uint64 Value : Values)
	{
		for (int32 Byte = 0; Byte < 8; ++Byte)
		{
			Hash ^= (Value >> (Byte * 8)) & 0xffULL;
			Hash *= 0x100000001b3ULL;
		}
	}
	return Hash;
}

void ConfigureWind(FKawaiiPhysics_ExternalForce_ProceduralWind& Wind)
{
	Wind.WindDirection = FVector(2.0, 1.0, 0.5);
	Wind.ConstantForce = 42.0f;
	Wind.SwayForce = 19.0f;
	Wind.SwayPeriod = 0.41f;
	Wind.SwayPhaseOffset = 23.0f;
	Wind.RippleForce = 17.0f;
	Wind.RipplePeriod = 0.57f;
	Wind.RippleTipPhaseDelay = 137.0f;
	Wind.RipplePhaseOffset = -31.0f;
	Wind.StrengthCycleRange = FFloatInterval(0.65f, 1.35f);
	Wind.StrengthCyclePeriod = 1.7f;
	Wind.StrengthCyclePhaseOffset = 47.0f;
	Wind.RandomForce = 11.0f;
	Wind.RandomForcePeriod = 0.29f;
	Wind.WindDirectionNoiseAngle = 18.0f;
	Wind.WindDirectionNoisePeriod = 0.73f;
	Wind.TimeScale = 1.25f;
	Wind.Seed = 314159;
	Wind.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(0.0f, 0.55f);
	Wind.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(0.45f, 1.2f);
	Wind.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(1.0f, 0.85f);
}

bool BuildLocalValues(FAutomationTestBase& Test, const bool bBoneSpace, TArray<uint64>& Summary)
{
	KawaiiPhysicsNodeFixture::FOptions FixtureOptions;
	FixtureOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(GBoneCount);
	FixtureOptions.FrameDt = bBoneSpace ? 1.0f / 30.0f : 1.0f / 60.0f;
	FixtureOptions.bUseLocalPose = true;
	FixtureOptions.bPreUpdateEveryFrame = true;
	FixtureOptions.ConfigureNode = [bBoneSpace](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.bNeedWarmUp = false;
		AnimNode.Gravity = FVector(0.0, 0.0, -980.0);
		auto& Wind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
		ConfigureWind(Wind);
		Wind.ExternalForceSpace = bBoneSpace ? EExternalForceSpace::BoneSpace : EExternalForceSpace::ComponentSpace;
	};
	FixtureOptions.SetInputPose = [bBoneSpace](FCompactPose& Pose, int32 Frame)
	{
		if (bBoneSpace)
		{
			Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::UpVector, 0.22 + Frame * 0.013));
		}
	};
	KawaiiPhysicsNodeFixture::FFixture Fixture(FixtureOptions);
	TArray<uint64> Trajectory;
	TArray<uint64> Final;
	const int32 FrameCount = bBoneSpace ? 30 : 40;
	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		if (Frame == 12)
		{
			Fixture.Node.ExternalForces[0].GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>()
				.RequestLocalGust(28.0f, 0.08f, 0.31f, 0.12f);
		}
		Fixture.EvaluateFrame(Frame);
		if (Fixture.OutTransforms.Num() != GBoneCount || Fixture.Node.ModifyBones.Num() == 0)
		{
			Test.AddError(TEXT("風を受けるボーンの出力数が不正"));
			return false;
		}
		FVector Locations[GBoneCount];
		bool Seen[GBoneCount] = {};
		for (const FBoneTransform& Bone : Fixture.OutTransforms)
		{
			const int32 Index = Bone.BoneIndex.GetInt();
			if (Index < 0 || Index >= GBoneCount || Seen[Index] || Bone.Transform.ContainsNaN())
			{
				Test.AddError(TEXT("ボーンの出力が不正"));
				return false;
			}
			Seen[Index] = true;
			Locations[Index] = Bone.Transform.GetLocation();
		}
		Final.Reset();
		for (int32 Index = 0; Index < GBoneCount; ++Index)
		{
			if (!Seen[Index]) { Test.AddError(TEXT("ボーンの出力が欠落")); return false; }
			Final.Add(GetVectorBits(Locations[Index].X));
			Final.Add(GetVectorBits(Locations[Index].Y));
			Final.Add(GetVectorBits(Locations[Index].Z));
			Trajectory.Append(Final);
		}
	}
	Summary.Add(ComputeHash(Trajectory));
	Summary.Append(Final);
	return true;
}

bool BuildSharedValues(FAutomationTestBase& Test, TArray<uint64>& Summary)
{
	UWorld* TestWorld = KawaiiPhysicsNodeFixture::MakeWorld();
	AActor* TestActor = TestWorld->SpawnActor<AActor>();
	USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(TestActor);
	TestActor->AddInstanceComponent(MeshComponent);
	TestActor->SetRootComponent(MeshComponent);
	UAnimInstance* AnimInstance = NewObject<UAnimInstance>(MeshComponent);
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	Publisher.SharedGroupTag = TAG_KawaiiPhysicsProceduralWindGolden;
	ConfigureWind(Publisher.SharedWind);
	Publisher.SharedWind.TimeScale = 1.5f;
	Publisher.SharedWind.EnsureRuntimeState();
	Publisher.SharedWind.RequestLocalGust(28.0f, 0.08f, 0.31f, 0.12f);
	FKawaiiPhysicsTestAccessor Accessor;
	FKawaiiPhysics_ExternalForce_ProceduralWind Consumer;
	Consumer.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
	Consumer.SharedWindTag = TAG_KawaiiPhysicsProceduralWindGolden;
	Consumer.Seed = 314159;
	TArray<uint64> Trajectory;
	TArray<uint64> Final;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		TestWorld->TimeSeconds = static_cast<double>(Frame) / 60.0;
		Publisher.PreUpdate(AnimInstance);
		FAnimInstanceProxy PublisherProxy(AnimInstance);
		FAnimationUpdateContext UpdateContext(&PublisherProxy, 1.0f / 60.0f);
		Publisher.Update_AnyThread(UpdateContext);
		Accessor.Node.PreUpdate(AnimInstance);
		Accessor.SetTimeState(1.0f / 60.0f, 1.0f / 60.0f);
		FAnimInstanceProxy ConsumerProxy(AnimInstance);
		FComponentSpacePoseContext PoseContext(&ConsumerProxy);
		Consumer.PreApply(Accessor.Node, PoseContext);
		if (!Consumer.RuntimeState.IsValid() || !Consumer.RuntimeState->SharedPublisherEntry.IsValid())
		{
			Test.AddError(TEXT("共有風の Publisher を解決できない"));
			TestWorld->DestroyWorld(false);
			return false;
		}
		const auto& State = *Consumer.RuntimeState;
		Final.Reset();
		Final.Add(GetFloatBits(State.Time));
		Final.Add(GetFloatBits(State.UnscaledTime));
		Final.Add(GetFloatBits(State.CachedSinesWithoutRipple));
		Final.Add(GetFloatBits(State.CachedStrengthCycle));
		Final.Add(GetFloatBits(State.CachedRandom));
		Final.Add(GetFloatBits(State.CachedGust));
		Final.Add(GetVectorBits(State.CachedWindVector.X));
		Final.Add(GetVectorBits(State.CachedWindVector.Y));
		Final.Add(GetVectorBits(State.CachedWindVector.Z));
		Trajectory.Append(Final);
	}
	TestWorld->DestroyWorld(false);
	Summary.Add(ComputeHash(Trajectory));
	Summary.Append(Final);
	return true;
}

void BuildSampleValues(TArray<uint64>& Values)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	ConfigureWind(Wind);
	Wind.EnsureRuntimeState();
	Wind.RequestLocalGust(28.0f, 0.08f, 0.31f, 0.12f);
	Wind.ConsumePendingRequests();
	const float Times[] = {0.04f, 0.13f, 0.27f, 0.43f, 0.61f, 0.88f, 1.17f, 1.53f};
	const float Rates[] = {0.0f, 0.37f, 1.0f};
	for (const float Time : Times)
	{
		Wind.AdvanceWindTime(Time - Wind.RuntimeState->UnscaledTime);
		for (const float Rate : Rates)
		{
			const auto Sample = Wind.ComputeWindSample(Wind.RuntimeState->Time, Rate);
			Values.Add(GetFloatBits(Sample.Constant));
			Values.Add(GetFloatBits(Sample.Sway));
			Values.Add(GetFloatBits(Sample.Ripple));
			Values.Add(GetFloatBits(Sample.StrengthCycle));
			Values.Add(GetFloatBits(Sample.Random));
			Values.Add(GetFloatBits(Sample.Gust));
			Values.Add(GetFloatBits(Sample.Total));
		}
	}
}

void LogCapture(FAutomationTestBase& Test, const TCHAR* Name, const TArray<uint64>& Values)
{
	FString Source = TEXT("{ ");
	for (const uint64 Value : Values)
	{
		Source += FString::Printf(TEXT("0x%016llxULL, "), static_cast<unsigned long long>(Value));
	}
	Source += TEXT("}");
	Test.AddInfo(FString::Printf(TEXT("GOLDEN_WIND %s %s"), Name, *Source));
}

bool CheckValues(FAutomationTestBase& Test, const TCHAR* Name, const TArray<uint64>& Actual,
	const TArray<uint64>& Expected, const int32 Count, const bool bCapture)
{
	if (Actual.Num() != Count)
	{
		Test.AddError(FString::Printf(TEXT("%s: 採取値数 %d、必要数 %d"), Name, Actual.Num(), Count));
		return false;
	}
	if (bCapture) { LogCapture(Test, Name, Actual); return true; }
	if (Expected.Num() != Count)
	{
		Test.AddError(FString::Printf(TEXT("%s: 期待値数 %d、必要数 %d。-KPGoldenCapture で採取する"), Name, Expected.Num(), Count));
		return false;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Actual[Index] != Expected[Index])
		{
			Test.AddError(FString::Printf(TEXT("%s[%d]: 期待 0x%016llx、実際 0x%016llx"), Name, Index,
				static_cast<unsigned long long>(Expected[Index]), static_cast<unsigned long long>(Actual[Index])));
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProceduralWindGoldenPositionsTest,
	"KawaiiPhysics.ProceduralWind.GoldenPositions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProceduralWindGoldenPositionsTest::RunTest(const FString& Parameters)
{
	const bool bCapture = FParse::Param(FCommandLine::Get(), TEXT("KPGoldenCapture"));
	const FString CurrentEnvironment = BuildEnvironment();
	if (bCapture)
	{
		AddInfo(FString::Printf(TEXT("GOLDEN_ENV ProceduralWindGoldenPositions \"%s\""), *CurrentEnvironment));
		AddWarning(TEXT("採取モードのため比較していない"));
	}
	else if (Golden_Environment[0] == 0 || CurrentEnvironment != Golden_Environment)
	{
		AddError(TEXT("採取環境が空か実行環境と異なるため比較できない。-KPGoldenCapture で採取する"));
		return false;
	}
	TArray<uint64> ComponentValues, BoneValues, SharedValues, SampleValues;
	bool bOk = BuildLocalValues(*this, false, ComponentValues);
	bOk &= BuildLocalValues(*this, true, BoneValues);
	bOk &= BuildSharedValues(*this, SharedValues);
	BuildSampleValues(SampleValues);
	bOk &= CheckValues(*this, TEXT("LocalComponentSpace"), ComponentValues, Golden_LocalComponentSpace, 19, bCapture);
	bOk &= CheckValues(*this, TEXT("LocalBoneSpace"), BoneValues, Golden_LocalBoneSpace, 19, bCapture);
	bOk &= CheckValues(*this, TEXT("SharedClock"), SharedValues, Golden_SharedClock, 1 + GSharedComponents, bCapture);
	bOk &= CheckValues(*this, TEXT("Sample"), SampleValues, Golden_Sample, 8 * 3 * 7, bCapture);
	if (ComponentValues.Num() && BoneValues.Num() && ComponentValues[0] == BoneValues[0])
	{
		AddError(TEXT("LocalComponentSpace と LocalBoneSpace の軌跡が同じ"));
		bOk = false;
	}
	return bOk;
}

} // namespace KawaiiPhysicsProceduralWindGoldenTestPrivate

#endif
