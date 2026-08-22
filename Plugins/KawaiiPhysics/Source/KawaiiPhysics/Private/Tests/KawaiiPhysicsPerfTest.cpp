// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "Templates/Function.h"
#include "Curves/CurveFloat.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsTestHarness.h"

namespace
{
	constexpr int32 GWarmupFrames = 100;
	constexpr int32 GMeasureFrames = 2000;
	constexpr int32 GTrials = 5;
	constexpr float GFrameDt = 1.0f / 90.0f;
	constexpr double GAverageSubsteps = 60.0 / 90.0; // 1/90秒を1/60秒固定ステップへ蓄積する理論平均。

	FKawaiiPhysicsSettings MakePerfSettings(const float Radius = 2.0f)
	{
		FKawaiiPhysicsSettings Settings;
		Settings.Damping = 0.15f;
		Settings.WorldDampingLocation = 0.2f;
		Settings.WorldDampingRotation = 0.3f;
		Settings.Stiffness = 0.07f;
		Settings.Radius = Radius;
		Settings.LimitAngle = 0.0f;
		return Settings;
	}

	void ConfigureBaseSimulation(FKawaiiPhysicsTestAccessor& A, const float Radius = 2.0f)
	{
		A.SetAllPhysicsSettings(MakePerfSettings(Radius));
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
		A.SetFixedSubstepping(true, 60, 4);
		// 常に横移動させ、チェーンが静止解に貼り付いたまま計測されるのを避ける。
		A.SetSkelCompMove(FVector(0.3f, 0.0f, 0.0f), FQuat::Identity);
	}

	void AddPerfCollisionLimits(FKawaiiPhysicsTestAccessor& A)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FSphericalLimit Sphere;
			Sphere.bEnable = true;
			Sphere.Location = FVector(2.0, 0.0, -100.0 - 180.0 * Index);
			Sphere.Rotation = FQuat::Identity;
			Sphere.Radius = 10.0f;
			Sphere.LimitType = ESphericalLimitType::Outer;
			A.Node.SphericalLimits.Add(Sphere);
		}

		for (int32 Index = 0; Index < 8; ++Index)
		{
			FCapsuleLimit Capsule;
			Capsule.bEnable = true;
			Capsule.Location = FVector(0.0, 2.0, -60.0 - 105.0 * Index);
			Capsule.Rotation = FQuat::Identity;
			Capsule.Radius = 5.0f;
			Capsule.Length = 80.0f;
			A.Node.CapsuleLimits.Add(Capsule);
		}

		for (int32 Index = 0; Index < 4; ++Index)
		{
			FBoxLimit Box;
			Box.bEnable = true;
			Box.Location = FVector(0.0, -2.0, -150.0 - 190.0 * Index);
			Box.Rotation = FQuat::Identity;
			Box.Extent = FVector(8.0, 8.0, 20.0);
			A.Node.BoxLimits.Add(Box);
		}

		for (int32 Index = 0; Index < 2; ++Index)
		{
			FPlanarLimit Planar;
			Planar.bEnable = true;
			Planar.Location = FVector(0.0, 0.0, -350.0 - 350.0 * Index);
			Planar.Rotation = FQuat::Identity;
			Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());
			A.Node.PlanarLimits.Add(Planar);
		}
	}

	bool RunSimulationPerf(FAutomationTestBase& Test, const TCHAR* TestName,
	                       const TFunction<void(FKawaiiPhysicsTestAccessor&)>& Setup)
	{
		TArray<double> MsPerFrameValues;
		MsPerFrameValues.Reserve(GTrials);
		double Checksum = 0.0;
		int32 BoneCount = 0;
		bool bFinite = true;

		for (int32 Trial = 0; Trial < GTrials; ++Trial)
		{
			FKawaiiPhysicsTestAccessor A;
			Setup(A);
			BoneCount = A.Num();

			for (int32 Frame = 0; Frame < GWarmupFrames; ++Frame)
			{
				A.StepFrame(GFrameDt);
			}

			const double StartSeconds = FPlatformTime::Seconds();
			double TrialChecksum = 0.0;
			for (int32 Frame = 0; Frame < GMeasureFrames; ++Frame)
			{
				A.StepFrame(GFrameDt);
				const FVector Tip = A.TipLocation();
				TrialChecksum += Tip.X + Tip.Y + Tip.Z;
			}
			const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			const double MsPerFrame = ElapsedSeconds * 1000.0 / static_cast<double>(GMeasureFrames);

			Test.AddInfo(FString::Printf(TEXT("PERF_RAW %s trial=%d ms=%.6f"), TestName, Trial, MsPerFrame));
			MsPerFrameValues.Add(MsPerFrame);
			Checksum += TrialChecksum;

			if (!A.AllFinite())
			{
				Test.AddError(FString::Printf(TEXT("PERF %s produced NaN or Inf"), TestName));
				bFinite = false;
			}
		}

		MsPerFrameValues.Sort();
		const double MedianMsPerFrame = MsPerFrameValues[GTrials / 2];
		const double NsPerBoneStep = MedianMsPerFrame * 1000000.0 /
			FMath::Max(1.0, static_cast<double>(BoneCount) * GAverageSubsteps);
		Test.AddInfo(FString::Printf(
			TEXT("PERF %s median_ms_per_frame=%.6f ns_per_bone_step=%.3f checksum=%.6f"),
			TestName, MedianMsPerFrame, NsPerBoneStep, Checksum));
		return bFinite;
	}

	void FillLengthRate(FKawaiiPhysicsTestAccessor& A)
	{
		const int32 LastIndex = FMath::Max(1, A.Num() - 1);
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			A.Bone(Index).LengthRateFromRoot = static_cast<float>(Index) / static_cast<float>(LastIndex);
		}
	}

	bool PhysicsSettingsFinite(const FKawaiiPhysicsTestAccessor& A)
	{
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			const FKawaiiPhysicsSettings& Settings = A.Bone(Index).PhysicsSettings;
			if (!FMath::IsFinite(Settings.Damping) ||
				!FMath::IsFinite(Settings.WorldDampingLocation) ||
				!FMath::IsFinite(Settings.WorldDampingRotation) ||
				!FMath::IsFinite(Settings.Stiffness) ||
				!FMath::IsFinite(Settings.Radius) ||
				!FMath::IsFinite(Settings.LimitAngle))
			{
				return false;
			}
		}
		return true;
	}

	bool RunPhysicsSettingsPerf(FAutomationTestBase& Test, const TCHAR* TestName, const bool bSetDampingCurve)
	{
		constexpr int32 Calls = 20000;
		TArray<double> MsPerCallValues;
		MsPerCallValues.Reserve(GTrials);
		double Checksum = 0.0;
		bool bFinite = true;

		for (int32 Trial = 0; Trial < GTrials; ++Trial)
		{
			FKawaiiPhysicsTestAccessor A;
			A.BuildVerticalChain(200, 5.0f);
			FillLengthRate(A);
			A.Node.PhysicsSettings = MakePerfSettings(2.0f);
			if (bSetDampingCurve)
			{
				FRichCurve* Curve = A.Node.DampingCurveData.GetRichCurve();
				Curve->Reset();
				Curve->AddKey(0.0f, 0.5f);
				Curve->AddKey(1.0f, 1.5f);
			}

			const double StartSeconds = FPlatformTime::Seconds();
			double TrialChecksum = 0.0;
			for (int32 Call = 0; Call < Calls; ++Call)
			{
				A.CallUpdatePhysicsSettings();
				const FKawaiiPhysicsSettings& TipSettings = A.Bone(A.Num() - 1).PhysicsSettings;
				TrialChecksum += TipSettings.Damping + TipSettings.WorldDampingLocation +
					TipSettings.WorldDampingRotation + TipSettings.Stiffness + TipSettings.Radius +
					TipSettings.LimitAngle;
			}
			const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			const double MsPerCall = ElapsedSeconds * 1000.0 / static_cast<double>(Calls);

			Test.AddInfo(FString::Printf(TEXT("PERF_RAW %s trial=%d ms=%.6f"), TestName, Trial, MsPerCall));
			MsPerCallValues.Add(MsPerCall);
			Checksum += TrialChecksum;

			if (!PhysicsSettingsFinite(A))
			{
				Test.AddError(FString::Printf(TEXT("PERF %s produced NaN or Inf"), TestName));
				bFinite = false;
			}
		}

		MsPerCallValues.Sort();
		const double MedianMsPerCall = MsPerCallValues[GTrials / 2];
		const double NsPerBone = MedianMsPerCall * 1000000.0 / 200.0;
		Test.AddInfo(FString::Printf(
			TEXT("PERF %s median_ms_per_frame=%.6f ns_per_bone_step=%.3f checksum=%.6f"),
			TestName, MedianMsPerCall, NsPerBone, Checksum));
		return bFinite;
	}

	// ---------------------------------------------------------------
	// Shared Collision Copy Perf
	// ---------------------------------------------------------------
	// Shared コリジョン経路（Publish→ReadMerged→格納）が丸ごとコピーする limit 構造体の量を計測する。
	// 非UPROPERTYキャッシュメンバの追加でサイズが増えた分（Capsule/TaperedCapsule/Box/Planar）が
	// フレーム毎コピーコストとして無視できる規模かどうかを実測するのが目的。
	// ソースは2つ、各ソースはSphere/Capsule/TaperedCapsule/Box各8個・Planar4個を持つ。

	constexpr int32 GSharedCollisionSphereCount = 8;
	constexpr int32 GSharedCollisionCapsuleCount = 8;
	constexpr int32 GSharedCollisionTaperedCapsuleCount = 8;
	constexpr int32 GSharedCollisionBoxCount = 8;
	constexpr int32 GSharedCollisionPlanarCount = 4;
	constexpr int32 GSharedCollisionSourceCount = 2;
	constexpr int32 GSharedCollisionLimitsPerSource =
		GSharedCollisionSphereCount + GSharedCollisionCapsuleCount + GSharedCollisionTaperedCapsuleCount +
		GSharedCollisionBoxCount + GSharedCollisionPlanarCount;
	constexpr int32 GSharedCollisionLimitsPerFrame = GSharedCollisionLimitsPerSource * GSharedCollisionSourceCount;

	// WriteSharedCollisionToSubsystemが送り出す変換済みデータ相当のテンプレートを1ソース分作る
	// （空間変換[ConvertSimulationSpaceTransform]は本ベンチの対象外。構造体コピーそのものの帯域を測るのが目的）。
	FKawaiiPhysicsSharedCollisionData MakeSharedCollisionSourceTemplate(float Base)
	{
		FKawaiiPhysicsSharedCollisionData Data;

		for (int32 Index = 0; Index < GSharedCollisionSphereCount; ++Index)
		{
			FSphericalLimit Sphere;
			Sphere.bEnable = true;
			Sphere.Location = FVector(Base + Index, Base + Index * 2.0f, Base + Index * 3.0f);
			Sphere.Rotation = FQuat(FVector(0.0, 0.0, 1.0), 0.1f * Index);
			Sphere.Radius = 10.0f + Index;
			Sphere.LimitType = ESphericalLimitType::Outer;
			Data.SphericalLimits.Add(Sphere);
		}

		for (int32 Index = 0; Index < GSharedCollisionCapsuleCount; ++Index)
		{
			FCapsuleLimit Capsule;
			Capsule.bEnable = true;
			Capsule.Location = FVector(Base + Index, Base - Index, Base + Index * 2.0f);
			Capsule.Rotation = FQuat(FVector(1.0, 0.0, 0.0), 0.1f * Index);
			Capsule.Radius = 5.0f;
			Capsule.Length = 40.0f + Index;
			Data.CapsuleLimits.Add(Capsule);
		}

		for (int32 Index = 0; Index < GSharedCollisionTaperedCapsuleCount; ++Index)
		{
			FTaperedCapsuleLimit TaperedCapsule;
			TaperedCapsule.bEnable = true;
			TaperedCapsule.Location = FVector(Base - Index, Base + Index, Base + Index * 4.0f);
			TaperedCapsule.Rotation = FQuat(FVector(0.0, 1.0, 0.0), 0.1f * Index);
			TaperedCapsule.Radius0 = 6.0f + Index;
			TaperedCapsule.Radius1 = 4.0f + Index;
			TaperedCapsule.Length = 50.0f + Index;
			Data.TaperedCapsuleLimits.Add(TaperedCapsule);
		}

		for (int32 Index = 0; Index < GSharedCollisionBoxCount; ++Index)
		{
			FBoxLimit Box;
			Box.bEnable = true;
			Box.Location = FVector(Base + Index * 2.0f, Base, Base - Index);
			Box.Rotation = FQuat(FVector(0.0, 0.0, 1.0), 0.05f * Index);
			Box.Extent = FVector(8.0f, 8.0f, 20.0f + Index);
			Data.BoxLimits.Add(Box);
		}

		for (int32 Index = 0; Index < GSharedCollisionPlanarCount; ++Index)
		{
			FPlanarLimit Planar;
			Planar.bEnable = true;
			Planar.Location = FVector(Base, Base + Index * 10.0f, Base - 100.0f - Index * 50.0f);
			Planar.Rotation = FQuat(FVector(1.0, 0.0, 0.0), 0.05f * Index);
			Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());
			Data.PlanarLimits.Add(Planar);
		}

		return Data;
	}

	// ConvertAndAppend/ConvertAndStoreが行う「要素毎に一旦ローカル変数へコピーしてAddする」を模した汎用コピー。
	template <typename TLimitArray>
	void CopyLimitsElementwise(const TLimitArray& InLimits, TLimitArray& OutLimits)
	{
		OutLimits.Reserve(OutLimits.Num() + InLimits.Num());
		for (const auto& Limit : InLimits)
		{
			auto Converted = Limit;
			OutLimits.Add(Converted);
		}
	}

	// WriteSharedCollisionToSubsystemのConvertAndAppendを模す: テンプレートからスクラッチへ要素毎コピーしてPublishする。
	void PublishSharedCollisionSource(const FKawaiiPhysicsSharedCollisionData& Template,
	                                  FKawaiiPhysicsSharedCollisionData& Scratch,
	                                  FKawaiiPhysicsSharedCollisionSourceSlot& Slot)
	{
		Scratch.Reset();
		CopyLimitsElementwise(Template.SphericalLimits, Scratch.SphericalLimits);
		CopyLimitsElementwise(Template.CapsuleLimits, Scratch.CapsuleLimits);
		CopyLimitsElementwise(Template.TaperedCapsuleLimits, Scratch.TaperedCapsuleLimits);
		CopyLimitsElementwise(Template.BoxLimits, Scratch.BoxLimits);
		CopyLimitsElementwise(Template.PlanarLimits, Scratch.PlanarLimits);
		Slot.Publish(Scratch);
	}

	// UpdateSharedCollisionLimitsのConvertAndStoreを模す: ReadMergedで受け取った配列をShared側へ要素毎コピーする。
	// 本番はSharedSphericalLimits等5本の個別TArrayだが、コピー対象の構造体・要素数は同一なので
	// FKawaiiPhysicsSharedCollisionDataを使い回して集約する（計測の本質＝要素毎コピー帯域には影響しない）。
	int32 StoreSharedCollisionLimits(const FKawaiiPhysicsSharedCollisionData& Merged,
	                                 FKawaiiPhysicsSharedCollisionData& SharedOut)
	{
		SharedOut.Reset();
		CopyLimitsElementwise(Merged.SphericalLimits, SharedOut.SphericalLimits);
		CopyLimitsElementwise(Merged.CapsuleLimits, SharedOut.CapsuleLimits);
		CopyLimitsElementwise(Merged.TaperedCapsuleLimits, SharedOut.TaperedCapsuleLimits);
		CopyLimitsElementwise(Merged.BoxLimits, SharedOut.BoxLimits);
		CopyLimitsElementwise(Merged.PlanarLimits, SharedOut.PlanarLimits);
		return SharedOut.SphericalLimits.Num() + SharedOut.CapsuleLimits.Num() +
			SharedOut.TaperedCapsuleLimits.Num() + SharedOut.BoxLimits.Num() + SharedOut.PlanarLimits.Num();
	}

	bool RunSharedCollisionCopyPerf(FAutomationTestBase& Test)
	{
		FKawaiiPhysicsSharedCollisionData SourceTemplates[GSharedCollisionSourceCount];
		for (int32 SourceIndex = 0; SourceIndex < GSharedCollisionSourceCount; ++SourceIndex)
		{
			SourceTemplates[SourceIndex] = MakeSharedCollisionSourceTemplate(10.0f + SourceIndex * 100.0f);
		}

		TArray<double> MsPerFrameValues;
		MsPerFrameValues.Reserve(GTrials);
		int32 LastMergedLimitCount = 0;

		for (int32 Trial = 0; Trial < GTrials; ++Trial)
		{
			// 本番のCachedSharedCollisionEntry/CachedSourceSlotに相当するキャッシュを試行毎に作り直す
			// （Entry/Slotはワールド非依存のプレーン構造体のため、Subsystem/Worldを経由せず直接構築できる）。
			FKawaiiPhysicsSharedCollisionEntry Entry;
			TArray<TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>> Slots;
			for (int32 SourceIndex = 0; SourceIndex < GSharedCollisionSourceCount; ++SourceIndex)
			{
				Slots.Add(Entry.GetOrCreateSlot(static_cast<uint64>(SourceIndex) + 1));
			}

			// 本番のSharedCollisionPublishScratchに相当する使い回しスクラッチ
			// （Publishのswapで前フレームのBufferが戻り、確保済みメモリを再利用できる）。
			TArray<FKawaiiPhysicsSharedCollisionData> PublishScratches;
			PublishScratches.SetNum(GSharedCollisionSourceCount);

			// 本番のSharedCollisionMergedData/Shared*Limitsに相当する使い回しバッファ。
			FKawaiiPhysicsSharedCollisionData MergedData;
			FKawaiiPhysicsSharedCollisionData SharedStore;

			for (int32 Frame = 0; Frame < GWarmupFrames; ++Frame)
			{
				for (int32 SourceIndex = 0; SourceIndex < GSharedCollisionSourceCount; ++SourceIndex)
				{
					PublishSharedCollisionSource(SourceTemplates[SourceIndex], PublishScratches[SourceIndex],
						*Slots[SourceIndex]);
				}
				Entry.ReadMerged(MergedData);
				StoreSharedCollisionLimits(MergedData, SharedStore);
			}

			const double StartSeconds = FPlatformTime::Seconds();
			for (int32 Frame = 0; Frame < GMeasureFrames; ++Frame)
			{
				for (int32 SourceIndex = 0; SourceIndex < GSharedCollisionSourceCount; ++SourceIndex)
				{
					PublishSharedCollisionSource(SourceTemplates[SourceIndex], PublishScratches[SourceIndex],
						*Slots[SourceIndex]);
				}
				Entry.ReadMerged(MergedData);
				LastMergedLimitCount = StoreSharedCollisionLimits(MergedData, SharedStore);
			}
			const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			const double MsPerFrame = ElapsedSeconds * 1000.0 / static_cast<double>(GMeasureFrames);

			Test.AddInfo(FString::Printf(TEXT("PERF_RAW KawaiiPhysics.Perf.SharedCollisionCopy trial=%d ms=%.6f"),
				Trial, MsPerFrame));
			MsPerFrameValues.Add(MsPerFrame);
		}

		// コピー漏れ/重複がないことを最終フレームのマージ結果件数で検証する（2ソース分の合計件数と一致するはず）。
		const bool bCountOk = Test.TestEqual(
			TEXT("Merged limit count matches two sources worth of template limits"),
			LastMergedLimitCount, GSharedCollisionLimitsPerFrame);

		MsPerFrameValues.Sort();
		const double MedianMsPerFrame = MsPerFrameValues[GTrials / 2];
		Test.AddInfo(FString::Printf(
			TEXT("PERF KawaiiPhysics.Perf.SharedCollisionCopy median_ms_per_frame=%.6f limits_per_frame=%d"),
			MedianMsPerFrame, GSharedCollisionLimitsPerFrame));

		return bCountOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfChainTest,
                                 "KawaiiPhysics.Perf.Chain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfChainTest::RunTest(const FString& Parameters)
{
	return RunSimulationPerf(*this, TEXT("KawaiiPhysics.Perf.Chain"),
		[](FKawaiiPhysicsTestAccessor& A)
		{
			A.BuildVerticalChain(200, 5.0f);
			ConfigureBaseSimulation(A);
		});
}

// legacy（サブステップOFF）。Exponent = TargetFramerate * DeltaTime となり 1.0f にならないため、
// 固定サブステップ時のように powf の y==1 特殊ケースへ落ちない。Stiffness の Pow コストはここで初めて現れる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfChainLegacyTest,
                                 "KawaiiPhysics.Perf.ChainLegacy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfChainLegacyTest::RunTest(const FString& Parameters)
{
	return RunSimulationPerf(*this, TEXT("KawaiiPhysics.Perf.ChainLegacy"),
		[](FKawaiiPhysicsTestAccessor& A)
		{
			A.BuildVerticalChain(200, 5.0f);
			A.SetAllPhysicsSettings(MakePerfSettings(2.0f));
			A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
			A.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
			A.SetFixedSubstepping(false, 60, 4);
			A.SetSkelCompMove(FVector(0.3f, 0.0f, 0.0f), FQuat::Identity);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfCollisionTest,
                                 "KawaiiPhysics.Perf.Collision",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfCollisionTest::RunTest(const FString& Parameters)
{
	return RunSimulationPerf(*this, TEXT("KawaiiPhysics.Perf.Collision"),
		[](FKawaiiPhysicsTestAccessor& A)
		{
			A.BuildVerticalChain(200, 5.0f);
			ConfigureBaseSimulation(A, 3.0f);
			AddPerfCollisionLimits(A);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfConstraintTest,
                                 "KawaiiPhysics.Perf.Constraint",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfConstraintTest::RunTest(const FString& Parameters)
{
	return RunSimulationPerf(*this, TEXT("KawaiiPhysics.Perf.Constraint"),
		[](FKawaiiPhysicsTestAccessor& A)
		{
			A.BuildTwoVerticalChains(100, 5.0f, 8.0f);
			ConfigureBaseSimulation(A);
			A.SetBoneConstraintIterations(4, 4);
			A.SetBoneConstraintGlobalComplianceType(EXPBDComplianceType::Leather);
			// 制約長を実際の横間隔(8)より短くして違反量を常に非ゼロにする（早期returnで計測が痩せるのを防ぐ）。
			for (int32 Depth = 0; Depth < 100; ++Depth)
			{
				A.AddRuntimeBoneConstraint(Depth, 100 + Depth, 6.0f);
			}
		});
}

// 拘束計算そのものを支配的にした重量ベンチ。1000ボーン / 999拘束 / 反復16+16。
// 制約毎の除算やコンプライアンス表引きのような小さな差を、ボーン側の処理に埋もれさせずに測るためのもの。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfConstraintHeavyTest,
                                 "KawaiiPhysics.Perf.ConstraintHeavy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfConstraintHeavyTest::RunTest(const FString& Parameters)
{
	return RunSimulationPerf(*this, TEXT("KawaiiPhysics.Perf.ConstraintHeavy"),
		[](FKawaiiPhysicsTestAccessor& A)
		{
			constexpr int32 PerChain = 500;
			A.BuildTwoVerticalChains(PerChain, 5.0f, 8.0f);
			ConfigureBaseSimulation(A);
			A.SetBoneConstraintIterations(16, 16);
			A.SetBoneConstraintGlobalComplianceType(EXPBDComplianceType::Leather);
			// 横方向と斜め方向の両方を張り、ボーン数に対して拘束数を稼ぐ。長さは実距離より短くして常に違反させる。
			for (int32 Depth = 0; Depth < PerChain; ++Depth)
			{
				A.AddRuntimeBoneConstraint(Depth, PerChain + Depth, 6.0f);
			}
			for (int32 Depth = 0; Depth < PerChain - 1; ++Depth)
			{
				A.AddRuntimeBoneConstraint(Depth, PerChain + Depth + 1, 7.0f);
			}
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfPhysicsSettingsTest,
                                 "KawaiiPhysics.Perf.PhysicsSettings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfPhysicsSettingsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	bOk &= RunPhysicsSettingsPerf(*this, TEXT("KawaiiPhysics.Perf.PhysicsSettings.CurvesEmpty"), false);
	bOk &= RunPhysicsSettingsPerf(*this, TEXT("KawaiiPhysics.Perf.PhysicsSettings.CurvesSet"), true);
	return bOk;
}

// Shared コリジョン経路（Publish→ReadMerged→格納）の構造体コピー帯域を計測する。
// ソース2つ×(Sphere8+Capsule8+TaperedCapsule8+Box8+Planar4) = 72limit/frame を毎フレーム
// Publish→ReadMerged→要素毎コピーし、その所要時間を中央値で報告する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfSharedCollisionCopyTest,
                                 "KawaiiPhysics.Perf.SharedCollisionCopy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfSharedCollisionCopyTest::RunTest(const FString& Parameters)
{
	return RunSharedCollisionCopyPerf(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfSizeofTest,
                                 "KawaiiPhysics.Perf.Sizeof",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfSizeofTest::RunTest(const FString& Parameters)
{
	AddInfo(FString::Printf(TEXT("SIZEOF FKawaiiPhysicsModifyBone = %d"),
	                        static_cast<int32>(sizeof(FKawaiiPhysicsModifyBone))));
	AddInfo(FString::Printf(TEXT("SIZEOF FKawaiiPhysicsSettings = %d"),
	                        static_cast<int32>(sizeof(FKawaiiPhysicsSettings))));
	AddInfo(FString::Printf(TEXT("SIZEOF FSphericalLimit = %d"), static_cast<int32>(sizeof(FSphericalLimit))));
	AddInfo(FString::Printf(TEXT("SIZEOF FCapsuleLimit = %d"), static_cast<int32>(sizeof(FCapsuleLimit))));
	AddInfo(FString::Printf(TEXT("SIZEOF FTaperedCapsuleLimit = %d"),
	                        static_cast<int32>(sizeof(FTaperedCapsuleLimit))));
	AddInfo(FString::Printf(TEXT("SIZEOF FBoxLimit = %d"), static_cast<int32>(sizeof(FBoxLimit))));
	AddInfo(FString::Printf(TEXT("SIZEOF FPlanarLimit = %d"), static_cast<int32>(sizeof(FPlanarLimit))));
	AddInfo(FString::Printf(TEXT("SIZEOF FModifyBoneConstraint = %d"),
	                        static_cast<int32>(sizeof(FModifyBoneConstraint))));
	AddInfo(FString::Printf(TEXT("SIZEOF FKawaiiPhysics_ExternalForce = %d"),
	                        static_cast<int32>(sizeof(FKawaiiPhysics_ExternalForce))));
	return true;
}

#endif
