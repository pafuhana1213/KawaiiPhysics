// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimGraphNode_KawaiiPhysics.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraph.h"
#include "Engine/SkeletalMesh.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsBoneChainUtils.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Selection.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSeparator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimGraphNode_KawaiiPhysics)

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

namespace
{
	void ShowKawaiiPhysicsNodeNotification(const FText& NotificationText,
	                                       SNotificationItem::ECompletionState CompletionState)
	{
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(
			NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(CompletionState);
		}
	}

	void BuildExcludedBoneIndexSet(const FReferenceSkeleton& RefSkeleton, const TArray<FBoneReference>& ExcludeBones,
	                               TSet<int32>& OutExcludedBoneIndices)
	{
		OutExcludedBoneIndices.Reset();
		for (const FBoneReference& ExcludeBone : ExcludeBones)
		{
			if (ExcludeBone.BoneName == NAME_None)
			{
				continue;
			}

			const int32 ExcludeBoneIndex = RefSkeleton.FindBoneIndex(ExcludeBone.BoneName);
			if (RefSkeleton.IsValidIndex(ExcludeBoneIndex))
			{
				OutExcludedBoneIndices.Add(ExcludeBoneIndex);
			}
		}
	}

	void AddUniqueCandidateRoot(const FReferenceSkeleton& RefSkeleton, int32 CandidateRootIndex,
	                            const TSet<int32>& ExcludedBoneIndices,
	                            TArray<int32>& OutCandidateRootIndices, TSet<int32>& InOutUniqueCandidateRootIndices)
	{
		if (!RefSkeleton.IsValidIndex(CandidateRootIndex)
			|| ExcludedBoneIndices.Contains(CandidateRootIndex)
			|| InOutUniqueCandidateRootIndices.Contains(CandidateRootIndex))
		{
			return;
		}

		OutCandidateRootIndices.Add(CandidateRootIndex);
		InOutUniqueCandidateRootIndices.Add(CandidateRootIndex);
	}

	void AppendChainRootCandidatesFromSearchRoot(const FReferenceSkeleton& RefSkeleton, int32 SearchRootIndex,
	                                             const TSet<int32>& ExcludedBoneIndices,
	                                             TArray<int32>& OutCandidateRootIndices,
	                                             TSet<int32>& InOutUniqueCandidateRootIndices)
	{
		if (!RefSkeleton.IsValidIndex(SearchRootIndex) || ExcludedBoneIndices.Contains(SearchRootIndex))
		{
			return;
		}

		TArray<int32> DetectedCandidateRootIndices;
		KawaiiPhysicsBoneChain::DetectChainRootCandidates(RefSkeleton, SearchRootIndex, DetectedCandidateRootIndices,
		                                                  &ExcludedBoneIndices);

		if (DetectedCandidateRootIndices.Num() == 0)
		{
			AddUniqueCandidateRoot(RefSkeleton, SearchRootIndex, ExcludedBoneIndices, OutCandidateRootIndices,
			                       InOutUniqueCandidateRootIndices);
			return;
		}

		for (int32 CandidateRootIndex : DetectedCandidateRootIndices)
		{
			AddUniqueCandidateRoot(RefSkeleton, CandidateRootIndex, ExcludedBoneIndices, OutCandidateRootIndices,
			                       InOutUniqueCandidateRootIndices);
		}
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
			.OnClicked_Lambda([this]()
			{
				this->ExportLimitsDataAsset();
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
			.OnClicked_Lambda([this]()
			{
				this->ExportBoneConstraintsDataAsset();
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
			.OnClicked_Lambda([this]()
			{
				this->GenerateBoneConstraintsDataAssetFromChains();
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Generate BoneConstraints (Chains)")))
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
	FNotificationInfo NotificationInfo(NotificationText);
	NotificationInfo.ExpireDuration = 5.0f;
	NotificationInfo.Hyperlink = FSimpleDelegate::CreateLambda([NewAsset]()
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	});
	NotificationInfo.HyperlinkText = LOCTEXT("OpenCreatedAsset", "Open Created Asset");

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(
		NotificationInfo);
	NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
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
			LOCTEXT("ExportedLimitsDataAsset", "Exposted Limits Data Asset: {0}"), FText::FromString(AssetName));
		ShowExportAssetNotification(NewDataAsset, NotificationText);
	}
}

USkeleton* UAnimGraphNode_KawaiiPhysics::GetBoneConstraintsPreviewSkeleton() const
{
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	if (AnimBlueprint == nullptr)
	{
		return nullptr;
	}

	if (UObject* ObjectBeingDebugged = AnimBlueprint->GetObjectBeingDebugged())
	{
		if (const UAnimInstance* InstanceBeingDebugged = Cast<UAnimInstance>(ObjectBeingDebugged))
		{
			if (const USkeletalMeshComponent* SkelMeshComponent = InstanceBeingDebugged->GetSkelMeshComponent())
			{
				if (USkeletalMesh* SkeletalMesh = SkelMeshComponent->GetSkeletalMeshAsset())
				{
					if (USkeleton* MeshSkeleton = SkeletalMesh->GetSkeleton())
					{
						return MeshSkeleton;
					}
				}
			}

			if (InstanceBeingDebugged->CurrentSkeleton)
			{
				return InstanceBeingDebugged->CurrentSkeleton;
			}
		}
	}

	return AnimBlueprint->TargetSkeleton;
}

UKawaiiPhysicsBoneConstraintsDataAsset* UAnimGraphNode_KawaiiPhysics::CreateBoneConstraintsDataAssetForChainGeneration(
	USkeleton* PreviewSkeleton)
{
	FString AssetName;
	UPackage* Package = CreateDataAssetPackage(
		TEXT("Choose Location for BoneConstraints Data Asset"), TEXT("_BoneConstraint"), AssetName);
	if (!Package)
	{
		return nullptr;
	}

	UKawaiiPhysicsBoneConstraintsDataAsset* NewDataAsset =
		NewObject<UKawaiiPhysicsBoneConstraintsDataAsset>(
			Package, UKawaiiPhysicsBoneConstraintsDataAsset::StaticClass(),
			FName(AssetName), RF_Public | RF_Standalone);
	if (NewDataAsset == nullptr)
	{
		return nullptr;
	}

	NewDataAsset->PreviewSkeleton = PreviewSkeleton;
	NewDataAsset->UpdatePreviewBoneList();

	NewDataAsset->BoneConstraintsData.SetNum(Node.BoneConstraints.Num());
	for (int32 Index = 0; Index < Node.BoneConstraints.Num(); ++Index)
	{
		NewDataAsset->BoneConstraintsData[Index].Update(Node.BoneConstraints[Index]);
	}

	USelection* SelectionSet = GEditor->GetSelectedObjects();
	SelectionSet->DeselectAll();
	SelectionSet->Select(NewDataAsset);

	FAssetRegistryModule::AssetCreated(NewDataAsset);
	Package->MarkPackageDirty();

	return NewDataAsset;
}

void UAnimGraphNode_KawaiiPhysics::GenerateBoneConstraintsDataAssetFromChains()
{
	USkeleton* PreviewSkeleton = GetBoneConstraintsPreviewSkeleton();
	if (PreviewSkeleton == nullptr)
	{
		ShowKawaiiPhysicsNodeNotification(
			LOCTEXT("GenerateBoneConstraintsChainsNoPreviewSkeleton",
			        "Generate BoneConstraints (Chains) failed: no preview mesh skeleton or AnimBP TargetSkeleton was found."),
			SNotificationItem::CS_Fail);
		return;
	}

	const FReferenceSkeleton& RefSkeleton = PreviewSkeleton->GetReferenceSkeleton();
	TArray<int32> CandidateRootIndices;
	TSet<int32> UniqueCandidateRootIndices;
	int32 SortSearchRootIndex = INDEX_NONE;
	FName DetectRootBoneName = NAME_None;

	const auto AppendCandidates = [&](const FBoneReference& SearchRootBone,
	                                  const TArray<FBoneReference>& ExcludeBones)
	{
		if (SearchRootBone.BoneName == NAME_None)
		{
			return;
		}

		const int32 SearchRootIndex = RefSkeleton.FindBoneIndex(SearchRootBone.BoneName);
		if (!RefSkeleton.IsValidIndex(SearchRootIndex))
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("Generate BoneConstraints (Chains): search root '%s' does not exist in the preview skeleton."),
			       *SearchRootBone.BoneName.ToString());
			return;
		}

		TSet<int32> ExcludedBoneIndices;
		BuildExcludedBoneIndexSet(RefSkeleton, ExcludeBones, ExcludedBoneIndices);
		AppendChainRootCandidatesFromSearchRoot(RefSkeleton, SearchRootIndex, ExcludedBoneIndices,
		                                        CandidateRootIndices, UniqueCandidateRootIndices);

		if (SortSearchRootIndex == INDEX_NONE)
		{
			SortSearchRootIndex = SearchRootIndex;
		}
		if (DetectRootBoneName == NAME_None)
		{
			DetectRootBoneName = SearchRootBone.BoneName;
		}
	};

	AppendCandidates(Node.RootBone, Node.ExcludeBones);
	for (const FKawaiiPhysicsRootBoneSetting& AdditionalRootBone : Node.AdditionalRootBones)
	{
		AppendCandidates(AdditionalRootBone.RootBone,
		                 AdditionalRootBone.bUseOverrideExcludeBones
			                 ? AdditionalRootBone.OverrideExcludeBones
			                 : Node.ExcludeBones);
	}

	if (!KawaiiPhysicsBoneChain::SortByNumericTokens(RefSkeleton, CandidateRootIndices))
	{
		KawaiiPhysicsBoneChain::SortByRefPoseAngle(RefSkeleton, SortSearchRootIndex, CandidateRootIndices);
	}

	if (CandidateRootIndices.Num() < 2)
	{
		ShowKawaiiPhysicsNodeNotification(
			LOCTEXT("GenerateBoneConstraintsChainsNotEnoughCandidates",
			        "Generate BoneConstraints (Chains) failed: fewer than two chain root candidates were detected. Set DetectRootBone or ChainRootBones in the DataAsset."),
			SNotificationItem::CS_Fail);
		return;
	}

	UKawaiiPhysicsBoneConstraintsDataAsset* DataAsset = Node.BoneConstraintsDataAsset;
	const bool bCreatedDataAsset = DataAsset == nullptr;
	if (DataAsset == nullptr)
	{
		DataAsset = CreateBoneConstraintsDataAssetForChainGeneration(PreviewSkeleton);
		if (DataAsset == nullptr)
		{
			return;
		}
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("Generate BoneConstraints (Chains)")));
	Modify();
	DataAsset->Modify();

	if (bCreatedDataAsset)
	{
		Node.BoneConstraintsDataAsset = DataAsset;
	}

	DataAsset->PreviewSkeleton = PreviewSkeleton;
	DataAsset->UpdatePreviewBoneList();

	const FString GroupName = FString::Printf(TEXT("Node: %s"), *DetectRootBoneName.ToString());
	FKawaiiPhysicsBoneChainGroup* ChainGroup = DataAsset->ChainGroups.FindByPredicate(
		[&GroupName](const FKawaiiPhysicsBoneChainGroup& Group)
		{
			return Group.GroupName == GroupName;
		});
	if (ChainGroup == nullptr)
	{
		ChainGroup = &DataAsset->ChainGroups.AddDefaulted_GetRef();
		ChainGroup->GroupName = GroupName;
	}

	ChainGroup->ChainRootBones.Reset();
	ChainGroup->ChainRootBones.Reserve(CandidateRootIndices.Num());
	for (int32 CandidateRootIndex : CandidateRootIndices)
	{
		if (RefSkeleton.IsValidIndex(CandidateRootIndex))
		{
			ChainGroup->ChainRootBones.Add(FBoneReference(RefSkeleton.GetBoneName(CandidateRootIndex)));
		}
	}
	ChainGroup->DetectRootBone = FBoneReference(DetectRootBoneName);

	DataAsset->MarkPackageDirty();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}

	GEditor->EndTransaction();

	DataAsset->ApplyChains();

	ShowExportAssetNotification(
		DataAsset,
		FText::Format(
			LOCTEXT("GeneratedBoneConstraintsChains",
			        "Generated BoneConstraints (Chains): {0}. Pairs not found in the runtime BoneContainer may be skipped safely."),
			FText::FromString(DataAsset->GetName())));
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
			LOCTEXT("ExportedBoneConstraintsDataAsset", "Exposted BoneConstraints Data Asset: {0}"),
			FText::FromString(AssetName));
		ShowExportAssetNotification(NewDataAsset, NotificationText);
	}
}

#undef LOCTEXT_NAMESPACE
