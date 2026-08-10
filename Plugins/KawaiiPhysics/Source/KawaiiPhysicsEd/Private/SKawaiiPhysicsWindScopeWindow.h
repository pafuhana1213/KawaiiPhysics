// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"

class SComboBoxBase;
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

	FReply OnBreezePresetClicked();
	FReply OnStrongPresetClicked();
	FReply OnStormPresetClicked();
	FReply ApplyPreset(float SteadyForce,
	                   float OscillationForce,
	                   float OscillationPeriod,
	                   float WaveAmplitude,
	                   float WavePeriod,
	                   float WaveSpatialOffset,
	                   float EnvelopeMin,
	                   float EnvelopeMax,
	                   float EnvelopeFrequency,
	                   float RandomForce,
	                   float RandomPeriod,
	                   float DirectionNoiseAngle,
	                   const FText& PresetName);

	EActiveTimerReturnType TickWindScope(double InCurrentTime, float InDeltaTime);
	void RefreshExternalForceItems();
	void RebuildPreviewSamples(float InDeltaTime);
	bool TryUpdateFromLiveRuntime();
	bool TryGetPreviewForceCopy(struct FKawaiiPhysics_ExternalForce_ProceduralWind& OutForce) const;
	UAnimGraphNode_KawaiiPhysics* ResolveGraphNode() const;

	FKawaiiPhysicsWindScopeWindowArgs Args;
	TArray<FExternalForceIndexPtr> ExternalForceItems;
	FExternalForceIndexPtr SelectedExternalForceItem;
	FKawaiiPhysicsWindScopeSeriesVisibility SeriesVisibility;
	TArray<FKawaiiProceduralWindScopeSample> DisplaySamples;
	FText CurrentModeText;
	float DisplaySeconds = 8.0f;
	float PreviewTime = 0.0f;
	uint64 LastLiveSampleCount = 0;
	bool bPaused = false;

	TSharedPtr<SComboBox<FExternalForceIndexPtr>> ExternalForceComboBox;
	TSharedPtr<SKawaiiPhysicsWindScopeGraph> GraphWidget;
};
