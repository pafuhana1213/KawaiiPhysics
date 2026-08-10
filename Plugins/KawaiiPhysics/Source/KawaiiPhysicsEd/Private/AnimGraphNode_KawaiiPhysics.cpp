// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimGraphNode_KawaiiPhysics.h"

#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IContentBrowserSingleton.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsPresetDiffSnapshot.h"
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
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSeparator.h"

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimGraphNode_KawaiiPhysics)

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

namespace
{
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
				                    LOCTEXT("AnimGraphNode_KawaiiPhysics_ListTitle",
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
				                    LOCTEXT("AnimGraphNode_KawaiiPhysics_Title",
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

	// ExternalForce
	KawaiiPhysics->Gravity = Node.Gravity;
	KawaiiPhysics->bUseLegacyGravity = Node.bUseLegacyGravity;
	KawaiiPhysics->bUseDefaultGravityZProjectSetting = Node.bUseDefaultGravityZProjectSetting;
	KawaiiPhysics->bUseWorldSpaceGravity = Node.bUseWorldSpaceGravity;
	KawaiiPhysics->SimpleExternalForce = Node.SimpleExternalForce;
	KawaiiPhysics->bUseWorldSpaceSimpleExternalForce = Node.bUseWorldSpaceSimpleExternalForce;
	KawaiiPhysics->ExternalForces = Node.ExternalForces;
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
	IDetailCategoryBuilder& ViewportCategory = DetailBuilder.EditCategory(TEXT("Kawaii Physics Tools"));
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
				.Text(FText::FromString(TEXT("Export Limits")))
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
		+ SUniformGridPanel::Slot(2, 1)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTipText(LOCTEXT("OpenWindScopeToolTip", "Procedural Wind の波形プレビューウィンドウを開く / Opens the waveform preview window for Procedural Wind."))
			.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics>(this)]()
			{
				if (UAnimGraphNode_KawaiiPhysics* Node = WeakThis.Get())
				{
					int32 ExternalForceIndex = INDEX_NONE;
					for (int32 Index = 0; Index < Node->Node.ExternalForces.Num(); ++Index)
					{
						if (Node->Node.ExternalForces[Index].IsValid() &&
							Node->Node.ExternalForces[Index].GetScriptStruct() ==
							FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct())
						{
							ExternalForceIndex = Index;
							break;
						}
					}

					if (ExternalForceIndex == INDEX_NONE)
					{
						KawaiiPhysicsEdWindowUtils::ShowNotification(
							LOCTEXT("NoProceduralWindExternalForce", "Procedural Wind の外力がありません / No Procedural Wind external force on this node."),
							SNotificationItem::CS_Fail);
						return FReply::Handled();
					}

					const UAnimBlueprint* AnimBlueprint = Node->GetAnimBlueprint();
					FKawaiiPhysicsWindScopeWindowArgs Args;
					Args.GraphNode = Node;
					Args.AnimBlueprintPath = AnimBlueprint ? FSoftObjectPath(AnimBlueprint) : FSoftObjectPath();
					Args.NodeGuid = Node->NodeGuid;
					Args.ExternalForceIndex = ExternalForceIndex;

					SKawaiiPhysicsWindScopeWindow::OpenWindow(MoveTemp(Args));
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OpenWindScope", "Wind Scope"))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
	];
}

void UAnimGraphNode_KawaiiPhysics::CustomizeDetailDebugVisualizations(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& ViewportCategory = DetailBuilder.EditCategory(TEXT("Debug Visualization"));
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
				CreateDebugButton(TEXT("Plane"),  bEnableDebugDrawPlanerLimit)
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

	CustomizeDetailTools(DetailBuilder);
	CustomizeDetailDebugVisualizations(DetailBuilder);

	// Force order of details panel categories - Must set order for all of them as any that are edited automatically move to the top.
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

		// Tools, Debug
		SafeSetOrder(FName("Kawaii Physics Tools"));
		SafeSetOrder(FName("Debug Visualization"));
		SafeSetOrder(FName("Functions"));
		SafeSetOrder(FName("Preset"));

		// Basic
		SafeSetOrder(FName("Bones"));
		SafeSetOrder(FName("Bones|Bone Subdivision"));
		SafeSetOrder(FName("Physics Settings"));
		SafeSetOrder(FName("Physics Settings|Curves"));

		// Limits
		SafeSetOrder(FName("Limits"));
		SafeSetOrder(FName("Limits|Bone Constraint"));
		SafeSetOrder(FName("Limits|Shared Collision"));
		SafeSetOrder(FName("Limits|World Collision"));

		// Force
		SafeSetOrder(FName("Force"));
		SafeSetOrder(FName("Force|External Force"));
		SafeSetOrder(FName("Force|Sync Bone"));

		// AnimNode
		SafeSetOrder(FName("Tag"));
		SafeSetOrder(FName("Alpha"));
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

UPackage* UAnimGraphNode_KawaiiPhysics::CreateDataAssetPackage(const FString& DialogTitle, const FString& DefaultSuffix,
                                                               FString& AssetName) const
{
	FString PackageName;
	CreateExportDataAssetPath(PackageName, DefaultSuffix);

	const TSharedRef<SDlgPickAssetPath> NewAssetDlg =
		SNew(SDlgPickAssetPath)
		.Title(FText::FromString(DialogTitle))
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
		TEXT("Choose Location for Collision Data Asset"), TEXT("_Collision"), AssetName);
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
			LOCTEXT("ExportedLimitsDataAsset", "Exported Limits Data Asset: {0}"), FText::FromString(AssetName));
		ShowExportAssetNotification(NewDataAsset, NotificationText);
	}
}

void UAnimGraphNode_KawaiiPhysics::ExportBoneConstraintsDataAsset()
{
	FString AssetName;
	UPackage* Package = CreateDataAssetPackage(
		TEXT("Choose Location for BoneConstraints Data Asset"), TEXT("_BoneConstraint"), AssetName);
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
		TEXT("Choose Location for Preset Data Asset"), TEXT("_Preset"), AssetName);
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

	// 差分ウィンドウのコンテキストラベル（ノードタイトル＋AnimBlueprint名＋Tag）を組み立てる。
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
		        "このノードとプリセットの差分をウィンドウで確認します / Shows the diff between this node and its presets in a window."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateUObject(MutableThis, &UAnimGraphNode_KawaiiPhysics::CheckPresetDiff)));

	Section.AddMenuEntry(
		"KawaiiPhysicsApplyPreset",
		LOCTEXT("ApplyPresetMenuLabel", "Apply Preset..."),
		LOCTEXT("ApplyPresetMenuToolTip",
		        "プリセットDataAssetをこのノードへ適用します / Applies a preset data asset to this node."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateUObject(MutableThis, &UAnimGraphNode_KawaiiPhysics::ApplyPresetDataAsset)));
}

#undef LOCTEXT_NAMESPACE
