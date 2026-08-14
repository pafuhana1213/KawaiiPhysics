// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "SKawaiiPhysicsWindScopeEditPanel.h"

#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsWindScopeStyle.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsWindScopeEditPanel"

namespace
{
	constexpr float LabelColumnWidth = 150.0f;

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
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled), 0.0f, 1.0f, true},
			}
		},
		{
			LOCTEXT("DirectionGroupLabel", "Direction"),
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection), -1.0f, 1.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, DirectionNoiseAngle), 0.0f, 90.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, DirectionNoisePeriod), 0.01f, 10.0f, true},
			}
		},
		{
			LOCTEXT("SteadyGroupLabel", "Steady"),
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Steady),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SteadyForce), 0.0f, 50.0f, true},
			}
		},
		{
			LOCTEXT("OscillationGroupLabel", "Oscillation"),
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Oscillation),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, OscillationForce), 0.0f, 50.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, OscillationPeriod), 0.01f, 10.0f, true},
			}
		},
		{
			LOCTEXT("WaveGroupLabel", "Wave"),
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
			TOptional<EKawaiiPhysicsWindScopeComponent>(EKawaiiPhysicsWindScopeComponent::Random),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce), 0.0f, 50.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomPeriod), 0.01f, 5.0f, true},
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomSeed), 0.0f, 10000.0f, false},
			}
		},
		{
			LOCTEXT("TimeGroupLabel", "Time"),
			TOptional<EKawaiiPhysicsWindScopeComponent>(),
			{
				{GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, TimeScale), 0.0f, 3.0f, true},
			}
		},
	};
	return Groups;
}

void SKawaiiPhysicsWindScopeEditPanel::Construct(const FArguments& InArgs)
{
	EditValues = InArgs._EditValues;
	OnParamEdit = InArgs._OnParamEdit;
	OnParamReset = InArgs._OnParamReset;
	OnFocusNode = InArgs._OnFocusNode;
	OnHighlightSeries = InArgs._OnHighlightSeries;

	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);
	for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
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

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeGroupWidget(const FKawaiiWindScopeParamGroup& Group)
{
	const FLinearColor GroupColor = ResolveGroupColor(Group.LinkedSeries);

	TSharedRef<SVerticalBox> GroupBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
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
			]
		];

	for (const FKawaiiWindScopeParamDef& Param : Group.Params)
	{
		GroupBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 0.0f)
		[
			MakeParamRow(Param)
		];
	}

	if (Group.Params.Num() > 0 &&
		Group.Params[0].PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
	{
		GroupBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 0.0f)
		[
			MakeCurveRow()
		];
	}

	return GroupBox;
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
				SNew(STextBlock)
				.Text(Label)
				.ToolTipText(ToolTipText)
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
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
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeResetButton(ParamDef.PropertyName)
		];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeEditPanel::MakeCurveRow() const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CurveEditGuide", "カーブ(ForceRateByBoneLengthRate)は詳細パネルで編集 / Edit the curve in the Details panel"))
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
		.ToolTipText(LOCTEXT("ResetToDefaultTooltip", "既定値へ戻します / Reset to default."))
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

EVisibility SKawaiiPhysicsWindScopeEditPanel::GetResetVisibility(FName PropertyName) const
{
	const FKawaiiWindScopeEditValues* Values = EditValues.Get();
	return Values && Values->ModifiedFromDefault.Contains(PropertyName)
		       ? EVisibility::Visible
		       : EVisibility::Hidden;
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
