// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/UnrealType.h"

#include <type_traits>

#include "KawaiiPhysicsLibrary.generated.h"

class UMirrorDataTable;

UENUM()
enum class EKawaiiPhysicsAccessExternalForceResult : uint8
{
	Valid,
	NotValid,
};

UENUM()
enum class EKawaiiPhysicsAccessResult : uint8
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
 * KawaiiPhysics アニメーションノードに対する Blueprint 操作を公開する関数ライブラリ。
 * Blueprint function library exposing operations on a KawaiiPhysics anim node.
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

	/**
	 * AnimInstance から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の AnimInstance に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 *
	 * Collect KawaiiPhysics node references from an AnimInstance (empty FilterTags collects all nodes).
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own AnimInstance.
	 * Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static bool CollectKawaiiPhysicsNodesFromAnimInstance(TArray<FKawaiiPhysicsReference>& Nodes,
	                                                      UAnimInstance* AnimInstance,
	                                                      const FGameplayTagContainer& FilterTags,
	                                                      bool bFilterExactMatch = false);

	/**
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 *
	 * Collect KawaiiPhysics node references from a component, linked instances, and post-process instance (empty FilterTags collects all nodes).
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component.
	 * Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static bool CollectKawaiiPhysicsNodesFromComponent(TArray<FKawaiiPhysicsReference>& Nodes,
	                                                   USkeletalMeshComponent* MeshComp,
	                                                   const FGameplayTagContainer& FilterTags,
	                                                   bool bFilterExactMatch = false);

	/**
	 * ランタイム名指定アクセスのスレッド契約:
	 * CallAnimNodeFunction は参照先ノードへ即時に関数を実行するだけで、評価スレッドとの同期は行いません。
	 * AnimGraph の BlueprintThreadSafe 文脈、または対象ノードが評価されていないタイミングの GameThread から呼び出してください。
	 *
	 * Thread contract for runtime property access:
	 * CallAnimNodeFunction executes immediately on the referenced node and does not synchronize with evaluation threads.
	 * Call the runtime property access APIs below from a BlueprintThreadSafe AnimGraph context, or from the GameThread while the target node is not being evaluated.
	 */

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
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetDummyBoneLength"),
			[DummyBoneLength](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.DummyBoneLength = FMath::Max(DummyBoneLength, 0.0f);
				InKawaiiPhysics.RequestModifyBonesReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(float, DummyBoneLength);
	}

	// BoneSubdivisionCount
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetBoneSubdivisionCount(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                       int32 BoneSubdivisionCount)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetBoneSubdivisionCount"),
			[BoneSubdivisionCount](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.BoneSubdivisionCount = FMath::Clamp(BoneSubdivisionCount, 0, 10);
				InKawaiiPhysics.RequestModifyBonesReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static int32 GetBoneSubdivisionCount(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(int32, BoneSubdivisionCount);
	}

	// BoneSubdivisionCollisionOnly
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetBoneSubdivisionCollisionOnly(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                               bool bBoneSubdivisionCollisionOnly)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetbBoneSubdivisionCollisionOnly"),
			[bBoneSubdivisionCollisionOnly](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.bBoneSubdivisionCollisionOnly = bBoneSubdivisionCollisionOnly;
				InKawaiiPhysics.RequestModifyBonesReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetBoneSubdivisionCollisionOnly(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bBoneSubdivisionCollisionOnly);
	}

	// BoneConstraintSubdivisionCount
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetBoneConstraintSubdivisionCount(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                                 int32 BoneConstraintSubdivisionCount)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetBoneConstraintSubdivisionCount"),
			[BoneConstraintSubdivisionCount](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.BoneConstraintSubdivisionCount = FMath::Clamp(BoneConstraintSubdivisionCount, 0, 10);
				InKawaiiPhysics.RequestModifyBonesReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static int32 GetBoneConstraintSubdivisionCount(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(int32, BoneConstraintSubdivisionCount);
	}

	// BoneConstraintSubdivisionFeedbackScale（ランタイムスカラー: トポロジ不変のためreinit不要 / runtime scalar, no reinit）
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetBoneConstraintSubdivisionFeedbackScale(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                                         float BoneConstraintSubdivisionFeedbackScale)
	{
		KAWAIIPHYSICS_VALUE_SETTER(float, BoneConstraintSubdivisionFeedbackScale);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static float GetBoneConstraintSubdivisionFeedbackScale(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(float, BoneConstraintSubdivisionFeedbackScale);
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
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetLimitsDataAsset"),
			[LimitsDataAsset](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.LimitsDataAsset = LimitsDataAsset;
				InKawaiiPhysics.RequestModifyBonesReinit();
			});
		return KawaiiPhysics;
	}

	/** LimitsDataAsset */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static UKawaiiPhysicsLimitsDataAsset* GetLimitsDataAsset(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(TObjectPtr<UKawaiiPhysicsLimitsDataAsset>, LimitsDataAsset);
	}

	/**
	 * コリジョンのミラーリング設定を設定（反映は次回ノード初期化時）
	 * Set the collision mirroring source (applied on the next node initialization).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetMirrorDataTableForLimits(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                           UMirrorDataTable* MirrorDataTableForLimits)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetMirrorDataTableForLimits"),
			[MirrorDataTableForLimits](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.MirrorDataTableForLimits = MirrorDataTableForLimits;
				InKawaiiPhysics.RequestModifyBonesReinit();
			});
		return KawaiiPhysics;
	}

	/**
	 * コリジョンのミラーリング設定を取得
	 * Get the collision mirroring source.
	 */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static UMirrorDataTable* GetMirrorDataTableForLimits(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(TObjectPtr<UMirrorDataTable>, MirrorDataTableForLimits);
	}

	/**
	 * ミラー先ボーンに同形状コリジョンがある場合にミラー生成をスキップするかを設定（反映は次回ノード初期化時）
	 * Set whether to skip mirrored collision generation when the mirrored bone already has a collision of the same shape type (applied on the next node initialization).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetSkipMirroredBoneWithExistingCollision(
		const FKawaiiPhysicsReference& KawaiiPhysics, bool bSkipMirroredBoneWithExistingCollision)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetbSkipMirroredBoneWithExistingCollision"),
			[bSkipMirroredBoneWithExistingCollision](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.bSkipMirroredBoneWithExistingCollision = bSkipMirroredBoneWithExistingCollision;
				InKawaiiPhysics.RequestModifyBonesReinit();
			});
		return KawaiiPhysics;
	}

	/**
	 * ミラー先ボーンに同形状コリジョンがある場合にミラー生成をスキップするかを取得
	 * Get whether mirrored collision generation is skipped when the mirrored bone already has a collision of the same shape type.
	 */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetSkipMirroredBoneWithExistingCollision(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bSkipMirroredBoneWithExistingCollision);
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

	/** Remove ExternalForces from SkeletalMeshComponent (by Owner) */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
	                                              UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                              bool bFilterExactMatch = false);

	/**
	 * ProceduralWind の突風をリクエストする。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * Request a ProceduralWind gust. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference TriggerProceduralWindGust(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		int32 ExternalForceIndex,
		float Strength,
		float RiseTime,
		float DecayTime);

	/**
	 * ProceduralWind の動的パラメータ更新をリクエストする。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * Request a ProceduralWind dynamic parameter update. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetProceduralWindParameters(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		int32 ExternalForceIndex,
		const FKawaiiProceduralWindDynamicParams& Params);

	/**
	 * Component 内の ProceduralWind へ突風を一括リクエストする。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * Request a gust for ProceduralWind entries in a component. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 TriggerProceduralWindGustOnComponent(
		USkeletalMeshComponent* MeshComp,
		float Strength,
		float RiseTime,
		float DecayTime,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false);

	/**
	 * Component 内の ProceduralWind へ動的パラメータ更新を一括リクエストする。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * Request dynamic parameter updates for ProceduralWind entries in a component. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 SetProceduralWindParametersOnComponent(
		USkeletalMeshComponent* MeshComp,
		const FKawaiiProceduralWindDynamicParams& Params,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false);

	/**
	 * Set alpha (input) to all KawaiiPhysics nodes in the component (and linked/post-process instances).
	 * This is intended for AnimNotifyState usage.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool SetAlphaToComponent(USkeletalMeshComponent* MeshComp, float Alpha,
	                                UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                bool bFilterExactMatch = false);

	/** Get current alpha (input) from the first matched KawaiiPhysics node in the component. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetAlphaFromComponent(USkeletalMeshComponent* MeshComp, float& OutAlpha,
	                                  UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                  bool bFilterExactMatch = false);

	// --- Shared Collision ---

	/**
	 * このノードをコリジョン共有のSourceにするかを設定
	 * Set whether this node acts as a shared collision source
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Shared Collision", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetSharedCollisionSource(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                        bool bSharedCollisionSource)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetbSharedCollisionSource"),
			[bSharedCollisionSource](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.bSharedCollisionSource = bSharedCollisionSource;
				InKawaiiPhysics.RequestSharedCollisionReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Shared Collision", meta=(BlueprintThreadSafe))
	static bool GetSharedCollisionSource(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bSharedCollisionSource);
	}

	/**
	 * 他のKawaiiPhysicsから共有コリジョンを使用するかを設定
	 * Set whether to use shared collision limits from other KawaiiPhysics nodes
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Shared Collision", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetUseSharedCollision(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                     bool bUseSharedCollision)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetbUseSharedCollision"),
			[bUseSharedCollision](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.bUseSharedCollision = bUseSharedCollision;
				InKawaiiPhysics.RequestSharedCollisionReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Shared Collision", meta=(BlueprintThreadSafe))
	static bool GetUseSharedCollision(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bUseSharedCollision);
	}

	/**
	 * 共有コリジョンのグループタグを設定
	 * Set the group tag for shared collision
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Shared Collision", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetSharedCollisionGroupTag(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                          FGameplayTag SharedCollisionGroupTag)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetSharedCollisionGroupTag"),
			[SharedCollisionGroupTag](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.SharedCollisionGroupTag = SharedCollisionGroupTag;
				InKawaiiPhysics.RequestSharedCollisionReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Shared Collision", meta=(BlueprintThreadSafe))
	static FGameplayTag GetSharedCollisionGroupTag(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(FGameplayTag, SharedCollisionGroupTag);
	}

	static bool IsNodePropertyAccessible(const FProperty* Property);
	static bool IsNodePropertyAccessible(FName PropertyName);
	static bool DoesNodePropertyRequireModifyBonesReinit(FName PropertyName);
	static bool DoesNodePropertyRequireSharedCollisionReinit(FName PropertyName);
	static bool SetNodeWildcardPropertyValue(FAnimNode_KawaiiPhysics& Node, FName PropertyName,
	                                         const FProperty* ValueProperty, const void* ValuePtr);
	static bool GetNodeWildcardPropertyValue(const FAnimNode_KawaiiPhysics& Node, FName PropertyName,
	                                         const FProperty* ValueProperty, void* ValuePtr);
	static bool SetNodePropertyValueFromString(FAnimNode_KawaiiPhysics& Node, FName PropertyName,
	                                           const FString& ValueText);
	static bool GetNodePropertyValueAsString(const FAnimNode_KawaiiPhysics& Node, FName PropertyName,
	                                         FString& OutValueText);

	template <typename ValueType, typename PropertyType>
	static bool SetNodePropertyValue(FAnimNode_KawaiiPhysics& Node, FName PropertyName, const ValueType& Value);
	template <typename ValueType, typename PropertyType>
	static bool GetNodePropertyValue(const FAnimNode_KawaiiPhysics& Node, FName PropertyName, ValueType& OutValue);
	template <typename ValueType>
	static bool SetNodeStructPropertyValue(FAnimNode_KawaiiPhysics& Node, FName PropertyName, const ValueType& Value);
	template <typename ValueType>
	static bool GetNodeStructPropertyValue(const FAnimNode_KawaiiPhysics& Node, FName PropertyName,
	                                       ValueType& OutValue);

	/** プリセットDataAssetをランタイムノードへ適用（ExternalForces と CustomExternalForces は安全のため除外） / Apply a preset data asset to a runtime node; ExternalForces and CustomExternalForces are skipped for runtime safety. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference ApplyPresetDataAsset(EKawaiiPhysicsAccessResult& ExecResult,
	                                                    const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                    UKawaiiPhysicsPresetDataAsset* Preset,
	                                                    FKawaiiPhysicsPresetApplyOptions Options);

	/** ノードの bool プロパティを名前で設定 / Set KawaiiPhysics node bool property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeBoolProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                   const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                   FName PropertyName, bool Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeBoolProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodePropertyValue<bool, FBoolProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの bool プロパティを名前で取得 / Get KawaiiPhysics node bool property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static bool GetNodeBoolProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		bool Value = false;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeBoolProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodePropertyValue<bool, FBoolProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードの int プロパティを名前で設定 / Set KawaiiPhysics node int property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeIntProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                  const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                  FName PropertyName, int32 Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeIntProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodePropertyValue<int32, FIntProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの int プロパティを名前で取得 / Get KawaiiPhysics node int property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static int32 GetNodeIntProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		int32 Value = 0;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeIntProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodePropertyValue<int32, FIntProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードの float プロパティを名前で設定 / Set KawaiiPhysics node float property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeFloatProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                    const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                    FName PropertyName, float Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeFloatProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodePropertyValue<float, FFloatProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの float プロパティを名前で取得 / Get KawaiiPhysics node float property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static float GetNodeFloatProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                  const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		float Value = 0.0f;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeFloatProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodePropertyValue<float, FFloatProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードの Vector プロパティを名前で設定 / Set KawaiiPhysics node Vector property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeVectorProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                     const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                     FName PropertyName, FVector Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeVectorProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodeStructPropertyValue<FVector>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの Vector プロパティを名前で取得 / Get KawaiiPhysics node Vector property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FVector GetNodeVectorProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                     const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		FVector Value = FVector::ZeroVector;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeVectorProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodeStructPropertyValue<FVector>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードの Rotator プロパティを名前で設定 / Set KawaiiPhysics node Rotator property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeRotatorProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                      const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                      FName PropertyName, FRotator Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeRotatorProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodeStructPropertyValue<FRotator>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの Rotator プロパティを名前で取得 / Get KawaiiPhysics node Rotator property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FRotator GetNodeRotatorProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                       const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		FRotator Value = FRotator::ZeroRotator;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeRotatorProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodeStructPropertyValue<FRotator>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードの Transform プロパティを名前で設定 / Set KawaiiPhysics node Transform property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeTransformProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                        const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                        FName PropertyName, FTransform Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeTransformProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodeStructPropertyValue<FTransform>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの Transform プロパティを名前で取得 / Get KawaiiPhysics node Transform property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FTransform GetNodeTransformProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                           const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		FTransform Value = FTransform::Identity;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeTransformProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodeStructPropertyValue<FTransform>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードの Name プロパティを名前で設定 / Set KawaiiPhysics node Name property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeNameProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                   const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                   FName PropertyName, FName Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeNameProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodePropertyValue<FName, FNameProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの Name プロパティを名前で取得 / Get KawaiiPhysics node Name property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FName GetNodeNameProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                 const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		FName Value = NAME_None;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeNameProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodePropertyValue<FName, FNameProperty>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードの GameplayTag プロパティを名前で設定 / Set KawaiiPhysics node GameplayTag property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetNodeGameplayTagProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                                          const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                          FName PropertyName, FGameplayTag Value)
	{
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetNodeGameplayTagProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (SetNodeStructPropertyValue<FGameplayTag>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return KawaiiPhysics;
	}

	/** ノードの GameplayTag プロパティを名前で取得 / Get KawaiiPhysics node GameplayTag property by name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FGameplayTag GetNodeGameplayTagProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                               const FKawaiiPhysicsReference& KawaiiPhysics, FName PropertyName)
	{
		FGameplayTag Value;
		ExecResult = EKawaiiPhysicsAccessResult::NotValid;
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetNodeGameplayTagProperty"),
			[&ExecResult, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (GetNodeStructPropertyValue<FGameplayTag>(InKawaiiPhysics, PropertyName, Value))
				{
					ExecResult = EKawaiiPhysicsAccessResult::Valid;
				}
			});
		return Value;
	}

	/** ノードのプロパティをワイルドカード値で設定 / Set KawaiiPhysics node property by wildcard value. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void SetNodeWildcardProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                    const FKawaiiPhysicsReference& KawaiiPhysics,
	                                    FName PropertyName, const int32& Value)
	{
		checkNoEntry();
	}

	/** ノードのプロパティをワイルドカード値で取得 / Get KawaiiPhysics node property by wildcard value. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void GetNodeWildcardProperty(EKawaiiPhysicsAccessResult& ExecResult,
	                                    const FKawaiiPhysicsReference& KawaiiPhysics,
	                                    FName PropertyName, int32& Value)
	{
		checkNoEntry();
	}

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
	DECLARE_FUNCTION(execSetNodeWildcardProperty);
	DECLARE_FUNCTION(execGetNodeWildcardProperty);
	DECLARE_FUNCTION(execSetExternalForceWildcardProperty);
	DECLARE_FUNCTION(execGetExternalForceWildcardProperty);
};

template <typename ValueType, typename PropertyType>
bool UKawaiiPhysicsLibrary::SetNodePropertyValue(FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                 const ValueType& Value)
{
	const PropertyType* Property = FindFProperty<PropertyType>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	if (!IsNodePropertyAccessible(Property))
	{
		return false;
	}

	if (void* ValuePtr = Property->ContainerPtrToValuePtr<void>(&Node))
	{
		Property->SetPropertyValue(ValuePtr, Value);
		if (DoesNodePropertyRequireModifyBonesReinit(PropertyName))
		{
			Node.RequestModifyBonesReinit();
		}
		if (DoesNodePropertyRequireSharedCollisionReinit(PropertyName))
		{
			Node.RequestSharedCollisionReinit();
		}
		return true;
	}

	return false;
}

template <typename ValueType, typename PropertyType>
bool UKawaiiPhysicsLibrary::GetNodePropertyValue(const FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                 ValueType& OutValue)
{
	const PropertyType* Property = FindFProperty<PropertyType>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	if (!IsNodePropertyAccessible(Property))
	{
		return false;
	}

	if (const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(&Node))
	{
		OutValue = Property->GetPropertyValue(ValuePtr);
		return true;
	}

	return false;
}

template <typename ValueType>
bool UKawaiiPhysicsLibrary::SetNodeStructPropertyValue(FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                       const ValueType& Value)
{
	const FStructProperty* StructProperty = FindFProperty<FStructProperty>(
		FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	const UScriptStruct* ExpectedStruct = nullptr;
	if constexpr (std::is_same_v<ValueType, FVector>)
	{
		ExpectedStruct = TBaseStructure<FVector>::Get();
	}
	else if constexpr (std::is_same_v<ValueType, FRotator>)
	{
		ExpectedStruct = TBaseStructure<FRotator>::Get();
	}
	else if constexpr (std::is_same_v<ValueType, FTransform>)
	{
		ExpectedStruct = TBaseStructure<FTransform>::Get();
	}
	else if constexpr (std::is_same_v<ValueType, FGameplayTag>)
	{
		ExpectedStruct = FGameplayTag::StaticStruct();
	}

	if (!ExpectedStruct || !IsNodePropertyAccessible(StructProperty) || StructProperty->Struct != ExpectedStruct)
	{
		return false;
	}

	if (void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(&Node))
	{
		StructProperty->CopyCompleteValue(ValuePtr, &Value);
		if (DoesNodePropertyRequireModifyBonesReinit(PropertyName))
		{
			Node.RequestModifyBonesReinit();
		}
		if (DoesNodePropertyRequireSharedCollisionReinit(PropertyName))
		{
			Node.RequestSharedCollisionReinit();
		}
		return true;
	}

	return false;
}

template <typename ValueType>
bool UKawaiiPhysicsLibrary::GetNodeStructPropertyValue(const FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                       ValueType& OutValue)
{
	const FStructProperty* StructProperty = FindFProperty<FStructProperty>(
		FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	const UScriptStruct* ExpectedStruct = nullptr;
	if constexpr (std::is_same_v<ValueType, FVector>)
	{
		ExpectedStruct = TBaseStructure<FVector>::Get();
	}
	else if constexpr (std::is_same_v<ValueType, FRotator>)
	{
		ExpectedStruct = TBaseStructure<FRotator>::Get();
	}
	else if constexpr (std::is_same_v<ValueType, FTransform>)
	{
		ExpectedStruct = TBaseStructure<FTransform>::Get();
	}
	else if constexpr (std::is_same_v<ValueType, FGameplayTag>)
	{
		ExpectedStruct = FGameplayTag::StaticStruct();
	}

	if (!ExpectedStruct || !IsNodePropertyAccessible(StructProperty) || StructProperty->Struct != ExpectedStruct)
	{
		return false;
	}

	if (const void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(&Node))
	{
		StructProperty->CopyCompleteValue(&OutValue, ValuePtr);
		return true;
	}

	return false;
}

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
	ValueType Result{};
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
	ValueType Result{};
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
