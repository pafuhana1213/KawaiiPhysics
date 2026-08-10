// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsAuditCommandlet.h"

#include "KawaiiPhysics.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagsManager.h"
#include "JsonObjectConverter.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsAuditCommandlet)

namespace
{
	void ParseCommaSeparatedParam(const FString& Value, TArray<FString>& OutValues)
	{
		OutValues.Reset();

		TArray<FString> Tokens;
		Value.ParseIntoArray(Tokens, TEXT(","), true);
		for (FString& Token : Tokens)
		{
			Token.TrimStartAndEndInline();
			if (!Token.IsEmpty())
			{
				OutValues.Add(Token);
			}
		}
	}

	FGameplayTagContainer ParseFilterTags(const FString& Value)
	{
		FGameplayTagContainer Result;

		TArray<FString> TagNames;
		ParseCommaSeparatedParam(Value, TagNames);

		UGameplayTagsManager& GameplayTagsManager = UGameplayTagsManager::Get();
		for (const FString& TagName : TagNames)
		{
			const FGameplayTag Tag = GameplayTagsManager.RequestGameplayTag(FName(*TagName), false);
			if (Tag.IsValid())
			{
				Result.AddTag(Tag);
			}
			else
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("KawaiiPhysicsAudit: FilterTags contains unknown tag '%s'."),
				       *TagName);
			}
		}

		return Result;
	}

	bool IsPresetDriftEntry(const FKawaiiPhysicsNodeAuditEntry& Entry)
	{
		return Entry.MatchedPresetPath.IsValid() && !Entry.bMatchesPreset;
	}

	int32 CountPresetDriftEntries(const TArray<FKawaiiPhysicsNodeAuditEntry>& Entries)
	{
		int32 Result = 0;
		for (const FKawaiiPhysicsNodeAuditEntry& Entry : Entries)
		{
			if (IsPresetDriftEntry(Entry))
			{
				++Result;
			}
		}

		return Result;
	}

	void ConfigureAnimBlueprintFilter(FARFilter& Filter, const TArray<FString>& ContentPaths)
	{
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		Filter.ClassNames.Add(UAnimBlueprint::StaticClass()->GetFName());
#else
		Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
#endif

		if (ContentPaths.IsEmpty())
		{
			Filter.PackagePaths.Add(FName(TEXT("/Game")));
			return;
		}

		for (const FString& ContentPath : ContentPaths)
		{
			if (!ContentPath.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*ContentPath));
			}
		}
	}

	int32 CountAnimBlueprintAssets(const TArray<FString>& ContentPaths)
	{
		FARFilter Filter;
		ConfigureAnimBlueprintFilter(Filter, ContentPaths);

		TArray<FAssetData> AnimBlueprintAssets;
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().GetAssets(Filter, AnimBlueprintAssets);
		return AnimBlueprintAssets.Num();
	}

	bool WriteAuditJsonFile(
		const FString& OutputPath,
		const TArray<FKawaiiPhysicsNodeAuditEntry>& Entries,
		int32 TotalAnimBlueprints)
	{
		TSharedPtr<FJsonObject> RootObject = KawaiiPhysicsAuditJson::MakeAuditJsonObject(Entries, TotalAnimBlueprints);
		if (!RootObject.IsValid())
		{
			return false;
		}

		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
		{
			return false;
		}

		return FFileHelper::SaveStringToFile(OutputString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void LogAuditEntries(const TArray<FKawaiiPhysicsNodeAuditEntry>& Entries)
	{
		for (const FKawaiiPhysicsNodeAuditEntry& Entry : Entries)
		{
			TArray<FString> DiffPropertyNames;
			DiffPropertyNames.Reserve(Entry.DiffProperties.Num());
			for (const FName DiffProperty : Entry.DiffProperties)
			{
				DiffPropertyNames.Add(DiffProperty.ToString());
			}

			UE_LOG(LogKawaiiPhysics, Display,
			       TEXT("KawaiiPhysicsAudit: ABP=%s Graph=%s RootBone=%s Tag=%s MatchedPreset=%s MatchesPreset=%s MatchedCount=%d DiffProperties=%s BoneSubdivision=%d BoneConstraintSubdivision=%d WorldCollision=%s UseSharedCollision=%s SharedCollisionSource=%s Wind=%s ExternalForces=%d WarmUpFrames=%d"),
			       *Entry.AnimBlueprintPath.ToString(),
			       *Entry.GraphName.ToString(),
			       *Entry.RootBoneName.ToString(),
			       *Entry.KawaiiPhysicsTag.ToString(),
			       *Entry.MatchedPresetPath.ToString(),
			       Entry.bMatchesPreset ? TEXT("true") : TEXT("false"),
			       Entry.MatchedPresetCount,
			       *FString::Join(DiffPropertyNames, TEXT(",")),
			       Entry.BoneSubdivisionCount,
			       Entry.BoneConstraintSubdivisionCount,
			       Entry.bAllowWorldCollision ? TEXT("true") : TEXT("false"),
			       Entry.bUseSharedCollision ? TEXT("true") : TEXT("false"),
			       Entry.bSharedCollisionSource ? TEXT("true") : TEXT("false"),
			       Entry.bEnableWind ? TEXT("true") : TEXT("false"),
			       Entry.ExternalForceCount,
			       Entry.WarmUpFrames);
		}
	}
}

namespace KawaiiPhysicsAuditJson
{
	TSharedPtr<FJsonObject> MakeAuditJsonObject(
		const TArray<FKawaiiPhysicsNodeAuditEntry>& Entries,
		int32 TotalAnimBlueprints)
	{
		const int32 PresetDriftCount = CountPresetDriftEntries(Entries);

		TSharedPtr<FJsonObject> SummaryObject = MakeShared<FJsonObject>();
		SummaryObject->SetNumberField(TEXT("TotalAnimBlueprints"), TotalAnimBlueprints);
		SummaryObject->SetNumberField(TEXT("TotalNodes"), Entries.Num());
		SummaryObject->SetNumberField(TEXT("PresetDriftCount"), PresetDriftCount);

		TArray<TSharedPtr<FJsonValue>> EntryValues;
		EntryValues.Reserve(Entries.Num());
		for (const FKawaiiPhysicsNodeAuditEntry& Entry : Entries)
		{
			TSharedPtr<FJsonObject> EntryObject = FJsonObjectConverter::UStructToJsonObject(Entry);
			if (EntryObject.IsValid())
			{
				EntryValues.Add(MakeShared<FJsonValueObject>(EntryObject));
			}
		}

		TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetObjectField(TEXT("Summary"), SummaryObject);
		RootObject->SetArrayField(TEXT("Entries"), EntryValues);
		return RootObject;
	}
}

UKawaiiPhysicsAuditCommandlet::UKawaiiPhysicsAuditCommandlet()
{
	IsClient = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UKawaiiPhysicsAuditCommandlet::Main(const FString& Params)
{
	FString ContentPathsParam;
	TArray<FString> ContentPaths;
	if (FParse::Value(*Params, TEXT("ContentPaths="), ContentPathsParam))
	{
		ParseCommaSeparatedParam(ContentPathsParam, ContentPaths);
	}

	FString FilterTagsParam;
	FGameplayTagContainer FilterTags;
	if (FParse::Value(*Params, TEXT("FilterTags="), FilterTagsParam))
	{
		FilterTags = ParseFilterTags(FilterTagsParam);
	}

	const bool bFilterExactMatch =
		FParse::Param(*Params, TEXT("Exact")) ||
		FParse::Param(*Params, TEXT("FilterExactMatch"));

	const bool bIncludeDiffValues = FParse::Param(*Params, TEXT("IncludeDiffValues"));

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().SearchAllAssets(true);

	const int32 TotalAnimBlueprints = CountAnimBlueprintAssets(ContentPaths);

	TArray<FKawaiiPhysicsNodeAuditEntry> Entries;
	if (!UKawaiiPhysicsEditorLibrary::AuditKawaiiPhysicsNodes(
		ContentPaths,
		FilterTags,
		bFilterExactMatch,
		Entries,
		bIncludeDiffValues))
	{
		UE_LOG(LogKawaiiPhysics, Error, TEXT("KawaiiPhysicsAudit: Audit failed."));
		return 2;
	}

	const int32 PresetDriftCount = CountPresetDriftEntries(Entries);
	UE_LOG(LogKawaiiPhysics, Display,
	       TEXT("KawaiiPhysicsAudit: AnimBlueprints=%d Nodes=%d PresetDrifts=%d"),
	       TotalAnimBlueprints,
	       Entries.Num(),
	       PresetDriftCount);

	FString OutputPath;
	if (FParse::Value(*Params, TEXT("Output="), OutputPath) && !OutputPath.IsEmpty())
	{
		if (!WriteAuditJsonFile(OutputPath, Entries, TotalAnimBlueprints))
		{
			UE_LOG(LogKawaiiPhysics, Error,
			       TEXT("KawaiiPhysicsAudit: Failed to write JSON file '%s'."),
			       *OutputPath);
			return 2;
		}

		UE_LOG(LogKawaiiPhysics, Display,
		       TEXT("KawaiiPhysicsAudit: Wrote JSON file '%s'."),
		       *OutputPath);
	}
	else
	{
		LogAuditEntries(Entries);
	}

	if (PresetDriftCount > 0)
	{
		return 1;
	}

	return 0;
}
