// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimGraphNode_KawaiiPhysicsSharedPublisher.h"

#include "Animation/AnimBlueprint.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IPropertyUtilities.h"
#include "KawaiiPhysicsEdUtils.h"
#include "KawaiiPhysicsEditorCategoryNames.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PropertyHandle.h"
#include "Styling/CoreStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimGraphNode_KawaiiPhysicsSharedPublisher)

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

namespace
{
	void ShowSharedPublisherNotification(const FText& NotificationText)
	{
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> NotificationItem =
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	void HideChildProperty(const TSharedPtr<IPropertyHandle>& ParentHandle, const FName ChildName)
	{
		if (!ParentHandle.IsValid())
		{
			return;
		}

		TSharedPtr<IPropertyHandle> ChildHandle = ParentHandle->GetChildHandle(ChildName);
		if (ChildHandle.IsValid())
		{
			ChildHandle->MarkHiddenByCustomization();
		}
	}

	void HideIgnoredSharedWindProperties(IDetailLayoutBuilder& DetailBuilder)
	{
		TSharedRef<IPropertyHandle> NodeHandle = DetailBuilder.GetProperty(
			GET_MEMBER_NAME_CHECKED(UAnimGraphNode_KawaiiPhysicsSharedPublisher, Node),
			UAnimGraphNode_KawaiiPhysicsSharedPublisher::StaticClass());
		TSharedPtr<IPropertyHandle> SharedWindHandle = NodeHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, SharedWind));
		if (!SharedWindHandle.IsValid())
		{
			return;
		}

		const FName HiddenPropertyNames[] =
		{
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bDrawDebug),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, ApplyBoneFilter),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, IgnoreBoneFilter),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, ExternalForceSpace),
			FName(TEXT("bCanSelectForceSpace")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, ForceRateByBoneLengthRate),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayPhaseOffset),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePhaseOffset),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePhaseOffset),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, Seed),
		};

		for (const FName& PropertyName : HiddenPropertyNames)
		{
			HideChildProperty(SharedWindHandle, PropertyName);
		}
	}

	FText MakePreviewStatusText(const TWeakObjectPtr<UAnimGraphNode_KawaiiPhysicsSharedPublisher> WeakGraphNode)
	{
		const UAnimGraphNode_KawaiiPhysicsSharedPublisher* GraphNode = WeakGraphNode.Get();
		const FAnimNode_KawaiiPhysicsSharedPublisher* LiveNode =
			KawaiiPhysicsEdUtils::ResolveLiveSharedPublisherNode(GraphNode);
		if (!LiveNode)
		{
			return LOCTEXT("SharedPublisherPreviewNotRunning", "Not running in preview");
		}

#if WITH_EDITORONLY_DATA
		if (!LiveNode->IsRecentlyUpdated())
		{
			return LOCTEXT("SharedPublisherPreviewNotUpdated", "Not updated (branch weight 0?)");
		}
#endif

		const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry =
			LiveNode->GetSharedPublisherEntry();
		const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
			LiveNode->GetSimpleWorldEntry();
		const uint64 PublishSerial = PublisherEntry.IsValid() ? PublisherEntry->GetPublishSerial() : 0;
		const int32 NumReaders = SimpleWorldEntry.IsValid() ? SimpleWorldEntry->GetNumReaders() : 0;

		return FText::Format(
			LOCTEXT("SharedPublisherPreviewPublishing", "Publishing: {0} / serial {1} / readers {2}"),
			FText::FromString(LiveNode->GetResolvedTag().ToString()),
			FText::AsNumber(static_cast<int64>(PublishSerial)),
			FText::AsNumber(NumReaders));
	}
}

FText UAnimGraphNode_KawaiiPhysicsSharedPublisher::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!CachedNodeTitles.IsTitleCached(TitleType, this))
	{
		const FText NodeTitle = LOCTEXT("SharedPublisherNodeTitle", "Kawaii Physics Shared Publisher");
		if (TitleType == ENodeTitleType::FullTitle)
		{
			const FText TagText = Node.SharedGroupTag.IsValid()
				                      ? FText::FromString(Node.SharedGroupTag.ToString())
				                      : LOCTEXT("SharedPublisherNoTagTitle", "(No Tag)");
			CachedNodeTitles.SetCachedTitle(
				TitleType,
				FText::Format(LOCTEXT("SharedPublisherFullTitle", "{0}\n{1}"), NodeTitle, TagText),
				this);
		}
		else
		{
			CachedNodeTitles.SetCachedTitle(TitleType, NodeTitle, this);
		}
	}

	return CachedNodeTitles[TitleType];
}

FText UAnimGraphNode_KawaiiPhysicsSharedPublisher::GetTooltipText() const
{
	return LOCTEXT("SharedPublisherNodeTooltip",
	               "Publishes Simple World Collision gather settings and wind state to every Kawaii Physics node that shares the same Shared Group Tag.\nPlace one per character (actor family) in the body mesh's Post Process AnimBP or on the trunk right before Output Pose.\nConsumers: set Simple World Collision > Source to Shared or Auto with the same tag.");
}

FString UAnimGraphNode_KawaiiPhysicsSharedPublisher::GetNodeCategory() const
{
	return TEXT("Animation|Skeletal Controls");
}

FText UAnimGraphNode_KawaiiPhysicsSharedPublisher::GetMenuCategory() const
{
	return FText::FromString(GetNodeCategory());
}

void UAnimGraphNode_KawaiiPhysicsSharedPublisher::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* ChangedProperty = PropertyChangedEvent.Property;
	if (ChangedProperty &&
		ChangedProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, SharedGroupTag))
	{
		CachedNodeTitles.MarkDirty();
	}
}

void UAnimGraphNode_KawaiiPhysicsSharedPublisher::ValidateAnimNodeDuringCompilation(
	USkeleton* ForSkeleton,
	FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	const FGameplayTag PublisherTag = Node.SharedGroupTag;
	if (!PublisherTag.IsValid())
	{
		MessageLog.Warning(*LOCTEXT("SharedPublisherNoTag",
		                            "@@ has no Shared Group Tag. Consumers cannot find it.").ToString(), this);
	}

	if (!Node.SimpleWorldCollision.bEnabled && !Node.SharedWind.bIsEnabled)
	{
		MessageLog.Note(*LOCTEXT("SharedPublisherAllDisabled",
		                         "@@ has both Simple World Collision and Shared Wind disabled.").ToString(), this);
	}

	if (!PublisherTag.IsValid())
	{
		return;
	}

	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	TArray<UAnimGraphNode_KawaiiPhysicsSharedPublisher*> Publishers;
	KawaiiPhysicsEdUtils::CollectAnimGraphNodes(AnimBlueprint, Publishers);
	int32 MatchingPublisherCount = 0;
	for (const UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher : Publishers)
	{
		if (Publisher && Publisher->Node.SharedGroupTag == PublisherTag)
		{
			++MatchingPublisherCount;
		}
	}

	if (MatchingPublisherCount >= 2)
	{
		MessageLog.Warning(*LOCTEXT("SharedPublisherDuplicateTag",
		                            "@@ shares its tag with another Shared Publisher in this Animation Blueprint. Only one publisher per tag per actor family is used.").ToString(), this);
	}

	TArray<UAnimGraphNode_KawaiiPhysics*> Consumers;
	KawaiiPhysicsEdUtils::FindKawaiiPhysicsConsumerGraphNodes(AnimBlueprint, PublisherTag, Consumers);
	if (Consumers.IsEmpty())
	{
		MessageLog.Note(*LOCTEXT("SharedPublisherNoConsumers",
		                         "@@ has no consumer in this Animation Blueprint. Consumers in other Animation Blueprints (Post Process, Linked Layers, child actors) can still read it.").ToString(), this);
	}
}

void UAnimGraphNode_KawaiiPhysicsSharedPublisher::CopyNodeDataToPreviewNode(FAnimNode_Base* InPreviewNode)
{
	FAnimNode_KawaiiPhysicsSharedPublisher* Preview =
		static_cast<FAnimNode_KawaiiPhysicsSharedPublisher*>(InPreviewNode);
	if (!Preview)
	{
		return;
	}

	const bool bNeedsReinit =
		Preview->SharedGroupTag != Node.SharedGroupTag ||
		Preview->SimpleWorldCollision.GatherScope != Node.SimpleWorldCollision.GatherScope ||
		Preview->SimpleWorldCollision.bGatherFamilyMembers != Node.SimpleWorldCollision.bGatherFamilyMembers ||
		Preview->SimpleWorldCollision.bEnabled != Node.SimpleWorldCollision.bEnabled;

	Preview->bEnabled = Node.bEnabled;
	Preview->SharedGroupTag = Node.SharedGroupTag;
	Preview->SimpleWorldCollision = Node.SimpleWorldCollision;
	Preview->SharedWind = Node.SharedWind;

	if (bNeedsReinit)
	{
		Preview->RequestSharedPublisherReinit();
	}
}

void UAnimGraphNode_KawaiiPhysicsSharedPublisher::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);

	IDetailCategoryBuilder& PublisherCategory = DetailBuilder.EditCategory(
		KawaiiPhysicsEditorCategoryNames::SharedPublisher,
		LOCTEXT("Category_SharedPublisher", "Shared Publisher"),
		ECategoryPriority::Important);
	DetailBuilder.EditCategory(
		KawaiiPhysicsEditorCategoryNames::SharedPublisherSimpleWorldCollision,
		LOCTEXT("Category_SharedPublisher_SimpleWorldCollision", "Shared Publisher > Simple World Collision"),
		ECategoryPriority::Important);
	DetailBuilder.EditCategory(
		KawaiiPhysicsEditorCategoryNames::SharedPublisherWind,
		LOCTEXT("Category_SharedPublisher_Wind", "Shared Publisher > Wind"),
		ECategoryPriority::Important);

	PublisherCategory.AddCustomRow(LOCTEXT("SharedPublisherPreviewStatus", "Preview Status"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SharedPublisherPreviewStatus", "Preview Status"))
		.Font(DetailBuilder.GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(260.0f)
	[
		SNew(STextBlock)
		.Text_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysicsSharedPublisher>(this)]()
		{
			return MakePreviewStatusText(WeakThis);
		})
		.Font(DetailBuilder.GetDetailFont())
	];

	PublisherCategory.AddCustomRow(LOCTEXT("SharedPublisherFindConsumers", "Find Consumers"))
	.WholeRowContent()
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ToolTipText(LOCTEXT("SharedPublisherFindConsumersTooltip",
		                     "Selects Kawaii Physics consumer nodes with the same Shared Group Tag in this Animation Blueprint."))
		.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysicsSharedPublisher>(this)]()
		{
			if (UAnimGraphNode_KawaiiPhysicsSharedPublisher* GraphNode = WeakThis.Get())
			{
				GraphNode->FindConsumers();
			}
			return FReply::Handled();
		})
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SharedPublisherFindConsumers", "Find Consumers"))
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
		]
	];

	HideIgnoredSharedWindProperties(DetailBuilder);

	DetailBuilder.SortCategories([](const TMap<FName, IDetailCategoryBuilder*>& Categories)
	{
		int32 Order = 0;
		for (const FName& CategoryName : KawaiiPhysicsEditorCategoryNames::GetSharedPublisherCategorySortOrderNames())
		{
			if (IDetailCategoryBuilder* const* Builder = Categories.Find(CategoryName))
			{
				(*Builder)->SetSortOrder(Order++);
			}
		}
	});
}

void UAnimGraphNode_KawaiiPhysicsSharedPublisher::FindConsumers()
{
	TArray<UAnimGraphNode_KawaiiPhysics*> Consumers;
	KawaiiPhysicsEdUtils::FindKawaiiPhysicsConsumerGraphNodes(GetAnimBlueprint(), Node.SharedGroupTag, Consumers);
	if (Consumers.IsEmpty())
	{
		ShowSharedPublisherNotification(LOCTEXT("SharedPublisherNoConsumerNotification",
		                                       "No consumer in this Animation Blueprint"));
		return;
	}

	UAnimGraphNode_KawaiiPhysics* FirstConsumer = Consumers[0];
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(FirstConsumer);

	if (UEdGraph* Graph = FirstConsumer ? FirstConsumer->GetGraph() : nullptr)
	{
		TSet<const UEdGraphNode*> NodesToSelect;
		for (const UAnimGraphNode_KawaiiPhysics* Consumer : Consumers)
		{
			if (Consumer && Consumer->GetGraph() == Graph)
			{
				NodesToSelect.Add(Consumer);
			}
		}
		Graph->SelectNodeSet(NodesToSelect, true);
	}
}

void UAnimGraphNode_KawaiiPhysicsSharedPublisher::GetNodeContextMenuActions(
	UToolMenu* Menu,
	UGraphNodeContextMenuContext* Context) const
{
	if (!Context || Context->bIsDebugging)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(
		"KawaiiPhysicsSharedPublisher",
		LOCTEXT("SharedPublisherContextMenuSection", "Kawaii Physics Shared Publisher"));
	UAnimGraphNode_KawaiiPhysicsSharedPublisher* MutableThis =
		const_cast<UAnimGraphNode_KawaiiPhysicsSharedPublisher*>(this);

	Section.AddMenuEntry(
		"KawaiiPhysicsSharedPublisherFindConsumers",
		LOCTEXT("SharedPublisherFindConsumersMenuLabel", "Find Consumers"),
		LOCTEXT("SharedPublisherFindConsumersMenuToolTip",
		        "Selects Kawaii Physics consumer nodes with the same Shared Group Tag in this Animation Blueprint."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateUObject(
			MutableThis,
			&UAnimGraphNode_KawaiiPhysicsSharedPublisher::FindConsumers)));
}

#undef LOCTEXT_NAMESPACE
