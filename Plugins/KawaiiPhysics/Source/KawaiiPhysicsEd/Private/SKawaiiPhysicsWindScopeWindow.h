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

class SComboBoxBase;
class SWrapBox;
class SWindow;
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

// グラフに表示する波形成分の種別（ランタイム側 FKawaiiPhysicsProceduralWindSample の各フィールドに対応）
enum class EKawaiiPhysicsWindScopeComponent : uint8
{
	Total,
	Steady,
	Oscillation,
	Wave,
	Envelope,
	Random,
	Gust,
};

// 上記各成分の表示ON/OFFを凡例チェックボックスと連動して保持する
struct FKawaiiPhysicsWindScopeSeriesVisibility
{
	bool bTotal = true;
	bool bSteady = true;
	bool bOscillation = true;
	bool bWave = true;
	bool bEnvelope = true;
	bool bRandom = true;
	bool bGust = true;

	bool IsVisible(EKawaiiPhysicsWindScopeComponent Component) const;
	void SetVisible(EKawaiiPhysicsWindScopeComponent Component, bool bVisible);
};

// 波形グラフ本体。SLeafWidget を継承し OnPaint でポリラインを直接描画する
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

	// グラフの描画本体（Slate のペイントコールバック）
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
	FKawaiiPhysicsWindScopeSeriesVisibility Visibility;
	float DisplaySeconds = 8.0f;
};

// Wind Scope ツールウィンドウ本体。ツールバー・凡例・グラフ・プリセットボタンをまとめる SCompoundWidget
class SKawaiiPhysicsWindScopeWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKawaiiPhysicsWindScopeWindow)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FKawaiiPhysicsWindScopeWindowArgs InitArgs);

	/** Wind Scope ウィンドウを開くか既存ウィンドウを更新する / Opens the Wind Scope window or updates the existing one. */
	static void OpenWindow(FKawaiiPhysicsWindScopeWindowArgs Args);

	/** 開いている Wind Scope ウィンドウをすべて閉じる / Closes all open Wind Scope windows. */
	static void CloseAllWindows();

	/** 現在の引数でウィジェット状態を置き換える / Replaces the widget state with the current arguments. */
	void SetArgs(FKawaiiPhysicsWindScopeWindowArgs Args);

	ECheckBoxState GetSeriesCheckState(EKawaiiPhysicsWindScopeComponent Component) const;
	void OnSeriesCheckStateChanged(ECheckBoxState NewState, EKawaiiPhysicsWindScopeComponent Component);

private:
	// ExternalForces 配列のインデックスを保持するコンボボックス項目型（SComboBox は TSharedPtr 項目を要求する）
	using FExternalForceIndexPtr = TSharedPtr<int32>;

	TSharedRef<SWidget> GenerateExternalForceComboWidget(FExternalForceIndexPtr Item) const;
	void OnExternalForceSelectionChanged(FExternalForceIndexPtr Item, ESelectInfo::Type SelectInfo);
	FText GetSelectedExternalForceText() const;
	FText GetTargetNodeText() const;
	FText GetModeText() const;
	FText GetCurrentValuesText() const;
	ECheckBoxState GetPauseCheckState() const;
	void OnPauseCheckStateChanged(ECheckBoxState NewState);
	float GetDisplaySeconds() const;
	void OnDisplaySecondsChanged(float NewValue);

	void RebuildPresetButtons();
	FReply OnPresetButtonClicked(int32 PresetIndex);
	FReply ApplyPreset(const FKawaiiProceduralWindPreset& Preset);

	// 毎フレームの active timer コールバック。Live/Preview いずれかでサンプルを更新し再描画する
	EActiveTimerReturnType TickWindScope(double InCurrentTime, float InDeltaTime);
	void RefreshExternalForceItems();
	// Live 実行中でない場合の理論波形（Preview）を再計算する
	void RebuildPreviewSamples(float InDeltaTime);
	// 実行中の RuntimeState からライブ波形を取得する。取得できなければ false（呼び出し側は Preview へフォールバック）
	bool TryUpdateFromLiveRuntime();
	// Preview 計算用に ProceduralWind 設定値のコピーを取得する
	bool TryGetPreviewForceCopy(struct FKawaiiPhysics_ExternalForce_ProceduralWind& OutForce) const;
	// 弱参照、または AnimBlueprintPath+NodeGuid から対象ノードを再解決する（BP再コンパイル等でポインタが失効しても追従できる）
	UAnimGraphNode_KawaiiPhysics* ResolveGraphNode() const;

	FKawaiiPhysicsWindScopeWindowArgs Args;
	TArray<FExternalForceIndexPtr> ExternalForceItems;
	FExternalForceIndexPtr SelectedExternalForceItem;
	FKawaiiPhysicsWindScopeSeriesVisibility SeriesVisibility;
	// 現在グラフに描画中のサンプル列（Live/Preview 共通）
	TArray<FKawaiiProceduralWindScopeSample> DisplaySamples;
	FText CurrentModeText;
	float DisplaySeconds = 8.0f;
	// Preview モードでの積算経過時間
	float PreviewTime = 0.0f;
	// 直前に読み取った RuntimeState->ScopeSampleCount。差分が無ければ新規サンプル無しと判断する
	uint64 LastLiveSampleCount = 0;
	bool bPaused = false;

	TSharedPtr<SComboBox<FExternalForceIndexPtr>> ExternalForceComboBox;
	TSharedPtr<SKawaiiPhysicsWindScopeGraph> GraphWidget;
	// ボタンindexとの整合を保つプリセットスナップショット
	TArray<FKawaiiProceduralWindPreset> CachedPresets;
	TSharedPtr<SWrapBox> PresetButtonBox;
};
