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
	const FKawaiiPhysics_ExternalForce_ProceduralWind DefaultWind;

	for (const FKawaiiWindScopeParamGroup& Group : GetWindScopeParamGroups())
	{
		for (const FKawaiiWindScopeParamDef& Param : Group.Params)
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
	bOk &= TestFalse(TEXT("RandomSeed is not live DynamicParams supported"),
	                 TableDynamicParamsSupportedNames.Contains(GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomSeed)));

	return bOk;
}

#endif
