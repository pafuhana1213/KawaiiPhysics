// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsExternalForce.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KawaiiPhysicsLibrary.generated.h"

struct FAnimNode_KawaiiPhysics;

#define KAWAIIPHYSICS_VALUE_SETTER(PropertyType, PropertyName) \
{ \
    KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>( \
        TEXT("Set" #PropertyName), \
        [PropertyName](FAnimNode_KawaiiPhysics& InKawaiiPhysics) { \
            InKawaiiPhysics.PropertyName = PropertyName; \
        }); \
    return KawaiiPhysics; \
}

#define KAWAIIPHYSICS_VALUE_GETTER(PropertyType, PropertyName) \
 { \
    PropertyType Value; \
    KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>( \
        TEXT("Get" #PropertyName), \
        [&Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics) { \
            Value = InKawaiiPhysics.PropertyName; \
        }); \
    return Value; \
}


USTRUCT(BlueprintType)
struct FKawaiiPhysicsReference : public FAnimNodeReference
{
	GENERATED_BODY()

	using FInternalNodeType = FAnimNode_KawaiiPhysics;
};

/**
 * Exposes operations to be performed on a blend space anim node.
 */
UCLASS()
class KAWAIIPHYSICS_API UKawaiiPhysicsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Get a KawaiiPhysics from an anim node */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "Result"))
	static FKawaiiPhysicsReference ConvertToKawaiiPhysics(const FAnimNodeReference& Node,
	                                                      EAnimNodeReferenceConversionResult& Result);

	/** Get a KawaiiPhysics from an anim node (pure). */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, DisplayName = "Convert to Kawaii Physics (Pure)"))
	static void ConvertToKawaiiPhysicsPure(const FAnimNodeReference& Node, FKawaiiPhysicsReference& KawaiiPhysics,
	                                       bool& Result)
	{
		EAnimNodeReferenceConversionResult ConversionResult;
		KawaiiPhysics = ConvertToKawaiiPhysics(Node, ConversionResult);
		Result = (ConversionResult == EAnimNodeReferenceConversionResult::Succeeded);
	}

	/** Set RootBone */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                               UPARAM(ref) FName& RootBoneName);
	/** Get RootBone */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FName GetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set ExcludeBones */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                   UPARAM(ref) TArray<FName>& ExcludeBoneNames);
	/** Get ExcludeBones */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static TArray<FName> GetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set PhysicsSettings */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                  UPARAM(ref) FKawaiiPhysicsSettings& PhysicsSettings)
	{
		KAWAIIPHYSICS_VALUE_SETTER(FKawaiiPhysicsSettings, PhysicsSettings);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsSettings GetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(FKawaiiPhysicsSettings, PhysicsSettings);
	}

	// DummyBoneLength
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                  float DummyBoneLength)
	{
		KAWAIIPHYSICS_VALUE_SETTER(float, DummyBoneLength);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(float, DummyBoneLength);
	}

	/** TeleportDistanceThreshold */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetTeleportDistanceThreshold(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                            float TeleportDistanceThreshold)
	{
		KAWAIIPHYSICS_VALUE_SETTER(float, TeleportDistanceThreshold);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetTeleportDistanceThreshold(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(float, TeleportDistanceThreshold);
	}

	/** TeleportRotationThreshold */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetTeleportRotationThreshold(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                            float TeleportRotationThreshold)
	{
		KAWAIIPHYSICS_VALUE_SETTER(float, TeleportRotationThreshold);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetTeleportRotationThreshold(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(float, TeleportRotationThreshold);
	}

	/** Gravity */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetGravity(const FKawaiiPhysicsReference& KawaiiPhysics, FVector Gravity)
	{
		KAWAIIPHYSICS_VALUE_SETTER(FVector, Gravity);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FVector GetGravity(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(FVector, Gravity);
	}

	/** EnableWind */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetEnableWind(const FKawaiiPhysicsReference& KawaiiPhysics, bool bEnableWind)
	{
		KAWAIIPHYSICS_VALUE_SETTER(bool, bEnableWind);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetEnableWind(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bEnableWind);
	}

	/** WindScale */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetWindScale(const FKawaiiPhysicsReference& KawaiiPhysics, float WindScale)
	{
		KAWAIIPHYSICS_VALUE_SETTER(float, WindScale);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetWindScale(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(float, WindScale);
	}

	/** AllowWorldCollision */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetAllowWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                      bool bAllowWorldCollision)
	{
		KAWAIIPHYSICS_VALUE_SETTER(bool, bAllowWorldCollision);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetAllowWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bAllowWorldCollision);
	}

	/** NeedWarmUp */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics, bool bNeedWarmUp)
	{
		KAWAIIPHYSICS_VALUE_SETTER(bool, bNeedWarmUp);
	}

	/** NeedWarmUp */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bNeedWarmUp);
	}

	/** LimitsDataAsset */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetLimitsDataAsset(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                  UKawaiiPhysicsLimitsDataAsset* LimitsDataAsset)
	{
		KAWAIIPHYSICS_VALUE_SETTER(TObjectPtr<UKawaiiPhysicsLimitsDataAsset>, LimitsDataAsset);
	}

	/** LimitsDataAsset */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static UKawaiiPhysicsLimitsDataAsset* GetLimitsDataAsset(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(TObjectPtr<UKawaiiPhysicsLimitsDataAsset>, LimitsDataAsset);
	}

	// /** Set ExternalForces */
	// UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	// static FKawaiiPhysicsReference SetExternalForces(const FKawaiiPhysicsReference& KawaiiPhysics,
	//                                                  UPARAM(ref) TArray<FInstancedStruct>& ExternalForces)
	// {
	// 	KAWAIIPHYSICS_VALUE_SETTER(TArray<FInstancedStruct>, ExternalForces);
	// }
	//
	// /** Get ExternalForces */
	// UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	// static TArray<FInstancedStruct> GetExternalForces(const FKawaiiPhysicsReference& KawaiiPhysics)
	// {
	// 	KAWAIIPHYSICS_VALUE_GETTER(TArray<FInstancedStruct>, ExternalForces);
	// }

	/** Set ExternalForceBoolParameter */
	template <typename ValueType, typename PropertyType>
	static FKawaiiPhysicsReference SetExternalForceProperty(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                        int ExternalForceIndex, FName PropertyName,
	                                                        ValueType Value);
	/** Get ExternalForceBoolParameter */
	template <typename ValueType>
	static ValueType GetExternalForceProperty(const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                          FName PropertyName);

	/** Set ExternalForceBoolParameter bool */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetExternalForceBoolProperty(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                            int ExternalForceIndex, FName PropertyName,
	                                                            bool Value)
	{
		return SetExternalForceProperty<bool, FBoolProperty>(KawaiiPhysics, ExternalForceIndex, PropertyName, Value);
	}

	/** Get ExternalForceBoolParameter bool */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetExternalForceBoolProperty(const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                         FName PropertyName)
	{
		return GetExternalForceProperty<bool>(KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Set ExternalForceBoolParameter int */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetExternalForceIntProperty(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                           int ExternalForceIndex, FName PropertyName,
	                                                           int32 Value)
	{
		return SetExternalForceProperty<int32, FIntProperty>(KawaiiPhysics, ExternalForceIndex, PropertyName, Value);
	}

	/** Get ExternalForceBoolParameter int */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static int32 GetExternalForceIntProperty(const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                         FName PropertyName)
	{
		return GetExternalForceProperty<int32>(KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Set ExternalForceBoolParameter float */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetExternalForceFloatProperty(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                             int ExternalForceIndex, FName PropertyName,
	                                                             int32 Value)
	{
		return SetExternalForceProperty<float, FFloatProperty>(KawaiiPhysics, ExternalForceIndex, PropertyName, Value);
	}

	/** Get ExternalForceBoolParameter int */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetExternalForceFloatProperty(const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                           FName PropertyName)
	{
		return GetExternalForceProperty<float>(KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** ResetDynamics */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics);
};

template <typename ValueType, typename PropertyType>
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExternalForceProperty(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                                        int ExternalForceIndex, FName PropertyName,
                                                                        ValueType Value)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExternalForceBoolParameter"),
		[&ExternalForceIndex, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const PropertyType* Property = FindFieldChecked<PropertyType>(ScriptStruct, PropertyName))
				{
					if (void* ValuePtr = Property->template ContainerPtrToValuePtr<uint8>(&Force))
					{
						Property->SetPropertyValue(ValuePtr, Value);
					}
				}
			}
		});

	return KawaiiPhysics;
}

template <typename ValueType>
ValueType UKawaiiPhysicsLibrary::GetExternalForceProperty(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                          int ExternalForceIndex, FName PropertyName)
{
	ValueType Result;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExternalForceBoolParameter"),
		[&Result, &ExternalForceIndex, &PropertyName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				const auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const FProperty* Property = FindFieldChecked<FBoolProperty>(ScriptStruct, PropertyName))
				{
					Result = *(Property->ContainerPtrToValuePtr<ValueType>(&Force));
				}
			}
		});

	return Result;
}
