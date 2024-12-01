// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimGraphNode_KawaiiPhysics.h"

#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
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

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

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

void UAnimGraphNode_KawaiiPhysics::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	Node.ModifyBones.Empty();
	ReconstructNode();
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
	KawaiiPhysics->OverrideTargetFramerate = Node.OverrideTargetFramerate;

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
	KawaiiPhysics->BoneForwardAxis = Node.BoneForwardAxis;

	// Limits
	KawaiiPhysics->SphericalLimits = Node.SphericalLimits;
	KawaiiPhysics->CapsuleLimits = Node.CapsuleLimits;
	KawaiiPhysics->BoxLimits = Node.BoxLimits;
	KawaiiPhysics->PlanarLimits = Node.PlanarLimits;
	KawaiiPhysics->LimitsDataAsset = Node.LimitsDataAsset;
	KawaiiPhysics->PhysicsAssetForLimits = Node.PhysicsAssetForLimits;

	// ExternalForce
	KawaiiPhysics->Gravity = Node.Gravity;
	KawaiiPhysics->ExternalForces = Node.ExternalForces;
	KawaiiPhysics->CustomExternalForces = Node.CustomExternalForces;

	// Wind
	KawaiiPhysics->bEnableWind = Node.bEnableWind;
	KawaiiPhysics->WindScale = Node.WindScale;

	// BoneConstraint
	KawaiiPhysics->BoneConstraintGlobalComplianceType = Node.BoneConstraintGlobalComplianceType;
	KawaiiPhysics->BoneConstraintIterationCountBeforeCollision = Node.BoneConstraintIterationCountBeforeCollision;
	KawaiiPhysics->BoneConstraintIterationCountAfterCollision = Node.BoneConstraintIterationCountAfterCollision;
	KawaiiPhysics->bAutoAddChildDummyBoneConstraint = Node.bAutoAddChildDummyBoneConstraint;
	KawaiiPhysics->BoneConstraints = Node.BoneConstraints;
	KawaiiPhysics->BoneConstraintsDataAsset = Node.BoneConstraintsDataAsset;

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
			]
		]
	];
}

void UAnimGraphNode_KawaiiPhysics::CustomizeDetailDebugVisualizations(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& ViewportCategory = DetailBuilder.EditCategory(TEXT("Debug Visualization"));
	FDetailWidgetRow& WidgetRow = ViewportCategory.AddCustomRow(
		LOCTEXT("ToggleDebugVisualizationButtonRow", "DebugVisualization"));

	WidgetRow
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2, 0, 2, 0))
		// Show/Hide Bones button.
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawBone = !this->bEnableDebugDrawBone;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawBone
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowBoneText", "Bone"); })
			]
		]
		// Show/Hide LengthRate button.
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugBoneLengthRate = !this->bEnableDebugBoneLengthRate;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugBoneLengthRate
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowLengthRateText", "Length Rate"); })
			]
		]
		// Show/Hide AngleLimit button.
		+ SUniformGridPanel::Slot(2, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawLimitAngle = !this->bEnableDebugDrawLimitAngle;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawLimitAngle
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowLimitAngleText", "Limit Angle"); })
			]
		]
		// Show/Hide SphereLimit button.
		+ SUniformGridPanel::Slot(0, 1)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawSphereLimit = !this->bEnableDebugDrawSphereLimit;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawSphereLimit
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowSphereLimitText", "Sphere Limit"); })
			]
		]
		// Show/Hide CapsuleLimit button.
		+ SUniformGridPanel::Slot(1, 1)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawCapsuleLimit = !this->bEnableDebugDrawCapsuleLimit;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawCapsuleLimit
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowCapsuleLimitText", "Capsule Limit"); })
			]
		]
		// Show/Hide BoxLimit button.
		+ SUniformGridPanel::Slot(2, 1)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawBoxLimit = !this->bEnableDebugDrawBoxLimit;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawBoxLimit
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowBoxLimitText", "Box Limit"); })
			]
		]
		// Show/Hide PlanerLimit button.
		+ SUniformGridPanel::Slot(0, 2)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawPlanerLimit = !this->bEnableDebugDrawPlanerLimit;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawPlanerLimit
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowPlanerLimitText", "Planer Limit"); })
			]
		]
		// Show/Hide BoneConstraint button.
		+ SUniformGridPanel::Slot(1, 2)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawBoneConstraint = !this->bEnableDebugDrawBoneConstraint;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawBoneConstraint
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowBoneConstraintText", "Bone Constraint"); })
			]
		]
		// Show/Hide ExternalForce button.
		+ SUniformGridPanel::Slot(2, 2)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				this->bEnableDebugDrawExternalForce = !this->bEnableDebugDrawExternalForce;
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return this->bEnableDebugDrawExternalForce
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return LOCTEXT("ShowExternalForceText", "External Force"); })
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
		SafeSetOrder(FName("Physics Settings"));
		SafeSetOrder(FName("Physics Settings Advanced"));

		// Limits
		SafeSetOrder(FName("Limits"));
		SafeSetOrder(FName("Bone Constraint (Experimental)"));

		// Other
		SafeSetOrder(FName("World Collision"));
		SafeSetOrder(FName("ExternalForce"));

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

	if (Ar.CustomVer(FKawaiiPhysicsVersion::GUID) < FKawaiiPhysicsVersion::UseRuntimeFloatCurve)
	{
		Node.DampingCurveData.ExternalCurve = Node.DampingCurve_DEPRECATED;
		Node.WorldDampingLocationCurveData.ExternalCurve = Node.WorldDampingLocationCurve_DEPRECATED;
		Node.WorldDampingRotationCurveData.ExternalCurve = Node.WorldDampingRotationCurve_DEPRECATED;
		Node.StiffnessCurveData.ExternalCurve = Node.StiffnessCurve_DEPRECATED;
		Node.RadiusCurveData.ExternalCurve = Node.RadiusCurve_DEPRECATED;
		Node.LimitAngleCurveData.ExternalCurve = Node.LimitAngleCurve_DEPRECATED;
	}
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
