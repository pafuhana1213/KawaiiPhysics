// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "../SKawaiiPhysicsWindScopeEditPanel.h"

#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
	FProperty* FindWindScopeTestProperty(const FName PropertyName)
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindScopeEditPanelDefinitionsTest,
                                 "KawaiiPhysics.Editor.WindScopeEditPanel.Definitions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindScopeEditPanelDefinitionsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bOk = true;
	TSet<FName> TableDynamicParamsSupportedNames;
	TSet<FName> RuntimeDynamicParamsSupportedNames;
	TSet<FName> GroupIds;
	const FKawaiiPhysics_ExternalForce_ProceduralWind DefaultWind;

	for (const FKawaiiPhysicsWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		bOk &= TestFalse(
			FString::Printf(TEXT("Wind Scope edit group ID is set: %s"), *Group.GroupLabel.ToString()),
			Group.GroupId.IsNone());
		bOk &= TestFalse(
			FString::Printf(TEXT("Wind Scope edit group ID is unique: %s"), *Group.GroupId.ToString()),
			GroupIds.Contains(Group.GroupId));
		GroupIds.Add(Group.GroupId);

		// 折りたたみ可能なグループはサマリーを最低1項目持つ（折りたたみ時に情報ゼロにならない）
		if (!Group.bPinned)
		{
			bOk &= TestTrue(
				FString::Printf(TEXT("Collapsible group has at least one summary item: %s"), *Group.GroupId.ToString()),
				Group.SummaryItems.Num() > 0);
		}

		for (const FKawaiiPhysicsWindScopeSummaryItem& SummaryItem : Group.SummaryItems)
		{
			bool bSummaryItemPropertyExistsInGroup = false;
			for (const FKawaiiPhysicsWindScopeParamDef& Param : Group.Params)
			{
				if (Param.PropertyName == SummaryItem.PropertyName)
				{
					bSummaryItemPropertyExistsInGroup = true;
					break;
				}
			}
			bOk &= TestTrue(
				FString::Printf(TEXT("SummaryItem property exists in group params: %s"), *SummaryItem.PropertyName.ToString()),
				bSummaryItemPropertyExistsInGroup);

			FProperty* SummaryItemProperty = FindWindScopeTestProperty(SummaryItem.PropertyName);
			bOk &= TestNotNull(
				FString::Printf(TEXT("SummaryItem property exists: %s"), *SummaryItem.PropertyName.ToString()),
				SummaryItemProperty);

			// ラベル無し表示（値のみ）が許されるのはベクトル表示の WindDirection だけ
			if (SummaryItem.PropertyName != GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
			{
				bOk &= TestFalse(
					FString::Printf(TEXT("SummaryItem has a short label: %s"), *SummaryItem.PropertyName.ToString()),
					SummaryItem.ShortLabel.IsEmpty());
			}

			if (SummaryItem.bHideWhenZero)
			{
				bOk &= TestNotNull(
					FString::Printf(TEXT("bHideWhenZero summary item is a float property: %s"), *SummaryItem.PropertyName.ToString()),
					CastField<FFloatProperty>(SummaryItemProperty));
			}

			if (SummaryItem.Unit == EKawaiiPhysicsWindScopeSummaryUnit::Seconds ||
				SummaryItem.Unit == EKawaiiPhysicsWindScopeSummaryUnit::Degrees)
			{
				const bool bSupportedSummaryUnitType =
					CastField<FFloatProperty>(SummaryItemProperty) ||
					(CastField<FStructProperty>(SummaryItemProperty) &&
						CastField<FStructProperty>(SummaryItemProperty)->Struct == TBaseStructure<FFloatInterval>::Get());
				bOk &= TestTrue(
					FString::Printf(TEXT("SummaryItem unit type is float or interval: %s"), *SummaryItem.PropertyName.ToString()),
					bSupportedSummaryUnitType);
			}
		}

		for (const FKawaiiPhysicsWindScopeParamDef& Param : Group.Params)
		{
			FProperty* Property = FindWindScopeTestProperty(Param.PropertyName);
			bOk &= TestNotNull(
				FString::Printf(TEXT("Wind Scope edit property exists: %s"), *Param.PropertyName.ToString()),
				Property);

			if (Param.bDynamicParamsSupported)
			{
				TableDynamicParamsSupportedNames.Add(Param.PropertyName);
			}

			if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
			{
				(void)FloatProperty;
				if (Property->HasMetaData(TEXT("ClampMin")))
				{
					float ClampMin = 0.0f;
					if (LexTryParseString(ClampMin, *Property->GetMetaData(TEXT("ClampMin"))))
					{
						bOk &= TestTrue(
							FString::Printf(TEXT("SliderMin is not below ClampMin: %s"), *Param.PropertyName.ToString()),
							Param.SliderMin + KINDA_SMALL_NUMBER >= ClampMin);
					}
				}
			}
		}
	}

	for (TFieldIterator<FProperty> PropertyIt(FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct(), EFieldIterationFlags::IncludeSuper);
	     PropertyIt;
	     ++PropertyIt)
	{
		FKawaiiProceduralWindDynamicParams Params;
		if (DefaultWind.BuildDynamicParamsForProperty(PropertyIt->GetFName(), Params))
		{
			RuntimeDynamicParamsSupportedNames.Add(PropertyIt->GetFName());
		}
	}

	bOk &= TestEqual(TEXT("DynamicParams supported property count"),
	                 RuntimeDynamicParamsSupportedNames.Num(),
	                 TableDynamicParamsSupportedNames.Num());
	for (const FName& RuntimeName : RuntimeDynamicParamsSupportedNames)
	{
		bOk &= TestTrue(
			FString::Printf(TEXT("DynamicParams supported property is listed as supported: %s"), *RuntimeName.ToString()),
			TableDynamicParamsSupportedNames.Contains(RuntimeName));
	}
	for (const FName& TableName : TableDynamicParamsSupportedNames)
	{
		bOk &= TestTrue(
			FString::Printf(TEXT("Table supported property is supported by DynamicParams: %s"), *TableName.ToString()),
			RuntimeDynamicParamsSupportedNames.Contains(TableName));
	}
	bOk &= TestFalse(TEXT("Seed is not live DynamicParams supported"),
	                 TableDynamicParamsSupportedNames.Contains(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, Seed)));

	const TSet<FName> EmptyParsedGroups = ParseWindScopeCollapsedGroups(FString());
	bOk &= TestEqual(TEXT("Empty collapsed group string parses as empty"), EmptyParsedGroups.Num(), 0);

	TSet<FName> CollapsedGroupRoundTripInput;
	CollapsedGroupRoundTripInput.Add(FName(TEXT("Constant")));
	CollapsedGroupRoundTripInput.Add(FName(TEXT("Ripple")));
	const TSet<FName> CollapsedGroupRoundTripOutput =
		ParseWindScopeCollapsedGroups(SerializeWindScopeCollapsedGroups(CollapsedGroupRoundTripInput));
	bOk &= TestEqual(TEXT("Collapsed group round-trip count"),
	                 CollapsedGroupRoundTripOutput.Num(),
	                 CollapsedGroupRoundTripInput.Num());
	for (const FName& GroupId : CollapsedGroupRoundTripInput)
	{
		bOk &= TestTrue(
			FString::Printf(TEXT("Collapsed group round-trip keeps ID: %s"), *GroupId.ToString()),
			CollapsedGroupRoundTripOutput.Contains(GroupId));
	}

	const TSet<FName> ParsedWithUnknown = ParseWindScopeCollapsedGroups(FString(TEXT("Constant,Time,UnknownGroup,Ripple")));
	bOk &= TestTrue(TEXT("Known collapsed group is parsed"), ParsedWithUnknown.Contains(FName(TEXT("Constant"))));
	bOk &= TestTrue(TEXT("Known collapsed group after unknown is parsed"), ParsedWithUnknown.Contains(FName(TEXT("Ripple"))));
	bOk &= TestFalse(TEXT("Removed Time collapsed group is discarded"), ParsedWithUnknown.Contains(FName(TEXT("Time"))));
	bOk &= TestFalse(TEXT("Unknown collapsed group is discarded"), ParsedWithUnknown.Contains(FName(TEXT("UnknownGroup"))));

	return bOk;
}

#endif
