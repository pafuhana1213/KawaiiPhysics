// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KawaiiPhysicsLibrary.generated.h"

struct FAnimNode_KawaiiPhysics;

USTRUCT(BlueprintType)
struct FKawaiiPhysicsReference : public FAnimNodeReference
{
	GENERATED_BODY()

	typedef FAnimNode_KawaiiPhysics FInternalNodeType;
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
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "Result"))
	static FKawaiiPhysicsReference ConvertToKawaiiPhysics(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result);

	/** Get a KawaiiPhysics from an anim node (pure). */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe, DisplayName = "Convert to Kawaii Physics (Pure)"))
	static void ConvertToKawaiiPhysicsPure(const FAnimNodeReference& Node, FKawaiiPhysicsReference& KawaiiPhysics, bool& Result)
	{
		EAnimNodeReferenceConversionResult ConversionResult;
		KawaiiPhysics = ConvertToKawaiiPhysics(Node, ConversionResult);
		Result = (ConversionResult == EAnimNodeReferenceConversionResult::Succeeded);
	}

	/** Set PhysicsSettings */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics, UPARAM(ref) FKawaiiPhysicsSettings& PhysicsSettings);
	/** Get PhysicsSettings */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsSettings GetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set DummyBoneLength */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics, float DummyBoneLength);
	/** Get DummyBoneLength */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics);
	
	/** Set TeleportDistanceThreshold */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetTeleportDistanceThreshold(const FKawaiiPhysicsReference& KawaiiPhysics, float TeleportDistanceThreshold);
	/** Get TeleportDistanceThreshold */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetTeleportDistanceThreshold(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set TeleportRotationThreshold */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetTeleportRotationThreshold(const FKawaiiPhysicsReference& KawaiiPhysics,float TeleportRotationThreshold);
	/** Get TeleportRotationThreshold */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetTeleportRotationThreshold(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set Gravity */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetGravity(const FKawaiiPhysicsReference& KawaiiPhysics, FVector Gravity);
	/** Get Gravity */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FVector GetGravity(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set EnableWind */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetEnableWind(const FKawaiiPhysicsReference& KawaiiPhysics, bool EnableWind);
	/** Get EnableWind */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetEnableWind(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set WindScale */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetWindScale(const FKawaiiPhysicsReference& KawaiiPhysics, float WindScale);
	/** Get WindScale */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetWindScale(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set AllowWorldCollision */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetAllowWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics,bool AllowWorldCollision);
	/** Get AllowWorldCollision */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetAllowWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** Set NeedWarmUp */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics,bool NeedWarmUp);
	/** Get NeedWarmUp */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics);

	/** ResetDynamics */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics);
};
