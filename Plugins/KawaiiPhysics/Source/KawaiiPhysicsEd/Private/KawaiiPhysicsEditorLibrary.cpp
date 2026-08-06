// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_Root.h"
#include "AnimationGraph.h"
#include "EdGraphSchema_K2.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintGameplayTagLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "GameplayTagsManager.h"
#include "Internationalization/Regex.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "UObject/NameTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsEditorLibrary)

namespace
{
	constexpr int32 KawaiiPhysicsEditorLibraryGCBatchSize = 20;
	constexpr int32 KawaiiPhysicsPlacementNodeOffsetX = -500;
	constexpr int32 KawaiiPhysicsPlacementNodeOffsetY = 300;

	UAnimGraphNode_KawaiiPhysics* GetGraphNode(const FKawaiiPhysicsGraphNodeHandle& Handle)
	{
		return Handle.Node.Get();
	}

	bool DoesNodeMatchTags(const FAnimNode_KawaiiPhysics& Node,
	                       const FGameplayTagContainer& FilterTags,
	                       bool bFilterExactMatch)
	{
		return FilterTags.IsEmpty() ||
			UBlueprintGameplayTagLibrary::MatchesAnyTags(Node.KawaiiPhysicsTag, FilterTags, bFilterExactMatch);
	}

	void MarkGraphNodeBlueprintModified(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		if (GraphNode)
		{
			if (UAnimBlueprint* AnimBlueprint = GraphNode->GetAnimBlueprint())
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
			}
		}
	}

	bool NotifyGraphNodePropertyChanged(UAnimGraphNode_KawaiiPhysics* GraphNode, const FProperty* ChangedProperty)
	{
		if (!GraphNode || !ChangedProperty)
		{
			return false;
		}

		FPropertyChangedEvent PropertyChangedEvent(const_cast<FProperty*>(ChangedProperty),
		                                           EPropertyChangeType::ValueSet);
		GraphNode->PostEditChangeProperty(PropertyChangedEvent);
		MarkGraphNodeBlueprintModified(GraphNode);
		return true;
	}

	bool MutateGraphNodeProperty(const FKawaiiPhysicsGraphNodeHandle& Handle,
	                             FName PropertyName,
	                             const FText& TransactionText,
	                             TFunctionRef<bool(UAnimGraphNode_KawaiiPhysics&)> Mutator)
	{
		UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
		const FProperty* ChangedProperty = FindFProperty<FProperty>(
			FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
		if (!GraphNode || !ChangedProperty)
		{
			return false;
		}

		FScopedTransaction Transaction(TransactionText);
		GraphNode->Modify();
		if (!Mutator(*GraphNode))
		{
			Transaction.Cancel();
			return false;
		}

		return NotifyGraphNodePropertyChanged(GraphNode, ChangedProperty);
	}

	void ConfigureAnimBlueprintFilter(FARFilter& Filter)
	{
		Filter.bRecursiveClasses = true;
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		Filter.ClassNames.Add(UAnimBlueprint::StaticClass()->GetFName());
#else
		Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
#endif
	}

	TArray<FString> NormalizeContentPaths(const TArray<FString>& ContentPaths)
	{
		TArray<FString> Result;
		if (ContentPaths.IsEmpty())
		{
			Result.Add(TEXT("/Game"));
			return Result;
		}

		for (const FString& ContentPath : ContentPaths)
		{
			if (!ContentPath.IsEmpty())
			{
				Result.AddUnique(ContentPath);
			}
		}

		if (Result.IsEmpty())
		{
			Result.Add(TEXT("/Game"));
		}

		return Result;
	}

	void AddPackagePathsToFilter(FARFilter& Filter, const TArray<FString>& ContentPaths)
	{
		Filter.bRecursivePaths = true;
		for (const FString& ContentPath : NormalizeContentPaths(ContentPaths))
		{
			Filter.PackagePaths.Add(FName(*ContentPath));
		}
	}

	void ScanAssetRegistryPaths(IAssetRegistry& AssetRegistry, const TArray<FString>& ContentPaths)
	{
		const TArray<FString> ScanPaths = NormalizeContentPaths(ContentPaths);
		AssetRegistry.ScanPathsSynchronous(ScanPaths, false);
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.WaitForCompletion();
		}
	}

	void GetAllAnimBlueprintAssets(TArray<FAssetData>& OutAssets)
	{
		UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssets(TArray<FString>(), OutAssets);
	}

	bool CheckOutPackageIfNeeded(UPackage* Package, bool bCheckOutFiles)
	{
		if (!Package || !bCheckOutFiles)
		{
			return true;
		}

		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		if (!SourceControlModule.IsEnabled() || !SourceControlModule.GetProvider().IsAvailable())
		{
			return true;
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		return Filename.IsEmpty() || USourceControlHelpers::CheckOutOrAddFile(Filename);
	}

	bool ResolveGameplayTagsFromNames(const TArray<FName>& TagNames,
	                                  FGameplayTagContainer& OutContainer,
	                                  const TCHAR* LogContext)
	{
		OutContainer.Reset();

		UGameplayTagsManager& GameplayTagsManager = UGameplayTagsManager::Get();
		TArray<FString> SkippedTagNames;
		for (const FName TagName : TagNames)
		{
			const FGameplayTag Tag = GameplayTagsManager.RequestGameplayTag(TagName, false);
			if (Tag.IsValid())
			{
				OutContainer.AddTag(Tag);
				continue;
			}

			SkippedTagNames.Add(TagName.ToString());
		}

		if (!SkippedTagNames.IsEmpty())
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("%s: Skipped %d unresolved tag(s): %s"),
			       LogContext,
			       SkippedTagNames.Num(),
			       *FString::Join(SkippedTagNames, TEXT(", ")));
		}

		return TagNames.IsEmpty() || !OutContainer.IsEmpty();
	}

	FKawaiiPhysicsNodeAuditEntry MakeAuditEntry(UAnimBlueprint* AnimBlueprint,
	                                            UAnimGraphNode_KawaiiPhysics* GraphNode,
	                                            const FKawaiiPhysicsPresetApplyOptions& Options,
	                                            const TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>>& Presets)
	{
		FKawaiiPhysicsNodeAuditEntry Entry;
		if (!AnimBlueprint || !GraphNode)
		{
			return Entry;
		}

		Entry.AnimBlueprintPath = FSoftObjectPath(AnimBlueprint);
		Entry.GraphName = GraphNode->GetGraph() ? GraphNode->GetGraph()->GetFName() : NAME_None;
		Entry.NodeGuid = GraphNode->NodeGuid;
		Entry.RootBoneName = GraphNode->Node.RootBone.BoneName;
		Entry.KawaiiPhysicsTag = GraphNode->Node.KawaiiPhysicsTag;
		Entry.BoneSubdivisionCount = GraphNode->Node.BoneSubdivisionCount;
		Entry.BoneConstraintSubdivisionCount = GraphNode->Node.BoneConstraintSubdivisionCount;
		Entry.bAllowWorldCollision = GraphNode->Node.bAllowWorldCollision;
		Entry.bUseSharedCollision = GraphNode->Node.bUseSharedCollision;
		Entry.bSharedCollisionSource = GraphNode->Node.bSharedCollisionSource;
		Entry.bEnableWind = GraphNode->Node.bEnableWind;
		Entry.ExternalForceCount = GraphNode->Node.ExternalForces.Num() + GraphNode->Node.CustomExternalForces.Num();
		Entry.WarmUpFrames = GraphNode->Node.WarmUpFrames;

		for (const TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>& PresetPtr : Presets)
		{
			UKawaiiPhysicsPresetDataAsset* Preset = PresetPtr.Get();
			if (!Preset || !Preset->TargetsNodeTag(GraphNode->Node.KawaiiPhysicsTag))
			{
				continue;
			}

			++Entry.MatchedPresetCount;
			if (!Entry.MatchedPresetPath.IsValid())
			{
				Entry.MatchedPresetPath = FSoftObjectPath(Preset);
				Entry.bMatchesPreset = Preset->MatchesNode(GraphNode->Node, Options, Entry.DiffProperties);
			}
		}

		return Entry;
	}

	struct FResolvedKawaiiPhysicsNodePlacementRequest
	{
		UKawaiiPhysicsPresetDataAsset* Preset = nullptr;
		FName RootBoneName;
		TArray<FName> ExcludeBoneNames;
		TArray<FKawaiiPhysicsRootBoneSetting> AdditionalRootBones;
		FGameplayTag KawaiiPhysicsTag;
		FVector2D NodePosition = FVector2D::ZeroVector;
		bool bAutoPosition = true;
	};

	void AddUniqueBoneName(TArray<FName>& BoneNames, FName BoneName)
	{
		if (!BoneName.IsNone())
		{
			BoneNames.AddUnique(BoneName);
		}
	}

	void AddUniqueAdditionalRootBone(TArray<FKawaiiPhysicsRootBoneSetting>& AdditionalRootBones,
	                                 const FKawaiiPhysicsRootBoneSetting& Setting)
	{
		if (Setting.RootBone.BoneName.IsNone())
		{
			return;
		}

		for (FKawaiiPhysicsRootBoneSetting& ExistingSetting : AdditionalRootBones)
		{
			if (ExistingSetting.RootBone.BoneName == Setting.RootBone.BoneName)
			{
				if (Setting.bUseOverrideExcludeBones)
				{
					ExistingSetting.bUseOverrideExcludeBones = true;
					for (const FBoneReference& ExcludeBone : Setting.OverrideExcludeBones)
					{
						if (!ExcludeBone.BoneName.IsNone())
						{
							ExistingSetting.OverrideExcludeBones.AddUnique(ExcludeBone);
						}
					}
				}
				return;
			}
		}

		AdditionalRootBones.Add(Setting);
	}

	FResolvedKawaiiPhysicsNodePlacementRequest ResolvePlacementRequest(
		USkeleton* Skeleton,
		const FKawaiiPhysicsNodePlacementRequest& Request)
	{
		FResolvedKawaiiPhysicsNodePlacementRequest ResolvedRequest;
		ResolvedRequest.Preset = Request.Preset;
		ResolvedRequest.RootBoneName = Request.RootBoneName;
		ResolvedRequest.ExcludeBoneNames = Request.ExcludeBoneNames;
		ResolvedRequest.AdditionalRootBones = Request.AdditionalRootBones;
		ResolvedRequest.KawaiiPhysicsTag = Request.KawaiiPhysicsTag;
		ResolvedRequest.NodePosition = Request.NodePosition;
		ResolvedRequest.bAutoPosition = Request.bAutoPosition;

		const TArray<FName> RootBonePatternMatches =
			UKawaiiPhysicsEditorLibrary::ResolveBonesByPattern(Skeleton, Request.RootBonePattern);
		if (!RootBonePatternMatches.IsEmpty())
		{
			if (ResolvedRequest.RootBoneName.IsNone())
			{
				ResolvedRequest.RootBoneName = RootBonePatternMatches[0];
			}

			for (int32 MatchIndex = 0; MatchIndex < RootBonePatternMatches.Num(); ++MatchIndex)
			{
				if (RootBonePatternMatches[MatchIndex] == ResolvedRequest.RootBoneName)
				{
					continue;
				}

				FKawaiiPhysicsRootBoneSetting AdditionalRootBone;
				AdditionalRootBone.RootBone = FBoneReference(RootBonePatternMatches[MatchIndex]);
				AddUniqueAdditionalRootBone(ResolvedRequest.AdditionalRootBones, AdditionalRootBone);
			}
		}

		const TArray<FName> ExcludeBonePatternMatches =
			UKawaiiPhysicsEditorLibrary::ResolveBonesByPattern(Skeleton, Request.ExcludeBonePattern);
		for (const FName ExcludeBoneName : ExcludeBonePatternMatches)
		{
			AddUniqueBoneName(ResolvedRequest.ExcludeBoneNames, ExcludeBoneName);
		}

		if (!ResolvedRequest.KawaiiPhysicsTag.IsValid() &&
			ResolvedRequest.Preset &&
			ResolvedRequest.Preset->Node.KawaiiPhysicsTag.IsValid())
		{
			ResolvedRequest.KawaiiPhysicsTag = ResolvedRequest.Preset->Node.KawaiiPhysicsTag;
		}

		return ResolvedRequest;
	}

	void AddValidationMessage(TArray<FString>& OutErrors,
	                          int32 RequestIndex,
	                          const FString& Message)
	{
		if (Message.StartsWith(TEXT("Warning:")))
		{
			OutErrors.Add(FString::Printf(
				TEXT("Warning: Request[%d]: %s"),
				RequestIndex,
				*Message.RightChop(UE_ARRAY_COUNT(TEXT("Warning:")) - 1).TrimStart()));
			return;
		}

		OutErrors.Add(FString::Printf(TEXT("Request[%d]: %s"), RequestIndex, *Message));
	}

	bool DoesSkeletonContainBone(USkeleton* Skeleton, FName BoneName)
	{
		return Skeleton &&
			!BoneName.IsNone() &&
			Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName) != INDEX_NONE;
	}

	bool ValidateBoneName(USkeleton* Skeleton,
	                      FName BoneName,
	                      int32 RequestIndex,
	                      const TCHAR* FieldName,
	                      TArray<FString>& OutErrors)
	{
		if (DoesSkeletonContainBone(Skeleton, BoneName))
		{
			return true;
		}

		AddValidationMessage(
			OutErrors,
			RequestIndex,
			FString::Printf(TEXT("%s bone '%s' does not exist in the target skeleton."),
			                FieldName,
			                *BoneName.ToString()));
		return false;
	}

	void AddResolvedRootBoneNames(const FResolvedKawaiiPhysicsNodePlacementRequest& ResolvedRequest,
	                              TArray<FName>& OutRootBoneNames)
	{
		if (!ResolvedRequest.RootBoneName.IsNone())
		{
			OutRootBoneNames.Add(ResolvedRequest.RootBoneName);
		}

		for (const FKawaiiPhysicsRootBoneSetting& AdditionalRootBone : ResolvedRequest.AdditionalRootBones)
		{
			if (!AdditionalRootBone.RootBone.BoneName.IsNone())
			{
				OutRootBoneNames.Add(AdditionalRootBone.RootBone.BoneName);
			}
		}
	}

	TArray<FName> CollectResolvedRootBoneNames(
		const TArray<FResolvedKawaiiPhysicsNodePlacementRequest>& ResolvedRequests)
	{
		TArray<FName> RootBoneNames;
		for (const FResolvedKawaiiPhysicsNodePlacementRequest& ResolvedRequest : ResolvedRequests)
		{
			AddResolvedRootBoneNames(ResolvedRequest, RootBoneNames);
		}
		return RootBoneNames;
	}

	bool IsBoneDescendantOf(
		const FReferenceSkeleton& RefSkeleton,
		FName BoneName,
		FName AncestorBoneName)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		const int32 AncestorBoneIndex = RefSkeleton.FindBoneIndex(AncestorBoneName);
		if (BoneIndex == INDEX_NONE ||
			AncestorBoneIndex == INDEX_NONE ||
			BoneIndex == AncestorBoneIndex)
		{
			return false;
		}

		for (int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		     ParentIndex != INDEX_NONE;
		     ParentIndex = RefSkeleton.GetParentIndex(ParentIndex))
		{
			if (ParentIndex == AncestorBoneIndex)
			{
				return true;
			}
		}

		return false;
	}

	void AddNestedResolvedRootWarnings(
		USkeleton* Skeleton,
		const FResolvedKawaiiPhysicsNodePlacementRequest& ResolvedRequest,
		const TArray<FName>& AllResolvedRootBoneNames,
		int32 RequestIndex,
		TArray<FString>& OutErrors)
	{
		if (!Skeleton)
		{
			return;
		}

		TArray<FName> RequestRootBoneNames;
		AddResolvedRootBoneNames(ResolvedRequest, RequestRootBoneNames);

		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
		for (const FName RootBoneName : RequestRootBoneNames)
		{
			for (const FName AncestorRootBoneName : AllResolvedRootBoneNames)
			{
				if (!IsBoneDescendantOf(RefSkeleton, RootBoneName, AncestorRootBoneName))
				{
					continue;
				}

				AddValidationMessage(
					OutErrors,
					RequestIndex,
					FString::Printf(
						TEXT("Warning: Resolved root '%s' is a descendant of another resolved root '%s'. Pattern should match only chain start bones."),
						*RootBoneName.ToString(),
						*AncestorRootBoneName.ToString()));
				break;
			}
		}
	}

	bool ValidateResolvedPlacementRequest(
		UAnimBlueprint* AnimBlueprint,
		const FKawaiiPhysicsNodePlacementRequest& SourceRequest,
		const FResolvedKawaiiPhysicsNodePlacementRequest& ResolvedRequest,
		const TArray<FName>& AllResolvedRootBoneNames,
		int32 RequestIndex,
		TArray<FString>& OutErrors)
	{
		bool bValid = true;
		if (!AnimBlueprint)
		{
			AddValidationMessage(OutErrors, RequestIndex, TEXT("AnimBlueprint is null."));
			return false;
		}

		USkeleton* TargetSkeleton = AnimBlueprint->TargetSkeleton;
		if (!TargetSkeleton)
		{
			AddValidationMessage(OutErrors, RequestIndex, TEXT("AnimBlueprint TargetSkeleton is null."));
			return false;
		}

		const bool bRootBoneSpecified =
			!SourceRequest.RootBoneName.IsNone() || !SourceRequest.RootBonePattern.IsEmpty();
		if (!bRootBoneSpecified)
		{
			AddValidationMessage(OutErrors, RequestIndex, TEXT("RootBoneName or RootBonePattern must be specified."));
			bValid = false;
		}

		if (bRootBoneSpecified)
		{
			bValid &= ValidateBoneName(
				TargetSkeleton,
				ResolvedRequest.RootBoneName,
				RequestIndex,
				TEXT("RootBone"),
				OutErrors);
		}

		for (const FName ExcludeBoneName : ResolvedRequest.ExcludeBoneNames)
		{
			bValid &= ValidateBoneName(
				TargetSkeleton,
				ExcludeBoneName,
				RequestIndex,
				TEXT("ExcludeBone"),
				OutErrors);
		}

		for (const FKawaiiPhysicsRootBoneSetting& AdditionalRootBone : ResolvedRequest.AdditionalRootBones)
		{
			bValid &= ValidateBoneName(
				TargetSkeleton,
				AdditionalRootBone.RootBone.BoneName,
				RequestIndex,
				TEXT("AdditionalRootBone"),
				OutErrors);

			for (const FBoneReference& OverrideExcludeBone : AdditionalRootBone.OverrideExcludeBones)
			{
				bValid &= ValidateBoneName(
					TargetSkeleton,
					OverrideExcludeBone.BoneName,
					RequestIndex,
					TEXT("AdditionalRootBone.OverrideExcludeBone"),
					OutErrors);
			}
		}

#if WITH_EDITORONLY_DATA
		if (ResolvedRequest.Preset &&
			ResolvedRequest.Preset->Skeleton &&
			ResolvedRequest.Preset->Skeleton != TargetSkeleton)
		{
			AddValidationMessage(
				OutErrors,
				RequestIndex,
				TEXT("Warning: Preset Skeleton does not match AnimBlueprint TargetSkeleton."));
		}
#endif

		AddNestedResolvedRootWarnings(
			TargetSkeleton,
			ResolvedRequest,
			AllResolvedRootBoneNames,
			RequestIndex,
			OutErrors);

		return bValid;
	}

	UEdGraph* FindPlacementAnimGraph(UAnimBlueprint* AnimBlueprint, FName GraphName)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

		const FName TargetGraphName = GraphName.IsNone() ? UEdGraphSchema_K2::GN_AnimGraph : GraphName;

		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph &&
				Graph->IsA<UAnimationGraph>() &&
				Graph->GetFName() == TargetGraphName)
			{
				return Graph;
			}
		}

		return nullptr;
	}

	FVector2D GetAutoPlacementBasePosition(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return FVector2D::ZeroVector;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
			{
				return FVector2D(
					static_cast<double>(RootNode->NodePosX + KawaiiPhysicsPlacementNodeOffsetX),
					static_cast<double>(RootNode->NodePosY));
			}
		}

		return FVector2D::ZeroVector;
	}

	FVector2D ResolveNodePosition(const FResolvedKawaiiPhysicsNodePlacementRequest& Request,
	                              const FVector2D& AutoPlacementBasePosition,
	                              int32 AutoPlacementIndex)
	{
		if (!Request.bAutoPosition)
		{
			return Request.NodePosition;
		}

		return FVector2D(
			AutoPlacementBasePosition.X,
			AutoPlacementBasePosition.Y + static_cast<double>(AutoPlacementIndex * KawaiiPhysicsPlacementNodeOffsetY));
	}

	void ApplyResolvedPlacementToGraphNode(
		UAnimGraphNode_KawaiiPhysics* GraphNode,
		const FResolvedKawaiiPhysicsNodePlacementRequest& Request)
	{
		if (!GraphNode)
		{
			return;
		}

		const FKawaiiPhysicsPresetApplyOptions Options;
		if (Request.Preset)
		{
			Request.Preset->ApplyToNode(GraphNode->Node, Options, GraphNode);
		}

		GraphNode->Node.RootBone = FBoneReference(Request.RootBoneName);

		GraphNode->Node.ExcludeBones.Empty(Request.ExcludeBoneNames.Num());
		for (const FName ExcludeBoneName : Request.ExcludeBoneNames)
		{
			GraphNode->Node.ExcludeBones.Add(FBoneReference(ExcludeBoneName));
		}

		GraphNode->Node.AdditionalRootBones = Request.AdditionalRootBones;
		GraphNode->Node.KawaiiPhysicsTag = Request.KawaiiPhysicsTag;
	}

	void FinalizeExistingPlacementGraphNode(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		if (!GraphNode)
		{
			return;
		}

		GraphNode->Node.ModifyBones.Empty();
		GraphNode->ReconstructNode();
	}

	bool DoesGraphNodeMatchUpsertKey(const UAnimGraphNode_KawaiiPhysics* GraphNode,
	                                 const FResolvedKawaiiPhysicsNodePlacementRequest& Request,
	                                 EKawaiiPhysicsPlacementUpsertKey UpsertKey)
	{
		if (!GraphNode)
		{
			return false;
		}

		const bool bTagMatches =
			Request.KawaiiPhysicsTag.IsValid() &&
			GraphNode->Node.KawaiiPhysicsTag == Request.KawaiiPhysicsTag;
		const bool bRootBoneMatches = GraphNode->Node.RootBone.BoneName == Request.RootBoneName;

		switch (UpsertKey)
		{
		case EKawaiiPhysicsPlacementUpsertKey::Tag:
			return bTagMatches;
		case EKawaiiPhysicsPlacementUpsertKey::RootBone:
			return bRootBoneMatches;
		case EKawaiiPhysicsPlacementUpsertKey::TagAndRootBone:
			return bTagMatches && bRootBoneMatches;
		case EKawaiiPhysicsPlacementUpsertKey::None:
		default:
			return false;
		}
	}

	UAnimGraphNode_KawaiiPhysics* FindUpsertGraphNode(
		UEdGraph* Graph,
		const FResolvedKawaiiPhysicsNodePlacementRequest& Request,
		EKawaiiPhysicsPlacementUpsertKey UpsertKey)
	{
		if (!Graph || UpsertKey == EKawaiiPhysicsPlacementUpsertKey::None)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_KawaiiPhysics* GraphNode = Cast<UAnimGraphNode_KawaiiPhysics>(Node);
			if (DoesGraphNodeMatchUpsertKey(GraphNode, Request, UpsertKey))
			{
				return GraphNode;
			}
		}

		return nullptr;
	}
}

void UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssets(const TArray<FString>& ContentPaths, TArray<FAssetData>& OutAssets)
{
	OutAssets.Reset();

	FARFilter Filter;
	ConfigureAnimBlueprintFilter(Filter);
	AddPackagePathsToFilter(Filter, ContentPaths);

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	ScanAssetRegistryPaths(AssetRegistry, ContentPaths);
	AssetRegistry.GetAssets(Filter, OutAssets);
}

void UKawaiiPhysicsEditorLibrary::GetAllPresetAssets(TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>>& OutPresets)
{
	OutPresets.Reset();

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	AddPackagePathsToFilter(Filter, TArray<FString>());
#if UE_VERSION_OLDER_THAN(5, 1, 0)
	Filter.ClassNames.Add(UKawaiiPhysicsPresetDataAsset::StaticClass()->GetFName());
#else
	Filter.ClassPaths.Add(UKawaiiPhysicsPresetDataAsset::StaticClass()->GetClassPathName());
#endif

	TArray<FAssetData> PresetAssets;
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	ScanAssetRegistryPaths(AssetRegistry, TArray<FString>());
	AssetRegistry.GetAssets(Filter, PresetAssets);
	PresetAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
	{
		return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
	});

	OutPresets.Reserve(PresetAssets.Num());
	for (const FAssetData& AssetData : PresetAssets)
	{
		if (UKawaiiPhysicsPresetDataAsset* Preset = Cast<UKawaiiPhysicsPresetDataAsset>(AssetData.GetAsset()))
		{
			OutPresets.Emplace(Preset);
		}
		else
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("GetAllPresetAssets: Failed to load KawaiiPhysics preset asset '%s'."),
			       *AssetData.GetSoftObjectPath().ToString());
		}
	}
}

TArray<FKawaiiPhysicsGraphNodeHandle> UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
	UAnimBlueprint* AnimBlueprint,
	const FGameplayTagContainer& FilterTags,
	bool bFilterExactMatch)
{
	TArray<FKawaiiPhysicsGraphNodeHandle> Result;
	if (!AnimBlueprint)
	{
		return Result;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_KawaiiPhysics* KawaiiPhysicsGraphNode = Cast<UAnimGraphNode_KawaiiPhysics>(Node);
			if (!KawaiiPhysicsGraphNode ||
				!DoesNodeMatchTags(KawaiiPhysicsGraphNode->Node, FilterTags, bFilterExactMatch))
			{
				continue;
			}

			FKawaiiPhysicsGraphNodeHandle Handle;
			Handle.Node = KawaiiPhysicsGraphNode;
			Result.Add(Handle);
		}
	}

	return Result;
}

TArray<FKawaiiPhysicsGraphNodeHandle> UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
	UAnimBlueprint* AnimBlueprint,
	const TArray<FKawaiiPhysicsNodePlacementRequest>& Requests,
	EKawaiiPhysicsPlacementUpsertKey UpsertKey,
	FName GraphName)
{
	TArray<FKawaiiPhysicsGraphNodeHandle> Result;
	if (!AnimBlueprint || Requests.IsEmpty())
	{
		return Result;
	}

	UEdGraph* Graph = FindPlacementAnimGraph(AnimBlueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("AddKawaiiPhysicsNodes: AnimGraph '%s' was not found in AnimBlueprint '%s'."),
		       *(GraphName.IsNone() ? UEdGraphSchema_K2::GN_AnimGraph : GraphName).ToString(),
		       *AnimBlueprint->GetName());
		return Result;
	}

	FScopedTransaction Transaction(
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "AddKawaiiPhysicsNodes", "Add Kawaii Physics Nodes"));

	USkeleton* TargetSkeleton = AnimBlueprint->TargetSkeleton;
	const FVector2D AutoPlacementBasePosition = GetAutoPlacementBasePosition(Graph);
	int32 AutoPlacementIndex = 0;
	bool bAddedNode = false;
	bool bUpdatedNode = false;

	for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
	{
		const FResolvedKawaiiPhysicsNodePlacementRequest ResolvedRequest =
			ResolvePlacementRequest(TargetSkeleton, Requests[RequestIndex]);

		TArray<FString> ValidationMessages;
		const bool bRequestValid = ValidateResolvedPlacementRequest(
			AnimBlueprint,
			Requests[RequestIndex],
			ResolvedRequest,
			RequestIndex,
			ValidationMessages);

		for (const FString& ValidationMessage : ValidationMessages)
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT("AddKawaiiPhysicsNodes: %s"), *ValidationMessage);
		}

		if (!bRequestValid)
		{
			continue;
		}

		const FVector2D NodePosition = ResolveNodePosition(
			ResolvedRequest,
			AutoPlacementBasePosition,
			AutoPlacementIndex);
		if (ResolvedRequest.bAutoPosition)
		{
			++AutoPlacementIndex;
		}

		if (UAnimGraphNode_KawaiiPhysics* ExistingGraphNode =
			FindUpsertGraphNode(Graph, ResolvedRequest, UpsertKey))
		{
			ExistingGraphNode->Modify();
			ApplyResolvedPlacementToGraphNode(ExistingGraphNode, ResolvedRequest);
			ExistingGraphNode->NodePosX = static_cast<int32>(NodePosition.X);
			ExistingGraphNode->NodePosY = static_cast<int32>(NodePosition.Y);
			FinalizeExistingPlacementGraphNode(ExistingGraphNode);

			FKawaiiPhysicsGraphNodeHandle Handle;
			Handle.Node = ExistingGraphNode;
			Result.Add(Handle);
			bUpdatedNode = true;
			continue;
		}

		Graph->Modify();
		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysics* NewGraphNode = NodeCreator.CreateNode(false);
		ApplyResolvedPlacementToGraphNode(NewGraphNode, ResolvedRequest);
		NewGraphNode->NodePosX = static_cast<int32>(NodePosition.X);
		NewGraphNode->NodePosY = static_cast<int32>(NodePosition.Y);
		NodeCreator.Finalize();

		FKawaiiPhysicsGraphNodeHandle Handle;
		Handle.Node = NewGraphNode;
		Result.Add(Handle);
		bAddedNode = true;
	}

	if (bAddedNode)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	}
	else if (bUpdatedNode)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	}
	else
	{
		Transaction.Cancel();
	}

	return Result;
}

TArray<FString> UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(
	UAnimBlueprint* AnimBlueprint,
	const TArray<FKawaiiPhysicsNodePlacementRequest>& Requests)
{
	TArray<FString> Errors;

	USkeleton* TargetSkeleton = AnimBlueprint ? AnimBlueprint->TargetSkeleton : nullptr;
	TArray<FResolvedKawaiiPhysicsNodePlacementRequest> ResolvedRequests;
	ResolvedRequests.Reserve(Requests.Num());
	for (const FKawaiiPhysicsNodePlacementRequest& Request : Requests)
	{
		ResolvedRequests.Add(ResolvePlacementRequest(TargetSkeleton, Request));
	}
	const TArray<FName> AllResolvedRootBoneNames = CollectResolvedRootBoneNames(ResolvedRequests);

	for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
	{
		ValidateResolvedPlacementRequest(
			AnimBlueprint,
			Requests[RequestIndex],
			ResolvedRequests[RequestIndex],
			AllResolvedRootBoneNames,
			RequestIndex,
			Errors);
	}

	return Errors;
}

TArray<FName> UKawaiiPhysicsEditorLibrary::ResolveBonesByPattern(USkeleton* Skeleton, const FString& Pattern)
{
	TArray<FName> Result;
	if (!Skeleton || Pattern.IsEmpty())
	{
		return Result;
	}

	FString BoneListString;
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FMeshBoneInfo>& RefBoneInfo = RefSkeleton.GetRefBoneInfo();
	for (const FMeshBoneInfo& BoneInfo : RefBoneInfo)
	{
		BoneListString.Append(BoneInfo.Name.ToString());
		BoneListString.Append(TEXT(", "));
	}

	const FRegexPattern RegexPattern(Pattern);
	FRegexMatcher Matcher(RegexPattern, BoneListString);
	bool bLongMatchWarningLogged = false;
	while (Matcher.FindNext())
	{
		const FString MatchedBoneName = Matcher.GetCaptureGroup(0);
		if (MatchedBoneName.Len() >= NAME_SIZE)
		{
			if (!bLongMatchWarningLogged)
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("ResolveBonesByPattern: Regex match is too long to convert to FName. Pattern=\"%s\", MatchLength=%d, MaxLength=%d"),
				       *Pattern, MatchedBoneName.Len(), NAME_SIZE - 1);
				bLongMatchWarningLogged = true;
			}
			continue;
		}

		const FName BoneName(*MatchedBoneName);
		if (!BoneName.IsNone() && RefSkeleton.FindBoneIndex(BoneName) != INDEX_NONE)
		{
			Result.AddUnique(BoneName);
		}
	}

	return Result;
}

bool UKawaiiPhysicsEditorLibrary::IsGraphNodeHandleValid(const FKawaiiPhysicsGraphNodeHandle& Handle)
{
	return Handle.IsValid();
}

bool UKawaiiPhysicsEditorLibrary::SetGraphNodePropertyByString(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FName PropertyName,
	const FString& Value)
{
	return MutateGraphNodeProperty(
		Handle,
		PropertyName,
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "SetGraphNodePropertyByString", "Set Kawaii Physics Graph Node Property"),
		[PropertyName, &Value](UAnimGraphNode_KawaiiPhysics& GraphNode)
		{
			return UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(GraphNode.Node, PropertyName, Value);
		});
}

bool UKawaiiPhysicsEditorLibrary::GetGraphNodePropertyAsString(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FName PropertyName,
	FString& OutValue)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
	return GraphNode &&
		UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(GraphNode->Node, PropertyName, OutValue);
}

bool UKawaiiPhysicsEditorLibrary::SetPresetNodePropertyByString(
	UKawaiiPhysicsPresetDataAsset* Preset,
	FName PropertyName,
	const FString& Value)
{
	if (!Preset)
	{
		return false;
	}

	// Modify前に一度コピーへImportして、失敗時にアセットへ副作用を残さない。
	FAnimNode_KawaiiPhysics TestNode = Preset->Node;
	if (!UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(TestNode, PropertyName, Value))
	{
		return false;
	}

	Preset->Modify();
	if (!UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(Preset->Node, PropertyName, Value))
	{
		return false;
	}

	Preset->MarkPackageDirty();
	return true;
}

bool UKawaiiPhysicsEditorLibrary::GetPresetNodePropertyAsString(
	UKawaiiPhysicsPresetDataAsset* Preset,
	FName PropertyName,
	FString& OutValue)
{
	return Preset &&
		UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(Preset->Node, PropertyName, OutValue);
}

bool UKawaiiPhysicsEditorLibrary::MakeGameplayTagContainerFromNames(
	const TArray<FName>& TagNames,
	FGameplayTagContainer& OutContainer)
{
	return ResolveGameplayTagsFromNames(
		TagNames,
		OutContainer,
		TEXT("MakeGameplayTagContainerFromNames"));
}

bool UKawaiiPhysicsEditorLibrary::SetPresetTargetTags(
	UKawaiiPhysicsPresetDataAsset* Preset,
	const TArray<FName>& TagNames,
	bool bExactMatch)
{
	if (!Preset)
	{
		return false;
	}

	FGameplayTagContainer TargetTags;
	if (!ResolveGameplayTagsFromNames(TagNames, TargetTags, TEXT("SetPresetTargetTags")))
	{
		return false;
	}

	Preset->Modify();
	Preset->TargetTags = TargetTags;
	Preset->bTargetTagsExactMatch = bExactMatch;
	Preset->MarkPackageDirty();
	return true;
}

bool UKawaiiPhysicsEditorLibrary::SetGraphNodeTag(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FGameplayTag Tag)
{
	return MutateGraphNodeProperty(
		Handle,
		GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag),
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "SetGraphNodeTag", "Set Kawaii Physics Graph Node Tag"),
		[Tag](UAnimGraphNode_KawaiiPhysics& GraphNode)
		{
			return UKawaiiPhysicsLibrary::SetNodeStructPropertyValue<FGameplayTag>(
				GraphNode.Node,
				GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag),
				Tag);
		});
}

bool UKawaiiPhysicsEditorLibrary::GetGraphNodeTag(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FGameplayTag& OutTag)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
	if (!GraphNode)
	{
		return false;
	}

	OutTag = GraphNode->Node.KawaiiPhysicsTag;
	return true;
}

bool UKawaiiPhysicsEditorLibrary::SetGraphNodeRootBoneName(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FName RootBoneName)
{
	return MutateGraphNodeProperty(
		Handle,
		GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RootBone),
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "SetGraphNodeRootBoneName", "Set Kawaii Physics Graph Node Root Bone"),
		[RootBoneName](UAnimGraphNode_KawaiiPhysics& GraphNode)
		{
			GraphNode.Node.RootBone.BoneName = RootBoneName;
			GraphNode.Node.RequestModifyBonesReinit();
			return true;
		});
}

bool UKawaiiPhysicsEditorLibrary::GetGraphNodeRootBoneName(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FName& OutRootBoneName)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
	if (!GraphNode)
	{
		return false;
	}

	OutRootBoneName = GraphNode->Node.RootBone.BoneName;
	return true;
}

bool UKawaiiPhysicsEditorLibrary::ApplyPresetToGraphNode(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	UKawaiiPhysicsPresetDataAsset* Preset,
	FKawaiiPhysicsPresetApplyOptions Options)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
	if (!GraphNode || !Preset)
	{
		return false;
	}

	FScopedTransaction Transaction(
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "ApplyPresetToGraphNode", "Apply Kawaii Physics Preset"));
	GraphNode->Modify();

	UKawaiiPhysicsLimitsDataAsset* OldLimitsDataAsset = GraphNode->Node.LimitsDataAsset;
	UKawaiiPhysicsBoneConstraintsDataAsset* OldBoneConstraintsDataAsset = GraphNode->Node.BoneConstraintsDataAsset;

	Preset->ApplyToNode(GraphNode->Node, Options, GraphNode);

	bool bNotifiedPropertyChange = false;
	if (OldLimitsDataAsset != GraphNode->Node.LimitsDataAsset)
	{
		bNotifiedPropertyChange |= NotifyGraphNodePropertyChanged(
			GraphNode,
			FindFProperty<FProperty>(
				FAnimNode_KawaiiPhysics::StaticStruct(),
				GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset)));
	}
	if (OldBoneConstraintsDataAsset != GraphNode->Node.BoneConstraintsDataAsset)
	{
		bNotifiedPropertyChange |= NotifyGraphNodePropertyChanged(
			GraphNode,
			FindFProperty<FProperty>(
				FAnimNode_KawaiiPhysics::StaticStruct(),
				GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintsDataAsset)));
	}

	if (!bNotifiedPropertyChange)
	{
		GraphNode->Node.ModifyBones.Empty();
		GraphNode->ReconstructNode();
		MarkGraphNodeBlueprintModified(GraphNode);
	}
	return true;
}

TArray<FName> UKawaiiPhysicsEditorLibrary::GetGraphNodePresetDiffProperties(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	UKawaiiPhysicsPresetDataAsset* Preset,
	FKawaiiPhysicsPresetApplyOptions Options)
{
	TArray<FName> DiffProperties;
	UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
	if (!GraphNode || !Preset)
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("GetGraphNodePresetDiffProperties: Invalid graph node handle or preset. HandleValid=%s PresetValid=%s."),
		       GraphNode ? TEXT("true") : TEXT("false"),
		       Preset ? TEXT("true") : TEXT("false"));
		return DiffProperties;
	}

	Preset->MatchesNode(GraphNode->Node, Options, DiffProperties);
	return DiffProperties;
}

bool UKawaiiPhysicsEditorLibrary::ExportGraphNodeToPreset(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	UKawaiiPhysicsPresetDataAsset* TargetAsset)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
	if (!GraphNode || !TargetAsset)
	{
		return false;
	}

	FScopedTransaction Transaction(
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "ExportGraphNodeToPreset", "Export Kawaii Physics Graph Node To Preset"));
	TargetAsset->Modify();
	TargetAsset->CopyFromNode(GraphNode->Node);
	TargetAsset->MarkPackageDirty();
	return true;
}

int32 UKawaiiPhysicsEditorLibrary::ReapplyPresetToProject(
	UKawaiiPhysicsPresetDataAsset* Preset,
	bool bDryRun,
	bool bCheckOutFiles,
	TArray<FKawaiiPhysicsNodeAuditEntry>& OutReport)
{
	OutReport.Reset();
	if (!Preset)
	{
		return 0;
	}

	if (Preset->TargetTags.IsEmpty())
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("ReapplyPresetToProject: Preset '%s' has empty TargetTags. No nodes will be targeted."),
		       *Preset->GetPathName());
		return 0;
	}

	TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>> Presets;
	Presets.Emplace(Preset);
	const FKawaiiPhysicsPresetApplyOptions Options;

	TArray<FAssetData> AnimBlueprintAssets;
	GetAllAnimBlueprintAssets(AnimBlueprintAssets);

	int32 AppliedNodeCount = 0;
	int32 MatchedNodeCount = 0;
	int32 ProcessedAssetCount = 0;
	for (const FAssetData& AssetData : AnimBlueprintAssets)
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
		if (!AnimBlueprint)
		{
			continue;
		}

		bool bPackageCheckedOut = false;
		TArray<FKawaiiPhysicsGraphNodeHandle> Handles = CollectKawaiiPhysicsGraphNodes(
			AnimBlueprint, Preset->TargetTags, Preset->bTargetTagsExactMatch);
		for (const FKawaiiPhysicsGraphNodeHandle& Handle : Handles)
		{
			UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
			if (!GraphNode)
			{
				continue;
			}

			++MatchedNodeCount;
			OutReport.Add(MakeAuditEntry(AnimBlueprint, GraphNode, Options, Presets));
			if (bDryRun)
			{
				continue;
			}

			if (!bPackageCheckedOut)
			{
				bPackageCheckedOut = CheckOutPackageIfNeeded(AnimBlueprint->GetOutermost(), bCheckOutFiles);
			}
			if (!bPackageCheckedOut)
			{
				continue;
			}

			if (ApplyPresetToGraphNode(Handle, Preset, Options))
			{
				++AppliedNodeCount;
			}
		}

		++ProcessedAssetCount;
		if (ProcessedAssetCount % KawaiiPhysicsEditorLibraryGCBatchSize == 0)
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	UE_LOG(LogKawaiiPhysics, Display,
	       TEXT("ReapplyPresetToProject: Preset=%s MatchedNodes=%d AppliedNodes=%d"),
	       *Preset->GetPathName(),
	       MatchedNodeCount,
	       AppliedNodeCount);
	return AppliedNodeCount;
}

bool UKawaiiPhysicsEditorLibrary::AuditKawaiiPhysicsNodes(
	const TArray<FString>& ContentPaths,
	const FGameplayTagContainer& FilterTags,
	bool bFilterExactMatch,
	TArray<FKawaiiPhysicsNodeAuditEntry>& OutEntries)
{
	OutEntries.Reset();

	TArray<FAssetData> AnimBlueprintAssets;
	GetAnimBlueprintAssets(ContentPaths, AnimBlueprintAssets);

	TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>> Presets;
	UKawaiiPhysicsEditorLibrary::GetAllPresetAssets(Presets);

	const FKawaiiPhysicsPresetApplyOptions Options;
	bool bResult = true;
	int32 ProcessedAssetCount = 0;
	for (const FAssetData& AssetData : AnimBlueprintAssets)
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
		if (!AnimBlueprint)
		{
			UE_LOG(LogKawaiiPhysics, Error,
			       TEXT("AuditKawaiiPhysicsNodes: Failed to load AnimBlueprint asset '%s'."),
			       *AssetData.GetSoftObjectPath().ToString());
			bResult = false;
			continue;
		}

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles = CollectKawaiiPhysicsGraphNodes(
			AnimBlueprint, FilterTags, bFilterExactMatch);
		for (const FKawaiiPhysicsGraphNodeHandle& Handle : Handles)
		{
			if (UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle))
			{
				OutEntries.Add(MakeAuditEntry(AnimBlueprint, GraphNode, Options, Presets));
			}
		}

		++ProcessedAssetCount;
		if (ProcessedAssetCount % KawaiiPhysicsEditorLibraryGCBatchSize == 0)
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	return bResult;
}

DEFINE_FUNCTION(UKawaiiPhysicsEditorLibrary::execSetGraphNodeWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsEditorAccessResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsGraphNodeHandle, Handle);
	P_GET_STRUCT_REF(FName, PropertyName);

	ExecResult = EKawaiiPhysicsEditorAccessResult::NotValid;

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	const void* ValuePtr = Stack.MostRecentPropertyAddress;

	if (MutateGraphNodeProperty(
		Handle,
		PropertyName,
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "SetGraphNodeWildcardProperty", "Set Kawaii Physics Graph Node Wildcard Property"),
		[PropertyName, ValueProp, ValuePtr](UAnimGraphNode_KawaiiPhysics& GraphNode)
		{
			return UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(
				GraphNode.Node, PropertyName, ValueProp, ValuePtr);
		}))
	{
		ExecResult = EKawaiiPhysicsEditorAccessResult::Valid;
	}

	P_FINISH;
}

DEFINE_FUNCTION(UKawaiiPhysicsEditorLibrary::execGetGraphNodeWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsEditorAccessResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsGraphNodeHandle, Handle);
	P_GET_STRUCT_REF(FName, PropertyName);

	ExecResult = EKawaiiPhysicsEditorAccessResult::NotValid;

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	void* ValuePtr = Stack.MostRecentPropertyAddress;

	if (UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle))
	{
		if (UKawaiiPhysicsLibrary::GetNodeWildcardPropertyValue(
			GraphNode->Node, PropertyName, ValueProp, ValuePtr))
		{
			ExecResult = EKawaiiPhysicsEditorAccessResult::Valid;
		}
	}

	P_FINISH;
}
