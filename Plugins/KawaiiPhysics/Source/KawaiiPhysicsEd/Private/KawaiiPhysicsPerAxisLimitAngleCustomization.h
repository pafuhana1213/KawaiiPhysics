// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * FKawaiiPhysicsPerAxisLimitAngle の詳細パネル表示。軸ごとに Min/Max を1行で並べ、BoneForwardAxis と同じ軸はツイストとして無効にする
 * Details panel layout for FKawaiiPhysicsPerAxisLimitAngle. Shows Min/Max on one row per axis and disables the axis matching BoneForwardAxis as the twist axis.
 */
class FKawaiiPhysicsPerAxisLimitAngleCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	                             IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	                               IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	void AddAxisRow(IDetailChildrenBuilder& ChildBuilder, TSharedRef<IPropertyHandle> AxisHandle,
	                EAxis::Type Axis, const FText& AxisName);

	// ノード外（Blueprint 変数など）では BoneForwardAxis が無いため EAxis::None を返し、全軸を有効にする
	EAxis::Type GetTwistAxis() const;

	TSharedPtr<IPropertyHandle> BoneForwardAxisHandle;
};
