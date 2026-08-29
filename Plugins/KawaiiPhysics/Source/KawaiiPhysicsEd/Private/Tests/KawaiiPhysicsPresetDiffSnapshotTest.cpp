#if WITH_DEV_AUTOMATION_TESTS

#include "../KawaiiPhysicsPresetDiffSnapshot.h"

#include "KawaiiPhysicsWindPresetTags.h"
#include "Misc/AutomationTest.h"

// UncookedOnlyモジュールではネイティブタグを定義できない（NativeGameplayTags.cppのensure対象）ため、
// Runtimeモジュールが登録済みのタグを流用する。テストに必要なのは相異なる有効タグ2つのみ

namespace
{
	UKawaiiPhysicsPresetDataAsset* MakeCopiedPreset(const FAnimNode_KawaiiPhysics& Node)
	{
		UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
		Preset->CopyFromNode(Node);
		return Preset;
	}

	FSphericalLimit MakeSnapshotSphereLimit()
	{
		FSphericalLimit Limit;
		Limit.DrivingBone = FBoneReference(TEXT("spine_01"));
		Limit.Location = FVector(1.0f, 2.0f, 3.0f);
		Limit.Radius = 9.0f;
		Limit.LimitType = ESphericalLimitType::Inner;
		Limit.bEnable = true;
		return Limit;
	}

	const FKawaiiPhysicsPresetDiffPropertyRow* FindSnapshotRow(
		const FKawaiiPhysicsPresetDiffSnapshot& Snapshot,
		const FName PropertyName)
	{
		for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& Row : Snapshot.Rows)
		{
			if (Row.IsValid() && Row->PropertyName == PropertyName)
			{
				return Row.Get();
			}
		}

		return nullptr;
	}

	int32 CountDifferingRows(const FKawaiiPhysicsPresetDiffSnapshot& Snapshot)
	{
		int32 Count = 0;
		for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& Row : Snapshot.Rows)
		{
			if (Row.IsValid() && Row->bDiffers)
			{
				++Count;
			}
		}
		return Count;
	}

	bool HasDifferingRows(const FKawaiiPhysicsPresetDiffSnapshot& Snapshot)
	{
		return CountDifferingRows(Snapshot) > 0;
	}

	TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> MakePhysicsSettingsDiffSnapshot(
		FAnimNode_KawaiiPhysics& OutNode,
		UKawaiiPhysicsPresetDataAsset*& OutPreset)
	{
		OutNode = FAnimNode_KawaiiPhysics();
		OutPreset = MakeCopiedPreset(OutNode);
		OutNode.PhysicsSettings.Damping += 0.25f;

		const FKawaiiPhysicsPresetApplyOptions Options;
		return KawaiiPhysicsPresetDiff::BuildSnapshot(OutNode, *OutPreset, Options);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotMatchesTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.Matches",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotMatchesTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	UKawaiiPhysicsPresetDataAsset* Preset = MakeCopiedPreset(Node);
	TestNotNull(TEXT("Preset is created"), Preset);
	if (!Preset)
	{
		return false;
	}

	const FKawaiiPhysicsPresetApplyOptions Options;
	const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> Snapshot =
		KawaiiPhysicsPresetDiff::BuildSnapshot(Node, *Preset, Options);

	bool bOk = true;
	bOk &= TestTrue(TEXT("Snapshot matches immediately after CopyFromNode"), Snapshot->bMatches);
	bOk &= TestEqual(TEXT("Snapshot has no diffs"), Snapshot->DiffCount, 0);
	bOk &= TestFalse(TEXT("Snapshot rows are not empty"), Snapshot->Rows.IsEmpty());
	bOk &= TestFalse(TEXT("No row differs"), HasDifferingRows(*Snapshot));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotPhysicsSettingsDiffTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.PhysicsSettingsDiff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotPhysicsSettingsDiffTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	UKawaiiPhysicsPresetDataAsset* Preset = nullptr;
	const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> Snapshot = MakePhysicsSettingsDiffSnapshot(Node, Preset);
	TestNotNull(TEXT("Preset is created"), Preset);
	if (!Preset)
	{
		return false;
	}

	const FName PhysicsSettingsName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsSettings);
	const FKawaiiPhysicsPresetDiffPropertyRow* PhysicsSettingsRow = FindSnapshotRow(*Snapshot, PhysicsSettingsName);

	bool bOk = true;
	bOk &= TestFalse(TEXT("Snapshot does not match after PhysicsSettings change"), Snapshot->bMatches);
	bOk &= TestEqual(TEXT("Snapshot has one diff"), Snapshot->DiffCount, 1);
	bOk &= TestEqual(TEXT("Exactly one row differs"), CountDifferingRows(*Snapshot), 1);
	bOk &= TestNotNull(TEXT("PhysicsSettings row exists"), PhysicsSettingsRow);
	if (PhysicsSettingsRow)
	{
		bOk &= TestTrue(TEXT("PhysicsSettings row differs"), PhysicsSettingsRow->bDiffers);
		bOk &= TestEqual(TEXT("PhysicsSettings row has expected property name"),
		                  PhysicsSettingsRow->PropertyName,
		                  PhysicsSettingsName);
		bOk &= TestFalse(TEXT("PhysicsSettings node value is not empty"), PhysicsSettingsRow->NodeValue.IsEmpty());
		bOk &= TestFalse(TEXT("PhysicsSettings preset value is not empty"), PhysicsSettingsRow->PresetValue.IsEmpty());
		bOk &= TestFalse(TEXT("PhysicsSettings values differ"),
		                  PhysicsSettingsRow->NodeValue == PhysicsSettingsRow->PresetValue);
		bOk &= TestFalse(TEXT("PhysicsSettings display name is not empty"),
		                  PhysicsSettingsRow->DisplayName.ToString().IsEmpty());
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotDiffValuesTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.DiffValues",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotDiffValuesTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics MatchingNode;
	UKawaiiPhysicsPresetDataAsset* MatchingPreset = MakeCopiedPreset(MatchingNode);
	TestNotNull(TEXT("Matching preset is created"), MatchingPreset);
	if (!MatchingPreset)
	{
		return false;
	}

	const FKawaiiPhysicsPresetApplyOptions Options;
	bool bOk = true;
	bOk &= TestTrue(TEXT("BuildDiffValues is empty immediately after CopyFromNode"),
	                KawaiiPhysicsPresetDiff::BuildDiffValues(MatchingNode, *MatchingPreset, Options).IsEmpty());

	FAnimNode_KawaiiPhysics DiffNode;
	UKawaiiPhysicsPresetDataAsset* DiffPreset = nullptr;
	MakePhysicsSettingsDiffSnapshot(DiffNode, DiffPreset);
	TestNotNull(TEXT("Diff preset is created"), DiffPreset);
	if (!DiffPreset)
	{
		return false;
	}

	const TArray<FKawaiiPhysicsPresetDiffValue> DiffValues =
		KawaiiPhysicsPresetDiff::BuildDiffValues(DiffNode, *DiffPreset, Options);

	const FName PhysicsSettingsName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsSettings);
	const FKawaiiPhysicsPresetDiffValue* PhysicsSettingsDiffValue = DiffValues.FindByPredicate(
		[PhysicsSettingsName](const FKawaiiPhysicsPresetDiffValue& Value)
		{
			return Value.PropertyName == PhysicsSettingsName;
		});

	bOk &= TestEqual(TEXT("Exactly one diff value after PhysicsSettings change"), DiffValues.Num(), 1);
	bOk &= TestNotNull(TEXT("PhysicsSettings diff value exists"), PhysicsSettingsDiffValue);
	if (PhysicsSettingsDiffValue)
	{
		bOk &= TestFalse(TEXT("PhysicsSettings node value is not empty"), PhysicsSettingsDiffValue->NodeValue.IsEmpty());
		bOk &= TestFalse(TEXT("PhysicsSettings preset value is not empty"), PhysicsSettingsDiffValue->PresetValue.IsEmpty());
		bOk &= TestFalse(TEXT("PhysicsSettings values differ"),
		                 PhysicsSettingsDiffValue->NodeValue == PhysicsSettingsDiffValue->PresetValue);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotOptionsExcludeTagTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.OptionsExcludeTag",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotOptionsExcludeTagTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	UKawaiiPhysicsPresetDataAsset* Preset = MakeCopiedPreset(Node);
	TestNotNull(TEXT("Preset is created"), Preset);
	if (!Preset)
	{
		return false;
	}

	Node.KawaiiPhysicsTag = TAG_KawaiiPhysics_WindPreset_Breeze;
	Preset->Node.KawaiiPhysicsTag = TAG_KawaiiPhysics_WindPreset_Strong;

	const FKawaiiPhysicsPresetApplyOptions Options;
	const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> Snapshot =
		KawaiiPhysicsPresetDiff::BuildSnapshot(Node, *Preset, Options);

	bool bOk = true;
	bOk &= TestTrue(TEXT("Different native test tags are valid"),
	                Node.KawaiiPhysicsTag.IsValid() && Preset->Node.KawaiiPhysicsTag.IsValid());
	bOk &= TestFalse(TEXT("Directly assigned tags differ"), Node.KawaiiPhysicsTag == Preset->Node.KawaiiPhysicsTag);
	bOk &= TestTrue(TEXT("Tag difference is ignored by default options"), Snapshot->bMatches);
	bOk &= TestEqual(TEXT("Ignored tag difference does not increase diff count"), Snapshot->DiffCount, 0);
	bOk &= TestNull(TEXT("KawaiiPhysicsTag row is not compared by default options"),
	                FindSnapshotRow(*Snapshot, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag)));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotArrayPropertyTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.ArrayProperty",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotArrayPropertyTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	UKawaiiPhysicsPresetDataAsset* Preset = MakeCopiedPreset(Node);
	TestNotNull(TEXT("Preset is created"), Preset);
	if (!Preset)
	{
		return false;
	}

	Node.SphericalLimits.Add(MakeSnapshotSphereLimit());

	const FKawaiiPhysicsPresetApplyOptions Options;
	const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> Snapshot =
		KawaiiPhysicsPresetDiff::BuildSnapshot(Node, *Preset, Options);

	const FName SphericalLimitsName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimits);
	const FKawaiiPhysicsPresetDiffPropertyRow* SphericalLimitsRow = FindSnapshotRow(*Snapshot, SphericalLimitsName);

	bool bOk = true;
	bOk &= TestFalse(TEXT("Snapshot does not match after SphericalLimits change"), Snapshot->bMatches);
	bOk &= TestEqual(TEXT("Snapshot has one SphericalLimits diff"), Snapshot->DiffCount, 1);
	bOk &= TestNotNull(TEXT("SphericalLimits row exists"), SphericalLimitsRow);
	if (SphericalLimitsRow)
	{
		bOk &= TestTrue(TEXT("SphericalLimits row differs"), SphericalLimitsRow->bDiffers);
		bOk &= TestFalse(TEXT("SphericalLimits node value is not empty"), SphericalLimitsRow->NodeValue.IsEmpty());
		// 空配列のExportTextは空文字列になるため、プリセット側（空配列）は空を許容し両値の相違のみ検証する
		bOk &= TestTrue(TEXT("SphericalLimits values differ"),
		                SphericalLimitsRow->NodeValue != SphericalLimitsRow->PresetValue);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotClipboardTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.Clipboard",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotClipboardTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	UKawaiiPhysicsPresetDataAsset* Preset = nullptr;
	const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> Snapshot = MakePhysicsSettingsDiffSnapshot(Node, Preset);
	TestNotNull(TEXT("Preset is created"), Preset);
	if (!Preset)
	{
		return false;
	}

	const FName PhysicsSettingsName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsSettings);
	const FKawaiiPhysicsPresetDiffPropertyRow* PhysicsSettingsRow = FindSnapshotRow(*Snapshot, PhysicsSettingsName);
	TestNotNull(TEXT("PhysicsSettings row exists"), PhysicsSettingsRow);
	if (!PhysicsSettingsRow)
	{
		return false;
	}

	const FText ContextLabel = FText::FromString(TEXT("PresetDiffSnapshotClipboardContext"));
	const FString ClipboardText = KawaiiPhysicsPresetDiff::MakeClipboardTextFromSnapshot(*Snapshot, ContextLabel);

	bool bOk = true;
	bOk &= TestTrue(TEXT("Clipboard text contains header"),
	                ClipboardText.Contains(TEXT("Context\tPreset\tPresetPath\tPropertyName\tDisplayName\tCategory\tNodeValue\tPresetValue")));
	bOk &= TestTrue(TEXT("Clipboard text contains context label"),
	                ClipboardText.Contains(ContextLabel.ToString()));
	bOk &= TestTrue(TEXT("Clipboard text contains PhysicsSettings"),
	                ClipboardText.Contains(PhysicsSettingsName.ToString()));
	bOk &= TestTrue(TEXT("Clipboard text contains node value"),
	                ClipboardText.Contains(PhysicsSettingsRow->NodeValue));
	bOk &= TestTrue(TEXT("Clipboard text contains preset value"),
	                ClipboardText.Contains(PhysicsSettingsRow->PresetValue));
	bOk &= TestFalse(TEXT("Clipboard text omits unchanged TeleportDistanceThreshold"),
	                 ClipboardText.Contains(GET_MEMBER_NAME_CHECKED(
		                 FAnimNode_KawaiiPhysics,
		                 TeleportDistanceThreshold).ToString()));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotComparedPropertiesTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.ComparedProperties",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotComparedPropertiesTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	UKawaiiPhysicsPresetDataAsset* Preset = MakeCopiedPreset(Node);
	TestNotNull(TEXT("Preset is created"), Preset);
	if (!Preset)
	{
		return false;
	}

	const FKawaiiPhysicsPresetApplyOptions Options;
	TArray<FName> DiffProperties;
	TArray<FName> ComparedProperties;
	const bool bMatches = Preset->MatchesNode(Node, Options, DiffProperties, &ComparedProperties);

	bool bOk = true;
	bOk &= TestTrue(TEXT("Copied node matches preset"), bMatches);
	bOk &= TestTrue(TEXT("Compared property count is broad enough"), ComparedProperties.Num() >= 30);
	bOk &= TestFalse(TEXT("DeltaTime is not compared"),
	                 ComparedProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DeltaTime)));
	return bOk;
}

#endif
