// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsPresetDiffSnapshot.h"

#include "KawaiiPhysicsEditorLibrary.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	static FString GetNodePropertyValueAsString(const FProperty* Property, const FAnimNode_KawaiiPhysics& Container)
	{
		FString ValueText;
		if (!Property)
		{
			return ValueText;
		}

#if UE_VERSION_OLDER_THAN(5, 1, 0)
		if (const void* NodeValuePtr = Property->ContainerPtrToValuePtr<void>(&Container))
		{
			Property->ExportTextItem(ValueText, NodeValuePtr, nullptr, nullptr, PPF_None);
		}
#else
		Property->ExportText_InContainer(0, ValueText, &Container, &Container, nullptr, PPF_None);
#endif
		return ValueText;
	}

	static FString GetPropertyCategory(const FProperty* Property)
	{
		if (!Property)
		{
			return FString();
		}

#if WITH_METADATA
		return Property->HasMetaData(TEXT("Category")) ? Property->GetMetaData(TEXT("Category")) : FString();
#else
		return FString();
#endif
	}

	static FString MakeClipboardCell(FString Value)
	{
		Value.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\t"), TEXT(" "));
		return Value;
	}
}

namespace KawaiiPhysicsPresetDiff
{
	TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> BuildSnapshot(const FAnimNode_KawaiiPhysics& Node,
	                                                          const UKawaiiPhysicsPresetDataAsset& Preset,
	                                                          const FKawaiiPhysicsPresetApplyOptions& Options)
	{
		TArray<FName> DiffProperties;
		TArray<FName> ComparedProperties;
		const bool bMatches = Preset.MatchesNode(Node, Options, DiffProperties, &ComparedProperties);

		TSet<FName> DiffPropertySet;
		DiffPropertySet.Reserve(DiffProperties.Num());
		for (const FName& DiffProperty : DiffProperties)
		{
			DiffPropertySet.Add(DiffProperty);
		}

		TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> Snapshot = MakeShared<FKawaiiPhysicsPresetDiffSnapshot>();
		Snapshot->PresetDisplayName = FText::FromString(Preset.GetName());
		Snapshot->PresetPath = FSoftObjectPath(&Preset);
		Snapshot->bMatches = bMatches;
		Snapshot->Rows.Reserve(ComparedProperties.Num());

		for (const FName& PropertyName : ComparedProperties)
		{
			const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
			if (!Property)
			{
				continue;
			}

			TSharedRef<FKawaiiPhysicsPresetDiffPropertyRow> Row = MakeShared<FKawaiiPhysicsPresetDiffPropertyRow>();
			Row->PropertyName = PropertyName;
			Row->DisplayName = Property->GetDisplayNameText();
			Row->Category = GetPropertyCategory(Property);
			Row->NodeValue = GetNodePropertyValueAsString(Property, Node);
			Row->PresetValue = GetNodePropertyValueAsString(Property, Preset.Node);
			Row->bDiffers = DiffPropertySet.Contains(PropertyName);
			if (Row->bDiffers)
			{
				++Snapshot->DiffCount;
			}
			Snapshot->Rows.Add(Row);
		}

		return Snapshot;
	}

	TArray<TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>> BuildSnapshotsForNode(
		const FAnimNode_KawaiiPhysics& Node,
		const FKawaiiPhysicsPresetApplyOptions& Options)
	{
		TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>> Presets;
		UKawaiiPhysicsEditorLibrary::FindAllPresetAssetData(Presets);

		TArray<TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>> Snapshots;
		for (const TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>& PresetPtr : Presets)
		{
			const UKawaiiPhysicsPresetDataAsset* Preset = PresetPtr.Get();
			if (Preset && Preset->TargetsNodeTag(Node.KawaiiPhysicsTag))
			{
				Snapshots.Add(BuildSnapshot(Node, *Preset, Options));
			}
		}
		return Snapshots;
	}

	FString MakeClipboardTextFromSnapshot(const FKawaiiPhysicsPresetDiffSnapshot& Snapshot, const FText& ContextLabel)
	{
		TArray<FString> Lines;
		Lines.Add(TEXT("Context\tPreset\tPresetPath\tPropertyName\tDisplayName\tCategory\tNodeValue\tPresetValue"));

		const FString ContextText = MakeClipboardCell(ContextLabel.ToString());
		const FString PresetText = MakeClipboardCell(Snapshot.PresetDisplayName.ToString());
		const FString PresetPathText = MakeClipboardCell(Snapshot.PresetPath.ToString());
		for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& Row : Snapshot.Rows)
		{
			if (!Row.IsValid() || !Row->bDiffers)
			{
				continue;
			}

			Lines.Add(FString::Printf(
				TEXT("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s"),
				*ContextText,
				*PresetText,
				*PresetPathText,
				*MakeClipboardCell(Row->PropertyName.ToString()),
				*MakeClipboardCell(Row->DisplayName.ToString()),
				*MakeClipboardCell(Row->Category),
				*MakeClipboardCell(Row->NodeValue),
				*MakeClipboardCell(Row->PresetValue)));
		}

		return FString::Join(Lines, TEXT("\n"));
	}

	TArray<FKawaiiPhysicsPresetDiffValue> BuildDiffValues(const FAnimNode_KawaiiPhysics& Node,
	                                                      const UKawaiiPhysicsPresetDataAsset& Preset,
	                                                      const FKawaiiPhysicsPresetApplyOptions& Options)
	{
		TArray<FName> DiffProperties;
		Preset.MatchesNode(Node, Options, DiffProperties);

		TArray<FKawaiiPhysicsPresetDiffValue> DiffValues;
		DiffValues.Reserve(DiffProperties.Num());
		for (const FName& PropertyName : DiffProperties)
		{
			const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
			if (!Property)
			{
				continue;
			}

			FKawaiiPhysicsPresetDiffValue DiffValue;
			DiffValue.PropertyName = PropertyName;
			DiffValue.NodeValue = GetNodePropertyValueAsString(Property, Node);
			DiffValue.PresetValue = GetNodePropertyValueAsString(Property, Preset.Node);
			DiffValues.Add(MoveTemp(DiffValue));
		}

		return DiffValues;
	}
}
