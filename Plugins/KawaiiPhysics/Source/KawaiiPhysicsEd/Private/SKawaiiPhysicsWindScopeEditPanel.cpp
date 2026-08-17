// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "SKawaiiPhysicsWindScopeEditPanel.h"

#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsWindScopeStyle.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsWindScopeEditPanel"

namespace
{
	constexpr float LabelColumnWidth = 150.0f;
	constexpr float LiveValueTolerance = 0.01f;
	const TCHAR* const WindScopeConfigSectionName = TEXT("KawaiiPhysicsEd");
	const TCHAR* const WindScopeCollapsedGroupsKey = TEXT("WindScopeEditPanelCollapsedGroups");

	FText GetPinDrivenWarningText()
	{
		return LOCTEXT("PinDrivenWarningTooltip", "Pin-driven values take precedence over panel edits.");
	}

	FProperty* FindWindScopeProperty(const FName PropertyName)
	{
		for (UStruct* Struct = FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct(); Struct; Struct = Struct->GetSuperStruct())
		{
			if (FProperty* Property = Struct->FindPropertyByName(PropertyName))
			{
				return Property;
			}
		}
		return nullptr;
	}

	TOptional<float> GetClampMinValue(const FProperty* Property)
	{
		if (!Property || !Property->HasMetaData(TEXT("ClampMin")))
		{
			return TOptional<float>();
		}

		float ClampMin = 0.0f;
		if (LexTryParseString(ClampMin, *Property->GetMetaData(TEXT("ClampMin"))))
		{
			return ClampMin;
		}
		return TOptional<float>();
	}

	FLinearColor ResolveGroupColor(const TOptional<EKawaiiPhysicsWindScopeComponent>& LinkedSeries)
	{
		if (!LinkedSeries.IsSet())
		{
			return FLinearColor(0.28f, 0.3f, 0.34f, 1.0f);
		}

		for (const FKawaiiWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
		{
			if (Style.Component == LinkedSeries.GetValue())
			{
				return Style.Color;
			}
		}
		return FLinearColor(0.28f, 0.3f, 0.34f, 1.0f);
	}

	FLinearColor ResolveComponentColor(EKawaiiPhysicsWindScopeComponent Component)
	{
		for (const FKawaiiWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
		{
			if (Style.Component == Component)
			{
				return Style.Color;
			}
		}
		return FLinearColor::White;
	}

	FSlateColor ResolveLiveWarningColor()
	{
		return FSlateColor(FLinearColor(1.0f, 0.7f, 0.2f));
	}

	FText FormatLiveFloat(float Value)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 2;
		Options.MaximumFractionalDigits = 2;
		return FText::AsNumber(Value, &Options);
	}

	bool IsKnownWindScopeGroupId(FName GroupId)
	{
		if (GroupId.IsNone())
		{
			return false;
		}

		for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
		{
			if (Group.GroupId == GroupId)
			{
				return true;
			}
		}
		return false;
	}

	TSharedRef<STextBlock> MakeFormulaText(const FText& Text, const FSlateColor& Color = FSlateColor::UseForeground())
	{
		return SNew(STextBlock)
			.Text(Text)
			.ColorAndOpacity(Color)
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9));
	}

	class SKawaiiWindScopeGroupHoverBorder : public SBorder
	{
	public:
		SLATE_BEGIN_ARGS(SKawaiiWindScopeGroupHoverBorder)
			{
			}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_EVENT(FSimpleDelegate, OnHovered)
			SLATE_EVENT(FSimpleDelegate, OnUnhovered)
			SLATE_ARGUMENT(const FSlateBrush*, BorderImage)
			SLATE_ATTRIBUTE(FSlateColor, BorderBackgroundColor)
			SLATE_ATTRIBUTE(FMargin, Padding)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnHovered = InArgs._OnHovered;
			OnUnhovered = InArgs._OnUnhovered;
			SBorder::Construct(SBorder::FArguments()
				.BorderImage(InArgs._BorderImage)
				.BorderBackgroundColor(InArgs._BorderBackgroundColor)
				.Padding(InArgs._Padding)
				[
					InArgs._Content.Widget
				]);
		}

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SBorder::OnMouseEnter(MyGeometry, MouseEvent);
			OnHovered.ExecuteIfBound();
		}

		virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
		{
			SBorder::OnMouseLeave(MouseEvent);
			OnUnhovered.ExecuteIfBound();
		}

	private:
		FSimpleDelegate OnHovered;
		FSimpleDelegate OnUnhovered;
	};
}

const TArray<FKawaiiWindScopeParamGroup>& GetWindScopeParamGroups()
{
	static const TArray<FKawaiiWindScopeParamGroup> Groups =
	{
		{
			LOCTEXT("CommonGroupLabel", "Common"),
			FName(TEXT("Common")),
			NAME_None,
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled), 0.0f, 1.0f, true},
			}
		},
		{
			LOCTEXT("DirectionGroupLabel", "Direction"),
			FName(TEXT("Direction")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, DirectionNoiseAngle),
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection), -1.0f, 1.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, DirectionNoiseAngle), 0.0f, 90.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, DirectionNoisePeriod), 0.01f, 10.0f, true},
			}
		},
		{
			LOCTEXT("SteadyGroupLabel", "Steady"),
			FName(TEXT("Steady")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SteadyForce),
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Steady),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SteadyForce), 0.0f, 50.0f, true},
			}
		},
		{
			LOCTEXT("OscillationGroupLabel", "Oscillation"),
			FName(TEXT("Oscillation")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, OscillationForce),
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Oscillation),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, OscillationForce), 0.0f, 50.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, OscillationPeriod), 0.01f, 10.0f, true},
			}
		},
		{
			LOCTEXT("WaveGroupLabel", "Wave"),
			FName(TEXT("Wave")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WaveAmplitude),
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Wave),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WaveAmplitude), 0.0f, 50.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WavePeriod), 0.01f, 10.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WavePhase), -360.0f, 360.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WaveSpatialOffset), 0.0f, 720.0f, true},
			}
		},
		{
			LOCTEXT("EnvelopeGroupLabel", "Envelope"),
			FName(TEXT("Envelope")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, EnvelopeMax),
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Envelope),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, EnvelopeMax), 0.0f, 3.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, EnvelopeMin), 0.0f, 3.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, EnvelopeFrequency), 0.0f, 2.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, EnvelopePhase), -360.0f, 360.0f, true},
			}
		},
		{
			LOCTEXT("RandomGroupLabel", "Random"),
			FName(TEXT("Random")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce),
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Random),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce), 0.0f, 50.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomPeriod), 0.01f, 5.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomSeed), 0.0f, 10000.0f, false},
			}
		},
		{
			LOCTEXT("TimeGroupLabel", "Time"),
			FName(TEXT("Time")),
			GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, TimeScale),
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, TimeScale), 0.0f, 3.0f, true},
			}
		},
	};
	return Groups;
}

FString SerializeWindScopeCollapsedGroups(const TSet<FName>& CollapsedGroups)
{
	TArray<FString> GroupIdStrings;
	for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		if (CollapsedGroups.Contains(Group.GroupId))
		{
			GroupIdStrings.Add(Group.GroupId.ToString());
		}
	}
	return FString::Join(GroupIdStrings, TEXT(","));
}

TSet<FName> ParseWindScopeCollapsedGroups(const FString& CollapsedGroupsValue)
{
	TSet<FName> ParsedGroups;
	TArray<FString> GroupIdStrings;
	CollapsedGroupsValue.ParseIntoArray(GroupIdStrings, TEXT(","), true);

	for (FString& GroupIdString : GroupIdStrings)
	{
		GroupIdString.TrimStartAndEndInline();
		const FName GroupId(*GroupIdString);
		if (IsKnownWindScopeGroupId(GroupId))
		{
			ParsedGroups.Add(GroupId);
		}
	}
	return ParsedGroups;
}

void SKawaiiPhysicsWindScopeEditPanel::Construct(const FArguments& InArgs)
{
	EditValues = InArgs._EditValues;
	LiveEditValues = InArgs._LiveEditValues;
	OnParamEdit = InArgs._OnParamEdit;
	OnParamReset = InArgs._OnParamReset;
	IsParamPinExposed = InArgs._IsParamPinExposed;
	OnFocusNode = InArgs._OnFocusNode;
	OnHighlightSeries = InArgs._OnHighlightSeries;

	LoadCollapsedGroupsFromConfig();
	GroupAreas.Reset();
	GroupPropertyNames.Reset();
	for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		TArray<FName> PropertyNames;
		PropertyNames.Reserve(Group.Params.Num());
		for (const FKawaiiWindScopeParamDef& Param : Group.Params)
		{
			PropertyNames.Add(Param.PropertyName);
		}
		GroupPropertyNames.Add(Group.GroupId, MoveTemp(PropertyNames));
	}

	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);
	ScrollBox->AddSlot()
	.Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EditPanelHeader", "Procedural Wind"))
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 10, TEXT("Bold")))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 3.0f, 0.0f)
		[
			MakeHeaderIconButton(
				FName(TEXT("DetailsView.ExpandAll")),
				LOCTEXT("ExpandAllGroupsTooltip", "Expand all categories."),
				FOnClicked::CreateSP(this, &SKawaiiPhysicsWindScopeEditPanel::OnExpandAllClicked))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 3.0f, 0.0f)
		[
			MakeHeaderIconButton(
				FName(TEXT("DetailsView.CollapseAll")),
				LOCTEXT("CollapseAllGroupsTooltip", "Collapse all categories."),
				FOnClicked::CreateSP(this, &SKawaiiPhysicsWindScopeEditPanel::OnCollapseAllClicked))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeFormulaHelpButton()
		]
	];
	for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		ScrollBox->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			MakeGroupWidget(Group)
		];
	}
	ScrollBox->AddSlot()
	.Padding(0.0f, 2.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(GetPinDrivenWarningText())
		.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
		.AutoWrapText(true)
		.ColorAndOpacity(FLinearColor(0.78f, 0.78f, 0.72f, 1.0f))
	];

	ChildSlot
	[
		ScrollBox
	];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeFormulaHelpButton() const
{
	return SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
		.ContentPadding(FMargin(4.0f, 1.0f))
		.ToolTipText(LOCTEXT("FormulaHelpTooltip", "Composition formula help."))
		.OnGetMenuContent(this, &SKawaiiPhysicsWindScopeEditPanel::MakeFormulaHelpContent)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FormulaHelpButton", "?"))
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9, TEXT("Bold")))
		];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeHeaderIconButton(
	const FName IconName,
	const FText& ToolTipText,
	FOnClicked OnClicked) const
{
	const FName FallbackIconName = IconName == FName(TEXT("DetailsView.ExpandAll"))
		                               ? FName(TEXT("Icons.ChevronDown"))
		                               : FName(TEXT("Icons.ChevronUp"));
	const FSlateBrush* IconBrush = FAppStyle::Get().GetOptionalBrush(
		IconName,
		nullptr,
		FAppStyle::Get().GetBrush(FallbackIconName));

	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
		.ContentPadding(FMargin(2.0f))
		.ToolTipText(ToolTipText)
		.OnClicked(MoveTemp(OnClicked))
		[
			SNew(SBox)
			.WidthOverride(14.0f)
			.HeightOverride(14.0f)
			[
				SNew(SImage)
				.Image(IconBrush)
			]
		];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeFormulaHelpContent() const
{
	return SNew(SBox)
		.WidthOverride(500.0f)
		.Padding(10.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(1.0f, 1.0f))
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaTotalPrefix", "Sample."))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaTotal", "Total"), ResolveComponentColor(EKawaiiPhysicsWindScopeComponent::Total))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaEquals", " = ("))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaSteadyPrefix", "Sample."))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaSteady", "Steady"), ResolveComponentColor(EKawaiiPhysicsWindScopeComponent::Steady))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaPlusOscillation", " + Sample."))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaOscillation", "Oscillation"), ResolveComponentColor(EKawaiiPhysicsWindScopeComponent::Oscillation))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaPlusWave", " + Sample."))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaWave", "Wave"), ResolveComponentColor(EKawaiiPhysicsWindScopeComponent::Wave))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaEnvelopePrefix", ") * Sample."))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaEnvelope", "Envelope"), ResolveComponentColor(EKawaiiPhysicsWindScopeComponent::Envelope))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaPlusRandom", " + Sample."))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaRandom", "Random"), ResolveComponentColor(EKawaiiPhysicsWindScopeComponent::Random))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaPlusGust", " + Sample."))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaGust", "Gust"), ResolveComponentColor(EKawaiiPhysicsWindScopeComponent::Gust))]
				+ SWrapBox::Slot()[MakeFormulaText(LOCTEXT("FormulaSemicolon", ";"))]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FormulaHelpGroupLine1", "Steady, Oscillation, Wave, Envelope, and Random map to their groups."))
				.AutoWrapText(true)
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FormulaHelpGroupLine2", "Gust maps to the Gust button and S/R/D inputs above."))
				.AutoWrapText(true)
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeGroupWidget(const FKawaiiWindScopeParamGroup& Group)
{
	const FLinearColor GroupColor = ResolveGroupColor(Group.LinkedSeries);

	TSharedRef<SWidget> HeaderContent =
		SNew(SKawaiiWindScopeGroupHoverBorder)
		.BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
		.BorderBackgroundColor(GroupColor.CopyWithNewOpacity(0.28f))
		.Padding(FMargin(8.0f, 4.0f))
		.OnHovered_Lambda([this, LinkedSeries = Group.LinkedSeries]()
		{
			OnHighlightSeries.ExecuteIfBound(LinkedSeries);
		})
		.OnUnhovered_Lambda([this]()
		{
			OnHighlightSeries.ExecuteIfBound(TOptional<EKawaiiPhysicsWindScopeComponent>());
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SColorBlock)
				.Color(GroupColor)
				.Size(FVector2D(10.0f, 10.0f))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Group.GroupLabel)
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 10, TEXT("Bold")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryText, Group.SummaryProperty)
				.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryVisibility, Group.GroupId, Group.SummaryProperty)
				.ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.68f, 1.0f))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(8.0f)
				.HeightOverride(8.0f)
				.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetGroupModifiedDotVisibility, Group.GroupId)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetOptionalBrush(
						FName(TEXT("Icons.FilledCircle")),
						nullptr,
						FAppStyle::Get().GetBrush(TEXT("PropertyWindow.DiffersFromDefault"))))
					.ColorAndOpacity(FLinearColor(1.0f, 0.72f, 0.18f, 1.0f))
					.ToolTipText(LOCTEXT("CollapsedGroupModifiedTooltip", "This collapsed category has edits that differ from defaults."))
				]
			]
		];

	TSharedRef<SVerticalBox> BodyBox = SNew(SVerticalBox);

	for (const FKawaiiWindScopeParamDef& Param : Group.Params)
	{
		BodyBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 0.0f)
		[
			MakeParamRow(Param)
		];
	}

	if (Group.Params.Num() > 0 &&
		Group.Params[0].PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
	{
		BodyBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 0.0f)
		[
			MakeCurveRow()
		];
	}

	TSharedPtr<SExpandableArea> GroupArea;
	SAssignNew(GroupArea, SExpandableArea)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBrush")))
		.BodyBorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBrush")))
		.HeaderPadding(FMargin(0.0f))
		.Padding(FMargin(0.0f))
		.InitiallyCollapsed(CollapsedGroups.Contains(Group.GroupId))
		.OnAreaExpansionChanged(this, &SKawaiiPhysicsWindScopeEditPanel::HandleGroupExpansionChanged, Group.GroupId)
		.HeaderContent()
		[
			HeaderContent
		]
		.BodyContent()
		[
			BodyBox
		];

	GroupAreas.Add(GroupArea);
	return GroupArea.ToSharedRef();
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeParamRow(const FKawaiiWindScopeParamDef& ParamDef)
{
	FProperty* Property = FindWindScopeProperty(ParamDef.PropertyName);
	const FText Label = Property ? Property->GetDisplayNameText() : FText::FromString(ParamDef.PropertyName.ToString());
	const FText ToolTipText = Property ? Property->GetToolTipText() : FText::GetEmpty();
	const TOptional<float> ClampMin = GetClampMinValue(Property);

	TSharedPtr<SWidget> ValueWidget;
	if (CastField<FBoolProperty>(Property))
	{
		ValueWidget =
			SNew(SCheckBox)
			.IsChecked(this, &SKawaiiPhysicsWindScopeEditPanel::GetBoolCheckState, ParamDef.PropertyName)
			.OnCheckStateChanged(this, &SKawaiiPhysicsWindScopeEditPanel::HandleBoolChanged, ParamDef.PropertyName)
			.ToolTipText(ToolTipText);
	}
	else if (CastField<FIntProperty>(Property))
	{
		ValueWidget =
			SNew(SSpinBox<int32>)
			.MinValue(ClampMin.IsSet() ? TOptional<int32>(FMath::CeilToInt(ClampMin.GetValue())) : TOptional<int32>())
			.MaxValue(TOptional<int32>())
			.MinSliderValue(FMath::RoundToInt(ParamDef.SliderMin))
			.MaxSliderValue(FMath::RoundToInt(ParamDef.SliderMax))
			.Value(this, &SKawaiiPhysicsWindScopeEditPanel::GetIntValue, ParamDef.PropertyName)
			.OnBeginSliderMovement_Lambda([this, PropertyName = ParamDef.PropertyName]()
			{
				HandleBegin(PropertyName, INDEX_NONE);
			})
			.OnValueChanged_Lambda([this, PropertyName = ParamDef.PropertyName](int32 NewValue)
			{
				HandleScalarChanged(PropertyName, NewValue);
			})
			.OnValueCommitted_Lambda([this, PropertyName = ParamDef.PropertyName](int32 NewValue, ETextCommit::Type CommitType)
			{
				HandleScalarCommitted(PropertyName, NewValue, CommitType);
			})
			.ToolTipText(ToolTipText);
	}
	else if (CastField<FStructProperty>(Property) && ParamDef.PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
	{
		ValueWidget =
			SNew(SNumericVectorInputBox<FVector::FReal>)
			.bColorAxisLabels(true)
			.AllowSpin(true)
			.X(this, &SKawaiiPhysicsWindScopeEditPanel::GetVectorValue, ParamDef.PropertyName, 0)
			.Y(this, &SKawaiiPhysicsWindScopeEditPanel::GetVectorValue, ParamDef.PropertyName, 1)
			.Z(this, &SKawaiiPhysicsWindScopeEditPanel::GetVectorValue, ParamDef.PropertyName, 2)
			.MinSliderVector(TOptional<FVector>(FVector(ParamDef.SliderMin)))
			.MaxSliderVector(TOptional<FVector>(FVector(ParamDef.SliderMax)))
			.OnBeginSliderMovement_Lambda([this, PropertyName = ParamDef.PropertyName]()
			{
				HandleBegin(PropertyName, INDEX_NONE);
			})
			.OnXChanged_Lambda([this, PropertyName = ParamDef.PropertyName](FVector::FReal NewValue)
			{
				HandleVectorChanged(PropertyName, 0, NewValue);
			})
			.OnYChanged_Lambda([this, PropertyName = ParamDef.PropertyName](FVector::FReal NewValue)
			{
				HandleVectorChanged(PropertyName, 1, NewValue);
			})
			.OnZChanged_Lambda([this, PropertyName = ParamDef.PropertyName](FVector::FReal NewValue)
			{
				HandleVectorChanged(PropertyName, 2, NewValue);
			})
			.OnXCommitted_Lambda([this, PropertyName = ParamDef.PropertyName](FVector::FReal NewValue, ETextCommit::Type CommitType)
			{
				HandleVectorCommitted(PropertyName, 0, NewValue, CommitType);
			})
			.OnYCommitted_Lambda([this, PropertyName = ParamDef.PropertyName](FVector::FReal NewValue, ETextCommit::Type CommitType)
			{
				HandleVectorCommitted(PropertyName, 1, NewValue, CommitType);
			})
			.OnZCommitted_Lambda([this, PropertyName = ParamDef.PropertyName](FVector::FReal NewValue, ETextCommit::Type CommitType)
			{
				HandleVectorCommitted(PropertyName, 2, NewValue, CommitType);
			})
			.ToolTipText(ToolTipText);
	}
	else
	{
		ValueWidget =
			SNew(SSpinBox<float>)
			.MinValue(ClampMin)
			.MaxValue(TOptional<float>())
			.MinSliderValue(ParamDef.SliderMin)
			.MaxSliderValue(ParamDef.SliderMax)
			.Value(this, &SKawaiiPhysicsWindScopeEditPanel::GetFloatValue, ParamDef.PropertyName)
			.OnBeginSliderMovement_Lambda([this, PropertyName = ParamDef.PropertyName]()
			{
				HandleBegin(PropertyName, INDEX_NONE);
			})
			.OnValueChanged_Lambda([this, PropertyName = ParamDef.PropertyName](float NewValue)
			{
				HandleScalarChanged(PropertyName, NewValue);
			})
			.OnValueCommitted_Lambda([this, PropertyName = ParamDef.PropertyName](float NewValue, ETextCommit::Type CommitType)
			{
				HandleScalarCommitted(PropertyName, NewValue, CommitType);
			})
			.ToolTipText(ToolTipText);
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(LabelColumnWidth)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Label)
					.ToolTipText(ToolTipText)
					.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					MakePinWarningIcon(ParamDef.PropertyName)
				]
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			ValueWidget.ToSharedRef()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(this, &SKawaiiPhysicsWindScopeEditPanel::GetLiveValueText, ParamDef.PropertyName)
			.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetLiveValueVisibility, ParamDef.PropertyName)
			.ColorAndOpacity(ResolveLiveWarningColor())
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeResetButton(ParamDef.PropertyName)
		];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakePinWarningIcon(FName PropertyName) const
{
	return SNew(SImage)
		.Image(FAppStyle::Get().GetBrush(TEXT("Icons.Warning")))
		.ColorAndOpacity(FLinearColor(1.0f, 0.72f, 0.18f, 1.0f))
		.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetPinWarningVisibility, PropertyName)
		.ToolTipText(GetPinDrivenWarningText());
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeCurveRow() const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CurveEditGuide", "Edit the curve in the Details panel"))
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			.AutoWrapText(true)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FocusNodeButton", "Focus Node"))
			.OnClicked_Lambda([this]()
			{
				OnFocusNode.ExecuteIfBound();
				return FReply::Handled();
			})
		];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeResetButton(FName PropertyName) const
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
		.ContentPadding(FMargin(2.0f))
		.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetResetVisibility, PropertyName)
		.ToolTipText(LOCTEXT("ResetToDefaultTooltip", "Reset to default."))
		.OnClicked_Lambda([this, PropertyName]()
		{
			if (OnParamReset.IsBound())
			{
				OnParamReset.Execute(PropertyName);
			}
			return FReply::Handled();
		})
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush(TEXT("PropertyWindow.DiffersFromDefault")))
		];
}

void SKawaiiPhysicsWindScopeEditPanel::LoadCollapsedGroupsFromConfig()
{
	CollapsedGroups.Reset();

	FString SavedCollapsedGroups;
	if (GConfig)
	{
		GConfig->GetString(
			WindScopeConfigSectionName,
			WindScopeCollapsedGroupsKey,
			SavedCollapsedGroups,
			GEditorPerProjectIni);
	}
	CollapsedGroups = ParseWindScopeCollapsedGroups(SavedCollapsedGroups);
}

void SKawaiiPhysicsWindScopeEditPanel::SaveCollapsedGroupsToConfig() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetString(
		WindScopeConfigSectionName,
		WindScopeCollapsedGroupsKey,
		*SerializeWindScopeCollapsedGroups(CollapsedGroups),
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SKawaiiPhysicsWindScopeEditPanel::HandleGroupExpansionChanged(bool bExpanded, FName GroupId)
{
	if (!IsKnownWindScopeGroupId(GroupId))
	{
		return;
	}

	const bool bChanged = bExpanded
		                      ? CollapsedGroups.Remove(GroupId) > 0
		                      : !CollapsedGroups.Contains(GroupId);
	if (!bExpanded)
	{
		CollapsedGroups.Add(GroupId);
	}

	if (bChanged && !bApplyingGroupExpansionBatch)
	{
		SaveCollapsedGroupsToConfig();
	}
}

FReply SKawaiiPhysicsWindScopeEditPanel::OnExpandAllClicked()
{
	bApplyingGroupExpansionBatch = true;
	CollapsedGroups.Reset();
	for (const TSharedPtr<SExpandableArea>& GroupArea : GroupAreas)
	{
		if (GroupArea.IsValid())
		{
			GroupArea->SetExpanded(true);
		}
	}
	bApplyingGroupExpansionBatch = false;

	SaveCollapsedGroupsToConfig();
	return FReply::Handled();
}

FReply SKawaiiPhysicsWindScopeEditPanel::OnCollapseAllClicked()
{
	bApplyingGroupExpansionBatch = true;
	CollapsedGroups.Reset();
	for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		CollapsedGroups.Add(Group.GroupId);
	}
	for (const TSharedPtr<SExpandableArea>& GroupArea : GroupAreas)
	{
		if (GroupArea.IsValid())
		{
			GroupArea->SetExpanded(false);
		}
	}
	bApplyingGroupExpansionBatch = false;

	SaveCollapsedGroupsToConfig();
	return FReply::Handled();
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetResetVisibility(FName PropertyName) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	return Values && Values->ModifiedFromDefault.Contains(PropertyName)
		       ? EVisibility::Visible
		       : EVisibility::Hidden;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetPinWarningVisibility(FName PropertyName) const
{
	return IsParamPinExposed.IsBound() && IsParamPinExposed.Execute(PropertyName)
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetGroupModifiedDotVisibility(FName GroupId) const
{
	if (!CollapsedGroups.Contains(GroupId))
	{
		return EVisibility::Collapsed;
	}

	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	const TArray<FName>* PropertyNames = GroupPropertyNames.Find(GroupId);
	if (!Values || !PropertyNames)
	{
		return EVisibility::Collapsed;
	}

	for (const FName& PropertyName : *PropertyNames)
	{
		if (Values->ModifiedFromDefault.Contains(PropertyName))
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryVisibility(FName GroupId, FName SummaryProperty) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	return CollapsedGroups.Contains(GroupId) &&
		!SummaryProperty.IsNone() &&
		Values &&
		Values->bValid &&
		Values->FloatValues.Contains(SummaryProperty)
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetLiveValueVisibility(FName PropertyName) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	const FKawaiiWindScopeEditValues* LiveValues = LiveEditValues.Get();
	if (!Values || !Values->bValid || !LiveValues || !LiveValues->bValid)
	{
		return EVisibility::Collapsed;
	}

	FProperty* Property = FindWindScopeProperty(PropertyName);
	if (CastField<FBoolProperty>(Property))
	{
		return PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled) &&
			Values->bIsEnabled != LiveValues->bIsEnabled
			       ? EVisibility::Visible
			       : EVisibility::Collapsed;
	}
	if (CastField<FIntProperty>(Property))
	{
		return PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomSeed) &&
			Values->RandomSeed != LiveValues->RandomSeed
			       ? EVisibility::Visible
			       : EVisibility::Collapsed;
	}
	if (CastField<FStructProperty>(Property) &&
		PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
	{
		for (int32 ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
		{
			if (!FMath::IsNearlyEqual(
				static_cast<float>(Values->WindDirection[ComponentIndex]),
				static_cast<float>(LiveValues->WindDirection[ComponentIndex]),
				LiveValueTolerance))
			{
				return EVisibility::Visible;
			}
		}
		return EVisibility::Collapsed;
	}

	const float* Value = Values->FloatValues.Find(PropertyName);
	const float* LiveValue = LiveValues->FloatValues.Find(PropertyName);
	return Value && LiveValue && !FMath::IsNearlyEqual(*Value, *LiveValue, LiveValueTolerance)
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

FText SKawaiiPhysicsWindScopeEditPanel::GetLiveValueText(FName PropertyName) const
{
	const FKawaiiWindScopeEditValues* LiveValues = LiveEditValues.Get();
	if (!LiveValues || !LiveValues->bValid)
	{
		return FText::GetEmpty();
	}

	FProperty* Property = FindWindScopeProperty(PropertyName);
	FText ValueText;
	if (CastField<FBoolProperty>(Property) &&
		PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
	{
		ValueText = LiveValues->bIsEnabled ? LOCTEXT("LiveBoolTrue", "true") : LOCTEXT("LiveBoolFalse", "false");
	}
	else if (CastField<FIntProperty>(Property) &&
		PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomSeed))
	{
		ValueText = FText::AsNumber(LiveValues->RandomSeed);
	}
	else if (CastField<FStructProperty>(Property) &&
		PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
	{
		ValueText = FText::Format(
			LOCTEXT("LiveVectorValueFormat", "({0}, {1}, {2})"),
			FormatLiveFloat(static_cast<float>(LiveValues->WindDirection.X)),
			FormatLiveFloat(static_cast<float>(LiveValues->WindDirection.Y)),
			FormatLiveFloat(static_cast<float>(LiveValues->WindDirection.Z)));
	}
	else if (const float* LiveValue = LiveValues->FloatValues.Find(PropertyName))
	{
		ValueText = FormatLiveFloat(*LiveValue);
	}
	else
	{
		return FText::GetEmpty();
	}

	return FText::Format(LOCTEXT("LiveValueFormat", "→ {0} (live)"), ValueText);
}

FText SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryText(FName SummaryProperty) const
{
	if (SummaryProperty.IsNone())
	{
		return FText::GetEmpty();
	}

	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	const float* SummaryValue = Values && Values->bValid
		                            ? Values->FloatValues.Find(SummaryProperty)
		                            : nullptr;
	if (!SummaryValue)
	{
		return FText::GetEmpty();
	}

	FProperty* Property = FindWindScopeProperty(SummaryProperty);
	const FText Label = Property ? Property->GetDisplayNameText() : FText::FromString(SummaryProperty.ToString());
	return FText::Format(
		LOCTEXT("CollapsedGroupSummaryFormat", "{0} {1}"),
		Label,
		FormatLiveFloat(*SummaryValue));
}

ECheckBoxState SKawaiiPhysicsWindScopeEditPanel::GetBoolCheckState(FName PropertyName) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	if (!Values || !Values->bValid || PropertyName != GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
	{
		return ECheckBoxState::Undetermined;
	}
	return Values->bIsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

float SKawaiiPhysicsWindScopeEditPanel::GetFloatValue(FName PropertyName) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	if (!Values || !Values->bValid)
	{
		return 0.0f;
	}

	if (const float* Value = Values->FloatValues.Find(PropertyName))
	{
		return *Value;
	}
	return 0.0f;
}

int32 SKawaiiPhysicsWindScopeEditPanel::GetIntValue(FName PropertyName) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	if (!Values || !Values->bValid || PropertyName != GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomSeed))
	{
		return 0;
	}
	return Values->RandomSeed;
}

TOptional<FVector::FReal> SKawaiiPhysicsWindScopeEditPanel::GetVectorValue(FName PropertyName, int32 ComponentIndex) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	if (!Values || !Values->bValid ||
		PropertyName != GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection) ||
		ComponentIndex < 0 || ComponentIndex > 2)
	{
		return TOptional<FVector::FReal>();
	}
	return Values->WindDirection[ComponentIndex];
}

void SKawaiiPhysicsWindScopeEditPanel::HandleBegin(FName PropertyName, int32 VectorComponentIndex) const
{
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, 0.0, VectorComponentIndex, EKawaiiWindEditPhase::Begin);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleScalarChanged(FName PropertyName, double NewValue) const
{
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, INDEX_NONE, EKawaiiWindEditPhase::Interactive);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleScalarCommitted(
	FName PropertyName,
	double NewValue,
	ETextCommit::Type CommitType) const
{
	(void)CommitType;
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, INDEX_NONE, EKawaiiWindEditPhase::Committed);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleVectorChanged(
	FName PropertyName,
	int32 ComponentIndex,
	FVector::FReal NewValue) const
{
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, ComponentIndex, EKawaiiWindEditPhase::Interactive);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleVectorCommitted(
	FName PropertyName,
	int32 ComponentIndex,
	FVector::FReal NewValue,
	ETextCommit::Type CommitType) const
{
	(void)CommitType;
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, ComponentIndex, EKawaiiWindEditPhase::Committed);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleBoolChanged(ECheckBoxState NewState, FName PropertyName) const
{
	const double NewValue = NewState == ECheckBoxState::Checked ? 1.0 : 0.0;
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, INDEX_NONE, EKawaiiWindEditPhase::Committed);
	}
}

#undef LOCTEXT_NAMESPACE
