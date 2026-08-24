// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Generators/MovieSceneEasingCurves.h"
#include "KawaiiPhysicsSequencerOverrideRegistry.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTemplate.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTrack.h"

namespace
{
constexpr float GSequencerSectionTol = 0.000001f;

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GSequencerSectionTol));
}

UMovieSceneKawaiiPhysicsSettingsOverrideSection* NewSection()
{
	return NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(GetTransientPackage());
}

void SetupLinearEaseIn(UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section)
{
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));
	Section->Easing.bManualEaseIn = true;
	Section->Easing.ManualEaseInDuration = 200;

	UMovieSceneBuiltInEasingFunction* LinearEase = NewObject<UMovieSceneBuiltInEasingFunction>(Section);
	LinearEase->Type = EMovieSceneBuiltInEasing::Linear;
	Section->Easing.EaseIn.SetObject(LinearEase);
	Section->Easing.EaseIn.SetInterface(static_cast<IMovieSceneEasingFunction*>(LinearEase));
}

FKawaiiPhysicsSettingsScale MakeScale()
{
	FKawaiiPhysicsSettingsScale Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 0.25f;
	Scale.WorldDampingLocation = 0.75f;
	Scale.WorldDampingRotation = 0.8f;
	Scale.Radius = 1.5f;
	Scale.LimitAngle = 0.6f;
	return Scale;
}

bool TestScaleEqual(FAutomationTestBase& Test, const FKawaiiPhysicsSettingsScale& Actual,
                    const FKawaiiPhysicsSettingsScale& Expected)
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightEasingTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_Easing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightEasingTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
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
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
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
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
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
	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(Track->CreateNewSection());

	const FKawaiiPhysicsSettingsScale ExpectedScale = MakeScale();
	Section->Scale = ExpectedScale;
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
	                FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate::StaticStruct());

	const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate* Template =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate*>(TemplatePtr.GetPtr());
	bOk &= TestScaleEqual(*this, Template->Scale, ExpectedScale);
	bOk &= TestTrue(TEXT("FilterTags copied"), Template->FilterTags == Section->FilterTags);
	bOk &= TestTrue(TEXT("Exact copied"), Template->bFilterExactMatch);
	bOk &= TestFloatNear(*this, TEXT("BlendOut copied"), Template->BlendOutTimeOnEnd, 0.75f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerChannelProxyAfterNewAndDuplicateTest,
                                 "KawaiiPhysics.Sequencer.Section.ChannelProxy_AfterNewAndDuplicate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerChannelProxyAfterNewAndDuplicateTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	bool bOk = TestEqual(TEXT("New channel count"),
	                     Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num(), 1);

	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Duplicate =
		DuplicateObject<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(Section, GetTransientPackage());
	bOk &= TestEqual(TEXT("Duplicate channel count"),
	                 Duplicate->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num(), 1);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopForSectionTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopForSection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopForSectionTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionA = NewSection();
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionB = NewSection();

	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryOther = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionA, EntryB);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionB, EntryOther);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	bOk &= TestFalse(TEXT("Other section untouched"), EntryOther->bStopped);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionA);
	bOk &= TestTrue(TEXT("EntryA still stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("Other section still untouched"), EntryOther->bStopped);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionB);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRegisterIdempotentTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RegisterIdempotent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRegisterIdempotentTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry);

	bool bOk = TestEqual(TEXT("Single registered entry"),
	                     FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	bOk &= TestTrue(TEXT("Entry stopped"), Entry->bStopped);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEntryStopIdempotentTest,
                                 "KawaiiPhysics.Sequencer.Section.Entry_StopIdempotent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEntryStopIdempotentTest::RunTest(const FString& Parameters)
{
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	Entry->Stop();
	bool bOk = TestTrue(TEXT("Entry stopped"), Entry->bStopped);
	Entry->Stop();
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerSectionDefaultsTest,
                                 "KawaiiPhysics.Sequencer.Section.Section_Defaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerSectionDefaultsTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());

	const FOptionalMovieSceneBlendType BlendType = Section->GetBlendType();
	bool bOk = TestTrue(TEXT("Section blend valid"), BlendType.IsValid());
	bOk &= TestTrue(TEXT("Section blend absolute"), BlendType.IsValid() && BlendType.Get() == EMovieSceneBlendType::Absolute);
	bOk &= TestTrue(TEXT("Completion mode"), Section->GetCompletionMode() == EMovieSceneCompletionMode::RestoreState);
	bOk &= TestFalse(TEXT("Completion mode locked"), Section->EvalOptions.bCanEditCompletionMode);
	bOk &= TestTrue(TEXT("Track supports absolute"),
	                Track->GetSupportedBlendTypes().Contains(EMovieSceneBlendType::Absolute));
	return bOk;
}

#endif
