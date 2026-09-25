// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Generators/MovieSceneEasingCurves.h"
#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "KawaiiPhysicsWindPresetTags.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTemplate.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTrack.h"
#include "UObject/Package.h"

namespace
{
constexpr float GSequencerSectionTol = 0.000001f;

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GSequencerSectionTol));
}

UMovieSceneKawaiiPhysicsSettingsMultiplierSection* NewSection()
{
	return NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(GetTransientPackage());
}

void SetupLinearEaseIn(UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section)
{
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));
	Section->Easing.bManualEaseIn = true;
	Section->Easing.ManualEaseInDuration = 200;

	UMovieSceneBuiltInEasingFunction* LinearEase = NewObject<UMovieSceneBuiltInEasingFunction>(Section);
	LinearEase->Type = EMovieSceneBuiltInEasing::Linear;
	Section->Easing.EaseIn.SetObject(LinearEase);
	Section->Easing.EaseIn.SetInterface(static_cast<IMovieSceneEasingFunction*>(LinearEase));
}

FKawaiiPhysicsSettingsMultiplier MakeScale()
{
	FKawaiiPhysicsSettingsMultiplier Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 0.25f;
	Scale.WorldDampingLocation = 0.75f;
	Scale.WorldDampingRotation = 0.8f;
	Scale.Radius = 1.5f;
	Scale.LimitAngle = 0.6f;
	return Scale;
}

void ApplyScaleToSection(UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section,
                         const FKawaiiPhysicsSettingsMultiplier& Scale)
{
	Section->Damping.SetDefault(Scale.Damping);
	Section->Stiffness.SetDefault(Scale.Stiffness);
	Section->WorldDampingLocation.SetDefault(Scale.WorldDampingLocation);
	Section->WorldDampingRotation.SetDefault(Scale.WorldDampingRotation);
	Section->Radius.SetDefault(Scale.Radius);
	Section->LimitAngle.SetDefault(Scale.LimitAngle);
}

bool TestScaleEqual(FAutomationTestBase& Test, const FKawaiiPhysicsSettingsMultiplier& Actual,
                    const FKawaiiPhysicsSettingsMultiplier& Expected)
{
	bool bOk = true;
	bOk &= TestFloatNear(Test, TEXT("Scale.Damping"), Actual.Damping, Expected.Damping);
	bOk &= TestFloatNear(Test, TEXT("Scale.Stiffness"), Actual.Stiffness, Expected.Stiffness);
	bOk &= TestFloatNear(Test, TEXT("Scale.WorldDampingLocation"), Actual.WorldDampingLocation,
	                     Expected.WorldDampingLocation);
	bOk &= TestFloatNear(Test, TEXT("Scale.WorldDampingRotation"), Actual.WorldDampingRotation,
	                     Expected.WorldDampingRotation);
	bOk &= TestFloatNear(Test, TEXT("Scale.Radius"), Actual.Radius, Expected.Radius);
	bOk &= TestFloatNear(Test, TEXT("Scale.LimitAngle"), Actual.LimitAngle, Expected.LimitAngle);
	return bOk;
}

bool TestChannelDefaultNear(FAutomationTestBase& Test, const TCHAR* Name, const FMovieSceneFloatChannel& Channel,
                            const float Expected)
{
	const TOptional<float> DefaultValue = Channel.GetDefault();
	bool bOk = Test.TestTrue(FString::Printf(TEXT("%s default set"), Name), DefaultValue.IsSet());
	if (DefaultValue.IsSet())
	{
		bOk &= TestFloatNear(Test, Name, DefaultValue.GetValue(), Expected);
	}
	return bOk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightEasingTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_Easing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightEasingTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	SetupLinearEaseIn(Section);

	bool bOk = TestFloatNear(*this, TEXT("Ease start"), Section->EvaluateWeightAtTime(FFrameTime(0)), 0.0f);
	bOk &= TestFloatNear(*this, TEXT("Ease mid"), Section->EvaluateWeightAtTime(FFrameTime(100)), 0.5f);
	bOk &= TestFloatNear(*this, TEXT("Ease outside"), Section->EvaluateWeightAtTime(FFrameTime(500)), 1.0f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightChannelDefaultTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_ChannelDefault",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightChannelDefaultTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	SetupLinearEaseIn(Section);
	Section->Weight.SetDefault(0.5f);

	bool bOk = TestFloatNear(*this, TEXT("Default outside ease"), Section->EvaluateWeightAtTime(FFrameTime(500)),
	                         0.5f);
	bOk &= TestFloatNear(*this, TEXT("Default ease mid"), Section->EvaluateWeightAtTime(FFrameTime(100)), 0.25f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightClampTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_Clamp",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightClampTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));

	Section->Weight.SetDefault(2.0f);
	bool bOk = TestFloatNear(*this, TEXT("Clamp high"), Section->EvaluateWeightAtTime(FFrameTime(500)), 1.0f);

	Section->Weight.SetDefault(-1.0f);
	bOk &= TestFloatNear(*this, TEXT("Clamp low"), Section->EvaluateWeightAtTime(FFrameTime(500)), 0.0f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTemplateFromTrackTest,
                                 "KawaiiPhysics.Sequencer.Section.TemplateFromTrack",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTemplateFromTrackTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Track->CreateNewSection());

	const FKawaiiPhysicsSettingsMultiplier ExpectedScale = MakeScale();
	ApplyScaleToSection(Section, ExpectedScale);
	Section->bFilterExactMatch = true;
	Section->BlendOutTimeOnEnd = 0.75f;

	const FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Sequencer.Test")), false);
	if (TestTag.IsValid())
	{
		Section->FilterTags.AddTag(TestTag);
	}

	Track->AddSection(*Section);

	FMovieSceneEvalTemplatePtr TemplatePtr = Track->CreateTemplateForSection(*Section);
	bool bOk = TestTrue(TEXT("Template valid"), TemplatePtr.IsValid());
	if (!TemplatePtr.IsValid())
	{
		return false;
	}

	bOk &= TestTrue(TEXT("Template script struct"),
	                &TemplatePtr->GetScriptStruct() ==
	                FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate::StaticStruct());

	const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate* Template =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate*>(TemplatePtr.GetPtr());
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Damping"), Template->Damping, ExpectedScale.Damping);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Stiffness"), Template->Stiffness, ExpectedScale.Stiffness);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.WorldDampingLocation"), Template->WorldDampingLocation,
	                              ExpectedScale.WorldDampingLocation);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.WorldDampingRotation"), Template->WorldDampingRotation,
	                              ExpectedScale.WorldDampingRotation);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Radius"), Template->Radius, ExpectedScale.Radius);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.LimitAngle"), Template->LimitAngle, ExpectedScale.LimitAngle);
	bOk &= TestTrue(TEXT("FilterTags copied"), Template->FilterTags == Section->FilterTags);
	bOk &= TestTrue(TEXT("Exact copied"), Template->bFilterExactMatch);
	bOk &= TestFloatNear(*this, TEXT("BlendOut copied"), Template->BlendOutTimeOnEnd, 0.75f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackRootFlagPropagatesToTemplateTest,
                                 "KawaiiPhysics.Sequencer.Section.Track_RootFlagPropagatesToTemplate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackRootFlagPropagatesToTemplateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* DefaultTrack =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* DefaultSection =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(DefaultTrack->CreateNewSection());
	FMovieSceneEvalTemplatePtr DefaultTemplatePtr = DefaultTrack->CreateTemplateForSection(*DefaultSection);

	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* RootTrack =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	RootTrack->bIsRootTrack = true;
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* RootSection =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(RootTrack->CreateNewSection());
	FMovieSceneEvalTemplatePtr RootTemplatePtr = RootTrack->CreateTemplateForSection(*RootSection);

	bool bOk = TestTrue(TEXT("Default template valid"), DefaultTemplatePtr.IsValid());
	bOk &= TestTrue(TEXT("Root template valid"), RootTemplatePtr.IsValid());
	if (!DefaultTemplatePtr.IsValid() || !RootTemplatePtr.IsValid())
	{
		return false;
	}

	const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate* DefaultTemplate =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate*>(DefaultTemplatePtr.GetPtr());
	const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate* RootTemplate =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate*>(RootTemplatePtr.GetPtr());
	bOk &= TestFalse(TEXT("Default track root flag"), DefaultTemplate->bIsRootTrack);
	bOk &= TestTrue(TEXT("Root track root flag"), RootTemplate->bIsRootTrack);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerChannelProxyAfterNewAndDuplicateTest,
                                 "KawaiiPhysics.Sequencer.Section.ChannelProxy_AfterNewAndDuplicate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerChannelProxyAfterNewAndDuplicateTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	bool bOk = TestEqual(TEXT("New channel count"),
	                     Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num(), 7);

	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Duplicate =
		DuplicateObject<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Section, GetTransientPackage());
	bOk &= TestEqual(TEXT("Duplicate channel count"),
	                 Duplicate->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num(), 7);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateScaleChannelKeysTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateScale_ChannelKeys",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateScaleChannelKeysTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	Section->Damping.AddLinearKey(FFrameNumber(0), 0.5f);
	Section->Damping.AddLinearKey(FFrameNumber(1000), 1.5f);

	FKawaiiPhysicsSettingsMultiplier Expected;
	FKawaiiPhysicsSettingsMultiplier Actual = Section->EvaluateScaleAtTime(FFrameTime(500));
	Expected.Damping = 1.0f;
	bool bOk = TestScaleEqual(*this, Actual, Expected);

	Section->Damping.AddLinearKey(FFrameNumber(2000), -1.0f);
	Actual = Section->EvaluateScaleAtTime(FFrameTime(2000));
	bOk &= TestFloatNear(*this, TEXT("Scale.Damping clamp low"), Actual.Damping, 0.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.Stiffness unchanged"), Actual.Stiffness, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.WorldDampingLocation unchanged"), Actual.WorldDampingLocation, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.WorldDampingRotation unchanged"), Actual.WorldDampingRotation, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.Radius unchanged"), Actual.Radius, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.LimitAngle unchanged"), Actual.LimitAngle, 1.0f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopForSectionTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopForSection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopForSectionTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionA = NewSection();
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionB = NewSection();

	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryOther = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryOther);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	bOk &= TestFalse(TEXT("Other section untouched"), EntryOther->bStopped);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);
	bOk &= TestTrue(TEXT("EntryA still stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("Other section still untouched"), EntryOther->bStopped);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionB);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRegisterIdempotentTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RegisterIdempotent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRegisterIdempotentTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);

	bool bOk = TestEqual(TEXT("Single registered entry"),
	                     FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	bOk &= TestTrue(TEXT("Entry stopped"), Entry->bStopped);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryGetQueuedNodeCountTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_GetQueuedNodeCount",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryGetQueuedNodeCountTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	USkeletalMeshComponent* ComponentA = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentB = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentC = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	// (a) Entry が一つも登録されていないセクションは bOutHasLiveEntry=false（未評価と 0 件を区別）
	bool bHasLiveEntry = true;
	int32 Count = FKawaiiPhysicsSequencerMultiplierRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bool bOk = TestFalse(TEXT("No entries: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("No entries: count"), Count, 0);

	// FilterTags がセクションと異なる Entry（フィルタ変更前の残留想定）
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> MismatchedFilterEntry =
		MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	MismatchedFilterEntry->Component = ComponentC;
	MismatchedFilterEntry->LastQueuedNodeCount = 11;
	MismatchedFilterEntry->FilterTags.AddTag(TAG_KawaiiPhysics_WindPreset_Breeze);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, MismatchedFilterEntry);

	// (b) フィルタが一致しない Entry しか無い場合は合算対象外→ bOutHasLiveEntry=false
	bHasLiveEntry = true;
	Count = FKawaiiPhysicsSequencerMultiplierRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bOk &= TestFalse(TEXT("Filter mismatch only: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("Filter mismatch only: count"), Count, 0);

	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> InvalidComponentEntry =
		MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	EntryA->Component = ComponentA;
	EntryA->LastQueuedNodeCount = 3;
	EntryB->Component = ComponentB;
	EntryB->LastQueuedNodeCount = 2;
	InvalidComponentEntry->LastQueuedNodeCount = 7;

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, InvalidComponentEntry);

	// (c) フィルタが一致し生存している Entry があれば合算されて bOutHasLiveEntry=true
	//     （FilterTags 不一致の MismatchedFilterEntry と Component 無効な InvalidComponentEntry は除外される）
	bHasLiveEntry = false;
	Count = FKawaiiPhysicsSequencerMultiplierRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bOk &= TestTrue(TEXT("Matching entries: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("Matching entries: count"), Count, 5);

	EntryA->Component = nullptr;
	EntryB->Component = nullptr;
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRemoveSectionAtStopsOnlyRemovedTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RemoveSectionAtStopsOnlyRemoved",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRemoveSectionAtStopsOnlyRemovedTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionA =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Track->CreateNewSection());
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionB =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Track->CreateNewSection());
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	Track->AddSection(*SectionA);
	Track->AddSection(*SectionB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryB);

	Track->RemoveSectionAt(1);

	bool bOk = TestFalse(TEXT("EntryA not stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEntryStopIdempotentTest,
                                 "KawaiiPhysics.Sequencer.Section.Entry_StopIdempotent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEntryStopIdempotentTest::RunTest(const FString& Parameters)
{
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	Entry->Stop();
	bool bOk = TestTrue(TEXT("Entry stopped"), Entry->bStopped);
	Entry->Stop();
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEntryStopImmediateOverrideTest,
                                 "KawaiiPhysics.Sequencer.Section.Entry_StopImmediateOverride",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEntryStopImmediateOverrideTest::RunTest(const FString& Parameters)
{
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	Entry->Stop(0.0f);
	bool bOk = TestTrue(TEXT("Entry stopped immediately"), Entry->bStopped);
	Entry->Stop(0.0f);
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopForSectionsNotInTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopForSectionsNotIn",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopForSectionsNotInTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneSection* SectionA = Track->CreateNewSection();
	UMovieSceneSection* SectionB = Track->CreateNewSection();
	Track->AddSection(*SectionA);
	Track->AddSection(*SectionB);

	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryB);

	TArray<UMovieSceneSection*> LiveSections;
	LiveSections.Add(SectionA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSectionsNotIn(Track, LiveSections);

	bool bOk = TestFalse(TEXT("EntryA still active"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRemoveInvalidEntriesTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RemoveInvalidEntries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRemoveInvalidEntriesTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	{
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);
		TestEqual(TEXT("Entry registered"),
		          FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	}

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().RemoveInvalidEntries();
	return TestEqual(TEXT("Invalid entry removed"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopAllTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopAll",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopAllTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionA = NewSection();
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionB = NewSection();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryB);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopAll();

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	bOk &= TestEqual(TEXT("SectionA entries removed"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(SectionA), 0);
	bOk &= TestEqual(TEXT("SectionB entries removed"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(SectionB), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreStopsRecreatedEntryTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreStopsRecreatedEntry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreStopsRecreatedEntryTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	TSharedRef<uint8> Owner = MakeShared<uint8>(0);
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry1 = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry2 = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	Entry1->Owner = Owner;
	Entry2->Owner = Owner;

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry1);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry2);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section, Owner);

	bool bOk = TestTrue(TEXT("Entry1 stopped"), Entry1->bStopped);
	bOk &= TestTrue(TEXT("Entry2 stopped"), Entry2->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreDoesNotStopOtherOwnerTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreDoesNotStopOtherOwner",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreDoesNotStopOtherOwnerTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	TSharedRef<uint8> OwnerA = MakeShared<uint8>(0);
	TSharedRef<uint8> OwnerB = MakeShared<uint8>(0);
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	EntryA->Owner = OwnerA;
	EntryB->Owner = OwnerB;

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section, OwnerA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("EntryB still active"), EntryB->bStopped);
	bOk &= TestEqual(TEXT("Other owner remains"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreStopsOnlyRestoredComponentTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreStopsOnlyRestoredComponent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreStopsOnlyRestoredComponentTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	TSharedRef<uint8> Owner = MakeShared<uint8>(0);
	USkeletalMeshComponent* ComponentA = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentB = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	EntryA->Owner = Owner;
	EntryA->Component = ComponentA;
	EntryB->Owner = Owner;
	EntryB->Component = ComponentB;

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section, Owner, ComponentA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("EntryB still active"), EntryB->bStopped);
	bOk &= TestEqual(TEXT("Other component remains"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreExpiredOwnerIsNoopTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreExpiredOwnerIsNoop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreExpiredOwnerIsNoopTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	TWeakPtr<uint8> ExpiredOwner;
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	{
		TSharedRef<uint8> Owner = MakeShared<uint8>(0);
		ExpiredOwner = Owner;
		Entry->Owner = Owner;
	}

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section, ExpiredOwner);

	bool bOk = TestFalse(TEXT("Entry still active"), Entry->bStopped);
	bOk &= TestEqual(TEXT("Entry remains"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerSectionDefaultsTest,
                                 "KawaiiPhysics.Sequencer.Section.Section_Defaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerSectionDefaultsTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());

	const FOptionalMovieSceneBlendType BlendType = Section->GetBlendType();
	bool bOk = TestTrue(TEXT("Section blend valid"), BlendType.IsValid());
	bOk &= TestTrue(TEXT("Section blend absolute"), BlendType.IsValid() && BlendType.Get() == EMovieSceneBlendType::Absolute);
	bOk &= TestTrue(TEXT("Completion mode"), Section->GetCompletionMode() == EMovieSceneCompletionMode::RestoreState);
	bOk &= TestFalse(TEXT("Completion mode locked"), Section->EvalOptions.bCanEditCompletionMode);
	bOk &= TestChannelDefaultNear(*this, TEXT("Damping"), Section->Damping, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("Stiffness"), Section->Stiffness, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("WorldDampingLocation"), Section->WorldDampingLocation, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("WorldDampingRotation"), Section->WorldDampingRotation, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("Radius"), Section->Radius, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("LimitAngle"), Section->LimitAngle, 1.0f);
	bOk &= TestTrue(TEXT("Track supports absolute"),
	                Track->GetSupportedBlendTypes().Contains(EMovieSceneBlendType::Absolute));
	return bOk;
}

#endif
