// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SKawaiiPhysicsWindScopeWindow.h"
#include "Widgets/SCompoundWidget.h"

// Wind Scope 編集パネルの1プロパティ定義
struct FKawaiiWindScopeParamDef
{
	FName PropertyName;
	float SliderMin = 0.0f;
	float SliderMax = 10.0f;
	bool bDynamicParamsSupported = true;
};

// Wind Scope 編集パネルのグループ定義
struct FKawaiiWindScopeParamGroup
{
	FText GroupLabel;
	TOptional<EKawaiiPhysicsWindScopeComponent> LinkedSeries;
	TArray<FKawaiiWindScopeParamDef> Params;
};

const TArray<FKawaiiWindScopeParamGroup>& GetWindScopeParamGroups();

DECLARE_DELEGATE_RetVal_FourParams(bool, FOnWindParamEdit, FName, double, int32, EKawaiiWindEditPhase);
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnWindParamReset, FName);
DECLARE_DELEGATE_RetVal_OneParam(bool, FIsWindParamPinExposed, FName);
DECLARE_DELEGATE_OneParam(FOnWindScopeHighlightSeries, TOptional<EKawaiiPhysicsWindScopeComponent>);

// Wind Scope の ProceduralWind パラメータ編集パネル
class SKawaiiPhysicsWindScopeEditPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKawaiiPhysicsWindScopeEditPanel)
		{
		}
		SLATE_ATTRIBUTE(const FKawaiiWindScopeEditValues*, EditValues)
		SLATE_EVENT(FOnWindParamEdit, OnParamEdit)
		SLATE_EVENT(FOnWindParamReset, OnParamReset)
		SLATE_EVENT(FIsWindParamPinExposed, IsParamPinExposed)
		SLATE_EVENT(FSimpleDelegate, OnFocusNode)
		SLATE_EVENT(FOnWindScopeHighlightSeries, OnHighlightSeries)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> MakeGroupWidget(const FKawaiiWindScopeParamGroup& Group);
	TSharedRef<SWidget> MakeParamRow(const FKawaiiWindScopeParamDef& ParamDef);
	TSharedRef<SWidget> MakeCurveRow() const;
	TSharedRef<SWidget> MakeResetButton(FName PropertyName) const;
	TSharedRef<SWidget> MakePinWarningIcon(FName PropertyName) const;

	EVisibility GetResetVisibility(FName PropertyName) const;
	EVisibility GetPinWarningVisibility(FName PropertyName) const;
	ECheckBoxState GetBoolCheckState(FName PropertyName) const;
	float GetFloatValue(FName PropertyName) const;
	int32 GetIntValue(FName PropertyName) const;
	TOptional<FVector::FReal> GetVectorValue(FName PropertyName, int32 ComponentIndex) const;

	void HandleBegin(FName PropertyName, int32 VectorComponentIndex) const;
	void HandleScalarChanged(FName PropertyName, double NewValue) const;
	void HandleScalarCommitted(FName PropertyName, double NewValue, ETextCommit::Type CommitType) const;
	void HandleVectorChanged(FName PropertyName, int32 ComponentIndex, FVector::FReal NewValue) const;
	void HandleVectorCommitted(FName PropertyName, int32 ComponentIndex, FVector::FReal NewValue, ETextCommit::Type CommitType) const;
	void HandleBoolChanged(ECheckBoxState NewState, FName PropertyName) const;

	TAttribute<const FKawaiiWindScopeEditValues*> EditValues;
	FOnWindParamEdit OnParamEdit;
	FOnWindParamReset OnParamReset;
	FIsWindParamPinExposed IsParamPinExposed;
	FSimpleDelegate OnFocusNode;
	FOnWindScopeHighlightSeries OnHighlightSeries;
};
