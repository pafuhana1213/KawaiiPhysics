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
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsWindScopeEditPanel"

namespace KawaiiPhysicsWindScopeEditPanelPrivate
{
	constexpr float LabelColumnWidth = 190.0f;
	constexpr float LiveValueTolerance = 0.01f;
	const TCHAR* const EditPanelConfigSectionName = TEXT("KawaiiPhysicsEd");
	const TCHAR* const WindScopeCollapsedGroupsKey = TEXT("WindScopeEditPanelCollapsedGroups");

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

	bool IsEditPanelFloatIntervalProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == TBaseStructure<FFloatInterval>::Get();
	}

	bool IsEditPanelParameterModeProperty(const FProperty* Property)
	{
		const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property);
		return EnumProperty &&
			EnumProperty->GetEnum() &&
			EnumProperty->GetEnum()->GetFName() == StaticEnum<EKawaiiProceduralWindParameterMode>()->GetFName();
	}

	TArray<TSharedPtr<EKawaiiProceduralWindParameterMode>>& GetParameterModeItems()
	{
		static TArray<TSharedPtr<EKawaiiProceduralWindParameterMode>> Items;
		if (Items.Num() == 0)
		{
			Items.Add(MakeShared<EKawaiiProceduralWindParameterMode>(EKawaiiProceduralWindParameterMode::Simple));
			Items.Add(MakeShared<EKawaiiProceduralWindParameterMode>(EKawaiiProceduralWindParameterMode::Advanced));
		}
		return Items;
	}

	FText FormatParameterMode(EKawaiiProceduralWindParameterMode Mode)
	{
		switch (Mode)
		{
		case EKawaiiProceduralWindParameterMode::Simple:
			return LOCTEXT("ParameterModeSimple", "Simple");
		case EKawaiiProceduralWindParameterMode::Advanced:
			return LOCTEXT("ParameterModeAdvanced", "Advanced");
		default:
			return FText::GetEmpty();
		}
	}

	FLinearColor ResolveGroupColor(const TOptional<EKawaiiPhysicsWindScopeComponent>& LinkedSeries)
	{
		if (!LinkedSeries.IsSet())
		{
			return FLinearColor(0.28f, 0.3f, 0.34f, 1.0f);
		}

		for (const FKawaiiPhysicsWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
		{
			if (Style.Component == LinkedSeries.GetValue())
			{
				return Style.Color;
			}
		}
		return FLinearColor(0.28f, 0.3f, 0.34f, 1.0f);
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

	FText FormatSummaryValueWithUnit(const FText& ValueText, EKawaiiPhysicsWindScopeSummaryUnit Unit)
	{
		switch (Unit)
		{
		case EKawaiiPhysicsWindScopeSummaryUnit::Seconds:
			return FText::Format(LOCTEXT("SummaryValueSeconds", "{0}s"), ValueText);
		case EKawaiiPhysicsWindScopeSummaryUnit::Degrees:
			return FText::Format(LOCTEXT("SummaryValueDegrees", "{0}°"), ValueText);
		case EKawaiiPhysicsWindScopeSummaryUnit::None:
		default:
			return ValueText;
		}
	}

	bool IsKnownWindScopeGroupId(FName GroupId)
	{
		if (GroupId.IsNone())
		{
			return false;
		}

		for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
		{
			if (Group.GroupId == GroupId)
			{
				return true;
			}
		}
		return false;
	}

	class SKawaiiPhysicsWindScopeGroupHoverBorder : public SBorder
	{
	public:
		SLATE_BEGIN_ARGS(SKawaiiPhysicsWindScopeGroupHoverBorder)
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

const TArray<FKawaiiPhysicsWindScopeParamGroup>& GetWindScopeParamGroups()
{
	static const TArray<FKawaiiPhysicsWindScopeParamGroup> Groups =
	{
		{
			LOCTEXT("CommonGroupLabel", "Common"),
			FName(TEXT("Common")),
			{},
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, ParameterMode), 0.0f, 1.0f, false},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled), 0.0f, 1.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, TimeScale), 0.0f, 3.0f, true, true},
			},
			true
		},
		{
			LOCTEXT("DirectionGroupLabel", "Direction"),
			FName(TEXT("Direction")),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection), FText::GetEmpty()},
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirectionNoiseAngle),
					LOCTEXT("SummaryLabelNoise", "Noise"),
					EKawaiiPhysicsWindScopeSummaryUnit::Degrees,
					true
				},
			},
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection), -1.0f, 1.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirectionNoiseAngle), 0.0f, 90.0f, true, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirectionNoisePeriod), 0.01f, 10.0f, true, true},
			}
		},
		{
			LOCTEXT("ConstantGroupLabel", "Constant"),
			FName(TEXT("Constant")),
			{
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, ConstantForce),
					LOCTEXT("SummaryLabelForce", "Force")
				},
			},
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Constant),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, ConstantForce), 0.0f, 50.0f, true},
			}
		},
		{
			LOCTEXT("SwayGroupLabel", "Sway"),
			FName(TEXT("Sway")),
			{
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayForce),
					LOCTEXT("SummaryLabelForce", "Force")
				},
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayPeriod),
					LOCTEXT("SummaryLabelPeriod", "Period"),
					EKawaiiPhysicsWindScopeSummaryUnit::Seconds
				},
			},
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Sway),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayForce), 0.0f, 50.0f, true, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayPeriod), 0.01f, 10.0f, true, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayPhaseOffset), -360.0f, 360.0f, true, true},
			}
		},
		{
			LOCTEXT("RippleGroupLabel", "Ripple"),
			FName(TEXT("Ripple")),
			{
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RippleForce),
					LOCTEXT("SummaryLabelForce", "Force")
				},
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePeriod),
					LOCTEXT("SummaryLabelPeriod", "Period"),
					EKawaiiPhysicsWindScopeSummaryUnit::Seconds
				},
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RippleTipPhaseDelay),
					LOCTEXT("SummaryLabelTipDelay", "Tip Delay"),
					EKawaiiPhysicsWindScopeSummaryUnit::Degrees
				},
			},
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Ripple),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RippleForce), 0.0f, 50.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePeriod), 0.01f, 10.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePhaseOffset), -360.0f, 360.0f, true, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RippleTipPhaseDelay), 0.0f, 720.0f, true},
			}
		},
		{
			LOCTEXT("StrengthCycleGroupLabel", "StrengthCycle"),
			FName(TEXT("StrengthCycle")),
			{
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCycleRange),
					LOCTEXT("SummaryLabelRange", "Range")
				},
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePeriod),
					LOCTEXT("SummaryLabelPeriod", "Period"),
					EKawaiiPhysicsWindScopeSummaryUnit::Seconds
				},
			},
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::StrengthCycle),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCycleRange), 0.0f, 3.0f, true, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePeriod), 0.01f, 60.0f, true, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePhaseOffset), -360.0f, 360.0f, true, true},
			}
		},
		{
			LOCTEXT("RandomGroupLabel", "Random"),
			FName(TEXT("Random")),
			{
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce),
					LOCTEXT("SummaryLabelForce", "Force")
				},
				{
					GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForcePeriod),
					LOCTEXT("SummaryLabelPeriod", "Period"),
					EKawaiiPhysicsWindScopeSummaryUnit::Seconds
				},
			},
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Random),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce), 0.0f, 50.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForcePeriod), 0.01f, 5.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, Seed), 0.0f, 10000.0f, false, true},
			}
		},
	};
	return Groups;
}

FString SerializeWindScopeCollapsedGroups(const TSet<FName>& CollapsedGroups)
{
	TArray<FString> GroupIdStrings;
	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
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
		if (KawaiiPhysicsWindScopeEditPanelPrivate::IsKnownWindScopeGroupId(GroupId))
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
	OnHighlightSeries = InArgs._OnHighlightSeries;

	LoadCollapsedGroupsFromConfig();
	GroupAreas.Reset();
	GroupPropertyNames.Reset();
	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		TArray<FName> PropertyNames;
		PropertyNames.Reserve(Group.Params.Num());
		for (const FKawaiiPhysicsWindScopeParamDef& Param : Group.Params)
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
	];
	// 固定グループ（表示モード・有効）はカテゴリ化せずヘッダー直下に常時表示
	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		if (!Group.bPinned)
		{
			continue;
		}
		for (const FKawaiiPhysicsWindScopeParamDef& Param : Group.Params)
		{
			ScrollBox->AddSlot()
			.Padding(4.0f, 0.0f, 4.0f, 4.0f)
			[
				MakeParamRow(Param)
			];
		}
	}
	ScrollBox->AddSlot()
	.Padding(0.0f, 2.0f, 0.0f, 6.0f)
	[
		SNew(SSeparator)
		.Orientation(Orient_Horizontal)
	];
	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		if (Group.bPinned)
		{
			continue;
		}
		ScrollBox->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			MakeGroupWidget(Group)
		];
	}

	ChildSlot
	[
		ScrollBox
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

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeGroupWidget(const FKawaiiPhysicsWindScopeParamGroup& Group)
{
	TSharedRef<SWidget> HeaderContent =
		SNew(KawaiiPhysicsWindScopeEditPanelPrivate::SKawaiiPhysicsWindScopeGroupHoverBorder)
		.BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
		.BorderBackgroundColor_Lambda([this, LinkedSeries = Group.LinkedSeries]()
		{
			return FSlateColor(ResolveSeriesDisplayColor(LinkedSeries).CopyWithNewOpacity(0.28f));
		})
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
				.Color_Lambda([this, LinkedSeries = Group.LinkedSeries]()
				{
					return ResolveSeriesDisplayColor(LinkedSeries);
				})
				.Size(FVector2D(10.0f, 10.0f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				// 最長ラベル（StrengthCycle）に合わせて最小幅を揃え、サマリーの開始位置を全カテゴリで縦に揃える
				SNew(SBox)
				.MinDesiredWidth(100.0f)
				[
					SNew(STextBlock)
					.Text(Group.GroupLabel)
					.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 10, TEXT("Bold")))
					.ColorAndOpacity_Lambda([this, LinkedSeries = Group.LinkedSeries]()
					{
						return FSlateColor(ResolveSeriesDisplayColor(LinkedSeries));
					})
				]
			]
			// サマリーはカテゴリ名の直後に左寄せで続けて視線移動を短くする。
			// FillWidth なのは狭いパネルで Ellipsis を効かせるため（AutoWidth だと右端の変更ドットを押し出す）
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryText, Group.GroupId)
				.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryVisibility, Group.GroupId)
				.ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.68f, 1.0f))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
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

	for (const FKawaiiPhysicsWindScopeParamDef& Param : Group.Params)
	{
		BodyBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 0.0f)
		[
			MakeParamRow(Param)
		];
	}

	TSharedPtr<SExpandableArea> GroupArea;
	SAssignNew(GroupArea, SExpandableArea)
		.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetGroupVisibility, Group.GroupId)
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

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeParamRow(const FKawaiiPhysicsWindScopeParamDef& ParamDef)
{
	FProperty* Property = KawaiiPhysicsWindScopeEditPanelPrivate::FindWindScopeProperty(ParamDef.PropertyName);
	const FText Label = Property ? Property->GetDisplayNameText() : FText::FromString(ParamDef.PropertyName.ToString());
	const FText ToolTipText = Property ? Property->GetToolTipText() : FText::GetEmpty();
	const TOptional<float> ClampMin = KawaiiPhysicsWindScopeEditPanelPrivate::GetClampMinValue(Property);

	TSharedPtr<SWidget> ValueWidget;
	if (CastField<FBoolProperty>(Property))
	{
		ValueWidget =
			SNew(SCheckBox)
			.IsChecked(this, &SKawaiiPhysicsWindScopeEditPanel::GetBoolCheckState, ParamDef.PropertyName)
			.OnCheckStateChanged(this, &SKawaiiPhysicsWindScopeEditPanel::HandleBoolChanged, ParamDef.PropertyName)
			.ToolTipText(ToolTipText);
	}
	else if (KawaiiPhysicsWindScopeEditPanelPrivate::IsEditPanelParameterModeProperty(Property))
	{
		ValueWidget =
			SAssignNew(ParameterModeComboBox, SComboBox<TSharedPtr<EKawaiiProceduralWindParameterMode>>)
			.OptionsSource(&KawaiiPhysicsWindScopeEditPanelPrivate::GetParameterModeItems())
			.InitiallySelectedItem(KawaiiPhysicsWindScopeEditPanelPrivate::GetParameterModeItems()[0])
			.OnGenerateWidget_Lambda([](TSharedPtr<EKawaiiProceduralWindParameterMode> Item)
			{
				return SNew(STextBlock)
					.Text(Item.IsValid() ? KawaiiPhysicsWindScopeEditPanelPrivate::FormatParameterMode(*Item) : FText::GetEmpty())
					.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9));
			})
			.OnSelectionChanged_Lambda([this, PropertyName = ParamDef.PropertyName](TSharedPtr<EKawaiiProceduralWindParameterMode> Item, ESelectInfo::Type SelectInfo)
			{
				(void)SelectInfo;
				if (bSyncingParameterModeCombo)
				{
					return;
				}
				if (Item.IsValid() && OnParamEdit.IsBound())
				{
					OnParamEdit.Execute(PropertyName, static_cast<double>(static_cast<uint8>(*Item)), INDEX_NONE, EKawaiiPhysicsWindEditPhase::Committed);
				}
			})
			.ToolTipText(ToolTipText)
			[
				SNew(STextBlock)
				.Text(this, &SKawaiiPhysicsWindScopeEditPanel::GetParameterModeText)
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			];
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
	else if (KawaiiPhysicsWindScopeEditPanelPrivate::IsEditPanelFloatIntervalProperty(Property))
	{
		ValueWidget =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("IntervalMinLabel", "Min"))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SSpinBox<float>)
				.MinValue(ClampMin)
				.MaxValue(TOptional<float>())
				.MinSliderValue(ParamDef.SliderMin)
				.MaxSliderValue(ParamDef.SliderMax)
				.Value_Lambda([this, PropertyName = ParamDef.PropertyName]()
				{
					const TOptional<float> Value = GetIntervalValue(PropertyName, 0);
					return Value.IsSet() ? Value.GetValue() : 0.0f;
				})
				.OnBeginSliderMovement_Lambda([this, PropertyName = ParamDef.PropertyName]()
				{
					HandleBegin(PropertyName, 0);
				})
				.OnValueChanged_Lambda([this, PropertyName = ParamDef.PropertyName](float NewValue)
				{
					HandleVectorChanged(PropertyName, 0, NewValue);
				})
				.OnValueCommitted_Lambda([this, PropertyName = ParamDef.PropertyName](float NewValue, ETextCommit::Type CommitType)
				{
					HandleVectorCommitted(PropertyName, 0, NewValue, CommitType);
				})
				.ToolTipText(ToolTipText)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("IntervalMaxLabel", "Max"))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(ClampMin)
				.MaxValue(TOptional<float>())
				.MinSliderValue(ParamDef.SliderMin)
				.MaxSliderValue(ParamDef.SliderMax)
				.Value_Lambda([this, PropertyName = ParamDef.PropertyName]()
				{
					const TOptional<float> Value = GetIntervalValue(PropertyName, 1);
					return Value.IsSet() ? Value.GetValue() : 0.0f;
				})
				.OnBeginSliderMovement_Lambda([this, PropertyName = ParamDef.PropertyName]()
				{
					HandleBegin(PropertyName, 1);
				})
				.OnValueChanged_Lambda([this, PropertyName = ParamDef.PropertyName](float NewValue)
				{
					HandleVectorChanged(PropertyName, 1, NewValue);
				})
				.OnValueCommitted_Lambda([this, PropertyName = ParamDef.PropertyName](float NewValue, ETextCommit::Type CommitType)
				{
					HandleVectorCommitted(PropertyName, 1, NewValue, CommitType);
				})
				.ToolTipText(ToolTipText)
			];
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

	// 値ウィジェットはパネル幅に追従させるため FillWidth + HAlign_Fill で伸縮させる
	// （固定幅の SBox で包まない。CheckBox もこの扱いで問題ない）
	return SNew(SHorizontalBox)
		.Visibility(this, &SKawaiiPhysicsWindScopeEditPanel::GetParamRowVisibility, ParamDef.PropertyName)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(KawaiiPhysicsWindScopeEditPanelPrivate::LabelColumnWidth)
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
					// ラベル列を広げたので折り返しは不要。クリップ（省略記号）も付けず全文表示する
					.AutoWrapText(false)
				]
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			ValueWidget.ToSharedRef()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(this, &SKawaiiPhysicsWindScopeEditPanel::GetLiveValueText, ParamDef.PropertyName)
			.Visibility_Lambda([this, PropertyName = ParamDef.PropertyName]()
			{
				// Hidden ではなく Collapsed で幅を消し、値ウィジェットの右端の位置が揺れないようにする
				return GetLiveValueVisibility(PropertyName) == EVisibility::Visible
					       ? EVisibility::Visible
					       : EVisibility::Collapsed;
			})
			.ColorAndOpacity(KawaiiPhysicsWindScopeEditPanelPrivate::ResolveLiveWarningColor())
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			.Justification(ETextJustify::Left)
		]
		// リセットボタンは非表示時も Hidden（Collapsed ではない）で 24px 分の幅を確保し、
		// 行ごとに右端の位置がずれないようにする
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(24.0f)
			[
				MakeResetButton(ParamDef.PropertyName)
			]
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

// SComboBox は内部 SelectedItem と異なる項目を選んだときしか OnSelectionChanged を発火せず、
// さらにキーボードの Up/Down はメニューを開かずにその内部 SelectedItem を起点に隣の項目を選ぶ
// （SComboBox::OnKeyDown）。開いた瞬間だけ同期してもキーボード・ゲームパッド経路を取りこぼすため、
// 毎フレーム実値へ寄せる。未生成・展開中・既に一致のいずれかで即 return するので、
// 親ウィンドウが毎フレーム行っている CachedEditValues の再構築に比べれば無視できるコスト
void SKawaiiPhysicsWindScopeEditPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	SyncParameterModeComboSelection();
}

// SetSelectedItem は SListView 経由で OnSelectionChanged を誘発するため、ガードを立てて
// 自前ハンドラ（＝不要な編集イベント）を抑止してから同期する。展開中に呼ぶと
// OnSelectionChanged_Internal の SetIsOpen(false) でドロップダウンが閉じてしまうので触らない
void SKawaiiPhysicsWindScopeEditPanel::SyncParameterModeComboSelection()
{
	if (!ParameterModeComboBox.IsValid() || ParameterModeComboBox->IsOpen())
	{
		return;
	}

	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	if (!Values || !Values->bValid)
	{
		return;
	}

	const TArray<TSharedPtr<EKawaiiProceduralWindParameterMode>>& Items =
		KawaiiPhysicsWindScopeEditPanelPrivate::GetParameterModeItems();
	const TSharedPtr<EKawaiiProceduralWindParameterMode>* MatchedItem = Items.FindByPredicate(
		[Mode = Values->ParameterMode](const TSharedPtr<EKawaiiProceduralWindParameterMode>& Item)
		{
			return Item.IsValid() && *Item == Mode;
		});
	if (!MatchedItem || *MatchedItem == ParameterModeComboBox->GetSelectedItem())
	{
		return;
	}

	TGuardValue<bool> SyncGuard(bSyncingParameterModeCombo, true);
	ParameterModeComboBox->SetSelectedItem(*MatchedItem);
}

void SKawaiiPhysicsWindScopeEditPanel::LoadCollapsedGroupsFromConfig()
{
	CollapsedGroups.Reset();

	FString SavedCollapsedGroups;
	if (GConfig)
	{
		GConfig->GetString(
			KawaiiPhysicsWindScopeEditPanelPrivate::EditPanelConfigSectionName,
			KawaiiPhysicsWindScopeEditPanelPrivate::WindScopeCollapsedGroupsKey,
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
		KawaiiPhysicsWindScopeEditPanelPrivate::EditPanelConfigSectionName,
		KawaiiPhysicsWindScopeEditPanelPrivate::WindScopeCollapsedGroupsKey,
		*SerializeWindScopeCollapsedGroups(CollapsedGroups),
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SKawaiiPhysicsWindScopeEditPanel::HandleGroupExpansionChanged(bool bExpanded, FName GroupId)
{
	if (!KawaiiPhysicsWindScopeEditPanelPrivate::IsKnownWindScopeGroupId(GroupId))
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
	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
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

bool SKawaiiPhysicsWindScopeEditPanel::IsAdvancedMode() const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	return Values && Values->bValid && Values->ParameterMode == EKawaiiProceduralWindParameterMode::Advanced;
}

bool SKawaiiPhysicsWindScopeEditPanel::IsParamAdvancedOnly(FName PropertyName) const
{
	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		for (const FKawaiiPhysicsWindScopeParamDef& Param : Group.Params)
		{
			if (Param.PropertyName == PropertyName)
			{
				return Param.bAdvancedOnly;
			}
		}
	}
	return false;
}

bool SKawaiiPhysicsWindScopeEditPanel::IsParamVisibleInCurrentMode(FName PropertyName) const
{
	return IsAdvancedMode() || !IsParamAdvancedOnly(PropertyName);
}

FLinearColor SKawaiiPhysicsWindScopeEditPanel::ResolveSeriesDisplayColor(
	TOptional<EKawaiiPhysicsWindScopeComponent> LinkedSeries) const
{
	// 非アクティブ系列の淡色表示は左上の計算式ヘッダに任せ、編集パネル側はカテゴリ名の視認性を優先して常に本来の色で表示する
	return KawaiiPhysicsWindScopeEditPanelPrivate::ResolveGroupColor(LinkedSeries);
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetResetVisibility(FName PropertyName) const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	return Values && Values->ModifiedFromDefault.Contains(PropertyName)
		       ? EVisibility::Visible
		       : EVisibility::Hidden;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetParamRowVisibility(FName PropertyName) const
{
	return IsParamVisibleInCurrentMode(PropertyName) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetGroupVisibility(FName GroupId) const
{
	const TArray<FName>* PropertyNames = GroupPropertyNames.Find(GroupId);
	if (!PropertyNames)
	{
		return EVisibility::Collapsed;
	}

	for (const FName& PropertyName : *PropertyNames)
	{
		if (IsParamVisibleInCurrentMode(PropertyName))
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetGroupModifiedDotVisibility(FName GroupId) const
{
	if (!CollapsedGroups.Contains(GroupId))
	{
		return EVisibility::Collapsed;
	}

	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	const TArray<FName>* PropertyNames = GroupPropertyNames.Find(GroupId);
	if (!Values || !PropertyNames)
	{
		return EVisibility::Collapsed;
	}

	for (const FName& PropertyName : *PropertyNames)
	{
		if (IsParamVisibleInCurrentMode(PropertyName) && Values->ModifiedFromDefault.Contains(PropertyName))
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryVisibility(FName GroupId) const
{
	return CollapsedGroups.Contains(GroupId) && !GetGroupSummaryText(GroupId).IsEmpty()
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetLiveValueVisibility(FName PropertyName) const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	const FKawaiiPhysicsWindScopeEditValues* LiveValues = LiveEditValues.Get();
	if (!Values || !Values->bValid || !LiveValues || !LiveValues->bValid)
	{
		return EVisibility::Collapsed;
	}

	FProperty* Property = KawaiiPhysicsWindScopeEditPanelPrivate::FindWindScopeProperty(PropertyName);
	if (KawaiiPhysicsWindScopeEditPanelPrivate::IsEditPanelParameterModeProperty(Property))
	{
		return Values->ParameterMode != LiveValues->ParameterMode
			       ? EVisibility::Visible
			       : EVisibility::Collapsed;
	}
	if (CastField<FBoolProperty>(Property))
	{
		return PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled) &&
			Values->bIsEnabled != LiveValues->bIsEnabled
			       ? EVisibility::Visible
			       : EVisibility::Collapsed;
	}
	if (CastField<FIntProperty>(Property))
	{
		return PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, Seed) &&
			Values->Seed != LiveValues->Seed
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
				KawaiiPhysicsWindScopeEditPanelPrivate::LiveValueTolerance))
			{
				return EVisibility::Visible;
			}
		}
		return EVisibility::Collapsed;
	}
	if (KawaiiPhysicsWindScopeEditPanelPrivate::IsEditPanelFloatIntervalProperty(Property))
	{
		const FFloatInterval* Value = Values->IntervalValues.Find(PropertyName);
		const FFloatInterval* LiveValue = LiveValues->IntervalValues.Find(PropertyName);
		return Value && LiveValue &&
			(!FMath::IsNearlyEqual(Value->Min, LiveValue->Min, KawaiiPhysicsWindScopeEditPanelPrivate::LiveValueTolerance) ||
				!FMath::IsNearlyEqual(Value->Max, LiveValue->Max, KawaiiPhysicsWindScopeEditPanelPrivate::LiveValueTolerance))
			       ? EVisibility::Visible
			       : EVisibility::Collapsed;
	}

	const float* Value = Values->FloatValues.Find(PropertyName);
	const float* LiveValue = LiveValues->FloatValues.Find(PropertyName);
	return Value && LiveValue && !FMath::IsNearlyEqual(*Value, *LiveValue, KawaiiPhysicsWindScopeEditPanelPrivate::LiveValueTolerance)
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

FText SKawaiiPhysicsWindScopeEditPanel::GetLiveValueText(FName PropertyName) const
{
	const FKawaiiPhysicsWindScopeEditValues* LiveValues = LiveEditValues.Get();
	if (!LiveValues || !LiveValues->bValid)
	{
		return FText::GetEmpty();
	}

	FProperty* Property = KawaiiPhysicsWindScopeEditPanelPrivate::FindWindScopeProperty(PropertyName);
	FText ValueText;
	if (KawaiiPhysicsWindScopeEditPanelPrivate::IsEditPanelParameterModeProperty(Property))
	{
		ValueText = KawaiiPhysicsWindScopeEditPanelPrivate::FormatParameterMode(LiveValues->ParameterMode);
	}
	else if (CastField<FBoolProperty>(Property) &&
		PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
	{
		ValueText = LiveValues->bIsEnabled ? LOCTEXT("LiveBoolTrue", "true") : LOCTEXT("LiveBoolFalse", "false");
	}
	else if (CastField<FIntProperty>(Property) &&
		PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, Seed))
	{
		ValueText = FText::AsNumber(LiveValues->Seed);
	}
	else if (CastField<FStructProperty>(Property) &&
		PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
	{
		ValueText = FText::Format(
			LOCTEXT("LiveVectorValueFormat", "({0}, {1}, {2})"),
			KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(static_cast<float>(LiveValues->WindDirection.X)),
			KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(static_cast<float>(LiveValues->WindDirection.Y)),
			KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(static_cast<float>(LiveValues->WindDirection.Z)));
	}
	else if (KawaiiPhysicsWindScopeEditPanelPrivate::IsEditPanelFloatIntervalProperty(Property))
	{
		if (const FFloatInterval* LiveValue = LiveValues->IntervalValues.Find(PropertyName))
		{
			ValueText = FText::Format(
				LOCTEXT("LiveIntervalValueFormat", "{0} - {1}"),
				KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(LiveValue->Min),
				KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(LiveValue->Max));
		}
		else
		{
			return FText::GetEmpty();
		}
	}
	else if (const float* LiveValue = LiveValues->FloatValues.Find(PropertyName))
	{
		ValueText = KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(*LiveValue);
	}
	else
	{
		return FText::GetEmpty();
	}

	return FText::Format(LOCTEXT("LiveValueFormat", "→ {0} (live)"), ValueText);
}

FText SKawaiiPhysicsWindScopeEditPanel::GetGroupSummaryText(FName GroupId) const
{
	const FKawaiiPhysicsWindScopeParamGroup* TargetGroup = nullptr;
	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		if (Group.GroupId == GroupId)
		{
			TargetGroup = &Group;
			break;
		}
	}

	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	if (!TargetGroup || !Values || !Values->bValid)
	{
		return FText::GetEmpty();
	}

	TArray<FText> SummaryTexts;
	for (const FKawaiiPhysicsWindScopeSummaryItem& Item : TargetGroup->SummaryItems)
	{
		if (Item.PropertyName.IsNone() || !IsParamVisibleInCurrentMode(Item.PropertyName))
		{
			continue;
		}

		FText ValueText;
		if (Item.PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
		{
			ValueText = FText::Format(
				LOCTEXT("LiveVectorValueFormat", "({0}, {1}, {2})"),
				KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(static_cast<float>(Values->WindDirection.X)),
				KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(static_cast<float>(Values->WindDirection.Y)),
				KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(static_cast<float>(Values->WindDirection.Z)));
		}
		else if (const FFloatInterval* SummaryInterval = Values->IntervalValues.Find(Item.PropertyName))
		{
			ValueText = FText::Format(
				INVTEXT("{0}-{1}"),
				KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(SummaryInterval->Min),
				KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(SummaryInterval->Max));
		}
		else if (const float* SummaryValue = Values->FloatValues.Find(Item.PropertyName))
		{
			if (Item.bHideWhenZero && FMath::IsNearlyZero(*SummaryValue))
			{
				continue;
			}
			ValueText = KawaiiPhysicsWindScopeEditPanelPrivate::FormatLiveFloat(*SummaryValue);
		}
		else
		{
			continue;
		}

		ValueText = KawaiiPhysicsWindScopeEditPanelPrivate::FormatSummaryValueWithUnit(ValueText, Item.Unit);
		SummaryTexts.Add(
			Item.ShortLabel.IsEmpty()
				? ValueText
				: FText::Format(LOCTEXT("SummaryItemFormat", "{0} {1}"), Item.ShortLabel, ValueText));
	}

	return SummaryTexts.Num() > 0
		       ? FText::Join(LOCTEXT("SummarySeparator", " · "), SummaryTexts)
		       : FText::GetEmpty();
}

FText SKawaiiPhysicsWindScopeEditPanel::GetParameterModeText() const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	return Values && Values->bValid
		       ? KawaiiPhysicsWindScopeEditPanelPrivate::FormatParameterMode(Values->ParameterMode)
		       : FText::GetEmpty();
}

ECheckBoxState SKawaiiPhysicsWindScopeEditPanel::GetBoolCheckState(FName PropertyName) const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	if (!Values || !Values->bValid || PropertyName != GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
	{
		return ECheckBoxState::Undetermined;
	}
	return Values->bIsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

float SKawaiiPhysicsWindScopeEditPanel::GetFloatValue(FName PropertyName) const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
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
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	if (!Values || !Values->bValid || PropertyName != GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, Seed))
	{
		return 0;
	}
	return Values->Seed;
}

TOptional<float> SKawaiiPhysicsWindScopeEditPanel::GetIntervalValue(FName PropertyName, int32 ComponentIndex) const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
	const FFloatInterval* Interval = Values && Values->bValid
		                                 ? Values->IntervalValues.Find(PropertyName)
		                                 : nullptr;
	if (!Interval || ComponentIndex < 0 || ComponentIndex > 1)
	{
		return TOptional<float>();
	}
	return ComponentIndex == 0 ? Interval->Min : Interval->Max;
}

TOptional<FVector::FReal> SKawaiiPhysicsWindScopeEditPanel::GetVectorValue(FName PropertyName, int32 ComponentIndex) const
{
	const FKawaiiPhysicsWindScopeEditValues* Values = EditValues.Get();
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
		OnParamEdit.Execute(PropertyName, 0.0, VectorComponentIndex, EKawaiiPhysicsWindEditPhase::Begin);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleScalarChanged(FName PropertyName, double NewValue) const
{
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, INDEX_NONE, EKawaiiPhysicsWindEditPhase::Interactive);
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
		OnParamEdit.Execute(PropertyName, NewValue, INDEX_NONE, EKawaiiPhysicsWindEditPhase::Committed);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleVectorChanged(
	FName PropertyName,
	int32 ComponentIndex,
	FVector::FReal NewValue) const
{
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, ComponentIndex, EKawaiiPhysicsWindEditPhase::Interactive);
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
		OnParamEdit.Execute(PropertyName, NewValue, ComponentIndex, EKawaiiPhysicsWindEditPhase::Committed);
	}
}

void SKawaiiPhysicsWindScopeEditPanel::HandleBoolChanged(ECheckBoxState NewState, FName PropertyName) const
{
	const double NewValue = NewState == ECheckBoxState::Checked ? 1.0 : 0.0;
	if (OnParamEdit.IsBound())
	{
		OnParamEdit.Execute(PropertyName, NewValue, INDEX_NONE, EKawaiiPhysicsWindEditPhase::Committed);
	}
}

#undef LOCTEXT_NAMESPACE
