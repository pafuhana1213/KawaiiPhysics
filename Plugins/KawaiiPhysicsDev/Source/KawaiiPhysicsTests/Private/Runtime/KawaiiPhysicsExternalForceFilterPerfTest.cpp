// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsHairNodeFixture.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Misc/MemStack.h"

// 外力のボーンフィルタの性能計測。master にある公開 API だけを使うので、変更前後で同じ計測ができる。
namespace KawaiiPhysicsExternalForceFilterPerfTestPrivate
{
	using namespace KawaiiPhysicsHairNodeFixture;

	constexpr int32 GNodeWarmupFrames = 120;
	constexpr int32 GNodeMeasureFrames = 600;
	constexpr int32 GTrials = 7;

	double Median(TArray<double> Values)
	{
		Values.Sort();
		return Values[Values.Num() / 2];
	}

// Issue #297: 600 ボーン・150 チェーン・120Hz 固定 substep の本番ノード評価で、
// フィルタ付き ProceduralWind を 4 / 8 本に分けたときと、1 ボーンだけの小さいフィルタのときのコストを測る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfExternalForceFilterNodeTest,
	"KawaiiPhysics.Perf.ExternalForceFilterNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfExternalForceFilterNodeTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsDeveloperSettings* Settings = GetMutableDefault<UKawaiiPhysicsDeveloperSettings>();
	const bool bOldFixed = Settings->bUseFixedSubstepping;
	const int32 OldMaxSubsteps = Settings->MaxSubsteps;
	Settings->bUseFixedSubstepping = true;
	Settings->MaxSubsteps = 4;

	TArray<FCase> Cases;
	for (const int32 Fps : {60, 30})
	{
		Cases.Add({FString::Printf(TEXT("Fps%d.NoWind"), Fps), Fps, 0, EFilterLayout::None});
		Cases.Add({FString::Printf(TEXT("Fps%d.Unfiltered1"), Fps), Fps, 1, EFilterLayout::None});
		Cases.Add({FString::Printf(TEXT("Fps%d.SingleBoneFilter1"), Fps), Fps, 1, EFilterLayout::SingleBone});
		Cases.Add({FString::Printf(TEXT("Fps%d.Filtered4"), Fps), Fps, 4, EFilterLayout::RoundRobinChains});
		Cases.Add({FString::Printf(TEXT("Fps%d.Filtered8"), Fps), Fps, 8, EFilterLayout::RoundRobinChains});
	}

	bool bOk = true;
	FMemMark MemMark(FMemStack::Get());
	TArray<TUniquePtr<FFixture>> Fixtures;
	TArray<TUniquePtr<FComponentSpacePoseContext>> Contexts;
	TArray<TArray<double>> Samples;
	for (const FCase& Case : Cases)
	{
		Fixtures.Add(MakeUnique<FFixture>(Case));
		Contexts.Add(MakeUnique<FComponentSpacePoseContext>(Fixtures.Last()->Proxy.Get()));
		Samples.AddDefaulted();
		for (int32 Frame = 0; Frame < GNodeWarmupFrames; ++Frame)
		{
			Fixtures.Last()->Evaluate(*Contexts.Last());
		}
	}

	// ケースを交互に回し、機械状態の揺れが特定のケースへ偏らないようにする
	for (int32 Trial = 0; Trial < GTrials; ++Trial)
	{
		for (int32 CaseIndex = 0; CaseIndex < Cases.Num(); ++CaseIndex)
		{
			FFixture& Fixture = *Fixtures[CaseIndex];
			const double Start = FPlatformTime::Seconds();
			for (int32 Frame = 0; Frame < GNodeMeasureFrames; ++Frame)
			{
				Fixture.Evaluate(*Contexts[CaseIndex]);
			}
			const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0 / GNodeMeasureFrames;
			Samples[CaseIndex].Add(Ms);
			AddInfo(FString::Printf(TEXT("PERF_RAW KawaiiPhysics.Perf.ExternalForceFilterNode.%s trial=%d ms=%.6f"),
				*Cases[CaseIndex].Name, Trial, Ms));
		}
	}

	for (int32 CaseIndex = 0; CaseIndex < Cases.Num(); ++CaseIndex)
	{
		bOk &= TestTrue(FString::Printf(TEXT("%s outputs all bones with finite values"), *Cases[CaseIndex].Name),
			Fixtures[CaseIndex]->OutputsAllBonesFinite());
		TArray<double> Sorted = Samples[CaseIndex];
		Sorted.Sort();
		AddInfo(FString::Printf(TEXT("PERF KawaiiPhysics.Perf.ExternalForceFilterNode.%s median_ms_per_frame=%.6f min_ms_per_frame=%.6f"),
			*Cases[CaseIndex].Name, Median(Samples[CaseIndex]), Sorted[0]));
	}

	Contexts.Reset();
	Fixtures.Reset();
	Settings->bUseFixedSubstepping = bOldFixed;
	Settings->MaxSubsteps = OldMaxSubsteps;
	return bOk;
}

	// ===== 外力の処理だけを切り出した、小さいフィルタの計測 =====

	constexpr int32 GSmallFilterBones = 600;
	constexpr int32 GSmallFilterSubsteps = 2;
	constexpr int32 GSmallFilterWarmupFrames = 100;
	constexpr int32 GSmallFilterMeasureFrames = 2000;

	struct FSmallFilterBench
	{
		FKawaiiPhysicsTestAccessor A;
		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext PoseContext{&Proxy};
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = nullptr;

		explicit FSmallFilterBench(const int32 FilterSize)
		{
			A.BuildVerticalChain(GSmallFilterBones, 5.0f);
			A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
			for (int32 Index = 0; Index < A.Num(); ++Index)
			{
				A.Bone(Index).BoneRef.BoneName = FName(TEXT("SmallFilterBone"), Index + 1);
				A.Bone(Index).LengthRateFromRoot = static_cast<float>(Index) / (A.Num() - 1);
			}
			A.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
			Wind = A.GetMutableProceduralWind(0);
			Wind->WindSource = EKawaiiPhysicsProceduralWindSource::Local;
			Wind->ExternalForceSpace = EExternalForceSpace::ComponentSpace;
			Wind->WindDirection = FVector(0.0f, 1.0f, 0.0f);
			Wind->ConstantForce = 10.0f;
			for (int32 Index = 1; Index <= FilterSize; ++Index)
			{
				Wind->ApplyBoneFilter.AddDefaulted_GetRef().BoneName = A.Bone(Index).BoneRef.BoneName;
			}
			A.SetSubstepTimeState(1.0f / 60.0f, 1.0f / 120.0f);
		}

		// 本番ノードと同じく、フレームに 1 回 PreApply、各 substep で全ボーンへ Apply する
		void Frame()
		{
			Wind->PreApply(A.Node, PoseContext);
			for (int32 Substep = 0; Substep < GSmallFilterSubsteps; ++Substep)
			{
				for (int32 Index = 1; Index < A.Num(); ++Index)
				{
					Wind->Apply(A.Bone(Index), A.Node, PoseContext);
				}
			}
		}
	};

// 1〜4 ボーンの小さいフィルタで、判定表の照合（毎フレームのボーン数ぶん）が元の探索より重くならないかを測る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfExternalForceSmallFilterTest,
	"KawaiiPhysics.Perf.ExternalForceSmallFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfExternalForceSmallFilterTest::RunTest(const FString& Parameters)
{
	const TCHAR* Names[] = {TEXT("Unfiltered"), TEXT("Filter1"), TEXT("Filter4")};
	const int32 Sizes[] = {0, 1, 4};
	TArray<TUniquePtr<FSmallFilterBench>> Benches;
	TArray<TArray<double>> Samples;
	for (const int32 Size : Sizes)
	{
		Benches.Add(MakeUnique<FSmallFilterBench>(Size));
		Samples.AddDefaulted();
		for (int32 Frame = 0; Frame < GSmallFilterWarmupFrames; ++Frame)
		{
			Benches.Last()->Frame();
		}
	}

	for (int32 Trial = 0; Trial < GTrials; ++Trial)
	{
		for (int32 CaseIndex = 0; CaseIndex < Benches.Num(); ++CaseIndex)
		{
			const double Start = FPlatformTime::Seconds();
			for (int32 Frame = 0; Frame < GSmallFilterMeasureFrames; ++Frame)
			{
				Benches[CaseIndex]->Frame();
			}
			const double Us = (FPlatformTime::Seconds() - Start) * 1000000.0 / GSmallFilterMeasureFrames;
			Samples[CaseIndex].Add(Us);
		}
	}

	bool bOk = true;
	for (int32 CaseIndex = 0; CaseIndex < Benches.Num(); ++CaseIndex)
	{
		bOk &= TestTrue(FString::Printf(TEXT("%s stays finite"), Names[CaseIndex]), Benches[CaseIndex]->A.AllFinite());
		AddInfo(FString::Printf(TEXT("PERF KawaiiPhysics.Perf.ExternalForceSmallFilter.%s median_us_per_frame=%.3f"),
			Names[CaseIndex], Median(Samples[CaseIndex])));
	}
	return bOk;
}

} // namespace KawaiiPhysicsExternalForceFilterPerfTestPrivate

#endif
