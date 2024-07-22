#include "AnimGraphNode_KawaiiPhysics.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Selection.h"
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

	// FText::Format() is slow, so we cache this to save on performance
	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(
			                                LOCTEXT("AnimGraphNode_KawaiiPhysics_ListTitle",
			                                        "{ControllerDescription} - Root: {RootBoneName}"), Args), this);
	}
	else
	{
		CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(
			                                LOCTEXT("AnimGraphNode_KawaiiPhysics_Title",
			                                        "{ControllerDescription}\nRoot: {RootBoneName} "), Args), this);
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
	KawaiiPhysics->TargetFramerate = Node.TargetFramerate;
	KawaiiPhysics->OverrideTargetFramerate = Node.OverrideTargetFramerate;

	// Physics Settings
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
	KawaiiPhysics->PlanarLimits = Node.PlanarLimits;
	KawaiiPhysics->LimitsDataAsset = Node.LimitsDataAsset;

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
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
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
				.Text(FText::FromString(TEXT("Export Limits Data Asset")))
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
		+ SUniformGridPanel::Slot(3, 0)
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
		+ SUniformGridPanel::Slot(0, 1)
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
		// Show/Hide PlanerLimit button.
		+ SUniformGridPanel::Slot(1, 1)
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
		+ SUniformGridPanel::Slot(2, 1)
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
		+ SUniformGridPanel::Slot(3, 1)
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

	// Force order of details panel catagories - Must set order for all of them as any that are edited automatically move to the top.
	{
		uint32 SortOrder = 0;

		// Tools, Debug
		DetailBuilder.EditCategory("Kawaii Physics Tools").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Debug Visualization").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Functions").SetSortOrder(SortOrder++);

		// Basic
		DetailBuilder.EditCategory("Bones").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Physics Settings").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Physics Settings Advanced").SetSortOrder(SortOrder++);

		// Limits
		DetailBuilder.EditCategory("Limits").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Bone Constraint (Experimental)").SetSortOrder(SortOrder++);

		// Other
		DetailBuilder.EditCategory("World Collision").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("ExternalForce").SetSortOrder(SortOrder++);

		// AnimNode
		DetailBuilder.EditCategory("Tag").SetSortOrder(SortOrder++);
		DetailBuilder.EditCategory("Alpha").SetSortOrder(SortOrder++);
	}
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

void UAnimGraphNode_KawaiiPhysics::ExportLimitsDataAsset()
{
	const FString DefaultAsset = FPackageName::GetLongPackagePath(GetOutermost()->GetName()) + TEXT("/") + GetName() +
		TEXT("_Collision");

	const TSharedRef<SDlgPickAssetPath> NewAssetDlg =
		SNew(SDlgPickAssetPath)
			.Title(LOCTEXT("NewDataAssetDialogTitle", "Choose Location for Collision Data Asset"))
			.DefaultAssetPath(FText::FromString(DefaultAsset));

	if (NewAssetDlg->ShowModal() == EAppReturnType::Cancel)
	{
		return;
	}

	const FString Package(NewAssetDlg->GetFullAssetPath().ToString());
	const FString Name(NewAssetDlg->GetAssetName().ToString());

	UPackage* Pkg = CreatePackage(*Package);

	if (UKawaiiPhysicsLimitsDataAsset* NewDataAsset =
		NewObject<UKawaiiPhysicsLimitsDataAsset>(Pkg, UKawaiiPhysicsLimitsDataAsset::StaticClass(), FName(Name),
		                                         RF_Public | RF_Standalone))
	{
		// copy data
		NewDataAsset->SphericalLimitsData.SetNum(Node.SphericalLimits.Num());
		for (int32 i = 0; i < Node.SphericalLimits.Num(); i++)
		{
			NewDataAsset->SphericalLimitsData[i].Update(&Node.SphericalLimits[i]);
		}

		NewDataAsset->CapsuleLimitsData.SetNum(Node.CapsuleLimits.Num());
		for (int32 i = 0; i < Node.CapsuleLimits.Num(); i++)
		{
			NewDataAsset->CapsuleLimitsData[i].Update(&Node.CapsuleLimits[i]);
		}

		NewDataAsset->PlanarLimitsData.SetNum(Node.PlanarLimits.Num());
		for (int32 i = 0; i < Node.PlanarLimits.Num(); i++)
		{
			NewDataAsset->PlanarLimitsData[i].Update(&Node.PlanarLimits[i]);
		}

		NewDataAsset->Sync();

		// select new asset
		USelection* SelectionSet = GEditor->GetSelectedObjects();
		SelectionSet->DeselectAll();
		SelectionSet->Select(NewDataAsset);

		FAssetRegistryModule::AssetCreated(NewDataAsset);
		Pkg->MarkPackageDirty();
	}
}

#undef LOCTEXT_NAMESPACE
