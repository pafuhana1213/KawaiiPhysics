// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MovieSceneKawaiiPhysicsSettingsOverrideTrack.h"
#include "Sequencer/KawaiiPhysicsSettingsOverrideSectionSummary.h"

#include "Misc/AutomationTest.h"
#include "MovieSceneTrack.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScaleSummaryNoChangeTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScaleSummary_NoChange",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScaleSummaryNoChangeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FKawaiiPhysicsSettingsScale Scale;
	return TestEqual(
		TEXT("全倍率が 1.0 の場合は変更なし表示になること"),
		MakeKawaiiPhysicsScaleSummaryText(Scale).ToString(),
		FString(TEXT("×1.0 (no change)")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScaleSummaryPartialTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScaleSummary_Partial",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScaleSummaryPartialTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSettingsScale Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 1.2f;

	return TestEqual(
		TEXT("1.0 以外の倍率だけが順番通りに表示されること"),
		MakeKawaiiPhysicsScaleSummaryText(Scale).ToString(),
		FString(TEXT("D×0.50  S×1.20")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScaleSummaryAllTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScaleSummary_All",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScaleSummaryAllTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSettingsScale Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 1.2f;
	Scale.WorldDampingLocation = 0.8f;
	Scale.WorldDampingRotation = 0.7f;
	Scale.Radius = 1.5f;
	Scale.LimitAngle = 0.25f;

	return TestEqual(
		TEXT("6 成分が D, S, WL, WR, R, LA の順で表示されること"),
		MakeKawaiiPhysicsScaleSummaryText(Scale).ToString(),
		FString(TEXT("D×0.50  S×1.20  WL×0.80  WR×0.70  R×1.50  LA×0.25")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorSupportsTypeTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.SupportsType",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorSupportsTypeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		GetDefault<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>();

	bool bOk = true;
	bOk &= TestTrue(
		TEXT("Kawaii Physics Settings Override Track が UMovieSceneTrack 派生であること"),
		UMovieSceneKawaiiPhysicsSettingsOverrideTrack::StaticClass()->IsChildOf(UMovieSceneTrack::StaticClass()));
	bOk &= TestTrue(
		TEXT("Kawaii Physics Settings Override Track が複数行をサポートすること"),
		Track && Track->SupportsMultipleRows());
	return bOk;
}

#endif
