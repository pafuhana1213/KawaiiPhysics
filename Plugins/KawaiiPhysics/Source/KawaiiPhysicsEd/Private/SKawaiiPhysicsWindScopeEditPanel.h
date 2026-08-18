// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SKawaiiPhysicsWindScopeWindow.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"

class SExpandableArea;

// Wind Scope 編集パネルの1プロパティ定義
struct FKawaiiWindScopeParamDef
{
	FName PropertyName;
	float SliderMin = 0.0f;
	float SliderMax = 10.0f;
	bool bDynamicParamsSupported = true;
	bool bAdvancedOnly = false;
};

// Wind Scope 編集パネルのグループ定義
struct FKawaiiWindScopeParamGroup
{
	FText GroupLabel;
	/** グループ永続化ID / Stable group ID used for persistence. */
	FName GroupId;
	/** 折りたたみ時に表示する代表プロパティ / Representative property shown when collapsed. */
	FName SummaryProperty;
	TOptional<EKawaiiPhysicsWindScopeComponent> LinkedSeries;
	TArray<FKawaiiWindScopeParamDef> Params;
};

const TArray<FKawaiiWindScopeParamGroup>& GetWindScopeParamGroups();
FString SerializeWindScopeCollapsedGroups(const TSet<FName>& CollapsedGroups);
TSet<FName> ParseWindScopeCollapsedGroups(const FString& CollapsedGroupsValue);

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
		SLATE_ATTRIBUTE(const FKawaiiWindScopeEditValues*, LiveEditValues)
		SLATE_EVENT(FOnWindParamEdit, OnParamEdit)
		SLATE_EVENT(FOnWindParamReset, OnParamReset)
		SLATE_EVENT(FIsWindParamPinExposed, IsParamPinExposed)
		SLATE_EVENT(FOnWindScopeHighlightSeries, OnHighlightSeries)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> MakeFormulaHelpButton() const;
	TSharedRef<SWidget> MakeFormulaHelpContent() const;
	TSharedRef<SWidget> MakeGroupWidget(const FKawaiiWindScopeParamGroup& Group);
	TSharedRef<SWidget> MakeParamRow(const FKawaiiWindScopeParamDef& ParamDef);
	TSharedRef<SWidget> MakeResetButton(FName PropertyName) const;
	TSharedRef<SWidget> MakePinWarningIcon(FName PropertyName) const;
	TSharedRef<SWidget> MakeHeaderIconButton(const FName IconName, const FText& ToolTipText, FOnClicked OnClicked) const;

	EVisibility GetResetVisibility(FName PropertyName) const;
	EVisibility GetPinWarningVisibility(FName PropertyName) const;
	EVisibility GetLiveValueVisibility(FName PropertyName) const;
	EVisibility GetParamRowVisibility(FName PropertyName) const;
	EVisibility GetGroupVisibility(FName GroupId) const;
	EVisibility GetGroupModifiedDotVisibility(FName GroupId) const;
	EVisibility GetGroupSummaryVisibility(FName GroupId, FName SummaryProperty) const;
	EVisibility GetSimpleHiddenHintVisibility() const;
	FText GetSimpleHiddenHintText() const;
	FText GetLiveValueText(FName PropertyName) const;
	FText GetGroupSummaryText(FName SummaryProperty) const;
	FText GetParameterModeText() const;
	ECheckBoxState GetBoolCheckState(FName PropertyName) const;
	float GetFloatValue(FName PropertyName) const;
	int32 GetIntValue(FName PropertyName) const;
	TOptional<float> GetIntervalValue(FName PropertyName, int32 ComponentIndex) const;
	TOptional<FVector::FReal> GetVectorValue(FName PropertyName, int32 ComponentIndex) const;

	bool IsAdvancedMode() const;
	bool IsParamAdvancedOnly(FName PropertyName) const;
	bool IsParamVisibleInCurrentMode(FName PropertyName) const;
	int32 CountHiddenAdvancedParams() const;
	FLinearColor ResolveSeriesDisplayColor(TOptional<EKawaiiPhysicsWindScopeComponent> LinkedSeries) const;

	void LoadCollapsedGroupsFromConfig();
	void SaveCollapsedGroupsToConfig() const;
	void HandleGroupExpansionChanged(bool bExpanded, FName GroupId);
	FReply OnExpandAllClicked();
	FReply OnCollapseAllClicked();

	void HandleBegin(FName PropertyName, int32 VectorComponentIndex) const;
	void HandleScalarChanged(FName PropertyName, double NewValue) const;
	void HandleScalarCommitted(FName PropertyName, double NewValue, ETextCommit::Type CommitType) const;
	void HandleVectorChanged(FName PropertyName, int32 ComponentIndex, FVector::FReal NewValue) const;
	void HandleVectorCommitted(FName PropertyName, int32 ComponentIndex, FVector::FReal NewValue, ETextCommit::Type CommitType) const;
	void HandleBoolChanged(ECheckBoxState NewState, FName PropertyName) const;

	TAttribute<const FKawaiiWindScopeEditValues*> EditValues;
	TAttribute<const FKawaiiWindScopeEditValues*> LiveEditValues;
	FOnWindParamEdit OnParamEdit;
	FOnWindParamReset OnParamReset;
	FIsWindParamPinExposed IsParamPinExposed;
	FOnWindScopeHighlightSeries OnHighlightSeries;
	TSet<FName> CollapsedGroups;
	TMap<FName, TArray<FName>> GroupPropertyNames;
	TArray<TSharedPtr<SExpandableArea>> GroupAreas;
	bool bApplyingGroupExpansionBatch = false;
};
