// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"

struct FStreamableHandle;
class SDockTab;
class SComboButton;
class SComboBoxBase;
class SSplitter;
class UAnimGraphNode_KawaiiPhysics;

struct FKawaiiPhysicsWindScopeWindowArgs
{
	/** 表示対象のグラフノード / Graph node to inspect. */
	TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics> GraphNode;

	/** ノードを再解決する AnimBlueprint パス / AnimBlueprint path used to re-resolve the node. */
	FSoftObjectPath AnimBlueprintPath;

	/** ノードを再解決する NodeGuid / NodeGuid used to re-resolve the node. */
	FGuid NodeGuid;

	/** 表示対象の ProceduralWind 外力インデックス / ProceduralWind external force index to inspect. */
	int32 ExternalForceIndex = INDEX_NONE;
};

// グラフに表示する波形成分の種別 / Waveform component shown in the graph.
enum class EKawaiiPhysicsWindScopeComponent : uint8
{
	Total,
	Constant,
	Sway,
	Ripple,
	StrengthCycle,
	Random,
	Gust,
};

// 上記各成分の表示ON/OFFを凡例チェックボックスと連動して保持する / Stores per-series visibility controlled by legend checkboxes.
struct FKawaiiPhysicsWindScopeSeriesVisibility
{
	bool bTotal = true;
	bool bConstant = true;
	bool bSway = true;
	bool bRipple = true;
	bool bStrengthCycle = true;
	bool bRandom = true;
	bool bGust = true;

	bool IsVisible(EKawaiiPhysicsWindScopeComponent Component) const;
	void SetVisible(EKawaiiPhysicsWindScopeComponent Component, bool bVisible);
};

// 編集パネルが表示する現在値キャッシュ
struct FKawaiiPhysicsWindScopeEditValues
{
	bool bValid = false;
	bool bIsEnabled = true;
	EKawaiiProceduralWindParameterMode ParameterMode = EKawaiiProceduralWindParameterMode::Simple;
	FVector WindDirection = FVector::ForwardVector;
	TMap<FName, float> FloatValues;
	TMap<FName, FFloatInterval> IntervalValues;
	int32 Seed = 0;
	TSet<FName> ModifiedFromDefault;
};

// 編集操作のフェーズ
enum class EKawaiiPhysicsWindEditPhase : uint8
{
	Begin,
	Interactive,
	Committed,
};

// 波形グラフ本体 / Waveform graph widget.
class SKawaiiPhysicsWindScopeGraph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKawaiiPhysicsWindScopeGraph)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetSamples(TArray<FKawaiiProceduralWindScopeSample> InSamples);
	void SetDisplaySeconds(float InDisplaySeconds);
	void SetSeriesVisibility(const FKawaiiPhysicsWindScopeSeriesVisibility& InVisibility);
	void SetHighlightSeries(TOptional<EKawaiiPhysicsWindScopeComponent> InHighlightSeries);
	void SetActiveEditGuide(TOptional<FName> PropertyName);
	void SetEditValues(const FKawaiiPhysicsWindScopeEditValues* InEditValues);
	void SetGhostSamples(TArray<FVector2D> InGhostSamples);
	void SetPaused(bool bInPaused);

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	// グラフの描画本体 / Slate paint callback for the graph.
	virtual int32 OnPaint(const FPaintArgs& Args,
	                      const FGeometry& AllottedGeometry,
	                      const FSlateRect& MyCullingRect,
	                      FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId,
	                      const FWidgetStyle& InWidgetStyle,
	                      bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TArray<FKawaiiProceduralWindScopeSample> Samples;
	TArray<FVector2D> GhostSamples;
	FKawaiiPhysicsWindScopeSeriesVisibility Visibility;
	TOptional<EKawaiiPhysicsWindScopeComponent> HighlightSeries;
	TOptional<FName> ActiveEditGuide;
	TOptional<FVector2D> HoverMousePosition;
	const FKawaiiPhysicsWindScopeEditValues* EditValues = nullptr;
	float DisplaySeconds = 8.0f;
	bool bPaused = false;
};

// Wind Scope タブ本体 / Wind Scope tab content widget.
class SKawaiiPhysicsWindScopeWindow : public SCompoundWidget
{
	SLATE_DECLARE_WIDGET(SKawaiiPhysicsWindScopeWindow, SCompoundWidget)

public:
	SLATE_BEGIN_ARGS(SKawaiiPhysicsWindScopeWindow)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FKawaiiPhysicsWindScopeWindowArgs InitArgs = FKawaiiPhysicsWindScopeWindowArgs());
	virtual ~SKawaiiPhysicsWindScopeWindow() override;

	static const FName WindScopeTabId;

	/** Wind Scope タブを開くか既存タブを更新する / Opens the Wind Scope tab or updates the existing one. */
	static void OpenWindow(FKawaiiPhysicsWindScopeWindowArgs Args);

	/** 開いている Wind Scope タブをすべて閉じる / Closes all open Wind Scope tabs. */
	static void CloseAllWindows();

	/** 所有 DockTab を弱参照で保持する / Stores the owning DockTab as a weak reference. */
	void SetOwnerTab(TSharedRef<SDockTab> InOwnerTab);

	/** 現在の引数でウィジェット状態を置き換える / Replaces the widget state with the current arguments. */
	void SetArgs(FKawaiiPhysicsWindScopeWindowArgs Args);

	bool HasTargetArgs() const;
	void LoadPendingReconnectFromConfig();

	ECheckBoxState GetSeriesCheckState(EKawaiiPhysicsWindScopeComponent Component) const;
	void OnSeriesCheckStateChanged(ECheckBoxState NewState, EKawaiiPhysicsWindScopeComponent Component);
	void SetHighlightSeries(TOptional<EKawaiiPhysicsWindScopeComponent> InHighlightSeries);
	bool IsSeriesActive(EKawaiiPhysicsWindScopeComponent Component) const;
	FLinearColor ResolveSeriesDisplayColor(EKawaiiPhysicsWindScopeComponent Component) const;

private:
	// ExternalForces 配列のインデックスを保持するコンボボックス項目型 / Combo item type storing an ExternalForces array index.
	using FExternalForceIndexPtr = TSharedPtr<int32>;

	TSharedRef<SWidget> GenerateExternalForceComboWidget(FExternalForceIndexPtr Item) const;
	void OnExternalForceSelectionChanged(FExternalForceIndexPtr Item, ESelectInfo::Type SelectInfo);
	FText GetSelectedExternalForceText() const;
	FText GetTargetNodeText() const;
	FText GetModeText() const;
	ECheckBoxState GetPauseCheckState() const;
	void OnPauseCheckStateChanged(ECheckBoxState NewState);
	float GetDisplaySeconds() const;
	void OnDisplaySecondsChanged(float NewValue);
	bool IsEditPanelExpanded() const;
	EVisibility GetEditPanelVisibility() const;
	const FSlateBrush* GetEditPanelToggleIcon() const;
	FReply OnToggleEditPanelClicked();
	void OnEditPanelSlotResized(float NewFraction);
	FSlateColor GetModeBadgeColor() const;
	EVisibility GetTargetNodeEmptyStateVisibility() const;
	bool IsWindEditable() const;
	const FKawaiiPhysicsWindScopeEditValues* GetEditValues() const;
	const FKawaiiPhysicsWindScopeEditValues* GetLiveEditValues() const;

	void RebuildPresetButtons();
	TSharedRef<SWidget> GeneratePresetMenu();
	TSharedRef<SWidget> GenerateGustMenu();
	FReply OnPresetButtonClicked(int32 PresetIndex);
	void OnPresetButtonHovered(int32 PresetIndex);
	void OnPresetButtonUnhovered();
	void OnPresetMenuOpenChanged(bool bIsOpen);
	FReply ApplyPreset(const FKawaiiProceduralWindPreset& Preset);
	TSharedRef<SWidget> GenerateSavePresetMenu();
	bool CanSaveWindPreset() const;
	FText GetSavePresetToolTipText() const;
	class UKawaiiPhysicsWindPresetDataAsset* ResolveWritablePresetDataAsset(bool bShowNotification) const;
	bool SaveCurrentWindAsPreset(int32 PresetIndex);
	FReply OnCopyWindParametersClicked();
	FReply OnPasteWindParametersClicked();
	FKawaiiPhysics_ExternalForce_ProceduralWind* ResolveEditableWind(
		UAnimGraphNode_KawaiiPhysics*& OutGraphNode,
		int32& OutResolvedIndex,
		bool bShowNotification = true);
	bool PushParamsToLiveRuntime(const FKawaiiProceduralWindDynamicParams& Params);
	bool PushGustToLiveRuntime(float Strength, float RiseTime, float DecayTime);
	bool ApplyWindParamEdit(FName PropertyName, double NewValue, int32 VectorComponentIndex, EKawaiiPhysicsWindEditPhase Phase);
	// 放棄されたドラッグ編集をトランザクションとして確定する / Finalizes an abandoned drag edit as a transaction.
	void FinalizeAbandonedWindDrag();
	bool ResetWindParamToDefault(FName PropertyName);

	// 毎フレームの active timer コールバック / Active timer callback that updates Live or Preview samples.
	EActiveTimerReturnType TickWindScope(double InCurrentTime, float InDeltaTime);
	void RefreshExternalForceItems();
	// Live 実行中でない場合の理論波形を再計算する / Rebuilds theoretical Preview samples when Live data is unavailable.
	void RebuildPreviewSamples(float InDeltaTime, const FKawaiiPhysics_ExternalForce_ProceduralWind* WindSnapshot = nullptr);
	// 実行中の RuntimeState からライブ波形を取得する / Reads Live samples from RuntimeState.
	bool TryUpdateFromLiveRuntime(bool& bOutLiveTargetResolved);
	// Live対象（ProceduralWind + RuntimeState）が現在解決可能かだけを判定する（サンプル取得は行わない）
	// Checks only whether a Live target currently resolves (does not fetch samples).
	bool IsLiveTargetResolved() const;
	// Preview 計算用に ProceduralWind 設定値のコピーを取得する / Gets a ProceduralWind copy for Preview calculation.
	bool TryGetPreviewForceCopy(struct FKawaiiPhysics_ExternalForce_ProceduralWind& OutForce) const;
	void UpdateEditValuesFromWind(const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind);
	// ノードへ書いた直後に表示キャッシュを同期する / Syncs the display cache immediately after writing to the node.
	void SyncEditValuesAfterWrite(const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind);
	void UpdateLiveEditValuesFromRuntime();
	void LoadEditPanelConfig();
	void SaveEditPanelConfig() const;
	// 弱参照、または AnimBlueprintPath+NodeGuid から対象ノードを再解決する / Resolves the target node from weak reference or AnimBlueprintPath+NodeGuid.
	UAnimGraphNode_KawaiiPhysics* ResolveGraphNode() const;
	static bool HasTargetArgs(const FKawaiiPhysicsWindScopeWindowArgs& InArgs);
	static void SaveLastTargetArgs(const FKawaiiPhysicsWindScopeWindowArgs& InArgs);
	void ClearPendingReconnect(bool bCancelAsyncLoad = true);
	void TryResolvePendingReconnect(float InDeltaTime);
	void StartPendingReconnectAsyncLoad();
	void OnPendingReconnectAsyncLoadComplete(FKawaiiPhysicsWindScopeWindowArgs ExpectedArgs);

	FKawaiiPhysicsWindScopeWindowArgs Args;
	FKawaiiPhysicsWindScopeWindowArgs PendingReconnectArgs;
	// GUID再解決済みノードの弱キャッシュ / Weak cache for the node resolved by GUID.
	mutable TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics> ResolvedGraphNodeCache;
	TArray<FExternalForceIndexPtr> ExternalForceItems;
	FExternalForceIndexPtr SelectedExternalForceItem;
	FKawaiiPhysicsWindScopeSeriesVisibility SeriesVisibility;
	// 現在グラフに描画中のサンプル列 / Samples currently drawn in the graph.
	TArray<FKawaiiProceduralWindScopeSample> DisplaySamples;
	FText CurrentModeText;
	float DisplaySeconds = 8.0f;
	FKawaiiPhysicsWindScopeEditValues CachedEditValues;
	FKawaiiPhysicsWindScopeEditValues CachedLiveEditValues;
	FKawaiiPhysics_ExternalForce_ProceduralWind DragStartWind;
	FName DragStartPropertyName;
	float GustStrength;
	float GustRiseTime;
	float GustDecayTime;
	bool bHasDragStartWind = false;
	bool bIsLiveMode = false;
	bool bEditPanelExpanded = false;
	float EditPanelSplitterFraction = 0.4f;
	// Preview モードでの積算経過時間 / Accumulated elapsed time in Preview mode.
	float PreviewTime = 0.0f;
	// 直前に読み取った RuntimeState->ScopeSampleCount / Last RuntimeState->ScopeSampleCount read.
	uint64 LastLiveSampleCount = 0;
	float PendingReconnectElapsedTime = 0.0f;
	bool bPaused = false;
	bool bHasPendingReconnect = false;
	bool bPendingReconnectAsyncLoadStarted = false;
	TSharedPtr<FStreamableHandle> PendingReconnectAsyncLoadHandle;

	TWeakPtr<SDockTab> OwnerTabWeak;
	TSharedPtr<SComboButton> PresetComboButton;
	TSharedPtr<SComboBox<FExternalForceIndexPtr>> ExternalForceComboBox;
	TSharedPtr<SKawaiiPhysicsWindScopeGraph> GraphWidget;
	TSharedPtr<SSplitter> EditPanelSplitter;
	// ボタンindexとの整合を保つプリセットスナップショット / Preset snapshot aligned with button indices.
	TArray<FKawaiiProceduralWindPreset> CachedPresets;
};
