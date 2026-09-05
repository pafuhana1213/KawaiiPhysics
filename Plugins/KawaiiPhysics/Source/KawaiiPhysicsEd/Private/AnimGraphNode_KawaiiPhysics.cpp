// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimGraphNode_KawaiiPhysics.h"

#include "AnimGraphNode_KawaiiPhysicsSharedPublisher.h"
#include "KawaiiPhysicsLibrary.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "IContentBrowserSingleton.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsEditorCategoryNames.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsPresetDiffSnapshot.h"
#include "KawaiiPhysicsEdUtils.h"
#include "KawaiiPhysicsEdWindowUtils.h"
#include "SKawaiiPhysicsPresetDiffWindow.h"
#include "SKawaiiPhysicsWindScopeWindow.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Selection.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ConfigCacheIni.h"
#include "PropertyHandle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSeparator.h"

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimGraphNode_KawaiiPhysics)

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

namespace
{
	const TCHAR* const CategoryFilterConfigSection = TEXT("KawaiiPhysicsEd");
	const TCHAR* const CategoryFilterConfigKey = TEXT("NodeDetailsCategoryFilter");

	const KawaiiPhysicsEditorCategoryNames::FCategoryFilterGroup* FindCategoryFilterGroup(const FName GroupId)
	{
		for (const KawaiiPhysicsEditorCategoryNames::FCategoryFilterGroup& Group :
		     KawaiiPhysicsEditorCategoryNames::GetFilterGroups())
		{
			if (Group.GroupId == GroupId)
			{
				return &Group;
			}
		}

		return nullptr;
	}

	FName NormalizeCategoryFilterGroupId(const FName GroupId)
	{
		return FindCategoryFilterGroup(GroupId) ? GroupId : NAME_None;
	}

	FName ReadKawaiiPhysicsCategoryFilter()
	{
		FString FilterValue;
		if (GConfig)
		{
			GConfig->GetString(CategoryFilterConfigSection, CategoryFilterConfigKey, FilterValue, GEditorPerProjectIni);
		}

		return FilterValue.IsEmpty() ? NAME_None : NormalizeCategoryFilterGroupId(FName(*FilterValue));
	}

	void HideCategoriesOutsideFilter(IDetailLayoutBuilder& DetailBuilder, const FName GroupId)
	{
		if (GroupId.IsNone())
		{
			return;
		}

		const KawaiiPhysicsEditorCategoryNames::FCategoryFilterGroup* FilterGroup = FindCategoryFilterGroup(GroupId);
		if (!FilterGroup)
		{
			return;
		}

		for (const FName& CategoryName : KawaiiPhysicsEditorCategoryNames::GetCategorySortOrderNames())
		{
			if (CategoryName != KawaiiPhysicsEditorCategoryNames::KawaiiPhysicsTools &&
				CategoryName != KawaiiPhysicsEditorCategoryNames::CategoryFilter &&
				!FilterGroup->CategoryNames.Contains(CategoryName))
			{
				DetailBuilder.HideCategory(CategoryName);
			}
		}

		for (const FName& CategoryName : KawaiiPhysicsEditorCategoryNames::GetFilterAdditionalHiddenNames())
		{
			DetailBuilder.HideCategory(CategoryName);
		}
	}

	void ShowKawaiiPhysicsNotification(const FText& NotificationText,
	                                   const SNotificationItem::ECompletionState CompletionState)
	{
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> NotificationItem =
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(CompletionState);
		}
	}

	void ShowKawaiiPhysicsAssetNotification(UObject* Asset,
	                                        const FText& NotificationText,
	                                        const FText& HyperlinkText,
	                                        const SNotificationItem::ECompletionState CompletionState)
	{
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.0f;
		if (Asset)
		{
			NotificationInfo.Hyperlink = FSimpleDelegate::CreateLambda([Asset]()
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
			});
			NotificationInfo.HyperlinkText = HyperlinkText;
		}

		TSharedPtr<SNotificationItem> NotificationItem =
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(CompletionState);
		}
	}

	bool IsAnimNodePropertyDifferent(const FAnimNode_KawaiiPhysics& Left,
	                                 const FAnimNode_KawaiiPhysics& Right,
	                                 const FName PropertyName)
	{
		const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
		return Property && !Property->Identical_InContainer(&Left, &Right);
	}

	void FocusKawaiiPhysicsGraphNode(UEdGraphNode* GraphNode)
	{
		if (!GraphNode)
		{
			return;
		}

		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(GraphNode);
		if (UEdGraph* Graph = GraphNode->GetGraph())
		{
			TSet<const UEdGraphNode*> NodesToSelect;
			NodesToSelect.Add(GraphNode);
			Graph->SelectNodeSet(NodesToSelect, true);
		}
	}

	bool IsUsingSharedSimpleWorldCollisionPublisher(const FAnimNode_KawaiiPhysics& Node)
	{
		return Node.bUseSimpleWorldCollision &&
			Node.SimpleWorldCollisionSource != EKawaiiPhysicsSimpleWorldCollisionSource::Local;
	}

	FString JoinPropertyNames(const TArray<FName>& PropertyNames)
	{
		TArray<FString> Strings;
		Strings.Reserve(PropertyNames.Num());
		for (const FName& PropertyName : PropertyNames)
		{
			Strings.Add(PropertyName.ToString());
		}
		return FString::Join(Strings, TEXT(", "));
	}

	bool IsProceduralWindExternalForcesEdit(const FPropertyChangedChainEvent& PropertyChangedEvent)
	{
		for (FEditPropertyChain::TDoubleLinkedListNode* ChainNode =
			     PropertyChangedEvent.PropertyChain.GetHead();
		     ChainNode;
		     ChainNode = ChainNode->GetNextNode())
		{
			const FProperty* ChainProperty = ChainNode->GetValue();
			if (ChainProperty &&
				ChainProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces))
			{
				return true;
			}
		}

		const FProperty* ChangedProperty = PropertyChangedEvent.Property;
		const UStruct* OwnerStruct = ChangedProperty ? ChangedProperty->GetOwnerStruct() : nullptr;
		return OwnerStruct &&
			(OwnerStruct == FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct() ||
				OwnerStruct->IsChildOf(FKawaiiPhysics_ExternalForce::StaticStruct()));
	}

	bool IsProceduralWindStructProperty(const FName PropertyName)
	{
		return PropertyName != NAME_None &&
			FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct()->FindPropertyByName(PropertyName) != nullptr;
	}

	bool IsProceduralWindExternalForce(const FInstancedStruct& ExternalForce)
	{
		return ExternalForce.IsValid() &&
			ExternalForce.GetScriptStruct() == FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct();
	}

	bool IsOtherExternalForceStructProperty(const FProperty* Property)
	{
		const UStruct* OwnerStruct = Property ? Property->GetOwnerStruct() : nullptr;
		return OwnerStruct &&
			OwnerStruct->IsChildOf(FKawaiiPhysics_ExternalForce::StaticStruct()) &&
			OwnerStruct != FKawaiiPhysics_ExternalForce::StaticStruct() &&
			OwnerStruct != FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct();
	}
}

// ----------------------------------------------------------------------------
UAnimGraphNode_KawaiiPhysics::UAnimGraphNode_KawaiiPhysics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_KawaiiPhysics::GetControllerDescription() const
{
	return LOCTEXT("Kawaii Physics", "Kawaii Physics");
}


// ----------------------------------------------------------------------------
FText UAnimGraphNode_KawaiiPhysics::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if ((TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle))
	{
		return GetControllerDescription();
	}
	// @TODO: the bone can be altered in the property editor, so we have to 
	//        choose to mark this dirty when that happens for this to properly work
	//if (!CachedNodeTitles.IsTitleCached(TitleType, this))

	FFormatNamedArguments Args;
	Args.Add(TEXT("ControllerDescription"), GetControllerDescription());
	Args.Add(TEXT("RootBoneName"), FText::FromName(Node.RootBone.BoneName));
	Args.Add(TEXT("Tag"), FText::FromString(Node.KawaiiPhysicsTag.ToString()));

	// FText::Format() is slow, so we cache this to save on performance
	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		const FText Title = Node.KawaiiPhysicsTag.IsValid()
			                    ? FText::Format(
				                    LOCTEXT("AnimGraphNode_KawaiiPhysics_ListTitleWithTag",
				                            "{ControllerDescription} - Root: {RootBoneName} - Tag: {Tag}"), Args)
			                    : FText::Format(
				                    LOCTEXT("AnimGraphNode_KawaiiPhysics_ListTitle",
				                            "{ControllerDescription} - Root: {RootBoneName}"), Args);

		CachedNodeTitles.SetCachedTitle(TitleType, Title, this);
	}
	else
	{
		const FText Title = Node.KawaiiPhysicsTag.IsValid()
			                    ? FText::Format(
				                    LOCTEXT("AnimGraphNode_KawaiiPhysics_TitleWithTag",
				                            "{ControllerDescription}\nRoot: {RootBoneName}\nTag:  {Tag} "), Args)
			                    : FText::Format(
				                    LOCTEXT("AnimGraphNode_KawaiiPhysics_Title",
				                            "{ControllerDescription}\nRoot: {RootBoneName}"), Args);

		CachedNodeTitles.SetCachedTitle(TitleType, Title, this);
	}
	return CachedNodeTitles[TitleType];
}

void UAnimGraphNode_KawaiiPhysics::EnsureUniqueCollisionGuids()
{
	// コリジョン配列のGuidを一意化する（複製/貼り付けでGuidごとコピーされ重複しうるため）。
	TSet<FGuid> SeenGuids;
	auto Dedup = [&SeenGuids](auto& Limits)
	{
		for (auto& Limit : Limits)
		{
			if (!Limit.Guid.IsValid() || SeenGuids.Contains(Limit.Guid))
			{
				Limit.Guid = FGuid::NewGuid();
			}
			SeenGuids.Add(Limit.Guid);
		}
	};
	Dedup(Node.SphericalLimits);
	Dedup(Node.CapsuleLimits);
	Dedup(Node.TaperedCapsuleLimits);
	Dedup(Node.BoxLimits);
	Dedup(Node.PlanarLimits);
}

void UAnimGraphNode_KawaiiPhysics::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 複製/貼り付け等でGuidが重複しうるため一意化する（EditModeのGuid削除が誤削除しないように）
	EnsureUniqueCollisionGuids();

	Node.ModifyBones.Empty();
	ReconstructNode();
}

void UAnimGraphNode_KawaiiPhysics::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// 既定実装が PostEditChangeProperty（ReconstructNode 等）を呼ぶため先に通す
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	PushProceduralWindEditToLiveInstance(PropertyChangedEvent);
}

void UAnimGraphNode_KawaiiPhysics::PushProceduralWindEditToLiveInstance(
	const FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (!IsProceduralWindExternalForcesEdit(PropertyChangedEvent))
	{
		return;
	}

	FAnimNode_KawaiiPhysics* RuntimeNode = KawaiiPhysicsEdUtils::ResolveLiveKawaiiPhysicsNode(this);
	if (!RuntimeNode ||
		!KawaiiPhysicsEdUtils::IsExternalForceShapeMatched(Node.ExternalForces, RuntimeNode->ExternalForces))
	{
		return;
	}

	const FName EditedPropertyName = PropertyChangedEvent.Property
		                                 ? PropertyChangedEvent.Property->GetFName()
		                                 : NAME_None;

	const auto PushWindAtIndex = [this, RuntimeNode, EditedPropertyName](const int32 Index)
	{
		const FKawaiiPhysics_ExternalForce_ProceduralWind* GraphWind =
			Node.ExternalForces[Index].GetPtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
		FKawaiiPhysics_ExternalForce_ProceduralWind* RuntimeWind =
			RuntimeNode->ExternalForces[Index].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
		if (!GraphWind || !RuntimeWind)
		{
			return;
		}

		FKawaiiProceduralWindDynamicParams Params;
		if (!GraphWind->BuildDynamicParamsForProperty(EditedPropertyName, Params))
		{
			if (IsProceduralWindStructProperty(EditedPropertyName))
			{
				// 未対応の ProceduralWind メンバは編集値を DynamicParams に載せられないためここでは送らない。
				// スナップショット送信はその編集値を含まないまま PIE 側の対応済み項目を上書きしてしまう。
				return;
			}
			Params = GraphWind->BuildDynamicParamsSnapshot();
		}
		RuntimeWind->RequestDynamicParams(Params);
	};

	const int32 EditedIndex = PropertyChangedEvent.GetArrayIndex(TEXT("ExternalForces"));
	if (EditedIndex != INDEX_NONE)
	{
		if (!Node.ExternalForces.IsValidIndex(EditedIndex) ||
			!RuntimeNode->ExternalForces.IsValidIndex(EditedIndex))
		{
			return;
		}

		PushWindAtIndex(EditedIndex);
		return;
	}

	if (IsOtherExternalForceStructProperty(PropertyChangedEvent.Property))
	{
		return;
	}

	// 配列インデックスを特定できない単一 ProceduralWind 構成が主用途のため、全 ProceduralWind へのフォールバックは残す。
	// Persona では CopyNodeDataToPreviewNode の直接同期と二重になるが同値なので無害。PIE では再コンパイル不要で反映される。
	for (int32 Index = 0; Index < Node.ExternalForces.Num(); ++Index)
	{
		PushWindAtIndex(Index);
	}
}

void UAnimGraphNode_KawaiiPhysics::PostLoad()
{
	Super::PostLoad();

	// 本修正以前に複製された古いデータが重複Guidを持つ場合に備え、ロード時にも一意化する
	EnsureUniqueCollisionGuids();
}

FEditorModeID UAnimGraphNode_KawaiiPhysics::GetEditorMode() const
{
	return "AnimGraph.SkeletalControl.KawaiiPhysics";
}

void UAnimGraphNode_KawaiiPhysics::ValidateAnimNodePostCompile(FCompilerResultsLog& MessageLog,
                                                               UAnimBlueprintGeneratedClass* CompiledClass,
                                                               int32 CompiledNodeIndex)
{
	UAnimGraphNode_SkeletalControlBase::ValidateAnimNodePostCompile(MessageLog, CompiledClass, CompiledNodeIndex);

	Node.RootBone.Initialize(CompiledClass->TargetSkeleton);
	if (Node.RootBone.BoneIndex >= 0)
	{
		if (Node.ExcludeBones.Contains(Node.RootBone))
		{
			MessageLog.Warning(TEXT("@@ ExcludeBones should NOT has RootBone."), this);
		}
	}
	// for template ABP
	else if (CompiledClass->TargetSkeleton)
	{
		MessageLog.Warning(TEXT("@@ RootBone is empty."), this);
	}
}

void UAnimGraphNode_KawaiiPhysics::CopyNodeDataToPreviewNode(FAnimNode_Base* AnimNode)
{
	FAnimNode_KawaiiPhysics* KawaiiPhysics = static_cast<FAnimNode_KawaiiPhysics*>(AnimNode);

	// pushing properties to preview instance, for live editing
	// Default
	KawaiiPhysics->RootBone = Node.RootBone;
	KawaiiPhysics->ExcludeBones = Node.ExcludeBones;
	KawaiiPhysics->AdditionalRootBones = Node.AdditionalRootBones;
	KawaiiPhysics->TargetFramerate = Node.TargetFramerate;

	// Physics Settings
	KawaiiPhysics->PhysicsSettings = Node.PhysicsSettings;
	KawaiiPhysics->DampingCurveData = Node.DampingCurveData;
	KawaiiPhysics->WorldDampingLocationCurveData = Node.WorldDampingLocationCurveData;
	KawaiiPhysics->WorldDampingRotationCurveData = Node.WorldDampingRotationCurveData;
	KawaiiPhysics->StiffnessCurveData = Node.StiffnessCurveData;
	KawaiiPhysics->RadiusCurveData = Node.RadiusCurveData;
	KawaiiPhysics->LimitAngleCurveData = Node.LimitAngleCurveData;
	KawaiiPhysics->bUpdatePhysicsSettingsInGame = Node.bUpdatePhysicsSettingsInGame;
	KawaiiPhysics->PlanarConstraint = Node.PlanarConstraint;
	KawaiiPhysics->ResetBoneTransformWhenBoneNotFound = Node.ResetBoneTransformWhenBoneNotFound;

	// DummyBone
	KawaiiPhysics->DummyBoneLength = Node.DummyBoneLength;
	KawaiiPhysics->BoneSubdivisionCount = Node.BoneSubdivisionCount;
	KawaiiPhysics->bBoneSubdivisionCollisionOnly = Node.bBoneSubdivisionCollisionOnly;
	KawaiiPhysics->bBoneSubdivisionDensifyByRadius = Node.bBoneSubdivisionDensifyByRadius;
	KawaiiPhysics->BoneConstraintSubdivisionCount = Node.BoneConstraintSubdivisionCount;
	KawaiiPhysics->BoneConstraintSubdivisionFeedbackScale = Node.BoneConstraintSubdivisionFeedbackScale;
	KawaiiPhysics->BoneForwardAxis = Node.BoneForwardAxis;

	// Limits
	KawaiiPhysics->SphericalLimits = Node.SphericalLimits;
	KawaiiPhysics->CapsuleLimits = Node.CapsuleLimits;
	KawaiiPhysics->TaperedCapsuleLimits = Node.TaperedCapsuleLimits;
	KawaiiPhysics->BoxLimits = Node.BoxLimits;
	KawaiiPhysics->PlanarLimits = Node.PlanarLimits;
	KawaiiPhysics->LimitsDataAsset = Node.LimitsDataAsset;
	KawaiiPhysics->PhysicsAssetForLimits = Node.PhysicsAssetForLimits;
	KawaiiPhysics->MirrorDataTableForLimits = Node.MirrorDataTableForLimits;
	KawaiiPhysics->bSkipMirroredBoneWithExistingCollision = Node.bSkipMirroredBoneWithExistingCollision;

	// Shared Collision
	if (KawaiiPhysics->bSharedCollisionSource != Node.bSharedCollisionSource ||
		KawaiiPhysics->bUseSharedCollision != Node.bUseSharedCollision ||
		KawaiiPhysics->SharedCollisionGroupTag != Node.SharedCollisionGroupTag)
	{
		KawaiiPhysics->RequestSharedCollisionReinit();
	}
	KawaiiPhysics->bSharedCollisionSource = Node.bSharedCollisionSource;
	KawaiiPhysics->bUseSharedCollision = Node.bUseSharedCollision;
	KawaiiPhysics->SharedCollisionGroupTag = Node.SharedCollisionGroupTag;

	// シンプルワールドコリジョン
	const FName SimpleWorldCollisionPreviewProperties[] =
	{
		GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision),
		GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSource),
		GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSharedTag),
	};
	for (const FName PropertyName : SimpleWorldCollisionPreviewProperties)
	{
		if (UKawaiiPhysicsLibrary::DoesNodePropertyRequireSimpleWorldCollisionReinit(PropertyName) &&
			IsAnimNodePropertyDifferent(*KawaiiPhysics, Node, PropertyName))
		{
			KawaiiPhysics->RequestSimpleWorldCollisionReinit();
			break;
		}
	}
	KawaiiPhysics->bUseSimpleWorldCollision = Node.bUseSimpleWorldCollision;
	KawaiiPhysics->SimpleWorldCollisionGatherInterval = Node.SimpleWorldCollisionGatherInterval;
	KawaiiPhysics->SimpleWorldCollisionObjectTypes = Node.SimpleWorldCollisionObjectTypes;
	KawaiiPhysics->SimpleWorldCollisionConvexFallbackShape = Node.SimpleWorldCollisionConvexFallbackShape;
	KawaiiPhysics->bOverrideSimpleWorldCollisionGatherRadius = Node.bOverrideSimpleWorldCollisionGatherRadius;
	KawaiiPhysics->SimpleWorldCollisionGatherRadius = Node.SimpleWorldCollisionGatherRadius;
	KawaiiPhysics->bSimpleWorldCollisionGroundCollision = Node.bSimpleWorldCollisionGroundCollision;
	KawaiiPhysics->SimpleWorldCollisionSkeletalMeshCollision = Node.SimpleWorldCollisionSkeletalMeshCollision;
	KawaiiPhysics->SimpleWorldCollisionSource = Node.SimpleWorldCollisionSource;
	KawaiiPhysics->SimpleWorldCollisionSharedTag = Node.SimpleWorldCollisionSharedTag;

	// ExternalForce
	KawaiiPhysics->Gravity = Node.Gravity;
	KawaiiPhysics->bUseLegacyGravity = Node.bUseLegacyGravity;
	KawaiiPhysics->bUseDefaultGravityZProjectSetting = Node.bUseDefaultGravityZProjectSetting;
	KawaiiPhysics->bUseWorldSpaceGravity = Node.bUseWorldSpaceGravity;
	KawaiiPhysics->SimpleExternalForce = Node.SimpleExternalForce;
	KawaiiPhysics->bUseWorldSpaceSimpleExternalForce = Node.bUseWorldSpaceSimpleExternalForce;
	// ExternalForces は丸ごと代入すると FInstancedStruct が破棄→再構築され、ProceduralWind の
	// RuntimeState（シミュレーション時刻・gust・Pending 要求）が失われる。形状（要素数と型）が
	// 一致している間は既存メモリへの in-place コピーでプロパティのみ同期し、実行中状態を保持する
	if (KawaiiPhysicsEdUtils::IsExternalForceShapeMatched(KawaiiPhysics->ExternalForces, Node.ExternalForces))
	{
		for (int32 Index = 0; Index < Node.ExternalForces.Num(); ++Index)
		{
			if (const UScriptStruct* ScriptStruct =
				KawaiiPhysicsEdUtils::GetExternalForceScriptStruct(Node.ExternalForces[Index]))
			{
				ScriptStruct->CopyScriptStruct(KawaiiPhysics->ExternalForces[Index].GetMutableMemory(),
				                               Node.ExternalForces[Index].GetMemory());
			}
		}
	}
	else
	{
		// 外力の追加/削除/並べ替え/型変更時は従来どおり丸ごと代入（フルリセット）
		KawaiiPhysics->ExternalForces = Node.ExternalForces;
	}
	KawaiiPhysics->CustomExternalForces = Node.CustomExternalForces;

	// Wind
	KawaiiPhysics->bEnableWind = Node.bEnableWind;
	KawaiiPhysics->WindScale = Node.WindScale;
	KawaiiPhysics->WindDirectionNoiseAngle = Node.WindDirectionNoiseAngle;

	// BoneConstraint
	KawaiiPhysics->BoneConstraintGlobalComplianceType = Node.BoneConstraintGlobalComplianceType;
	KawaiiPhysics->BoneConstraintIterationCountBeforeCollision = Node.BoneConstraintIterationCountBeforeCollision;
	KawaiiPhysics->BoneConstraintIterationCountAfterCollision = Node.BoneConstraintIterationCountAfterCollision;
	KawaiiPhysics->bAutoAddChildDummyBoneConstraint = Node.bAutoAddChildDummyBoneConstraint;
	KawaiiPhysics->BoneConstraints = Node.BoneConstraints;
	KawaiiPhysics->BoneConstraintsDataAsset = Node.BoneConstraintsDataAsset;

	// SimulationSpace
	KawaiiPhysics->SimulationSpace = Node.SimulationSpace;
	KawaiiPhysics->SimulationBaseBone = Node.SimulationBaseBone;

	// SyncBone
	KawaiiPhysics->SyncBones = Node.SyncBones;

	// Reset for sync without compile
	KawaiiPhysics->ModifyBones.Empty();
}

void UAnimGraphNode_KawaiiPhysics::CustomizeDetailTools(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& ViewportCategory = DetailBuilder.EditCategory(
		KawaiiPhysicsEditorCategoryNames::KawaiiPhysicsTools,
		LOCTEXT("KawaiiPhysicsToolsCategory", "Kawaii Physics Tools"));
	const FName SelectedFilterGroupId = ReadKawaiiPhysicsCategoryFilter();
	IDetailLayoutBuilder* LayoutBuilder = &DetailBuilder;

	// カテゴリの表示範囲をワンクリックで切り替えるため、独立したカテゴリにフィルタチップを置く。
	IDetailCategoryBuilder& FilterCategory = DetailBuilder.EditCategory(
		KawaiiPhysicsEditorCategoryNames::CategoryFilter,
		LOCTEXT("CategoryFilterRow", "Category Filter"));
	FDetailWidgetRow& FilterWidgetRow = FilterCategory.AddCustomRow(LOCTEXT("CategoryFilterRow", "Category Filter"));
	FilterWidgetRow.WholeRowContent()
	[
		SNew(SSegmentedControl<FName>)
		.Value(SelectedFilterGroupId)
		.OnValueChanged_Lambda([LayoutBuilder](const FName NewFilterGroupId)
		{
			const FName NormalizedFilterGroupId = NormalizeCategoryFilterGroupId(NewFilterGroupId);
			const FString SavedFilterValue = NormalizedFilterGroupId.IsNone()
				                                 ? FString()
				                                 : NormalizedFilterGroupId.ToString();
			if (GConfig)
			{
				GConfig->SetString(CategoryFilterConfigSection, CategoryFilterConfigKey, *SavedFilterValue,
				                   GEditorPerProjectIni);
				GConfig->Flush(false, GEditorPerProjectIni);
			}

			LayoutBuilder->ForceRefreshDetails();
		})
		+ SSegmentedControl<FName>::Slot(NAME_None)
		.Text(LOCTEXT("CategoryFilter_All", "All"))
		+ SSegmentedControl<FName>::Slot(KawaiiPhysicsEditorCategoryNames::Bones)
		.Text(LOCTEXT("CategoryFilter_Bones", "Bones"))
		+ SSegmentedControl<FName>::Slot(KawaiiPhysicsEditorCategoryNames::Physics)
		.Text(LOCTEXT("CategoryFilter_Physics", "Physics"))
		+ SSegmentedControl<FName>::Slot(KawaiiPhysicsEditorCategoryNames::Collision)
		.Text(LOCTEXT("CategoryFilter_Collision", "Collision"))
		+ SSegmentedControl<FName>::Slot(KawaiiPhysicsEditorCategoryNames::Force)
		.Text(LOCTEXT("CategoryFilter_Force", "Force"))
	];

	FDetailWidgetRow& WidgetRow = ViewportCategory.AddCustomRow(LOCTEXT("KawaiiPhysics", "KawaiiPhysicsTools"));

	WidgetRow
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2, 0, 2, 0))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* Node = WeakThis.Get())
				{
					Node->ExportLimitsDataAsset();
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExportCollisionButton", "Export Collision"))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* Node = WeakThis.Get())
				{
					Node->ExportBoneConstraintsDataAsset();
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Export BoneConstraints")))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
		+ SUniformGridPanel::Slot(2, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* Node = WeakThis.Get())
				{
					Node->ExportPresetDataAsset();
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Export Preset")))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
		+ SUniformGridPanel::Slot(0, 1)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* Node = WeakThis.Get())
				{
					Node->ApplyPresetDataAsset();
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Apply Preset")))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
		+ SUniformGridPanel::Slot(1, 1)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* Node = WeakThis.Get())
				{
					Node->CheckPresetDiff();
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Check Preset Diff")))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
	];
}

void UAnimGraphNode_KawaiiPhysics::CustomizeDetailDebugVisualizations(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& ViewportCategory = DetailBuilder.EditCategory(
		KawaiiPhysicsEditorCategoryNames::DebugVisualization,
		LOCTEXT("DebugVisualizationCategory", "Debug Visualization"));
	FDetailWidgetRow& WidgetRow = ViewportCategory.AddCustomRow(
		LOCTEXT("ToggleDebugVisualizationButtonRow", "DebugVisualization"));

	auto CreateDebugButton = [&](const FString& Label, bool& DebugFlag)
	{
		return SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([&]()
			{
				DebugFlag = !DebugFlag;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([&]()
			{
				return DebugFlag
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			];
	};
	
	auto CreateCategorySeparator = [&](const FString& Label, const int32 FontSize = 9)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.01f)
			.VAlign(VAlign_Center)
			[
				SNew(SSeparator)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.f, 0.f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), FontSize))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.9f)
			.VAlign(VAlign_Center)
			[
				SNew(SSeparator)
			];
	};

	WidgetRow
	[
		SNew(SVerticalBox)

		// Common
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f))
		[
			CreateCategorySeparator(TEXT("Common"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SUniformGridPanel)
			+ SUniformGridPanel::Slot(0, 0)
			[
				CreateDebugButton(TEXT("Bone"), bEnableDebugDrawBone)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				CreateDebugButton(TEXT("Length Rate"), bEnableDebugBoneLengthRate)
			]
			+ SUniformGridPanel::Slot(2, 0)
			[
				CreateDebugButton(TEXT("Limit Angle") , bEnableDebugDrawLimitAngle)
			]
		]

		// Limits
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f))
		[
			CreateCategorySeparator(TEXT("Collision"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SUniformGridPanel)
			+ SUniformGridPanel::Slot(0, 0)
			[
				CreateDebugButton(TEXT("Sphere"),  bEnableDebugDrawSphereLimit)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				CreateDebugButton(TEXT("Capsule"),  bEnableDebugDrawCapsuleLimit)
			]
			+ SUniformGridPanel::Slot(2, 0)
			[
				CreateDebugButton(TEXT("Box"), bEnableDebugDrawBoxLimit)
			]
			+ SUniformGridPanel::Slot(0, 1)
			[
				CreateDebugButton(TEXT("Plane"),  bEnableDebugDrawPlanarLimit)
			]
			+ SUniformGridPanel::Slot(1, 1)
			[
				CreateDebugButton(TEXT("Tapered Capsule"),  bEnableDebugDrawTaperedCapsuleLimit)
			]
		]

		// Advanced
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f))
		[
			CreateCategorySeparator(TEXT("Advanced"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f))
		[
			SNew(SUniformGridPanel)
			+ SUniformGridPanel::Slot(0, 0)
			[
				CreateDebugButton(TEXT("Sync Bone"), bEnableDebugDrawSyncBone)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				CreateDebugButton(TEXT("Bone Constraint"),  bEnableDebugDrawBoneConstraint)
			]
			+ SUniformGridPanel::Slot(2, 0)
			[
				CreateDebugButton(TEXT("External Force"), bEnableDebugDrawExternalForce)
			]
		]
	];
}

void UAnimGraphNode_KawaiiPhysics::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);

	const FName SelectedFilterGroupId = ReadKawaiiPhysicsCategoryFilter();

	CustomizeDetailTools(DetailBuilder);
	CustomizeDetailDebugVisualizations(DetailBuilder);

	IDetailLayoutBuilder* LayoutBuilder = &DetailBuilder;
	TSharedRef<IPropertyHandle> NodeHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UAnimGraphNode_KawaiiPhysics, Node),
		UAnimGraphNode_KawaiiPhysics::StaticClass());
	const FSimpleDelegate RefreshDetailsDelegate = FSimpleDelegate::CreateLambda([LayoutBuilder]()
	{
		if (LayoutBuilder)
		{
			LayoutBuilder->ForceRefreshDetails();
		}
	});
	if (TSharedPtr<IPropertyHandle> UseSimpleWorldHandle =
		NodeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision)))
	{
		UseSimpleWorldHandle->SetOnPropertyValueChanged(RefreshDetailsDelegate);
	}
	if (TSharedPtr<IPropertyHandle> SimpleWorldSourceHandle =
		NodeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSource)))
	{
		SimpleWorldSourceHandle->SetOnPropertyValueChanged(RefreshDetailsDelegate);
	}

	if (IsUsingSharedSimpleWorldCollisionPublisher(Node))
	{
		IDetailCategoryBuilder& SimpleWorldCategory = DetailBuilder.EditCategory(
			KawaiiPhysicsEditorCategoryNames::CollisionSimpleWorldCollision);
		const FText SharedPublisherInfoText =
			Node.SimpleWorldCollisionSource == EKawaiiPhysicsSimpleWorldCollisionSource::Auto
				? FText::Format(
					LOCTEXT("SimpleWorldCollisionAutoSharedPublisherInfo",
					        "Gather settings are provided by Shared Publisher ({0}) when a Shared Publisher exists; otherwise this node gathers locally"),
					FText::FromString(Node.SimpleWorldCollisionSharedTag.ToString()))
				: FText::Format(
					LOCTEXT("SimpleWorldCollisionSharedPublisherInfo",
					        "Gather settings are provided by Shared Publisher ({0})"),
					FText::FromString(Node.SimpleWorldCollisionSharedTag.ToString()));

		SimpleWorldCategory.AddCustomRow(LOCTEXT("GoToSharedPublisher", "Go to Publisher"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(SharedPublisherInfoText)
			.Font(DetailBuilder.GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTipText(LOCTEXT("GoToSharedPublisherTooltip",
			                     "Opens the Shared Publisher with the same Shared Tag in this Animation Blueprint."))
			.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* GraphNode = WeakThis.Get())
				{
					UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
						KawaiiPhysicsEdUtils::FindSharedPublisherGraphNodeByTag(
							GraphNode->GetAnimBlueprint(),
							GraphNode->Node.SimpleWorldCollisionSharedTag);
					if (Publisher)
					{
						FocusKawaiiPhysicsGraphNode(Publisher);
					}
					else
					{
						ShowKawaiiPhysicsNotification(
							LOCTEXT("SharedPublisherNotFoundNotification",
							        "Not found in this Animation Blueprint (it may live in another Animation Blueprint)"),
							SNotificationItem::CS_Fail);
					}
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GoToSharedPublisher", "Go to Publisher"))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		];
	}

	// External Forceカテゴリに Wind Scope ボタン（波形プレビュータブを開く）を追加
	IDetailCategoryBuilder& ExternalForceCategory = DetailBuilder.EditCategory(
		KawaiiPhysicsEditorCategoryNames::ForceExternalForce);
	FDetailWidgetRow& WindScopeWidgetRow = ExternalForceCategory.AddCustomRow(LOCTEXT("OpenWindScope", "Wind Scope"));
	WindScopeWidgetRow
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ToolTipText(LOCTEXT("OpenWindScopeToolTip", "Opens the waveform preview tab for Procedural Wind."))
		.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
		{
			if (UAnimGraphNode_KawaiiPhysics* Node = WeakThis.Get())
			{
				Node->OpenWindScopeWindow();
			}
			return FReply::Handled();
		})
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OpenWindScope", "Wind Scope"))
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
		]
	];

	HideCategoriesOutsideFilter(DetailBuilder, SelectedFilterGroupId);

	// 編集したカテゴリは自動で上に移動するため、すべてのカテゴリ順を固定する。
	auto CategorySorter = [](const TMap<FName, IDetailCategoryBuilder*>& Categories)
	{
		int32 Order = 0;
		auto SafeSetOrder = [&Categories, &Order](const FName& CategoryName)
		{
			if (IDetailCategoryBuilder* const* Builder = Categories.Find(CategoryName))
			{
				(*Builder)->SetSortOrder(Order++);
			}
		};

		for (const FName& CategoryName : KawaiiPhysicsEditorCategoryNames::GetCategorySortOrderNames())
		{
			SafeSetOrder(CategoryName);
		}

		// エンジンのFAnimGraphNodeDetails::CustomizeDetailsがUAnimGraphNode::CustomizeDetails後にEditCategory(表示名引数なし)で表示名を既定へ戻すため、後段で必ず実行されるSortCategories内で再適用する。
		struct FCategoryDisplayNameOverride
		{
			FName CategoryName;
			FText DisplayName;
		};

		const FCategoryDisplayNameOverride DisplayNameOverrides[] =
		{
			{ KawaiiPhysicsEditorCategoryNames::CategoryFilter,
			  LOCTEXT("CategoryFilterRow", "Category Filter") },
			{ KawaiiPhysicsEditorCategoryNames::BonesBoneSubdivision,
			  LOCTEXT("Category_Bones_BoneSubdivision", "Bones > Bone Subdivision") },
			{ KawaiiPhysicsEditorCategoryNames::PhysicsSettingsCurves,
			  LOCTEXT("Category_PhysicsSettings_Curves", "Physics Settings > Curves") },
			{ KawaiiPhysicsEditorCategoryNames::CollisionBoneConstraint,
			  LOCTEXT("Category_Collision_BoneConstraint", "Collision > Bone Constraint") },
			{ KawaiiPhysicsEditorCategoryNames::CollisionSharedCollision,
			  LOCTEXT("Category_Collision_SharedCollision", "Collision > Shared Collision") },
			{ KawaiiPhysicsEditorCategoryNames::CollisionWorldCollision,
			  LOCTEXT("Category_Collision_WorldCollision", "Collision > World Collision") },
			{ KawaiiPhysicsEditorCategoryNames::CollisionSimpleWorldCollision,
			  LOCTEXT("Category_Collision_SimpleWorldCollision", "Collision > Simple World Collision") },
			{ KawaiiPhysicsEditorCategoryNames::ForceExternalForce,
			  LOCTEXT("Category_Force_ExternalForce", "Force > External Force") },
			{ KawaiiPhysicsEditorCategoryNames::ForceSyncBone,
			  LOCTEXT("Category_Force_SyncBone", "Force > Sync Bone") },
		};

		for (const FCategoryDisplayNameOverride& Override : DisplayNameOverrides)
		{
			if (IDetailCategoryBuilder* const* Builder = Categories.Find(Override.CategoryName))
			{
				(*Builder)->SetDisplayName(Override.DisplayName);
			}
		}
	};

	DetailBuilder.SortCategories(CategorySorter);
}

struct FKawaiiPhysicsVersion
{
	enum Type
	{
		BeforeCustomVersionWasAdded,
		UseRuntimeFloatCurve,
		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FKawaiiPhysicsVersion()
	{
	};
};

const FGuid FKawaiiPhysicsVersion::GUID(0x4B2D3E25, 0xCD681D29, 0x2DB298D7, 0xAD3E55FA);

const FCustomVersionRegistration GRegisterKawaiiPhysCustomVersion(FKawaiiPhysicsVersion::GUID,
                                                                  FKawaiiPhysicsVersion::LatestVersion,
                                                                  TEXT("Kawaii-Phys"));

void UAnimGraphNode_KawaiiPhysics::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FKawaiiPhysicsVersion::GUID);
}

void UAnimGraphNode_KawaiiPhysics::CreateExportDataAssetPath(FString& PackageName, const FString& DefaultSuffix) const
{
	FString AssetName;
	const FString AnimBlueprintPath = GetAnimBlueprint()->GetPackage()->GetName();
	const FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(AnimBlueprintPath, DefaultSuffix, PackageName, AssetName);
}

UPackage* UAnimGraphNode_KawaiiPhysics::CreateDataAssetPackage(const FText& DialogTitle, const FString& DefaultSuffix,
                                                               FString& AssetName) const
{
	FString PackageName;
	CreateExportDataAssetPath(PackageName, DefaultSuffix);

	const TSharedRef<SDlgPickAssetPath> NewAssetDlg =
		SNew(SDlgPickAssetPath)
		.Title(DialogTitle)
		.DefaultAssetPath(FText::FromString(PackageName));

	if (NewAssetDlg->ShowModal() == EAppReturnType::Cancel)
	{
		return nullptr;
	}

	const FString PackagePath(NewAssetDlg->GetFullAssetPath().ToString());
	AssetName = NewAssetDlg->GetAssetName().ToString();

	return CreatePackage(*PackagePath);
}

void UAnimGraphNode_KawaiiPhysics::ShowExportAssetNotification(UObject* NewAsset,
                                                               FText NotificationText)
{
	ShowKawaiiPhysicsAssetNotification(NewAsset, NotificationText, LOCTEXT("OpenCreatedAsset", "Open Created Asset"),
	                                   SNotificationItem::CS_Success);
}

void UAnimGraphNode_KawaiiPhysics::ExportLimitsDataAsset()
{
	FString AssetName;
	UPackage* Package = CreateDataAssetPackage(
		LOCTEXT("ExportCollisionDialogTitle", "Choose Location for Collision Data Asset"), TEXT("_Collision"), AssetName);
	if (!Package)
	{
		return;
	}

	if (UKawaiiPhysicsLimitsDataAsset* NewDataAsset =
		NewObject<UKawaiiPhysicsLimitsDataAsset>(Package, UKawaiiPhysicsLimitsDataAsset::StaticClass(),
		                                         FName(AssetName), RF_Public | RF_Standalone))
	{
		// look for a valid component in the object being debugged,
		// we might be set to something other than the preview.
		if (UObject* ObjectBeingDebugged = GetAnimBlueprint()->GetObjectBeingDebugged())
		{
			if (const UAnimInstance* InstanceBeingDebugged = Cast<UAnimInstance>(ObjectBeingDebugged))
			{
				NewDataAsset->Skeleton = InstanceBeingDebugged->CurrentSkeleton;
			}
		}

		// copy data
		auto CopyLimits = [&](auto& DataLimits, auto& SourceLimits)
		{
			DataLimits = SourceLimits;
			for (auto& DataLimit : DataLimits)
			{
				DataLimit.SourceType = ECollisionSourceType::DataAsset;
			}
		};
		CopyLimits(NewDataAsset->SphericalLimits, Node.SphericalLimits);
		CopyLimits(NewDataAsset->CapsuleLimits, Node.CapsuleLimits);
		CopyLimits(NewDataAsset->TaperedCapsuleLimits, Node.TaperedCapsuleLimits);
		CopyLimits(NewDataAsset->BoxLimits, Node.BoxLimits);
		CopyLimits(NewDataAsset->PlanarLimits, Node.PlanarLimits);

		// select new asset
		USelection* SelectionSet = GEditor->GetSelectedObjects();
		SelectionSet->DeselectAll();
		SelectionSet->Select(NewDataAsset);

		FAssetRegistryModule::AssetCreated(NewDataAsset);
		Package->MarkPackageDirty();

		// Add Notification
		FText NotificationText = FText::Format(
			LOCTEXT("ExportedCollisionDataAsset", "Exported Collision Data Asset: {0}"), FText::FromString(AssetName));
		ShowExportAssetNotification(NewDataAsset, NotificationText);
	}
}

void UAnimGraphNode_KawaiiPhysics::ExportBoneConstraintsDataAsset()
{
	FString AssetName;
	UPackage* Package = CreateDataAssetPackage(
		LOCTEXT("ExportBoneConstraintsDialogTitle", "Choose Location for BoneConstraints Data Asset"), TEXT("_BoneConstraint"), AssetName);
	if (!Package)
	{
		return;
	}

	if (UKawaiiPhysicsBoneConstraintsDataAsset* NewDataAsset =
		NewObject<UKawaiiPhysicsBoneConstraintsDataAsset>(
			Package, UKawaiiPhysicsBoneConstraintsDataAsset::StaticClass(),
			FName(AssetName), RF_Public | RF_Standalone))
	{
		// look for a valid component in the object being debugged,
		// we might be set to something other than the preview.
		if (UObject* ObjectBeingDebugged = GetAnimBlueprint()->GetObjectBeingDebugged())
		{
			if (const UAnimInstance* InstanceBeingDebugged = Cast<UAnimInstance>(ObjectBeingDebugged))
			{
				NewDataAsset->PreviewSkeleton = InstanceBeingDebugged->CurrentSkeleton;
				NewDataAsset->UpdatePreviewBoneList();
			}
		}

		// copy data
		NewDataAsset->BoneConstraintsData.SetNum(Node.BoneConstraints.Num());
		for (int32 i = 0; i < Node.BoneConstraints.Num(); i++)
		{
			NewDataAsset->BoneConstraintsData[i].Update(Node.BoneConstraints[i]);
		}

		// select new asset
		USelection* SelectionSet = GEditor->GetSelectedObjects();
		SelectionSet->DeselectAll();
		SelectionSet->Select(NewDataAsset);

		FAssetRegistryModule::AssetCreated(NewDataAsset);
		Package->MarkPackageDirty();

		// Add Notification
		FText NotificationText = FText::Format(
			LOCTEXT("ExportedBoneConstraintsDataAsset", "Exported BoneConstraints Data Asset: {0}"),
			FText::FromString(AssetName));
		ShowExportAssetNotification(NewDataAsset, NotificationText);
	}
}

void UAnimGraphNode_KawaiiPhysics::ExportPresetDataAsset()
{
	FString AssetName;
	UPackage* Package = CreateDataAssetPackage(
		LOCTEXT("ExportPresetDialogTitle", "Choose Location for Preset Data Asset"), TEXT("_Preset"), AssetName);
	if (!Package)
	{
		return;
	}

	if (UKawaiiPhysicsPresetDataAsset* NewDataAsset =
		NewObject<UKawaiiPhysicsPresetDataAsset>(Package, UKawaiiPhysicsPresetDataAsset::StaticClass(),
		                                         FName(AssetName), RF_Public | RF_Standalone))
	{
		// デバッグ対象がプレビュー以外の場合もあるため、有効な対象からSkeletonを取得する。
		if (UObject* ObjectBeingDebugged = GetAnimBlueprint()->GetObjectBeingDebugged())
		{
			if (const UAnimInstance* InstanceBeingDebugged = Cast<UAnimInstance>(ObjectBeingDebugged))
			{
#if WITH_EDITORONLY_DATA
				NewDataAsset->Skeleton = InstanceBeingDebugged->CurrentSkeleton;
#endif
			}
		}

		// ノード設定を丸ごとコピーする。
		NewDataAsset->CopyFromNode(Node);

		if (const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint())
		{
			NewDataAsset->Description = FText::Format(
				LOCTEXT("ExportedPresetDataAssetDescription", "Exported from Anim Blueprint: {0}"),
				FText::FromString(AnimBlueprint->GetName()));
		}

		// 新規アセットを選択する。
		USelection* SelectionSet = GEditor->GetSelectedObjects();
		SelectionSet->DeselectAll();
		SelectionSet->Select(NewDataAsset);

		FAssetRegistryModule::AssetCreated(NewDataAsset);
		Package->MarkPackageDirty();

		// 通知を表示する。
		const FText NotificationText = Node.KawaiiPhysicsTag.IsValid()
			                               ? FText::Format(
				                               LOCTEXT("ExportedPresetDataAsset",
				                                       "Exported Preset Data Asset: {0}"),
				                               FText::FromString(AssetName))
			                               : FText::Format(
				                               LOCTEXT("ExportedPresetDataAssetNoTag",
				                                       "Exported Preset Data Asset: {0}\nWarning: this node has no KawaiiPhysicsTag, so TargetTags is empty. Set a tag to include it in Reapply/Audit."),
				                               FText::FromString(AssetName));
		ShowExportAssetNotification(NewDataAsset, NotificationText);
	}
}

void UAnimGraphNode_KawaiiPhysics::ApplyPresetDataAsset()
{
	// 適用するPresetを選択するためのアセットピッカーを開く。
	FOpenAssetDialogConfig OpenAssetDialogConfig;
	OpenAssetDialogConfig.DialogTitleOverride = LOCTEXT("ApplyPresetDialogTitle", "Choose Kawaii Physics Preset");
	OpenAssetDialogConfig.bAllowMultipleSelection = false;
	OpenAssetDialogConfig.AssetClassNames.Add(UKawaiiPhysicsPresetDataAsset::StaticClass()->GetClassPathName());

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const TArray<FAssetData> SelectedAssets =
		ContentBrowserModule.Get().CreateModalOpenAssetDialog(OpenAssetDialogConfig);
	if (SelectedAssets.IsEmpty())
	{
		return;
	}

	UKawaiiPhysicsPresetDataAsset* Preset = Cast<UKawaiiPhysicsPresetDataAsset>(SelectedAssets[0].GetAsset());
	if (!Preset)
	{
		ShowKawaiiPhysicsNotification(
			LOCTEXT("ApplyPresetLoadFailed", "Failed to load selected Kawaii Physics preset."),
			SNotificationItem::CS_Fail);
		return;
	}

	bool bSkeletonMismatch = false;
#if WITH_EDITORONLY_DATA
	// Skeleton不一致は適用を止めず、後続の通知で警告するために保持する。
	if (const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint())
	{
		bSkeletonMismatch = Preset->Skeleton && AnimBlueprint->TargetSkeleton &&
			Preset->Skeleton != AnimBlueprint->TargetSkeleton;
	}
#endif

	FKawaiiPhysicsPresetApplyOptions Options;
	// 既存ノードのボーン割り当て設定は維持する。
	Options.bApplyBoneAssignment = false;
	// Tagは未設定ノードへPresetのTagを引き継げる場合だけ反映する。
	Options.bApplyTag = !Node.KawaiiPhysicsTag.IsValid() && Preset->Node.KawaiiPhysicsTag.IsValid();
	const bool bStampedTag = Options.bApplyTag;

	FKawaiiPhysicsGraphNodeHandle Handle;
	Handle.Node = this;
	// PresetをGraphNodeへ適用し、失敗時は対象Presetを開ける通知で中断する。
	if (!UKawaiiPhysicsEditorLibrary::ApplyPresetToGraphNode(Handle, Preset, Options))
	{
		ShowKawaiiPhysicsAssetNotification(
			Preset,
			FText::Format(LOCTEXT("ApplyPresetFailed", "Failed to apply Preset: {0}"),
			              FText::FromString(Preset->GetName())),
			LOCTEXT("OpenPresetAsset", "Open Preset"),
			SNotificationItem::CS_Fail);
		return;
	}

	// 適用結果の通知文を組み立てる。
	FString NotificationMessage = FString::Printf(TEXT("Applied Preset: %s"), *Preset->GetName());
	if (bStampedTag)
	{
		// Tagを自動付与した場合のみ、その事実を通知へ追記する。
		NotificationMessage += FString::Printf(TEXT("\nStamped tag: %s"), *Node.KawaiiPhysicsTag.ToString());
	}
	if (bSkeletonMismatch)
	{
		// Skeleton不一致は操作を止めない警告として通知へ追記する。
		NotificationMessage += TEXT("\nWarning: preset skeleton differs from this AnimBlueprint target skeleton.");
	}
	if (!Preset->TargetsNodeTag(Node.KawaiiPhysicsTag))
	{
		// 現在のTagがTargetTagsに一致しない場合、以後のReapply/Audit対象外になることを明示する。
		NotificationMessage += TEXT("\nWarning: this node will not be targeted by Reapply/Audit because its tag does not match TargetTags.");
	}

	// 警告を含む成功は見落とし防止のためCS_Failスタイルで表示する。
	const bool bHasWarning = bSkeletonMismatch || !Preset->TargetsNodeTag(Node.KawaiiPhysicsTag);
	ShowKawaiiPhysicsAssetNotification(
		Preset,
		FText::FromString(NotificationMessage),
		LOCTEXT("OpenAppliedPresetAsset", "Open Preset"),
		bHasWarning ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
}

void UAnimGraphNode_KawaiiPhysics::CheckPresetDiff()
{
	const TArray<TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>> Snapshots =
		KawaiiPhysicsPresetDiff::BuildSnapshotsForNode(Node, FKawaiiPhysicsPresetApplyOptions());

	if (Snapshots.IsEmpty())
	{
		ShowKawaiiPhysicsNotification(
			LOCTEXT("NoPresetTargetsNodeTag", "No preset targets this node's tag."),
			SNotificationItem::CS_Fail);
		return;
	}

	// 差分のあるプリセットごとにログを1行出力する。
	for (const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>& Snapshot : Snapshots)
	{
		if (Snapshot->DiffCount == 0)
		{
			continue;
		}

		TArray<FName> DiffProperties;
		DiffProperties.Reserve(Snapshot->DiffCount);
		for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& Row : Snapshot->Rows)
		{
			if (Row.IsValid() && Row->bDiffers)
			{
				DiffProperties.Add(Row->PropertyName);
			}
		}

		UE_LOG(LogKawaiiPhysics, Display,
		       TEXT("CheckPresetDiff: NodeTag=%s Preset=%s DiffCount=%d DiffProperties=%s"),
		       *Node.KawaiiPhysicsTag.ToString(),
		       *Snapshot->PresetPath.ToString(),
		       Snapshot->DiffCount,
		       *JoinPropertyNames(DiffProperties));
	}

	// 差分タブのコンテキストラベル（ノードタイトル＋AnimBlueprint名＋Tag）を組み立てる。
	const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	const FText ContextLabel = FText::Format(
		LOCTEXT("CheckPresetDiffContextLabel", "{0}  |  {1}  |  Tag: {2}"),
		GetNodeTitle(ENodeTitleType::ListView),
		AnimBlueprint
			? FText::FromString(AnimBlueprint->GetName())
			: LOCTEXT("CheckPresetDiffUnknownAnimBlueprint", "(Unknown AnimBlueprint)"),
		FText::FromString(Node.KawaiiPhysicsTag.ToString()));

	FKawaiiPhysicsPresetDiffWindowArgs Args;
	Args.ContextLabel = ContextLabel;
	Args.Snapshots = Snapshots;
	Args.AnimBlueprintPath = AnimBlueprint ? FSoftObjectPath(AnimBlueprint) : FSoftObjectPath();
	Args.NodeGuid = NodeGuid;

	SKawaiiPhysicsPresetDiffWindow::OpenWindow(MoveTemp(Args));
}

void UAnimGraphNode_KawaiiPhysics::OpenWindScopeWindow(int32 ExternalForceIndex)
{
	if (ExternalForceIndex == INDEX_NONE)
	{
		// ExternalForcesから最初のProceduralWindを探す
		for (int32 Index = 0; Index < Node.ExternalForces.Num(); ++Index)
		{
			if (IsProceduralWindExternalForce(Node.ExternalForces[Index]))
			{
				ExternalForceIndex = Index;
				break;
			}
		}
	}

	if (ExternalForceIndex == INDEX_NONE)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("NoProceduralWindExternalForce", "No Procedural Wind external force on this node."),
			SNotificationItem::CS_Fail);
		return;
	}

	// タブへ渡す引数を組み立てて開く
	const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	FKawaiiPhysicsWindScopeWindowArgs Args;
	Args.GraphNode = this;
	Args.AnimBlueprintPath = AnimBlueprint ? FSoftObjectPath(AnimBlueprint) : FSoftObjectPath();
	Args.NodeGuid = NodeGuid;
	Args.ExternalForceIndex = ExternalForceIndex;

	SKawaiiPhysicsWindScopeWindow::OpenWindow(MoveTemp(Args));
}

void UAnimGraphNode_KawaiiPhysics::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Context || Context->bIsDebugging)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(
		"KawaiiPhysics", LOCTEXT("KawaiiPhysicsContextMenuSection", "Kawaii Physics"));

	// private関数だがCreateUObjectはメンバ関数内での束縛のためアクセス可能（弱参照バインドでノード破棄後も安全）。
	UAnimGraphNode_KawaiiPhysics* MutableThis = const_cast<UAnimGraphNode_KawaiiPhysics*>(this);

	Section.AddMenuEntry(
		"KawaiiPhysicsCheckPresetDiff",
		LOCTEXT("CheckPresetDiffMenuLabel", "Check Preset Diff"),
		LOCTEXT("CheckPresetDiffMenuToolTip",
		        "Shows the diff between this node and its presets in a tab."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateUObject(MutableThis, &UAnimGraphNode_KawaiiPhysics::CheckPresetDiff)));

	Section.AddMenuEntry(
		"KawaiiPhysicsApplyPreset",
		LOCTEXT("ApplyPresetMenuLabel", "Apply Preset..."),
		LOCTEXT("ApplyPresetMenuToolTip",
		        "Applies a preset data asset to this node."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateUObject(MutableThis, &UAnimGraphNode_KawaiiPhysics::ApplyPresetDataAsset)));

	if (IsUsingSharedSimpleWorldCollisionPublisher(Node))
	{
		Section.AddMenuEntry(
			"KawaiiPhysicsGoToSharedPublisher",
			LOCTEXT("GoToSharedPublisherMenuLabel", "Go to Shared Publisher"),
			LOCTEXT("GoToSharedPublisherMenuToolTip",
			        "Opens the Shared Publisher with the same Shared Tag in this Animation Blueprint."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(MutableThis)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* GraphNode = WeakThis.Get())
				{
					UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
						KawaiiPhysicsEdUtils::FindSharedPublisherGraphNodeByTag(
							GraphNode->GetAnimBlueprint(),
							GraphNode->Node.SimpleWorldCollisionSharedTag);
					if (Publisher)
					{
						FocusKawaiiPhysicsGraphNode(Publisher);
					}
					else
					{
						ShowKawaiiPhysicsNotification(
							LOCTEXT("SharedPublisherNotFoundContextNotification",
							        "Not found in this Animation Blueprint (it may live in another Animation Blueprint)"),
							SNotificationItem::CS_Fail);
					}
				}
			})));
	}

	TArray<int32> ProceduralWindExternalForceIndices;
	for (int32 Index = 0; Index < Node.ExternalForces.Num(); ++Index)
	{
		if (IsProceduralWindExternalForce(Node.ExternalForces[Index]))
		{
			ProceduralWindExternalForceIndices.Add(Index);
		}
	}

	if (ProceduralWindExternalForceIndices.Num() >= 2)
	{
		for (const int32 ExternalForceIndex : ProceduralWindExternalForceIndices)
		{
			const int32 MenuExternalForceIndex = ExternalForceIndex;
			Section.AddMenuEntry(
				FName(*FString::Printf(TEXT("KawaiiPhysicsWindScope_%d"), MenuExternalForceIndex)),
				FText::Format(
					LOCTEXT("WindScopeContextMenuWithForceIndex", "Wind Scope (Force [{0}])"),
					FText::AsNumber(MenuExternalForceIndex)),
				LOCTEXT("WindScopeMenuToolTip",
				        "Opens the waveform preview tab for Procedural Wind."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateUObject(
					MutableThis,
					&UAnimGraphNode_KawaiiPhysics::OpenWindScopeWindow,
					MenuExternalForceIndex)));
		}
	}
	else
	{
		// コンテキストメニューにも Wind Scope を追加
		Section.AddMenuEntry(
			"KawaiiPhysicsWindScope",
			LOCTEXT("WindScopeContextMenu", "Wind Scope"),
			LOCTEXT("WindScopeMenuToolTip",
			        "Opens the waveform preview tab for Procedural Wind."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateUObject(
				MutableThis,
				&UAnimGraphNode_KawaiiPhysics::OpenWindScopeWindow,
				static_cast<int32>(INDEX_NONE))));
	}
}

#undef LOCTEXT_NAMESPACE
