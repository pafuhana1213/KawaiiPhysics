// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsMemoryTraceRegion.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// Intentionally uses only the pre-optimization public API so this exact test can run against a baseline.
	SIZE_T GetTickPerfDataBytes(const FKawaiiPhysicsSharedCollisionData& Data)
	{
		SIZE_T Bytes = Data.SphericalLimits.GetAllocatedSize() + Data.CapsuleLimits.GetAllocatedSize()
			+ Data.TaperedCapsuleLimits.GetAllocatedSize() + Data.BoxLimits.GetAllocatedSize()
			+ Data.PlanarLimits.GetAllocatedSize() + Data.ConvexLimits.GetAllocatedSize();
		for (const FKawaiiPhysicsConvexLimit& Convex : Data.ConvexLimits)
		{
			Bytes += Convex.LocalPlanes.GetAllocatedSize();
#if !UE_BUILD_SHIPPING
			Bytes += Convex.LocalVertices.GetAllocatedSize() + Convex.LocalEdges.GetAllocatedSize();
#endif
		}
		return Bytes;
	}

	struct FSubsystemTickPerfFixture
	{
		UWorld* World = nullptr;
		UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = nullptr;
		TStrongObjectPtr<USkeletalMeshComponent> Reader;
		TStrongObjectPtr<UStaticMeshComponent> Collider;
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry;
		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;

		FSubsystemTickPerfFixture()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.InitializeScenes(false).AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			Subsystem = World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>();
			Reader.Reset(NewObject<USkeletalMeshComponent>(World));
			Collider.Reset(NewObject<UStaticMeshComponent>(World));
			Desc.GatherIntervalSec = 1000000.0f;
			Desc.bGroundCollision = false;
			Entry = Subsystem->FindOrCreateSimpleWorldEntry(Reader.Get(), 1, Desc);
			for (uint64 SourceID = 2; SourceID <= 16; ++SourceID)
			{
				Entry->SetDesc(SourceID, Desc, GFrameCounter, Reader.Get(), true);
			}
			Entry->ConsumeRegatherRequested();
			Entry->bHasGatheredOnce = true;
			Entry->TimeSinceLastGather = 0;
			Entry->bGroundBoxDirty = false;
			// A zero-radius synthetic reader prevents scene queries. Gather is deliberately excluded;
			// transform changes independently exercise the production update + Publish path every 1/12 ticks.
			Reader->Bounds = FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0);
			auto& Gathered = Entry->GatheredComponents.AddDefaulted_GetRef();
			Gathered.Component = Collider.Get();
			Gathered.LastComponentTM = FTransform::Identity;
			Gathered.bStatic = false;
			for (int32 Index = 0; Index < 32; ++Index)
			{
				FKawaiiPhysicsConvexLimit& Convex = Gathered.LocalLimits.ConvexLimits.AddDefaulted_GetRef();
				Convex.Location = FVector(Index * 3, 0, 0);
				Convex.LocalBounds = FBox(FVector(-1), FVector(1));
				Convex.bEnable = true;
				Convex.LocalPlanes = { FPlane(1, 0, 0, 1), FPlane(-1, 0, 0, 1),
					FPlane(0, 1, 0, 1), FPlane(0, -1, 0, 1), FPlane(0, 0, 1, 1), FPlane(0, 0, -1, 1) };
			}
			for (int32 Index = 0; Index < 16; ++Index)
			{
				FBoxLimit& Box = Gathered.LocalLimits.BoxLimits.AddDefaulted_GetRef();
				Box.Location = FVector(Index * 3, 10, 0);
				Box.Extent = FVector(1);
				Box.bEnable = true;
			}
		}

		~FSubsystemTickPerfFixture()
		{
			Entry.Reset();
			Collider.Reset();
			Reader.Reset();
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		void Update(int32 Frame, int32 PublishInterval, bool bChangeDesc)
		{
			for (uint64 SourceID = 1; SourceID <= 16; ++SourceID)
			{
				Entry->MarkRead(SourceID);
			}
			if (bChangeDesc)
			{
				Desc.GatherIntervalSec = Frame % 2 == 0 ? 1000000.0f : 2000000.0f;
				Entry->SetDesc(1, Desc, GFrameCounter, Reader.Get(), true);
			}
			if (Frame % PublishInterval == 0)
			{
				Collider->SetWorldLocation(FVector(Frame + 1, 0, 0));
			}
		}
	};

	bool RunSubsystemTickPerf(FAutomationTestBase& Test, const TCHAR* Label, int32 PublishInterval, bool bChangeDesc)
	{
		constexpr int32 WarmupFrames = 120;
		constexpr int32 MeasureFrames = 2400;
		constexpr float DeltaTime = 1.0f / 60.0f;
		TArray<double> FrameTrials;
		TArray<double> TickTrials;
		for (int32 Trial = 0; Trial < 5; ++Trial)
		{
			FKawaiiPhysicsMemoryTraceRegion MemoryRegion(Label, Trial + 1);
			FSubsystemTickPerfFixture Fixture;
			for (int32 Frame = 0; Frame < WarmupFrames; ++Frame)
			{
				Fixture.Update(Frame, PublishInterval, bChangeDesc);
				Fixture.Subsystem->Tick(DeltaTime);
			}
			const uint64 SerialBefore = Fixture.Entry->Slot.GetPublishSerial();
			double TickSeconds = 0;
			MemoryRegion.Warmup();
			const double FrameStart = FPlatformTime::Seconds();
			for (int32 Frame = WarmupFrames; Frame < WarmupFrames + MeasureFrames; ++Frame)
			{
				Fixture.Update(Frame, PublishInterval, bChangeDesc);
				const double TickStart = FPlatformTime::Seconds();
				Fixture.Subsystem->Tick(DeltaTime);
				TickSeconds += FPlatformTime::Seconds() - TickStart;
			}
			const double FrameMs = (FPlatformTime::Seconds() - FrameStart) * 1000 / MeasureFrames;
			MemoryRegion.End();
			const double TickMs = TickSeconds * 1000 / MeasureFrames;
			FrameTrials.Add(FrameMs);
			TickTrials.Add(TickMs);
			const uint64 Publishes = Fixture.Entry->Slot.GetPublishSerial() - SerialBefore;
			Test.TestEqual(TEXT("Publish cadence follows transform changes, independently of gather"),
				Publishes, static_cast<uint64>(MeasureFrames / PublishInterval));
			FKawaiiPhysicsSharedCollisionData Published;
			Fixture.Entry->Slot.AppendTo(Published);
			Test.TestEqual(TEXT("Real Tick publishes all seeded convexes"), Published.ConvexLimits.Num(), 32);
			Test.TestEqual(TEXT("Real Tick publishes all seeded boxes"), Published.BoxLimits.Num(), 16);
			Test.TestEqual(TEXT("Seeded gathered component survives the benchmark"), Fixture.Entry->GatheredComponents.Num(), 1);
			Test.TestTrue(TEXT("No gather was run during the benchmark"), Fixture.Entry->TimeSinceLastGather > 1);
			const SIZE_T ScratchBytes = GetTickPerfDataBytes(Fixture.Entry->PublishScratch);
			Test.AddInfo(FString::Printf(
				TEXT("SUBSYSTEM_TICK_PERF case=%s trial=%d frame_ms=%.6f tick_ms=%.6f publishes=%llu scratch_bytes=%llu published_copy_bytes=%llu allocation_calls=unavailable"),
				Label, Trial + 1, FrameMs, TickMs, Publishes, static_cast<uint64>(ScratchBytes),
				static_cast<uint64>(GetTickPerfDataBytes(Published))));
		}
		FrameTrials.Sort();
		TickTrials.Sort();
		Test.AddInfo(FString::Printf(TEXT("SUBSYSTEM_TICK_PERF_MEDIAN case=%s frame_ms=%.6f tick_ms=%.6f providers=16 convexes=32 boxes=16"),
			Label, FrameTrials[2], TickTrials[2]));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSubsystemTickStablePerfTest,
	"KawaiiPhysics.Perf.SubsystemTick.StableProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

bool FKawaiiPhysicsSubsystemTickStablePerfTest::RunTest(const FString& Parameters)
{
	RunSubsystemTickPerf(*this, TEXT("Stable.PublishEveryFrame"), 1, false);
	return RunSubsystemTickPerf(*this, TEXT("Stable.PublishEvery12"), 12, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSubsystemTickChangingPerfTest,
	"KawaiiPhysics.Perf.SubsystemTick.ChangingProvider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

bool FKawaiiPhysicsSubsystemTickChangingPerfTest::RunTest(const FString& Parameters)
{
	RunSubsystemTickPerf(*this, TEXT("Changing.PublishEveryFrame"), 1, true);
	return RunSubsystemTickPerf(*this, TEXT("Changing.PublishEvery12"), 12, true);
}

#endif // WITH_DEV_AUTOMATION_TESTS
