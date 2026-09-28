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
	TestEqual(TEXT("ReadState keeps gather interval"), ReadState.SimpleWorldDesc.GatherIntervalSec, 0.05f);

	FKawaiiPhysicsSharedPublisherState BlockedState = State;
	BlockedState.bSimpleWorldEnabled = false;
	BlockedState.SimpleWorldDesc.GatherIntervalSec = 0.75f;
	BlockedState.Wind.Time = 20.0f;
	TestFalse(TEXT("Live different provider is rejected"), Entry.PublishState(BlockedState, 22, 105, 10));

	FKawaiiPhysicsSharedPublisherState AfterRejected;
	Entry.ReadState(AfterRejected);
	TestTrue(TEXT("Rejected publish keeps SimpleWorld enabled"), AfterRejected.bSimpleWorldEnabled);
	TestEqual(TEXT("Rejected publish keeps serial"), Entry.GetPublishSerial(), static_cast<uint64>(1));

	TestTrue(TEXT("Expired previous provider allows replacement"), Entry.PublishState(BlockedState, 22, 111, 10));
	TestEqual(TEXT("Publish serial after provider replacement"), Entry.GetPublishSerial(), static_cast<uint64>(2));
	TestEqual(TEXT("Provider ID after provider replacement"), Entry.GetProviderID(), static_cast<uint64>(22));
	TestEqual(TEXT("Last publish frame after provider replacement"), Entry.GetLastPublishFrame(), static_cast<uint64>(111));

	FKawaiiPhysicsSharedWindState ReadWind;
	const uint64 WindSerial = Entry.ReadWindState(ReadWind);
	TestEqual(TEXT("ReadWindState returns current serial"), WindSerial, static_cast<uint64>(2));
	TestEqual(TEXT("ReadWindState keeps wind time"), ReadWind.Time, BlockedState.Wind.Time);

	TestFalse(TEXT("Entry is not expired within max age"), Entry.IsExpired(120, 10));
	TestTrue(TEXT("Entry is expired beyond max age"), Entry.IsExpired(122, 10));
	Entry.MarkExpired();
	TestTrue(TEXT("MarkExpired makes entry expired"), Entry.IsExpired(111, 10));

	TestTrue(TEXT("Marked entry remains expired at later frame"), Entry.IsExpired(200, 10));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherWindParamsRequestsMergeTest,
                                 "KawaiiPhysics.SharedPublisher.WindParamsRequestsMerge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherWindParamsRequestsMergeTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;

	// 風パラメータ要求を項目単位でマージし、突風要求の順序と一度きりの消費を確認する。
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
	// 突風要求の順序と 2 回目の consume が空であることを確認する。
	Entry.RequestGust(1.0f, 0.1f, 0.2f, 0.3f);
	Entry.RequestGust(2.0f, 0.4f, 0.5f, 0.6f);
	Entry.RequestGustStop(0.7f);
	TArray<FKawaiiPhysicsSharedPublisherGustRequest> GustRequests;
	Entry.ConsumePendingGustRequests(GustRequests);
	bOk &= TestEqual(TEXT("Gust request count"), GustRequests.Num(), 3);
	if (GustRequests.Num() == 3)
	{
		bOk &= TestFalse(TEXT("First gust request starts"), GustRequests[0].bStop);
		bOk &= TestFalse(TEXT("Second gust request starts"), GustRequests[1].bStop);
		bOk &= TestTrue(TEXT("Third gust request stops"), GustRequests[2].bStop);
	}
	GustRequests.Reset();
	Entry.ConsumePendingGustRequests(GustRequests);
	bOk &= TestEqual(TEXT("Second gust consume is empty"), GustRequests.Num(), 0);

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
	PublisherEntry->RequestPublisherEnabled(false);
	FKawaiiPhysicsSimpleWorldCollisionSettings EffectiveSettings = Inputs.SimpleWorld;
	EffectiveSettings.GatherInterval = Inputs.SimpleWorld.GatherInterval + 0.25f;
	PublisherEntry->RequestSimpleWorldSettings(EffectiveSettings);
	TestTrue(TEXT("Blueprint override publishes"), Helper.Update(Inputs, WindState, 0.1f, 4, 60));
	TestFalse(TEXT("Blueprint override disables effective publisher"), Helper.IsEffectiveEnabled());

	OldEntry->RemoveDesc(SourceID);
	TestTrue(TEXT("Detached SimpleWorld entry retires"), OldEntry->MarkRetiredIfEmpty());
	TestTrue(TEXT("Retired SimpleWorld entry does not reject publisher state"), Helper.Update(Inputs, WindState, 0.1f, 5, 60));
	TestTrue(TEXT("Only SimpleWorld entry needs rebinding"), Helper.NeedsSimpleWorldEntryReacquire());
	TestFalse(TEXT("Publisher entry does not need rebinding"), Helper.NeedsEntryReacquire());
	TestTrue(TEXT("Publisher entry pointer is preserved"), Helper.GetSharedPublisherEntry() == PublisherEntry);
	TestFalse(TEXT("Publisher entry remains alive"), PublisherEntry->IsExpired(5, 60));
	TestFalse(TEXT("Effective enabled override survives retirement"), Helper.IsEffectiveEnabled());
	TestFalse(TEXT("Retired entry has no recreated provider"), OldEntry->HasAnyDesc());
	TestFalse(TEXT("Retired entry has no recreated reader"), OldEntry->HasAnyReader());

	const uint64 SerialBeforeRebind = Helper.GetLastPublishSerial();
	const float PublishedTimeBeforeRebind = Helper.GetLastPublishedState().Wind.Time;
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> NewEntry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	Helper.SetSimpleWorldEntry(NewEntry);
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

	FKawaiiPhysicsSharedPublisherState ReadState;
	PublisherEntry->ReadState(ReadState);
	TestTrue(TEXT("SimpleWorld is enabled"), ReadState.bSimpleWorldEnabled);
	TestEqual(TEXT("Published default settings gather interval"),
		ReadState.SimpleWorldSettings.GatherInterval, Defaults.SimpleWorld.GatherInterval);
	TestTrue(TEXT("SimpleWorld provider desc exists"), SimpleWorldEntry->HasProviderDesc());
	TestTrue(TEXT("Provider SkelComp is preserved"), SimpleWorldEntry->GetPrimarySkelComp() == SkelComp);
	TestEqual(TEXT("Provider heartbeat reaches frame 3"), SimpleWorldEntry->GetLastProviderFrame(), static_cast<uint64>(3));

	Inputs.WindTimeScale = 2.0f;
	TestTrue(TEXT("TimeScale 2 publishes"), Helper.Update(Inputs, WindState, 0.1f, 4, 60));


	Inputs.SimpleWorld.GatherInterval = 0.5f;
	TestTrue(TEXT("Desc change publishes"), Helper.Update(Inputs, WindState, 0.1f, 5, 60));
	FKawaiiPhysicsSimpleWorldCollisionDesc MergedDesc;
	TestTrue(TEXT("Merged desc exists after desc change"), SimpleWorldEntry->BuildMergedDesc(MergedDesc));
	TestEqual(TEXT("Merged desc follows gather interval"), MergedDesc.GatherIntervalSec, 0.5f);

	Inputs.SimpleWorld.bEnabled = false;
	Helper.Update(Inputs, WindState, 0.1f, 5, 60);
	PublisherEntry->ReadState(ReadState);
	TestTrue(TEXT("Disabled SimpleWorld settings mark provider disabled"),
		ReadState.SimpleWorldDesc.bProviderDisabled);
	Inputs.SimpleWorld.bEnabled = true;
	Inputs.bEnabled = false;
	TestTrue(TEXT("Disabled state still publishes"), Helper.Update(Inputs, WindState, 0.1f, 6, 60));
	PublisherEntry->ReadState(ReadState);
	TestFalse(TEXT("Disabled publish disables SimpleWorld"), ReadState.bSimpleWorldEnabled);
	TestTrue(TEXT("Disabled publish marks provider disabled"), ReadState.SimpleWorldDesc.bProviderDisabled);
	TestFalse(TEXT("Disabled publish disables wind"), ReadState.Wind.bPublisherWindEnabled);
	TestFalse(TEXT("Disabled publish clears publisher enabled"), ReadState.bPublisherEnabled);
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

		// 負け側は Provider A と違う設定を持たせる
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

	Helper.ReleaseEntries();
	TestFalse(TEXT("Release removes provider desc"), SimpleWorldEntry->HasProviderDesc());
	// 新しい Entry を再取得して publish を再開する。
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> ReacquiredPublisherEntry =
		MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> ReacquiredSimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	Helper.SetEntries(ReacquiredPublisherEntry, ReacquiredSimpleWorldEntry, SkelComp);
	Helper.ResetEffectiveValues(Defaults);
	TestTrue(TEXT("Reacquired entry publishes"), Helper.Update(Defaults, WindState, 0.0f, 12, 60));
	TestEqual(TEXT("Reacquired entry is owned by the publisher"), ReacquiredPublisherEntry->GetProviderID(), SourceID);
	TestTrue(TEXT("Reacquired SimpleWorld entry receives desc"), ReacquiredSimpleWorldEntry->HasProviderDesc());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherRejectedDuplicateRemovesPreUpdateDescTest,
                                 "KawaiiPhysics.SharedPublisher.RejectedDuplicateRemovesPreUpdateDesc",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 同じ Tag の Publisher 2 個が同じフレームに PreUpdate で provider Desc を登録した場合、
// publish に負けた側の Desc をその場で取り下げる（age-out を待たずに収集設定から外れる）
bool FKawaiiPhysicsSharedPublisherRejectedDuplicateRemovesPreUpdateDescTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceIDA = 0xA001;
	constexpr uint64 SourceIDB = 0xA002;
	constexpr uint64 MaxAgeFrames = 60;

	const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry =
		MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> WindState =
		MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();

	FKawaiiPhysicsSharedPublishInputs InputsA;
	// 負け側にはマージ結果が変わる値を持たせる（GatherInterval は min、bGatherFamilyMembers は or）
	FKawaiiPhysicsSharedPublishInputs InputsB;
	InputsB.SimpleWorld.GatherInterval = 0.05f;
	InputsB.SimpleWorld.bGatherFamilyMembers = true;

	// PreUpdate 相当。両方が FindOrCreateSimpleWorldEntry(bProvider=true) を通った直後と同じ状態を作る
	const FKawaiiPhysicsSimpleWorldCollisionDesc DescA =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(InputsA.SimpleWorld);
	const FKawaiiPhysicsSimpleWorldCollisionDesc DescB =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(InputsB.SimpleWorld);
	const TWeakObjectPtr<const USkeletalMeshComponent> WeakSkelComp(SkelComp);
	TestTrue(TEXT("PreUpdate registers the provider A desc"),
		SimpleWorldEntry->SetDesc(SourceIDA, DescA, 1, WeakSkelComp, true));
	TestTrue(TEXT("PreUpdate registers the provider B desc"),
		SimpleWorldEntry->SetDesc(SourceIDB, DescB, 1, WeakSkelComp, true));
	TestEqual(TEXT("Both PreUpdate descs are registered"), SimpleWorldEntry->GetNumDescs(), 2);

	FKawaiiPhysicsSharedPublishHelper ProviderA;
	FKawaiiPhysicsSharedPublishHelper ProviderB;
	ProviderA.SetSourceID(SourceIDA);
	ProviderB.SetSourceID(SourceIDB);
	ProviderA.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp, /*bInProviderDescRegistered*/ true);
	ProviderB.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp, /*bInProviderDescRegistered*/ true);
	ProviderA.ResetEffectiveValues(InputsA);
	ProviderB.ResetEffectiveValues(InputsB);

	AddExpectedError(TEXT("Kawaii Physics Shared Publisher rejected publish"),
	                 EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Provider A claims the publisher entry"),
		ProviderA.Update(InputsA, WindState, 0.0f, 1, MaxAgeFrames));
	TestFalse(TEXT("Provider B is rejected in the same frame"),
		ProviderB.Update(InputsB, WindState, 0.0f, 1, MaxAgeFrames));

	TestEqual(TEXT("Rejected provider desc is withdrawn"), SimpleWorldEntry->GetNumDescs(), 1);
	TestFalse(TEXT("Rejected provider has no desc left"), SimpleWorldEntry->MarkRead(SourceIDB, 1));
	TestTrue(TEXT("Winning provider keeps its desc"), SimpleWorldEntry->MarkRead(SourceIDA, 1));

	FKawaiiPhysicsSimpleWorldCollisionDesc MergedDesc;
	TestTrue(TEXT("Merged desc exists after the rejection"), SimpleWorldEntry->BuildMergedDesc(MergedDesc));
	TestEqual(TEXT("Merged desc keeps the winning gather interval"),
		MergedDesc.GatherIntervalSec, InputsA.SimpleWorld.GatherInterval);
	TestFalse(TEXT("Merged desc drops the rejected family member flag"), MergedDesc.bGatherFamilyMembers);

	// 取り下げ済みなので、拒否が続くフレームで Desc 数も勝ち側の登録も動かない
	for (uint64 Frame = 2; Frame <= 4; ++Frame)
	{
		TestFalse(TEXT("Provider B stays rejected"),
			ProviderB.Update(InputsB, WindState, 0.0f, Frame, MaxAgeFrames));
		TestTrue(TEXT("Provider A keeps publishing"),
			ProviderA.Update(InputsA, WindState, 0.0f, Frame, MaxAgeFrames));
		TestEqual(TEXT("Rejected provider stays unregistered"), SimpleWorldEntry->GetNumDescs(), 1);
	}
	TestEqual(TEXT("Winning provider sends its desc only once"), ProviderA.GetNumSetDescCalls(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherInputChangeBeatsSameFramePendingTest,
                                 "KawaiiPhysics.SharedPublisher.InputChangeBeatsSameFramePending",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 同じフレームに Blueprint の Pending 要求と UPROPERTY 変化が競合したら UPROPERTY 値が勝つ（docs §5-(b) 手順 2）
bool FKawaiiPhysicsSharedPublisherInputChangeBeatsSameFramePendingTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xA001;
	constexpr uint64 ClaimSourceID = 0xA004;
	constexpr uint64 MaxAgeFrames = 60;

	const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry =
		MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> WindState =
		MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();

	FKawaiiPhysicsSharedPublishInputs Defaults;
	FKawaiiPhysicsSharedPublishHelper Helper;
	Helper.SetSourceID(SourceID);
	Helper.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	Helper.ResetEffectiveValues(Defaults);

	FKawaiiPhysicsSharedPublishInputs Inputs = Defaults;
	TestTrue(TEXT("Claim frame publishes"), Helper.Update(Inputs, WindState, 0.0f, 1, MaxAgeFrames));
	TestTrue(TEXT("Steady frame publishes"), Helper.Update(Inputs, WindState, 0.0f, 2, MaxAgeFrames));

	// (a) Pending の無効化と UPROPERTY の有効化が同フレームで競合する
	Inputs.bEnabled = false;
	TestTrue(TEXT("UPROPERTY disable publishes"), Helper.Update(Inputs, WindState, 0.0f, 3, MaxAgeFrames));
	TestFalse(TEXT("UPROPERTY disable takes effect"), Helper.IsEffectiveEnabled());

	PublisherEntry->RequestPublisherEnabled(false);
	Inputs.bEnabled = true;
	TestTrue(TEXT("Conflicting enable publishes"), Helper.Update(Inputs, WindState, 0.0f, 4, MaxAgeFrames));
	TestTrue(TEXT("UPROPERTY enable beats the same frame pending disable"), Helper.IsEffectiveEnabled());
	FKawaiiPhysicsSharedPublisherState ReadState;
	PublisherEntry->ReadState(ReadState);
	TestTrue(TEXT("Published state follows the UPROPERTY enable"), ReadState.bPublisherEnabled);

	// Pending は消費済みなので、次フレーム以降も UPROPERTY 値のまま
	TestTrue(TEXT("Next frame publishes"), Helper.Update(Inputs, WindState, 0.0f, 5, MaxAgeFrames));
	TestTrue(TEXT("Effective enabled stays true on the next frame"), Helper.IsEffectiveEnabled());

	// (b) 収集設定も同じ規則
	FKawaiiPhysicsSimpleWorldCollisionSettings PendingSettings = Inputs.SimpleWorld;
	PendingSettings.GatherInterval = 0.25f;
	PublisherEntry->RequestSimpleWorldSettings(PendingSettings);
	Inputs.SimpleWorld.GatherInterval = 0.5f;
	TestTrue(TEXT("Conflicting settings publish"), Helper.Update(Inputs, WindState, 0.0f, 6, MaxAgeFrames));
	TestEqual(TEXT("UPROPERTY settings beat the same frame pending settings"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, 0.5f);
	PublisherEntry->ReadState(ReadState);
	TestEqual(TEXT("Published settings follow the UPROPERTY change"),
		ReadState.SimpleWorldSettings.GatherInterval, 0.5f);

	// (c) 競合していない項目は Pending が勝つ
	PublisherEntry->RequestPublisherEnabled(false);
	Inputs.SimpleWorld.GatherInterval = 0.35f;
	TestTrue(TEXT("Non conflicting pending publishes"), Helper.Update(Inputs, WindState, 0.0f, 7, MaxAgeFrames));
	TestFalse(TEXT("Pending disable wins while the UPROPERTY did not change"), Helper.IsEffectiveEnabled());
	TestEqual(TEXT("Changed settings still follow the UPROPERTY value"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, 0.35f);

	// (d) claim フレーム（受理後に Pending を消費する経路）でも同じ裁定になる
	{
		const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> ClaimPublisherEntry =
			MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
		const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> ClaimSimpleWorldEntry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		FKawaiiPhysicsSharedPublishInputs ClaimDefaults;
		ClaimDefaults.bEnabled = false;
		FKawaiiPhysicsSharedPublishHelper ClaimProvider;
		ClaimProvider.SetSourceID(ClaimSourceID);
		ClaimProvider.SetEntries(ClaimPublisherEntry, ClaimSimpleWorldEntry, SkelComp);
		ClaimProvider.ResetEffectiveValues(ClaimDefaults);

		ClaimPublisherEntry->RequestPublisherEnabled(false);
		FKawaiiPhysicsSharedPublishInputs ClaimInputs = ClaimDefaults;
		ClaimInputs.bEnabled = true;
		TestTrue(TEXT("Claim frame with a conflicting request publishes"),
			ClaimProvider.Update(ClaimInputs, WindState, 0.0f, 1, MaxAgeFrames));
		TestTrue(TEXT("Claim frame is reported as a claim"), ClaimProvider.WasLastUpdateClaim());
		TestTrue(TEXT("UPROPERTY enable beats the pending disable on a claim frame"),
			ClaimProvider.IsEffectiveEnabled());
		FKawaiiPhysicsSharedPublisherState ClaimState;
		ClaimPublisherEntry->ReadState(ClaimState);
		TestTrue(TEXT("Claim frame republishes the UPROPERTY value"), ClaimState.bPublisherEnabled);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherStaleProviderHandoffTest,
                                 "KawaiiPhysics.SharedPublisher.StaleProviderHandoff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherStaleProviderHandoffTest::RunTest(const FString& Parameters)
{
	// 期限切れ後の所有権移譲で、旧 provider の更新と解放が新 provider を壊さないことを確認する。
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

	bool bOk = TestTrue(TEXT("Provider A publishes at frame 1"),
		HelperA.Update(InputsA, SharedWindA.RuntimeState, 0.0f, 1, MaxAgeFrames));
	FKawaiiProceduralWindDynamicParams PendingParams;
	PendingParams.bOverrideConstantForce = true;
	PendingParams.ConstantForce = 42.0f;
	PublisherEntry->RequestWindParams(PendingParams);
	PublisherEntry->RequestGust(50.0f, 0.1f, 0.2f, 0.3f);

	bOk &= TestTrue(TEXT("Provider B claims at frame 100"),
		HelperB.Update(InputsB, SharedWindB.RuntimeState, 0.0f, 100, MaxAgeFrames));
	bOk &= TestEqual(TEXT("Provider B consumes the pending ConstantForce"), SharedWindB.ConstantForce, 42.0f);
	bOk &= TestTrue(TEXT("Provider B consumes the pending gust"),
		SharedWindB.RuntimeState.IsValid() && SharedWindB.RuntimeState->ActiveGust.bIsActive);
	bOk &= TestEqual(TEXT("Entry provider is now B"), PublisherEntry->GetProviderID(), SourceIDB);

	AddExpectedError(TEXT("Kawaii Physics Shared Publisher rejected publish"),
	                 EAutomationExpectedErrorFlags::Contains, 1);
	bOk &= TestFalse(TEXT("Stale provider A is rejected after handoff"),
		HelperA.Update(InputsA, SharedWindA.RuntimeState, 0.0f, 100, MaxAgeFrames));
	bOk &= TestFalse(TEXT("Stale provider A does not request reacquire"), HelperA.NeedsEntryReacquire());
	bOk &= TestEqual(TEXT("Stale provider A did not consume the pending ConstantForce"),
		SharedWindA.ConstantForce, 0.0f);
	bOk &= TestEqual(TEXT("Entry provider stays B"), PublisherEntry->GetProviderID(), SourceIDB);

	HelperA.ReleaseEntries();
	bOk &= TestEqual(TEXT("Entry provider stays B after A releases"), PublisherEntry->GetProviderID(), SourceIDB);
	bOk &= TestFalse(TEXT("A release does not expire B"), PublisherEntry->IsMarkedExpired());
	bOk &= TestTrue(TEXT("Provider B continues publishing at frame 101"),
		HelperB.Update(InputsB, SharedWindB.RuntimeState, 0.0f, 101, MaxAgeFrames));
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

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherStalledPublisherResumesWithoutTimeRewindTest,
                                 "KawaiiPhysics.SharedPublisher.StalledPublisherResumesWithoutTimeRewind",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherStalledPublisherResumesWithoutTimeRewindTest::RunTest(const FString& Parameters)
{
	// 消費側が先に外挿するフレームで、停止中の要求を反映しても時計が巻き戻らないことを確認する。
	constexpr float Dt = 1.0f / 60.0f;
	constexpr float Tol = 1.0e-4f;
	constexpr int32 StallFrames = 5;
	const uint64 PublishFrame = GFrameCounter;
	bool bOk = true;

	struct FResumeCase
	{
		const TCHAR* Name;
		bool bDisable;
	};
	const FResumeCase Cases[] = {
		{TEXT("Zero scale"), false},
		{TEXT("Disable"), true}
	};
	for (const FResumeCase& Case : Cases)
	{
		TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry =
			MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		FKawaiiPhysics_ExternalForce_ProceduralWind SharedWind;
		SharedWind.bIsEnabled = true;
		SharedWind.TimeScale = 1.0f;
		FKawaiiPhysicsSharedPublishInputs Inputs;
		Inputs.SharedWind = &SharedWind;
		Inputs.bWindEnabled = true;
		Inputs.WindTimeScale = 1.0f;
		Inputs.GameTimeSeconds = 0.0;
		FKawaiiPhysicsSharedPublishHelper Helper;
		Helper.SetSourceID(0xC001);
		Helper.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
		Helper.ResetEffectiveValues(Inputs);
		bOk &= TestTrue(FString::Printf(TEXT("%s: initial claim publishes"), Case.Name),
			Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, 60));
		bOk &= TestTrue(FString::Printf(TEXT("%s: steady frame publishes"), Case.Name),
			Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, 60));

		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.BuildVerticalChain(2, 10.0f);
		FKawaiiPhysics_ExternalForce_ProceduralWind ConsumerWind;
		ConsumerWind.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
		FKawaiiPhysicsTestAccessor::BindSharedWindEntry(ConsumerWind, PublisherEntry);
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		const float InitialTime = SharedWind.RuntimeState->Time;
		bOk &= TestTrue(FString::Printf(TEXT("%s: consumer adopts published time"), Case.Name),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->Time, InitialTime, Tol));

		for (int32 Index = 0; Index < StallFrames; ++Index)
		{
			Inputs.GameTimeSeconds = Inputs.GameTimeSeconds.GetValue() + Dt;
			RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		}

		// 同じフレームで消費側を先に進め、次に BP 要求と publish を処理する。
		Inputs.GameTimeSeconds = Inputs.GameTimeSeconds.GetValue() + Dt;
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		const float ExtrapolatedTime = ConsumerWind.RuntimeState->Time;
		FKawaiiProceduralWindDynamicParams Request;
		if (Case.bDisable)
		{
			Request.bOverrideIsEnabled = true;
			Request.bIsEnabled = false;
		}
		else
		{
			Request.bOverrideTimeScale = true;
			Request.TimeScale = 0.0f;
		}
		PublisherEntry->RequestWindParams(Request);
		bOk &= TestTrue(FString::Printf(TEXT("%s: resume publishes"), Case.Name),
			Helper.Update(Inputs, SharedWind.RuntimeState, Dt, PublishFrame, 60));
		bOk &= TestTrue(FString::Printf(TEXT("%s: publish catches consumer clock"), Case.Name),
			FMath::IsNearlyEqual(SharedWind.RuntimeState->Time, ExtrapolatedTime, Tol));
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
		bOk &= TestTrue(FString::Printf(TEXT("%s: consumer does not rewind"), Case.Name),
			ConsumerWind.RuntimeState->Time >= ExtrapolatedTime - Tol);
		bOk &= TestEqual(FString::Printf(TEXT("%s: disable request is reflected"), Case.Name),
			ConsumerWind.RuntimeState->bPublisherWindDisabled, Case.bDisable);
		bOk &= TestTrue(FString::Printf(TEXT("%s: time scale request is reflected"), Case.Name),
			FMath::IsNearlyEqual(ConsumerWind.RuntimeState->CachedPublisherTimeScale,
				Case.bDisable ? 1.0f : 0.0f, Tol));
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
	Inputs.GameTimeSeconds = 0.0;

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

	// 2. Publisher の枝を止め、World のゲーム内時刻と消費側だけを進める
	for (int32 Index = 0; Index < StallFrames; ++Index)
	{
		Inputs.GameTimeSeconds = Inputs.GameTimeSeconds.GetValue() + Dt;
		RunSharedPublisherConsumerPreApply(Accessor, ConsumerWind, Dt);
	}
	const float ConsumerTimeBeforeClaim = ConsumerWind.RuntimeState->Time;

	// 3. World 時計を進めてから、期限切れの claim 経路に入れる
	Inputs.GameTimeSeconds = Inputs.GameTimeSeconds.GetValue() + Dt;
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
	Inputs.GameTimeSeconds = Inputs.GameTimeSeconds.GetValue() + Dt;
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
