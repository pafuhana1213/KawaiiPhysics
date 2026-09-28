#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsPresetDiffSnapshot.h"

#include "Misc/AutomationTest.h"

namespace
{
	UKawaiiPhysicsPresetDataAsset* MakeCopiedPreset(const FAnimNode_KawaiiPhysics& Node)
	{
		UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
		Preset->CopyFromNode(Node);
		return Preset;
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
	// コピー直後の一致と、比較対象の広さ・除外プロパティを確認する
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
	bOk &= TestTrue(TEXT("Snapshot compares at least 30 rows"), Snapshot->Rows.Num() >= 30);
	bOk &= TestFalse(TEXT("No row differs"), HasDifferingRows(*Snapshot));
	// 一致時の比較範囲とランタイム専用プロパティの除外を確認する
	TArray<FName> DiffProperties;
	TArray<FName> ComparedProperties;
	const bool bMatches = Preset->MatchesNode(Node, Options, DiffProperties, &ComparedProperties);

	bOk &= TestTrue(TEXT("Copied node matches preset"), bMatches);
	bOk &= TestTrue(TEXT("Compared property count is broad enough"), ComparedProperties.Num() >= 30);
	bOk &= TestFalse(TEXT("DeltaTime is not compared"),
	                 ComparedProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DeltaTime)));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetDiffSnapshotPhysicsSettingsDiffTest,
                                 "KawaiiPhysics.Editor.PresetDiffSnapshot.PhysicsSettingsDiff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetDiffSnapshotPhysicsSettingsDiffTest::RunTest(const FString& Parameters)
{
	// PhysicsSettings の差分値とクリップボード出力を確認する
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
		bOk &= TestFalse(TEXT("PhysicsSettings values differ"),
		                  PhysicsSettingsRow->NodeValue == PhysicsSettingsRow->PresetValue);
	}
	// 一致時は値差分が空で、変更時は 1 件の差分になる
	{
		FAnimNode_KawaiiPhysics MatchingNode;
		UKawaiiPhysicsPresetDataAsset* MatchingPreset = MakeCopiedPreset(MatchingNode);
		TestNotNull(TEXT("Matching preset is created"), MatchingPreset);
		if (!MatchingPreset)
		{
			return false;
		}

		const FKawaiiPhysicsPresetApplyOptions Options;

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
	}

	// 変更した行だけがクリップボードへ出力される
	{
		FAnimNode_KawaiiPhysics ClipboardNode;
		UKawaiiPhysicsPresetDataAsset* ClipboardPreset = nullptr;
		const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> ClipboardSnapshot =
			MakePhysicsSettingsDiffSnapshot(ClipboardNode, ClipboardPreset);
		TestNotNull(TEXT("Preset is created"), ClipboardPreset);
		if (!ClipboardPreset)
		{
			return false;
		}

		const FKawaiiPhysicsPresetDiffPropertyRow* ClipboardRow = FindSnapshotRow(*ClipboardSnapshot, PhysicsSettingsName);
		TestNotNull(TEXT("PhysicsSettings row exists"), ClipboardRow);
		if (!ClipboardRow)
		{
			return false;
		}

		const FText ContextLabel = FText::FromString(TEXT("PresetDiffSnapshotClipboardContext"));
		const FString ClipboardText = KawaiiPhysicsPresetDiff::MakeClipboardTextFromSnapshot(*ClipboardSnapshot, ContextLabel);

		bOk &= TestTrue(TEXT("Clipboard text contains header"),
		                ClipboardText.Contains(TEXT("Context\tPreset\tPresetPath\tPropertyName\tDisplayName\tCategory\tNodeValue\tPresetValue")));
		bOk &= TestTrue(TEXT("Clipboard text contains context label"),
		                ClipboardText.Contains(ContextLabel.ToString()));
		bOk &= TestTrue(TEXT("Clipboard text contains PhysicsSettings"),
		                ClipboardText.Contains(PhysicsSettingsName.ToString()));
		bOk &= TestTrue(TEXT("Clipboard text contains node value"),
		                ClipboardText.Contains(ClipboardRow->NodeValue));
		bOk &= TestTrue(TEXT("Clipboard text contains preset value"),
		                ClipboardText.Contains(ClipboardRow->PresetValue));
		bOk &= TestFalse(TEXT("Clipboard text omits unchanged TeleportDistanceThreshold"),
		                 ClipboardText.Contains(GET_MEMBER_NAME_CHECKED(
			                 FAnimNode_KawaiiPhysics,
			                 TeleportDistanceThreshold).ToString()));
	}
	return bOk;
}

#endif
