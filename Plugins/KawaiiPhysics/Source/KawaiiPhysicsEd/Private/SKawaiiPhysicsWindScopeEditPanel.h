// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SKawaiiPhysicsWindScopeWindow.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"

class SExpandableArea;

// Wind Scope 編集パネルの1プロパティ定義
struct FKawaiiPhysicsWindScopeParamDef
{
	FName PropertyName;
	float SliderMin = 0.0f;
	float SliderMax = 10.0f;
	bool bDynamicParamsSupported = true;
	bool bAdvancedOnly = false;
};

// 折りたたみ時サマリーの単位 / Unit suffix for collapsed summaries.
enum class EKawaiiPhysicsWindScopeSummaryUnit : uint8
{
	None,
	Seconds,
	Degrees,
};

// 折りたたみ時サマリーの1項目 / Single item shown in a collapsed summary.
struct FKawaiiPhysicsWindScopeSummaryItem
{
	FName PropertyName;
	FText ShortLabel;
	EKawaiiPhysicsWindScopeSummaryUnit Unit = EKawaiiPhysicsWindScopeSummaryUnit::None;
	bool bHideWhenZero = false;
};

// Wind Scope 編集パネルのグループ定義
struct FKawaiiPhysicsWindScopeParamGroup
{
	FText GroupLabel;
	/** グループ永続化ID / Stable group ID used for persistence. */
	FName GroupId;
	/** 折りたたみ時に表示するサマリー項目 / Summary items shown when collapsed. */
	TArray<FKawaiiPhysicsWindScopeSummaryItem> SummaryItems;
	TOptional<EKawaiiPhysicsWindScopeComponent> LinkedSeries;
	TArray<FKawaiiPhysicsWindScopeParamDef> Params;
	/** 折りたたみカテゴリにせずヘッダー直下に常時表示する / Pinned below the header instead of a collapsible category. */
	bool bPinned = false;
};

const TArray<FKawaiiPhysicsWindScopeParamGroup>& GetWindScopeParamGroups();
FString SerializeWindScopeCollapsedGroups(const TSet<FName>& CollapsedGroups);
TSet<FName> ParseWindScopeCollapsedGroups(const FString& CollapsedGroupsValue);

DECLARE_DELEGATE_RetVal_FourParams(bool, FOnWindParamEdit, FName, double, int32, EKawaiiPhysicsWindEditPhase);
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnWindParamReset, FName);
DECLARE_DELEGATE_OneParam(FOnWindScopeHighlightSeries, TOptional<EKawaiiPhysicsWindScopeComponent>);

// Wind Scope の ProceduralWind パラメータ編集パネル
class SKawaiiPhysicsWindScopeEditPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKawaiiPhysicsWindScopeEditPanel)
		{
		}
		SLATE_ATTRIBUTE(const FKawaiiPhysicsWindScopeEditValues*, EditValues)
		SLATE_ATTRIBUTE(const FKawaiiPhysicsWindScopeEditValues*, LiveEditValues)
		SLATE_EVENT(FOnWindParamEdit, OnParamEdit)
		SLATE_EVENT(FOnWindParamReset, OnParamReset)
		SLATE_EVENT(FOnWindScopeHighlightSeries, OnHighlightSeries)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> MakeGroupWidget(const FKawaiiPhysicsWindScopeParamGroup& Group);
	TSharedRef<SWidget> MakeParamRow(const FKawaiiPhysicsWindScopeParamDef& ParamDef);
	TSharedRef<SWidget> MakeResetButton(FName PropertyName) const;
	TSharedRef<SWidget> MakeHeaderIconButton(const FName IconName, const FText& ToolTipText, FOnClicked OnClicked) const;

	EVisibility GetResetVisibility(FName PropertyName) const;
	EVisibility GetLiveValueVisibility(FName PropertyName) const;
	EVisibility GetParamRowVisibility(FName PropertyName) const;
	EVisibility GetGroupVisibility(FName GroupId) const;
	EVisibility GetGroupModifiedDotVisibility(FName GroupId) const;
	EVisibility GetGroupSummaryVisibility(FName GroupId) const;
	FText GetLiveValueText(FName PropertyName) const;
	FText GetGroupSummaryText(FName GroupId) const;
	FText GetParameterModeText() const;
	ECheckBoxState GetBoolCheckState(FName PropertyName) const;
	float GetFloatValue(FName PropertyName) const;
	int32 GetIntValue(FName PropertyName) const;
	TOptional<float> GetIntervalValue(FName PropertyName, int32 ComponentIndex) const;
	TOptional<FVector::FReal> GetVectorValue(FName PropertyName, int32 ComponentIndex) const;

	bool IsAdvancedMode() const;
	bool IsParamAdvancedOnly(FName PropertyName) const;
	bool IsParamVisibleInCurrentMode(FName PropertyName) const;
	FLinearColor ResolveSeriesDisplayColor(TOptional<EKawaiiPhysicsWindScopeComponent> LinkedSeries) const;

	// ParameterMode コンボの内部選択状態をノードの実値へ合わせる / Syncs the ParameterMode combo's internal selection with the node value.
	void SyncParameterModeComboSelection();

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

	TAttribute<const FKawaiiPhysicsWindScopeEditValues*> EditValues;
	TAttribute<const FKawaiiPhysicsWindScopeEditValues*> LiveEditValues;
	FOnWindParamEdit OnParamEdit;
	FOnWindParamReset OnParamReset;
	FOnWindScopeHighlightSeries OnHighlightSeries;
	TSet<FName> CollapsedGroups;
	TMap<FName, TArray<FName>> GroupPropertyNames;
	TArray<TSharedPtr<SExpandableArea>> GroupAreas;
	TSharedPtr<SComboBox<TSharedPtr<EKawaiiProceduralWindParameterMode>>> ParameterModeComboBox;
	bool bApplyingGroupExpansionBatch = false;
	// SetSelectedItem が誘発する OnSelectionChanged を自前ハンドラで無視するためのガード / Guards against the OnSelectionChanged triggered by SetSelectedItem.
	bool bSyncingParameterModeCombo = false;
};
