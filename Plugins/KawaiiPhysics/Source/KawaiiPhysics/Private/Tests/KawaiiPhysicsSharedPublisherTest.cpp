// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "KawaiiPhysicsSharedTags.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "KawaiiPhysicsWindPresetTags.h"
#include "AnimNode_KawaiiPhysicsSharedPublisherInternal.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

namespace
{
	constexpr float GSharedPublisherPresetTol = KINDA_SMALL_NUMBER;

	bool TestSharedPublisherFloatNear(FAutomationTestBase& Test, const FString& Name, const float Actual,
	                                  const float Expected)
	{
		return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), *Name, Actual, Expected),
		                     FMath::IsNearlyEqual(Actual, Expected, GSharedPublisherPresetTol));
	}

	bool TestSharedPublisherIntervalNear(FAutomationTestBase& Test, const FString& Name,
	                                     const FFloatInterval& Actual,
	                                     const FFloatInterval& Expected)
	{
		bool bResult = true;
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s Min"), *Name), Actual.Min,
		                                        Expected.Min);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s Max"), *Name), Actual.Max,
		                                        Expected.Max);
		return bResult;
	}

	bool TestSharedPublisherWindMatchesPreset(FAutomationTestBase& Test, const FString& Prefix,
	                                          const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
	                                          const FKawaiiProceduralWindPreset& Preset)
	{
		bool bResult = true;
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s ConstantForce"), *Prefix),
		                                        Wind.ConstantForce, Preset.ConstantForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s SwayForce"), *Prefix),
		                                        Wind.SwayForce, Preset.SwayForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s SwayPeriod"), *Prefix),
		                                        Wind.SwayPeriod, Preset.SwayPeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RippleForce"), *Prefix),
		                                        Wind.RippleForce, Preset.RippleForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RipplePeriod"), *Prefix),
		                                        Wind.RipplePeriod, Preset.RipplePeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RippleTipPhaseDelay"), *Prefix),
		                                        Wind.RippleTipPhaseDelay, Preset.RippleTipPhaseDelay);
		bResult &= TestSharedPublisherIntervalNear(Test, FString::Printf(TEXT("%s StrengthCycleRange"), *Prefix),
		                                           Wind.StrengthCycleRange, Preset.StrengthCycleRange);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s StrengthCyclePeriod"), *Prefix),
		                                        Wind.StrengthCyclePeriod, Preset.StrengthCyclePeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RandomForce"), *Prefix),
		                                        Wind.RandomForce, Preset.RandomForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RandomForcePeriod"), *Prefix),
		                                        Wind.RandomForcePeriod, Preset.RandomForcePeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s WindDirectionNoiseAngle"), *Prefix),
		                                        Wind.WindDirectionNoiseAngle, Preset.WindDirectionNoiseAngle);
		return bResult;
	}

	// プリセットを外したあとに authored 値へ戻ったかの確認用。プリセットが上書きする 11 項目に加えて、
	// プリセットが強制する bIsEnabled / TimeScale と、プリセットが持たない WindDirection も比較する
	bool TestSharedPublisherWindMatchesAuthored(FAutomationTestBase& Test, const FString& Prefix,
	                                            const FKawaiiPhysics_ExternalForce_ProceduralWind& Actual,
	                                            const FKawaiiPhysics_ExternalForce_ProceduralWind& Authored)
	{
		bool bResult = true;
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s ConstantForce"), *Prefix),
		                                        Actual.ConstantForce, Authored.ConstantForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s SwayForce"), *Prefix),
		                                        Actual.SwayForce, Authored.SwayForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s SwayPeriod"), *Prefix),
		                                        Actual.SwayPeriod, Authored.SwayPeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RippleForce"), *Prefix),
		                                        Actual.RippleForce, Authored.RippleForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RipplePeriod"), *Prefix),
		                                        Actual.RipplePeriod, Authored.RipplePeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RippleTipPhaseDelay"), *Prefix),
		                                        Actual.RippleTipPhaseDelay, Authored.RippleTipPhaseDelay);
		bResult &= TestSharedPublisherIntervalNear(Test, FString::Printf(TEXT("%s StrengthCycleRange"), *Prefix),
		                                           Actual.StrengthCycleRange, Authored.StrengthCycleRange);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s StrengthCyclePeriod"), *Prefix),
		                                        Actual.StrengthCyclePeriod, Authored.StrengthCyclePeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RandomForce"), *Prefix),
		                                        Actual.RandomForce, Authored.RandomForce);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s RandomForcePeriod"), *Prefix),
		                                        Actual.RandomForcePeriod, Authored.RandomForcePeriod);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s WindDirectionNoiseAngle"), *Prefix),
		                                        Actual.WindDirectionNoiseAngle, Authored.WindDirectionNoiseAngle);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s WindDirection"), *Prefix),
		                          Actual.WindDirection, Authored.WindDirection);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s bIsEnabled"), *Prefix),
		                          Actual.bIsEnabled, Authored.bIsEnabled);
		bResult &= TestSharedPublisherFloatNear(Test, FString::Printf(TEXT("%s TimeScale"), *Prefix),
		                                        Actual.TimeScale, Authored.TimeScale);
		return bResult;
	}

	bool TestKawaiiPhysicsSharedPublisherWind(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		const FKawaiiPhysicsSharedWindState& Actual,
		const FKawaiiPhysicsSharedWindState& Expected)
	{
		bool bResult = true;
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s bPublisherWindEnabled"), Context),
			Actual.bPublisherWindEnabled, Expected.bPublisherWindEnabled);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s Time"), Context), Actual.Time, Expected.Time);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s PublisherTimeScale"), Context),
			Actual.PublisherTimeScale, Expected.PublisherTimeScale);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s ConstantForce"), Context),
			Actual.Params.ConstantForce, Expected.Params.ConstantForce);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s bOverrideConstantForce"), Context),
			Actual.Params.bOverrideConstantForce, Expected.Params.bOverrideConstantForce);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s ActiveGust bIsActive"), Context),
			Actual.ActiveGust.bIsActive, Expected.ActiveGust.bIsActive);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s ActiveGust Strength"), Context),
			Actual.ActiveGust.Strength, Expected.ActiveGust.Strength);
		return bResult;
	}

	// 消費側 ProceduralWind の PreApply を 1 フレーム分だけ回す
	//（KawaiiPhysicsProceduralWindTest.cpp の RunProceduralWindPreApply と同等。あちらは無名 namespace なので共有できない）
	void RunSharedPublisherConsumerPreApply(FKawaiiPhysicsTestAccessor& Accessor,
	                                        FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
	                                        const float Dt)
	{
		Accessor.SetTimeState(Dt, Dt);
		FAnimInstanceProxy AnimInstanceProxy;
		FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);
		Wind.PreApply(Accessor.Node, PoseContext);
	}

	int32 CountSharedPublisherWindOverrideFlags(const FKawaiiProceduralWindDynamicParams& Params)
	{
		return
			(Params.bOverrideWindDirection ? 1 : 0) +
			(Params.bOverrideWindDirectionNoiseAngle ? 1 : 0) +
			(Params.bOverrideWindDirectionNoisePeriod ? 1 : 0) +
			(Params.bOverrideConstantForce ? 1 : 0) +
			(Params.bOverrideSwayForce ? 1 : 0) +
			(Params.bOverrideSwayPeriod ? 1 : 0) +
			(Params.bOverrideRippleForce ? 1 : 0) +
			(Params.bOverrideRipplePeriod ? 1 : 0) +
			(Params.bOverrideRippleTipPhaseDelay ? 1 : 0) +
			(Params.bOverrideStrengthCycleRange ? 1 : 0) +
			(Params.bOverrideStrengthCyclePeriod ? 1 : 0) +
			(Params.bOverrideRandomForce ? 1 : 0) +
			(Params.bOverrideRandomForcePeriod ? 1 : 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionSubsystemSupportsEditorPreviewTest,
                                 "KawaiiPhysics.SharedCollision.SubsystemSupportsEditorPreview",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionSubsystemSupportsEditorPreviewTest::RunTest(const FString& Parameters)
{
	const UKawaiiPhysicsSharedCollisionSubsystem* CDO = GetDefault<UKawaiiPhysicsSharedCollisionSubsystem>();
	UWorld* World = NewObject<UWorld>(GetTransientPackage(), NAME_None, RF_Transient);

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.EnableInPreviewWorld"));
	if (!TestNotNull(TEXT("EnableInPreviewWorld CVar exists"), CVar))
	{
		return false;
	}

	const int32 Saved = CVar->GetInt();
	ON_SCOPE_EXIT
	{
		CVar->Set(Saved, ECVF_SetByCode);
	};

	CVar->Set(1, ECVF_SetByCode);

	World->WorldType = EWorldType::EditorPreview;
	TestTrue(TEXT("EditorPreview world creates subsystem by default"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Game;
	TestTrue(TEXT("Game world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Editor;
	TestTrue(TEXT("Editor world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::PIE;
	TestTrue(TEXT("PIE world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::GamePreview;
	TestFalse(TEXT("GamePreview world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Inactive;
	TestFalse(TEXT("Inactive world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::None;
	TestFalse(TEXT("None world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	CVar->Set(0, ECVF_SetByCode);

	World->WorldType = EWorldType::EditorPreview;
	TestFalse(TEXT("EditorPreview world does not create subsystem when disabled by CVar"),
	          CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Game;
	TestTrue(TEXT("Game world still creates subsystem when preview CVar is disabled"),
	         CDO->ShouldCreateSubsystem(World));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherEntryPublishReadTest,
                                 "KawaiiPhysics.SharedPublisher.EntryPublishRead",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherEntryPublishReadTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;

	// provider ID 0 は未所有の番兵と衝突するため拒否される。
	{
		FKawaiiPhysicsSharedPublisherState ZeroState;
		const uint64 SerialBeforeZero = Entry.GetPublishSerial();
		TestFalse(TEXT("Zero provider ID is rejected"), Entry.PublishState(ZeroState, 0, 1, 10));
		TestEqual(TEXT("Zero provider ID keeps serial"), Entry.GetPublishSerial(), SerialBeforeZero);
		TestEqual(TEXT("Zero provider ID leaves entry unowned"), Entry.GetProviderID(), static_cast<uint64>(0));
	}

	FKawaiiPhysicsSharedPublisherState State;
	State.bSimpleWorldEnabled = true;
	State.GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;
	State.SimpleWorldDesc.GatherIntervalSec = 0.05f;
	State.SimpleWorldDesc.bGatherFamilyMembers = true;
	State.Wind.bPublisherWindEnabled = true;
	State.Wind.Time = 12.5f;
	State.Wind.PublisherTimeScale = 0.5f;
	State.Wind.Params.bOverrideConstantForce = true;
	State.Wind.Params.ConstantForce = 3.0f;
	State.Wind.ActiveGust.bIsActive = true;
	State.Wind.ActiveGust.Strength = 7.0f;

	TestTrue(TEXT("Initial provider publishes"), Entry.PublishState(State, 11, 100, 10));
	TestEqual(TEXT("Publish serial after first publish"), Entry.GetPublishSerial(), static_cast<uint64>(1));
	TestEqual(TEXT("Provider ID after first publish"), Entry.GetProviderID(), static_cast<uint64>(11));
	TestEqual(TEXT("Last publish frame after first publish"), Entry.GetLastPublishFrame(), static_cast<uint64>(100));

	FKawaiiPhysicsSharedPublisherState ReadState;
	const uint64 ReadSerial = Entry.ReadState(ReadState);
	TestEqual(TEXT("ReadState returns current serial"), ReadSerial, static_cast<uint64>(1));
	TestTrue(TEXT("ReadState keeps SimpleWorld enabled"), ReadState.bSimpleWorldEnabled);
	TestTrue(TEXT("ReadState keeps gather scope"),
		ReadState.GatherScope == EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily);
	TestEqual(TEXT("ReadState keeps gather interval"), ReadState.SimpleWorldDesc.GatherIntervalSec, 0.05f);
	TestTrue(TEXT("ReadState keeps family members flag"), ReadState.SimpleWorldDesc.bGatherFamilyMembers);
	TestKawaiiPhysicsSharedPublisherWind(*this, TEXT("ReadState wind"), ReadState.Wind, State.Wind);

	FKawaiiPhysicsSharedPublisherState BlockedState = State;
	BlockedState.bSimpleWorldEnabled = false;
	BlockedState.SimpleWorldDesc.GatherIntervalSec = 0.75f;
	BlockedState.Wind.Time = 20.0f;
	TestFalse(TEXT("Live different provider is rejected"), Entry.PublishState(BlockedState, 22, 105, 10));

	FKawaiiPhysicsSharedPublisherState AfterRejected;
	Entry.ReadState(AfterRejected);
	TestTrue(TEXT("Rejected publish keeps SimpleWorld enabled"), AfterRejected.bSimpleWorldEnabled);
	TestEqual(TEXT("Rejected publish keeps gather interval"), AfterRejected.SimpleWorldDesc.GatherIntervalSec, 0.05f);
	TestEqual(TEXT("Rejected publish keeps wind time"), AfterRejected.Wind.Time, 12.5f);
	TestEqual(TEXT("Rejected publish keeps serial"), Entry.GetPublishSerial(), static_cast<uint64>(1));

	TestTrue(TEXT("Expired previous provider allows replacement"), Entry.PublishState(BlockedState, 22, 111, 10));
	TestEqual(TEXT("Publish serial after provider replacement"), Entry.GetPublishSerial(), static_cast<uint64>(2));
	TestEqual(TEXT("Provider ID after provider replacement"), Entry.GetProviderID(), static_cast<uint64>(22));
	TestEqual(TEXT("Last publish frame after provider replacement"), Entry.GetLastPublishFrame(), static_cast<uint64>(111));

	FKawaiiPhysicsSharedWindState ReadWind;
	const uint64 WindSerial = Entry.ReadWindState(ReadWind);
	TestEqual(TEXT("ReadWindState returns current serial"), WindSerial, static_cast<uint64>(2));
	TestKawaiiPhysicsSharedPublisherWind(*this, TEXT("ReadWindState"), ReadWind, BlockedState.Wind);

	TestFalse(TEXT("Entry is not expired within max age"), Entry.IsExpired(120, 10));
	TestTrue(TEXT("Entry is expired beyond max age"), Entry.IsExpired(122, 10));
	Entry.MarkExpired();
	TestTrue(TEXT("MarkExpired makes entry expired"), Entry.IsExpired(111, 10));

	// 期限切れ Entry は同じ provider が publish しても復活しない。
	const uint64 ExpiredSerial = Entry.GetPublishSerial();
	FKawaiiPhysicsSharedPublisherState RevivedState = BlockedState;
	RevivedState.Wind.Time = 33.0f;
	TestFalse(TEXT("Expired entry rejects publish"), Entry.PublishState(RevivedState, 22, 200, 10));
	TestEqual(TEXT("Expired entry keeps serial"), Entry.GetPublishSerial(), ExpiredSerial);
	TestTrue(TEXT("Expired entry stays expired"), Entry.IsExpired(200, 10));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherExpiredEntryReplacedOnAcquireTest,
                                 "KawaiiPhysics.SharedPublisher.ExpiredEntryReplacedOnAcquire",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherExpiredEntryReplacedOnAcquireTest::RunTest(const FString& Parameters)
{
	// FindOrCreateSharedPublisherEntry は GetFamilyRoot のアタッチ階層参照しか行わないので、World 無しの
	// Subsystem / Actor で足りる。GC に回収されないよう TStrongObjectPtr で保持する。
	// Tick は Initialize を呼ばない限り登録されない（UTickableWorldSubsystem は ETickableTickType::Never で構築される）。
	const TStrongObjectPtr<UKawaiiPhysicsSharedCollisionSubsystem> Subsystem(
		NewObject<UKawaiiPhysicsSharedCollisionSubsystem>(GetTransientPackage(), NAME_None, RF_Transient));
	const TStrongObjectPtr<AActor> Actor(NewObject<AActor>(GetTransientPackage(), NAME_None, RF_Transient));
	const FGameplayTag Tag = TAG_KawaiiPhysics_Shared_Default;

	const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry1 =
		Subsystem->FindOrCreateSharedPublisherEntry(Actor.Get(), Tag);
	if (!TestTrue(TEXT("First acquire creates an entry"), Entry1.IsValid()))
	{
		return false;
	}

	FKawaiiPhysicsSharedPublisherState State;
	State.bPublisherEnabled = true;
	State.bSimpleWorldEnabled = true;
	TestTrue(TEXT("Provider A publishes on the first entry"), Entry1->PublishState(State, 0xA001, 1, 60));

	// provider A の ReleaseEntries 相当（Entry は Tick の Cleanup まで Registry に残る）
	Entry1->MarkExpired();
	TestTrue(TEXT("Released entry is marked expired"), Entry1->IsMarkedExpired());

	const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry2 =
		Subsystem->FindOrCreateSharedPublisherEntry(Actor.Get(), Tag);
	if (!TestTrue(TEXT("Second acquire returns an entry"), Entry2.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("Marked expired entry is replaced on acquire"), Entry2 != Entry1);
	TestFalse(TEXT("Replacement entry is not marked expired"), Entry2->IsMarkedExpired());
	TestTrue(TEXT("Old entry stays marked expired"), Entry1->IsMarkedExpired());
	TestEqual(TEXT("Replacement entry starts unowned"), Entry2->GetProviderID(), static_cast<uint64>(0));
	TestTrue(TEXT("Provider B publishes on the replacement entry"),
		Entry2->PublishState(State, 0xA002, 2, 60));
	TestEqual(TEXT("Replacement entry is owned by Provider B"),
		Entry2->GetProviderID(), static_cast<uint64>(0xA002));

	// 旧 Entry は publish を拒否し続け、掴んだままの provider は再取得へ回る
	TestFalse(TEXT("Old entry keeps rejecting publishes"), Entry1->PublishState(State, 0xA001, 3, 60));

	TestTrue(TEXT("Lookup returns the replacement entry"),
		Subsystem->FindSharedPublisherEntry(Actor.Get(), Tag) == Entry2);
	TestTrue(TEXT("Live entry is reused on the next acquire"),
		Subsystem->FindOrCreateSharedPublisherEntry(Actor.Get(), Tag) == Entry2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherGustQueueTest,
                                 "KawaiiPhysics.SharedPublisher.GustQueue",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherGustQueueTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;
	Entry.RequestGust(1.0f, 0.1f, 0.2f, 0.3f);
	Entry.RequestGust(2.0f, 0.4f, 0.5f, 0.6f);
	Entry.RequestGustStop(0.7f);

	TArray<FKawaiiPhysicsSharedPublisherGustRequest> Requests;
	Entry.ConsumePendingGustRequests(Requests);
	TestEqual(TEXT("Consumes all gust requests"), Requests.Num(), 3);

	if (Requests.Num() == 3)
	{
		TestFalse(TEXT("First request starts gust"), Requests[0].bStop);
		TestEqual(TEXT("First request strength"), Requests[0].Strength, 1.0f);
		TestEqual(TEXT("First request rise"), Requests[0].RiseTime, 0.1f);
		TestEqual(TEXT("First request decay"), Requests[0].DecayTime, 0.2f);
		TestEqual(TEXT("First request hold"), Requests[0].HoldTime, 0.3f);

		TestFalse(TEXT("Second request starts gust"), Requests[1].bStop);
		TestEqual(TEXT("Second request strength"), Requests[1].Strength, 2.0f);
		TestEqual(TEXT("Second request rise"), Requests[1].RiseTime, 0.4f);
		TestEqual(TEXT("Second request decay"), Requests[1].DecayTime, 0.5f);
		TestEqual(TEXT("Second request hold"), Requests[1].HoldTime, 0.6f);

		TestTrue(TEXT("Third request stops gust"), Requests[2].bStop);
		TestEqual(TEXT("Third request blend out"), Requests[2].BlendOutTime, 0.7f);
	}

	Requests.Reset();
	Entry.ConsumePendingGustRequests(Requests);
	TestEqual(TEXT("Second consume is empty"), Requests.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherPublisherRequestQueueTest,
                                 "KawaiiPhysics.SharedPublisher.PublisherRequestQueue",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherPublisherRequestQueueTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;

	FKawaiiPhysicsSimpleWorldCollisionSettings Settings;
	Settings.GatherInterval = 0.4f;
	Settings.bGatherFamilyMembers = true;
	Entry.RequestPublisherEnabled(false);
	Entry.RequestSimpleWorldSettings(Settings);

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests Requests;
	TestTrue(TEXT("Consumes pending publisher requests"), Entry.ConsumePendingPublisherRequests(Requests));
	TestTrue(TEXT("Enabled request is set"), Requests.Enabled.IsSet());
	if (Requests.Enabled.IsSet())
	{
		TestFalse(TEXT("Enabled request value is false"), Requests.Enabled.GetValue());
	}
	TestTrue(TEXT("SimpleWorld settings request is set"), Requests.SimpleWorldSettings.IsSet());
	if (Requests.SimpleWorldSettings.IsSet())
	{
		TestEqual(TEXT("SimpleWorld settings gather interval"),
			Requests.SimpleWorldSettings.GetValue().GatherInterval, 0.4f);
		TestTrue(TEXT("SimpleWorld settings family members flag"),
			Requests.SimpleWorldSettings.GetValue().bGatherFamilyMembers);
	}

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests EmptyRequests;
	TestFalse(TEXT("Second publisher request consume is empty"),
		Entry.ConsumePendingPublisherRequests(EmptyRequests));

	FKawaiiPhysicsSimpleWorldCollisionSettings DefaultSettings;
	const FKawaiiPhysicsSimpleWorldCollisionDesc DefaultDesc =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(DefaultSettings);
	TestTrue(TEXT("Default settings use ActorFamily gather scope"),
		DefaultDesc.GatherScope == EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily);
	TestFalse(TEXT("Default settings keep provider enabled"), DefaultDesc.bProviderDisabled);
	TestTrue(TEXT("Default settings keep collision channel unspecified"), DefaultDesc.CollisionChannel == ECC_MAX);
	TestFalse(TEXT("Default settings do not gather family members"), DefaultDesc.bGatherFamilyMembers);

	DefaultSettings.bEnabled = false;
	const FKawaiiPhysicsSimpleWorldCollisionDesc DisabledDesc =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(DefaultSettings);
	TestTrue(TEXT("Disabled settings disable provider"), DisabledDesc.bProviderDisabled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherWindParamsRequestsMergeTest,
                                 "KawaiiPhysics.SharedPublisher.WindParamsRequestsMerge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherWindParamsRequestsMergeTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;

	// consume前に別項目の要求が複数回届いても、単純代入では先の要求が消える（項目単位でマージされるべき）
	FKawaiiProceduralWindDynamicParams ConstantForceParams;
	ConstantForceParams.bOverrideConstantForce = true;
	ConstantForceParams.ConstantForce = 10.0f;
	Entry.RequestWindParams(ConstantForceParams);

	FKawaiiProceduralWindDynamicParams SwayForceParams;
	SwayForceParams.bOverrideSwayForce = true;
	SwayForceParams.SwayForce = 5.0f;
	Entry.RequestWindParams(SwayForceParams);

	FKawaiiProceduralWindDynamicParams OverwriteConstantForceParams;
	OverwriteConstantForceParams.bOverrideConstantForce = true;
	OverwriteConstantForceParams.ConstantForce = 20.0f;
	Entry.RequestWindParams(OverwriteConstantForceParams);

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests Requests;
	bool bOk = TestTrue(TEXT("Consumes pending wind params request"), Entry.ConsumePendingPublisherRequests(Requests));
	bOk &= TestTrue(TEXT("Wind params request is set"), Requests.WindParams.IsSet());
	if (Requests.WindParams.IsSet())
	{
		const FKawaiiProceduralWindDynamicParams& Out = Requests.WindParams.GetValue();
		bOk &= TestTrue(TEXT("ConstantForce override kept"), Out.bOverrideConstantForce);
		bOk &= TestSharedPublisherFloatNear(*this, TEXT("Later ConstantForce request wins"), Out.ConstantForce, 20.0f);
		bOk &= TestTrue(TEXT("SwayForce override kept"), Out.bOverrideSwayForce);
		bOk &= TestSharedPublisherFloatNear(*this, TEXT("Earlier SwayForce request is preserved"), Out.SwayForce, 5.0f);
		bOk &= TestFalse(TEXT("Untouched field stays unset"), Out.bOverrideRippleForce);
	}

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests EmptyRequests;
	bOk &= TestFalse(TEXT("Second wind params consume is empty"),
		Entry.ConsumePendingPublisherRequests(EmptyRequests));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherWindPresetAppliedTest,
                                 "KawaiiPhysics.SharedPublisher.WindPresetApplied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherWindPresetAppliedTest::RunTest(const FString& Parameters)
{
	const TArray<FKawaiiProceduralWindPreset> Defaults = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	bool bOk = TestTrue(TEXT("Default presets contain Strong"), Defaults.Num() > 1);
	if (!bOk)
	{
		return false;
	}

	const FKawaiiProceduralWindPreset Preset = Defaults[1];
	UKawaiiPhysicsWindPresetDataAsset* Asset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	Asset->Presets.Add(Preset);

	FAnimNode_KawaiiPhysicsSharedPublisher Node;
	Node.WindPresetDataAsset = Asset;
	Node.WindPresetTag = Preset.PresetTag;
	Node.SharedWind.bIsEnabled = false;
	Node.SharedWind.TimeScale = 2.0f;
	Node.SharedWind.ConstantForce = 77.0f;
	Node.SharedWind.ResetRuntimeState();
	Node.SharedWind.RuntimeState->Time = 3.0f;

	// プリセットを外す（アセット null／Tag が引けない）と、最初の適用前の authored 値へ書き戻る契約
	Node.ApplySharedWindPreset();

	bOk &= TestSharedPublisherWindMatchesPreset(*this, TEXT("Applied preset"), Node.SharedWind, Preset);
	bOk &= TestTrue(TEXT("Preset enables SharedWind"), Node.SharedWind.bIsEnabled);
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Preset resets TimeScale"), Node.SharedWind.TimeScale, 1.0f);
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Runtime time preserved"), Node.SharedWind.RuntimeState->Time, 3.0f);

	Node.WindPresetDataAsset = nullptr;
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Null asset restores authored value"),
	                                    Node.SharedWind.ConstantForce, 77.0f);
	bOk &= TestFalse(TEXT("Null asset restores authored Enabled"), Node.SharedWind.bIsEnabled);
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Null asset restores authored TimeScale"),
	                                    Node.SharedWind.TimeScale, 2.0f);
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Null asset keeps runtime time"),
	                                    Node.SharedWind.RuntimeState->Time, 3.0f);

	Node.WindPresetDataAsset = Asset;
	Node.WindPresetTag = FGameplayTag();
	AddExpectedError(TEXT("failed to apply Wind Preset Tag"), EAutomationExpectedErrorFlags::Contains, 1);
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Invalid tag keeps authored value"),
	                                    Node.SharedWind.ConstantForce, 77.0f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherWindPresetClearedRestoresAuthoredTest,
                                 "KawaiiPhysics.SharedPublisher.WindPresetClearedRestoresAuthored",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherWindPresetClearedRestoresAuthoredTest::RunTest(const FString& Parameters)
{
	const TArray<FKawaiiProceduralWindPreset> Defaults = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	bool bOk = TestTrue(TEXT("Default presets contain at least three entries"), Defaults.Num() > 2);
	if (!bOk)
	{
		return false;
	}

	// DataAsset にはプリセット A / B の 2 件だけを入れる。3 件目の Tag は「有効だが引けない Tag」として使う
	//（プリセットを持つ DataAsset は組み込み既定へフォールバックしないため）
	const FKawaiiProceduralWindPreset PresetA = Defaults[0];
	const FKawaiiProceduralWindPreset PresetB = Defaults[1];
	const FGameplayTag MissingPresetTag = Defaults[2].PresetTag;
	UKawaiiPhysicsWindPresetDataAsset* Asset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	Asset->Presets.Add(PresetA);
	Asset->Presets.Add(PresetB);
	bOk &= TestTrue(TEXT("Missing preset tag is valid but absent from the asset"),
	                MissingPresetTag.IsValid() && Asset->FindPresetByTag(MissingPresetTag) == nullptr);

	// 1. authored 値を既定値と違う値にしておき、戻ってきたことを判別できるようにする
	FAnimNode_KawaiiPhysicsSharedPublisher Node;
	Node.SharedWind.ResetRuntimeState();
	Node.SharedWind.ConstantForce = 77.0f;
	Node.SharedWind.SwayForce = 3.0f;
	Node.SharedWind.WindDirection = FVector(0.0f, 1.0f, 0.0f);
	Node.SharedWind.bIsEnabled = false;
	Node.SharedWind.TimeScale = 2.0f;
	Node.SharedWind.RuntimeState->Time = 5.0f;
	const FKawaiiPhysics_ExternalForce_ProceduralWind AuthoredWind = Node.SharedWind;

	// 2. 有効なプリセットを適用すると 11 項目が上書きされ、bIsEnabled / TimeScale は強制される。位相は保つ
	Node.WindPresetDataAsset = Asset;
	Node.WindPresetTag = PresetB.PresetTag;
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherWindMatchesPreset(*this, TEXT("Applied preset"), Node.SharedWind, PresetB);
	bOk &= TestTrue(TEXT("Applied preset enables SharedWind"), Node.SharedWind.bIsEnabled);
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Applied preset resets TimeScale"),
	                                    Node.SharedWind.TimeScale, 1.0f);
	bOk &= TestTrue(TEXT("Applied preset keeps the authored snapshot"), Node.HasSharedWindBeforePreset());
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Applied preset keeps runtime time"),
	                                    Node.SharedWind.RuntimeState->Time, 5.0f);

	// 3. DataAsset を None にすると authored 値へ戻り、退避も解放される
	Node.WindPresetDataAsset = nullptr;
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherWindMatchesAuthored(*this, TEXT("Cleared asset"), Node.SharedWind, AuthoredWind);
	bOk &= TestFalse(TEXT("Cleared asset releases the authored snapshot"), Node.HasSharedWindBeforePreset());
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Cleared asset keeps runtime time"),
	                                    Node.SharedWind.RuntimeState->Time, 5.0f);

	// 4. 引けない Tag に変わった場合も authored 値へ戻る（once 警告つき）
	Node.WindPresetDataAsset = Asset;
	Node.WindPresetTag = PresetB.PresetTag;
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Re-applied preset ConstantForce"),
	                                    Node.SharedWind.ConstantForce, PresetB.ConstantForce);
	bOk &= TestTrue(TEXT("Re-applied preset takes a new snapshot"), Node.HasSharedWindBeforePreset());

	Node.WindPresetTag = MissingPresetTag;
	AddExpectedError(TEXT("failed to apply Wind Preset Tag"), EAutomationExpectedErrorFlags::Contains, 1);
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherWindMatchesAuthored(*this, TEXT("Unresolvable tag"), Node.SharedWind, AuthoredWind);
	bOk &= TestFalse(TEXT("Unresolvable tag releases the authored snapshot"), Node.HasSharedWindBeforePreset());
	bOk &= TestSharedPublisherFloatNear(*this, TEXT("Unresolvable tag keeps runtime time"),
	                                    Node.SharedWind.RuntimeState->Time, 5.0f);

	// 5. 対照: プリセットを一度も適用していないノードは、None のまま呼んでも値を書き換えない
	{
		FAnimNode_KawaiiPhysicsSharedPublisher ControlNode;
		ControlNode.SharedWind.ConstantForce = 123.0f;
		ControlNode.ApplySharedWindPreset();
		bOk &= TestSharedPublisherFloatNear(*this, TEXT("Control node keeps its value"),
		                                    ControlNode.SharedWind.ConstantForce, 123.0f);
		bOk &= TestFalse(TEXT("Control node takes no snapshot"), ControlNode.HasSharedWindBeforePreset());
	}

	// 6. プリセット A → B と切り替えてから None にすると、戻るのは B 適用前ではなく A 適用前の authored 値
	Node.WindPresetDataAsset = Asset;
	Node.WindPresetTag = PresetA.PresetTag;
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherWindMatchesPreset(*this, TEXT("Preset A"), Node.SharedWind, PresetA);

	Node.WindPresetTag = PresetB.PresetTag;
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherWindMatchesPreset(*this, TEXT("Preset B"), Node.SharedWind, PresetB);
	bOk &= TestTrue(TEXT("Preset switch keeps the first snapshot"), Node.HasSharedWindBeforePreset());

	Node.WindPresetDataAsset = nullptr;
	Node.ApplySharedWindPreset();
	bOk &= TestSharedPublisherWindMatchesAuthored(*this, TEXT("After preset switch"), Node.SharedWind, AuthoredWind);
	bOk &= TestFalse(TEXT("Preset switch snapshot is released once"), Node.HasSharedWindBeforePreset());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLibrarySharedPublisherWindApiTest,
                                 "KawaiiPhysics.Library.SharedPublisherWindApi",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLibrarySharedPublisherWindApiTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<AActor> Actor(NewObject<AActor>(GetTransientPackage(), NAME_None, RF_Transient));
	const FGameplayTag Tag = TAG_KawaiiPhysics_Shared_Default;

	// World 無し Actor では Subsystem が解決できないため、公開 API は false を返す。
	bool bOk = true;
	bOk &= TestFalse(TEXT("Start gust without subsystem returns false"),
	                 UKawaiiPhysicsLibrary::StartProceduralWindGustOnSharedPublisher(Actor.Get(), Tag));
	bOk &= TestFalse(TEXT("Stop gust without subsystem returns false"),
	                 UKawaiiPhysicsLibrary::StopProceduralWindGustOnSharedPublisher(Actor.Get(), Tag));

	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideConstantForce = true;
	Params.ConstantForce = 31.0f;
	bOk &= TestFalse(TEXT("Set wind params without subsystem returns false"),
	                 UKawaiiPhysicsLibrary::SetProceduralWindParametersOnSharedPublisher(Actor.Get(), Tag, Params));

	// Entry 経由の公開 API 受理は Subsystem 所有の live Entry が要るため、ここでは Pending キュー単体を確認する。
	FKawaiiPhysicsSharedPublisherEntry Entry;
	// 未 claim（ProviderID 0）の Entry は、起動直後で期限切れに見えなくても live 扱いにしない
	// （ResolveLiveSharedPublisherEntry と同じ判定ヘルパで確認する）
	bOk &= TestFalse(TEXT("Unclaimed entry is not live"),
	                 KawaiiPhysicsProceduralWindInternal::IsSharedPublisherEntryLive(Entry, 5, 60));
	Entry.RequestWindParams(Params);

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests Requests;
	bOk &= TestTrue(TEXT("Wind params pending request consumed"), Entry.ConsumePendingPublisherRequests(Requests));
	bOk &= TestTrue(TEXT("Wind params request is set"), Requests.WindParams.IsSet());
	if (Requests.WindParams.IsSet())
	{
		bOk &= TestTrue(TEXT("Wind params override preserved"),
		                Requests.WindParams.GetValue().bOverrideConstantForce);
		bOk &= TestSharedPublisherFloatNear(*this, TEXT("Wind params value preserved"),
		                                    Requests.WindParams.GetValue().ConstantForce, 31.0f);
	}

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests EmptyRequests;
	bOk &= TestFalse(TEXT("Wind params pending request consumed once"),
	                 Entry.ConsumePendingPublisherRequests(EmptyRequests));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherDetachedSimpleWorldEntryReboundTest,
                                 "KawaiiPhysics.SharedPublisher.DetachedSimpleWorldEntryRebound",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherDetachedSimpleWorldEntryReboundTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xA001;
	const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> OldEntry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> WindState =
		MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();
	FKawaiiPhysicsSharedPublishInputs Inputs;
	FKawaiiPhysicsSharedPublishHelper Helper;
	Helper.SetSourceID(SourceID);
	Helper.SetEntries(PublisherEntry, OldEntry, SkelComp);
	Helper.ResetEffectiveValues(Inputs);
	for (uint64 Frame = 1; Frame <= 3; ++Frame)
	{
		TestTrue(TEXT("Initial updates publish"), Helper.Update(Inputs, WindState, 0.1f, Frame, 60));
	}
	TestTrue(TEXT("Initial provider desc exists"), OldEntry->HasAnyDesc());
	TestEqual(TEXT("Initial desc is sent once"), Helper.GetNumSetDescCalls(), 1);
	PublisherEntry->RequestPublisherEnabled(false);
	FKawaiiPhysicsSimpleWorldCollisionSettings EffectiveSettings = Inputs.SimpleWorld;
	EffectiveSettings.GatherInterval = Inputs.SimpleWorld.GatherInterval + 0.25f;
	PublisherEntry->RequestSimpleWorldSettings(EffectiveSettings);
	TestTrue(TEXT("Blueprint override publishes"), Helper.Update(Inputs, WindState, 0.1f, 4, 60));
	TestFalse(TEXT("Blueprint override disables effective publisher"), Helper.IsEffectiveEnabled());

	OldEntry->RemoveDesc(SourceID);
	TestTrue(TEXT("Detached SimpleWorld entry retires"), OldEntry->MarkRetiredIfEmpty());
	const float PendingBeforeRetiredUpdate = Helper.GetPendingDeltaTime();
	const int32 NumSetDescBeforeRetiredUpdate = Helper.GetNumSetDescCalls();
	TestTrue(TEXT("Retired SimpleWorld entry does not reject publisher state"), Helper.Update(Inputs, WindState, 0.1f, 5, 60));
	TestTrue(TEXT("Only SimpleWorld entry needs rebinding"), Helper.NeedsSimpleWorldEntryReacquire());
	TestFalse(TEXT("Publisher entry does not need rebinding"), Helper.NeedsEntryReacquire());
	TestTrue(TEXT("Publisher entry pointer is preserved"), Helper.GetSharedPublisherEntry() == PublisherEntry);
	TestFalse(TEXT("Publisher entry remains alive"), PublisherEntry->IsExpired(5, 60));
	TestFalse(TEXT("Effective enabled override survives retirement"), Helper.IsEffectiveEnabled());
	TestEqual(TEXT("Retired update keeps pending time unchanged"), Helper.GetPendingDeltaTime(), PendingBeforeRetiredUpdate);
	TestEqual(TEXT("Rejected registration is not counted"), Helper.GetNumSetDescCalls(), NumSetDescBeforeRetiredUpdate);
	TestFalse(TEXT("Retired entry has no recreated provider"), OldEntry->HasAnyDesc());
	TestFalse(TEXT("Retired entry has no recreated reader"), OldEntry->HasAnyReader());

	Helper.AccumulatePendingDeltaTime(0.3f);
	const float PendingBeforeRebind = Helper.GetPendingDeltaTime();
	const uint64 SerialBeforeRebind = Helper.GetLastPublishSerial();
	const float PublishedTimeBeforeRebind = Helper.GetLastPublishedState().Wind.Time;
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> NewEntry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	Helper.SetSimpleWorldEntry(NewEntry);
	TestEqual(TEXT("Rebinding preserves pending time"), Helper.GetPendingDeltaTime(), PendingBeforeRebind);
	TestEqual(TEXT("Rebinding preserves publisher serial"), Helper.GetLastPublishSerial(), SerialBeforeRebind);
	TestEqual(TEXT("Rebinding preserves published wind time"), Helper.GetLastPublishedState().Wind.Time, PublishedTimeBeforeRebind);
	TestTrue(TEXT("Rebinding preserves publisher pointer"), Helper.GetSharedPublisherEntry() == PublisherEntry);
	TestFalse(TEXT("Rebinding does not expire publisher"), PublisherEntry->IsExpired(5, 60));
	TestFalse(TEXT("Rebinding preserves effective enabled"), Helper.IsEffectiveEnabled());
	TestTrue(TEXT("Replacement update publishes"), Helper.Update(Inputs, WindState, 0.1f, 6, 60));
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	TestTrue(TEXT("Replacement receives provider desc"), NewEntry->BuildMergedDesc(Desc));
	TestTrue(TEXT("Replacement desc reflects disabled override"), Desc.bProviderDisabled);
	TestEqual(TEXT("Replacement desc preserves effective settings"), Desc.GatherIntervalSec, EffectiveSettings.GatherInterval);
	TestFalse(TEXT("Replacement clears SimpleWorld rebind request"), Helper.NeedsSimpleWorldEntryReacquire());
	TestFalse(TEXT("Replacement does not request publisher rebind"), Helper.NeedsEntryReacquire());
	TestTrue(TEXT("Replacement desc preserves skeletal mesh component"), NewEntry->GetPrimarySkelComp() == SkelComp);

	// null の差し替えでも Publisher Entry は保持し、次の SimpleWorld 再取得を待つ。
	NewEntry->RemoveDesc(SourceID);
	TestTrue(TEXT("Replacement is detached before null retry"), NewEntry->MarkRetiredIfEmpty());
	Helper.SetSimpleWorldEntry(nullptr);
	TestTrue(TEXT("Null SimpleWorld entry needs rebinding"), Helper.NeedsSimpleWorldEntryReacquire());
	TestFalse(TEXT("Null SimpleWorld entry keeps publisher"), Helper.NeedsEntryReacquire());
	TestTrue(TEXT("Null SimpleWorld entry still permits publishing"), Helper.Update(Inputs, WindState, 0.1f, 7, 60));
	TestFalse(TEXT("Null update keeps publisher alive"), PublisherEntry->IsExpired(7, 60));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherPublishHelperUpdateTest,
                                 "KawaiiPhysics.SharedPublisher.PublishHelperUpdate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherPublishHelperUpdateTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xA001;
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> WindState =
		MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();

	FKawaiiPhysicsSharedPublishInputs Defaults;
	FKawaiiPhysicsSharedPublishHelper Helper;
	Helper.SetSourceID(SourceID);
	Helper.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	Helper.ResetEffectiveValues(Defaults);

	FKawaiiPhysicsSharedPublishInputs Inputs = Defaults;
	uint64 PreviousSerial = 0;
	for (uint64 Frame = 1; Frame <= 3; ++Frame)
	{
		TestTrue(FString::Printf(TEXT("Update frame %llu publishes"), static_cast<unsigned long long>(Frame)),
			Helper.Update(Inputs, WindState, 0.1f, Frame, 60));
		TestTrue(TEXT("Publish serial increases"), Helper.GetLastPublishSerial() > PreviousSerial);
		PreviousSerial = Helper.GetLastPublishSerial();
	}

	TestEqual(TEXT("Wind time accumulates at scale 1"), WindState->Time, 0.3f);
	FKawaiiPhysicsSharedPublisherState ReadState;
	PublisherEntry->ReadState(ReadState);
	TestEqual(TEXT("Published wind time matches runtime state"), ReadState.Wind.Time, 0.3f);
	TestTrue(TEXT("SimpleWorld is enabled"), ReadState.bSimpleWorldEnabled);
	TestEqual(TEXT("Published default settings gather interval"),
		ReadState.SimpleWorldSettings.GatherInterval, Defaults.SimpleWorld.GatherInterval);
	TestTrue(TEXT("SimpleWorld provider desc exists"), SimpleWorldEntry->HasProviderDesc());
	TestTrue(TEXT("Provider SkelComp is preserved"), SimpleWorldEntry->GetPrimarySkelComp() == SkelComp);
	TestEqual(TEXT("Provider heartbeat reaches frame 3"), SimpleWorldEntry->GetLastProviderFrame(), static_cast<uint64>(3));
	TestEqual(TEXT("SetDesc called only for first desc"), Helper.GetNumSetDescCalls(), 1);

	Inputs.WindTimeScale = 2.0f;
	TestTrue(TEXT("TimeScale 2 publishes"), Helper.Update(Inputs, WindState, 0.1f, 4, 60));
	TestEqual(TEXT("Wind time accumulates at scale 2"), WindState->Time, 0.5f);
	TestEqual(TEXT("SetDesc not called for wind-only change"), Helper.GetNumSetDescCalls(), 1);

	Inputs.SimpleWorld.GatherInterval = 0.5f;
	TestTrue(TEXT("Desc change publishes"), Helper.Update(Inputs, WindState, 0.1f, 5, 60));
	TestEqual(TEXT("SetDesc called for desc change"), Helper.GetNumSetDescCalls(), 2);
	FKawaiiPhysicsSimpleWorldCollisionDesc MergedDesc;
	TestTrue(TEXT("Merged desc exists after desc change"), SimpleWorldEntry->BuildMergedDesc(MergedDesc));
	TestEqual(TEXT("Merged desc follows gather interval"), MergedDesc.GatherIntervalSec, 0.5f);

	Inputs.bEnabled = false;
	const float TimeBeforeDisabled = WindState->Time;
	TestTrue(TEXT("Disabled state still publishes"), Helper.Update(Inputs, WindState, 0.1f, 6, 60));
	PublisherEntry->ReadState(ReadState);
	TestFalse(TEXT("Disabled publish disables SimpleWorld"), ReadState.bSimpleWorldEnabled);
	TestTrue(TEXT("Disabled publish marks provider disabled"), ReadState.SimpleWorldDesc.bProviderDisabled);
	TestFalse(TEXT("Disabled publish disables wind"), ReadState.Wind.bPublisherWindEnabled);
	TestFalse(TEXT("Disabled publish clears publisher enabled"), ReadState.bPublisherEnabled);
	TestEqual(TEXT("Disabled publish stops wind time"), WindState->Time, TimeBeforeDisabled);
	TestTrue(TEXT("SimpleWorld entry reports provider disabled"), SimpleWorldEntry->IsProviderDisabled());

	Inputs.bEnabled = true;
	TestTrue(TEXT("Re-enabled state publishes"), Helper.Update(Inputs, WindState, 0.1f, 7, 60));
	PublisherEntry->ReadState(ReadState);
	TestTrue(TEXT("Re-enabled publish enables SimpleWorld"), ReadState.bSimpleWorldEnabled);
	TestTrue(TEXT("Re-enabled publish restores publisher enabled"), ReadState.bPublisherEnabled);
	TestFalse(TEXT("Re-enabled publish clears provider disabled"), ReadState.SimpleWorldDesc.bProviderDisabled);

	PublisherEntry->RequestPublisherEnabled(false);
	TestTrue(TEXT("Pending enabled request publishes"), Helper.Update(Inputs, WindState, 0.1f, 8, 60));
	TestFalse(TEXT("Pending enabled request changes effective enabled"), Helper.IsEffectiveEnabled());

	FKawaiiPhysicsSimpleWorldCollisionSettings PendingSettings = Inputs.SimpleWorld;
	PendingSettings.GatherInterval = 0.25f;
	PublisherEntry->RequestSimpleWorldSettings(PendingSettings);
	TestTrue(TEXT("Pending settings request publishes"), Helper.Update(Inputs, WindState, 0.1f, 9, 60));
	TestEqual(TEXT("Pending settings update effective gather interval"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, 0.25f);
	PublisherEntry->ReadState(ReadState);
	TestEqual(TEXT("Published state keeps effective settings"),
		ReadState.SimpleWorldSettings.GatherInterval, 0.25f);
	TestTrue(TEXT("Pending settings persist after consume"), Helper.Update(Inputs, WindState, 0.1f, 10, 60));
	TestEqual(TEXT("Effective settings persist after consume"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, 0.25f);

	Helper.ResetEffectiveValues(Defaults);
	TestTrue(TEXT("Reset effective values publishes defaults"), Helper.Update(Defaults, WindState, 0.1f, 11, 60));
	TestTrue(TEXT("Reset effective values restores enabled"), Helper.IsEffectiveEnabled());
	TestEqual(TEXT("Reset effective values restores gather interval"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, Defaults.SimpleWorld.GatherInterval);

	PublisherEntry->RequestPublisherEnabled(false);
	TestTrue(TEXT("Pending disable before UPROPERTY change publishes"), Helper.Update(Defaults, WindState, 0.1f, 12, 60));
	TestFalse(TEXT("Pending disable takes effect"), Helper.IsEffectiveEnabled());
	FKawaiiPhysicsSharedPublishInputs ChangedInputs = Defaults;
	ChangedInputs.bEnabled = false;
	TestTrue(TEXT("UPROPERTY false change publishes"), Helper.Update(ChangedInputs, WindState, 0.1f, 13, 60));
	TestFalse(TEXT("UPROPERTY false keeps effective false"), Helper.IsEffectiveEnabled());
	ChangedInputs.bEnabled = true;
	TestTrue(TEXT("UPROPERTY true change publishes"), Helper.Update(ChangedInputs, WindState, 0.1f, 14, 60));
	TestTrue(TEXT("UPROPERTY true restores effective true"), Helper.IsEffectiveEnabled());

	{
		TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> ConflictPublisherEntry =
			MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> ConflictSimpleWorldEntry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		FKawaiiPhysicsSharedPublishHelper ProviderA;
		FKawaiiPhysicsSharedPublishHelper ProviderB;
		ProviderA.SetSourceID(0xA001);
		ProviderB.SetSourceID(0xA002);
		ProviderA.SetEntries(ConflictPublisherEntry, ConflictSimpleWorldEntry, SkelComp);
		ProviderB.SetEntries(ConflictPublisherEntry, ConflictSimpleWorldEntry, SkelComp);
		ProviderA.ResetEffectiveValues(Defaults);
		ProviderB.ResetEffectiveValues(Defaults);

		TestTrue(TEXT("Provider A publishes conflict setup"),
			ProviderA.Update(Defaults, WindState, 0.0f, 1, 60));
		const uint64 ConflictSerial = ConflictPublisherEntry->GetPublishSerial();
		TestEqual(TEXT("Provider A is the only registered provider"),
			ConflictSimpleWorldEntry->GetNumDescs(), 1);

		// 負け側は Provider A と違う設定を持たせ、収集 Desc へ混入しないことを確認する。
		// GatherInterval は Merge が min、bGatherFamilyMembers は or なので、混入すればマージ結果が変わる値を選ぶ。
		FKawaiiPhysicsSharedPublishInputs ConflictInputs = Defaults;
		ConflictInputs.SimpleWorld.GatherInterval = 0.05f;
		ConflictInputs.SimpleWorld.bGatherFamilyMembers = true;
		ConflictInputs.bEnabled = false;

		AddExpectedError(TEXT("Kawaii Physics Shared Publisher rejected publish"),
		                 EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Provider B is rejected while Provider A is alive"),
			ProviderB.Update(ConflictInputs, WindState, 0.0f, 2, 60));
		TestEqual(TEXT("Rejected provider keeps serial"), ConflictPublisherEntry->GetPublishSerial(), ConflictSerial);
		TestFalse(TEXT("Rejected provider does not request reacquire"), ProviderB.NeedsEntryReacquire());
		TestEqual(TEXT("Rejected provider registers no desc"), ConflictSimpleWorldEntry->GetNumDescs(), 1);

		FKawaiiPhysicsSimpleWorldCollisionDesc ConflictMergedDesc;
		TestTrue(TEXT("Conflict merged desc exists"),
			ConflictSimpleWorldEntry->BuildMergedDesc(ConflictMergedDesc));
		TestEqual(TEXT("Conflict merged desc keeps Provider A gather interval"),
			ConflictMergedDesc.GatherIntervalSec, Defaults.SimpleWorld.GatherInterval);
		TestFalse(TEXT("Conflict merged desc ignores the rejected provider family members"),
			ConflictMergedDesc.bGatherFamilyMembers);
		TestFalse(TEXT("Conflict merged desc ignores the rejected provider disable"),
			ConflictMergedDesc.bProviderDisabled);

		// BP からの Pending 要求は勝ち側（Provider A）だけが消費する。
		ConflictPublisherEntry->RequestPublisherEnabled(false);
		TestFalse(TEXT("Provider B stays rejected while a request is pending"),
			ProviderB.Update(ConflictInputs, WindState, 0.0f, 3, 60));
		TestTrue(TEXT("Provider A keeps publishing"),
			ProviderA.Update(Defaults, WindState, 0.0f, 4, 60));
		TestFalse(TEXT("Provider A consumes the pending disable request"), ProviderA.IsEffectiveEnabled());
		TestTrue(TEXT("Provider B keeps its own effective enabled"), ProviderB.IsEffectiveEnabled());

		ProviderA.ReleaseEntries();
		TestEqual(TEXT("Release removes the winning provider desc"),
			ConflictSimpleWorldEntry->GetNumDescs(), 0);
		TestFalse(TEXT("Expired entry rejects Provider B"),
			ProviderB.Update(ConflictInputs, WindState, 0.0f, 5, 60));
		TestTrue(TEXT("Expired entry requests reacquire"), ProviderB.NeedsEntryReacquire());
		// 期限切れ経路では provider Desc も取り下げるので、幽霊 Desc が収集側に残らない
		TestEqual(TEXT("Expired entry leaves Provider B unregistered"),
			ConflictSimpleWorldEntry->GetNumDescs(), 0);
	}

	// 自分が provider の Entry を外部（Subsystem の Cleanup 等）で期限切れにされた場合も、
	// 次の Update で登録済みの provider Desc を取り下げてから再取得を要求する。
	{
		TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> ExpiringPublisherEntry =
			MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> ExpiringSimpleWorldEntry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		FKawaiiPhysicsSharedPublishHelper ProviderC;
		ProviderC.SetSourceID(0xA003);
		ProviderC.SetEntries(ExpiringPublisherEntry, ExpiringSimpleWorldEntry, SkelComp);
		ProviderC.ResetEffectiveValues(Defaults);

		TestTrue(TEXT("Provider C publishes before expiry"),
			ProviderC.Update(Defaults, WindState, 0.0f, 1, 60));
		TestEqual(TEXT("Provider C registers its desc"), ExpiringSimpleWorldEntry->GetNumDescs(), 1);

		ExpiringPublisherEntry->MarkExpired();
		TestFalse(TEXT("Expired entry rejects its own provider"),
			ProviderC.Update(Defaults, WindState, 0.0f, 2, 60));
		TestTrue(TEXT("Expired entry requests reacquire for its own provider"),
			ProviderC.NeedsEntryReacquire());
		TestEqual(TEXT("Expired entry withdraws the provider desc"),
			ExpiringSimpleWorldEntry->GetNumDescs(), 0);
	}

	const float TimeBeforeRelease = WindState->Time;
	Helper.ReleaseEntries();
	TestFalse(TEXT("Release removes provider desc"), SimpleWorldEntry->HasProviderDesc());
	TestEqual(TEXT("Release keeps wind time"), WindState->Time, TimeBeforeRelease);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherStaleProviderDoesNotConsumeAfterHandoffTest,
                                 "KawaiiPhysics.SharedPublisher.StaleProviderDoesNotConsumeAfterHandoff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherStaleProviderDoesNotConsumeAfterHandoffTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceIDA = 0xA001;
	constexpr uint64 SourceIDB = 0xA002;
	constexpr uint64 MaxAgeFrames = 60;

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWindA;
	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWindB;

	FKawaiiPhysicsSharedPublishInputs InputsA;
	InputsA.SharedWind = &SharedWindA;
	InputsA.bWindEnabled = SharedWindA.bIsEnabled;
	InputsA.WindTimeScale = SharedWindA.TimeScale;

	FKawaiiPhysicsSharedPublishInputs InputsB;
	InputsB.SharedWind = &SharedWindB;
	InputsB.bWindEnabled = SharedWindB.bIsEnabled;
	InputsB.WindTimeScale = SharedWindB.TimeScale;

	FKawaiiPhysicsSharedPublishHelper HelperA;
	FKawaiiPhysicsSharedPublishHelper HelperB;
	HelperA.SetSourceID(SourceIDA);
	HelperB.SetSourceID(SourceIDB);
	HelperA.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	HelperB.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	HelperA.ResetEffectiveValues(InputsA);
	HelperB.ResetEffectiveValues(InputsB);

	bool bOk = true;

	// Frame 1: Provider A が受理される
	bOk &= TestTrue(TEXT("Provider A publishes at frame 1"),
		HelperA.Update(InputsA, SharedWindA.RuntimeState, 0.0f, 1, MaxAgeFrames));

	// ReadProviderSnapshot 単体確認: A が publish した直後は所有・非期限切れ
	{
		const FKawaiiPhysicsSharedPublisherEntry::FProviderSnapshot SnapshotAfterA =
			PublisherEntry->ReadProviderSnapshot(1, MaxAgeFrames);
		bOk &= TestEqual(TEXT("Snapshot after A publish reports provider A"), SnapshotAfterA.ProviderID, SourceIDA);
		bOk &= TestFalse(TEXT("Snapshot after A publish is not expired"), SnapshotAfterA.bExpired);
	}

	// A がまだ provider の間に BP から Pending 要求を積む（本来は A が消費するはずの要求）
	FKawaiiProceduralWindDynamicParams PendingParams;
	PendingParams.bOverrideConstantForce = true;
	PendingParams.ConstantForce = 42.0f;
	PublisherEntry->RequestWindParams(PendingParams);
	PublisherEntry->RequestGust(50.0f, 0.1f, 0.2f, 0.3f);

	// Frame 100: MaxAge(60) を超えて A は期限切れになる。ReadProviderSnapshot は 1 回のロックで期限切れを報告する
	{
		const FKawaiiPhysicsSharedPublisherEntry::FProviderSnapshot SnapshotAtHandoff =
			PublisherEntry->ReadProviderSnapshot(100, MaxAgeFrames);
		bOk &= TestTrue(TEXT("Snapshot at frame 100 is expired"), SnapshotAtHandoff.bExpired);
	}

	// Provider B が claim し、A が積んだ Pending を自分の SharedWind へ消費する
	bOk &= TestTrue(TEXT("Provider B claims at frame 100"),
		HelperB.Update(InputsB, SharedWindB.RuntimeState, 0.0f, 100, MaxAgeFrames));
	bOk &= TestEqual(TEXT("Provider B consumes the pending ConstantForce"), SharedWindB.ConstantForce, 42.0f);
	bOk &= TestTrue(TEXT("Provider B consumes the pending gust"),
		SharedWindB.RuntimeState.IsValid() && SharedWindB.RuntimeState->ActiveGust.bIsActive);
	bOk &= TestEqual(TEXT("Entry provider is now B"), PublisherEntry->GetProviderID(), SourceIDB);

	// 旧 provider の A が同フレームで Update しても、B の claim 後は「別 provider 拒否」と同じ経路で拒否され、
	// Pending を消費しない（修正前は ProviderID / IsExpired の別読みで自分を生存 provider と誤認し、ここで消費し得た）
	AddExpectedError(TEXT("Kawaii Physics Shared Publisher rejected publish"),
	                 EAutomationExpectedErrorFlags::Contains, 1);
	bOk &= TestFalse(TEXT("Stale provider A is rejected after handoff"),
		HelperA.Update(InputsA, SharedWindA.RuntimeState, 0.0f, 100, MaxAgeFrames));
	bOk &= TestFalse(TEXT("Stale provider A does not request reacquire (same as other-provider rejection)"),
		HelperA.NeedsEntryReacquire());
	bOk &= TestEqual(TEXT("Stale provider A did not consume the pending ConstantForce"),
		SharedWindA.ConstantForce, 0.0f);
	bOk &= TestEqual(TEXT("Entry provider stays B"), PublisherEntry->GetProviderID(), SourceIDB);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherStaleProviderReleaseKeepsNewProviderTest,
                                 "KawaiiPhysics.SharedPublisher.StaleProviderReleaseKeepsNewProvider",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherStaleProviderReleaseKeepsNewProviderTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceIDA = 0xA001;
	constexpr uint64 SourceIDB = 0xA002;
	constexpr uint64 MaxAgeFrames = 60;

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWindA;
	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWindB;

	FKawaiiPhysicsSharedPublishInputs InputsA;
	InputsA.SharedWind = &SharedWindA;
	InputsA.bWindEnabled = SharedWindA.bIsEnabled;
	InputsA.WindTimeScale = SharedWindA.TimeScale;

	FKawaiiPhysicsSharedPublishInputs InputsB;
	InputsB.SharedWind = &SharedWindB;
	InputsB.bWindEnabled = SharedWindB.bIsEnabled;
	InputsB.WindTimeScale = SharedWindB.TimeScale;

	FKawaiiPhysicsSharedPublishHelper HelperA;
	FKawaiiPhysicsSharedPublishHelper HelperB;
	HelperA.SetSourceID(SourceIDA);
	HelperB.SetSourceID(SourceIDB);
	HelperA.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	HelperB.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	HelperA.ResetEffectiveValues(InputsA);
	HelperB.ResetEffectiveValues(InputsB);

	bool bOk = true;

	// Frame 1: Provider A が受理される
	bOk &= TestTrue(TEXT("Provider A publishes at frame 1"),
		HelperA.Update(InputsA, SharedWindA.RuntimeState, 0.0f, 1, MaxAgeFrames));

	// Frame 100: MaxAge(60) を超えて A は期限切れになり、B が claim する
	bOk &= TestTrue(TEXT("Provider B claims at frame 100"),
		HelperB.Update(InputsB, SharedWindB.RuntimeState, 0.0f, 100, MaxAgeFrames));
	bOk &= TestEqual(TEXT("Entry provider is now B"), PublisherEntry->GetProviderID(), SourceIDB);

	// 旧 provider A が ReleaseEntries を呼んでも、所有権確認と期限切れマークが同じロック区間で行われるため、
	// B が claim した生存 Entry を誤って expire させない
	//（修正前は GetProviderID / MarkExpired の別読みで、A が期限切れ後に自分の所有と誤認して B の Entry を expire しえた）。
	HelperA.ReleaseEntries();

	{
		const FKawaiiPhysicsSharedPublisherEntry::FProviderSnapshot SnapshotAfterRelease =
			PublisherEntry->ReadProviderSnapshot(100, MaxAgeFrames);
		bOk &= TestEqual(TEXT("Entry provider stays B after A releases"), SnapshotAfterRelease.ProviderID, SourceIDB);
		bOk &= TestFalse(TEXT("Entry is not expired after A releases"), SnapshotAfterRelease.bExpired);
	}
	bOk &= TestFalse(TEXT("Entry is not MarkExpired'd after A releases"), PublisherEntry->IsMarkedExpired());

	// Frame 101: B は引き続き publish できる（A の Release で B の状態が失われていない）
	bOk &= TestTrue(TEXT("Provider B continues publishing at frame 101"),
		HelperB.Update(InputsB, SharedWindB.RuntimeState, 0.0f, 101, MaxAgeFrames));

	// MarkExpiredIfProvider 単体確認: 所有者でない ID を渡しても何も変えず false を返す
	bOk &= TestFalse(TEXT("MarkExpiredIfProvider(stale A) returns false while B owns the entry"),
		PublisherEntry->MarkExpiredIfProvider(SourceIDA));
	bOk &= TestFalse(TEXT("Entry stays alive after a mismatched MarkExpiredIfProvider call"),
		PublisherEntry->IsMarkedExpired());

	// MarkExpiredIfProvider 単体確認: 現在の所有者を渡すと期限切れにでき true を返す
	bOk &= TestTrue(TEXT("MarkExpiredIfProvider(B) returns true while B owns the entry"),
		PublisherEntry->MarkExpiredIfProvider(SourceIDB));
	bOk &= TestTrue(TEXT("Entry is expired after MarkExpiredIfProvider(B)"),
		PublisherEntry->IsMarkedExpired());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherPublishHelperWindParamsTest,
                                 "KawaiiPhysics.SharedPublisher.PublishHelperWindParams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherPublishHelperWindParamsTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xB001;
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWind;
	SharedWind.WindDirection = FVector(0.0f, 1.0f, 0.0f);
	SharedWind.WindDirectionNoiseAngle = 4.0f;
	SharedWind.WindDirectionNoisePeriod = 0.6f;
	SharedWind.ConstantForce = 10.0f;
	SharedWind.SwayForce = 2.0f;
	SharedWind.SwayPeriod = 0.5f;
	SharedWind.SwayPhaseOffset = 30.0f;
	SharedWind.RippleForce = 3.0f;
	SharedWind.RipplePeriod = 0.7f;
	SharedWind.RipplePhaseOffset = 45.0f;
	SharedWind.RippleTipPhaseDelay = 120.0f;
	SharedWind.StrengthCycleRange = FFloatInterval(0.75f, 1.5f);
	SharedWind.StrengthCyclePeriod = 3.0f;
	SharedWind.StrengthCyclePhaseOffset = 60.0f;
	SharedWind.RandomForce = 1.0f;
	SharedWind.RandomForcePeriod = 0.4f;
	SharedWind.TimeScale = 1.25f;

	FKawaiiPhysicsSharedPublishInputs Inputs;
	Inputs.SharedWind = &SharedWind;
	Inputs.bWindEnabled = SharedWind.bIsEnabled;
	Inputs.WindTimeScale = SharedWind.TimeScale;

	FKawaiiPhysicsSharedPublishHelper Helper;
	Helper.SetSourceID(SourceID);
	Helper.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	Helper.ResetEffectiveValues(Inputs);

	bool bOk = TestTrue(TEXT("Initial wind update publishes"),
	                    Helper.Update(Inputs, SharedWind.RuntimeState, 0.1f, 1, 60));

	FKawaiiPhysicsSharedWindState ReadWind;
	PublisherEntry->ReadWindState(ReadWind);
	bOk &= TestEqual(TEXT("Shared override flag count"), CountSharedPublisherWindOverrideFlags(ReadWind.Params), 13);
	bOk &= TestTrue(TEXT("IsEnabled is local"), !ReadWind.Params.bOverrideIsEnabled);
	bOk &= TestTrue(TEXT("SwayPhaseOffset is local"), !ReadWind.Params.bOverrideSwayPhaseOffset);
	bOk &= TestTrue(TEXT("RipplePhaseOffset is local"), !ReadWind.Params.bOverrideRipplePhaseOffset);
	bOk &= TestTrue(TEXT("StrengthCyclePhaseOffset is local"), !ReadWind.Params.bOverrideStrengthCyclePhaseOffset);
	bOk &= TestTrue(TEXT("TimeScale is local"), !ReadWind.Params.bOverrideTimeScale);
	bOk &= TestEqual(TEXT("Published ConstantForce"), ReadWind.Params.ConstantForce, 10.0f);
	bOk &= TestTrue(TEXT("Published WindDirection"), ReadWind.Params.WindDirection.Equals(FVector(0.0f, 1.0f, 0.0f)));

	PublisherEntry->RequestGust(50.0f, 0.1f, 0.2f, 0.3f);
	bOk &= TestTrue(TEXT("Gust update publishes"), Helper.Update(Inputs, SharedWind.RuntimeState, 0.1f, 2, 60));
	PublisherEntry->ReadWindState(ReadWind);
	bOk &= TestTrue(TEXT("SharedWind has active gust"), SharedWind.RuntimeState->ActiveGust.bIsActive);
	bOk &= TestEqual(TEXT("Published gust strength"), ReadWind.ActiveGust.Strength, 50.0f);

	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideConstantForce = true;
	Params.ConstantForce = 123.0f;
	PublisherEntry->RequestWindParams(Params);
	bOk &= TestTrue(TEXT("Wind params update publishes"), Helper.Update(Inputs, SharedWind.RuntimeState, 0.1f, 3, 60));
	PublisherEntry->ReadWindState(ReadWind);
	bOk &= TestEqual(TEXT("SharedWind constant updated"), SharedWind.ConstantForce, 123.0f);
	bOk &= TestEqual(TEXT("Published constant updated"), ReadWind.Params.ConstantForce, 123.0f);

	bOk &= TestTrue(TEXT("Wind params persist"), Helper.Update(Inputs, SharedWind.RuntimeState, 0.1f, 4, 60));
	PublisherEntry->ReadWindState(ReadWind);
	bOk &= TestEqual(TEXT("Published constant persists"), ReadWind.Params.ConstantForce, 123.0f);

	Helper.ResetEffectiveValues(FKawaiiPhysicsSharedPublishInputs());
	FKawaiiPhysicsSharedPublishInputs ResetInputs;
	ResetInputs.SharedWind = &SharedWind;
	ResetInputs.bWindEnabled = SharedWind.bIsEnabled;
	ResetInputs.WindTimeScale = SharedWind.TimeScale;
	bOk &= TestTrue(TEXT("Reset effective keeps wind params"), Helper.Update(ResetInputs, SharedWind.RuntimeState, 0.1f, 5, 60));
	PublisherEntry->ReadWindState(ReadWind);
	bOk &= TestEqual(TEXT("Published constant survives reset"), ReadWind.Params.ConstantForce, 123.0f);

#if WITH_EDITOR
	const uint64 ScopeSamples = SharedWind.RuntimeState->ScopeSampleCount;
	bOk &= TestTrue(TEXT("Scope samples increase"), Helper.Update(ResetInputs, SharedWind.RuntimeState, 0.1f, 6, 60));
	bOk &= TestTrue(TEXT("Scope sample count advanced"), SharedWind.RuntimeState->ScopeSampleCount > ScopeSamples);
#endif
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherStalledPublisherResumesWithoutTimeRewindTest,
                                 "KawaiiPhysics.SharedPublisher.StalledPublisherResumesWithoutTimeRewind",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Publisher の枝が blend weight 0 などで Update されない間、消費側は PublisherTimeScale で外挿する。
// PreUpdate は枝の relevance に関係なく走るので、そこで累積した DeltaSeconds を再開した Update がまとめて消費し、
// 消費側が先へ進めた Time より手前を publish して巻き戻す（位相のポップ・突風エンベロープの巻き戻し）ことがないのを確認する。
bool FKawaiiPhysicsSharedPublisherStalledPublisherResumesWithoutTimeRewindTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xC001;
	constexpr uint64 MaxAgeFrames = 60;
	constexpr float Dt = 1.0f / 60.0f;
	constexpr float Tol = 1.0e-4f;
	constexpr int32 StallFrames = 5;

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWind;
	SharedWind.bIsEnabled = true;
	SharedWind.TimeScale = 1.0f;

	FKawaiiPhysicsSharedPublishInputs Inputs;
	Inputs.SharedWind = &SharedWind;
	Inputs.bWindEnabled = SharedWind.bIsEnabled;
	Inputs.WindTimeScale = SharedWind.TimeScale;

	FKawaiiPhysicsSharedPublishHelper Helper;
	Helper.SetSourceID(SourceID);
	Helper.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	Helper.ResetEffectiveValues(Inputs);

	// 消費側の Entry 期限切れ判定は GFrameCounter を見るので、publish も同じフレームで行う
	const uint64 PublishFrame = GFrameCounter;

	bool bOk = true;

	// 1. claim → 定常 publish の 2 フレームを流し、消費側に採用させる
	bOk &= TestTrue(TEXT("Publisher claims the entry"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	bOk &= TestTrue(TEXT("Publisher keeps publishing"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	FKawaiiPhysics_ExternalForce_ProceduralWind ConsumerWind;
	ConsumerWind.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
	FKawaiiPhysicsTestAccessor::BindSharedWindEntry(ConsumerWind, PublisherEntry);

	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	const float ConsumerTime0 = ConsumerWind.RuntimeState->Time;
	bOk &= TestTrue(TEXT("Consumer adopts the published time"),
		FMath::IsNearlyEqual(ConsumerTime0, SharedWind.RuntimeState->Time, Tol));

	// 2. Publisher の枝が止まる（PreUpdate だけが走り、Update は走らない）。消費側は外挿で先へ進む
	float PreviousConsumerTime = ConsumerTime0;
	for (int32 Index = 0; Index < StallFrames; ++Index)
	{
		Helper.AccumulatePendingDeltaTime(Dt);
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		bOk &= TestTrue(FString::Printf(TEXT("Consumer time increases while the publisher is stalled (%d)"), Index),
			ConsumerWind.RuntimeState->Time > PreviousConsumerTime);
		PreviousConsumerTime = ConsumerWind.RuntimeState->Time;
	}
	bOk &= TestTrue(TEXT("Pending delta time accumulates while stalled"),
		FMath::IsNearlyEqual(Helper.GetPendingDeltaTime(), StallFrames * Dt, Tol));

	// 3. 再開: 累積分をまとめて消費して publish する。消費側の Time は巻き戻らない
	Helper.AccumulatePendingDeltaTime(Dt);
	const float ConsumerTimeBeforeResume = ConsumerWind.RuntimeState->Time;
	bOk &= TestTrue(TEXT("Publisher publishes on resume"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);

	bOk &= TestTrue(FString::Printf(TEXT("Consumer time does not rewind on resume: got %.9f before %.9f"),
			ConsumerWind.RuntimeState->Time, ConsumerTimeBeforeResume),
		ConsumerWind.RuntimeState->Time >= ConsumerTimeBeforeResume);
	bOk &= TestTrue(FString::Printf(TEXT("Publisher time catches up the stalled frames: got %.9f expected %.9f"),
			SharedWind.RuntimeState->Time, ConsumerTime0 + (StallFrames + 1) * Dt),
		FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, ConsumerTime0 + (StallFrames + 1) * Dt, Tol));
	bOk &= TestTrue(TEXT("Consumer adopts the caught-up publisher time"),
		FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, SharedWind.RuntimeState->Time, Tol));
	bOk &= TestTrue(TEXT("Pending delta time is consumed on resume"), Helper.GetPendingDeltaTime() == 0.0f);

	// 4. 対照: 毎フレーム PreUpdate と Update が揃う通常運転では、累積が二重計上されない
	const float TimeBeforeNormalFrame = SharedWind.RuntimeState->Time;
	Helper.AccumulatePendingDeltaTime(Dt);
	bOk &= TestTrue(TEXT("Normal frame publishes"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	bOk &= TestTrue(FString::Printf(TEXT("Normal frame advances time by dt only: got %.9f expected %.9f"),
			SharedWind.RuntimeState->Time - TimeBeforeNormalFrame, Dt * SharedWind.TimeScale),
		FMath::IsNearlyEqual(SharedWind.RuntimeState->Time - TimeBeforeNormalFrame, Dt * SharedWind.TimeScale, Tol));

	// 5. 突風: 停止・再開をまたいでも消費側の突風エンベロープが巻き戻らない
	SharedWind.RequestGust(10.0f, 0.1f, 0.5f, 0.2f);
	bOk &= TestTrue(TEXT("Gust frame publishes"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	bOk &= TestTrue(TEXT("Publisher starts the gust"), SharedWind.RuntimeState->ActiveGust.bIsActive);

	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	bOk &= TestTrue(TEXT("Consumer adopts the active gust"), ConsumerWind.RuntimeState->ActiveGust.bIsActive);
	bOk &= TestTrue(TEXT("Consumer adopts the gust start time"),
		FMath::IsNearlyEqual(ConsumerWind.RuntimeState->ActiveGust.StartTime,
			SharedWind.RuntimeState->ActiveGust.StartTime, Tol));

	for (int32 Index = 0; Index < StallFrames; ++Index)
	{
		Helper.AccumulatePendingDeltaTime(Dt);
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	}
	const float GustElapsedBeforeResume =
		ConsumerWind.RuntimeState->Time - ConsumerWind.RuntimeState->ActiveGust.StartTime;

	Helper.AccumulatePendingDeltaTime(Dt);
	bOk &= TestTrue(TEXT("Publisher resumes after the gust stall"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);

	bOk &= TestTrue(TEXT("Consumer gust start time still matches the publisher after resume"),
		FMath::IsNearlyEqual(ConsumerWind.RuntimeState->ActiveGust.StartTime,
			SharedWind.RuntimeState->ActiveGust.StartTime, Tol));
	const float GustElapsedAfterResume =
		ConsumerWind.RuntimeState->Time - ConsumerWind.RuntimeState->ActiveGust.StartTime;
	bOk &= TestTrue(FString::Printf(TEXT("Gust elapsed time does not rewind: got %.9f before %.9f"),
			GustElapsedAfterResume, GustElapsedBeforeResume),
		GustElapsedAfterResume >= GustElapsedBeforeResume);
	bOk &= TestTrue(TEXT("Consumer gust envelope matches the publisher after resume"),
		FMath::IsNearlyEqual(ConsumerWind.ComputeWindSample(ConsumerWind.RuntimeState->Time).Gust,
			SharedWind.ComputeWindSample(SharedWind.RuntimeState->Time).Gust, Tol));

	// 6. 停止中に積まれた TimeScale 上書き要求（BP の SetProceduralWindParametersOnSharedPublisher や
	//    Publisher Details のライブプッシュ）は、publish された次のフレームからだけ効かせる。
	//    停止区間や再開フレームまで新しい scale で進めると、消費側が旧 scale で外挿した Time より手前を publish して
	//    巻き戻ってしまう（消費側は Publisher より先に評価されることがあるので、当フレーム分も旧 scale で進める）。
	//    ここまでの 1〜5 が「要求を積まない従来ケース」の対照になっている。

	// 突風エンベロープの内側でケースを回すため、rise / hold / decay の長い突風を張り直す
	SharedWind.RequestGust(10.0f, 0.5f, 20.0f, 10.0f);
	Helper.AccumulatePendingDeltaTime(Dt);
	bOk &= TestTrue(TEXT("Long gust frame publishes"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	bOk &= TestTrue(TEXT("Consumer adopts the long gust"), ConsumerWind.RuntimeState->ActiveGust.bIsActive);

	const auto RunStalledTimeScaleRequestCase = [&](const TCHAR* CaseName, const float RequestedTimeScale) -> bool
	{
		bool bCaseOk = true;
		// 停止中に消費側が外挿へ使う scale（＝最後に publish された PublisherTimeScale）
		const float StalledTimeScale = SharedWind.TimeScale;
		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: consumer caches the published time scale"), CaseName),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->CachedPublisherTimeScale, StalledTimeScale, Tol));

		// 停止: PreUpdate だけが走り、消費側は旧 scale で外挿する
		for (int32 Index = 0; Index < StallFrames; ++Index)
		{
			Helper.AccumulatePendingDeltaTime(Dt);
			RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		}
		const float ConsumerTimeBeforeResume = ConsumerWind.RuntimeState->Time;
		const float GustElapsedBeforeResume =
			ConsumerWind.RuntimeState->Time - ConsumerWind.RuntimeState->ActiveGust.StartTime;

		// 停止中に TimeScale 上書き要求を Entry へ積む
		FKawaiiProceduralWindDynamicParams StalledParams;
		StalledParams.bOverrideTimeScale = true;
		StalledParams.TimeScale = RequestedTimeScale;
		PublisherEntry->RequestWindParams(StalledParams);

		// 再開フレーム: 停止区間も当フレームの dt も旧 scale で進み、新しい scale は次のフレームから効く
		Helper.AccumulatePendingDeltaTime(Dt);
		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: publisher publishes on resume"), CaseName),
			Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);

		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: requested time scale is applied"), CaseName),
			FMath::IsNearlyEqual(SharedWind.TimeScale, RequestedTimeScale, Tol));
		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: consumer time does not rewind: got %.9f before %.9f"),
				CaseName, ConsumerWind.RuntimeState->Time, ConsumerTimeBeforeResume),
			ConsumerWind.RuntimeState->Time >= ConsumerTimeBeforeResume - Tol);

		// 停止区間＋再開フレームをまとめて旧 scale で進めるので、消費側の外挿値ぴったりに追いつく
		const float ExpectedPublisherTime = ConsumerTimeBeforeResume + Dt * StalledTimeScale;
		bCaseOk &= TestTrue(FString::Printf(
				TEXT("%s: publisher advances the stalled span and the resume frame with the old scale: got %.9f expected %.9f"),
				CaseName, SharedWind.RuntimeState->Time, ExpectedPublisherTime),
			FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, ExpectedPublisherTime, Tol));
		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: consumer adopts the published time"), CaseName),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, SharedWind.RuntimeState->Time, Tol));
		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: consumer caches the new time scale"), CaseName),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->CachedPublisherTimeScale, RequestedTimeScale, Tol));

		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: consumer gust start time matches the publisher"), CaseName),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->ActiveGust.StartTime,
				SharedWind.RuntimeState->ActiveGust.StartTime, Tol));
		const float GustElapsedAfterResume =
			ConsumerWind.RuntimeState->Time - ConsumerWind.RuntimeState->ActiveGust.StartTime;
		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: gust elapsed time does not rewind: got %.9f before %.9f"),
				CaseName, GustElapsedAfterResume, GustElapsedBeforeResume),
			GustElapsedAfterResume >= GustElapsedBeforeResume - Tol);

		// 次のケースに入る前に通常フレームを 1 回挟み、serial を進めて新しい scale の採用を確定させる。
		// このフレームは新しい scale が publish 済みなので、ここから新しい scale で進む
		const float CaseNormalFrameTimeBefore = SharedWind.RuntimeState->Time;
		Helper.AccumulatePendingDeltaTime(Dt);
		bCaseOk &= TestTrue(FString::Printf(TEXT("%s: normal frame publishes after the case"), CaseName),
			Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
		bCaseOk &= TestTrue(FString::Printf(
				TEXT("%s: the next normal frame advances with the new scale: got %.9f expected %.9f"),
				CaseName, SharedWind.RuntimeState->Time - CaseNormalFrameTimeBefore, Dt * RequestedTimeScale),
			FMath::IsNearlyEqual(SharedWind.RuntimeState->Time - CaseNormalFrameTimeBefore,
				Dt * RequestedTimeScale, Tol));
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);

		return bCaseOk;
	};

	// 遅くする / 止める / 速くする の順（再開フレームは旧 scale で進み、新しい scale はその次の通常フレームから効く）
	bOk &= RunStalledTimeScaleRequestCase(TEXT("Lower time scale requested while stalled"), 0.25f);
	bOk &= RunStalledTimeScaleRequestCase(TEXT("Zero time scale requested while stalled"), 0.0f);
	bOk &= RunStalledTimeScaleRequestCase(TEXT("Higher time scale requested while stalled"), 4.0f);

	// 7. 無効中の停止: Publisher は無効の間クロックを止めて publish するので、消費側の外挿も同じ規則で止まる。
	//    片側だけ進むと、無効の Publisher が停止→再開したときに消費側の Time が巻き戻る。
	constexpr float StoppedTol = 1.0e-6f;

	FKawaiiProceduralWindDynamicParams DisableParams;
	DisableParams.bOverrideIsEnabled = true;
	DisableParams.bIsEnabled = false;
	PublisherEntry->RequestWindParams(DisableParams);
	Helper.AccumulatePendingDeltaTime(Dt);
	bOk &= TestTrue(TEXT("Disable request frame publishes"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	bOk &= TestFalse(TEXT("Disable request disables the publisher wind"), SharedWind.bIsEnabled);
	bOk &= TestTrue(TEXT("Consumer sees the publisher wind disabled"),
		ConsumerWind.RuntimeState->bPublisherWindDisabled);

	const float DisabledPublisherTime = SharedWind.RuntimeState->Time;
	const float DisabledConsumerTime = ConsumerWind.RuntimeState->Time;

	for (int32 Index = 0; Index < StallFrames; ++Index)
	{
		Helper.AccumulatePendingDeltaTime(Dt);
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		bOk &= TestTrue(FString::Printf(
				TEXT("Consumer clock stays stopped while the disabled publisher is stalled (%d): got %.9f expected %.9f"),
				Index, ConsumerWind.RuntimeState->Time, DisabledConsumerTime),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, DisabledConsumerTime, StoppedTol));
	}
	bOk &= TestTrue(TEXT("Disabled publisher clock stays stopped while stalled"),
		FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, DisabledPublisherTime, StoppedTol));

	Helper.AccumulatePendingDeltaTime(Dt);
	bOk &= TestTrue(TEXT("Disabled publisher publishes on resume"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	bOk &= TestTrue(FString::Printf(
			TEXT("Disabled publisher does not advance the stalled span: got %.9f expected %.9f"),
			SharedWind.RuntimeState->Time, DisabledPublisherTime),
		FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, DisabledPublisherTime, StoppedTol));
	bOk &= TestTrue(FString::Printf(
			TEXT("Consumer time does not rewind after the disabled stall: got %.9f before %.9f"),
			ConsumerWind.RuntimeState->Time, DisabledConsumerTime),
		ConsumerWind.RuntimeState->Time >= DisabledConsumerTime - StoppedTol);
	bOk &= TestTrue(TEXT("Consumer matches the disabled publisher after resume"),
		FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, SharedWind.RuntimeState->Time, Tol));

	// 8. 停止中に Enable 要求: 停止区間も再開フレームも「最後に publish した無効」で進めないので Time は据え置き。
	//    有効化は publish された次のフレームから効く（消費側が Publisher より先に評価されても外挿値と一致する）
	const float DisabledTimeBeforeEnable = SharedWind.RuntimeState->Time;
	const float ConsumerTimeBeforeEnable = ConsumerWind.RuntimeState->Time;
	for (int32 Index = 0; Index < StallFrames; ++Index)
	{
		Helper.AccumulatePendingDeltaTime(Dt);
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	}

	FKawaiiProceduralWindDynamicParams EnableParams;
	EnableParams.bOverrideIsEnabled = true;
	EnableParams.bIsEnabled = true;
	PublisherEntry->RequestWindParams(EnableParams);

	Helper.AccumulatePendingDeltaTime(Dt);
	bOk &= TestTrue(TEXT("Enable request frame publishes on resume"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);

	bOk &= TestTrue(TEXT("Enable request re-enables the publisher wind"), SharedWind.bIsEnabled);
	bOk &= TestTrue(FString::Printf(
			TEXT("Enabled resume keeps the clock stopped for the resume frame: got %.9f expected %.9f"),
			SharedWind.RuntimeState->Time, DisabledTimeBeforeEnable),
		FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, DisabledTimeBeforeEnable, StoppedTol));
	bOk &= TestTrue(FString::Printf(
			TEXT("Consumer time does not rewind on the enable resume: got %.9f before %.9f"),
			ConsumerWind.RuntimeState->Time, ConsumerTimeBeforeEnable),
		ConsumerWind.RuntimeState->Time >= ConsumerTimeBeforeEnable - Tol);
	bOk &= TestTrue(TEXT("Consumer adopts the re-enabled publisher time"),
		FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, SharedWind.RuntimeState->Time, Tol));
	bOk &= TestFalse(TEXT("Consumer clears the publisher disabled flag on re-enable"),
		ConsumerWind.RuntimeState->bPublisherWindDisabled);

	// 有効フラグが publish された次の通常フレームから、ようやくクロックが動き出す
	const float EnabledNormalFrameTimeBefore = SharedWind.RuntimeState->Time;
	Helper.AccumulatePendingDeltaTime(Dt);
	bOk &= TestTrue(TEXT("Normal frame after the enable request publishes"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	bOk &= TestTrue(FString::Printf(
			TEXT("The frame after the enable request advances by dt: got %.9f expected %.9f"),
			SharedWind.RuntimeState->Time - EnabledNormalFrameTimeBefore, Dt * SharedWind.TimeScale),
		FMath::IsNearlyEqual(SharedWind.RuntimeState->Time - EnabledNormalFrameTimeBefore,
			Dt * SharedWind.TimeScale, Tol));
	bOk &= TestTrue(TEXT("Consumer follows the publisher after the enable request"),
		FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, SharedWind.RuntimeState->Time, Tol));

	// 9. 停止中の同 Entry reinit: Persona で Wind Preset や Simple World 設定を変えると PreUpdate が reinit を要求し、
	//    同じ Tag / Entry のまま ResetEffectiveValues だけが呼ばれる（ReleaseEntries は呼ばれない）。
	//    このとき累積を捨てると再開時の追いつきが不足し、消費側（Entry と serial を持ったまま外挿している）の Time より
	//    手前を publish して巻き戻ってしまうので、Entry を保持したままの reinit では累積を残す。
	{
		const float ReinitTimeScale = SharedWind.TimeScale;

		for (int32 Index = 0; Index < StallFrames; ++Index)
		{
			Helper.AccumulatePendingDeltaTime(Dt);
			RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		}
		const float ReinitConsumerTimeBeforeResume = ConsumerWind.RuntimeState->Time;
		const bool ReinitGustActiveBeforeResume = ConsumerWind.RuntimeState->ActiveGust.bIsActive;
		const float ReinitGustElapsedBeforeResume =
			ConsumerWind.RuntimeState->Time - ConsumerWind.RuntimeState->ActiveGust.StartTime;

		// 停止中に同じ Entry のまま reinit（Persona のプリセット変更等）が入る
		Helper.ResetEffectiveValues(Inputs);
		bOk &= TestTrue(FString::Printf(
				TEXT("Reinit keeps the accumulated delta time: got %.9f expected %.9f"),
				Helper.GetPendingDeltaTime(), StallFrames * Dt),
			FMath::IsNearlyEqual(Helper.GetPendingDeltaTime(), StallFrames * Dt, Tol));

		Helper.AccumulatePendingDeltaTime(Dt);
		bOk &= TestTrue(TEXT("Publisher publishes on resume after the reinit"),
			Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);

		const float ExpectedPublisherTime = ReinitConsumerTimeBeforeResume + Dt * ReinitTimeScale;
		bOk &= TestTrue(FString::Printf(
				TEXT("Publisher catches up the stalled span across the reinit: got %.9f expected %.9f"),
				SharedWind.RuntimeState->Time, ExpectedPublisherTime),
			FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, ExpectedPublisherTime, Tol));
		bOk &= TestTrue(FString::Printf(
				TEXT("Consumer time does not rewind after the reinit resume: got %.9f before %.9f"),
				ConsumerWind.RuntimeState->Time, ReinitConsumerTimeBeforeResume),
			ConsumerWind.RuntimeState->Time >= ReinitConsumerTimeBeforeResume - Tol);
		bOk &= TestTrue(TEXT("Consumer adopts the caught-up time after the reinit"),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, SharedWind.RuntimeState->Time, Tol));

		if (ReinitGustActiveBeforeResume)
		{
			bOk &= TestTrue(TEXT("Consumer gust start time matches the publisher after the reinit"),
				FMath::IsNearlyEqual(ConsumerWind.RuntimeState->ActiveGust.StartTime,
					SharedWind.RuntimeState->ActiveGust.StartTime, Tol));
			const float ReinitGustElapsedAfterResume =
				ConsumerWind.RuntimeState->Time - ConsumerWind.RuntimeState->ActiveGust.StartTime;
			bOk &= TestTrue(FString::Printf(
					TEXT("Gust elapsed time does not rewind across the reinit: got %.9f before %.9f"),
					ReinitGustElapsedAfterResume, ReinitGustElapsedBeforeResume),
				ReinitGustElapsedAfterResume >= ReinitGustElapsedBeforeResume - Tol);
		}
	}

	// 10. 消費側が先に評価されるフレーム: 並列アニメ更新に順序保証は無いので、別 AnimBlueprint の消費側は
	//     同じフレームで Publisher の Update より先に評価されることがある。その消費側は serial 未変化を見て
	//     旧 scale / 旧有効フラグで当フレーム分を外挿するため、Publisher も当フレーム分を同じ値で進めてから
	//     新しい要求を取り込む。こうすると停止の有無に関係なく publish 値が外挿値を下回らない。
	{
		FKawaiiProceduralWindDynamicParams OrderFirstDefaultParams;
		OrderFirstDefaultParams.bOverrideTimeScale = true;
		OrderFirstDefaultParams.TimeScale = 1.0f;
		OrderFirstDefaultParams.bOverrideIsEnabled = true;
		OrderFirstDefaultParams.bIsEnabled = true;

		// Publisher → 消費側の順で 1 フレーム回す（定常運転）
		const auto RunOrderFirstSteadyFrame = [&]() -> bool
		{
			Helper.AccumulatePendingDeltaTime(Dt);
			const bool bPublished = Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames);
			RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
			return bPublished;
		};

		const auto RunOrderFirstCase = [&](const TCHAR* CaseName,
		                                   const FKawaiiProceduralWindDynamicParams& OrderFirstRequest,
		                                   const int32 OrderFirstStallFrames,
		                                   const float OrderFirstExpectedTimeScale,
		                                   const bool bOrderFirstExpectDisabled) -> bool
		{
			bool bCaseOk = true;

			// 直前のケースの影響を消し、scale 1 / 有効の定常運転から始める（2 フレーム目で消費側が新しい scale を採用する）
			PublisherEntry->RequestWindParams(OrderFirstDefaultParams);
			bCaseOk &= TestTrue(FString::Printf(TEXT("%s: setup frame publishes"), CaseName),
				RunOrderFirstSteadyFrame());
			bCaseOk &= TestTrue(FString::Printf(TEXT("%s: steady frame publishes"), CaseName),
				RunOrderFirstSteadyFrame());

			const float OrderFirstScale = SharedWind.TimeScale;
			bCaseOk &= TestTrue(FString::Printf(TEXT("%s: consumer caches the steady time scale"), CaseName),
				FMath::IsNearlyEqual(ConsumerWind.RuntimeState->CachedPublisherTimeScale, OrderFirstScale, Tol));

			// 停止フレーム（PreUpdate だけが走る）。0 フレームなら停止無しの純粋な定常運転ケースになる
			for (int32 Index = 0; Index < OrderFirstStallFrames; ++Index)
			{
				Helper.AccumulatePendingDeltaTime(Dt);
				RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
			}

			// (a) 消費側が Publisher より先に評価される: serial 未変化なので旧 scale / 旧有効フラグで外挿する
			RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
			const float OrderFirstExtrapolatedTime = ConsumerWind.RuntimeState->Time;

			// (b) 同じフレームで BP から TimeScale / Enabled の変更要求が届く
			PublisherEntry->RequestWindParams(OrderFirstRequest);

			// (c) 同じフレームの Publisher の Update（当フレーム分は旧 scale で進め、要求は次フレームから効かせる）
			Helper.AccumulatePendingDeltaTime(Dt);
			bCaseOk &= TestTrue(FString::Printf(TEXT("%s: publisher publishes after the consumer"), CaseName),
				Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, MaxAgeFrames));
			bCaseOk &= TestTrue(FString::Printf(
					TEXT("%s: published time matches the consumer extrapolation: got %.9f expected %.9f"),
					CaseName, SharedWind.RuntimeState->Time, OrderFirstExtrapolatedTime),
				FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, OrderFirstExtrapolatedTime, Tol));

			// 次のフレーム: 消費側が新しい serial を採用する。外挿済みの Time より手前へは戻らない
			RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
			bCaseOk &= TestTrue(FString::Printf(
					TEXT("%s: consumer time does not rewind when it adopts: got %.9f before %.9f"),
					CaseName, ConsumerWind.RuntimeState->Time, OrderFirstExtrapolatedTime),
				ConsumerWind.RuntimeState->Time >= OrderFirstExtrapolatedTime - Tol);
			bCaseOk &= TestTrue(FString::Printf(TEXT("%s: consumer caches the requested time scale"), CaseName),
				FMath::IsNearlyEqual(ConsumerWind.RuntimeState->CachedPublisherTimeScale,
					OrderFirstExpectedTimeScale, Tol));
			bCaseOk &= TestEqual(FString::Printf(TEXT("%s: consumer publisher disabled flag"), CaseName),
				ConsumerWind.RuntimeState->bPublisherWindDisabled, bOrderFirstExpectDisabled);

			return bCaseOk;
		};

		FKawaiiProceduralWindDynamicParams OrderFirstZeroScaleParams;
		OrderFirstZeroScaleParams.bOverrideTimeScale = true;
		OrderFirstZeroScaleParams.TimeScale = 0.0f;

		FKawaiiProceduralWindDynamicParams OrderFirstDisableParams;
		OrderFirstDisableParams.bOverrideIsEnabled = true;
		OrderFirstDisableParams.bIsEnabled = false;

		bOk &= RunOrderFirstCase(TEXT("Consumer evaluated first with a zero time scale request"),
			OrderFirstZeroScaleParams, 0, 0.0f, false);
		bOk &= RunOrderFirstCase(TEXT("Consumer evaluated first with a disable request"),
			OrderFirstDisableParams, 0, 1.0f, true);
		bOk &= RunOrderFirstCase(TEXT("Consumer evaluated first after a stall with a zero time scale request"),
			OrderFirstZeroScaleParams, StallFrames, 0.0f, false);

		// 既定へ戻し、通常運転が Dt ずつ進むところまで確認する
		PublisherEntry->RequestWindParams(OrderFirstDefaultParams);
		bOk &= TestTrue(TEXT("Consumer-first restore frame publishes"), RunOrderFirstSteadyFrame());
		bOk &= TestTrue(TEXT("Consumer-first restore steady frame publishes"), RunOrderFirstSteadyFrame());
		const float OrderFirstNormalTimeBefore = SharedWind.RuntimeState->Time;
		bOk &= TestTrue(TEXT("Consumer-first normal frame publishes"), RunOrderFirstSteadyFrame());
		bOk &= TestTrue(FString::Printf(
				TEXT("Normal operation advances by dt after the consumer-first cases: got %.9f expected %.9f"),
				SharedWind.RuntimeState->Time - OrderFirstNormalTimeBefore, Dt),
			FMath::IsNearlyEqual(SharedWind.RuntimeState->Time - OrderFirstNormalTimeBefore, Dt, Tol));
		bOk &= TestTrue(TEXT("Consumer matches the publisher after the consumer-first cases"),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, SharedWind.RuntimeState->Time, Tol));
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherClaimFramePublishesCaughtUpClockTest,
                                 "KawaiiPhysics.SharedPublisher.ClaimFramePublishesCaughtUpClock",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// claim フレーム（provider 不在・期限切れからの取り直し）は受理判定のために publish を先に行うので、
// 停止区間と当フレーム分の追いつきはその publish より前に済ませる。済ませないと、別 AnimBlueprint の消費側が
// 並列評価でその中間 publish を読んだときに Time が巻き戻る。
bool FKawaiiPhysicsSharedPublisherClaimFramePublishesCaughtUpClockTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xC101;
	constexpr uint64 MaxAgeFrames = 60;
	constexpr float Dt = 1.0f / 60.0f;
	constexpr float Tol = 1.0e-4f;
	constexpr int32 StallFrames = 5;

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWind;
	SharedWind.bIsEnabled = true;
	SharedWind.TimeScale = 1.0f;

	FKawaiiPhysicsSharedPublishInputs Inputs;
	Inputs.SharedWind = &SharedWind;
	Inputs.bWindEnabled = SharedWind.bIsEnabled;
	Inputs.WindTimeScale = SharedWind.TimeScale;

	FKawaiiPhysicsSharedPublishHelper Helper;
	Helper.SetSourceID(SourceID);
	Helper.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	Helper.ResetEffectiveValues(Inputs);

	// 消費側の Entry 期限切れ判定は GFrameCounter を見るので、publish も同じフレームから始める
	uint64 Frame = GFrameCounter;

	bool bOk = true;

	// 1. 定常 publish 2 回で消費側に採用させる
	bOk &= TestTrue(TEXT("Publisher claims the entry"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, Frame, MaxAgeFrames));
	bOk &= TestTrue(TEXT("First update is a claim frame"), Helper.WasLastUpdateClaim());
	bOk &= TestTrue(TEXT("Publisher keeps publishing"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, Frame, MaxAgeFrames));
	bOk &= TestFalse(TEXT("Steady frame is not a claim frame"), Helper.WasLastUpdateClaim());

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	FKawaiiPhysics_ExternalForce_ProceduralWind ConsumerWind;
	ConsumerWind.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
	FKawaiiPhysicsTestAccessor::BindSharedWindEntry(ConsumerWind, PublisherEntry);
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);

	const float T0 = SharedWind.RuntimeState->Time;
	bOk &= TestTrue(TEXT("Consumer adopts the published time"),
		FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, T0, Tol));

	// 2. Publisher の枝が止まる（PreUpdate だけが走る）。消費側は外挿で先へ進む
	for (int32 Index = 0; Index < StallFrames; ++Index)
	{
		Helper.AccumulatePendingDeltaTime(Dt);
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	}
	const float ConsumerTimeBeforeClaim = ConsumerWind.RuntimeState->Time;

	// 3. 期限切れを再現して claim 経路に入れる（provider は自分のままだが MaxAge を超えたので取り直しになる）
	Helper.AccumulatePendingDeltaTime(Dt);
	Frame += MaxAgeFrames + 1;
	bOk &= TestTrue(TEXT("Publisher re-claims the expired entry"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, Frame, MaxAgeFrames));
	bOk &= TestTrue(TEXT("Expired entry takes the claim path"), Helper.WasLastUpdateClaim());

	// 停止区間だけでなく当フレーム分も publish 前に進めるので、受理判定の publish が既に最終 Time を載せている
	const float ExpectedResumeTime = T0 + (StallFrames + 1) * Dt;
	bOk &= TestTrue(FString::Printf(
			TEXT("Claim publish carries the caught-up clock: got %.9f expected %.9f"),
			Helper.GetLastClaimPublishedWindTime(), ExpectedResumeTime),
		FMath::IsNearlyEqual(Helper.GetLastClaimPublishedWindTime(), ExpectedResumeTime, Tol));

	bOk &= TestTrue(FString::Printf(TEXT("Claim frame ends at the resumed clock: got %.9f expected %.9f"),
			SharedWind.RuntimeState->Time, ExpectedResumeTime),
		FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, ExpectedResumeTime, Tol));

	FKawaiiPhysicsSharedWindState ReadWind;
	PublisherEntry->ReadWindState(ReadWind);
	bOk &= TestTrue(FString::Printf(TEXT("Re-published state carries the final time: got %.9f expected %.9f"),
			ReadWind.Time, SharedWind.RuntimeState->Time),
		FMath::IsNearlyEqual(ReadWind.Time, SharedWind.RuntimeState->Time, Tol));

	// 消費側の期限切れ判定は GFrameCounter を見るので MarkExpired は使わず、再 bind して採用させる
	FKawaiiPhysicsTestAccessor::BindSharedWindEntry(ConsumerWind, PublisherEntry);
	RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	bOk &= TestTrue(FString::Printf(TEXT("Consumer time does not rewind on the claim frame: got %.9f before %.9f"),
			ConsumerWind.RuntimeState->Time, ConsumerTimeBeforeClaim),
		ConsumerWind.RuntimeState->Time >= ConsumerTimeBeforeClaim - Tol);

	// 4. 対照: 停止無しの claim（reinit 直後の初回 Update 相当）も当フレーム分は publish 前に進むので、
	//    claim publish の Time は最終 Time と一致する
	Frame += MaxAgeFrames + 1;
	bOk &= TestTrue(TEXT("Publisher re-claims without a stall"),
		Helper.Update(Inputs, SharedWind.RuntimeState, Dt, Frame, MaxAgeFrames));
	bOk &= TestTrue(TEXT("Second re-claim takes the claim path"), Helper.WasLastUpdateClaim());
	const float ExpectedClaimTimeWithoutStall = SharedWind.RuntimeState->Time;
	bOk &= TestTrue(FString::Printf(
			TEXT("Claim without a stall publishes the current frame clock: got %.9f expected %.9f"),
			Helper.GetLastClaimPublishedWindTime(), ExpectedClaimTimeWithoutStall),
		FMath::IsNearlyEqual(Helper.GetLastClaimPublishedWindTime(), ExpectedClaimTimeWithoutStall, Tol));

	return bOk;
}

#endif
