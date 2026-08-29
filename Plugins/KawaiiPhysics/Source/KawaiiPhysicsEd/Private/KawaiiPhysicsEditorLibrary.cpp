// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimationGraphSchema.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_Root.h"
#include "AnimationGraph.h"
#include "Animation/AnimNode_Root.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "EdGraphSchema_K2.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintGameplayTagLibrary.h"
#include "EdGraphNode_Comment.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Application/SlateApplication.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "Internationalization/Regex.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsMcpCommentNode.h"
#include "KawaiiPhysicsPresetDiffSnapshot.h"
#include "Misc/App.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
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
	// アセット走査中にGCを実行する間隔。
	constexpr int32 KawaiiPhysicsEditorLibraryGCBatchSize = 20;
	// MCPコメント枠の推定サイズと余白、および自動配置の重なり判定に使う推定値。
	constexpr int32 KawaiiPhysicsMcpCommentPaddingX = 50;
	constexpr int32 KawaiiPhysicsMcpCommentTopPaddingY = 80;
	constexpr int32 KawaiiPhysicsPlacementExpectedNodeWidth = 400;
	constexpr int32 KawaiiPhysicsPlacementExpectedNodeHeight = 260;
	constexpr int32 KawaiiPhysicsMcpCommentExpectedNodeWidthWithPadding = 450;
	constexpr int32 KawaiiPhysicsMcpCommentExpectedNodeHeightWithPadding = 310;
	constexpr int32 KawaiiPhysicsPlacementMaxOverlapResolutionAttempts = 100;
	// AutoConnectが生成するComponentToLocalSpace変換ノード用に確保する横幅
	constexpr int32 KawaiiPhysicsPlacementConversionNodeReserveX = 220;
	// AutoConnect時に基準位置を追加で左へずらす幅。
	// コメント枠右パディング450と変換ノードスロット220が重ならないよう、450-Spacing下限400+220+10(隙間)=280
	constexpr int32 KawaiiPhysicsPlacementAutoConnectBaseReserveX = 280;
	// KawaiiPhysics/MCPコメント以外のノード（変換ノード等）の推定サイズ
	constexpr int32 KawaiiPhysicsPlacementEstimatedOtherNodeWidth = 250;
	constexpr int32 KawaiiPhysicsPlacementEstimatedOtherNodeHeight = 120;

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
			// このライブラリは AnimBlueprint 配下の AnimGraph ノードを扱う前提。
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

	bool ModifyGraphNodeProperty(const FKawaiiPhysicsGraphNodeHandle& Handle,
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
		// 共通のGraphNode編集フローとして、TransactionとModify後にMutatorを実行し、失敗時はCancelで巻き戻す。
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
		// 未指定、または空要素のみの場合は/Game全体を対象にする。
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
		// commandlet等でAssetRegistryの走査が未完了でも結果が揃うよう、同期走査してロード完了を待つ。
		AssetRegistry.ScanPathsSynchronous(ScanPaths, false);
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.WaitForCompletion();
		}
	}

	struct FKawaiiPhysicsTagNameFilter
	{
		TSet<FName> ExactNames;
		TArray<FString> ChildPrefixes;

		bool Matches(const FName TagName) const
		{
			if (ExactNames.Contains(TagName))
			{
				return true;
			}

			if (ChildPrefixes.IsEmpty())
			{
				return false;
			}

			const FString TagString = TagName.ToString();
			for (const FString& ChildPrefix : ChildPrefixes)
			{
				if (TagString.StartsWith(ChildPrefix, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		}
	};

	FKawaiiPhysicsTagNameFilter MakeTagNameFilter(const FGameplayTagContainer& FilterTags, const bool bFilterExactMatch)
	{
		FKawaiiPhysicsTagNameFilter Result;
		for (const FGameplayTag& FilterTag : FilterTags)
		{
			const FName TagName = FilterTag.GetTagName();
			if (TagName.IsNone())
			{
				continue;
			}

			Result.ExactNames.Add(TagName);
			if (!bFilterExactMatch)
			{
				Result.ChildPrefixes.Add(TagName.ToString() + TEXT("."));
			}
		}

		const UGameplayTagsSettings* GameplayTagsSettings = GetDefault<UGameplayTagsSettings>();
		if (GameplayTagsSettings)
		{
			const TArray<FGameplayTagRedirect>& GameplayTagRedirects = GameplayTagsSettings->GameplayTagRedirects;
			const int32 MaxIterations = GameplayTagRedirects.Num() + 1;
			for (int32 Iteration = 0; Iteration < MaxIterations; ++Iteration)
			{
				bool bAddedRedirect = false;
				for (const FGameplayTagRedirect& Redirect : GameplayTagRedirects)
				{
					// 旧名のまま保存されたアセットを拾うため、現在の一致対象へリダイレクトされる旧名を追加が止まるまで取り込む。
					// 追加した旧名と子階層接頭辞は同じ反復内で以降の判定にも使う。
					if (Redirect.OldTagName.IsNone() || Redirect.NewTagName.IsNone() ||
						Result.ExactNames.Contains(Redirect.OldTagName) ||
						!Result.Matches(Redirect.NewTagName))
					{
						continue;
					}

					Result.ExactNames.Add(Redirect.OldTagName);
					if (!bFilterExactMatch)
					{
						const FString ChildPrefix = Redirect.OldTagName.ToString() + TEXT(".");
						if (!Result.ChildPrefixes.Contains(ChildPrefix))
						{
							Result.ChildPrefixes.Add(ChildPrefix);
						}
					}
					bAddedRedirect = true;
				}
				if (!bAddedRedirect)
				{
					break;
				}
			}
		}

		return Result;
	}

	bool IsDirtyPackageCandidate(const FAssetData& AssetData)
	{
		const FString PackageNameString = AssetData.PackageName.ToString();
		UPackage* Package = FindPackage(nullptr, *PackageNameString);
		return Package && Package->IsDirty();
	}

	bool CheckOutPackageIfNeeded(UPackage* Package, bool bCheckOutFiles)
	{
		// source controlが無効、またはcheckout不要な場合は成功扱いで通す。
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
		// 未解決Tagはスキップし、警告ログはまとめて出す。戻り値は入力が空、または1件以上解決できたことを表す。
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
	                                            const TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>>& Presets,
	                                            bool bIncludeDiffValues = false)
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
		Entry.bUseSimpleWorldCollision = GraphNode->Node.bUseSimpleWorldCollision;
		Entry.SimpleWorldCollisionSkeletalMeshCollision = GraphNode->Node.SimpleWorldCollisionSkeletalMeshCollision;
		Entry.bEnableWind = GraphNode->Node.bEnableWind;
		Entry.ExternalForceCount = GraphNode->Node.ExternalForces.Num() + GraphNode->Node.CustomExternalForces.Num();
		Entry.WarmUpFrames = GraphNode->Node.WarmUpFrames;

		// ノード設定のスナップショットを監査エントリへ詰め、差分比較には最初に一致したPresetのみを使う。
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
				if (bIncludeDiffValues && !Entry.bMatchesPreset)
				{
					// 値付き差分はオプトイン。監査APIでbIncludeDiffValues=trueの時のみ計算する。
					Entry.DiffValues = KawaiiPhysicsPresetDiff::BuildDiffValues(GraphNode->Node, *Preset, Options);
				}
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
		bool bAutoConnect = false;
		EKawaiiPhysicsNodePlacementDirectionOverride PlacementDirection =
			EKawaiiPhysicsNodePlacementDirectionOverride::Default;
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
				// 同名RootBoneは追加せず、OverrideExcludeBonesだけをマージする。
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
		ResolvedRequest.bAutoConnect = Request.bAutoConnect;
		ResolvedRequest.PlacementDirection = Request.PlacementDirection;

		// RootBonePatternの先頭一致をRootBoneへ、残りをAdditionalRootBonesへ解決する。
		const TArray<FName> RootBonePatternMatches =
			UKawaiiPhysicsEditorLibrary::FindBonesByPattern(Skeleton, Request.RootBonePattern);
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
			UKawaiiPhysicsEditorLibrary::FindBonesByPattern(Skeleton, Request.ExcludeBonePattern);
		// ExcludeBonePatternの一致は既存指定へ追記する。
		for (const FName ExcludeBoneName : ExcludeBonePatternMatches)
		{
			AddUniqueBoneName(ResolvedRequest.ExcludeBoneNames, ExcludeBoneName);
		}

		// Tag未指定時はPreset側のTagを既定値として使う。
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
		// Warning:接頭辞の有無で整形を分け、警告では接頭辞を先頭へ移動する。
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

	bool AnyResolvedRequestHasAutoConnect(
		const TArray<FResolvedKawaiiPhysicsNodePlacementRequest>& ResolvedRequests)
	{
		for (const FResolvedKawaiiPhysicsNodePlacementRequest& ResolvedRequest : ResolvedRequests)
		{
			if (ResolvedRequest.bAutoConnect)
			{
				return true;
			}
		}
		return false;
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
		// 解決済みRoot同士が親子関係にある場合、Patternがチェーン途中のBoneまで拾った兆候として警告する。
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
		// RootBoneは明示名またはPatternのどちらかを必須にする。
		if (!bRootBoneSpecified)
		{
			AddValidationMessage(OutErrors, RequestIndex, TEXT("RootBoneName or RootBonePattern must be specified."));
			bValid = false;
		}

		// 解決済みの各BoneがTargetSkeletonに存在するか確認する。
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
			// PresetとAnimBlueprintのSkeleton不一致は警告のみとし、適用可否はBone解決結果に委ねる。
			AddValidationMessage(
				OutErrors,
				RequestIndex,
				TEXT("Warning: Preset Skeleton does not match AnimBlueprint TargetSkeleton."));
		}
#endif

		// Pattern解決後のRootが入れ子になっていないか確認する。
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

		// GraphName未指定時は既定のAnimGraphを対象にする。
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

	bool ComputeMcpCommentBounds(const TArray<FKawaiiPhysicsGraphNodeHandle>& Handles,
	                             int32& OutNodePosX,
	                             int32& OutNodePosY,
	                             int32& OutNodeWidth,
	                             int32& OutNodeHeight)
	{
		int32 MinX = TNumericLimits<int32>::Max();
		int32 MinY = TNumericLimits<int32>::Max();
		int32 MaxX = TNumericLimits<int32>::Lowest();
		int32 MaxY = TNumericLimits<int32>::Lowest();
		bool bHasNode = false;

		for (const FKawaiiPhysicsGraphNodeHandle& Handle : Handles)
		{
			const UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
			if (!GraphNode)
			{
				continue;
			}

			MinX = FMath::Min(MinX, GraphNode->NodePosX);
			MinY = FMath::Min(MinY, GraphNode->NodePosY);
			MaxX = FMath::Max(MaxX, GraphNode->NodePosX);
			MaxY = FMath::Max(MaxY, GraphNode->NodePosY);
			bHasNode = true;
		}

		if (!bHasNode)
		{
			return false;
		}

		OutNodePosX = MinX - KawaiiPhysicsMcpCommentPaddingX;
		OutNodePosY = MinY - KawaiiPhysicsMcpCommentTopPaddingY;
		// 対象ノード群の外接矩形に余白を足してコメント枠サイズを決める。
		const int32 BoundsMaxX = MaxX + KawaiiPhysicsMcpCommentExpectedNodeWidthWithPadding;
		const int32 BoundsMaxY = MaxY + KawaiiPhysicsMcpCommentExpectedNodeHeightWithPadding;
		OutNodeWidth = BoundsMaxX - OutNodePosX;
		OutNodeHeight = BoundsMaxY - OutNodePosY;
		return true;
	}

	// UE5.5未満ではUEdGraphNode_Comment継承クラスがリンクできないため、素のコメント枠を使う
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
	void ApplyMcpCommentNodeState(UKawaiiPhysicsMcpCommentNode* CommentNode,
#else
	void ApplyMcpCommentNodeState(UEdGraphNode_Comment* CommentNode,
#endif
	                              const TArray<FKawaiiPhysicsGraphNodeHandle>& Handles,
	                              const FString& CommentText,
	                              const FString& Prompt,
	                              bool bNewCommentNode)
	{
		if (!CommentNode)
		{
			return;
		}

		int32 NodePosX = 0;
		int32 NodePosY = 0;
		int32 NodeWidth = 0;
		int32 NodeHeight = 0;
		if (!ComputeMcpCommentBounds(Handles, NodePosX, NodePosY, NodeWidth, NodeHeight))
		{
			return;
		}

		// コメント枠の位置とサイズを対象ノード群に合わせて更新する。
		CommentNode->NodeComment = CommentText;
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
		const FDateTime Now = FDateTime::Now();
		// MCPメタデータとしてPromptと作成・更新時刻を保持する。
		CommentNode->Prompt = Prompt;
		if (bNewCommentNode)
		{
			CommentNode->CreatedAt = Now;
		}
		CommentNode->UpdatedAt = Now;
#else
		(void)Prompt;
		(void)bNewCommentNode;
#endif

		// 設定色とGroupMovementを適用し、コメント枠として一緒に動く状態にする。
		if (const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>())
		{
			CommentNode->CommentColor = Settings->McpCommentColor;
		}
		CommentNode->MoveMode = ECommentBoxMode::GroupMovement;
		CommentNode->NodePosX = NodePosX;
		CommentNode->NodePosY = NodePosY;
		CommentNode->NodeWidth = NodeWidth;
		CommentNode->NodeHeight = NodeHeight;

		// 枠内ノードを再登録して、更新後のコメント枠に現在の対象ノードだけを紐付ける。
		CommentNode->ClearNodesUnderComment();
		for (const FKawaiiPhysicsGraphNodeHandle& Handle : Handles)
		{
			if (UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle))
			{
				CommentNode->AddNodeUnderComment(GraphNode);
			}
		}
	}

	bool FindOrAddMcpCommentNode(UEdGraph* Graph,
	                          const TArray<FKawaiiPhysicsGraphNodeHandle>& Handles,
	                          const FString& CommentText,
	                          const FString& Prompt)
	{
		if (!Graph || Handles.IsEmpty())
		{
			return false;
		}

		// 同じコメント本文の枠があれば更新し、なければ新規作成する。戻り値は新規作成の有無を表す。
		for (UEdGraphNode* Node : Graph->Nodes)
		{
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
			UKawaiiPhysicsMcpCommentNode* CommentNode = Cast<UKawaiiPhysicsMcpCommentNode>(Node);
			if (!CommentNode ||
				CommentNode->GetClass() != UKawaiiPhysicsMcpCommentNode::StaticClass() ||
				CommentNode->NodeComment != CommentText)
#else
			UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node);
			if (!CommentNode ||
				CommentNode->GetClass() != UEdGraphNode_Comment::StaticClass() ||
				CommentNode->NodeComment != CommentText)
#endif
			{
				continue;
			}

			CommentNode->Modify();
			ApplyMcpCommentNodeState(CommentNode, Handles, CommentText, Prompt, false);
			Graph->NotifyNodeChanged(CommentNode);
			return false;
		}

		Graph->Modify();
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
		FGraphNodeCreator<UKawaiiPhysicsMcpCommentNode> NodeCreator(*Graph);
		UKawaiiPhysicsMcpCommentNode* CommentNode = NodeCreator.CreateNode(false);
#else
		FGraphNodeCreator<UEdGraphNode_Comment> NodeCreator(*Graph);
		UEdGraphNode_Comment* CommentNode = NodeCreator.CreateNode(false);
#endif
		NodeCreator.Finalize();
		ApplyMcpCommentNodeState(CommentNode, Handles, CommentText, Prompt, true);
		Graph->NotifyNodeChanged(CommentNode);
		return true;
	}

	int32 GetAutoPlacementSpacingX();
	int32 GetAutoPlacementSpacingY();

	FVector2D GetAutoPlacementBasePosition(UEdGraph* Graph, bool bAutoConnect)
	{
		if (!Graph)
		{
			return FVector2D::ZeroVector;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
			{
				// AutoConnect時はComponentToLocalSpace変換ノードの通り道を確保するため、さらに左へずらす
				int32 BaseX = RootNode->NodePosX - GetAutoPlacementSpacingX();
				if (bAutoConnect)
				{
					BaseX -= KawaiiPhysicsPlacementAutoConnectBaseReserveX;
				}
				return FVector2D(static_cast<double>(BaseX), static_cast<double>(RootNode->NodePosY));
			}
		}

		return FVector2D::ZeroVector;
	}

	FIntRect MakePlacementRect(int32 NodePosX, int32 NodePosY, int32 NodeWidth, int32 NodeHeight)
	{
		return FIntRect(NodePosX, NodePosY, NodePosX + NodeWidth, NodePosY + NodeHeight);
	}

	bool DoPlacementRectsOverlap(const FIntRect& A, const FIntRect& B)
	{
		return A.Min.X < B.Max.X &&
			A.Max.X > B.Min.X &&
			A.Min.Y < B.Max.Y &&
			A.Max.Y > B.Min.Y;
	}

	bool DoesAutoPlacementOverlapExistingNode(UEdGraph* Graph, const FVector2D& NodePosition)
	{
		if (!Graph)
		{
			return false;
		}

		const FIntRect CandidateRect = MakePlacementRect(
			static_cast<int32>(NodePosition.X),
			static_cast<int32>(NodePosition.Y),
			KawaiiPhysicsPlacementExpectedNodeWidth,
			KawaiiPhysicsPlacementExpectedNodeHeight);

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			if (const UAnimGraphNode_KawaiiPhysics* KawaiiPhysicsNode = Cast<UAnimGraphNode_KawaiiPhysics>(Node))
			{
				if (DoPlacementRectsOverlap(
					CandidateRect,
					MakePlacementRect(
						KawaiiPhysicsNode->NodePosX,
						KawaiiPhysicsNode->NodePosY,
						KawaiiPhysicsPlacementExpectedNodeWidth,
						KawaiiPhysicsPlacementExpectedNodeHeight)))
				{
					return true;
				}
				continue;
			}

#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
			if (const UKawaiiPhysicsMcpCommentNode* CommentNode = Cast<UKawaiiPhysicsMcpCommentNode>(Node))
			{
				if (DoPlacementRectsOverlap(
					CandidateRect,
					MakePlacementRect(
						CommentNode->NodePosX,
						CommentNode->NodePosY,
						CommentNode->NodeWidth,
						CommentNode->NodeHeight)))
				{
					return true;
				}
				continue;
			}
#endif

			// 素のコメント枠（囲いのみ）は障害物として扱わない
			if (Node->IsA<UEdGraphNode_Comment>())
			{
				continue;
			}

			// KawaiiPhysics/MCPコメント以外のノード（変換ノード等）は推定サイズで重なり判定する
			if (DoPlacementRectsOverlap(
				CandidateRect,
				MakePlacementRect(
					Node->NodePosX,
					Node->NodePosY,
					KawaiiPhysicsPlacementEstimatedOtherNodeWidth,
					KawaiiPhysicsPlacementEstimatedOtherNodeHeight)))
			{
				return true;
			}
		}

		return false;
	}

	bool IsHorizontalAutoPlacement(const FResolvedKawaiiPhysicsNodePlacementRequest& Request)
	{
		// request指定を最優先し、次にSettings、Auto時はAutoConnectなら横配置にする。
		switch (Request.PlacementDirection)
		{
		case EKawaiiPhysicsNodePlacementDirectionOverride::Horizontal:
			return true;
		case EKawaiiPhysicsNodePlacementDirectionOverride::Vertical:
			return false;
		case EKawaiiPhysicsNodePlacementDirectionOverride::Default:
		default:
			break;
		}

		const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
		const EKawaiiPhysicsMcpNodePlacementDirection SettingsDirection =
			Settings ? Settings->McpNodePlacementDirection : EKawaiiPhysicsMcpNodePlacementDirection::Auto;
		switch (SettingsDirection)
		{
		case EKawaiiPhysicsMcpNodePlacementDirection::Horizontal:
			return true;
		case EKawaiiPhysicsMcpNodePlacementDirection::Vertical:
			return false;
		case EKawaiiPhysicsMcpNodePlacementDirection::Auto:
		default:
			return Request.bAutoConnect;
		}
	}

	int32 GetAutoPlacementWrapCount()
	{
		const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
		return Settings ? FMath::Max(Settings->McpNodePlacementWrapCount, 0) : 0;
	}

	int32 GetAutoPlacementSpacingX()
	{
		const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
		// 設定値が推定ノード幅を下回らないようにして、既定の重なりを避ける。
		return Settings
			       ? FMath::Max(Settings->McpNodePlacementSpacingX, KawaiiPhysicsPlacementExpectedNodeWidth)
			       : 420;
	}

	int32 GetAutoPlacementSpacingY()
	{
		const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
		// 設定値が推定ノード高さを下回らないようにして、既定の重なりを避ける。
		return Settings
			       ? FMath::Max(Settings->McpNodePlacementSpacingY, KawaiiPhysicsPlacementExpectedNodeHeight)
			       : 260;
	}

	FVector2D ResolveNodePosition(UEdGraph* Graph,
	                              const FResolvedKawaiiPhysicsNodePlacementRequest& Request,
	                              const FVector2D& AutoPlacementBasePosition,
	                              int32 AutoPlacementIndex,
	                              int32 TotalAutoPlacementCount)
	{
		if (!Request.bAutoPosition)
		{
			return Request.NodePosition;
		}

		const int32 WrapCount = GetAutoPlacementWrapCount();
		const int32 SpacingX = GetAutoPlacementSpacingX();
		const int32 SpacingY = GetAutoPlacementSpacingY();
		const int32 PrimaryIndex = WrapCount > 0 ? AutoPlacementIndex % WrapCount : AutoPlacementIndex;
		const int32 SecondaryIndex = WrapCount > 0 ? AutoPlacementIndex / WrapCount : 0;
		const bool bHorizontalPlacement = IsHorizontalAutoPlacement(Request);

		// AutoConnectのチェーンはリクエスト順=上流→下流のため、X軸を鏡映して
		// リクエスト順に左から右へ（最後のリクエストがResult直前=基準位置）並ぶようにする。
		// 横配置は1段あたりの列数、縦配置は折り返しの列数を基準に反転させる
		const int32 BlockCols = bHorizontalPlacement
			? (WrapCount > 0 ? FMath::Min(TotalAutoPlacementCount, WrapCount) : TotalAutoPlacementCount)
			: (WrapCount > 0 ? FMath::DivideAndRoundUp(TotalAutoPlacementCount, WrapCount) : 1);

		FVector2D NodePosition = bHorizontalPlacement
			? FVector2D(
				AutoPlacementBasePosition.X -
					static_cast<double>((BlockCols - 1 - PrimaryIndex) * SpacingX),
				AutoPlacementBasePosition.Y +
					static_cast<double>(SecondaryIndex * SpacingY))
			: FVector2D(
				AutoPlacementBasePosition.X -
					static_cast<double>((BlockCols - 1 - SecondaryIndex) * SpacingX),
				AutoPlacementBasePosition.Y +
					static_cast<double>(PrimaryIndex * SpacingY));

		// 既存ノードとの重なりを避けるため、空き位置が見つかるまで下方向へずらす。
		for (int32 AttemptIndex = 0;
		     AttemptIndex < KawaiiPhysicsPlacementMaxOverlapResolutionAttempts &&
		     DoesAutoPlacementOverlapExistingNode(Graph, NodePosition);
		     ++AttemptIndex)
		{
			NodePosition.Y += static_cast<double>(SpacingY);
		}

		if (DoesAutoPlacementOverlapExistingNode(Graph, NodePosition))
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("AddKawaiiPhysicsNodes: 自動配置の重なり回避が上限に達しました。最後に計算した座標を使用します。"));
		}

		return NodePosition;
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

		// Preset適用後、Requestで明示されたRootBone/ExcludeBones/AdditionalRootBones/Tagを上書きする。
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

		// 新しいBone設定からReconstructNodeでModifyBonesを再収集させるため、古い結果をクリアする。
		GraphNode->Node.ModifyBones.Empty();
		GraphNode->ReconstructNode();
	}

	bool DoesGraphNodeMatchKey(const UAnimGraphNode_KawaiiPhysics* GraphNode,
	                                 const FResolvedKawaiiPhysicsNodePlacementRequest& Request,
	                                 EKawaiiPhysicsPlacementMatchKey MatchKey)
	{
		if (!GraphNode)
		{
			return false;
		}

		const bool bTagMatches =
			Request.KawaiiPhysicsTag.IsValid() &&
			GraphNode->Node.KawaiiPhysicsTag == Request.KawaiiPhysicsTag;
		const bool bRootBoneMatches = GraphNode->Node.RootBone.BoneName == Request.RootBoneName;

		switch (MatchKey)
		{
		case EKawaiiPhysicsPlacementMatchKey::Tag:
			return bTagMatches;
		case EKawaiiPhysicsPlacementMatchKey::RootBone:
			return bRootBoneMatches;
		case EKawaiiPhysicsPlacementMatchKey::TagAndRootBone:
			return bTagMatches && bRootBoneMatches;
		case EKawaiiPhysicsPlacementMatchKey::None:
		default:
			return false;
		}
	}

	UAnimGraphNode_KawaiiPhysics* FindMatchingGraphNode(
		UEdGraph* Graph,
		const FResolvedKawaiiPhysicsNodePlacementRequest& Request,
		EKawaiiPhysicsPlacementMatchKey MatchKey)
	{
		if (!Graph || MatchKey == EKawaiiPhysicsPlacementMatchKey::None)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_KawaiiPhysics* GraphNode = Cast<UAnimGraphNode_KawaiiPhysics>(Node);
			if (DoesGraphNodeMatchKey(GraphNode, Request, MatchKey))
			{
				return GraphNode;
			}
		}

		return nullptr;
	}

	UAnimGraphNode_Root* FindResultRootNodeForEditorLibrary(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
			{
				return RootNode;
			}
		}

		return nullptr;
	}

	UEdGraphPin* FindFirstPosePinForEditorLibrary(UEdGraphNode* Node, EEdGraphPinDirection Dir)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Dir && UAnimationGraphSchema::IsPosePin(Pin->PinType))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	bool IsGraphNodePoseConnected(const UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		if (!GraphNode)
		{
			return false;
		}

		const UEdGraphPin* ComponentPosePin =
			GraphNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_SkeletalControlBase, ComponentPose), EGPD_Input);
		const UEdGraphPin* PosePin = GraphNode->FindPin(TEXT("Pose"), EGPD_Output);
		// 入力側または出力側のPose pinが接続済みなら、ノードは接続済みとみなす。
		return (ComponentPosePin && !ComponentPosePin->LinkedTo.IsEmpty()) ||
			(PosePin && !PosePin->LinkedTo.IsEmpty());
	}

	bool ConnectGraphNodeBeforeResult(UEdGraph* Graph,
	                                  UAnimGraphNode_KawaiiPhysics* GraphNode,
	                                  bool& bOutSpawnedConversionNode)
	{
		bOutSpawnedConversionNode = false;
		if (!Graph || !GraphNode)
		{
			return false;
		}

		UAnimGraphNode_Root* RootNode = FindResultRootNodeForEditorLibrary(Graph);
		if (!RootNode)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("AddKawaiiPhysicsNodes: Result node was not found in AnimGraph '%s'."),
			       *Graph->GetName());
			return false;
		}

		// Result入力pinと、新規ノード側の入出力Pose pinを取得する。
		UEdGraphPin* ResultPin = RootNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_Root, Result), EGPD_Input);
		UEdGraphPin* ComponentPosePin =
			GraphNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_SkeletalControlBase, ComponentPose), EGPD_Input);
		UEdGraphPin* PosePin = GraphNode->FindPin(TEXT("Pose"), EGPD_Output);
		if (!ResultPin || !ComponentPosePin || !PosePin)
		{
			return false;
		}

		// Result直前がComponentToLocalSpaceなら、その入力側へ挿入する。
		UEdGraphPin* InsertionPointPin = ResultPin;
		if (!ResultPin->LinkedTo.IsEmpty())
		{
			UEdGraphNode* ResultSourceNode = ResultPin->LinkedTo[0] ? ResultPin->LinkedTo[0]->GetOwningNode() : nullptr;
			if (UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalSpaceNode =
				Cast<UAnimGraphNode_ComponentToLocalSpace>(ResultSourceNode))
			{
				if (UEdGraphPin* ComponentToLocalSpaceInputPin =
					FindFirstPosePinForEditorLibrary(ComponentToLocalSpaceNode, EGPD_Input))
				{
					InsertionPointPin = ComponentToLocalSpaceInputPin;
				}
			}
		}

		UEdGraphPin* PreviousSourcePin =
			!InsertionPointPin->LinkedTo.IsEmpty() ? InsertionPointPin->LinkedTo[0] : nullptr;
		UEdGraphNode* PreviousSourceOwningNode = PreviousSourcePin ? PreviousSourcePin->GetOwningNode() : nullptr;

		Graph->Modify();
		GraphNode->Modify();
		const UAnimationGraphSchema* Schema = CastChecked<UAnimationGraphSchema>(Graph->GetSchema());
		bool bSpawnedInputConversionNode = false;
		TArray<UEdGraphNode*> SpawnedInputConversionNodes;
		if (PreviousSourcePin)
		{
			// 入力側接続の前後でノード集合を比較し、自動生成された変換ノードを検出する。
			TSet<UEdGraphNode*> NodesBeforePrevConnection;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				NodesBeforePrevConnection.Add(Node);
			}

			const int32 NodeCountBeforePrevConnection = Graph->Nodes.Num();
			if (!Schema->TryCreateConnection(PreviousSourcePin, ComponentPosePin))
			{
				return false;
			}

			bSpawnedInputConversionNode = Graph->Nodes.Num() > NodeCountBeforePrevConnection;
			if (bSpawnedInputConversionNode)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node && !NodesBeforePrevConnection.Contains(Node))
					{
						SpawnedInputConversionNodes.Add(Node);
					}
				}
			}

			if (bSpawnedInputConversionNode && !ComponentPosePin->LinkedTo.IsEmpty())
			{
				// 既存のローカル空間チェーンへ挿入する際に生成される変換ノード。
				// チェーン左隣は次ノードのスロットのため使えず、上へ逃がして配置する
				UEdGraphNode* ConversionNode = ComponentPosePin->LinkedTo[0]->GetOwningNode();
				if (ConversionNode && ConversionNode != PreviousSourceOwningNode)
				{
					ConversionNode->Modify();
					ConversionNode->NodePosX = GraphNode->NodePosX;
					ConversionNode->NodePosY = GraphNode->NodePosY - 150;
				}
			}
		}

		const int32 NodeCountBeforeResultConnection = Graph->Nodes.Num();
		if (!Schema->TryCreateConnection(PosePin, InsertionPointPin))
		{
			if (PreviousSourcePin)
			{
				// Result側接続に失敗したら入力側リンクを切り、生成済み変換ノードを削除して元のリンクを復元する。
				Schema->BreakPinLinks(*ComponentPosePin, true);

				if (!SpawnedInputConversionNodes.IsEmpty())
				{
					UAnimBlueprint* AnimBlueprint = GraphNode->GetAnimBlueprint();
					for (UEdGraphNode* SpawnedInputConversionNode : SpawnedInputConversionNodes)
					{
						if (!SpawnedInputConversionNode || !Graph->Nodes.Contains(SpawnedInputConversionNode))
						{
							continue;
						}

						SpawnedInputConversionNode->Modify();
						for (UEdGraphPin* SpawnedInputConversionPin : SpawnedInputConversionNode->Pins)
						{
							if (SpawnedInputConversionPin)
							{
								Schema->BreakPinLinks(*SpawnedInputConversionPin, true);
							}
						}

						if (AnimBlueprint)
						{
							FBlueprintEditorUtils::RemoveNode(AnimBlueprint, SpawnedInputConversionNode, true);
						}
						else
						{
							Graph->RemoveNode(SpawnedInputConversionNode);
						}
					}
				}

				if (!PreviousSourcePin->LinkedTo.Contains(InsertionPointPin))
				{
					UEdGraphNode* InsertionPointOwningNode = InsertionPointPin->GetOwningNode();
					if (PreviousSourceOwningNode)
					{
						PreviousSourceOwningNode->Modify();
					}
					if (InsertionPointOwningNode)
					{
						InsertionPointOwningNode->Modify();
					}
					PreviousSourcePin->MakeLinkTo(InsertionPointPin);
					if (PreviousSourceOwningNode)
					{
						PreviousSourceOwningNode->PinConnectionListChanged(PreviousSourcePin);
					}
					if (InsertionPointOwningNode)
					{
						InsertionPointOwningNode->PinConnectionListChanged(InsertionPointPin);
					}
					Graph->NotifyGraphChanged();
				}
			}
			return false;
		}

		// 接続成功時は入力側またはResult側で変換ノードが生成されたかを呼び出し元へ返す。
		bOutSpawnedConversionNode |= bSpawnedInputConversionNode;
		const bool bSpawnedResultConversionNode = Graph->Nodes.Num() > NodeCountBeforeResultConnection;
		bOutSpawnedConversionNode |= bSpawnedResultConversionNode;
		if (bSpawnedResultConversionNode && !ResultPin->LinkedTo.IsEmpty())
		{
			// Result直前へ挿入する際に生成される変換ノード。KawaiiPhysicsノード分の幅を確保した位置へ明示配置する
			if (UAnimGraphNode_ComponentToLocalSpace* ConversionNode =
				Cast<UAnimGraphNode_ComponentToLocalSpace>(ResultPin->LinkedTo[0]->GetOwningNode()))
			{
				ConversionNode->Modify();
				ConversionNode->NodePosX = RootNode->NodePosX - KawaiiPhysicsPlacementConversionNodeReserveX;
				ConversionNode->NodePosY = RootNode->NodePosY;
			}
		}
		return true;
	}
}

void UKawaiiPhysicsEditorLibrary::FindAnimBlueprintAssetData(const TArray<FString>& ContentPaths, TArray<FAssetData>& OutAssets)
{
	OutAssets.Reset();

	// filterを構築し、同期走査後にAnimBlueprintアセットを取得する。
	FARFilter Filter;
	ConfigureAnimBlueprintFilter(Filter);
	AddPackagePathsToFilter(Filter, ContentPaths);

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	ScanAssetRegistryPaths(AssetRegistry, ContentPaths);
	AssetRegistry.GetAssets(Filter, OutAssets);
}

void UKawaiiPhysicsEditorLibrary::FindAnimBlueprintAssetDataReferencingTags(
	const FGameplayTagContainer& FilterTags,
	bool bFilterExactMatch,
	const TArray<FString>& ContentPaths,
	TArray<FAssetData>& OutAssets)
{
	OutAssets.Reset();

	TArray<FAssetData> CandidateAssets;
	FindAnimBlueprintAssetData(ContentPaths, CandidateAssets);
	if (FilterTags.IsEmpty())
	{
		OutAssets = MoveTemp(CandidateAssets);
		return;
	}

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	const FKawaiiPhysicsTagNameFilter TagNameFilter = MakeTagNameFilter(FilterTags, bFilterExactMatch);
	const FName GameplayTagPackageName = FGameplayTag::StaticStruct()->GetOutermost()->GetFName();
	const FName GameplayTagStructName = FGameplayTag::StaticStruct()->GetFName();

	OutAssets.Reserve(CandidateAssets.Num());
	for (const FAssetData& AssetData : CandidateAssets)
	{
		if (IsDirtyPackageCandidate(AssetData))
		{
			OutAssets.Add(AssetData);
			continue;
		}

		TArray<FAssetIdentifier> Dependencies;
		AssetRegistry.GetDependencies(
			FAssetIdentifier(AssetData.PackageName),
			Dependencies,
			UE::AssetRegistry::EDependencyCategory::SearchableName);
		for (const FAssetIdentifier& Dependency : Dependencies)
		{
			if (Dependency.PackageName == GameplayTagPackageName &&
				Dependency.ObjectName == GameplayTagStructName &&
				TagNameFilter.Matches(Dependency.ValueName))
			{
				OutAssets.Add(AssetData);
				break;
			}
		}
	}
}

void UKawaiiPhysicsEditorLibrary::FindAllPresetAssetData(TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>>& OutPresets)
{
	OutPresets.Reset();

	// filterを構築し、同期走査後にPresetアセットを取得する。
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
	// path順にSortして、呼び出しごとの結果順を決定的にする。
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
			       TEXT("FindAllPresetAssetData: Failed to load KawaiiPhysics preset asset '%s'."),
			       *AssetData.GetSoftObjectPath().ToString());
		}
	}
}

TArray<UKawaiiPhysicsPresetDataAsset*> UKawaiiPhysicsEditorLibrary::FindAllPresetAssets()
{
	TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>> StrongPresets;
	FindAllPresetAssetData(StrongPresets);

	// 強参照版の結果をスクリプト向けの生ポインタ配列へ詰め替える。
	TArray<UKawaiiPhysicsPresetDataAsset*> Presets;
	Presets.Reserve(StrongPresets.Num());
	for (const TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>& Preset : StrongPresets)
	{
		Presets.Add(Preset.Get());
	}
	return Presets;
}

TArray<FSoftObjectPath> UKawaiiPhysicsEditorLibrary::FindAnimBlueprintAssets(const TArray<FString>& ContentPaths)
{
	TArray<FAssetData> AssetDataList;
	FindAnimBlueprintAssetData(ContentPaths, AssetDataList);

	// アセット情報をスクリプトから扱いやすいソフトオブジェクトパスへ変換する。
	TArray<FSoftObjectPath> AssetPaths;
	AssetPaths.Reserve(AssetDataList.Num());
	for (const FAssetData& AssetData : AssetDataList)
	{
		AssetPaths.Add(AssetData.GetSoftObjectPath());
	}
	return AssetPaths;
}

TArray<FSoftObjectPath> UKawaiiPhysicsEditorLibrary::FindAnimBlueprintAssetsReferencingTags(
	const FGameplayTagContainer& FilterTags, bool bFilterExactMatch, const TArray<FString>& ContentPaths)
{
	TArray<FAssetData> AssetDataList;
	FindAnimBlueprintAssetDataReferencingTags(FilterTags, bFilterExactMatch, ContentPaths, AssetDataList);

	// アセット情報をスクリプトから扱いやすいソフトオブジェクトパスへ変換する。
	TArray<FSoftObjectPath> AssetPaths;
	AssetPaths.Reserve(AssetDataList.Num());
	for (const FAssetData& AssetData : AssetDataList)
	{
		AssetPaths.Add(AssetData.GetSoftObjectPath());
	}
	return AssetPaths;
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

UAnimGraphNode_KawaiiPhysics* UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(
	const FSoftObjectPath& AnimBlueprintPath,
	const FGuid& NodeGuid)
{
	UObject* LoadedObject = AnimBlueprintPath.TryLoad();
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
	if (!AnimBlueprint || !NodeGuid.IsValid())
	{
		return nullptr;
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
			if (KawaiiPhysicsGraphNode && KawaiiPhysicsGraphNode->NodeGuid == NodeGuid)
			{
				return KawaiiPhysicsGraphNode;
			}
		}
	}

	return nullptr;
}

TArray<FKawaiiPhysicsGraphNodeHandle> UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
	UAnimBlueprint* AnimBlueprint,
	const TArray<FKawaiiPhysicsNodePlacementRequest>& Requests,
	EKawaiiPhysicsPlacementMatchKey MatchKey,
	FName GraphName,
	const FString& Comment,
	const FString& Prompt)
{
	TArray<FKawaiiPhysicsGraphNodeHandle> Result;
	if (!AnimBlueprint || Requests.IsEmpty())
	{
		return Result;
	}

	// 対象AnimGraphを解決する。
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
	TArray<FResolvedKawaiiPhysicsNodePlacementRequest> ResolvedRequests;
	ResolvedRequests.Reserve(Requests.Num());
	// RequestをPresetやPattern込みの配置指定へ解決する。
	for (const FKawaiiPhysicsNodePlacementRequest& Request : Requests)
	{
		ResolvedRequests.Add(ResolvePlacementRequest(TargetSkeleton, Request));
	}
	const TArray<FName> AllResolvedRootBoneNames = CollectResolvedRootBoneNames(ResolvedRequests);

	const FVector2D AutoPlacementBasePosition =
		GetAutoPlacementBasePosition(Graph, AnyResolvedRequestHasAutoConnect(ResolvedRequests));

	// 横配置がリクエスト順に左から右へ並ぶよう、事前にバリデーションを済ませて
	// 自動配置対象の総数(TotalAutoPlacementCount)を数えておく。ログはここで1回だけ出す
	TArray<bool> RequestValidities;
	RequestValidities.SetNum(Requests.Num());
	int32 TotalAutoPlacementCount = 0;
	for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
	{
		const FResolvedKawaiiPhysicsNodePlacementRequest& ResolvedRequest = ResolvedRequests[RequestIndex];

		TArray<FString> ValidationMessages;
		const bool bRequestValid = ValidateResolvedPlacementRequest(
			AnimBlueprint,
			Requests[RequestIndex],
			ResolvedRequest,
			AllResolvedRootBoneNames,
			RequestIndex,
			ValidationMessages);

		for (const FString& ValidationMessage : ValidationMessages)
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT("AddKawaiiPhysicsNodes: %s"), *ValidationMessage);
		}

		RequestValidities[RequestIndex] = bRequestValid;
		if (bRequestValid && ResolvedRequest.bAutoPosition)
		{
			++TotalAutoPlacementCount;
		}
	}

	int32 AutoPlacementIndex = 0;
	bool bAddedNode = false;
	bool bUpdatedNode = false;
	bool bConnectedNode = false;
	bool bSpawnedConversionNode = false;
	bool bAddedCommentNode = false;

	for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
	{
		if (!RequestValidities[RequestIndex])
		{
			continue;
		}

		const FResolvedKawaiiPhysicsNodePlacementRequest& ResolvedRequest = ResolvedRequests[RequestIndex];

		const FVector2D NodePosition = ResolveNodePosition(
			Graph,
			ResolvedRequest,
			AutoPlacementBasePosition,
			AutoPlacementIndex,
			TotalAutoPlacementCount);
		if (ResolvedRequest.bAutoPosition)
		{
			++AutoPlacementIndex;
		}

		if (UAnimGraphNode_KawaiiPhysics* ExistingGraphNode =
			FindMatchingGraphNode(Graph, ResolvedRequest, MatchKey))
		{
			// Match対象が既にある場合は既存ノードを更新する。
			ExistingGraphNode->Modify();
			ApplyResolvedPlacementToGraphNode(ExistingGraphNode, ResolvedRequest);
			if (!ResolvedRequest.bAutoPosition)
			{
				ExistingGraphNode->NodePosX = static_cast<int32>(NodePosition.X);
				ExistingGraphNode->NodePosY = static_cast<int32>(NodePosition.Y);
			}
			FinalizeExistingPlacementGraphNode(ExistingGraphNode);
			// 必要な場合だけResult手前へのAutoConnectを試みる。
			if (ResolvedRequest.bAutoConnect && !IsGraphNodePoseConnected(ExistingGraphNode))
			{
				bool bSpawnedConversionNodeForConnection = false;
				if (ConnectGraphNodeBeforeResult(Graph, ExistingGraphNode, bSpawnedConversionNodeForConnection))
				{
					bConnectedNode = true;
					bSpawnedConversionNode |= bSpawnedConversionNodeForConnection;
				}
				else
				{
					UE_LOG(LogKawaiiPhysics, Warning,
					       TEXT("AddKawaiiPhysicsNodes: Request[%d] RootBone=%s auto-connect failed. Node was left unconnected."),
					       RequestIndex,
					       *ResolvedRequest.RootBoneName.ToString());
				}
			}

			FKawaiiPhysicsGraphNodeHandle Handle;
			Handle.Node = ExistingGraphNode;
			Result.Add(Handle);
			bUpdatedNode = true;
			continue;
		}

		// Match対象がなければ新規ノードを作成する。
		Graph->Modify();
		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysics* NewGraphNode = NodeCreator.CreateNode(false);
		ApplyResolvedPlacementToGraphNode(NewGraphNode, ResolvedRequest);
		NewGraphNode->NodePosX = static_cast<int32>(NodePosition.X);
		NewGraphNode->NodePosY = static_cast<int32>(NodePosition.Y);
		NodeCreator.Finalize();
		// 新規ノードもRequest指定に応じてResult手前へのAutoConnectを試みる。
		if (ResolvedRequest.bAutoConnect)
		{
			bool bSpawnedConversionNodeForConnection = false;
			if (ConnectGraphNodeBeforeResult(Graph, NewGraphNode, bSpawnedConversionNodeForConnection))
			{
				bConnectedNode = true;
				bSpawnedConversionNode |= bSpawnedConversionNodeForConnection;
			}
			else
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("AddKawaiiPhysicsNodes: Request[%d] RootBone=%s auto-connect failed. Node was left unconnected."),
				       RequestIndex,
				       *ResolvedRequest.RootBoneName.ToString());
			}
		}

		FKawaiiPhysicsGraphNodeHandle Handle;
		Handle.Node = NewGraphNode;
		Result.Add(Handle);
		bAddedNode = true;
	}

	const FString TrimmedComment = Comment.TrimStartAndEnd();
	if (!Result.IsEmpty() && !TrimmedComment.IsEmpty())
	{
		// 生成・更新したノード群を囲むMCPコメント枠を検索または追加する。
		const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
		const FString CommentPrefix = Settings ? Settings->McpCommentPrefix : TEXT("[MCP] ");
		bAddedCommentNode = FindOrAddMcpCommentNode(Graph, Result, CommentPrefix + TrimmedComment, Prompt);
	}

	// 構造変更、軽微な変更、変更なしを分けてBlueprintの変更状態とTransactionを確定する。
	if (bAddedNode || bSpawnedConversionNode || bAddedCommentNode)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	}
	else if (bUpdatedNode || bConnectedNode)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	}
	else
	{
		Transaction.Cancel();
	}

	return Result;
}

TArray<FKawaiiPhysicsAnimGraphCommentInfo> UKawaiiPhysicsEditorLibrary::GetAnimGraphComments(
	UAnimBlueprint* AnimBlueprint,
	FName GraphName)
{
	TArray<FKawaiiPhysicsAnimGraphCommentInfo> Result;
	UEdGraph* Graph = FindPlacementAnimGraph(AnimBlueprint, GraphName);
	if (!Graph)
	{
		return Result;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node);
		if (!CommentNode)
		{
			continue;
		}

		FKawaiiPhysicsAnimGraphCommentInfo Info;
		Info.Title = CommentNode->NodeComment;
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
		if (const UKawaiiPhysicsMcpCommentNode* McpCommentNode = Cast<UKawaiiPhysicsMcpCommentNode>(CommentNode))
		{
			Info.Prompt = McpCommentNode->Prompt;
			Info.CreatedAt = McpCommentNode->CreatedAt;
			Info.UpdatedAt = McpCommentNode->UpdatedAt;
			Info.bMcpComment = true;
		}
#endif
		Result.Add(Info);
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

TArray<FName> UKawaiiPhysicsEditorLibrary::FindBonesByPattern(USkeleton* Skeleton, const FString& Pattern)
{
	TArray<FName> Result;
	if (!Skeleton || Pattern.IsEmpty())
	{
		return Result;
	}

	FString BoneListString;
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FMeshBoneInfo>& RefBoneInfo = RefSkeleton.GetRefBoneInfo();
	// Bone名をカンマ区切りの1文字列へ連結し、その全体にregexを適用する。
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
			// NAME_SIZEを超える一致はFName化できないためスキップし、警告は1回だけ出す。
			if (!bLongMatchWarningLogged)
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("FindBonesByPattern: Regex match is too long to convert to FName. Pattern=\"%s\", MatchLength=%d, MaxLength=%d"),
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

bool UKawaiiPhysicsEditorLibrary::SetGraphNodePropertyFromString(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FName PropertyName,
	const FString& Value)
{
	return ModifyGraphNodeProperty(
		Handle,
		PropertyName,
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "SetGraphNodePropertyFromString", "Set Kawaii Physics Graph Node Property"),
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

bool UKawaiiPhysicsEditorLibrary::SetPresetNodePropertyFromString(
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

bool UKawaiiPhysicsEditorLibrary::SetPresetDescription(UKawaiiPhysicsPresetDataAsset* Preset, const FText& Description)
{
	if (!Preset)
	{
		UE_LOG(LogKawaiiPhysics, Warning, TEXT("SetPresetDescription: Preset is null."));
		return false;
	}

	// 対象タグ設定と同じ編集フローで説明文を更新する。
	Preset->Modify();
	Preset->Description = Description;
	Preset->MarkPackageDirty();
	return true;
}

FText UKawaiiPhysicsEditorLibrary::GetPresetDescription(const UKawaiiPhysicsPresetDataAsset* Preset)
{
	// 未指定時は空のテキストを返す。
	return Preset ? Preset->Description : FText::GetEmpty();
}

bool UKawaiiPhysicsEditorLibrary::SetGraphNodeTag(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	FGameplayTag Tag)
{
	return ModifyGraphNodeProperty(
		Handle,
		GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag),
		NSLOCTEXT("KawaiiPhysicsEditorLibrary", "SetGraphNodeTag", "Set Kawaii Physics Graph Node Tag"),
		[Tag](UAnimGraphNode_KawaiiPhysics& GraphNode)
		{
			GraphNode.Node.KawaiiPhysicsTag = Tag;
			return true;
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
	return ModifyGraphNodeProperty(
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
		// DataAsset参照の差し替えはPostEditChangePropertyで通知する。
		bNotifiedPropertyChange |= NotifyGraphNodePropertyChanged(
			GraphNode,
			FindFProperty<FProperty>(
				FAnimNode_KawaiiPhysics::StaticStruct(),
				GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset)));
	}
	if (OldBoneConstraintsDataAsset != GraphNode->Node.BoneConstraintsDataAsset)
	{
		// DataAsset参照の差し替えはPostEditChangePropertyで通知する。
		bNotifiedPropertyChange |= NotifyGraphNodePropertyChanged(
			GraphNode,
			FindFProperty<FProperty>(
				FAnimNode_KawaiiPhysics::StaticStruct(),
				GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintsDataAsset)));
	}

	if (!bNotifiedPropertyChange)
	{
		// 参照差し替えがない場合だけReconstructNodeで通常の再構築を行う。
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

TArray<FKawaiiPhysicsPresetDiffValue> UKawaiiPhysicsEditorLibrary::GetGraphNodePresetDiffValues(
	const FKawaiiPhysicsGraphNodeHandle& Handle,
	UKawaiiPhysicsPresetDataAsset* Preset,
	FKawaiiPhysicsPresetApplyOptions Options)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = GetGraphNode(Handle);
	if (!GraphNode || !Preset)
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("GetGraphNodePresetDiffValues: Invalid graph node handle or preset. HandleValid=%s PresetValid=%s."),
		       GraphNode ? TEXT("true") : TEXT("false"),
		       Preset ? TEXT("true") : TEXT("false"));
		return TArray<FKawaiiPhysicsPresetDiffValue>();
	}

	return KawaiiPhysicsPresetDiff::BuildDiffValues(GraphNode->Node, *Preset, Options);
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

int32 UKawaiiPhysicsEditorLibrary::ApplyPresetToProject(
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
		// TargetTagsが空なら対象ノードが存在しないため、何もせず戻る。
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("ApplyPresetToProject: Preset '%s' has empty TargetTags. No nodes will be targeted."),
		       *Preset->GetPathName());
		return 0;
	}

	TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>> Presets;
	Presets.Emplace(Preset);
	const FKawaiiPhysicsPresetApplyOptions Options;

	TArray<FAssetData> AnimBlueprintAssets;
	FindAnimBlueprintAssetDataReferencingTags(
		Preset->TargetTags,
		Preset->bTargetTagsExactMatch,
		TArray<FString>(),
		AnimBlueprintAssets);

	// アセット数が多いとロード＋適用に時間がかかるため、進捗ダイアログを表示する（キャンセル非対応）。
	FScopedSlowTask SlowTask(static_cast<float>(AnimBlueprintAssets.Num()),
	                          NSLOCTEXT("KawaiiPhysicsEditorLibrary", "ApplyPresetToProjectProgress",
	                                    "Applying Kawaii Physics preset to AnimBlueprints..."));
	if (!IsRunningCommandlet() && !FApp::IsUnattended() && FSlateApplication::IsInitialized())
	{
		SlowTask.MakeDialog();
	}

	int32 AppliedNodeCount = 0;
	int32 MatchedNodeCount = 0;
	int32 SkippedNodeCount = 0;
	int32 ProcessedAssetCount = 0;
	// GC対策: 適用済みのAnimBlueprintを関数終了まで保持する
	TArray<TStrongObjectPtr<UAnimBlueprint>> ModifiedAnimBlueprints;
	for (const FAssetData& AssetData : AnimBlueprintAssets)
	{
		SlowTask.EnterProgressFrame(1.0f, FText::FromName(AssetData.AssetName));

		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
		if (!AnimBlueprint)
		{
			continue;
		}

		bool bPackageCheckedOut = false;
		bool bPackageCheckoutAttempted = false;
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
				// dryRunでは監査エントリの収集だけを行い、ノードは変更しない。
				continue;
			}

			if (!bPackageCheckoutAttempted)
			{
				// checkoutはアセットごとに最大1回だけ試み、失敗時は以降の一致ノードをskip数に入れる。
				bPackageCheckoutAttempted = true;
				UPackage* Package = AnimBlueprint->GetOutermost();
				bPackageCheckedOut = CheckOutPackageIfNeeded(Package, bCheckOutFiles);
				if (!bPackageCheckedOut)
				{
					UE_LOG(LogKawaiiPhysics, Warning,
					       TEXT("ApplyPresetToProject: Failed to check out package '%s'. Skipping matched nodes in this asset."),
					       Package ? *Package->GetName() : TEXT("<None>"));
				}
			}
			if (!bPackageCheckedOut)
			{
				++SkippedNodeCount;
				continue;
			}

			if (ApplyPresetToGraphNode(Handle, Preset, Options))
			{
				++AppliedNodeCount;
				if (!ModifiedAnimBlueprints.ContainsByPredicate(
					[AnimBlueprint](const TStrongObjectPtr<UAnimBlueprint>& ModifiedAnimBlueprint)
					{
						return ModifiedAnimBlueprint.Get() == AnimBlueprint;
					}))
				{
					ModifiedAnimBlueprints.Emplace(AnimBlueprint);
				}
			}
		}

		++ProcessedAssetCount;
		if (ProcessedAssetCount % KawaiiPhysicsEditorLibraryGCBatchSize == 0)
		{
			// 大量アセット処理中は一定件数ごとにGCする。
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	UE_LOG(LogKawaiiPhysics, Display,
	       TEXT("ApplyPresetToProject: Preset=%s MatchedNodes=%d AppliedNodes=%d SkippedNodes=%d"),
	       *Preset->GetPathName(),
	       MatchedNodeCount,
	       AppliedNodeCount,
	       SkippedNodeCount);
	return AppliedNodeCount;
}

bool UKawaiiPhysicsEditorLibrary::AuditKawaiiPhysicsNodes(
	const TArray<FString>& ContentPaths,
	const FGameplayTagContainer& FilterTags,
	bool bFilterExactMatch,
	TArray<FKawaiiPhysicsNodeAuditEntry>& OutEntries,
	bool bIncludeDiffValues)
{
	OutEntries.Reset();

	TArray<FAssetData> AnimBlueprintAssets;
	FindAnimBlueprintAssetDataReferencingTags(FilterTags, bFilterExactMatch, ContentPaths, AnimBlueprintAssets);

	TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>> Presets;
	UKawaiiPhysicsEditorLibrary::FindAllPresetAssetData(Presets);

	const FKawaiiPhysicsPresetApplyOptions Options;
	bool bResult = true;
	int32 ProcessedAssetCount = 0;
	for (const FAssetData& AssetData : AnimBlueprintAssets)
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
		if (!AnimBlueprint)
		{
			// ロード失敗は結果をfalseにするが、他のアセットの監査は継続する。
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
				OutEntries.Add(MakeAuditEntry(AnimBlueprint, GraphNode, Options, Presets, bIncludeDiffValues));
			}
		}

		++ProcessedAssetCount;
		if (ProcessedAssetCount % KawaiiPhysicsEditorLibraryGCBatchSize == 0)
		{
			// 大量アセット監査中は一定件数ごとにGCする。
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

	// wildcard pinの値をscript stackからFPropertyとアドレスとして取り出すcustom thunkである。
	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	const void* ValuePtr = Stack.MostRecentPropertyAddress;

	if (ModifyGraphNodeProperty(
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

	// wildcard pinの値をscript stackからFPropertyとアドレスとして取り出すcustom thunkである。
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
