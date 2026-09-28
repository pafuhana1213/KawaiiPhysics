// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstanceProxy.h"
#include "HAL/PlatformTime.h"
#include "Runtime/Launch/Resources/Version.h"
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
	constexpr int32 GKawaiiPhysicsPerfCalibrationIterations = 47000; // 開発機 Ryzen 9 3950X / Development Editor で約30〜45ms。
	static volatile double GKawaiiPhysicsPerfCalibrationSink = 0.0;
#ifdef _MSC_FULL_VER
	constexpr int32 KawaiiPerfMscFullVer = _MSC_FULL_VER;
#else
	constexpr int32 KawaiiPerfMscFullVer = 0;
#endif

	// 較正ループはベンチ本体とは別の固定計算を試行ごとに走らせ、実行時の機械状態を見るための指標にする。
	// 合否判定や過去ログ比較への使い方は compare-perf.ps1 側に任せ、このテストでは calib_ms として記録だけ行う。
	FORCENOINLINE double RunKawaiiPhysicsPerfCalibrationLoop()
	{
		constexpr int32 PointCount = 256;
		constexpr double Dt = 1.0 / 60.0;
		constexpr double DtSquared = Dt * Dt;
		FVector Positions[PointCount];
		FVector PreviousPositions[PointCount];

		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			const double Scale = static_cast<double>(Index + 1);
			Positions[Index] = FVector(Scale * 0.25, Scale * -0.125, Scale * 0.0625);
			PreviousPositions[Index] = Positions[Index] - FVector(0.01 * Scale, -0.02 * Scale, 0.015 * Scale);
		}

		for (int32 Iteration = 0; Iteration < GKawaiiPhysicsPerfCalibrationIterations; ++Iteration)
		{
			for (int32 Index = 0; Index < PointCount; ++Index)
			{
				const double AccelScale = static_cast<double>((Index % 17) + 1);
				const FVector Accel(AccelScale * 0.001, AccelScale * -0.002, -0.01 - AccelScale * 0.0005);
				const FVector Current = Positions[Index];
				const FVector Next = Current + (Current - PreviousPositions[Index]) * 0.99 + Accel * DtSquared;
				PreviousPositions[Index] = Current;
				Positions[Index] = Next;
			}
		}

		double Sum = 0.0;
		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			Sum += Positions[Index].X + Positions[Index].Y + Positions[Index].Z;
		}

		GKawaiiPhysicsPerfCalibrationSink = GKawaiiPhysicsPerfCalibrationSink + Sum;
		return Sum;
	}

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

	// シミュレーション系ベンチはベンチごとのフレーム数で1試行の実時間を伸ばす。
	// 較正ループの直後にwarmupを挟んでから計測し、中央値・最小値・tip座標checksumを記録する。
	bool RunSimulationPerf(FAutomationTestBase& Test, const TCHAR* TestName,
	                       const TFunction<void(FKawaiiPhysicsTestAccessor&)>& Setup,
	                       const int32 MeasureFrames = GMeasureFrames,
	                       const double StepsPerFrame = GAverageSubsteps,
	                       const TFunction<void(FKawaiiPhysicsTestAccessor&, int32)>& BeforeMeasureFrame =
		                       TFunction<void(FKawaiiPhysicsTestAccessor&, int32)>())
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

			const double CalibrationStartSeconds = FPlatformTime::Seconds();
			RunKawaiiPhysicsPerfCalibrationLoop();
			const double CalibMs = (FPlatformTime::Seconds() - CalibrationStartSeconds) * 1000.0;

			for (int32 Frame = 0; Frame < GWarmupFrames; ++Frame)
			{
				A.StepFrame(GFrameDt);
			}

			const double StartSeconds = FPlatformTime::Seconds();
			double TrialChecksum = 0.0;
			for (int32 Frame = 0; Frame < MeasureFrames; ++Frame)
			{
				if (BeforeMeasureFrame)
				{
					BeforeMeasureFrame(A, Frame);
				}
				A.StepFrame(GFrameDt);
				const FVector Tip = A.TipLocation();
				TrialChecksum += Tip.X + Tip.Y + Tip.Z;
			}
			const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			const double MsPerFrame = ElapsedSeconds * 1000.0 / static_cast<double>(MeasureFrames);

			Test.AddInfo(FString::Printf(
				TEXT("PERF_RAW %s trial=%d ms=%.6f calib_ms=%.6f"),
				TestName, Trial, MsPerFrame, CalibMs));
			MsPerFrameValues.Add(MsPerFrame);
			Checksum += TrialChecksum;

			if (!A.AllFinite())
			{
				Test.AddError(FString::Printf(TEXT("PERF %s produced NaN or Inf"), TestName));
				bFinite = false;
			}
		}

		MsPerFrameValues.Sort();
		const double MinMsPerFrame = MsPerFrameValues[0];
		const double MedianMsPerFrame = MsPerFrameValues[GTrials / 2];
		const double NsPerBoneStep = MedianMsPerFrame * 1000000.0 /
			FMath::Max(1.0, static_cast<double>(BoneCount) * StepsPerFrame);
		Test.AddInfo(FString::Printf(
			TEXT("PERF %s median_ms_per_frame=%.6f min_ms_per_frame=%.6f ns_per_bone_step=%.3f checksum=%.6f"),
			TestName, MedianMsPerFrame, MinMsPerFrame, NsPerBoneStep, Checksum));
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

	// PhysicsSettings更新ベンチは曲線有無ごとの呼び出し回数で試行長を揃える。
	// 較正ループの直後に未計測warmupでベンチ経路のキャッシュを温め直してから計測し、
	// 200ボーン基準の ns_per_bone_step と、試行ごとの較正時間を同じログへ出す。
	bool RunPhysicsSettingsPerf(FAutomationTestBase& Test, const TCHAR* TestName, const bool bSetDampingCurve,
	                            const int32 Calls)
	{
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

			const double CalibrationStartSeconds = FPlatformTime::Seconds();
			RunKawaiiPhysicsPerfCalibrationLoop();
			const double CalibMs = (FPlatformTime::Seconds() - CalibrationStartSeconds) * 1000.0;

			// 較正後にベンチ経路のキャッシュを温め直す（計測に含めないwarmup呼び出し）。
			for (int32 WarmupCall = 0; WarmupCall < GWarmupFrames; ++WarmupCall)
			{
				A.CallUpdatePhysicsSettings();
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

			Test.AddInfo(FString::Printf(
				TEXT("PERF_RAW %s trial=%d ms=%.6f calib_ms=%.6f"),
				TestName, Trial, MsPerCall, CalibMs));
			MsPerCallValues.Add(MsPerCall);
			Checksum += TrialChecksum;

			if (!PhysicsSettingsFinite(A))
			{
				Test.AddError(FString::Printf(TEXT("PERF %s produced NaN or Inf"), TestName));
				bFinite = false;
			}
		}

		MsPerCallValues.Sort();
		const double MinMsPerCall = MsPerCallValues[0];
		const double MedianMsPerCall = MsPerCallValues[GTrials / 2];
		const double NsPerBone = MedianMsPerCall * 1000000.0 / 200.0;
		Test.AddInfo(FString::Printf(
			TEXT("PERF %s median_ms_per_frame=%.6f min_ms_per_frame=%.6f ns_per_bone_step=%.3f checksum=%.6f"),
			TestName, MedianMsPerCall, MinMsPerCall, NsPerBone, Checksum));
		return bFinite;
	}

	// ---------------------------------------------------------------
	// SimpleWorld 読み取りベンチ
	// ---------------------------------------------------------------
	// SimpleWorld のワーカー側読み取り経路（Slot serial 判定、必要時 AppendTo、simulation 空間配列更新）を計測する。
	// 形状 Slot は Convex64+Box64、GroundSlot は Box1。Publish 間隔 1 と 12 で全再構築寄り/インプレース更新寄りを分ける。

	constexpr int32 GSimpleWorldReadConvexCount = 64;
	constexpr int32 GSimpleWorldReadBoxCount = 64;
	constexpr int32 GSimpleWorldReadGroundBoxCount = 1;
	constexpr int32 GSimpleWorldReadLimitsPerFrame =
		GSimpleWorldReadConvexCount + GSimpleWorldReadBoxCount + GSimpleWorldReadGroundBoxCount;

	template <typename TLimitArray>
	void CopySimpleWorldReadLimitsElementwise(const TLimitArray& InLimits, TLimitArray& OutLimits)
	{
		OutLimits.Reserve(OutLimits.Num() + InLimits.Num());
		for (const auto& Limit : InLimits)
		{
			auto Converted = Limit;
			OutLimits.Add(Converted);
		}
	}

	TArray<FPlane> MakeSimpleWorldReadUnitCubePlanes()
	{
		TArray<FPlane> Planes;
		Planes.Reserve(6);
		Planes.Add(FPlane(1.0f, 0.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(-1.0f, 0.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(0.0f, 1.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(0.0f, -1.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(0.0f, 0.0f, 1.0f, 1.0f));
		Planes.Add(FPlane(0.0f, 0.0f, -1.0f, 1.0f));
		return Planes;
	}

	FKawaiiPhysicsSharedCollisionData MakeSimpleWorldReadSourceTemplate()
	{
		FKawaiiPhysicsSharedCollisionData Data;
		Data.ConvexLimits.Reserve(GSimpleWorldReadConvexCount);
		Data.BoxLimits.Reserve(GSimpleWorldReadBoxCount);

		const TArray<FPlane> UnitCubePlanes = MakeSimpleWorldReadUnitCubePlanes();
		for (int32 Index = 0; Index < GSimpleWorldReadConvexCount; ++Index)
		{
			const int32 GridX = Index % 8;
			const int32 GridY = Index / 8;

			FKawaiiPhysicsConvexLimit Convex;
			Convex.Location = FVector(GridX * 25.0f, GridY * 25.0f, 30.0f + (Index % 4) * 6.0f);
			Convex.Rotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Index * 3.0f));
			Convex.LocalPlanes = UnitCubePlanes;
			Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
			Convex.bEnable = true;
			Convex.SourceType = ECollisionSourceType::SimpleWorld;
			Data.ConvexLimits.Add(Convex);
		}

		for (int32 Index = 0; Index < GSimpleWorldReadBoxCount; ++Index)
		{
			const int32 GridX = Index % 8;
			const int32 GridY = Index / 8;

			FBoxLimit Box;
			Box.Location = FVector(220.0f + GridX * 28.0f, GridY * 28.0f, 40.0f + (Index % 5) * 5.0f);
			Box.Rotation = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Index * 2.0f));
			Box.Extent = FVector(10.0f, 10.0f, 10.0f);
			Box.bEnable = true;
			Box.SourceType = ECollisionSourceType::SimpleWorld;
			Data.BoxLimits.Add(Box);
		}

		return Data;
	}

	FKawaiiPhysicsSharedCollisionData MakeSimpleWorldReadGroundTemplate()
	{
		FKawaiiPhysicsSharedCollisionData Data;
		Data.BoxLimits.Reserve(GSimpleWorldReadGroundBoxCount);

		FBoxLimit GroundBox;
		GroundBox.Location = FVector(120.0f, 120.0f, -20.0f);
		GroundBox.Rotation = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(2.0f));
		GroundBox.Extent = FVector(180.0f, 180.0f, 10.0f);
		GroundBox.bEnable = true;
		GroundBox.SourceType = ECollisionSourceType::SimpleWorld;
		Data.BoxLimits.Add(GroundBox);

		return Data;
	}

	void EnsureSimpleWorldReadScratch(const FKawaiiPhysicsSharedCollisionData& Template,
	                                  FKawaiiPhysicsSharedCollisionData& Scratch)
	{
		if (Scratch.SphericalLimits.Num() == Template.SphericalLimits.Num() &&
			Scratch.CapsuleLimits.Num() == Template.CapsuleLimits.Num() &&
			Scratch.TaperedCapsuleLimits.Num() == Template.TaperedCapsuleLimits.Num() &&
			Scratch.BoxLimits.Num() == Template.BoxLimits.Num() &&
			Scratch.PlanarLimits.Num() == Template.PlanarLimits.Num() &&
			Scratch.ConvexLimits.Num() == Template.ConvexLimits.Num())
		{
			return;
		}

		Scratch.Reset();
		CopySimpleWorldReadLimitsElementwise(Template.SphericalLimits, Scratch.SphericalLimits);
		CopySimpleWorldReadLimitsElementwise(Template.CapsuleLimits, Scratch.CapsuleLimits);
		CopySimpleWorldReadLimitsElementwise(Template.TaperedCapsuleLimits, Scratch.TaperedCapsuleLimits);
		CopySimpleWorldReadLimitsElementwise(Template.BoxLimits, Scratch.BoxLimits);
		CopySimpleWorldReadLimitsElementwise(Template.PlanarLimits, Scratch.PlanarLimits);
		CopySimpleWorldReadLimitsElementwise(Template.ConvexLimits, Scratch.ConvexLimits);
	}

	template <typename TLimitArray>
	void OffsetSimpleWorldReadLimitLocations(const TLimitArray& TemplateLimits, TLimitArray& ScratchLimits,
	                                         const FVector& Offset)
	{
		for (int32 Index = 0; Index < ScratchLimits.Num(); ++Index)
		{
			ScratchLimits[Index].Location = TemplateLimits[Index].Location + Offset;
		}
	}

	void PublishSimpleWorldReadSource(const FKawaiiPhysicsSharedCollisionData& Template,
	                                  FKawaiiPhysicsSharedCollisionData& Scratch,
	                                  FKawaiiPhysicsSharedCollisionSourceSlot& Slot,
	                                  uint64 PublishIndex)
	{
		EnsureSimpleWorldReadScratch(Template, Scratch);

		const FVector Offset(0.01f * static_cast<float>(PublishIndex), 0.0f, 0.0f);
		OffsetSimpleWorldReadLimitLocations(Template.SphericalLimits, Scratch.SphericalLimits, Offset);
		OffsetSimpleWorldReadLimitLocations(Template.CapsuleLimits, Scratch.CapsuleLimits, Offset);
		OffsetSimpleWorldReadLimitLocations(Template.TaperedCapsuleLimits, Scratch.TaperedCapsuleLimits, Offset);
		OffsetSimpleWorldReadLimitLocations(Template.BoxLimits, Scratch.BoxLimits, Offset);
		OffsetSimpleWorldReadLimitLocations(Template.PlanarLimits, Scratch.PlanarLimits, Offset);
		OffsetSimpleWorldReadLimitLocations(Template.ConvexLimits, Scratch.ConvexLimits, Offset);

		Slot.Publish(Scratch);
	}

	bool RunSimpleWorldReadPerf(FAutomationTestBase& Test, const TCHAR* TestLabel, int32 PublishInterval)
	{
		const int32 MeasureFrames = 15000;
		const int32 SafePublishInterval = FMath::Max(1, PublishInterval);
		const FKawaiiPhysicsSharedCollisionData SourceTemplate = MakeSimpleWorldReadSourceTemplate();
		const FKawaiiPhysicsSharedCollisionData GroundTemplate = MakeSimpleWorldReadGroundTemplate();

		TArray<double> MsPerFrameValues;
		MsPerFrameValues.Reserve(GTrials);

		for (int32 Trial = 0; Trial < GTrials; ++Trial)
		{
			TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
				MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
			FKawaiiPhysicsTestAccessor Accessor;
			Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::WorldSpace);
			Accessor.BuildVerticalChain(4, 10.0f);
			Accessor.SetSimpleWorldEntry(Entry);

			FAnimInstanceProxy AnimInstanceProxy;
			FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

			FKawaiiPhysicsSharedCollisionData ShapeScratch;
			FKawaiiPhysicsSharedCollisionData GroundScratch;
			uint64 ShapePublishCount = 0;
			uint64 GroundPublishCount = 0;

			const double CalibrationStartSeconds = FPlatformTime::Seconds();
			RunKawaiiPhysicsPerfCalibrationLoop();
			const double CalibMs = (FPlatformTime::Seconds() - CalibrationStartSeconds) * 1000.0;

			for (int32 Frame = 0; Frame < GWarmupFrames; ++Frame)
			{
				if (Frame % SafePublishInterval == 0)
				{
					PublishSimpleWorldReadSource(SourceTemplate, ShapeScratch, Entry->Slot, ShapePublishCount);
					++ShapePublishCount;
				}
				PublishSimpleWorldReadSource(GroundTemplate, GroundScratch, Entry->GroundSlot, GroundPublishCount);
				++GroundPublishCount;
				Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
			}

			const double StartSeconds = FPlatformTime::Seconds();
			for (int32 Frame = 0; Frame < MeasureFrames; ++Frame)
			{
				if (Frame % SafePublishInterval == 0)
				{
					PublishSimpleWorldReadSource(SourceTemplate, ShapeScratch, Entry->Slot, ShapePublishCount);
					++ShapePublishCount;
				}
				PublishSimpleWorldReadSource(GroundTemplate, GroundScratch, Entry->GroundSlot, GroundPublishCount);
				++GroundPublishCount;
				Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
			}
			const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			const double MsPerFrame = ElapsedSeconds * 1000.0 / static_cast<double>(MeasureFrames);

			Test.AddInfo(FString::Printf(
				TEXT("PERF_RAW KawaiiPhysics.Perf.SimpleWorldRead.%s trial=%d ms=%.6f calib_ms=%.6f"),
				TestLabel, Trial, MsPerFrame, CalibMs));
			MsPerFrameValues.Add(MsPerFrame);

		}

		MsPerFrameValues.Sort();
		const double MinMsPerFrame = MsPerFrameValues[0];
		const double MedianMsPerFrame = MsPerFrameValues[GTrials / 2];
		Test.AddInfo(FString::Printf(
			TEXT("PERF KawaiiPhysics.Perf.SimpleWorldRead.%s median_ms_per_frame=%.6f min_ms_per_frame=%.6f limits_per_frame=%d"),
			TestLabel, MedianMsPerFrame, MinMsPerFrame, GSimpleWorldReadLimitsPerFrame));

		return true;
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
		},
		12000);
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
		},
		12000,
		1.0);
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
		},
		5000);
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
		},
		1000);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfPhysicsSettingsTest,
                                 "KawaiiPhysics.Perf.PhysicsSettings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfPhysicsSettingsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	bOk &= RunPhysicsSettingsPerf(*this, TEXT("KawaiiPhysics.Perf.PhysicsSettings.CurvesEmpty"), false, 500000);
	bOk &= RunPhysicsSettingsPerf(*this, TEXT("KawaiiPhysics.Perf.PhysicsSettings.CurvesSet"), true, 30000);
	return bOk;
}

// 形状 Slot の毎フレーム更新と 12 フレーム間隔更新を、同じ読み取りベンチで計測する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfSimpleWorldReadTest,
                                 "KawaiiPhysics.Perf.SimpleWorldRead",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfSimpleWorldReadTest::RunTest(const FString& Parameters)
{
	bool bOk = RunSimpleWorldReadPerf(*this, TEXT("PublishEveryFrame"), 1);
	bOk &= RunSimpleWorldReadPerf(*this, TEXT("PublishEvery12"), 12);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerfSizeofTest,
                                 "KawaiiPhysics.Perf.Sizeof",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerfSizeofTest::RunTest(const FString& Parameters)
{
	AddInfo(FString::Printf(TEXT("TOOLCHAIN msc_full_ver=%d engine=%s"),
	                        KawaiiPerfMscFullVer, ENGINE_VERSION_STRING));
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
