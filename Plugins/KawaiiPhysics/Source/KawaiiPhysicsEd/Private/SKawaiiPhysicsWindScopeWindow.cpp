// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "SKawaiiPhysicsWindScopeWindow.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "HAL/CriticalSection.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsEdStyle.h"
#include "KawaiiPhysicsEdUtils.h"
#include "KawaiiPhysicsEdWindowUtils.h"
#include "SKawaiiPhysicsWindScopeEditPanel.h"
#include "KawaiiPhysicsWindScopeStyle.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "UObject/UnrealType.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsWindScopeWindow"

namespace
{
	constexpr int32 WindScopePreviewSampleCount = 240;
	constexpr float WindScopeGraphPaddingLeft = 42.0f;
	constexpr float WindScopeGraphPaddingTop = 12.0f;
	constexpr float WindScopeGraphPaddingRight = 12.0f;
	constexpr float WindScopeGraphPaddingBottom = 24.0f;
	constexpr float WindScopeGustStrength = 6.0f;
	constexpr float WindScopeGustRiseTime = 0.1f;
	constexpr float WindScopeGustDecayTime = 0.5f;
	constexpr float WindScopeReconnectTryLoadDelay = 5.0f;
	const TCHAR* WindScopeConfigSectionName = TEXT("KawaiiPhysicsEd");
	const TCHAR* WindScopeLastAnimBlueprintKey = TEXT("WindScopeLastAnimBlueprint");
	const TCHAR* WindScopeLastNodeGuidKey = TEXT("WindScopeLastNodeGuid");
	const TCHAR* WindScopeLastForceIndexKey = TEXT("WindScopeLastForceIndex");
	const TCHAR* WindScopeEditPanelExpandedKey = TEXT("WindScopeEditPanelExpanded");
	const TCHAR* WindScopeEditPanelSplitterFractionKey = TEXT("WindScopeEditPanelSplitterFraction");

	TWeakPtr<SDockTab> KawaiiWindScopeTabWeak;
	TWeakPtr<SKawaiiPhysicsWindScopeWindow> KawaiiWindScopeWidgetWeak;

	bool AreReconnectArgsSame(const FKawaiiPhysicsWindScopeWindowArgs& Lhs, const FKawaiiPhysicsWindScopeWindowArgs& Rhs)
	{
		return Lhs.AnimBlueprintPath == Rhs.AnimBlueprintPath &&
			Lhs.NodeGuid == Rhs.NodeGuid &&
			Lhs.ExternalForceIndex == Rhs.ExternalForceIndex;
	}

	UAnimGraphNode_KawaiiPhysics* FindLoadedGraphNodeByGuid(UObject* AnimBlueprintObject, const FGuid& NodeGuid)
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AnimBlueprintObject);
		if (!AnimBlueprint || !NodeGuid.IsValid())
		{
			return nullptr;
		}

		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UAnimGraphNode_KawaiiPhysics* KawaiiPhysicsGraphNode = Cast<UAnimGraphNode_KawaiiPhysics>(Node);
				if (KawaiiPhysicsGraphNode && KawaiiPhysicsGraphNode->NodeGuid == NodeGuid)
				{
					return KawaiiPhysicsGraphNode;
				}
			}
		}

		return nullptr;
	}

	struct FKawaiiWindScopeRange
	{
		float MinTime = 0.0f;
		float MaxTime = 1.0f;
		float MinValue = -1.0f;
		float MaxValue = 1.0f;
	};

	// FKawaiiPhysicsProceduralWindSample から指定成分の値を取り出す
	float GetWindScopeComponentValue(const FKawaiiPhysicsProceduralWindSample& Sample,
	                                 EKawaiiPhysicsWindScopeComponent Component)
	{
		switch (Component)
		{
		case EKawaiiPhysicsWindScopeComponent::Total:
			return Sample.Total;
		case EKawaiiPhysicsWindScopeComponent::Steady:
			return Sample.Steady;
		case EKawaiiPhysicsWindScopeComponent::Oscillation:
			return Sample.Oscillation;
		case EKawaiiPhysicsWindScopeComponent::Wave:
			return Sample.Wave;
		case EKawaiiPhysicsWindScopeComponent::Envelope:
			return Sample.Envelope;
		case EKawaiiPhysicsWindScopeComponent::Random:
			return Sample.Random;
		case EKawaiiPhysicsWindScopeComponent::Gust:
			return Sample.Gust;
		default:
			return 0.0f;
		}
	}

	bool IsProceduralWindStruct(const FInstancedStruct& InstancedStruct)
	{
		return InstancedStruct.IsValid() &&
			InstancedStruct.GetScriptStruct() == FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct();
	}

	FProperty* FindProceduralWindProperty(const FName PropertyName)
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

	bool IsFVectorProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == TBaseStructure<FVector>::Get();
	}

	bool SetProceduralWindPropertyValue(
		FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
		const FProperty* Property,
		double NewValue,
		int32 VectorComponentIndex)
	{
		if (!Property)
		{
			return false;
		}

		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			BoolProperty->SetPropertyValue_InContainer(&Wind, NewValue != 0.0);
			return true;
		}

		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			FloatProperty->SetPropertyValue_InContainer(&Wind, static_cast<float>(NewValue));
			return true;
		}

		if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			IntProperty->SetPropertyValue_InContainer(&Wind, FMath::RoundToInt32(NewValue));
			return true;
		}

		if (IsFVectorProperty(Property) && VectorComponentIndex >= 0 && VectorComponentIndex <= 2)
		{
			FVector* Vector = Property->ContainerPtrToValuePtr<FVector>(&Wind);
			(*Vector)[VectorComponentIndex] = static_cast<FVector::FReal>(NewValue);
			return true;
		}

		return false;
	}

	bool CopyProceduralWindPropertyValue(
		FKawaiiPhysics_ExternalForce_ProceduralWind& TargetWind,
		const FKawaiiPhysics_ExternalForce_ProceduralWind& SourceWind,
		const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		void* TargetValue = Property->ContainerPtrToValuePtr<void>(&TargetWind);
		const void* SourceValue = Property->ContainerPtrToValuePtr<void>(&SourceWind);
		Property->CopyCompleteValue(TargetValue, SourceValue);
		return true;
	}

	bool IsProceduralWindPropertyModifiedFromDefault(
		const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
		const FKawaiiPhysics_ExternalForce_ProceduralWind& DefaultWind,
		const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const void* Value = Property->ContainerPtrToValuePtr<void>(&Wind);
		const void* DefaultValue = Property->ContainerPtrToValuePtr<void>(&DefaultWind);
		return !Property->Identical(Value, DefaultValue);
	}

	bool IsProceduralWindPropertyValueEqualToEdit(
		const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
		const FProperty* Property,
		double NewValue,
		int32 VectorComponentIndex)
	{
		if (!Property)
		{
			return false;
		}

		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			return BoolProperty->GetPropertyValue_InContainer(&Wind) == (NewValue != 0.0);
		}

		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			return FMath::IsNearlyEqual(FloatProperty->GetPropertyValue_InContainer(&Wind), static_cast<float>(NewValue));
		}

		if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			return IntProperty->GetPropertyValue_InContainer(&Wind) == FMath::RoundToInt32(NewValue);
		}

		if (IsFVectorProperty(Property) && VectorComponentIndex >= 0 && VectorComponentIndex <= 2)
		{
			const FVector* Vector = Property->ContainerPtrToValuePtr<FVector>(&Wind);
			FVector EditedVector = *Vector;
			EditedVector[VectorComponentIndex] = static_cast<FVector::FReal>(NewValue);
			return Vector->Equals(EditedVector);
		}

		return false;
	}

	int32 FindFirstProceduralWindIndex(const FAnimNode_KawaiiPhysics& Node)
	{
		for (int32 Index = 0; Index < Node.ExternalForces.Num(); ++Index)
		{
			if (IsProceduralWindStruct(Node.ExternalForces[Index]))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// 要求indexが無効（削除済み・型変更済み等）なら、先頭の ProceduralWind へフォールバックする
	int32 ResolveProceduralWindIndex(const FAnimNode_KawaiiPhysics& Node, int32 RequestedIndex)
	{
		if (Node.ExternalForces.IsValidIndex(RequestedIndex) &&
			IsProceduralWindStruct(Node.ExternalForces[RequestedIndex]))
		{
			return RequestedIndex;
		}
		return FindFirstProceduralWindIndex(Node);
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind* ResolveLiveProceduralWind(
		UAnimGraphNode_KawaiiPhysics* GraphNode,
		FAnimNode_KawaiiPhysics* RuntimeNode,
		const int32 RequestedIndex)
	{
		if (!GraphNode || !RuntimeNode ||
			!KawaiiPhysicsEdUtils::IsExternalForceShapeMatched(GraphNode->Node.ExternalForces, RuntimeNode->ExternalForces))
		{
			return nullptr;
		}

		if (!GraphNode->Node.ExternalForces.IsValidIndex(RequestedIndex) ||
			!IsProceduralWindStruct(GraphNode->Node.ExternalForces[RequestedIndex]) ||
			!RuntimeNode->ExternalForces.IsValidIndex(RequestedIndex) ||
			!IsProceduralWindStruct(RuntimeNode->ExternalForces[RequestedIndex]))
		{
			return nullptr;
		}

		return RuntimeNode->ExternalForces[RequestedIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	}

	TArray<FKawaiiProceduralWindPreset> ResolveWindScopePresets()
	{
		if (const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>())
		{
			if (const UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset =
				Settings->WindScopePresetDataAsset.LoadSynchronous())
			{
				if (PresetDataAsset->Presets.Num() > 0)
				{
					return PresetDataAsset->Presets;
				}
			}
		}

		return UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	}

	FText ResolveWindPresetDisplayName(const FKawaiiProceduralWindPreset& Preset, int32 PresetIndex)
	{
		if (!Preset.PresetName.IsEmpty())
		{
			return Preset.PresetName;
		}

		if (Preset.PresetTag.IsValid())
		{
			const FString TagString = Preset.PresetTag.GetTagName().ToString();
			FString ParentName;
			FString LeafName;
			if (TagString.Split(TEXT("."), &ParentName, &LeafName, ESearchCase::CaseSensitive, ESearchDir::FromEnd) &&
				!LeafName.IsEmpty())
			{
				return FText::FromString(LeafName);
			}
			if (!TagString.IsEmpty())
			{
				return FText::FromString(TagString);
			}
		}

		return FText::Format(LOCTEXT("WindPresetFallbackNameFormat", "Preset {0}"), FText::AsNumber(PresetIndex));
	}

	// サンプルが無い時の初期表示値。Envelope=1にして Total=0（無風）を表すサンプルにする
	FKawaiiPhysicsProceduralWindSample MakeZeroWindSample()
	{
		FKawaiiPhysicsProceduralWindSample Sample;
		Sample.Envelope = 1.0f;
		return Sample;
	}

	FText FormatFloat2(float Value)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 2;
		Options.MaximumFractionalDigits = 2;
		return FText::AsNumber(Value, &Options);
	}

	FKawaiiWindScopeRange ComputeWindScopeRange(const TArray<FKawaiiProceduralWindScopeSample>& Samples,
	                                           float DisplaySeconds,
	                                           const FKawaiiPhysicsWindScopeSeriesVisibility& Visibility)
	{
		FKawaiiWindScopeRange Range;
		// 直近 DisplaySeconds 秒分を表示ウィンドウとする
		Range.MaxTime = Samples.Num() > 0 ? Samples.Last().Time : 0.0f;
		Range.MinTime = Range.MaxTime - FMath::Max(DisplaySeconds, 0.1f);
		bool bHasValue = false;

		// 表示ウィンドウ内かつ可視な系列の値だけを対象に最小/最大を求める
		for (const FKawaiiProceduralWindScopeSample& Point : Samples)
		{
			if (Point.Time < Range.MinTime)
			{
				continue;
			}

			for (const FKawaiiWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
			{
				if (!Visibility.IsVisible(Style.Component))
				{
					continue;
				}

				const float Value = GetWindScopeComponentValue(Point.Sample, Style.Component);
				if (!bHasValue)
				{
					Range.MinValue = Value;
					Range.MaxValue = Value;
					bHasValue = true;
				}
				else
				{
					Range.MinValue = FMath::Min(Range.MinValue, Value);
					Range.MaxValue = FMath::Max(Range.MaxValue, Value);
				}
			}
		}

		// 表示対象が無い場合はデフォルトの [-1,1] 範囲にフォールバック
		if (!bHasValue)
		{
			Range.MinValue = -1.0f;
			Range.MaxValue = 1.0f;
		}

		// 上下に少し余白を持たせる
		const float Span = Range.MaxValue - Range.MinValue;
		const float Padding = FMath::Max(Span * 0.1f, 0.01f);
		Range.MinValue -= Padding;
		Range.MaxValue += Padding;
		return Range;
	}

	// 契約: Slate 型に依存せず、時刻順サンプル列をグラフローカル座標のポリラインへ変換する。
	// 入力の時刻・値範囲は呼び出し側が確定し、範囲外の時刻サンプルはここで捨てる。
	static TArray<FVector2D> BuildWindScopePolylinePoints(
		const TArray<FKawaiiProceduralWindScopeSample>& Samples,
		EKawaiiPhysicsWindScopeComponent Component,
		float MinTime,
		float MaxTime,
		float MinValue,
		float MaxValue,
		const FVector2D& GraphOrigin,
		const FVector2D& GraphSize)
	{
		TArray<FVector2D> Points;
		const float TimeSpan = FMath::Max(MaxTime - MinTime, KINDA_SMALL_NUMBER);
		const float ValueSpan = FMath::Max(MaxValue - MinValue, KINDA_SMALL_NUMBER);

		for (const FKawaiiProceduralWindScopeSample& SamplePoint : Samples)
		{
			if (SamplePoint.Time < MinTime || SamplePoint.Time > MaxTime)
			{
				continue;
			}

			const float XRate = (SamplePoint.Time - MinTime) / TimeSpan;
			const float Value = GetWindScopeComponentValue(SamplePoint.Sample, Component);
			const float YRate = (Value - MinValue) / ValueSpan;
			// Slate のローカル座標はY下向きのため、値が大きいほどYが小さくなるよう反転する
			Points.Add(FVector2D(
				GraphOrigin.X + GraphSize.X * XRate,
				GraphOrigin.Y + GraphSize.Y * (1.0f - YRate)));
		}
		return Points;
	}

	// セグメントごとに Dash/Gap を繰り返して破線を描画する
	void DrawWindScopeDashedLine(FSlateWindowElementList& OutDrawElements,
	                             int32 LayerId,
	                             const FGeometry& AllottedGeometry,
	                             const TArray<FVector2D>& Points,
	                             const FLinearColor& Color,
	                             float Thickness)
	{
		constexpr float DashLength = 6.0f;
		constexpr float GapLength = 4.0f;
		for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
		{
			const FVector2D Start = Points[PointIndex - 1];
			const FVector2D End = Points[PointIndex];
			const FVector2D Segment = End - Start;
			const float SegmentLength = Segment.Size();
			if (SegmentLength <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Direction = Segment / SegmentLength;
			for (float Offset = 0.0f; Offset < SegmentLength; Offset += DashLength + GapLength)
			{
				const float DashEndOffset = FMath::Min(Offset + DashLength, SegmentLength);
				TArray<FVector2D> DashPoints;
				DashPoints.Add(Start + Direction * Offset);
				DashPoints.Add(Start + Direction * DashEndOffset);
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(),
					DashPoints,
					ESlateDrawEffect::None,
					Color,
					true,
					Thickness);
			}
		}
	}

	class SKawaiiWindScopeLegendHoverBorder : public SBorder
	{
	public:
		SLATE_BEGIN_ARGS(SKawaiiWindScopeLegendHoverBorder)
			{
			}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_EVENT(FSimpleDelegate, OnHovered)
			SLATE_EVENT(FSimpleDelegate, OnUnhovered)
			SLATE_ARGUMENT(const FSlateBrush*, BorderImage)
			SLATE_ATTRIBUTE(FMargin, Padding)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnHovered = InArgs._OnHovered;
			OnUnhovered = InArgs._OnUnhovered;
			SBorder::Construct(SBorder::FArguments()
				.BorderImage(InArgs._BorderImage)
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

	// 凡例1項目（色スウォッチ＋表示切替チェックボックス＋ラベル）を生成する
	TSharedRef<SWidget> MakeWindScopeLegendItem(SKawaiiPhysicsWindScopeWindow* Owner,
	                                            const FKawaiiWindScopeComponentStyle& Style)
	{
		return SNew(SKawaiiWindScopeLegendHoverBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBrush")))
			.Padding(FMargin(0.0f))
			.OnHovered_Lambda([Owner, Component = Style.Component]()
			{
				Owner->SetHighlightSeries(Component);
			})
			.OnUnhovered_Lambda([Owner]()
			{
				Owner->SetHighlightSeries(TOptional<EKawaiiPhysicsWindScopeComponent>());
			})
			[
				SNew(SCheckBox)
				.IsChecked(Owner, &SKawaiiPhysicsWindScopeWindow::GetSeriesCheckState, Style.Component)
				.OnCheckStateChanged(Owner, &SKawaiiPhysicsWindScopeWindow::OnSeriesCheckStateChanged, Style.Component)
				.ToolTipText(Style.Label)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SColorBlock)
						.Color(Style.Color)
						.Size(FVector2D(12.0f, 8.0f))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Style.Label)
						.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
					]
				]
			];
	}

	// 所属 AnimBlueprint を変更済みとしてマークする（プリセット適用の Undo/Redo・保存ダーティ化用）
	void MarkWindScopeGraphNodeModified(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		if (!GraphNode)
		{
			return;
		}

		if (UAnimBlueprint* AnimBlueprint = GraphNode->GetAnimBlueprint())
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		}
	}
}

const FName SKawaiiPhysicsWindScopeWindow::WindScopeTabId(TEXT("KawaiiPhysicsWindScope"));

bool FKawaiiPhysicsWindScopeSeriesVisibility::IsVisible(EKawaiiPhysicsWindScopeComponent Component) const
{
	switch (Component)
	{
	case EKawaiiPhysicsWindScopeComponent::Total:
		return bTotal;
	case EKawaiiPhysicsWindScopeComponent::Steady:
		return bSteady;
	case EKawaiiPhysicsWindScopeComponent::Oscillation:
		return bOscillation;
	case EKawaiiPhysicsWindScopeComponent::Wave:
		return bWave;
	case EKawaiiPhysicsWindScopeComponent::Envelope:
		return bEnvelope;
	case EKawaiiPhysicsWindScopeComponent::Random:
		return bRandom;
	case EKawaiiPhysicsWindScopeComponent::Gust:
		return bGust;
	default:
		return false;
	}
}

void FKawaiiPhysicsWindScopeSeriesVisibility::SetVisible(
	EKawaiiPhysicsWindScopeComponent Component,
	bool bVisible)
{
	switch (Component)
	{
	case EKawaiiPhysicsWindScopeComponent::Total:
		bTotal = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Steady:
		bSteady = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Oscillation:
		bOscillation = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Wave:
		bWave = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Envelope:
		bEnvelope = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Random:
		bRandom = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Gust:
		bGust = bVisible;
		break;
	default:
		break;
	}
}

void SKawaiiPhysicsWindScopeGraph::Construct(const FArguments& InArgs)
{
	(void)InArgs;
}

void SKawaiiPhysicsWindScopeGraph::SetSamples(TArray<FKawaiiProceduralWindScopeSample> InSamples)
{
	Samples = MoveTemp(InSamples);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKawaiiPhysicsWindScopeGraph::SetDisplaySeconds(float InDisplaySeconds)
{
	DisplaySeconds = FMath::Clamp(InDisplaySeconds, 2.0f, 30.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKawaiiPhysicsWindScopeGraph::SetSeriesVisibility(const FKawaiiPhysicsWindScopeSeriesVisibility& InVisibility)
{
	Visibility = InVisibility;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKawaiiPhysicsWindScopeGraph::SetHighlightSeries(TOptional<EKawaiiPhysicsWindScopeComponent> InHighlightSeries)
{
	HighlightSeries = InHighlightSeries;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SKawaiiPhysicsWindScopeGraph::OnPaint(const FPaintArgs& Args,
                                            const FGeometry& AllottedGeometry,
                                            const FSlateRect& MyCullingRect,
                                            FSlateWindowElementList& OutDrawElements,
                                            int32 LayerId,
                                            const FWidgetStyle& InWidgetStyle,
                                            bool bParentEnabled) const
{
	(void)Args;
	(void)MyCullingRect;
	(void)InWidgetStyle;
	(void)bParentEnabled;

	// 背景を塗る
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush,
		ESlateDrawEffect::None,
		FLinearColor(0.015f, 0.018f, 0.022f, 1.0f));

	// 描画領域（余白を除いたグラフ内側の矩形）と値レンジを計算
	const FVector2D GraphOrigin(WindScopeGraphPaddingLeft, WindScopeGraphPaddingTop);
	const FVector2D GraphSize(
		FMath::Max(LocalSize.X - WindScopeGraphPaddingLeft - WindScopeGraphPaddingRight, 1.0f),
		FMath::Max(LocalSize.Y - WindScopeGraphPaddingTop - WindScopeGraphPaddingBottom, 1.0f));
	const FKawaiiWindScopeRange Range = ComputeWindScopeRange(Samples, DisplaySeconds, Visibility);
	const FLinearColor GridColor(0.22f, 0.24f, 0.28f, 0.55f);
	const FLinearColor AxisColor(0.52f, 0.56f, 0.62f, 0.9f);
	const FSlateFontInfo AxisFont(FCoreStyle::GetDefaultFont(), 9);

	// グリッド線（縦横4分割）を描画
	for (int32 GridIndex = 0; GridIndex <= 4; ++GridIndex)
	{
		const float X = GraphOrigin.X + GraphSize.X * (static_cast<float>(GridIndex) / 4.0f);
		TArray<FVector2D> VerticalLine;
		VerticalLine.Add(FVector2D(X, GraphOrigin.Y));
		VerticalLine.Add(FVector2D(X, GraphOrigin.Y + GraphSize.Y));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			VerticalLine,
			ESlateDrawEffect::None,
			GridColor,
			true,
			1.0f);

		const float Y = GraphOrigin.Y + GraphSize.Y * (static_cast<float>(GridIndex) / 4.0f);
		TArray<FVector2D> HorizontalLine;
		HorizontalLine.Add(FVector2D(GraphOrigin.X, Y));
		HorizontalLine.Add(FVector2D(GraphOrigin.X + GraphSize.X, Y));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			HorizontalLine,
			ESlateDrawEffect::None,
			GridColor,
			true,
			1.0f);
	}

	// 0ライン（値レンジが0を跨ぐ場合のみ）を強調表示
	if (Range.MinValue <= 0.0f && Range.MaxValue >= 0.0f)
	{
		const float ZeroY = GraphOrigin.Y + GraphSize.Y * (1.0f - ((0.0f - Range.MinValue) / (Range.MaxValue - Range.MinValue)));
		TArray<FVector2D> ZeroLine;
		ZeroLine.Add(FVector2D(GraphOrigin.X, ZeroY));
		ZeroLine.Add(FVector2D(GraphOrigin.X + GraphSize.X, ZeroY));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(),
			ZeroLine,
			ESlateDrawEffect::None,
			AxisColor,
			true,
			1.0f);
	}

	// 各波形成分のポリラインを描画（Envelope等 bDashed 指定の系列は破線）
	for (const FKawaiiWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
	{
		if (!Visibility.IsVisible(Style.Component))
		{
			continue;
		}

		const TArray<FVector2D> Points = BuildWindScopePolylinePoints(
			Samples,
			Style.Component,
			Range.MinTime,
			Range.MaxTime,
			Range.MinValue,
			Range.MaxValue,
			GraphOrigin,
			GraphSize);
		if (Points.Num() < 2)
		{
			continue;
		}

		FLinearColor DrawColor = Style.Color;
		float DrawThickness = Style.Thickness;
		if (HighlightSeries.IsSet() && Style.Component != EKawaiiPhysicsWindScopeComponent::Total)
		{
			if (Style.Component == HighlightSeries.GetValue())
			{
				DrawThickness += 1.0f;
			}
			else
			{
				DrawColor.A *= 0.25f;
			}
		}

		if (Style.bDashed)
		{
			DrawWindScopeDashedLine(OutDrawElements, LayerId + 3, AllottedGeometry, Points, DrawColor, DrawThickness);
		}
		else
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 3,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				DrawColor,
				true,
				DrawThickness);
		}
	}

	// 軸ラベル（最大値・0・最小値・時間軸）を描画
	const auto DrawAxisText = [&](const FString& Text, const FVector2D& Position)
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2D(80.0f, 14.0f), FSlateLayoutTransform(Position)),
			Text,
			AxisFont,
			ESlateDrawEffect::None,
			AxisColor);
	};

	DrawAxisText(FString::Printf(TEXT("%.2f"), Range.MaxValue), FVector2D(2.0f, GraphOrigin.Y - 2.0f));
	if (Range.MinValue <= 0.0f && Range.MaxValue >= 0.0f)
	{
		const float ZeroLabelY = GraphOrigin.Y + GraphSize.Y * (1.0f - ((0.0f - Range.MinValue) / (Range.MaxValue - Range.MinValue)));
		DrawAxisText(TEXT("0.00"), FVector2D(2.0f, ZeroLabelY - 7.0f));
	}
	DrawAxisText(FString::Printf(TEXT("%.2f"), Range.MinValue), FVector2D(2.0f, GraphOrigin.Y + GraphSize.Y - 14.0f));
	DrawAxisText(FString::Printf(TEXT("-%.0fs"), DisplaySeconds), FVector2D(GraphOrigin.X, GraphOrigin.Y + GraphSize.Y + 4.0f));
	DrawAxisText(TEXT("0s"), FVector2D(GraphOrigin.X + GraphSize.X - 24.0f, GraphOrigin.Y + GraphSize.Y + 4.0f));

	return LayerId + 5;
}

FVector2D SKawaiiPhysicsWindScopeGraph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	(void)LayoutScaleMultiplier;
	return FVector2D(520.0f, 220.0f);
}

void SKawaiiPhysicsWindScopeWindow::Construct(
	const FArguments& InArgs,
	FKawaiiPhysicsWindScopeWindowArgs InitArgs)
{
	(void)InArgs;
	CurrentModeText = LOCTEXT("InitialMode", "Preview");
	LoadEditPanelConfig();
	if (HasTargetArgs(InitArgs))
	{
		SetArgs(MoveTemp(InitArgs));
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		// 上段: 対象ノード名・外力選択コンボ・現在モード表示
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ContentPadding(FMargin(3.0f))
				.ToolTipText(LOCTEXT("ToggleEditPanelTooltip", "編集パネルの表示切替 / Toggle the parameter edit panel."))
				.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnToggleEditPanelClicked)
				[
					SNew(SImage)
					.Image(this, &SKawaiiPhysicsWindScopeWindow::GetEditPanelToggleIcon)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SKawaiiPhysicsWindScopeWindow::GetTargetNodeText)
				.Clipping(EWidgetClipping::OnDemand)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExternalForceLabel", "Force"))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(ExternalForceComboBox, SComboBox<FExternalForceIndexPtr>)
				.OptionsSource(&ExternalForceItems)
				.InitiallySelectedItem(SelectedExternalForceItem)
				.OnGenerateWidget(this, &SKawaiiPhysicsWindScopeWindow::GenerateExternalForceComboWidget)
				.OnSelectionChanged(this, &SKawaiiPhysicsWindScopeWindow::OnExternalForceSelectionChanged)
				[
					SNew(STextBlock)
					.Text(this, &SKawaiiPhysicsWindScopeWindow::GetSelectedExternalForceText)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SKawaiiPhysicsWindScopeWindow::GetModeText)
				.ColorAndOpacity(FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentGreen")))
			]
		]
		// プリセットボタン列
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(PresetButtonBox, SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4.0f, 2.0f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GustButton", "Gust"))
				.ContentPadding(FMargin(6.0f, 2.0f))
				.ToolTipText(LOCTEXT("GustButtonTooltip", "実行中のライブ対象へテスト突風を送ります / Sends a test gust to the live target."))
				.OnClicked_Lambda([this]()
				{
					const bool bAppliedLive = PushGustToLiveRuntime(
						WindScopeGustStrength,
						WindScopeGustRiseTime,
						WindScopeGustDecayTime);
					KawaiiPhysicsEdWindowUtils::ShowNotification(
						bAppliedLive
							? LOCTEXT("GustSucceededLive", "Triggered wind gust. (live)")
							: LOCTEXT("GustNoLiveTarget", "Skipped wind gust. (no live target)"),
						bAppliedLive ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ReloadPresetsButton", "Reload"))
				.ContentPadding(FMargin(6.0f, 2.0f))
				.ToolTipText(LOCTEXT("ReloadPresetsTooltip", "プリセットDataAssetを再読み込みします / Reload the preset DataAsset."))
				.OnClicked_Lambda([this]()
				{
					RebuildPresetButtons();
					return FReply::Handled();
				})
			]
		]
		// 編集パネル・凡例・波形グラフ本体
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 2.0f, 8.0f, 4.0f)
		[
			SAssignNew(EditPanelSplitter, SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot()
			.Value_Lambda([this]()
			{
				return EditPanelSplitterFraction;
			})
			.MinSize(220.0f)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SKawaiiPhysicsWindScopeWindow::OnEditPanelSlotResized))
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBrush")))
				.Visibility(this, &SKawaiiPhysicsWindScopeWindow::GetEditPanelVisibility)
				.IsEnabled(this, &SKawaiiPhysicsWindScopeWindow::IsWindEditable)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SKawaiiPhysicsWindScopeEditPanel)
					.EditValues(this, &SKawaiiPhysicsWindScopeWindow::GetEditValues)
					.OnParamEdit(this, &SKawaiiPhysicsWindScopeWindow::ApplyWindParamEdit)
					.OnParamReset(this, &SKawaiiPhysicsWindScopeWindow::ResetWindParamToDefault)
					.OnFocusNode(FSimpleDelegate::CreateSP(this, &SKawaiiPhysicsWindScopeWindow::OnFocusWindScopeNodeClicked))
					.OnHighlightSeries(this, &SKawaiiPhysicsWindScopeWindow::SetHighlightSeries)
				]
			]
			+ SSplitter::Slot()
			.Value_Lambda([this]()
			{
				return 1.0f - EditPanelSplitterFraction;
			})
			.MinSize(260.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(6.0f, 2.0f))
					+ SWrapBox::Slot()[MakeWindScopeLegendItem(this, GetWindScopeComponentStyles()[0])]
					+ SWrapBox::Slot()[MakeWindScopeLegendItem(this, GetWindScopeComponentStyles()[1])]
					+ SWrapBox::Slot()[MakeWindScopeLegendItem(this, GetWindScopeComponentStyles()[2])]
					+ SWrapBox::Slot()[MakeWindScopeLegendItem(this, GetWindScopeComponentStyles()[3])]
					+ SWrapBox::Slot()[MakeWindScopeLegendItem(this, GetWindScopeComponentStyles()[4])]
					+ SWrapBox::Slot()[MakeWindScopeLegendItem(this, GetWindScopeComponentStyles()[5])]
					+ SWrapBox::Slot()[MakeWindScopeLegendItem(this, GetWindScopeComponentStyles()[6])]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(GraphWidget, SKawaiiPhysicsWindScopeGraph)
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 4.0f)
		[
			SNew(SSeparator)
		]
		// 下段: 現在値テキスト・表示秒数スピンボックス・一時停止チェックボックス
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f, 8.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SKawaiiPhysicsWindScopeWindow::GetCurrentValuesText)
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
				.Clipping(EWidgetClipping::OnDemand)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DisplaySecondsLabel", "Seconds"))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(2.0f)
				.MaxValue(30.0f)
				.MinSliderValue(2.0f)
				.MaxSliderValue(30.0f)
				.Value(this, &SKawaiiPhysicsWindScopeWindow::GetDisplaySeconds)
				.OnValueChanged(this, &SKawaiiPhysicsWindScopeWindow::OnDisplaySecondsChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SKawaiiPhysicsWindScopeWindow::GetPauseCheckState)
				.OnCheckStateChanged(this, &SKawaiiPhysicsWindScopeWindow::OnPauseCheckStateChanged)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PauseLabel", "Pause"))
				]
			]
		]
	];

	if (HasTargetArgs())
	{
		RebuildPresetButtons();
	}

	// 60fps 相当でティックし、Live/Preview のサンプル更新とグラフ再描画を行う
	RegisterActiveTimer(1.0f / 60.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SKawaiiPhysicsWindScopeWindow::TickWindScope));
}

SKawaiiPhysicsWindScopeWindow::~SKawaiiPhysicsWindScopeWindow()
{
	SaveEditPanelConfig();
	ClearPendingReconnect();
}

void SKawaiiPhysicsWindScopeWindow::RegisterTabSpawner(const TSharedRef<FWorkspaceItem>& InMenuGroup)
{
	const FSlateIcon KawaiiPhysicsIcon(
		FKawaiiPhysicsEdStyle::GetStyleSetName(),
		TEXT("KawaiiPhysics.TabIcon"));
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			WindScopeTabId,
			FOnSpawnTab::CreateStatic(&SKawaiiPhysicsWindScopeWindow::SpawnWindScopeTab))
		.SetDisplayName(LOCTEXT("WindScopeMenuDisplayName", "Kawaii Physics: Wind Scope"))
		.SetTooltipText(LOCTEXT("WindScopeMenuTooltip", "KawaiiPhysics の風プレビュータブを開きます / Opens the KawaiiPhysics wind preview tab."))
		.SetGroup(InMenuGroup)
		.SetIcon(KawaiiPhysicsIcon);
}

void SKawaiiPhysicsWindScopeWindow::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WindScopeTabId);
}

TSharedRef<SDockTab> SKawaiiPhysicsWindScopeWindow::SpawnWindScopeTab(const FSpawnTabArgs& SpawnTabArgs)
{
	(void)SpawnTabArgs;

	TSharedRef<SKawaiiPhysicsWindScopeWindow> ScopeWidget = SNew(SKawaiiPhysicsWindScopeWindow);
	if (!ScopeWidget->HasTargetArgs())
	{
		ScopeWidget->LoadPendingReconnectFromConfig();
	}

	TSharedRef<SDockTab> ScopeTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("WindScopeTabLabel", "Kawaii Wind Scope"))
		.OnTabClosed_Lambda([](TSharedRef<SDockTab> ClosedTab)
		{
			(void)ClosedTab;
			KawaiiWindScopeTabWeak.Reset();
			KawaiiWindScopeWidgetWeak.Reset();
		})
		[
			ScopeWidget
		];

	KawaiiWindScopeTabWeak = ScopeTab;
	KawaiiWindScopeWidgetWeak = ScopeWidget;
	return ScopeTab;
}

void SKawaiiPhysicsWindScopeWindow::OpenWindow(FKawaiiPhysicsWindScopeWindowArgs Args)
{
	if (HasTargetArgs(Args))
	{
		SaveLastTargetArgs(Args);
	}

	// タブを呼び出してから、選択ノード由来の引数を既存コンテンツへ注入する
	TSharedPtr<SDockTab> InvokedTab = FGlobalTabmanager::Get()->TryInvokeTab(WindScopeTabId);
	if (!InvokedTab.IsValid())
	{
		return;
	}

	TSharedPtr<SWidget> TabContent = InvokedTab->GetContent();
	if (!TabContent.IsValid())
	{
		return;
	}

	if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> ExistingWidget = KawaiiWindScopeWidgetWeak.Pin())
	{
		ExistingWidget->ClearPendingReconnect();
		ExistingWidget->SetArgs(MoveTemp(Args));
		return;
	}

	// Hot Reload等でファイルスコープの弱参照だけが失効した場合、タブ内容から復旧する
	if (TabContent->GetType() == FName(TEXT("SKawaiiPhysicsWindScopeWindow")))
	{
		TSharedPtr<SKawaiiPhysicsWindScopeWindow> RecoveredWidget =
			StaticCastSharedPtr<SKawaiiPhysicsWindScopeWindow>(TabContent);
		KawaiiWindScopeTabWeak = InvokedTab;
		KawaiiWindScopeWidgetWeak = RecoveredWidget;
		RecoveredWidget->ClearPendingReconnect();
		RecoveredWidget->SetArgs(MoveTemp(Args));
	}
}

void SKawaiiPhysicsWindScopeWindow::CloseAllWindows()
{
	if (TSharedPtr<SDockTab> ScopeTab = KawaiiWindScopeTabWeak.Pin())
	{
		ScopeTab->RequestCloseTab();
	}
	KawaiiWindScopeTabWeak.Reset();
	KawaiiWindScopeWidgetWeak.Reset();
}

void SKawaiiPhysicsWindScopeWindow::SetArgs(FKawaiiPhysicsWindScopeWindowArgs InArgs)
{
	// 対象引数を差し替え、表示状態をリセットして外力一覧を再構築する
	ResolvedGraphNodeCache.Reset();
	Args = MoveTemp(InArgs);
	DisplaySamples.Reset();
	PreviewTime = 0.0f;
	LastLiveSampleCount = 0;
	bHasDragStartWind = false;
	CurrentModeText = LOCTEXT("PreviewMode", "Preview");
	RefreshExternalForceItems();
	if (HasTargetArgs())
	{
		RebuildPresetButtons();
	}
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeWindow::GenerateExternalForceComboWidget(FExternalForceIndexPtr Item) const
{
	return SNew(STextBlock)
		.Text(Item.IsValid()
			      ? FText::Format(LOCTEXT("ExternalForceItemFormat", "#{0} ProceduralWind"), FText::AsNumber(*Item))
			      : LOCTEXT("NoExternalForceItem", "None"));
}

void SKawaiiPhysicsWindScopeWindow::OnExternalForceSelectionChanged(
	FExternalForceIndexPtr Item,
	ESelectInfo::Type SelectInfo)
{
	(void)SelectInfo;
	SelectedExternalForceItem = Item;
	Args.ExternalForceIndex = Item.IsValid() ? *Item : INDEX_NONE;
	// 選択切替時は別の外力の波形を混在させないよう表示状態を丸ごとリセットする
	DisplaySamples.Reset();
	LastLiveSampleCount = 0;
	PreviewTime = 0.0f;
	bHasDragStartWind = false;
}

FText SKawaiiPhysicsWindScopeWindow::GetSelectedExternalForceText() const
{
	return SelectedExternalForceItem.IsValid()
		       ? FText::Format(LOCTEXT("SelectedExternalForceFormat", "#{0} ProceduralWind"), FText::AsNumber(*SelectedExternalForceItem))
		       : LOCTEXT("NoExternalForceSelected", "No ProceduralWind");
}

FText SKawaiiPhysicsWindScopeWindow::GetTargetNodeText() const
{
	if (!HasTargetArgs())
	{
		return LOCTEXT("NoTargetNodeGuidance", "ノード未選択: KawaiiPhysics ノードの [Wind Scope] から開いてください / No node selected: open from [Wind Scope] on a KawaiiPhysics node.");
	}

	if (const UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode())
	{
		return GraphNode->GetNodeTitle(ENodeTitleType::ListView);
	}
	return LOCTEXT("UnknownTargetNode", "KawaiiPhysics Node");
}

FText SKawaiiPhysicsWindScopeWindow::GetModeText() const
{
	return CurrentModeText;
}

FText SKawaiiPhysicsWindScopeWindow::GetCurrentValuesText() const
{
	const FKawaiiPhysicsProceduralWindSample Sample =
		DisplaySamples.Num() > 0 ? DisplaySamples.Last().Sample : MakeZeroWindSample();
	return FText::Format(
		LOCTEXT("CurrentValuesFormat", "Total {0}  Steady {1}  Osc {2}  Wave {3}  Env {4}  Rand {5}  Gust {6}"),
		FormatFloat2(Sample.Total),
		FormatFloat2(Sample.Steady),
		FormatFloat2(Sample.Oscillation),
		FormatFloat2(Sample.Wave),
		FormatFloat2(Sample.Envelope),
		FormatFloat2(Sample.Random),
		FormatFloat2(Sample.Gust));
}

ECheckBoxState SKawaiiPhysicsWindScopeWindow::GetSeriesCheckState(
	EKawaiiPhysicsWindScopeComponent Component) const
{
	return SeriesVisibility.IsVisible(Component) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SKawaiiPhysicsWindScopeWindow::OnSeriesCheckStateChanged(
	ECheckBoxState NewState,
	EKawaiiPhysicsWindScopeComponent Component)
{
	SeriesVisibility.SetVisible(Component, NewState == ECheckBoxState::Checked);
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetSeriesVisibility(SeriesVisibility);
	}
}

ECheckBoxState SKawaiiPhysicsWindScopeWindow::GetPauseCheckState() const
{
	return bPaused ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SKawaiiPhysicsWindScopeWindow::OnPauseCheckStateChanged(ECheckBoxState NewState)
{
	bPaused = NewState == ECheckBoxState::Checked;
}

float SKawaiiPhysicsWindScopeWindow::GetDisplaySeconds() const
{
	return DisplaySeconds;
}

void SKawaiiPhysicsWindScopeWindow::OnDisplaySecondsChanged(float NewValue)
{
	DisplaySeconds = FMath::Clamp(NewValue, 2.0f, 30.0f);
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetDisplaySeconds(DisplaySeconds);
	}
}

bool SKawaiiPhysicsWindScopeWindow::IsEditPanelExpanded() const
{
	return bEditPanelExpanded;
}

EVisibility SKawaiiPhysicsWindScopeWindow::GetEditPanelVisibility() const
{
	return IsEditPanelExpanded() ? EVisibility::Visible : EVisibility::Collapsed;
}

const FSlateBrush* SKawaiiPhysicsWindScopeWindow::GetEditPanelToggleIcon() const
{
	return FAppStyle::Get().GetBrush(IsEditPanelExpanded() ? TEXT("Icons.ChevronLeft") : TEXT("Icons.ChevronRight"));
}

FReply SKawaiiPhysicsWindScopeWindow::OnToggleEditPanelClicked()
{
	bEditPanelExpanded = !bEditPanelExpanded;
	SaveEditPanelConfig();
	return FReply::Handled();
}

void SKawaiiPhysicsWindScopeWindow::OnEditPanelSlotResized(float NewFraction)
{
	EditPanelSplitterFraction = FMath::Clamp(NewFraction, 0.2f, 0.8f);
}

bool SKawaiiPhysicsWindScopeWindow::IsWindEditable() const
{
	return ResolveGraphNode() != nullptr;
}

const FKawaiiWindScopeEditValues* SKawaiiPhysicsWindScopeWindow::GetEditValues() const
{
	return &CachedEditValues;
}

void SKawaiiPhysicsWindScopeWindow::SetHighlightSeries(TOptional<EKawaiiPhysicsWindScopeComponent> InHighlightSeries)
{
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetHighlightSeries(InHighlightSeries);
	}
}

void SKawaiiPhysicsWindScopeWindow::RebuildPresetButtons()
{
	CachedPresets = ResolveWindScopePresets();
	if (!PresetButtonBox.IsValid())
	{
		return;
	}

	PresetButtonBox->ClearChildren();
	for (int32 PresetIndex = 0; PresetIndex < CachedPresets.Num(); ++PresetIndex)
	{
		PresetButtonBox->AddSlot()
		[
			SNew(SButton)
			.Text(ResolveWindPresetDisplayName(CachedPresets[PresetIndex], PresetIndex))
			.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnPresetButtonClicked, PresetIndex)
		];
	}
}

FReply SKawaiiPhysicsWindScopeWindow::OnPresetButtonClicked(int32 PresetIndex)
{
	if (!CachedPresets.IsValidIndex(PresetIndex))
	{
		return FReply::Handled();
	}

	return ApplyPreset(CachedPresets[PresetIndex]);
}

FReply SKawaiiPhysicsWindScopeWindow::ApplyPreset(const FKawaiiProceduralWindPreset& Preset)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(GraphNode, ResolvedIndex);
	if (!Wind)
	{
		return FReply::Handled();
	}

	// Undo/Redo 対応のトランザクションでパラメータを書き込む
	const FScopedTransaction Transaction(LOCTEXT("ApplyWindPresetTransaction", "Apply Kawaii Physics Wind Preset"));
	GraphNode->Modify();
	FKawaiiProceduralWindDynamicParams Params = Preset.ToDynamicParams();
	Params.bOverrideIsEnabled = true;
	Params.bIsEnabled = true;
	Params.bOverrideTimeScale = true;
	Params.TimeScale = 1.0f;
	Wind->ApplyDynamicParams(Params);
	MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	// シミュレーションリセット回避のため PostEditChangeProperty / NotifyGraphNodePropertyChanged は呼ばず、
	// ライブ側には PendingParams 経由で同じ値を送る
	const bool bAppliedLive = PushParamsToLiveRuntime(Params);

	// 外力一覧を更新し、成功通知を表示
	RefreshExternalForceItems();
	const int32 PresetIndex = CachedPresets.IndexOfByPredicate([&Preset](const FKawaiiProceduralWindPreset& CachedPreset)
	{
		return &CachedPreset == &Preset;
	});
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		FText::Format(bAppliedLive
			              ? LOCTEXT("ApplyPresetSucceededLive", "Applied {0} wind preset. (live)")
			              : LOCTEXT("ApplyPresetSucceededNodeOnly", "Applied {0} wind preset. (node only — no live target)"),
			ResolveWindPresetDisplayName(Preset, PresetIndex)),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

FKawaiiPhysics_ExternalForce_ProceduralWind* SKawaiiPhysicsWindScopeWindow::ResolveEditableWind(
	UAnimGraphNode_KawaiiPhysics*& OutGraphNode,
	int32& OutResolvedIndex,
	bool bShowNotification)
{
	OutGraphNode = nullptr;
	OutResolvedIndex = INDEX_NONE;

	// 対象グラフノードを解決（失敗時は通知して終了）
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	if (!GraphNode)
	{
		if (bShowNotification)
		{
			KawaiiPhysicsEdWindowUtils::ShowNotification(
				LOCTEXT("ApplyPresetNoNode", "Failed to resolve the KawaiiPhysics graph node."),
				SNotificationItem::CS_Fail);
		}
		return nullptr;
	}

	// ProceduralWind 外力を解決・型チェック
	const int32 ResolvedIndex = ResolveProceduralWindIndex(GraphNode->Node, Args.ExternalForceIndex);
	if (!GraphNode->Node.ExternalForces.IsValidIndex(ResolvedIndex))
	{
		if (bShowNotification)
		{
			KawaiiPhysicsEdWindowUtils::ShowNotification(
				LOCTEXT("ApplyPresetNoWind", "No ProceduralWind external force was found."),
				SNotificationItem::CS_Fail);
		}
		return nullptr;
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
		GraphNode->Node.ExternalForces[ResolvedIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	if (!Wind)
	{
		if (bShowNotification)
		{
			KawaiiPhysicsEdWindowUtils::ShowNotification(
				LOCTEXT("ApplyPresetInvalidWind", "The selected external force is not ProceduralWind."),
				SNotificationItem::CS_Fail);
		}
		return nullptr;
	}

	OutGraphNode = GraphNode;
	OutResolvedIndex = ResolvedIndex;
	return Wind;
}

bool SKawaiiPhysicsWindScopeWindow::PushParamsToLiveRuntime(
	const FKawaiiProceduralWindDynamicParams& Params)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	FAnimNode_KawaiiPhysics* RuntimeNode = KawaiiPhysicsEdUtils::ResolveLiveKawaiiPhysicsNode(GraphNode);
	FKawaiiPhysics_ExternalForce_ProceduralWind* RuntimeWind = ResolveLiveProceduralWind(
		GraphNode,
		RuntimeNode,
		Args.ExternalForceIndex);
	if (!RuntimeWind)
	{
		return false;
	}

	RuntimeWind->RequestDynamicParams(Params);
	return true;
}

bool SKawaiiPhysicsWindScopeWindow::PushGustToLiveRuntime(
	const float Strength, const float RiseTime, const float DecayTime)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	FAnimNode_KawaiiPhysics* RuntimeNode = KawaiiPhysicsEdUtils::ResolveLiveKawaiiPhysicsNode(GraphNode);
	FKawaiiPhysics_ExternalForce_ProceduralWind* RuntimeWind = ResolveLiveProceduralWind(
		GraphNode,
		RuntimeNode,
		Args.ExternalForceIndex);
	if (!RuntimeWind)
	{
		return false;
	}

	RuntimeWind->RequestGust(Strength, RiseTime, DecayTime);
	return true;
}

bool SKawaiiPhysicsWindScopeWindow::ApplyWindParamEdit(
	FName PropertyName,
	double NewValue,
	int32 VectorComponentIndex,
	EKawaiiWindEditPhase Phase)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(
		GraphNode,
		ResolvedIndex,
		Phase == EKawaiiWindEditPhase::Committed);
	if (!Wind)
	{
		return false;
	}
	Args.ExternalForceIndex = ResolvedIndex;

	FProperty* Property = FindProceduralWindProperty(PropertyName);
	if (!Property)
	{
		if (Phase == EKawaiiWindEditPhase::Committed)
		{
			KawaiiPhysicsEdWindowUtils::ShowNotification(
				LOCTEXT("EditWindParamPropertyMissing", "Failed to resolve the wind parameter property."),
				SNotificationItem::CS_Fail);
		}
		return false;
	}

	if (Phase == EKawaiiWindEditPhase::Begin)
	{
		DragStartWind = *Wind;
		DragStartPropertyName = PropertyName;
		bHasDragStartWind = true;
		return true;
	}

	if (Phase == EKawaiiWindEditPhase::Interactive)
	{
		if (!SetProceduralWindPropertyValue(*Wind, Property, NewValue, VectorComponentIndex))
		{
			return false;
		}

		FKawaiiProceduralWindDynamicParams Params;
		if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
		{
			PushParamsToLiveRuntime(Params);
		}
		return true;
	}

	const bool bUseDragStartValue = bHasDragStartWind && DragStartPropertyName == PropertyName;
	const FKawaiiPhysics_ExternalForce_ProceduralWind& CompareWind = bUseDragStartValue ? DragStartWind : *Wind;
	if (IsProceduralWindPropertyValueEqualToEdit(CompareWind, Property, NewValue, VectorComponentIndex))
	{
		if (bUseDragStartValue)
		{
			CopyProceduralWindPropertyValue(*Wind, DragStartWind, Property);
			FKawaiiProceduralWindDynamicParams Params;
			if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
			{
				PushParamsToLiveRuntime(Params);
			}
		}
		bHasDragStartWind = false;
		return true;
	}

	if (bUseDragStartValue)
	{
		CopyProceduralWindPropertyValue(*Wind, DragStartWind, Property);
	}

	const FScopedTransaction Transaction(LOCTEXT("EditWindParameterTransaction", "Edit Kawaii Physics Wind Parameter"));
	GraphNode->Modify();
	if (!SetProceduralWindPropertyValue(*Wind, Property, NewValue, VectorComponentIndex))
	{
		bHasDragStartWind = false;
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("EditWindParamSetFailed", "Failed to update the wind parameter."),
			SNotificationItem::CS_Fail);
		return false;
	}

	MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	bHasDragStartWind = false;

	FKawaiiProceduralWindDynamicParams Params;
	if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
	{
		PushParamsToLiveRuntime(Params);
	}
	return true;
}

bool SKawaiiPhysicsWindScopeWindow::ResetWindParamToDefault(FName PropertyName)
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(GraphNode, ResolvedIndex);
	if (!Wind)
	{
		return false;
	}

	FProperty* Property = FindProceduralWindProperty(PropertyName);
	if (!Property)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ResetWindParamPropertyMissing", "Failed to resolve the wind parameter property."),
			SNotificationItem::CS_Fail);
		return false;
	}

	static const FKawaiiPhysics_ExternalForce_ProceduralWind DefaultWind;
	const FScopedTransaction Transaction(LOCTEXT("EditWindParameterTransaction", "Edit Kawaii Physics Wind Parameter"));
	GraphNode->Modify();
	CopyProceduralWindPropertyValue(*Wind, DefaultWind, Property);
	MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	bHasDragStartWind = false;

	FKawaiiProceduralWindDynamicParams Params;
	if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
	{
		PushParamsToLiveRuntime(Params);
	}
	return true;
}

void SKawaiiPhysicsWindScopeWindow::OnFocusWindScopeNodeClicked()
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	if (!GraphNode)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("FocusWindScopeNodeFailed", "Failed to resolve the KawaiiPhysics graph node."),
			SNotificationItem::CS_Fail);
		return;
	}

	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(GraphNode);
}

EActiveTimerReturnType SKawaiiPhysicsWindScopeWindow::TickWindScope(double InCurrentTime, float InDeltaTime)
{
	(void)InCurrentTime;
	TryResolvePendingReconnect(InDeltaTime);

	FKawaiiPhysics_ExternalForce_ProceduralWind WindSnapshot;
	bool bHasWindSnapshot = false;
	if (bEditPanelExpanded)
	{
		bHasWindSnapshot = TryGetPreviewForceCopy(WindSnapshot);
		UpdateEditValuesFromWind(bHasWindSnapshot ? &WindSnapshot : nullptr);
	}

	if (bPaused)
	{
		return EActiveTimerReturnType::Continue;
	}

	// Live 取得に失敗したら（PIE/デバッグ対象なし等）Preview 波形を計算する
	if (!TryUpdateFromLiveRuntime())
	{
		if (!bHasWindSnapshot)
		{
			bHasWindSnapshot = TryGetPreviewForceCopy(WindSnapshot);
		}
		RebuildPreviewSamples(InDeltaTime, bHasWindSnapshot ? &WindSnapshot : nullptr);
		CurrentModeText = LOCTEXT("PreviewModeTick", "Preview");
	}
	else
	{
		CurrentModeText = LOCTEXT("LiveModeTick", "Live");
	}

	// グラフウィジェットへ最新サンプル・表示設定を反映
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetDisplaySeconds(DisplaySeconds);
		GraphWidget->SetSeriesVisibility(SeriesVisibility);
		GraphWidget->SetSamples(DisplaySamples);
	}
	Invalidate(EInvalidateWidgetReason::Paint);

	return EActiveTimerReturnType::Continue;
}

void SKawaiiPhysicsWindScopeWindow::RefreshExternalForceItems()
{
	// ノードが持つ ProceduralWind 外力のインデックス一覧を再収集
	ExternalForceItems.Reset();
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	if (GraphNode)
	{
		for (int32 Index = 0; Index < GraphNode->Node.ExternalForces.Num(); ++Index)
		{
			if (IsProceduralWindStruct(GraphNode->Node.ExternalForces[Index]))
			{
				ExternalForceItems.Add(MakeShared<int32>(Index));
			}
		}
	}

	// 直前の選択Indexを維持できなければ先頭を選択
	SelectedExternalForceItem.Reset();
	for (const FExternalForceIndexPtr& Item : ExternalForceItems)
	{
		if (Item.IsValid() && *Item == Args.ExternalForceIndex)
		{
			SelectedExternalForceItem = Item;
			break;
		}
	}
	if (!SelectedExternalForceItem.IsValid() && ExternalForceItems.Num() > 0)
	{
		SelectedExternalForceItem = ExternalForceItems[0];
		Args.ExternalForceIndex = *SelectedExternalForceItem;
	}

	// コンボボックスへ反映
	if (ExternalForceComboBox.IsValid())
	{
		ExternalForceComboBox->RefreshOptions();
		ExternalForceComboBox->SetSelectedItem(SelectedExternalForceItem);
	}
}

void SKawaiiPhysicsWindScopeWindow::RebuildPreviewSamples(
	float InDeltaTime,
	const FKawaiiPhysics_ExternalForce_ProceduralWind* WindSnapshot)
{
	// 対象の ProceduralWind 設定を取得できなければ（未選択・ノード未解決等）表示をクリア
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	if (WindSnapshot)
	{
		Wind = *WindSnapshot;
	}
	else if (!TryGetPreviewForceCopy(Wind))
	{
		DisplaySamples.Reset();
		return;
	}

	// エディタ経過時間を積算し、直近 DisplaySeconds 秒分を等間隔サンプリングする
	PreviewTime += FMath::Clamp(InDeltaTime, 0.0f, 0.1f);
	DisplaySamples.Reset();
	DisplaySamples.Reserve(WindScopePreviewSampleCount);
	const float EndTime = PreviewTime;
	const float StartTime = EndTime - DisplaySeconds;
	const int32 SampleCount = FMath::Max(WindScopePreviewSampleCount, 2);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
		const float SampleTime = FMath::Lerp(StartTime, EndTime, Alpha);
		FKawaiiProceduralWindScopeSample ScopeSample;
		ScopeSample.Time = SampleTime;
		// LengthRate=0（ルート相当）で評価。PreApply が ScopeBuffer に書き込む Live サンプルも同じ LengthRate=0
		// で計算されるため、両者は同一基準で比較できる（実ボーンの LengthRateFromRoot による Wave 位相ずれは含まない）
		ScopeSample.Sample = Wind.ComputeWindSample(SampleTime, 0.0f);
		DisplaySamples.Add(ScopeSample);
	}
}

void SKawaiiPhysicsWindScopeWindow::UpdateEditValuesFromWind(
	const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind)
{
	CachedEditValues = FKawaiiWindScopeEditValues();
	if (!Wind)
	{
		return;
	}

	static const FKawaiiPhysics_ExternalForce_ProceduralWind DefaultWind;
	CachedEditValues.bValid = true;
	for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		for (const FKawaiiWindScopeParamDef& Param : Group.Params)
		{
			FProperty* Property = FindProceduralWindProperty(Param.PropertyName);
			if (!Property)
			{
				continue;
			}

			if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				if (Param.PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
				{
					CachedEditValues.bIsEnabled = BoolProperty->GetPropertyValue_InContainer(Wind);
				}
			}
			else if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
			{
				CachedEditValues.FloatValues.Add(Param.PropertyName, FloatProperty->GetPropertyValue_InContainer(Wind));
			}
			else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
			{
				if (Param.PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomSeed))
				{
					CachedEditValues.RandomSeed = IntProperty->GetPropertyValue_InContainer(Wind);
				}
			}
			else if (IsFVectorProperty(Property) &&
				Param.PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
			{
				CachedEditValues.WindDirection = *Property->ContainerPtrToValuePtr<FVector>(Wind);
			}

			if (IsProceduralWindPropertyModifiedFromDefault(*Wind, DefaultWind, Property))
			{
				CachedEditValues.ModifiedFromDefault.Add(Param.PropertyName);
			}
		}
	}
}

bool SKawaiiPhysicsWindScopeWindow::TryUpdateFromLiveRuntime()
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	if (!GraphNode)
	{
		return false;
	}

	// 実行中の FAnimNode_KawaiiPhysics から対象 ProceduralWind の RuntimeState を解決
	FAnimNode_KawaiiPhysics* RuntimeNode = KawaiiPhysicsEdUtils::ResolveLiveKawaiiPhysicsNode(GraphNode);
	if (!RuntimeNode)
	{
		return false;
	}

	const int32 ResolvedIndex = ResolveProceduralWindIndex(*RuntimeNode, Args.ExternalForceIndex);
	if (!RuntimeNode->ExternalForces.IsValidIndex(ResolvedIndex))
	{
		return false;
	}

	const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
		RuntimeNode->ExternalForces[ResolvedIndex].GetPtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	if (!Wind || !Wind->RuntimeState.IsValid())
	{
		return false;
	}

	// ScopeBuffer は PreApply（アニメーションワーカースレッド）が Mutex 下で書き込むリングバッファなので、
	// ロック区間はコピーのみに留め、Game Thread 側にスナップショットを持ち出してから解放する
	TArray<FKawaiiProceduralWindScopeSample> Snapshot;
	uint64 ScopeSampleCount = 0;
	{
		FScopeLock Lock(&Wind->RuntimeState->Mutex);
		ScopeSampleCount = Wind->RuntimeState->ScopeSampleCount;
		if (ScopeSampleCount <= LastLiveSampleCount || Wind->RuntimeState->ScopeBuffer.Num() == 0)
		{
			return false;
		}

		// ScopeWriteIndex は次に書き込むスロットを指すため、そこから CopyCount 分遡った位置が最古のサンプルになる
		const int32 BufferNum = Wind->RuntimeState->ScopeBuffer.Num();
		const int32 CopyCount = FMath::Min<int32>(BufferNum, static_cast<int32>(FMath::Min<uint64>(ScopeSampleCount, MAX_int32)));
		Snapshot.Reserve(CopyCount);
		const int32 StartIndex = (Wind->RuntimeState->ScopeWriteIndex - CopyCount + BufferNum) % BufferNum;
		for (int32 CopyIndex = 0; CopyIndex < CopyCount; ++CopyIndex)
		{
			Snapshot.Add(Wind->RuntimeState->ScopeBuffer[(StartIndex + CopyIndex) % BufferNum]);
		}
	}

	// 新規サンプル分だけ取り込めたので Display 側へ反映
	LastLiveSampleCount = ScopeSampleCount;
	Args.ExternalForceIndex = ResolvedIndex;
	DisplaySamples = MoveTemp(Snapshot);
	return DisplaySamples.Num() > 0;
}

bool SKawaiiPhysicsWindScopeWindow::TryGetPreviewForceCopy(
	FKawaiiPhysics_ExternalForce_ProceduralWind& OutForce) const
{
	const UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	if (!GraphNode)
	{
		return false;
	}

	const int32 ResolvedIndex = ResolveProceduralWindIndex(GraphNode->Node, Args.ExternalForceIndex);
	if (!GraphNode->Node.ExternalForces.IsValidIndex(ResolvedIndex))
	{
		return false;
	}

	const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
		GraphNode->Node.ExternalForces[ResolvedIndex].GetPtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	if (!Wind)
	{
		return false;
	}

	// 値をコピーして返す。Preview計算のためだけに複製し、編集中のグラフノード本体には触れない
	OutForce = *Wind;
	return true;
}

UAnimGraphNode_KawaiiPhysics* SKawaiiPhysicsWindScopeWindow::ResolveGraphNode() const
{
	// まず弱参照を優先し、失効していれば AnimBlueprintPath+NodeGuid から再解決する
	// （BP再コンパイル等でノードインスタンスが差し替わっても追従できる）
	if (Args.GraphNode.IsValid())
	{
		return Args.GraphNode.Get();
	}

	if (ResolvedGraphNodeCache.IsValid())
	{
		return ResolvedGraphNodeCache.Get();
	}

	if (Args.AnimBlueprintPath.IsValid() && Args.NodeGuid.IsValid())
	{
		if (UAnimGraphNode_KawaiiPhysics* ResolvedGraphNode = FindLoadedGraphNodeByGuid(
			Args.AnimBlueprintPath.ResolveObject(),
			Args.NodeGuid))
		{
			ResolvedGraphNodeCache = ResolvedGraphNode;
			return ResolvedGraphNode;
		}
	}

	return nullptr;
}

bool SKawaiiPhysicsWindScopeWindow::HasTargetArgs() const
{
	return HasTargetArgs(Args);
}

bool SKawaiiPhysicsWindScopeWindow::HasTargetArgs(const FKawaiiPhysicsWindScopeWindowArgs& InArgs)
{
	return InArgs.GraphNode.IsValid() || (InArgs.AnimBlueprintPath.IsValid() && InArgs.NodeGuid.IsValid());
}

void SKawaiiPhysicsWindScopeWindow::SaveLastTargetArgs(const FKawaiiPhysicsWindScopeWindowArgs& InArgs)
{
	if (!GConfig || !InArgs.AnimBlueprintPath.IsValid() || !InArgs.NodeGuid.IsValid())
	{
		return;
	}

	GConfig->SetString(
		WindScopeConfigSectionName,
		WindScopeLastAnimBlueprintKey,
		*InArgs.AnimBlueprintPath.ToString(),
		GEditorPerProjectIni);
	GConfig->SetString(
		WindScopeConfigSectionName,
		WindScopeLastNodeGuidKey,
		*InArgs.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
		GEditorPerProjectIni);
	GConfig->SetInt(
		WindScopeConfigSectionName,
		WindScopeLastForceIndexKey,
		InArgs.ExternalForceIndex,
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SKawaiiPhysicsWindScopeWindow::LoadEditPanelConfig()
{
	if (!GConfig)
	{
		return;
	}

	GConfig->GetBool(
		WindScopeConfigSectionName,
		WindScopeEditPanelExpandedKey,
		bEditPanelExpanded,
		GEditorPerProjectIni);
	GConfig->GetFloat(
		WindScopeConfigSectionName,
		WindScopeEditPanelSplitterFractionKey,
		EditPanelSplitterFraction,
		GEditorPerProjectIni);
	EditPanelSplitterFraction = FMath::Clamp(EditPanelSplitterFraction, 0.2f, 0.8f);
}

void SKawaiiPhysicsWindScopeWindow::SaveEditPanelConfig() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetBool(
		WindScopeConfigSectionName,
		WindScopeEditPanelExpandedKey,
		bEditPanelExpanded,
		GEditorPerProjectIni);
	GConfig->SetFloat(
		WindScopeConfigSectionName,
		WindScopeEditPanelSplitterFractionKey,
		EditPanelSplitterFraction,
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SKawaiiPhysicsWindScopeWindow::LoadPendingReconnectFromConfig()
{
	ClearPendingReconnect();
	if (!GConfig)
	{
		return;
	}

	FString AnimBlueprintPathString;
	FString NodeGuidString;
	if (!GConfig->GetString(WindScopeConfigSectionName, WindScopeLastAnimBlueprintKey, AnimBlueprintPathString, GEditorPerProjectIni) ||
		!GConfig->GetString(WindScopeConfigSectionName, WindScopeLastNodeGuidKey, NodeGuidString, GEditorPerProjectIni))
	{
		return;
	}

	FGuid ParsedNodeGuid;
	if (!FGuid::Parse(NodeGuidString, ParsedNodeGuid))
	{
		return;
	}

	FSoftObjectPath ParsedAnimBlueprintPath(AnimBlueprintPathString);
	if (!ParsedAnimBlueprintPath.IsValid() || !ParsedNodeGuid.IsValid())
	{
		return;
	}

	PendingReconnectArgs.AnimBlueprintPath = ParsedAnimBlueprintPath;
	PendingReconnectArgs.NodeGuid = ParsedNodeGuid;
	PendingReconnectArgs.ExternalForceIndex = INDEX_NONE;
	GConfig->GetInt(
		WindScopeConfigSectionName,
		WindScopeLastForceIndexKey,
		PendingReconnectArgs.ExternalForceIndex,
		GEditorPerProjectIni);
	bHasPendingReconnect = true;
	bPendingReconnectAsyncLoadStarted = false;
	PendingReconnectElapsedTime = 0.0f;
}

void SKawaiiPhysicsWindScopeWindow::ClearPendingReconnect(bool bCancelAsyncLoad)
{
	if (bCancelAsyncLoad && PendingReconnectAsyncLoadHandle.IsValid())
	{
		PendingReconnectAsyncLoadHandle->CancelHandle();
	}
	PendingReconnectAsyncLoadHandle.Reset();
	ResolvedGraphNodeCache.Reset();
	PendingReconnectArgs = FKawaiiPhysicsWindScopeWindowArgs();
	bHasPendingReconnect = false;
	bPendingReconnectAsyncLoadStarted = false;
	PendingReconnectElapsedTime = 0.0f;
}

void SKawaiiPhysicsWindScopeWindow::TryResolvePendingReconnect(float InDeltaTime)
{
	if (!bHasPendingReconnect)
	{
		return;
	}

	if (!HasTargetArgs(PendingReconnectArgs))
	{
		ClearPendingReconnect();
		return;
	}

	if (Cast<UAnimBlueprint>(PendingReconnectArgs.AnimBlueprintPath.ResolveObject()))
	{
		FKawaiiPhysicsWindScopeWindowArgs ReconnectArgs = PendingReconnectArgs;
		ClearPendingReconnect();
		SetArgs(MoveTemp(ReconnectArgs));
		return;
	}

	PendingReconnectElapsedTime += FMath::Max(InDeltaTime, 0.0f);
	if (bPendingReconnectAsyncLoadStarted || PendingReconnectElapsedTime < WindScopeReconnectTryLoadDelay)
	{
		return;
	}

	StartPendingReconnectAsyncLoad();
}

void SKawaiiPhysicsWindScopeWindow::StartPendingReconnectAsyncLoad()
{
	if (!bHasPendingReconnect || bPendingReconnectAsyncLoadStarted || !HasTargetArgs(PendingReconnectArgs))
	{
		return;
	}

	bPendingReconnectAsyncLoadStarted = true;
	const FKawaiiPhysicsWindScopeWindowArgs ExpectedArgs = PendingReconnectArgs;
	PendingReconnectAsyncLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		PendingReconnectArgs.AnimBlueprintPath,
		FStreamableDelegate::CreateSP(
			this,
			&SKawaiiPhysicsWindScopeWindow::OnPendingReconnectAsyncLoadComplete,
			ExpectedArgs));
}

void SKawaiiPhysicsWindScopeWindow::OnPendingReconnectAsyncLoadComplete(FKawaiiPhysicsWindScopeWindowArgs ExpectedArgs)
{
	PendingReconnectAsyncLoadHandle.Reset();
	if (!bHasPendingReconnect || !AreReconnectArgsSame(PendingReconnectArgs, ExpectedArgs))
	{
		return;
	}

	if (Cast<UAnimBlueprint>(PendingReconnectArgs.AnimBlueprintPath.ResolveObject()))
	{
		FKawaiiPhysicsWindScopeWindowArgs ReconnectArgs = PendingReconnectArgs;
		ClearPendingReconnect(false);
		SetArgs(MoveTemp(ReconnectArgs));
	}
	else
	{
		ClearPendingReconnect(false);
	}
}

#undef LOCTEXT_NAMESPACE
