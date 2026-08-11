// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "SKawaiiPhysicsWindScopeWindow.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/CriticalSection.h"
#include "KawaiiPhysicsEdWindowUtils.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeLock.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsWindScopeWindow"

namespace
{
	const TCHAR* WindScopeConfigSectionName = TEXT("KawaiiPhysicsEd");
	const TCHAR* WindScopeWindowPosConfigKey = TEXT("WindScopeWindowPos");
	const TCHAR* WindScopeWindowSizeConfigKey = TEXT("WindScopeWindowSize");
	constexpr int32 WindScopePreviewSampleCount = 240;
	constexpr float WindScopeGraphPaddingLeft = 42.0f;
	constexpr float WindScopeGraphPaddingTop = 12.0f;
	constexpr float WindScopeGraphPaddingRight = 12.0f;
	constexpr float WindScopeGraphPaddingBottom = 24.0f;

	TWeakPtr<SWindow> KawaiiWindScopeWindowWeak;
	TWeakPtr<SKawaiiPhysicsWindScopeWindow> KawaiiWindScopeWidgetWeak;

	struct FKawaiiWindScopeComponentStyle
	{
		EKawaiiPhysicsWindScopeComponent Component;
		FText Label;
		FLinearColor Color;
		float Thickness = 1.0f;
		bool bDashed = false;
	};

	struct FKawaiiWindScopeRange
	{
		float MinTime = 0.0f;
		float MaxTime = 1.0f;
		float MinValue = -1.0f;
		float MaxValue = 1.0f;
	};

	// 各波形成分の表示スタイル（色・凡例ラベル・線種）を定義する
	const TArray<FKawaiiWindScopeComponentStyle>& GetWindScopeComponentStyles()
	{
		static const TArray<FKawaiiWindScopeComponentStyle> Styles =
		{
			{EKawaiiPhysicsWindScopeComponent::Total, LOCTEXT("TotalLabel", "Total"), FLinearColor::White, 2.0f, false},
			{EKawaiiPhysicsWindScopeComponent::Steady, LOCTEXT("SteadyLabel", "Steady"), FLinearColor(1.0f, 0.48f, 0.08f), 1.0f, false},
			{EKawaiiPhysicsWindScopeComponent::Oscillation, LOCTEXT("OscillationLabel", "Oscillation"), FLinearColor(1.0f, 0.86f, 0.05f), 1.0f, false},
			{EKawaiiPhysicsWindScopeComponent::Wave, LOCTEXT("WaveLabel", "Wave"), FLinearColor(0.0f, 0.85f, 1.0f), 1.0f, false},
			{EKawaiiPhysicsWindScopeComponent::Envelope, LOCTEXT("EnvelopeLabel", "Envelope"), FLinearColor(0.2f, 0.42f, 1.0f), 1.0f, true},
			{EKawaiiPhysicsWindScopeComponent::Random, LOCTEXT("RandomLabel", "Random"), FLinearColor(1.0f, 0.25f, 0.78f), 1.0f, false},
			{EKawaiiPhysicsWindScopeComponent::Gust, LOCTEXT("GustLabel", "Gust"), FLinearColor(1.0f, 0.12f, 0.08f), 1.0f, false},
		};
		return Styles;
	}

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

	// 凡例1項目（色スウォッチ＋表示切替チェックボックス＋ラベル）を生成する
	TSharedRef<SWidget> MakeWindScopeLegendItem(SKawaiiPhysicsWindScopeWindow* Owner,
	                                            const FKawaiiWindScopeComponentStyle& Style)
	{
		return SNew(SCheckBox)
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

		if (Style.bDashed)
		{
			DrawWindScopeDashedLine(OutDrawElements, LayerId + 3, AllottedGeometry, Points, Style.Color, Style.Thickness);
		}
		else
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 3,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				Style.Color,
				true,
				Style.Thickness);
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
	SetArgs(MoveTemp(InitArgs));

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
		// プリセットボタン列（Breeze / Strong / Storm）
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BreezePresetButton", "Breeze / そよ風"))
				.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnBreezePresetClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("StrongPresetButton", "Strong / 強風"))
				.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnStrongPresetClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("StormPresetButton", "Storm / 嵐"))
				.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnStormPresetClicked)
			]
		]
		// 凡例（系列ごとの表示切替チェックボックス）
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
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
		// 波形グラフ本体
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 4.0f)
		[
			SAssignNew(GraphWidget, SKawaiiPhysicsWindScopeGraph)
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

	// 60fps 相当でティックし、Live/Preview のサンプル更新とグラフ再描画を行う
	RegisterActiveTimer(1.0f / 60.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SKawaiiPhysicsWindScopeWindow::TickWindScope));
}

void SKawaiiPhysicsWindScopeWindow::OpenWindow(FKawaiiPhysicsWindScopeWindowArgs Args)
{
	// 既存ウィンドウがあれば引数を差し替えて前面に出すだけ（ウィンドウは常に1つに集約する）
	if (TSharedPtr<SWindow> ExistingWindow = KawaiiWindScopeWindowWeak.Pin())
	{
		if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> ExistingWidget = KawaiiWindScopeWidgetWeak.Pin())
		{
			ExistingWidget->SetArgs(MoveTemp(Args));
		}
		ExistingWindow->BringToFront();
		return;
	}

	// 新規ウィンドウを生成
	TSharedRef<SKawaiiPhysicsWindScopeWindow> ScopeWidget =
		SNew(SKawaiiPhysicsWindScopeWindow, MoveTemp(Args));

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Kawaii Physics: Wind Scope"))
		.ClientSize(FVector2D(720.0f, 420.0f))
		.MinWidth(520.0f)
		.MinHeight(320.0f)
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		[
			ScopeWidget
		];

	// 閉じたら配置を保存し弱参照をクリアする（次回 OpenWindow 時は上のブロックで新規生成される）
	Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>& ClosedWindow)
	{
		KawaiiPhysicsEdWindowUtils::PersistWindowPlacement(
			ClosedWindow,
			WindScopeConfigSectionName,
			WindScopeWindowPosConfigKey,
			WindScopeWindowSizeConfigKey);
		KawaiiWindScopeWindowWeak.Reset();
		KawaiiWindScopeWidgetWeak.Reset();
	}));

	KawaiiWindScopeWindowWeak = Window;
	KawaiiWindScopeWidgetWeak = ScopeWidget;
	FSlateApplication::Get().AddWindow(Window);
	KawaiiPhysicsEdWindowUtils::RestoreWindowPlacement(
		Window,
		WindScopeConfigSectionName,
		WindScopeWindowPosConfigKey,
		WindScopeWindowSizeConfigKey);
}

void SKawaiiPhysicsWindScopeWindow::CloseAllWindows()
{
	if (TSharedPtr<SWindow> Window = KawaiiWindScopeWindowWeak.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

void SKawaiiPhysicsWindScopeWindow::SetArgs(FKawaiiPhysicsWindScopeWindowArgs InArgs)
{
	// 対象引数を差し替え、表示状態をリセットして外力一覧を再構築する
	Args = MoveTemp(InArgs);
	DisplaySamples.Reset();
	PreviewTime = 0.0f;
	LastLiveSampleCount = 0;
	CurrentModeText = LOCTEXT("PreviewMode", "Preview");
	RefreshExternalForceItems();
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
}

FText SKawaiiPhysicsWindScopeWindow::GetSelectedExternalForceText() const
{
	return SelectedExternalForceItem.IsValid()
		       ? FText::Format(LOCTEXT("SelectedExternalForceFormat", "#{0} ProceduralWind"), FText::AsNumber(*SelectedExternalForceItem))
		       : LOCTEXT("NoExternalForceSelected", "No ProceduralWind");
}

FText SKawaiiPhysicsWindScopeWindow::GetTargetNodeText() const
{
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

// 以下3つは強さの異なる決め打ちプリセット（そよ風/強風/嵐）を ApplyPreset に渡すだけの薄いラッパー
FReply SKawaiiPhysicsWindScopeWindow::OnBreezePresetClicked()
{
	return ApplyPreset(
		2.0f,
		1.0f,
		2.0f,
		1.0f,
		1.5f,
		90.0f,
		0.6f,
		1.0f,
		0.05f,
		0.5f,
		0.8f,
		5.0f,
		LOCTEXT("BreezePresetName", "Breeze"));
}

FReply SKawaiiPhysicsWindScopeWindow::OnStrongPresetClicked()
{
	return ApplyPreset(
		8.0f,
		4.0f,
		0.8f,
		3.0f,
		0.6f,
		120.0f,
		0.7f,
		1.3f,
		0.08f,
		2.0f,
		0.5f,
		10.0f,
		LOCTEXT("StrongPresetName", "Strong"));
}

FReply SKawaiiPhysicsWindScopeWindow::OnStormPresetClicked()
{
	return ApplyPreset(
		15.0f,
		10.0f,
		0.4f,
		8.0f,
		0.35f,
		180.0f,
		0.5f,
		1.6f,
		0.15f,
		6.0f,
		0.3f,
		20.0f,
		LOCTEXT("StormPresetName", "Storm"));
}

FReply SKawaiiPhysicsWindScopeWindow::ApplyPreset(float SteadyForce,
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
                                                  const FText& PresetName)
{
	// 対象グラフノードを解決（失敗時は通知して終了）
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	if (!GraphNode)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyPresetNoNode", "Failed to resolve the KawaiiPhysics graph node."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	// ProceduralWind 外力を解決・型チェック
	const int32 ResolvedIndex = ResolveProceduralWindIndex(GraphNode->Node, Args.ExternalForceIndex);
	if (!GraphNode->Node.ExternalForces.IsValidIndex(ResolvedIndex))
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyPresetNoWind", "No ProceduralWind external force was found."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
		GraphNode->Node.ExternalForces[ResolvedIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	if (!Wind)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyPresetInvalidWind", "The selected external force is not ProceduralWind."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	// Undo/Redo 対応のトランザクションでパラメータを書き込む
	const FScopedTransaction Transaction(LOCTEXT("ApplyWindPresetTransaction", "Apply Kawaii Physics Wind Preset"));
	GraphNode->Modify();
	Wind->bIsEnabled = true;
	Wind->SteadyForce = SteadyForce;
	Wind->OscillationForce = OscillationForce;
	Wind->OscillationPeriod = OscillationPeriod;
	Wind->WaveAmplitude = WaveAmplitude;
	Wind->WavePeriod = WavePeriod;
	Wind->WaveSpatialOffset = WaveSpatialOffset;
	Wind->EnvelopeMin = EnvelopeMin;
	Wind->EnvelopeMax = EnvelopeMax;
	Wind->EnvelopeFrequency = EnvelopeFrequency;
	Wind->RandomForce = RandomForce;
	Wind->RandomPeriod = RandomPeriod;
	Wind->DirectionNoiseAngle = DirectionNoiseAngle;
	Wind->TimeScale = 1.0f;
	MarkWindScopeGraphNodeModified(GraphNode);

	// 外力一覧を更新し、成功通知を表示
	Args.ExternalForceIndex = ResolvedIndex;
	RefreshExternalForceItems();
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		FText::Format(LOCTEXT("ApplyPresetSucceeded", "Applied {0} wind preset."), PresetName),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

EActiveTimerReturnType SKawaiiPhysicsWindScopeWindow::TickWindScope(double InCurrentTime, float InDeltaTime)
{
	(void)InCurrentTime;
	if (bPaused)
	{
		return EActiveTimerReturnType::Continue;
	}

	// Live 取得に失敗したら（PIE/デバッグ対象なし等）Preview 波形を計算する
	if (!TryUpdateFromLiveRuntime())
	{
		RebuildPreviewSamples(InDeltaTime);
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

void SKawaiiPhysicsWindScopeWindow::RebuildPreviewSamples(float InDeltaTime)
{
	// 対象の ProceduralWind 設定を取得できなければ（未選択・ノード未解決等）表示をクリア
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	if (!TryGetPreviewForceCopy(Wind))
	{
		DisplaySamples.Reset();
		return;
	}

	// エディタ経過時間を積算し、直近 DisplaySeconds 秒分を等間隔サンプリングする
	PreviewTime += FMath::Max(InDeltaTime, 0.0f);
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

bool SKawaiiPhysicsWindScopeWindow::TryUpdateFromLiveRuntime()
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	if (!GraphNode)
	{
		return false;
	}

	// PIE等でデバッグ対象になっているノードを取得（デバッグ対象でなければ Live 扱いにしない）
	UAnimBlueprint* AnimBlueprint = GraphNode->GetAnimBlueprint();
	UObject* ObjectBeingDebugged = AnimBlueprint ? AnimBlueprint->GetObjectBeingDebugged() : nullptr;
	if (!Cast<UAnimInstance>(ObjectBeingDebugged))
	{
		return false;
	}

	// 実行中の FAnimNode_KawaiiPhysics から対象 ProceduralWind の RuntimeState を解決
	FAnimNode_KawaiiPhysics* RuntimeNode = GraphNode->GetDebuggedAnimNode<FAnimNode_KawaiiPhysics>();
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

	if (Args.AnimBlueprintPath.IsValid() && Args.NodeGuid.IsValid())
	{
		return UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(Args.AnimBlueprintPath, Args.NodeGuid);
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
