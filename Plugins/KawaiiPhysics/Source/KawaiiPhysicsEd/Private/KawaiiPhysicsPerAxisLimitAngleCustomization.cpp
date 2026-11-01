// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsPerAxisLimitAngleCustomization.h"

#include "AnimNode_KawaiiPhysics.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "KawaiiPhysicsAngleLimit.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsPerAxisLimitAngleCustomization"

namespace KawaiiPhysicsPerAxisLimitAngleCustomizationPrivate
{
	void SetAxisRange(const TSharedRef<IPropertyHandle>& MinHandle, const TSharedRef<IPropertyHandle>& MaxHandle,
	                  const float Min, const float Max, const FText& TransactionText)
	{
		const FScopedTransaction Transaction(TransactionText);
		MinHandle->SetValue(Min);
		MaxHandle->SetValue(Max);
	}
} // namespace KawaiiPhysicsPerAxisLimitAngleCustomizationPrivate

TSharedRef<IPropertyTypeCustomization> FKawaiiPhysicsPerAxisLimitAngleCustomization::MakeInstance()
{
	return MakeShared<FKawaiiPhysicsPerAxisLimitAngleCustomization>();
}

void FKawaiiPhysicsPerAxisLimitAngleCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// PerAxisLimitAngle -> PhysicsSettings -> ノード本体の順にたどって BoneForwardAxis を探す
	if (const TSharedPtr<IPropertyHandle> SettingsHandle = PropertyHandle->GetParentHandle())
	{
		if (const TSharedPtr<IPropertyHandle> NodeHandle = SettingsHandle->GetParentHandle())
		{
			BoneForwardAxisHandle = NodeHandle->GetChildHandle(
				GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneForwardAxis), false);
		}
	}

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HeaderHint", "Min / Max around each local axis of the parent bone"))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		];
}

void FKawaiiPhysicsPerAxisLimitAngleCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const auto AddRow = [&](const FName AxisProperty, const EAxis::Type Axis, const FText& AxisName)
	{
		if (const TSharedPtr<IPropertyHandle> AxisHandle = PropertyHandle->GetChildHandle(AxisProperty))
		{
			AddAxisRow(ChildBuilder, AxisHandle.ToSharedRef(), Axis, AxisName);
		}
	};

	AddRow(GET_MEMBER_NAME_CHECKED(FKawaiiPhysicsPerAxisLimitAngle, X), EAxis::X, LOCTEXT("AxisX", "X"));
	AddRow(GET_MEMBER_NAME_CHECKED(FKawaiiPhysicsPerAxisLimitAngle, Y), EAxis::Y, LOCTEXT("AxisY", "Y"));
	AddRow(GET_MEMBER_NAME_CHECKED(FKawaiiPhysicsPerAxisLimitAngle, Z), EAxis::Z, LOCTEXT("AxisZ", "Z"));
}

void FKawaiiPhysicsPerAxisLimitAngleCustomization::AddAxisRow(
	IDetailChildrenBuilder& ChildBuilder, TSharedRef<IPropertyHandle> AxisHandle,
	const EAxis::Type Axis, const FText& AxisName)
{
	const TSharedPtr<IPropertyHandle> MinHandle =
		AxisHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKawaiiPhysicsAxisAngleRange, Min));
	const TSharedPtr<IPropertyHandle> MaxHandle =
		AxisHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKawaiiPhysicsAxisAngleRange, Max));
	if (!MinHandle.IsValid() || !MaxHandle.IsValid())
	{
		return;
	}

	const TAttribute<bool> IsActive = TAttribute<bool>::CreateLambda([this, Axis]()
	{
		return GetTwistAxis() != Axis;
	});
	const TAttribute<FText> Label = TAttribute<FText>::CreateLambda([this, Axis, AxisName]()
	{
		return GetTwistAxis() == Axis
			       ? FText::Format(LOCTEXT("TwistAxisLabel", "{0} (Twist: unused)"), AxisName)
			       : AxisName;
	});
	const TAttribute<FText> LabelToolTip = TAttribute<FText>::CreateLambda([this, Axis]()
	{
		return GetTwistAxis() == Axis
			       ? LOCTEXT("TwistAxisToolTip",
			                 "This axis matches Bone Forward Axis. KawaiiPhysics never twists bones, so it is ignored.")
			       : LOCTEXT("SwingAxisToolTip",
			                 "Allowed rotation around this local axis of the parent bone. Min and Max of 0 lock the axis.");
	});

	const TSharedRef<IPropertyHandle> MinHandleRef = MinHandle.ToSharedRef();
	const TSharedRef<IPropertyHandle> MaxHandleRef = MaxHandle.ToSharedRef();

	ChildBuilder.AddCustomRow(AxisName)
	            .IsEnabled(IsActive)
	            .NameContent()
		[
			SNew(STextBlock)
			.Text(Label)
			.ToolTipText(LabelToolTip)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(280.0f)
		.MaxDesiredWidth(360.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MinHandleRef->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MaxHandleRef->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("LockAxis", "Lock"))
				.ToolTipText(LOCTEXT("LockAxisToolTip", "Lock this axis (Min and Max to 0)."))
				.OnClicked_Lambda([MinHandleRef, MaxHandleRef]()
				{
					KawaiiPhysicsPerAxisLimitAngleCustomizationPrivate::SetAxisRange(MinHandleRef, MaxHandleRef, 0.0f, 0.0f,
					             LOCTEXT("LockAxisTransaction", "Lock Limit Angle Axis"));
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("FreeAxis", "Free"))
				.ToolTipText(LOCTEXT("FreeAxisToolTip", "Free this axis (Min -180, Max 180)."))
				.OnClicked_Lambda([MinHandleRef, MaxHandleRef]()
				{
					KawaiiPhysicsPerAxisLimitAngleCustomizationPrivate::SetAxisRange(MinHandleRef, MaxHandleRef, -180.0f, 180.0f,
					             LOCTEXT("FreeAxisTransaction", "Free Limit Angle Axis"));
					return FReply::Handled();
				})
			]
		];
}

EAxis::Type FKawaiiPhysicsPerAxisLimitAngleCustomization::GetTwistAxis() const
{
	uint8 ForwardAxisValue = 0;
	if (!BoneForwardAxisHandle.IsValid() ||
		BoneForwardAxisHandle->GetValue(ForwardAxisValue) != FPropertyAccess::Success)
	{
		return EAxis::None;
	}
	return KawaiiPhysics::AngleLimit::GetTwistAxis(static_cast<EBoneForwardAxis>(ForwardAxisValue));
}

#undef LOCTEXT_NAMESPACE
