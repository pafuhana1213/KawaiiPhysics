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
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ISettingsModule.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsEdUtils.h"
#include "KawaiiPhysicsEdWindowUtils.h"
#include "SKawaiiPhysicsWindScopeEditPanel.h"
#include "KawaiiPhysicsWindScopeStyle.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/ToolBarStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsWindScopeWindow"

SLATE_IMPLEMENT_WIDGET(SKawaiiPhysicsWindScopeWindow)

void SKawaiiPhysicsWindScopeWindow::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	(void)AttributeInitializer;
}

namespace KawaiiPhysicsWindScopeWindowPrivate
{
	constexpr int32 WindScopePreviewSampleCount = 240;
	constexpr float WindScopeGraphPaddingLeft = 42.0f;
	constexpr float WindScopeGraphPaddingTop = 14.0f;
	constexpr float WindScopeGraphPaddingRight = 36.0f;
	constexpr float WindScopeGraphPaddingBottom = 34.0f;
	constexpr float WindScopeDefaultGustStrength = 6.0f;
	constexpr float WindScopeDefaultGustRiseTime = 0.1f;
	constexpr float WindScopeDefaultGustDecayTime = 0.5f;
	constexpr float WindScopeReconnectTryLoadDelay = 5.0f;
	const TCHAR* WindScopeClipboardMarker = TEXT("KawaiiPhysicsProceduralWind:");
	const TCHAR* WindScopeConfigSectionName = TEXT("KawaiiPhysicsEd");
	const TCHAR* WindScopeLastAnimBlueprintKey = TEXT("WindScopeLastAnimBlueprint");
	const TCHAR* WindScopeLastNodeGuidKey = TEXT("WindScopeLastNodeGuid");
	const TCHAR* WindScopeLastForceIndexKey = TEXT("WindScopeLastForceIndex");
	const TCHAR* WindScopeEditPanelExpandedKey = TEXT("WindScopeEditPanelExpanded");
	const TCHAR* WindScopeEditPanelSplitterFractionKey = TEXT("WindScopeEditPanelSplitterFraction");
	const TCHAR* WindScopeGustStrengthKey = TEXT("WindScopeGustStrength");
	const TCHAR* WindScopeGustRiseTimeKey = TEXT("WindScopeGustRiseTime");
	const TCHAR* WindScopeGustDecayTimeKey = TEXT("WindScopeGustDecayTime");

	TArray<TWeakPtr<SKawaiiPhysicsWindScopeWindow>> LiveWindScopeWindows;

	void RegisterLiveWindow(TSharedRef<SKawaiiPhysicsWindScopeWindow> Window)
	{
		LiveWindScopeWindows.RemoveAllSwap([](const TWeakPtr<SKawaiiPhysicsWindScopeWindow>& ExistingWindow)
		{
			return !ExistingWindow.IsValid();
		});
		for (const TWeakPtr<SKawaiiPhysicsWindScopeWindow>& ExistingWindow : LiveWindScopeWindows)
		{
			if (ExistingWindow.Pin().Get() == &Window.Get())
			{
				return;
			}
		}
		LiveWindScopeWindows.Add(Window);
	}

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

	struct FKawaiiPhysicsWindScopeRange
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
		case EKawaiiPhysicsWindScopeComponent::Constant:
			return Sample.Constant;
		case EKawaiiPhysicsWindScopeComponent::Sway:
			return Sample.Sway;
		case EKawaiiPhysicsWindScopeComponent::Ripple:
			return Sample.Ripple;
		case EKawaiiPhysicsWindScopeComponent::StrengthCycle:
			return Sample.StrengthCycle;
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

	bool IsFloatIntervalProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == TBaseStructure<FFloatInterval>::Get();
	}

	bool IsParameterModeProperty(const FProperty* Property)
	{
		const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property);
		return EnumProperty &&
			EnumProperty->GetEnum() &&
			EnumProperty->GetEnum()->GetFName() == StaticEnum<EKawaiiProceduralWindParameterMode>()->GetFName();
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

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProperty->ContainerPtrToValuePtr<void>(&Wind),
				FMath::Clamp(FMath::RoundToInt64(NewValue), 0, 1));
			return true;
		}

		if (IsFVectorProperty(Property) && VectorComponentIndex >= 0 && VectorComponentIndex <= 2)
		{
			FVector* Vector = Property->ContainerPtrToValuePtr<FVector>(&Wind);
			(*Vector)[VectorComponentIndex] = static_cast<FVector::FReal>(NewValue);
			return true;
		}

		if (IsFloatIntervalProperty(Property) && VectorComponentIndex >= 0 && VectorComponentIndex <= 1)
		{
			FFloatInterval* Interval = Property->ContainerPtrToValuePtr<FFloatInterval>(&Wind);
			if (VectorComponentIndex == 0)
			{
				Interval->Min = static_cast<float>(NewValue);
			}
			else
			{
				Interval->Max = static_cast<float>(NewValue);
			}
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

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 CurrentValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(
				EnumProperty->ContainerPtrToValuePtr<void>(&Wind));
			return CurrentValue == FMath::Clamp(FMath::RoundToInt64(NewValue), 0, 1);
		}

		if (IsFVectorProperty(Property) && VectorComponentIndex >= 0 && VectorComponentIndex <= 2)
		{
			const FVector* Vector = Property->ContainerPtrToValuePtr<FVector>(&Wind);
			FVector EditedVector = *Vector;
			EditedVector[VectorComponentIndex] = static_cast<FVector::FReal>(NewValue);
			return Vector->Equals(EditedVector);
		}

		if (IsFloatIntervalProperty(Property) && VectorComponentIndex >= 0 && VectorComponentIndex <= 1)
		{
			const FFloatInterval* Interval = Property->ContainerPtrToValuePtr<FFloatInterval>(&Wind);
			FFloatInterval EditedInterval = *Interval;
			if (VectorComponentIndex == 0)
			{
				EditedInterval.Min = static_cast<float>(NewValue);
			}
			else
			{
				EditedInterval.Max = static_cast<float>(NewValue);
			}
			return FMath::IsNearlyEqual(Interval->Min, EditedInterval.Min) &&
				FMath::IsNearlyEqual(Interval->Max, EditedInterval.Max);
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

	FKawaiiProceduralWindPreset MakeWindPresetFromCurrentWind(
		const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind)
	{
		FKawaiiProceduralWindPreset Preset;
		Preset.ConstantForce = Wind.ConstantForce;
		Preset.SwayForce = Wind.SwayForce;
		Preset.SwayPeriod = Wind.SwayPeriod;
		Preset.RippleForce = Wind.RippleForce;
		Preset.RipplePeriod = Wind.RipplePeriod;
		Preset.RippleTipPhaseDelay = Wind.RippleTipPhaseDelay;
		Preset.StrengthCycleRange = Wind.StrengthCycleRange;
		Preset.StrengthCyclePeriod = Wind.StrengthCyclePeriod;
		Preset.RandomForce = Wind.RandomForce;
		Preset.RandomForcePeriod = Wind.RandomForcePeriod;
		Preset.WindDirectionNoiseAngle = Wind.WindDirectionNoiseAngle;
		return Preset;
	}

	void FillWindScopeEditValuesFromWind(
		const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind,
		FKawaiiPhysicsWindScopeEditValues& OutValues,
		const bool bTrackDefaultDiff)
	{
		OutValues = FKawaiiPhysicsWindScopeEditValues();
		if (!Wind)
		{
			return;
		}

		static const FKawaiiPhysics_ExternalForce_ProceduralWind DefaultWind;
		OutValues.bValid = true;
		for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
		{
			for (const FKawaiiPhysicsWindScopeParamDef& Param : Group.Params)
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
						OutValues.bIsEnabled = BoolProperty->GetPropertyValue_InContainer(Wind);
					}
				}
				else if (IsParameterModeProperty(Property))
				{
					const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property);
					const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(
						EnumProperty->ContainerPtrToValuePtr<void>(Wind));
					OutValues.ParameterMode = EnumValue == static_cast<int64>(EKawaiiProceduralWindParameterMode::Advanced)
						                          ? EKawaiiProceduralWindParameterMode::Advanced
						                          : EKawaiiProceduralWindParameterMode::Simple;
				}
				else if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
				{
					OutValues.FloatValues.Add(Param.PropertyName, FloatProperty->GetPropertyValue_InContainer(Wind));
				}
				else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
				{
					if (Param.PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, Seed))
					{
						OutValues.Seed = IntProperty->GetPropertyValue_InContainer(Wind);
					}
				}
				else if (IsFVectorProperty(Property) &&
					Param.PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
				{
					OutValues.WindDirection = *Property->ContainerPtrToValuePtr<FVector>(Wind);
				}
				else if (IsFloatIntervalProperty(Property))
				{
					OutValues.IntervalValues.Add(Param.PropertyName, *Property->ContainerPtrToValuePtr<FFloatInterval>(Wind));
				}

				if (bTrackDefaultDiff && IsProceduralWindPropertyModifiedFromDefault(*Wind, DefaultWind, Property))
				{
					OutValues.ModifiedFromDefault.Add(Param.PropertyName);
				}
			}
		}
	}

	FLinearColor MakeInactiveSeriesColor(FLinearColor Color)
	{
		Color = Color.Desaturate(0.5f);
		Color.A *= 0.6f;
		return Color;
	}

	FLinearColor ResolveBaseComponentColor(EKawaiiPhysicsWindScopeComponent Component)
	{
		for (const FKawaiiPhysicsWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
		{
			if (Style.Component == Component)
			{
				return Style.Color;
			}
		}
		return FLinearColor::White;
	}

	bool IsSeriesActiveFromValues(
		const FKawaiiPhysicsWindScopeEditValues* Values,
		EKawaiiPhysicsWindScopeComponent Component,
		bool bGustActive)
	{
		if (Component == EKawaiiPhysicsWindScopeComponent::Total)
		{
			return true;
		}
		if (Component == EKawaiiPhysicsWindScopeComponent::Gust)
		{
			return bGustActive;
		}
		if (!Values || !Values->bValid)
		{
			return true;
		}

		const auto IsNonZeroFloat = [Values](const FName PropertyName)
		{
			const float* Value = Values->FloatValues.Find(PropertyName);
			return Value && !FMath::IsNearlyZero(*Value);
		};

		switch (Component)
		{
		case EKawaiiPhysicsWindScopeComponent::Constant:
			return IsNonZeroFloat(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, ConstantForce));
		case EKawaiiPhysicsWindScopeComponent::Sway:
			return IsNonZeroFloat(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayForce));
		case EKawaiiPhysicsWindScopeComponent::Ripple:
			return IsNonZeroFloat(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RippleForce));
		case EKawaiiPhysicsWindScopeComponent::StrengthCycle:
			if (const FFloatInterval* Range = Values->IntervalValues.Find(
				GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCycleRange)))
			{
				return !FMath::IsNearlyEqual(Range->Min, 1.0f) || !FMath::IsNearlyEqual(Range->Max, 1.0f);
			}
			return true;
		case EKawaiiPhysicsWindScopeComponent::Random:
			return IsNonZeroFloat(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce));
		default:
			return true;
		}
	}

	FKawaiiPhysicsWindScopeRange ComputeWindScopeRange(const TArray<FKawaiiProceduralWindScopeSample>& Samples,
	                                           float DisplaySeconds,
	                                           const FKawaiiPhysicsWindScopeSeriesVisibility& Visibility)
	{
		FKawaiiPhysicsWindScopeRange Range;
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

			for (const FKawaiiPhysicsWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
			{
				if (!Visibility.IsVisible(Style.Component) ||
					Style.Component == EKawaiiPhysicsWindScopeComponent::StrengthCycle)
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

	float ComputeStrengthCycleAxisMax(
		const TArray<FKawaiiProceduralWindScopeSample>& Samples,
		float MinTime,
		const FKawaiiPhysicsWindScopeEditValues* EditValues)
	{
		float MaxValue = 0.0f;
		bool bHasValue = false;
		if (EditValues && EditValues->bValid)
		{
			if (const FFloatInterval* StrengthCycleRange = EditValues->IntervalValues.Find(
				GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCycleRange)))
			{
				MaxValue = FMath::Max(StrengthCycleRange->Min, StrengthCycleRange->Max);
				bHasValue = true;
			}
		}

		for (const FKawaiiProceduralWindScopeSample& Point : Samples)
		{
			if (Point.Time < MinTime)
			{
				continue;
			}

			MaxValue = bHasValue ? FMath::Max(MaxValue, Point.Sample.StrengthCycle) : Point.Sample.StrengthCycle;
			bHasValue = true;
		}

		return FMath::Max(2.0f, FMath::CeilToFloat(FMath::Max(MaxValue, 0.0f)));
	}

	FString FormatWindScopeAxisNumber(float Value)
	{
		return FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value), 0.01f)
			       ? FString::Printf(TEXT("%.0f"), Value)
			       : FString::Printf(TEXT("%.1f"), Value);
	}

	FString FormatWindScopeSecondsLabel(float Seconds)
	{
		// 浮動小数演算による -0.0 などの微小な負値を 0 に丸めてから整形する（"-0s" 表記を防ぐため）
		const float ClampedSeconds = FMath::IsNearlyZero(Seconds, 0.01f) ? 0.0f : Seconds;
		return FString::Printf(TEXT("%ss"), *FormatWindScopeAxisNumber(ClampedSeconds));
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
		Points.Reserve(Samples.Num());
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

	static TArray<FVector2D> BuildWindScopeGhostPolylinePoints(
		const TArray<FVector2D>& GhostSamples,
		float MinTime,
		float MaxTime,
		float MinValue,
		float MaxValue,
		const FVector2D& GraphOrigin,
		const FVector2D& GraphSize)
	{
		TArray<FVector2D> Points;
		Points.Reserve(GhostSamples.Num());
		const float TimeSpan = FMath::Max(MaxTime - MinTime, KINDA_SMALL_NUMBER);
		const float ValueSpan = FMath::Max(MaxValue - MinValue, KINDA_SMALL_NUMBER);

		for (const FVector2D& SamplePoint : GhostSamples)
		{
			if (SamplePoint.X < MinTime || SamplePoint.X > MaxTime)
			{
				continue;
			}

			const float XRate = (SamplePoint.X - MinTime) / TimeSpan;
			const float YRate = (SamplePoint.Y - MinValue) / ValueSpan;
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
				DashPoints.Reserve(2);
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

	class SKawaiiPhysicsWindScopeLegendHoverBorder : public SBorder
	{
	public:
		SLATE_BEGIN_ARGS(SKawaiiPhysicsWindScopeLegendHoverBorder)
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

	// 凡例1項目（表示切替チェックボックス＋ラベル）を生成する
	TSharedRef<SWidget> MakeWindScopeLegendItem(SKawaiiPhysicsWindScopeWindow* Owner,
	                                            const FKawaiiPhysicsWindScopeComponentStyle& Style)
	{
		return SNew(SKawaiiPhysicsWindScopeLegendHoverBorder)
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
				.Padding(FMargin(2.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(Style.Label)
					.ColorAndOpacity_Lambda([Owner, Component = Style.Component]()
					{
						return FSlateColor(Owner->ResolveSeriesDisplayColor(Component));
					})
					.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
				]
			];
	}

	TSharedRef<STextBlock> MakeFormulaHeaderStaticText(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 10, TEXT("Bold")));
	}

	TSharedRef<SWidget> MakeWindScopeFormulaLegend(SKawaiiPhysicsWindScopeWindow* Owner)
	{
		// グラフ直上の専用行に配置するため、幅が狭い場合は折り返す
		TSharedRef<SWrapBox> FormulaLegend = SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(2.0f, 2.0f));

		const auto AddSeries = [&FormulaLegend, Owner](EKawaiiPhysicsWindScopeComponent Component)
		{
			for (const FKawaiiPhysicsWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
			{
				if (Style.Component == Component)
				{
					FormulaLegend->AddSlot()
						.VAlign(VAlign_Center)
						.Padding(1.0f, 0.0f)
						[MakeWindScopeLegendItem(Owner, Style)];
					return;
				}
			}
		};
		const auto AddText = [&FormulaLegend](const FText& Text)
		{
			FormulaLegend->AddSlot()
				.VAlign(VAlign_Center)
				.Padding(1.0f, 0.0f)
				[MakeFormulaHeaderStaticText(Text)];
		};

		AddSeries(EKawaiiPhysicsWindScopeComponent::Total);
		AddText(LOCTEXT("FormulaHeaderEquals", " = ("));
		AddSeries(EKawaiiPhysicsWindScopeComponent::Constant);
		AddText(LOCTEXT("FormulaHeaderPlusSway", " + "));
		AddSeries(EKawaiiPhysicsWindScopeComponent::Sway);
		AddText(LOCTEXT("FormulaHeaderPlusRipple", " + "));
		AddSeries(EKawaiiPhysicsWindScopeComponent::Ripple);
		AddText(LOCTEXT("FormulaHeaderStrengthCyclePrefix", ") × "));
		AddSeries(EKawaiiPhysicsWindScopeComponent::StrengthCycle);
		AddText(LOCTEXT("FormulaHeaderPlusRandom", " + "));
		AddSeries(EKawaiiPhysicsWindScopeComponent::Random);
		AddText(LOCTEXT("FormulaHeaderPlusGust", " + "));
		AddSeries(EKawaiiPhysicsWindScopeComponent::Gust);

		return FormulaLegend;
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
	case EKawaiiPhysicsWindScopeComponent::Constant:
		return bConstant;
	case EKawaiiPhysicsWindScopeComponent::Sway:
		return bSway;
	case EKawaiiPhysicsWindScopeComponent::Ripple:
		return bRipple;
	case EKawaiiPhysicsWindScopeComponent::StrengthCycle:
		return bStrengthCycle;
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
	case EKawaiiPhysicsWindScopeComponent::Constant:
		bConstant = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Sway:
		bSway = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::Ripple:
		bRipple = bVisible;
		break;
	case EKawaiiPhysicsWindScopeComponent::StrengthCycle:
		bStrengthCycle = bVisible;
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

void SKawaiiPhysicsWindScopeGraph::SetActiveEditGuide(TOptional<FName> PropertyName)
{
	ActiveEditGuide = PropertyName;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKawaiiPhysicsWindScopeGraph::SetEditValues(const FKawaiiPhysicsWindScopeEditValues* InEditValues)
{
	EditValues = InEditValues;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKawaiiPhysicsWindScopeGraph::SetGhostSamples(TArray<FVector2D> InGhostSamples)
{
	GhostSamples = MoveTemp(InGhostSamples);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKawaiiPhysicsWindScopeGraph::SetPaused(bool bInPaused)
{
	bPaused = bInPaused;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SKawaiiPhysicsWindScopeGraph::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	HoverMousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Unhandled();
}

void SKawaiiPhysicsWindScopeGraph::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	(void)MouseEvent;
	HoverMousePosition.Reset();
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
	const FVector2D GraphOrigin(KawaiiPhysicsWindScopeWindowPrivate::WindScopeGraphPaddingLeft, KawaiiPhysicsWindScopeWindowPrivate::WindScopeGraphPaddingTop);
	const FVector2D GraphSize(
		FMath::Max(LocalSize.X - KawaiiPhysicsWindScopeWindowPrivate::WindScopeGraphPaddingLeft - KawaiiPhysicsWindScopeWindowPrivate::WindScopeGraphPaddingRight, 1.0f),
		FMath::Max(LocalSize.Y - KawaiiPhysicsWindScopeWindowPrivate::WindScopeGraphPaddingTop - KawaiiPhysicsWindScopeWindowPrivate::WindScopeGraphPaddingBottom, 1.0f));
	const KawaiiPhysicsWindScopeWindowPrivate::FKawaiiPhysicsWindScopeRange Range = KawaiiPhysicsWindScopeWindowPrivate::ComputeWindScopeRange(Samples, DisplaySeconds, Visibility);
	const float StrengthCycleAxisMin = 0.0f;
	const float StrengthCycleAxisMax = KawaiiPhysicsWindScopeWindowPrivate::ComputeStrengthCycleAxisMax(Samples, Range.MinTime, EditValues);
	const FLinearColor GridColor(0.22f, 0.24f, 0.28f, 0.55f);
	const FLinearColor AxisColor(0.52f, 0.56f, 0.62f, 0.9f);
	const FSlateFontInfo AxisFont(FCoreStyle::GetDefaultFont(), 9);

	// グリッド線（縦横4分割）を描画
	for (int32 GridIndex = 0; GridIndex <= 4; ++GridIndex)
	{
		const float X = GraphOrigin.X + GraphSize.X * (static_cast<float>(GridIndex) / 4.0f);
		TArray<FVector2D> VerticalLine;
		VerticalLine.Reserve(2);
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
		HorizontalLine.Reserve(2);
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
		ZeroLine.Reserve(2);
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

	// 各波形成分のポリラインを描画（StrengthCycle等 bDashed 指定の系列は破線）
	for (const FKawaiiPhysicsWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
	{
		if (!Visibility.IsVisible(Style.Component))
		{
			continue;
		}

		const bool bStrengthCycleSeries = Style.Component == EKawaiiPhysicsWindScopeComponent::StrengthCycle;
		const TArray<FVector2D> Points = KawaiiPhysicsWindScopeWindowPrivate::BuildWindScopePolylinePoints(
			Samples,
			Style.Component,
			Range.MinTime,
			Range.MaxTime,
			bStrengthCycleSeries ? StrengthCycleAxisMin : Range.MinValue,
			bStrengthCycleSeries ? StrengthCycleAxisMax : Range.MaxValue,
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
			KawaiiPhysicsWindScopeWindowPrivate::DrawWindScopeDashedLine(OutDrawElements, LayerId + 3, AllottedGeometry, Points, DrawColor, DrawThickness);
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

	if (GhostSamples.Num() >= 2)
	{
		const TArray<FVector2D> GhostPoints = KawaiiPhysicsWindScopeWindowPrivate::BuildWindScopeGhostPolylinePoints(
			GhostSamples,
			Range.MinTime,
			Range.MaxTime,
			Range.MinValue,
			Range.MaxValue,
			GraphOrigin,
			GraphSize);
		if (GhostPoints.Num() >= 2)
		{
			const FPaintGeometry GhostClipGeometry = AllottedGeometry.ToPaintGeometry(
				GraphSize,
				FSlateLayoutTransform(GraphOrigin));
			// ゴーストはYレンジ超過時にプロット矩形外へはみ出しうるためクリップする
			OutDrawElements.PushClip(FSlateClippingZone(GhostClipGeometry));
			KawaiiPhysicsWindScopeWindowPrivate::DrawWindScopeDashedLine(
				OutDrawElements,
				LayerId + 4,
				AllottedGeometry,
				GhostPoints,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.5f),
				1.5f);
			OutDrawElements.PopClip();
		}
	}

	if (ActiveEditGuide.IsSet() && EditValues && EditValues->bValid)
	{
		const FName PropertyName = ActiveEditGuide.GetValue();
		const float TimeSpan = FMath::Max(Range.MaxTime - Range.MinTime, KINDA_SMALL_NUMBER);
		const float ValueSpan = FMath::Max(Range.MaxValue - Range.MinValue, KINDA_SMALL_NUMBER);
		const float StrengthCycleValueSpan = FMath::Max(StrengthCycleAxisMax - StrengthCycleAxisMin, KINDA_SMALL_NUMBER);
		const float GraphBottom = GraphOrigin.Y + GraphSize.Y;
		const float GraphRight = GraphOrigin.X + GraphSize.X;
		const FLinearColor GuideLineColor(1.0f, 0.74f, 0.16f, 0.5f);
		const FLinearColor GuideBandColor(1.0f, 0.74f, 0.16f, 0.12f);

		const auto GetFloatEditValue = [this](const FName InPropertyName, float& OutValue)
		{
			if (const float* Value = EditValues->FloatValues.Find(InPropertyName))
			{
				OutValue = *Value;
				return true;
			}
			return false;
		};
		const auto GetIntervalEditValue = [this](const FName InPropertyName, FFloatInterval& OutValue)
		{
			if (const FFloatInterval* Value = EditValues->IntervalValues.Find(InPropertyName))
			{
				OutValue = *Value;
				return true;
			}
			return false;
		};
		const auto TimeToX = [&](const float Time)
		{
			return GraphOrigin.X + GraphSize.X * ((Time - Range.MinTime) / TimeSpan);
		};
		const auto ValueToY = [&](const float Value)
		{
			return GraphOrigin.Y + GraphSize.Y * (1.0f - ((Value - Range.MinValue) / ValueSpan));
		};
		const auto StrengthCycleValueToY = [&](const float Value)
		{
			return GraphOrigin.Y + GraphSize.Y * (1.0f - ((Value - StrengthCycleAxisMin) / StrengthCycleValueSpan));
		};
		const auto DrawVerticalGuideLine = [&](const float Time, const FLinearColor& Color)
		{
			const float X = TimeToX(Time);
			if (X < GraphOrigin.X || X > GraphRight)
			{
				return;
			}

			TArray<FVector2D> GuideLine;
			GuideLine.Reserve(2);
			GuideLine.Add(FVector2D(X, GraphOrigin.Y));
			GuideLine.Add(FVector2D(X, GraphBottom));
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 5,
				AllottedGeometry.ToPaintGeometry(),
				GuideLine,
				ESlateDrawEffect::None,
				Color,
				true,
				1.0f);
		};
		const auto DrawHorizontalGuideLine = [&](const float Value, const FLinearColor& Color)
		{
			const float Y = ValueToY(Value);
			if (Y < GraphOrigin.Y || Y > GraphBottom)
			{
				return;
			}

			TArray<FVector2D> GuideLine;
			GuideLine.Reserve(2);
			GuideLine.Add(FVector2D(GraphOrigin.X, Y));
			GuideLine.Add(FVector2D(GraphRight, Y));
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 5,
				AllottedGeometry.ToPaintGeometry(),
				GuideLine,
				ESlateDrawEffect::None,
				Color,
				true,
				1.0f);
		};
		const auto DrawStrengthCycleHorizontalGuideLine = [&](const float Value, const FLinearColor& Color)
		{
			const float Y = StrengthCycleValueToY(Value);
			if (Y < GraphOrigin.Y || Y > GraphBottom)
			{
				return;
			}

			TArray<FVector2D> GuideLine;
			GuideLine.Reserve(2);
			GuideLine.Add(FVector2D(GraphOrigin.X, Y));
			GuideLine.Add(FVector2D(GraphRight, Y));
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 5,
				AllottedGeometry.ToPaintGeometry(),
				GuideLine,
				ESlateDrawEffect::None,
				Color,
				true,
				1.0f);
		};
		const auto DrawPeriodGuideLines = [&](const float Period)
		{
			if (Period <= KINDA_SMALL_NUMBER)
			{
				return;
			}

			const int32 GuideCount = FMath::Min(50, FMath::FloorToInt(TimeSpan / Period) + 1);
			for (int32 GuideIndex = 0; GuideIndex < GuideCount; ++GuideIndex)
			{
				DrawVerticalGuideLine(Range.MinTime + Period * GuideIndex, GuideLineColor);
			}
		};
		const auto DrawPhaseGuideLine = [&](const float PhaseDegrees, const float Period)
		{
			if (Period <= KINDA_SMALL_NUMBER)
			{
				return;
			}

			float PhaseTime = FMath::Fmod(PhaseDegrees / 360.0f * Period, Period);
			if (PhaseTime < 0.0f)
			{
				PhaseTime += Period;
			}
			DrawVerticalGuideLine(Range.MinTime + PhaseTime, GuideLineColor);
		};

		if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCycleRange))
		{
			FFloatInterval StrengthCycleRange;
			if (GetIntervalEditValue(PropertyName, StrengthCycleRange))
			{
				const float MaxY = StrengthCycleValueToY(StrengthCycleRange.Max);
				const float MinY = StrengthCycleValueToY(StrengthCycleRange.Min);
				const float BandTop = FMath::Clamp(FMath::Min(MaxY, MinY), GraphOrigin.Y, GraphBottom);
				const float BandBottom = FMath::Clamp(FMath::Max(MaxY, MinY), GraphOrigin.Y, GraphBottom);
				if (BandBottom > BandTop)
				{
					FSlateDrawElement::MakeBox(
						OutDrawElements,
						LayerId + 4,
						AllottedGeometry.ToPaintGeometry(
							FVector2D(GraphSize.X, BandBottom - BandTop),
							FSlateLayoutTransform(FVector2D(GraphOrigin.X, BandTop))),
						WhiteBrush,
					ESlateDrawEffect::None,
					GuideBandColor);
				}
				DrawStrengthCycleHorizontalGuideLine(StrengthCycleRange.Max, GuideLineColor);
				DrawStrengthCycleHorizontalGuideLine(StrengthCycleRange.Min, GuideLineColor);
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, ConstantForce))
		{
			float ConstantForce = 0.0f;
			if (GetFloatEditValue(PropertyName, ConstantForce))
			{
				DrawHorizontalGuideLine(ConstantForce, GuideLineColor);
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayPeriod) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePeriod) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForcePeriod) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirectionNoisePeriod) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePeriod))
		{
			float Period = 0.0f;
			if (GetFloatEditValue(PropertyName, Period))
			{
				DrawPeriodGuideLines(Period);
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePhaseOffset))
		{
			float Phase = 0.0f;
			float Period = 0.0f;
			if (GetFloatEditValue(PropertyName, Phase) &&
				GetFloatEditValue(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePeriod), Period))
			{
				DrawPhaseGuideLine(Phase, Period);
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePhaseOffset))
		{
			float Phase = 0.0f;
			float Period = 0.0f;
			if (GetFloatEditValue(PropertyName, Phase) &&
				GetFloatEditValue(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePeriod), Period))
			{
				DrawPhaseGuideLine(Phase, Period);
			}
		}
	}

	// 軸ラベル（左=Force、右=StrengthCycle、下=時間）を描画
	const auto DrawAxisText = [&](const FString& Text, const FVector2D& Position, const FLinearColor& TextColor)
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 6,
			AllottedGeometry.ToPaintGeometry(FVector2D(80.0f, 14.0f), FSlateLayoutTransform(Position)),
			Text,
			AxisFont,
			ESlateDrawEffect::None,
			TextColor);
	};
	const auto DrawLeftAxisText = [&](const FString& Text, const FVector2D& Position)
	{
		DrawAxisText(Text, Position, AxisColor);
	};

	DrawLeftAxisText(FString::Printf(TEXT("%.2f"), Range.MaxValue), FVector2D(2.0f, GraphOrigin.Y - 2.0f));
	if (Range.MinValue <= 0.0f && Range.MaxValue >= 0.0f)
	{
		const float ZeroLabelY = GraphOrigin.Y + GraphSize.Y * (1.0f - ((0.0f - Range.MinValue) / (Range.MaxValue - Range.MinValue)));
		DrawLeftAxisText(TEXT("0.00"), FVector2D(2.0f, ZeroLabelY - 7.0f));
	}
	DrawLeftAxisText(FString::Printf(TEXT("%.2f"), Range.MinValue), FVector2D(2.0f, GraphOrigin.Y + GraphSize.Y - 14.0f));

	for (int32 LabelIndex = 0; LabelIndex <= 4; ++LabelIndex)
	{
		const float LabelAlpha = static_cast<float>(LabelIndex) / 4.0f;
		const float X = GraphOrigin.X + GraphSize.X * LabelAlpha;
		// 右端＝現在時刻(0s)となる相対時間表記（例: -8s, -6s, -4s, -2s, 0s）
		const float Seconds = -DisplaySeconds * (1.0f - LabelAlpha);
		const float LabelWidth = 44.0f;
		const float TextX = FMath::Clamp(X - LabelWidth * 0.5f, GraphOrigin.X, GraphOrigin.X + GraphSize.X - LabelWidth);
		DrawLeftAxisText(
			KawaiiPhysicsWindScopeWindowPrivate::FormatWindScopeSecondsLabel(Seconds),
			FVector2D(TextX, GraphOrigin.Y + GraphSize.Y + 6.0f));
	}

	// 右軸は StrengthCycle（倍率）専用なので系列色で描き、目盛は「×N」表記で倍率であることを示す
	FLinearColor StrengthCycleAxisColor = KawaiiPhysicsWindScopeWindowPrivate::ResolveBaseComponentColor(EKawaiiPhysicsWindScopeComponent::StrengthCycle);
	const bool bStrengthCycleHighlighted = HighlightSeries.IsSet() && HighlightSeries.GetValue() == EKawaiiPhysicsWindScopeComponent::StrengthCycle;
	StrengthCycleAxisColor.A *= bStrengthCycleHighlighted ? 1.0f : 0.75f;
	const auto DrawStrengthCycleAxisText = [&](float Value)
	{
		const float ValueSpan = FMath::Max(StrengthCycleAxisMax - StrengthCycleAxisMin, KINDA_SMALL_NUMBER);
		const float Y = GraphOrigin.Y + GraphSize.Y * (1.0f - ((Value - StrengthCycleAxisMin) / ValueSpan));
		DrawAxisText(
			TEXT("×") + KawaiiPhysicsWindScopeWindowPrivate::FormatWindScopeAxisNumber(Value),
			FVector2D(GraphOrigin.X + GraphSize.X + 4.0f, Y - 7.0f),
			StrengthCycleAxisColor);
	};
	if (StrengthCycleAxisMax <= 5.0f)
	{
		for (int32 Tick = 0; Tick <= FMath::RoundToInt(StrengthCycleAxisMax); ++Tick)
		{
			DrawStrengthCycleAxisText(static_cast<float>(Tick));
		}
	}
	else
	{
		DrawStrengthCycleAxisText(0.0f);
		DrawStrengthCycleAxisText(StrengthCycleAxisMax * 0.5f);
		DrawStrengthCycleAxisText(StrengthCycleAxisMax);
	}

	struct FCursorTextLine
	{
		FString Text;
		FLinearColor Color;

		FCursorTextLine(const FString& InText, const FLinearColor& InColor)
			: Text(InText)
			, Color(InColor)
		{
		}
	};

	const float GraphRight = GraphOrigin.X + GraphSize.X;
	const float GraphBottom = GraphOrigin.Y + GraphSize.Y;
	bool bHoveringGraphPlot = false;
	const auto BuildCursorTextLines = [&](const int32 SampleIndex)
	{
		TArray<FCursorTextLine> CursorTextLines;
		CursorTextLines.Reserve(1 + GetWindScopeComponentStyles().Num());
		CursorTextLines.Emplace(
			FString::Printf(TEXT("t=%.2fs"), Samples[SampleIndex].Time - Range.MaxTime),
			FLinearColor(1.0f, 1.0f, 1.0f, 0.92f));
		for (const FKawaiiPhysicsWindScopeComponentStyle& Style : GetWindScopeComponentStyles())
		{
			if (!Visibility.IsVisible(Style.Component))
			{
				continue;
			}

			FLinearColor TextColor = Style.Color;
			TextColor.A = FMath::Max(TextColor.A, 0.9f);
			const FString LabelString = Style.Label.ToString();
			CursorTextLines.Emplace(
				FString::Printf(TEXT("%s %.2f"), *LabelString, KawaiiPhysicsWindScopeWindowPrivate::GetWindScopeComponentValue(Samples[SampleIndex].Sample, Style.Component)),
				TextColor);
		}
		return CursorTextLines;
	};
	const auto DrawSampleReadout = [&](const FVector2D& AnchorPosition, const int32 SampleIndex)
	{
		const TArray<FCursorTextLine> CursorTextLines = BuildCursorTextLines(SampleIndex);
		const FSlateFontInfo CursorFont(FCoreStyle::GetDefaultFont(), 9);
		constexpr float CursorTextWidth = 128.0f;
		constexpr float CursorTextHeight = 13.0f;
		const float PreferredTextX = AnchorPosition.X + CursorTextWidth + 8.0f > GraphRight
			                           ? AnchorPosition.X - CursorTextWidth - 8.0f
			                           : AnchorPosition.X + 8.0f;
		const float TextX = FMath::Clamp(
			PreferredTextX,
			GraphOrigin.X,
			FMath::Max(GraphOrigin.X, GraphRight - CursorTextWidth));
		const float TextY = FMath::Clamp(
			AnchorPosition.Y - CursorTextHeight * CursorTextLines.Num() - 4.0f,
			GraphOrigin.Y,
			FMath::Max(GraphOrigin.Y, GraphBottom - CursorTextHeight * CursorTextLines.Num()));
		for (int32 LineIndex = 0; LineIndex < CursorTextLines.Num(); ++LineIndex)
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 8,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(CursorTextWidth, CursorTextHeight),
					FSlateLayoutTransform(FVector2D(TextX, TextY + CursorTextHeight * LineIndex))),
				CursorTextLines[LineIndex].Text,
				CursorFont,
				ESlateDrawEffect::None,
				CursorTextLines[LineIndex].Color);
		}
	};

	if (HoverMousePosition.IsSet() && Samples.Num() > 0)
	{
		const FVector2D CursorPosition = HoverMousePosition.GetValue();
		if (CursorPosition.X >= GraphOrigin.X && CursorPosition.X <= GraphRight &&
			CursorPosition.Y >= GraphOrigin.Y && CursorPosition.Y <= GraphBottom)
		{
			bHoveringGraphPlot = true;
			const float TimeSpan = FMath::Max(Range.MaxTime - Range.MinTime, KINDA_SMALL_NUMBER);
			const float CursorTime = Range.MinTime + TimeSpan * ((CursorPosition.X - GraphOrigin.X) / GraphSize.X);

			int32 LowerIndex = 0;
			int32 UpperIndex = Samples.Num();
			while (LowerIndex < UpperIndex)
			{
				const int32 MiddleIndex = LowerIndex + (UpperIndex - LowerIndex) / 2;
				if (Samples[MiddleIndex].Time < CursorTime)
				{
					LowerIndex = MiddleIndex + 1;
				}
				else
				{
					UpperIndex = MiddleIndex;
				}
			}

			int32 ClosestIndex = FMath::Clamp(LowerIndex, 0, Samples.Num() - 1);
			if (ClosestIndex > 0 &&
				FMath::Abs(Samples[ClosestIndex - 1].Time - CursorTime) < FMath::Abs(Samples[ClosestIndex].Time - CursorTime))
			{
				--ClosestIndex;
			}

			TArray<FVector2D> CursorLine;
			CursorLine.Reserve(2);
			CursorLine.Add(FVector2D(CursorPosition.X, GraphOrigin.Y));
			CursorLine.Add(FVector2D(CursorPosition.X, GraphBottom));
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 7,
				AllottedGeometry.ToPaintGeometry(),
				CursorLine,
				ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.32f),
				true,
				1.0f);

			DrawSampleReadout(CursorPosition, ClosestIndex);
		}
	}

	if (bPaused && Samples.Num() > 0 && !bHoveringGraphPlot)
	{
		const int32 LatestIndex = Samples.Num() - 1;
		const float ValueSpan = FMath::Max(Range.MaxValue - Range.MinValue, KINDA_SMALL_NUMBER);
		const float TotalY = GraphOrigin.Y + GraphSize.Y * (1.0f - ((Samples[LatestIndex].Sample.Total - Range.MinValue) / ValueSpan));
		DrawSampleReadout(FVector2D(GraphRight, FMath::Clamp(TotalY, GraphOrigin.Y, GraphBottom)), LatestIndex);
	}

	return LayerId + 9;
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

	const FToolBarStyle& SlimToolBarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>(TEXT("SlimToolbar"));

	ChildSlot
	[
		SNew(SVerticalBox)
		// 上段: Wind Scope 操作用のスリムツールバー
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
				.Padding(FMargin(8.0f, 4.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(PresetComboButton, SComboButton)
						.ComboButtonStyle(&SlimToolBarStyle.ComboButtonStyle)
						.ContentPadding(FMargin(6.0f, 2.0f))
						.ToolTipText(LOCTEXT("PresetRowLabelTooltip", "Click a preset to apply it to the current wind parameters. Hover to preview it on the graph."))
						.OnGetMenuContent(this, &SKawaiiPhysicsWindScopeWindow::GeneratePresetMenu)
						.OnMenuOpenChanged(this, &SKawaiiPhysicsWindScopeWindow::OnPresetMenuOpenChanged)
						.ButtonContent()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush(TEXT("Icons.Settings")))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PresetToolbarButton", "Presets"))
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&SlimToolBarStyle.ButtonStyle)
						.ContentPadding(FMargin(6.0f, 2.0f))
						.Text(LOCTEXT("GustButton", "Test Gust"))
						.ToolTipText(LOCTEXT("GustButtonTooltip", "Sends a one-shot test gust to the live target. Does not change any parameters."))
						.OnClicked_Lambda([this]()
						{
							const bool bAppliedLive = PushGustToLiveRuntime(
								GustStrength,
								GustRiseTime,
								GustDecayTime);
							// 成功時は通知を出さない: 連打時に右下ポップアップがエディタを重くし、波形の目視確認を妨げるため。
							// 失敗時（ライブ対象なし）のみ理由を通知する
							if (!bAppliedLive)
							{
								KawaiiPhysicsEdWindowUtils::ShowNotification(
									LOCTEXT("GustNoLiveTarget", "Skipped wind gust. (no live target)"),
									SNotificationItem::CS_Fail);
							}
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SComboButton)
						.ComboButtonStyle(&SlimToolBarStyle.ComboButtonStyle)
						.ContentPadding(FMargin(2.0f, 2.0f))
						.ToolTipText(LOCTEXT("GustSettingsTooltip", "Adjust the test gust settings."))
						.OnGetMenuContent(this, &SKawaiiPhysicsWindScopeWindow::GenerateGustMenu)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(FText::GetEmpty())
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.0f, 0.0f)
					[
						SNew(SSeparator)
						.Orientation(Orient_Vertical)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(&SlimToolBarStyle.ButtonStyle)
						.ContentPadding(FMargin(6.0f, 2.0f))
						.ToolTipText(LOCTEXT("CopyWindParametersTooltip", "Copy current ProceduralWind parameters."))
						.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnCopyWindParametersClicked)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush(TEXT("GenericCommands.Copy")))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("CopyWindParametersButton", "Copy"))
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&SlimToolBarStyle.ButtonStyle)
						.ContentPadding(FMargin(6.0f, 2.0f))
						.ToolTipText(LOCTEXT("PasteWindParametersTooltip", "Paste ProceduralWind parameters from the clipboard."))
						.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnPasteWindParametersClicked)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush(TEXT("GenericCommands.Paste")))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PasteWindParametersButton", "Paste"))
							]
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNullWidget::NullWidget
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
						.ToolTipText(this, &SKawaiiPhysicsWindScopeWindow::GetTargetNodeText)
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
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(8.0f)
							.HeightOverride(8.0f)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush(TEXT("Icons.FilledCircle")))
								.ColorAndOpacity(this, &SKawaiiPhysicsWindScopeWindow::GetModeBadgeColor)
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SKawaiiPhysicsWindScopeWindow::GetModeText)
							.ColorAndOpacity(this, &SKawaiiPhysicsWindScopeWindow::GetModeBadgeColor)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&SlimToolBarStyle.ButtonStyle)
						.ContentPadding(FMargin(3.0f))
						.ToolTipText(LOCTEXT("ToggleEditPanelTooltip", "Toggle the parameter edit panel."))
						.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnToggleEditPanelClicked)
						[
							SNew(SImage)
							.Image(this, &SKawaiiPhysicsWindScopeWindow::GetEditPanelToggleIcon)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
				.Padding(0.0f)
				[
					SNew(SBox)
					.HeightOverride(2.0f)
				]
			]
		]
		// 凡例・波形グラフ本体・編集パネル
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 10.0f, 8.0f, 4.0f)
		[
			SAssignNew(EditPanelSplitter, SSplitter)
			.Orientation(Orient_Horizontal)
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
					KawaiiPhysicsWindScopeWindowPrivate::MakeWindScopeFormulaLegend(this)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SAssignNew(GraphWidget, SKawaiiPhysicsWindScopeGraph)
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SKawaiiPhysicsWindScopeWindow::GetTargetNodeText)
						.Visibility(this, &SKawaiiPhysicsWindScopeWindow::GetTargetNodeEmptyStateVisibility)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 12))
						.Justification(ETextJustify::Center)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DisplayWindowLabel", "Window"))
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
					.Padding(8.0f, 0.0f, 12.0f, 0.0f)
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
			]
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
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SKawaiiPhysicsWindScopeEditPanel)
					.EditValues(this, &SKawaiiPhysicsWindScopeWindow::GetEditValues)
					.LiveEditValues(this, &SKawaiiPhysicsWindScopeWindow::GetLiveEditValues)
					.OnParamEdit(this, &SKawaiiPhysicsWindScopeWindow::ApplyWindParamEdit)
					.OnParamReset(this, &SKawaiiPhysicsWindScopeWindow::ResetWindParamToDefault)
					.OnHighlightSeries(this, &SKawaiiPhysicsWindScopeWindow::SetHighlightSeries)
				]
			]
		]
	];

	if (GraphWidget.IsValid())
	{
		GraphWidget->SetEditValues(&CachedEditValues);
		GraphWidget->SetPaused(bPaused);
	}

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

void SKawaiiPhysicsWindScopeWindow::OpenWindow(FKawaiiPhysicsWindScopeWindowArgs Args)
{
	if (HasTargetArgs(Args))
	{
		SaveLastTargetArgs(Args);
	}

	TSharedPtr<SDockTab> InvokedTab = KawaiiPhysicsEdWindowUtils::InvokeAnimBlueprintEditorTab(
		Args.AnimBlueprintPath,
		WindScopeTabId,
		LOCTEXT("WindScopeOpenEditorFailed", "Failed to open the Animation Blueprint editor for Wind Scope."));
	if (!InvokedTab.IsValid())
	{
		return;
	}

	TSharedPtr<SWidget> TabContent = InvokedTab->GetContent();
	if (!TabContent.IsValid())
	{
		return;
	}

	if (TabContent->GetType() == FName(TEXT("SKawaiiPhysicsWindScopeWindow")))
	{
		TSharedPtr<SKawaiiPhysicsWindScopeWindow> WindowWidget =
			StaticCastSharedPtr<SKawaiiPhysicsWindScopeWindow>(TabContent);
		WindowWidget->SetOwnerTab(InvokedTab.ToSharedRef());
		WindowWidget->ClearPendingReconnect();
		WindowWidget->SetArgs(MoveTemp(Args));
		return;
	}

	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("WindScopeTabContentInvalid", "Failed to update the Wind Scope tab content."),
		SNotificationItem::CS_Fail);
}

void SKawaiiPhysicsWindScopeWindow::CloseAllWindows()
{
	TArray<TSharedPtr<SDockTab>> TabsToClose;
	for (const TWeakPtr<SKawaiiPhysicsWindScopeWindow>& WeakWindow : KawaiiPhysicsWindScopeWindowPrivate::LiveWindScopeWindows)
	{
		if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Window = WeakWindow.Pin())
		{
			if (TSharedPtr<SDockTab> OwnerTab = Window->OwnerTabWeak.Pin())
			{
				TabsToClose.Add(OwnerTab);
			}
		}
	}

	for (const TSharedPtr<SDockTab>& Tab : TabsToClose)
	{
		Tab->RequestCloseTab();
	}
	KawaiiPhysicsWindScopeWindowPrivate::LiveWindScopeWindows.Reset();
}

void SKawaiiPhysicsWindScopeWindow::SetOwnerTab(TSharedRef<SDockTab> InOwnerTab)
{
	OwnerTabWeak = InOwnerTab;
	KawaiiPhysicsWindScopeWindowPrivate::RegisterLiveWindow(
		StaticCastSharedRef<SKawaiiPhysicsWindScopeWindow>(AsShared()));
}

void SKawaiiPhysicsWindScopeWindow::SetArgs(FKawaiiPhysicsWindScopeWindowArgs InArgs)
{
	// 対象引数を差し替え、表示状態をリセットして外力一覧を再構築する
	ResolvedGraphNodeCache.Reset();
	Args = MoveTemp(InArgs);
	DisplaySamples.Reset();
	// Pause 中でも旧波形を残さないため即時反映
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetSamples(DisplaySamples);
	}
	PreviewTime = 0.0f;
	LastLiveSampleCount = 0;
	CachedLiveEditValues = FKawaiiPhysicsWindScopeEditValues();
	bHasDragStartWind = false;
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetActiveEditGuide(TOptional<FName>());
		GraphWidget->SetGhostSamples(TArray<FVector2D>());
	}
	CurrentModeText = LOCTEXT("PreviewMode", "Preview");
	bIsLiveMode = false;
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
	// Pause 中でも旧波形を残さないため即時反映
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetSamples(DisplaySamples);
	}
	LastLiveSampleCount = 0;
	PreviewTime = 0.0f;
	CachedLiveEditValues = FKawaiiPhysicsWindScopeEditValues();
	bHasDragStartWind = false;
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetActiveEditGuide(TOptional<FName>());
		GraphWidget->SetGhostSamples(TArray<FVector2D>());
	}
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
		return LOCTEXT("NoTargetNodeGuidance", "No node selected: open from [Wind Scope] on a KawaiiPhysics node.");
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
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetPaused(bPaused);
	}
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
	return FAppStyle::Get().GetBrush(IsEditPanelExpanded() ? TEXT("Icons.ChevronRight") : TEXT("Icons.ChevronLeft"));
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

FSlateColor SKawaiiPhysicsWindScopeWindow::GetModeBadgeColor() const
{
	if (bIsLiveMode)
	{
		return FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentGreen"));
	}

	return FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 0.5f));
}

EVisibility SKawaiiPhysicsWindScopeWindow::GetTargetNodeEmptyStateVisibility() const
{
	return ResolveGraphNode() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

bool SKawaiiPhysicsWindScopeWindow::IsWindEditable() const
{
	return ResolveGraphNode() != nullptr;
}

const FKawaiiPhysicsWindScopeEditValues* SKawaiiPhysicsWindScopeWindow::GetEditValues() const
{
	return &CachedEditValues;
}

const FKawaiiPhysicsWindScopeEditValues* SKawaiiPhysicsWindScopeWindow::GetLiveEditValues() const
{
	return CachedLiveEditValues.bValid ? &CachedLiveEditValues : nullptr;
}

void SKawaiiPhysicsWindScopeWindow::SetHighlightSeries(TOptional<EKawaiiPhysicsWindScopeComponent> InHighlightSeries)
{
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetHighlightSeries(InHighlightSeries);
	}
}

bool SKawaiiPhysicsWindScopeWindow::IsSeriesActive(EKawaiiPhysicsWindScopeComponent Component) const
{
	const bool bGustActive = DisplaySamples.Num() > 0 && !FMath::IsNearlyZero(DisplaySamples.Last().Sample.Gust);
	// Live 値が有効な間は runtime 側（DynamicParams 反映後）で判定し、グラフの live 波形と凡例/式ヘッダの淡色表示を一致させる
	const FKawaiiPhysicsWindScopeEditValues* Values = CachedLiveEditValues.bValid ? &CachedLiveEditValues : &CachedEditValues;
	return KawaiiPhysicsWindScopeWindowPrivate::IsSeriesActiveFromValues(Values, Component, bGustActive);
}

FLinearColor SKawaiiPhysicsWindScopeWindow::ResolveSeriesDisplayColor(
	EKawaiiPhysicsWindScopeComponent Component) const
{
	const FLinearColor Color = KawaiiPhysicsWindScopeWindowPrivate::ResolveBaseComponentColor(Component);
	return IsSeriesActive(Component) ? Color : KawaiiPhysicsWindScopeWindowPrivate::MakeInactiveSeriesColor(Color);
}

void SKawaiiPhysicsWindScopeWindow::RebuildPresetButtons()
{
	CachedPresets = KawaiiPhysicsWindScopeWindowPrivate::ResolveWindScopePresets();

	if (GraphWidget.IsValid())
	{
		GraphWidget->SetGhostSamples(TArray<FVector2D>());
	}
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeWindow::GeneratePresetMenu()
{
	RebuildPresetButtons();

	// ポップアップはタブ破棄後も生存しうるため弱参照で捕捉
	const TWeakPtr<SKawaiiPhysicsWindScopeWindow> WeakSelf = SharedThis(this);

	TSharedRef<SVerticalBox> PresetMenuBox = SNew(SVerticalBox);
	for (int32 PresetIndex = 0; PresetIndex < CachedPresets.Num(); ++PresetIndex)
	{
		const FText PresetDisplayName =
			KawaiiPhysicsWindScopeWindowPrivate::ResolveWindPresetDisplayName(CachedPresets[PresetIndex], PresetIndex);
		PresetMenuBox->AddSlot()
		.AutoHeight()
		[
			SNew(SButton)
			.Text(PresetDisplayName)
			.HAlign(HAlign_Left)
			.ToolTipText(FText::Format(
				LOCTEXT("PresetButtonTooltipFormat", "Apply the \"{0}\" wind preset to the current parameters.\nHover to preview it on the graph."),
				PresetDisplayName))
			.OnClicked(this, &SKawaiiPhysicsWindScopeWindow::OnPresetButtonClicked, PresetIndex)
			.OnHovered_Lambda([WeakSelf, PresetIndex]()
			{
				if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
				{
					Self->OnPresetButtonHovered(PresetIndex);
				}
			})
			.OnUnhovered_Lambda([WeakSelf]()
			{
				if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
				{
					Self->OnPresetButtonUnhovered();
				}
			})
		];
	}

	if (CachedPresets.Num() == 0)
	{
		PresetMenuBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(8.0f, 4.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoPresetsMenuInfo", "No wind presets found."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}

	PresetMenuBox->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.0f, 4.0f))
	[
		SNew(SSeparator)
	];

	PresetMenuBox->AddSlot()
	.AutoHeight()
	[
		SNew(SComboButton)
		.ContentPadding(FMargin(8.0f, 3.0f))
		.ToolTipText(this, &SKawaiiPhysicsWindScopeWindow::GetSavePresetToolTipText)
		.IsEnabled(this, &SKawaiiPhysicsWindScopeWindow::CanSaveWindPreset)
		.OnGetMenuContent(this, &SKawaiiPhysicsWindScopeWindow::GenerateSavePresetMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SavePresetButton", "Save as Preset"))
		]
	];

	PresetMenuBox->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
	[
		SNew(SButton)
		.Text(LOCTEXT("ReloadPresetsButton", "Reload"))
		.HAlign(HAlign_Left)
		.ContentPadding(FMargin(8.0f, 3.0f))
		.ToolTipText(LOCTEXT("ReloadPresetsTooltip", "Reload the preset DataAsset."))
		.OnClicked_Lambda([WeakSelf]()
		{
			if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
			{
				Self->RebuildPresetButtons();
				if (Self->PresetComboButton.IsValid())
				{
					Self->PresetComboButton->SetIsOpen(false);
				}
			}
			return FReply::Handled();
		})
	];

	return SNew(SBox)
		.MinDesiredWidth(220.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush(TEXT("Menu.Background")))
			.Padding(2.0f)
			[
				PresetMenuBox
			]
		];
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeWindow::GenerateGustMenu()
{
	// ポップアップはタブ破棄後も生存しうるため弱参照で捕捉
	const TWeakPtr<SKawaiiPhysicsWindScopeWindow> WeakSelf = SharedThis(this);

	return SNew(SBox)
		.MinDesiredWidth(220.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush(TEXT("Menu.Background")))
			.Padding(8.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(56.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GustStrengthLabel", "Strength"))
							.ToolTipText(LOCTEXT("GustStrengthTooltip", "Gust strength."))
							.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.0f)
						.MaxValue(50.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(50.0f)
						.Value_Lambda([WeakSelf]()
						{
							if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
							{
								return Self->GustStrength;
							}
							return 0.0f;
						})
						.OnValueChanged_Lambda([WeakSelf](float NewValue)
						{
							if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
							{
								Self->GustStrength = FMath::Clamp(NewValue, 0.0f, 50.0f);
							}
						})
						.ToolTipText(LOCTEXT("GustStrengthSpinTooltip", "Gust strength."))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(56.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GustRiseLabel", "Rise"))
							.ToolTipText(LOCTEXT("GustRiseTooltip", "Gust rise time."))
							.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.01f)
						.MaxValue(5.0f)
						.MinSliderValue(0.01f)
						.MaxSliderValue(5.0f)
						.Value_Lambda([WeakSelf]()
						{
							if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
							{
								return Self->GustRiseTime;
							}
							return 0.0f;
						})
						.OnValueChanged_Lambda([WeakSelf](float NewValue)
						{
							if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
							{
								Self->GustRiseTime = FMath::Clamp(NewValue, 0.01f, 5.0f);
							}
						})
						.ToolTipText(LOCTEXT("GustRiseSpinTooltip", "Gust rise time."))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(56.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GustDecayLabel", "Decay"))
							.ToolTipText(LOCTEXT("GustDecayTooltip", "Gust decay time."))
							.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.01f)
						.MaxValue(10.0f)
						.MinSliderValue(0.01f)
						.MaxSliderValue(10.0f)
						.Value_Lambda([WeakSelf]()
						{
							if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
							{
								return Self->GustDecayTime;
							}
							return 0.0f;
						})
						.OnValueChanged_Lambda([WeakSelf](float NewValue)
						{
							if (TSharedPtr<SKawaiiPhysicsWindScopeWindow> Self = WeakSelf.Pin())
							{
								Self->GustDecayTime = FMath::Clamp(NewValue, 0.01f, 10.0f);
							}
						})
						.ToolTipText(LOCTEXT("GustDecaySpinTooltip", "Gust decay time."))
					]
				]
			]
		];
}

FReply SKawaiiPhysicsWindScopeWindow::OnPresetButtonClicked(int32 PresetIndex)
{
	if (!CachedPresets.IsValidIndex(PresetIndex))
	{
		return FReply::Handled();
	}

	FReply Reply = ApplyPreset(CachedPresets[PresetIndex]);
	OnPresetButtonUnhovered();
	if (PresetComboButton.IsValid())
	{
		PresetComboButton->SetIsOpen(false);
	}
	return Reply;
}

void SKawaiiPhysicsWindScopeWindow::OnPresetButtonHovered(int32 PresetIndex)
{
	if (!GraphWidget.IsValid())
	{
		return;
	}

	if (!CachedPresets.IsValidIndex(PresetIndex))
	{
		GraphWidget->SetGhostSamples(TArray<FVector2D>());
		return;
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind PreviewWind;
	if (!TryGetPreviewForceCopy(PreviewWind))
	{
		GraphWidget->SetGhostSamples(TArray<FVector2D>());
		return;
	}

	PreviewWind.ApplyDynamicParams(CachedPresets[PresetIndex].ToDynamicParams());

	TArray<FVector2D> NewGhostSamples;
	NewGhostSamples.Reserve(KawaiiPhysicsWindScopeWindowPrivate::WindScopePreviewSampleCount);
	const float EndTime = DisplaySamples.Num() > 0 ? DisplaySamples.Last().Time : PreviewTime;
	const float StartTime = EndTime - DisplaySeconds;
	const int32 SampleCount = FMath::Max(KawaiiPhysicsWindScopeWindowPrivate::WindScopePreviewSampleCount, 2);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
		const float SampleTime = FMath::Lerp(StartTime, EndTime, Alpha);
		NewGhostSamples.Add(FVector2D(SampleTime, PreviewWind.ComputeWindSample(SampleTime, 0.0f).Total));
	}

	GraphWidget->SetGhostSamples(MoveTemp(NewGhostSamples));
}

void SKawaiiPhysicsWindScopeWindow::OnPresetButtonUnhovered()
{
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetGhostSamples(TArray<FVector2D>());
	}
}

void SKawaiiPhysicsWindScopeWindow::OnPresetMenuOpenChanged(bool bIsOpen)
{
	// メニューを閉じた際、ホバー中のプレビュー波形（ゴースト）を消し忘れないようにする
	if (!bIsOpen)
	{
		OnPresetButtonUnhovered();
	}
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
	KawaiiPhysicsWindScopeWindowPrivate::MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	// シミュレーションリセット回避のため PostEditChangeProperty / NotifyGraphNodePropertyChanged は呼ばず、
	// ライブ側には PendingParams 経由で同じ値を送る
	const bool bAppliedLive = PushParamsToLiveRuntime(Params);
	SyncEditValuesAfterWrite(Wind);

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
			KawaiiPhysicsWindScopeWindowPrivate::ResolveWindPresetDisplayName(Preset, PresetIndex)),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

TSharedRef<SWidget> SKawaiiPhysicsWindScopeWindow::GenerateSavePresetMenu()
{
	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	if (!Settings || Settings->WindScopePresetDataAsset.IsNull())
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		const FText NoDataAssetText = LOCTEXT("SavePresetNoDataAssetMenuInfo", "Set the Wind Scope Preset Data Asset in Project Settings to save presets.");
		MenuBuilder.AddWidget(
			SNew(SBox)
			.MaxDesiredWidth(320.0f)
			.Padding(FMargin(8.0f, 4.0f))
			[
				SNew(STextBlock)
				.Text(NoDataAssetText)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			],
			FText::GetEmpty());
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenProjectSettingsMenu", "Open Project Settings"),
			LOCTEXT("OpenProjectSettingsTooltip", "Open Project Settings > Plugins > Kawaii Physics."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Settings]()
			{
				if (ISettingsModule* SettingsModule = FModuleManager::LoadModulePtr<ISettingsModule>("Settings"))
				{
					const FName SettingsSectionName = Settings
						                                  ? Settings->GetSectionName()
						                                  : UKawaiiPhysicsDeveloperSettings::StaticClass()->GetFName();
					SettingsModule->ShowViewer("Project", "Plugins", SettingsSectionName);
				}
			})));
		return MenuBuilder.MakeWidget();
	}

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("SavePresetAddNewMenu", "Add New Preset"),
		LOCTEXT("SavePresetAddNewTooltip", "Add current parameters as a new preset."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			SaveCurrentWindAsPreset(INDEX_NONE);
		})));

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("SavePresetOverwriteSection", "Overwrite:"));
	UKawaiiPhysicsWindPresetDataAsset* WritablePresetDataAsset = ResolveWritablePresetDataAsset(false);
	for (int32 PresetIndex = 0; PresetIndex < CachedPresets.Num(); ++PresetIndex)
	{
		MenuBuilder.AddMenuEntry(
			KawaiiPhysicsWindScopeWindowPrivate::ResolveWindPresetDisplayName(CachedPresets[PresetIndex], PresetIndex),
			LOCTEXT("SavePresetOverwriteTooltip", "Overwrite this preset with current parameters."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, PresetIndex]()
				{
					SaveCurrentWindAsPreset(PresetIndex);
				}),
				FCanExecuteAction::CreateLambda([WritablePresetDataAsset, PresetIndex]()
				{
					return WritablePresetDataAsset && WritablePresetDataAsset->Presets.IsValidIndex(PresetIndex);
				})));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

bool SKawaiiPhysicsWindScopeWindow::CanSaveWindPreset() const
{
	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	return Settings && IsWindEditable();
}

FText SKawaiiPhysicsWindScopeWindow::GetSavePresetToolTipText() const
{
	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	if (!Settings || Settings->WindScopePresetDataAsset.IsNull())
	{
		return LOCTEXT("SavePresetNoDataAssetTooltip", "Set the Wind Scope Preset Data Asset in Project Settings to save presets.");
	}

	return LOCTEXT("SavePresetTooltip", "Save current parameters to the preset DataAsset.");
}

UKawaiiPhysicsWindPresetDataAsset* SKawaiiPhysicsWindScopeWindow::ResolveWritablePresetDataAsset(
	bool bShowNotification) const
{
	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	if (!Settings || Settings->WindScopePresetDataAsset.IsNull())
	{
		if (bShowNotification)
		{
			KawaiiPhysicsEdWindowUtils::ShowNotification(
				LOCTEXT("SavePresetNoDataAsset", "Set the Wind Scope Preset Data Asset in Project Settings first."),
				SNotificationItem::CS_Fail);
		}
		return nullptr;
	}

	UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset = Settings->WindScopePresetDataAsset.LoadSynchronous();
	if (!PresetDataAsset && bShowNotification)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("SavePresetLoadDataAssetFailed", "Failed to load the Wind Scope Preset Data Asset."),
			SNotificationItem::CS_Fail);
	}
	return PresetDataAsset;
}

bool SKawaiiPhysicsWindScopeWindow::SaveCurrentWindAsPreset(int32 PresetIndex)
{
	UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset = ResolveWritablePresetDataAsset(true);
	if (!PresetDataAsset)
	{
		return false;
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(GraphNode, ResolvedIndex);
	if (!Wind)
	{
		return false;
	}

	const bool bAddNewPreset = PresetIndex == INDEX_NONE;
	if (!bAddNewPreset && !PresetDataAsset->Presets.IsValidIndex(PresetIndex))
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("SavePresetInvalidOverwriteTarget", "Failed to resolve the wind preset to overwrite."),
			SNotificationItem::CS_Fail);
		return false;
	}
	if (!bAddNewPreset)
	{
		const FKawaiiProceduralWindPreset& TargetPreset = PresetDataAsset->Presets[PresetIndex];
		if (!CachedPresets.IsValidIndex(PresetIndex) ||
			!TargetPreset.PresetName.EqualTo(CachedPresets[PresetIndex].PresetName) ||
			TargetPreset.PresetTag != CachedPresets[PresetIndex].PresetTag)
		{
			KawaiiPhysicsEdWindowUtils::ShowNotification(
				LOCTEXT("SavePresetListChanged", "Preset list has changed. Reload first."),
				SNotificationItem::CS_Fail);
			return false;
		}
	}

	FKawaiiProceduralWindPreset Preset = KawaiiPhysicsWindScopeWindowPrivate::MakeWindPresetFromCurrentWind(*Wind);
	FText SavedPresetName;
	const FScopedTransaction Transaction(LOCTEXT("SaveWindPresetTransaction", "Save Kawaii Physics Wind Preset"));
	PresetDataAsset->Modify();
	if (bAddNewPreset)
	{
		Preset.PresetName = FText::Format(
			LOCTEXT("SavePresetCustomNameFormat", "Custom {0}"),
			FText::AsNumber(PresetDataAsset->Presets.Num() + 1));
		SavedPresetName = Preset.PresetName;
		PresetDataAsset->Presets.Add(Preset);
	}
	else
	{
		FKawaiiProceduralWindPreset& TargetPreset = PresetDataAsset->Presets[PresetIndex];
		SavedPresetName = KawaiiPhysicsWindScopeWindowPrivate::ResolveWindPresetDisplayName(TargetPreset, PresetIndex);
		Preset.PresetTag = TargetPreset.PresetTag;
		Preset.PresetName = TargetPreset.PresetName;
		TargetPreset = Preset;
	}

	PresetDataAsset->MarkPackageDirty();
	Args.ExternalForceIndex = ResolvedIndex;
	RebuildPresetButtons();
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		FText::Format(
			bAddNewPreset
				? LOCTEXT("SavePresetSucceededAdd", "Saved {0} wind preset.")
				: LOCTEXT("SavePresetSucceededOverwrite", "Overwrote {0} wind preset."),
			SavedPresetName),
		SNotificationItem::CS_Success);
	return true;
}

FReply SKawaiiPhysicsWindScopeWindow::OnCopyWindParametersClicked()
{
	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(GraphNode, ResolvedIndex);
	if (!Wind)
	{
		return FReply::Handled();
	}

	FString ExportedText;
	FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct()->ExportText(
		ExportedText,
		Wind,
		nullptr,
		nullptr,
		PPF_None,
		nullptr);
	const FString ClipboardText = FString::Printf(TEXT("%s%s%s"), KawaiiPhysicsWindScopeWindowPrivate::WindScopeClipboardMarker, LINE_TERMINATOR, *ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ClipboardText);
	Args.ExternalForceIndex = ResolvedIndex;
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("CopyWindParametersSucceeded", "Copied ProceduralWind parameters to clipboard."),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

FReply SKawaiiPhysicsWindScopeWindow::OnPasteWindParametersClicked()
{
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
	if (!ClipboardText.StartsWith(KawaiiPhysicsWindScopeWindowPrivate::WindScopeClipboardMarker))
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("PasteWindParametersInvalidClipboard", "Clipboard does not contain ProceduralWind parameters."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	const FString ImportText = ClipboardText.RightChop(FCString::Strlen(KawaiiPhysicsWindScopeWindowPrivate::WindScopeClipboardMarker)).TrimStartAndEnd();

	FKawaiiPhysics_ExternalForce_ProceduralWind PastedWind;
	// UScriptStruct::ImportTextは5.3では非constメンバ関数のため、constを付けずに受け取る。
	UScriptStruct* WindStruct = FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct();
	const TCHAR* ImportResult = WindStruct->ImportText(
		*ImportText,
		&PastedWind,
		nullptr,
		PPF_None,
		nullptr,
		WindStruct->GetName());
	if (!ImportResult)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("PasteWindParametersInvalidClipboard", "Clipboard does not contain ProceduralWind parameters."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(GraphNode, ResolvedIndex);
	if (!Wind)
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("PasteWindParametersTransaction", "Paste Kawaii Physics Wind Parameters"));
	GraphNode->Modify();
	*Wind = PastedWind;
	KawaiiPhysicsWindScopeWindowPrivate::MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	const bool bAppliedLive = PushParamsToLiveRuntime(Wind->BuildDynamicParamsSnapshot());
	SyncEditValuesAfterWrite(Wind);
	RefreshExternalForceItems();
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		bAppliedLive
			? LOCTEXT("PasteWindParametersSucceededLive", "Pasted ProceduralWind parameters. (live)")
			: LOCTEXT("PasteWindParametersSucceededNodeOnly", "Pasted ProceduralWind parameters. (node only — no live target)"),
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
	const int32 ResolvedIndex = KawaiiPhysicsWindScopeWindowPrivate::ResolveProceduralWindIndex(GraphNode->Node, Args.ExternalForceIndex);
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
	FKawaiiPhysics_ExternalForce_ProceduralWind* RuntimeWind = KawaiiPhysicsWindScopeWindowPrivate::ResolveLiveProceduralWind(
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
	FKawaiiPhysics_ExternalForce_ProceduralWind* RuntimeWind = KawaiiPhysicsWindScopeWindowPrivate::ResolveLiveProceduralWind(
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
	EKawaiiPhysicsWindEditPhase Phase)
{
	const auto ClearActiveEditGuide = [this]()
	{
		if (GraphWidget.IsValid())
		{
			GraphWidget->SetActiveEditGuide(TOptional<FName>());
		}
	};
	const auto FailEdit = [&ClearActiveEditGuide]()
	{
		ClearActiveEditGuide();
		return false;
	};
	const bool bHasMatchingDragStart = bHasDragStartWind && DragStartPropertyName == PropertyName;
	const bool bApplyAsCommitted = Phase == EKawaiiPhysicsWindEditPhase::Committed ||
		(Phase == EKawaiiPhysicsWindEditPhase::Interactive && !bHasMatchingDragStart);

	if (GraphWidget.IsValid())
	{
		GraphWidget->SetActiveEditGuide(
			bApplyAsCommitted
				? TOptional<FName>()
				: TOptional<FName>(PropertyName));
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(
		GraphNode,
		ResolvedIndex,
		bApplyAsCommitted);
	if (!Wind)
	{
		return FailEdit();
	}
	Args.ExternalForceIndex = ResolvedIndex;

	FProperty* Property = KawaiiPhysicsWindScopeWindowPrivate::FindProceduralWindProperty(PropertyName);
	if (!Property)
	{
		if (bApplyAsCommitted)
		{
			KawaiiPhysicsEdWindowUtils::ShowNotification(
				LOCTEXT("EditWindParamPropertyMissing", "Failed to resolve the wind parameter property."),
				SNotificationItem::CS_Fail);
		}
		return FailEdit();
	}

	if (Phase == EKawaiiPhysicsWindEditPhase::Begin)
	{
		DragStartWind = *Wind;
		DragStartPropertyName = PropertyName;
		bHasDragStartWind = true;
		return true;
	}

	if (Phase == EKawaiiPhysicsWindEditPhase::Interactive && bHasMatchingDragStart)
	{
		if (!KawaiiPhysicsWindScopeWindowPrivate::SetProceduralWindPropertyValue(*Wind, Property, NewValue, VectorComponentIndex))
		{
			return FailEdit();
		}

		FKawaiiProceduralWindDynamicParams Params;
		if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
		{
			PushParamsToLiveRuntime(Params);
		}
		SyncEditValuesAfterWrite(Wind);
		return true;
	}

	const bool bUseDragStartValue = bHasMatchingDragStart;
	const FKawaiiPhysics_ExternalForce_ProceduralWind& CompareWind = bUseDragStartValue ? DragStartWind : *Wind;
	if (KawaiiPhysicsWindScopeWindowPrivate::IsProceduralWindPropertyValueEqualToEdit(CompareWind, Property, NewValue, VectorComponentIndex))
	{
		if (bUseDragStartValue)
		{
			KawaiiPhysicsWindScopeWindowPrivate::CopyProceduralWindPropertyValue(*Wind, DragStartWind, Property);
			FKawaiiProceduralWindDynamicParams Params;
			if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
			{
				PushParamsToLiveRuntime(Params);
			}
		}
		bHasDragStartWind = false;
		SyncEditValuesAfterWrite(Wind);
		return true;
	}

	if (bUseDragStartValue)
	{
		KawaiiPhysicsWindScopeWindowPrivate::CopyProceduralWindPropertyValue(*Wind, DragStartWind, Property);
	}

	const FScopedTransaction Transaction(LOCTEXT("EditWindParameterTransaction", "Edit Kawaii Physics Wind Parameter"));
	GraphNode->Modify();
	if (!KawaiiPhysicsWindScopeWindowPrivate::SetProceduralWindPropertyValue(*Wind, Property, NewValue, VectorComponentIndex))
	{
		bHasDragStartWind = false;
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("EditWindParamSetFailed", "Failed to update the wind parameter."),
			SNotificationItem::CS_Fail);
		SyncEditValuesAfterWrite(Wind);
		return FailEdit();
	}

	KawaiiPhysicsWindScopeWindowPrivate::MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	bHasDragStartWind = false;

	FKawaiiProceduralWindDynamicParams Params;
	if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
	{
		PushParamsToLiveRuntime(Params);
	}
	SyncEditValuesAfterWrite(Wind);
	return true;
}

void SKawaiiPhysicsWindScopeWindow::FinalizeAbandonedWindDrag()
{
	const auto ClearActiveEditGuide = [this]()
	{
		if (GraphWidget.IsValid())
		{
			GraphWidget->SetActiveEditGuide(TOptional<FName>());
		}
	};

	if (!bHasDragStartWind)
	{
		return;
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(GraphNode, ResolvedIndex, false);
	if (!Wind)
	{
		bHasDragStartWind = false;
		ClearActiveEditGuide();
		return;
	}

	FProperty* Property = KawaiiPhysicsWindScopeWindowPrivate::FindProceduralWindProperty(DragStartPropertyName);
	if (!Property)
	{
		bHasDragStartWind = false;
		ClearActiveEditGuide();
		return;
	}

	if (Property->Identical_InContainer(Wind, &DragStartWind))
	{
		bHasDragStartWind = false;
		ClearActiveEditGuide();
		return;
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind DraggedWind = DragStartWind;
	if (!KawaiiPhysicsWindScopeWindowPrivate::CopyProceduralWindPropertyValue(DraggedWind, *Wind, Property))
	{
		bHasDragStartWind = false;
		ClearActiveEditGuide();
		return;
	}

	KawaiiPhysicsWindScopeWindowPrivate::CopyProceduralWindPropertyValue(*Wind, DragStartWind, Property);

	const FScopedTransaction Transaction(LOCTEXT("EditWindParameterTransaction", "Edit Kawaii Physics Wind Parameter"));
	GraphNode->Modify();
	KawaiiPhysicsWindScopeWindowPrivate::CopyProceduralWindPropertyValue(*Wind, DraggedWind, Property);
	KawaiiPhysicsWindScopeWindowPrivate::MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	bHasDragStartWind = false;
	ClearActiveEditGuide();

	FKawaiiProceduralWindDynamicParams Params;
	if (Wind->BuildDynamicParamsForProperty(DragStartPropertyName, Params))
	{
		PushParamsToLiveRuntime(Params);
	}
	SyncEditValuesAfterWrite(Wind);
}

bool SKawaiiPhysicsWindScopeWindow::ResetWindParamToDefault(FName PropertyName)
{
	if (GraphWidget.IsValid())
	{
		GraphWidget->SetActiveEditGuide(TOptional<FName>());
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	int32 ResolvedIndex = INDEX_NONE;
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveEditableWind(GraphNode, ResolvedIndex);
	if (!Wind)
	{
		return false;
	}

	FProperty* Property = KawaiiPhysicsWindScopeWindowPrivate::FindProceduralWindProperty(PropertyName);
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
	KawaiiPhysicsWindScopeWindowPrivate::CopyProceduralWindPropertyValue(*Wind, DefaultWind, Property);
	KawaiiPhysicsWindScopeWindowPrivate::MarkWindScopeGraphNodeModified(GraphNode);
	Args.ExternalForceIndex = ResolvedIndex;
	bHasDragStartWind = false;

	FKawaiiProceduralWindDynamicParams Params;
	if (Wind->BuildDynamicParamsForProperty(PropertyName, Params))
	{
		PushParamsToLiveRuntime(Params);
	}
	SyncEditValuesAfterWrite(Wind);
	return true;
}

bool SKawaiiPhysicsWindScopeWindow::IsLiveTargetResolved() const
{
	// Editor側ノードからLive実行中のランタイムノードを解決し、対象ProceduralWindの
	// RuntimeStateが有効かどうかだけを判定する（サンプル取得は行わない）
	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	FAnimNode_KawaiiPhysics* RuntimeNode = KawaiiPhysicsEdUtils::ResolveLiveKawaiiPhysicsNode(GraphNode);
	if (!RuntimeNode)
	{
		return false;
	}

	const int32 ResolvedIndex =
		KawaiiPhysicsWindScopeWindowPrivate::ResolveProceduralWindIndex(*RuntimeNode, Args.ExternalForceIndex);
	if (!RuntimeNode->ExternalForces.IsValidIndex(ResolvedIndex))
	{
		return false;
	}

	const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
		RuntimeNode->ExternalForces[ResolvedIndex].GetPtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	return Wind && Wind->RuntimeState.IsValid();
}

EActiveTimerReturnType SKawaiiPhysicsWindScopeWindow::TickWindScope(double InCurrentTime, float InDeltaTime)
{
	(void)InCurrentTime;
	if (bHasDragStartWind && !FSlateApplication::Get().HasAnyMouseCaptor())
	{
		FinalizeAbandonedWindDrag();
	}

	TryResolvePendingReconnect(InDeltaTime);

	FKawaiiPhysics_ExternalForce_ProceduralWind WindSnapshot;
	bool bHasWindSnapshot = false;
	bHasWindSnapshot = TryGetPreviewForceCopy(WindSnapshot);
	if (!bHasWindSnapshot)
	{
		// Editor側ノード（GraphNode->Node）で対象を解決できなかった場合でも、Live実行中は
		// ランタイム側の別ノードを参照して独立に解決されることがある（BP未再コンパイルの編集で
		// Editor側だけ対象を失っている等）。両方とも解決できないと確認できたときだけ、
		// Pause中に旧波形を残さないようクリアする
		if (!IsLiveTargetResolved())
		{
			DisplaySamples.Reset();
			LastLiveSampleCount = 0;
		}
	}
	UpdateEditValuesFromWind(bHasWindSnapshot ? &WindSnapshot : nullptr);
	UpdateLiveEditValuesFromRuntime();

	if (!bPaused)
	{
		// Live 取得に失敗したら（PIE/デバッグ対象なし等）Preview 波形を計算する
		bool bLiveTargetResolved = false;
		const bool bLiveUpdated = TryUpdateFromLiveRuntime(bLiveTargetResolved);
		if (bLiveUpdated)
		{
			CurrentModeText = LOCTEXT("LiveModeTick", "Live");
			bIsLiveMode = true;
		}
		else if (bLiveTargetResolved)
		{
			// Live対象が有効で新規サンプルが無いだけの tick は Preview に落とさず現状維持する
			// （DisplaySamples はそのまま。CurrentModeText/bIsLiveMode も Live 表示を継続する）
			CurrentModeText = LOCTEXT("LiveModeTick", "Live");
			bIsLiveMode = true;
		}
		else
		{
			if (!bHasWindSnapshot)
			{
				bHasWindSnapshot = TryGetPreviewForceCopy(WindSnapshot);
			}
			RebuildPreviewSamples(InDeltaTime, bHasWindSnapshot ? &WindSnapshot : nullptr);
			CurrentModeText = LOCTEXT("PreviewModeTick", "Preview");
			bIsLiveMode = false;
		}
	}
	else
	{
		// Pause中もバッジ表示だけは対象の生死に追従させる（サンプルは凍結）
		bIsLiveMode = IsLiveTargetResolved();
		CurrentModeText = bIsLiveMode ? LOCTEXT("LiveModeTick", "Live") : LOCTEXT("PreviewModeTick", "Preview");
	}

	// グラフウィジェットへ最新サンプル・表示設定を反映（Pause中もクリア直後の状態を反映するため実行する）
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
			if (KawaiiPhysicsWindScopeWindowPrivate::IsProceduralWindStruct(GraphNode->Node.ExternalForces[Index]))
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
	DisplaySamples.Reserve(KawaiiPhysicsWindScopeWindowPrivate::WindScopePreviewSampleCount);
	const float EndTime = PreviewTime;
	const float StartTime = EndTime - DisplaySeconds;
	const int32 SampleCount = FMath::Max(KawaiiPhysicsWindScopeWindowPrivate::WindScopePreviewSampleCount, 2);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
		const float SampleTime = FMath::Lerp(StartTime, EndTime, Alpha);
		FKawaiiProceduralWindScopeSample ScopeSample;
		ScopeSample.Time = SampleTime;
		// LengthRate=0（ルート相当）で評価。PreApply が ScopeBuffer に書き込む Live サンプルも同じ LengthRate=0
		// で計算されるため、両者は同一基準で比較できる（実ボーンの LengthRateFromRoot による Ripple 位相ずれは含まない）
		ScopeSample.Sample = Wind.ComputeWindSample(SampleTime, 0.0f);
		DisplaySamples.Add(ScopeSample);
	}
}

void SKawaiiPhysicsWindScopeWindow::UpdateEditValuesFromWind(
	const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind)
{
	KawaiiPhysicsWindScopeWindowPrivate::FillWindScopeEditValuesFromWind(Wind, CachedEditValues, true);
}

// ノード実体を書き換えた経路は必ずここを通す。編集パネルの SSpinBox は Value を CachedEditValues に
// バインドしており、Slate のテキスト確定は OnTextCommitted の直後、同一コールスタック内で
// バインド値を読み直す（FSlateEditableTextLayout::LoadText）。TickWindScope による次フレームの更新を
// 待つと、そこで旧値が読まれて入力欄が巻き戻り、さらにフォーカスを外した際にその旧値が
// ノードへ書き戻されてしまう
void SKawaiiPhysicsWindScopeWindow::SyncEditValuesAfterWrite(
	const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind)
{
	UpdateEditValuesFromWind(Wind);
}

void SKawaiiPhysicsWindScopeWindow::UpdateLiveEditValuesFromRuntime()
{
	CachedLiveEditValues = FKawaiiPhysicsWindScopeEditValues();

	UAnimGraphNode_KawaiiPhysics* GraphNode = ResolveGraphNode();
	FAnimNode_KawaiiPhysics* RuntimeNode = KawaiiPhysicsEdUtils::ResolveLiveKawaiiPhysicsNode(GraphNode);
	FKawaiiPhysics_ExternalForce_ProceduralWind* RuntimeWind = KawaiiPhysicsWindScopeWindowPrivate::ResolveLiveProceduralWind(
		GraphNode,
		RuntimeNode,
		Args.ExternalForceIndex);
	if (!RuntimeWind)
	{
		return;
	}

	// Worker スレッドが DynamicParams 反映で値を書き換える可能性があるが、表示専用の乖離ヒントなので torn-read を許容し Mutex は取得しない。
	KawaiiPhysicsWindScopeWindowPrivate::FillWindScopeEditValuesFromWind(RuntimeWind, CachedLiveEditValues, false);
}

bool SKawaiiPhysicsWindScopeWindow::TryUpdateFromLiveRuntime(bool& bOutLiveTargetResolved)
{
	bOutLiveTargetResolved = false;

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

	const int32 ResolvedIndex = KawaiiPhysicsWindScopeWindowPrivate::ResolveProceduralWindIndex(*RuntimeNode, Args.ExternalForceIndex);
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

	// Live対象（ProceduralWind + RuntimeState）自体は解決できた。以降は新規サンプルの有無だけを判定する
	bOutLiveTargetResolved = true;

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

	const int32 ResolvedIndex = KawaiiPhysicsWindScopeWindowPrivate::ResolveProceduralWindIndex(GraphNode->Node, Args.ExternalForceIndex);
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
		if (UAnimGraphNode_KawaiiPhysics* ResolvedGraphNode = KawaiiPhysicsWindScopeWindowPrivate::FindLoadedGraphNodeByGuid(
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
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeLastAnimBlueprintKey,
		*InArgs.AnimBlueprintPath.ToString(),
		GEditorPerProjectIni);
	GConfig->SetString(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeLastNodeGuidKey,
		*InArgs.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
		GEditorPerProjectIni);
	GConfig->SetInt(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeLastForceIndexKey,
		InArgs.ExternalForceIndex,
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SKawaiiPhysicsWindScopeWindow::LoadEditPanelConfig()
{
	GustStrength = KawaiiPhysicsWindScopeWindowPrivate::WindScopeDefaultGustStrength;
	GustRiseTime = KawaiiPhysicsWindScopeWindowPrivate::WindScopeDefaultGustRiseTime;
	GustDecayTime = KawaiiPhysicsWindScopeWindowPrivate::WindScopeDefaultGustDecayTime;

	if (!GConfig)
	{
		return;
	}

	GConfig->GetBool(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeEditPanelExpandedKey,
		bEditPanelExpanded,
		GEditorPerProjectIni);
	GConfig->GetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeEditPanelSplitterFractionKey,
		EditPanelSplitterFraction,
		GEditorPerProjectIni);
	EditPanelSplitterFraction = FMath::Clamp(EditPanelSplitterFraction, 0.2f, 0.8f);
	GConfig->GetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeGustStrengthKey,
		GustStrength,
		GEditorPerProjectIni);
	GConfig->GetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeGustRiseTimeKey,
		GustRiseTime,
		GEditorPerProjectIni);
	GConfig->GetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeGustDecayTimeKey,
		GustDecayTime,
		GEditorPerProjectIni);
	GustStrength = FMath::Clamp(GustStrength, 0.0f, 50.0f);
	GustRiseTime = FMath::Clamp(GustRiseTime, 0.01f, 5.0f);
	GustDecayTime = FMath::Clamp(GustDecayTime, 0.01f, 10.0f);
}

void SKawaiiPhysicsWindScopeWindow::SaveEditPanelConfig() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetBool(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeEditPanelExpandedKey,
		bEditPanelExpanded,
		GEditorPerProjectIni);
	GConfig->SetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeEditPanelSplitterFractionKey,
		EditPanelSplitterFraction,
		GEditorPerProjectIni);
	GConfig->SetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeGustStrengthKey,
		GustStrength,
		GEditorPerProjectIni);
	GConfig->SetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeGustRiseTimeKey,
		GustRiseTime,
		GEditorPerProjectIni);
	GConfig->SetFloat(
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeGustDecayTimeKey,
		GustDecayTime,
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
	if (!GConfig->GetString(KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName, KawaiiPhysicsWindScopeWindowPrivate::WindScopeLastAnimBlueprintKey, AnimBlueprintPathString, GEditorPerProjectIni) ||
		!GConfig->GetString(KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName, KawaiiPhysicsWindScopeWindowPrivate::WindScopeLastNodeGuidKey, NodeGuidString, GEditorPerProjectIni))
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
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeConfigSectionName,
		KawaiiPhysicsWindScopeWindowPrivate::WindScopeLastForceIndexKey,
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
	if (bPendingReconnectAsyncLoadStarted || PendingReconnectElapsedTime < KawaiiPhysicsWindScopeWindowPrivate::WindScopeReconnectTryLoadDelay)
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
	if (!bHasPendingReconnect || !KawaiiPhysicsWindScopeWindowPrivate::AreReconnectArgsSame(PendingReconnectArgs, ExpectedArgs))
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
