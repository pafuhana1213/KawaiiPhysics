// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsExternalForce.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KawaiiPhysicsLibrary.generated.h"

UENUM()
enum class EKawaiiPhysicsAccessExternalForceResult : uint8
{
	Valid,
	NotValid,
};

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

	/** Collect KawaiiPhysics Node References from AnimInstance(ABP)  */
	static bool CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes,
	                                      UAnimInstance* AnimInstance, const FGameplayTagContainer& FilterTags,
	                                      bool bFilterExactMatch);

	/** Collect KawaiiPhysics Node References from SkeletalMeshComponent  */
	static bool CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes,
	                                      USkeletalMeshComponent* MeshComp, const FGameplayTagContainer& FilterTags,
	                                      bool bFilterExactMatch);

	/** ResetDynamics */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics);

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

	// PhysicsSettings
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

	/** Add ExternalForce With ExecResult */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference AddExternalForceWithExecResult(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                              const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                              FInstancedStruct& ExternalForce, UObject* Owner);

	/** Add ExternalForce */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool AddExternalForce(const FKawaiiPhysicsReference& KawaiiPhysics,
	                             FInstancedStruct& ExternalForce, UObject* Owner, bool bIsOneShot = false);

	/** Add ExternalForces to SkeletalMeshComponent */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
	                                         UPARAM(ref) TArray<FInstancedStruct>& ExternalForces, UObject* Owner,
	                                         UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                         bool bFilterExactMatch = false,
	                                         bool bIsOneShot = false);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
	                                              UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                              bool bFilterExactMatch = false);


	/** Set ExternalForceParameter template */
	template <typename ValueType, typename PropertyType>
	static FKawaiiPhysicsReference SetExternalForceProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                        const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                        int ExternalForceIndex, FName PropertyName,
	                                                        ValueType Value);
	/** Get ExternalForceParameter template */
	template <typename ValueType>
	static ValueType GetExternalForceProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                          const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                          FName PropertyName);

	/** Set ExternalForceParameter template struct */
	template <typename ValueType>
	static FKawaiiPhysicsReference SetExternalForceStructProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                              const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                              int ExternalForceIndex, FName PropertyName,
	                                                              ValueType Value);
	/** Get ExternalForceParameter template struct */
	template <typename ValueType>
	static ValueType GetExternalForceStructProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                int ExternalForceIndex,
	                                                FName PropertyName);

	/** Set ExternalForceParameter bool */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceBoolProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                            const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                            int ExternalForceIndex, FName PropertyName,
	                                                            bool Value)
	{
		return SetExternalForceProperty<bool, FBoolProperty>(ExecResult, KawaiiPhysics, ExternalForceIndex,
		                                                     PropertyName, Value);
	}

	/** Get ExternalForceParameter bool */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static bool GetExternalForceBoolProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                         const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                         FName PropertyName)
	{
		return GetExternalForceProperty<bool>(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Set ExternalForceParameter int */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceIntProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                           const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                           int ExternalForceIndex, FName PropertyName,
	                                                           int32 Value)
	{
		return SetExternalForceProperty<int32, FIntProperty>(ExecResult, KawaiiPhysics, ExternalForceIndex,
		                                                     PropertyName, Value);
	}

	/** Get ExternalForceParameter int */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static int32 GetExternalForceIntProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                         const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                         FName PropertyName)
	{
		return GetExternalForceProperty<int32>(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Set ExternalForceParameter float */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceFloatProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                             const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                             int ExternalForceIndex, FName PropertyName,
	                                                             float Value)
	{
		return SetExternalForceProperty<float, FFloatProperty>(ExecResult, KawaiiPhysics, ExternalForceIndex,
		                                                       PropertyName, Value);
	}

	/** Get ExternalForceParameter float */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static float GetExternalForceFloatProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                           const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                           FName PropertyName)
	{
		return GetExternalForceProperty<float>(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Get ExternalForceParameter Vector */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceVectorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                              const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                              int ExternalForceIndex, FName PropertyName,
	                                                              FVector Value)
	{
		return SetExternalForceStructProperty<FVector>(ExecResult, KawaiiPhysics, ExternalForceIndex,
		                                               PropertyName, Value);
	}

	/** Get ExternalForceParameter Vector */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FVector GetExternalForceVectorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                              const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                              FName PropertyName)
	{
		return GetExternalForceStructProperty<FVector>(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Get ExternalForceParameter Rotator */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceRotatorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                               const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                               int ExternalForceIndex, FName PropertyName,
	                                                               FRotator Value)
	{
		return SetExternalForceStructProperty<FRotator>(ExecResult, KawaiiPhysics, ExternalForceIndex,
		                                                PropertyName, Value);
	}

	/** Get ExternalForceParameter Rotator */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FRotator GetExternalForceRotatorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                int ExternalForceIndex,
	                                                FName PropertyName)
	{
		return GetExternalForceStructProperty<FRotator>(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Get ExternalForceParameter Transform */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceTransformProperty(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		int ExternalForceIndex, FName PropertyName,
		FTransform Value)
	{
		return SetExternalForceStructProperty<FTransform>(ExecResult, KawaiiPhysics, ExternalForceIndex,
		                                                  PropertyName, Value);
	}

	/** Get ExternalForceParameter Transform */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FTransform GetExternalForceTransformProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                    const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                    int ExternalForceIndex,
	                                                    FName PropertyName)
	{
		return GetExternalForceStructProperty<FTransform>(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName);
	}

	/** Set ExternalForceParameter Wildcard */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void SetExternalForceWildcardProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                             const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                             FName PropertyName, const int32& Value)
	{
		checkNoEntry();
	}


	/** Get ExternalForceParameter Wildcard */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void GetExternalForceWildcardProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                             const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                             FName PropertyName, int32& Value)
	{
		checkNoEntry();
	}

private:
	DECLARE_FUNCTION(execSetExternalForceWildcardProperty);
	DECLARE_FUNCTION(execGetExternalForceWildcardProperty);
};

template <typename ValueType, typename PropertyType>
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExternalForceProperty(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics,
	int ExternalForceIndex, FName PropertyName, ValueType Value)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExternalForceProperty"),
		[&ExecResult, &ExternalForceIndex, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const PropertyType* Property = FindFProperty<PropertyType>(ScriptStruct, PropertyName))
				{
					if (void* ValuePtr = Property->template ContainerPtrToValuePtr<uint8>(&Force))
					{
						Property->SetPropertyValue(ValuePtr, Value);
						ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
					}
				}
			}
		});

	return KawaiiPhysics;
}

template <typename ValueType>
ValueType UKawaiiPhysicsLibrary::GetExternalForceProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
                                                          const FKawaiiPhysicsReference& KawaiiPhysics,
                                                          int ExternalForceIndex, FName PropertyName)
{
	ValueType Result;
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExternalForceProperty"),
		[&Result, &ExecResult, &ExternalForceIndex, &PropertyName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				const auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const FProperty* Property = FindFProperty<FProperty>(ScriptStruct, PropertyName))
				{
					Result = *(Property->ContainerPtrToValuePtr<ValueType>(&Force));
					ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
				}
			}
		});

	return Result;
}

template <typename ValueType>
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExternalForceStructProperty(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics,
	int ExternalForceIndex, FName PropertyName, ValueType Value)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExternalForceStructProperty"),
		[&ExecResult, &ExternalForceIndex, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const FStructProperty* StructProperty = FindFProperty<FStructProperty>(
					ScriptStruct, PropertyName))
				{
					if (StructProperty->Struct == TBaseStructure<ValueType>::Get())
					{
						if (void* ValuePtr = StructProperty->ContainerPtrToValuePtr<uint8>(&Force))
						{
							StructProperty->CopyCompleteValue(ValuePtr, &Value);
							ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
						}
					}
				}
			}
		});

	return KawaiiPhysics;
}

template <typename ValueType>
ValueType UKawaiiPhysicsLibrary::GetExternalForceStructProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
                                                                const FKawaiiPhysicsReference& KawaiiPhysics,
                                                                int ExternalForceIndex, FName PropertyName)
{
	ValueType Result;
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExternalForceStructProperty"),
		[&Result, &ExecResult, &ExternalForceIndex, &PropertyName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				const auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const FStructProperty* StructProperty = FindFProperty<FStructProperty>(
					ScriptStruct, PropertyName))
				{
					if (StructProperty->Struct == TBaseStructure<ValueType>::Get())
					{
						Result = *(StructProperty->ContainerPtrToValuePtr<ValueType>(&Force));
						ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
					}
				}
			}
		});

	return Result;
}
