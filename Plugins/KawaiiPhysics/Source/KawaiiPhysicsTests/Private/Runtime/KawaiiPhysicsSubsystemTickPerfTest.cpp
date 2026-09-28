// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
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
			// 半径ゼロの reader でシーンクエリを避け、変換更新から Publish までを確認する。
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

	bool RunSubsystemTickPublishCase(FAutomationTestBase& Test, const TCHAR* Label, int32 PublishInterval, bool bChangeDesc)
	{
		constexpr int32 CheckFrames = 24;
		constexpr float DeltaTime = 1.0f / 60.0f;
		FSubsystemTickPerfFixture Fixture;
		const uint64 SerialBefore = Fixture.Entry->Slot.GetPublishSerial();
		for (int32 Frame = 0; Frame < CheckFrames; ++Frame)
		{
			Fixture.Update(Frame, PublishInterval, bChangeDesc);
			Fixture.Subsystem->Tick(DeltaTime);
		}
		bool bOk = true;
		const uint64 Publishes = Fixture.Entry->Slot.GetPublishSerial() - SerialBefore;
		bOk &= Test.TestEqual(FString::Printf(TEXT("%s: Publish cadence follows transform changes"), Label),
			Publishes, static_cast<uint64>(CheckFrames / PublishInterval));
		FKawaiiPhysicsSharedCollisionData Published;
		Fixture.Entry->Slot.AppendTo(Published);
		bOk &= Test.TestEqual(FString::Printf(TEXT("%s: Tick publishes all seeded convexes"), Label), Published.ConvexLimits.Num(), 32);
		bOk &= Test.TestEqual(FString::Printf(TEXT("%s: Tick publishes all seeded boxes"), Label), Published.BoxLimits.Num(), 16);
		bOk &= Test.TestEqual(FString::Printf(TEXT("%s: gathered component survives"), Label), Fixture.Entry->GatheredComponents.Num(), 1);
		bOk &= Test.TestTrue(FString::Printf(TEXT("%s: no gather was run"), Label),
			FMath::IsNearlyEqual(Fixture.Entry->TimeSinceLastGather, CheckFrames * DeltaTime, 1.0e-3f));
		return bOk;
	}
}

// provider の設定が安定する場合と変化する場合に、移動に応じた Publish と gather 状態を守る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSubsystemTickPublishTest,
	"KawaiiPhysics.SimpleWorld.SubsystemTickPublish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSubsystemTickPublishTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	bOk &= RunSubsystemTickPublishCase(*this, TEXT("Stable.PublishEveryFrame"), 1, false);
	bOk &= RunSubsystemTickPublishCase(*this, TEXT("Stable.PublishEvery12"), 12, false);
	bOk &= RunSubsystemTickPublishCase(*this, TEXT("Changing.PublishEveryFrame"), 1, true);
	bOk &= RunSubsystemTickPublishCase(*this, TEXT("Changing.PublishEvery12"), 12, true);
	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
