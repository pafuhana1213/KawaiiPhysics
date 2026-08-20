// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/UnrealType.h"

#include <type_traits>

#include "KawaiiPhysicsLibrary.generated.h"

class UMirrorDataTable;
class UKawaiiPhysicsWindPresetDataAsset;

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
	                                                              UPARAM(meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) FInstancedStruct& ExternalForce,
	                                                              UObject* Owner);

	/** Add ExternalForce */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool AddExternalForce(const FKawaiiPhysicsReference& KawaiiPhysics,
	                             UPARAM(meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) FInstancedStruct& ExternalForce,
	                             UObject* Owner, bool bIsOneShot = false);

	/** Add ExternalForces to SkeletalMeshComponent */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
	                                         UPARAM(ref, meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) TArray<FInstancedStruct>& ExternalForces,
	                                         UObject* Owner,
	                                         UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                         bool bFilterExactMatch = false,
	                                         bool bIsOneShot = false);

	/** Remove ExternalForces from SkeletalMeshComponent (by Owner) */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
	                                              UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                              bool bFilterExactMatch = false);

	/**
	 * ランタイム専用の一時外力を追加する。LifetimeSeconds 経過で自動除去。ハンドルで StopTransientExternalForce による早期除去（汎用外力は寿命短縮のみ・フェード無し）が可能。
	 * Initialize(Context) は呼ばれないため、Curve 等 Initialize 依存の外力は挙動制限あり（ProceduralWind は PreApply で RuntimeState を遅延生成するため安全）。
	 * BP再コンパイルやノード再初期化で失われる。transient スロット上限8、超過時最古破棄。
	 * 一時外力ストレージはGC追跡外のため、liveなUObject参照（ExternalOwner・カーブアセット等）を含む外力は拒否される。
	 * Add a runtime-only transient external force. Automatically removed after LifetimeSeconds. The handle can be used with StopTransientExternalForce for early removal (generic external forces only shorten lifetime and do not fade).
	 * Initialize(Context) is not called, so forces that depend on Initialize, such as Curve-based forces, have limited behavior (ProceduralWind is safe because it lazily creates RuntimeState in PreApply).
	 * Lost on BP recompile or node re-initialization. Transient slots are capped at 8; beyond the cap, the oldest entry is evicted.
	 * Transient force storage is not GC-tracked, so forces containing live UObject references (ExternalOwner, curve assets, etc.) are rejected.
	 * @param ExecResult ノード参照と外力型の解決結果 / Result of resolving the node reference and external force type.
	 * @param OutHandle 早期除去に使うハンドル / Handle used for early removal.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param ExternalForce 追加する FKawaiiPhysics_ExternalForce 派生外力 / FKawaiiPhysics_ExternalForce-derived force to add.
	 * @param LifetimeSeconds 自動除去までの寿命（秒） / Lifetime in seconds before automatic removal.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference AddTransientExternalForce(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		FKawaiiPhysicsTransientForceHandle& OutHandle,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		UPARAM(meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) FInstancedStruct ExternalForce,
		float LifetimeSeconds = 3.0f);

	/**
	 * ランタイム専用の一時 ProceduralWind として突風をスポーンする。ノードに作成済み ProceduralWind がなくても動作し、複数の同時突風は加算される。
	 * 突風はノード再初期化時に失われる。キュー経由でスレッドセーフに適用され、次回 Evaluate から反映される。
	 * Spawn a gust as a runtime-only transient ProceduralWind. Works even without an authored ProceduralWind on the node, and multiple simultaneous gusts stack.
	 * Gusts are lost on node re-initialization. Applied thread-safely through a queue and takes effect from the next Evaluate.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param ExternalForceIndex 方向継承元の ExternalForces インデックス（無効なら最初の有効な ProceduralWind から継承） / ExternalForces index used as the inheritance source; falls back to the first enabled ProceduralWind when invalid.
	 * @param Strength 突風のピーク強度 / Peak gust strength.
	 * @param RiseTime 0->ピークまでの立ち上がり時間（秒） / Time in seconds to rise from zero to peak.
	 * @param DecayTime ピーク->0 までの減衰時間（秒） / Time in seconds to decay from peak to zero.
	 * @param GustDirection 突風の方向（ワールド空間・非正規化可）。ゼロベクトルなら既存 ProceduralWind の風向き・空間・ボーンフィルタ等を継承 / Gust direction (world space; may be non-normalized). Zero vector inherits direction, space, and bone filters from an authored ProceduralWind if present.
	 * エンベロープは Rise→Hold→Decay の台形（HoldTime=0 で従来の三角波） / The envelope is a Rise-Hold-Decay trapezoid (HoldTime=0 keeps the legacy triangular shape).
	 * @param HoldTime ピーク強度を保持する時間（秒） / Time in seconds to hold the peak strength.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference TriggerProceduralWindGust(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		int32 ExternalForceIndex,
		float Strength,
		float RiseTime,
		float DecayTime,
		FVector GustDirection = FVector(0, 0, 0),
		float HoldTime = 0.0f);

	/**
	 * ハンドルで停止できるランタイム専用 ProceduralWind Blow を開始する。Duration は Rise/Decay 込みの合計実秒で、TimeScale を継承せず実時間で動作する（wind 時間で動く TriggerProceduralWindGust とは異なる）。
	 * エンベロープは Rise→Hold→Decay の台形（Hold=max(0,Duration-Rise-Decay)、Rise+Decay>Duration は比例圧縮）。同時に保持できる transient スロットは上限8で、超過時は最古が破棄される。破棄された風のハンドルは失効し Stop は no-op。URO/LOD で評価が止まると実時間が伸びる。
	 * Start a runtime-only ProceduralWind blow that can be stopped by handle. Duration is total real seconds including Rise/Decay, does not inherit TimeScale, and runs in real time (unlike TriggerProceduralWindGust, which runs in wind time).
	 * The envelope is a Rise-Hold-Decay trapezoid (Hold=max(0,Duration-Rise-Decay); Rise+Decay>Duration is scaled proportionally). Transient slots are capped at 8; beyond the cap, the oldest entry is evicted. Handles for evicted winds become stale and Stop is a no-op. If URO/LOD stops evaluation, real time is extended.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param OutHandle 停止に使うハンドル / Handle used to stop the blow.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param Strength Blow のピーク強度 / Peak blow strength.
	 * @param Duration Rise/Decay 込みの合計実秒 / Total real seconds including Rise/Decay.
	 * @param RiseTime 0->ピークまでの立ち上がり時間（秒） / Time in seconds to rise from zero to peak.
	 * @param DecayTime ピーク->0 までの減衰時間（秒） / Time in seconds to decay from peak to zero.
	 * @param GustDirection Blow の方向（ワールド空間・非正規化可）。ゼロベクトルなら既存 ProceduralWind の風向き・空間・ボーンフィルタ等を継承 / Blow direction (world space; may be non-normalized). Zero vector inherits direction, space, and bone filters from an authored ProceduralWind if present.
	 * @param ExternalForceIndex 方向継承元の ExternalForces インデックス（-1なら最初の有効な ProceduralWind から継承） / ExternalForces index used as the inheritance source; -1 falls back to the first enabled ProceduralWind.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", AdvancedDisplay = "GustDirection,ExternalForceIndex"))
	static FKawaiiPhysicsReference StartProceduralWindBlow(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		FKawaiiPhysicsTransientForceHandle& OutHandle,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		float Strength = 100.0f,
		float Duration = 3.0f,
		float RiseTime = 0.5f,
		float DecayTime = 1.0f,
		FVector GustDirection = FVector(0, 0, 0),
		int32 ExternalForceIndex = -1);

	/**
	 * ProceduralWind の動的パラメータ更新をリクエストする。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * Request a ProceduralWind dynamic parameter update. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 * @param ExecResult 対象 ProceduralWind へのアクセス結果 / Result of accessing the target ProceduralWind.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param ExternalForceIndex 対象の ExternalForces インデックス / Target ExternalForces index.
	 * @param Params ProceduralWind に適用する動的パラメータ / Dynamic parameters to apply to ProceduralWind.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetProceduralWindParameters(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		int32 ExternalForceIndex,
		const FKawaiiProceduralWindDynamicParams& Params);

	/**
	 * ProceduralWind の現在の動的パラメータを取得する。全 bOverride フラグは true で返るため、一部だけ変えて SetProceduralWindParameters に渡す用途（例: 風向きだけ変更）にそのまま使える。未反映の PendingRequest（同フレーム内の Set 内容）も反映済みの値として返る。
	 * Get the current ProceduralWind dynamic parameters. Every bOverride flag comes back true, so the result can be passed straight into SetProceduralWindParameters after changing only a few fields (e.g. wind direction only). Unapplied PendingRequest content (a Set issued earlier in the same frame) is also returned as if already applied.
	 * @param ExecResult 対象 ProceduralWind へのアクセス結果 / Result of accessing the target ProceduralWind.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param ExternalForceIndex 対象の ExternalForces インデックス / Target ExternalForces index.
	 * @param OutParams 取得した ProceduralWind の動的パラメータ / Retrieved ProceduralWind dynamic parameters.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference GetProceduralWindParameters(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		const int32 ExternalForceIndex,
		FKawaiiProceduralWindDynamicParams& OutParams);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、enabled な authored ProceduralWind ごとにランタイム専用の一時 ProceduralWind 突風をスポーンする。有効な authored ProceduralWind がないノードではデフォルト突風を 1 つスポーンし、複数の同時突風は加算される。
	 * 突風は明示的な gameplay イベントとして扱われ、authored ProceduralWind の有無・有効無効に関わらず対象ノードで発火する（対象を絞るには FilterTags を使用）。
	 * 突風はノード再初期化時に失われる。キュー経由でスレッドセーフに適用され、次回 Evaluate から反映される。
	 * 同時に保持できる一時外力はノードあたり MaxTransientExternalForces（8）件までで、超過分は最古から破棄される。
	 * Spawn gusts on target KawaiiPhysics nodes in a component as runtime-only transient ProceduralWind entries, one per enabled authored ProceduralWind. Nodes without enabled authored ProceduralWind entries spawn one default gust, and multiple simultaneous gusts stack.
	 * Gusts are explicit gameplay events: they fire on matched nodes regardless of whether authored ProceduralWind entries exist or are enabled (use FilterTags to narrow targets).
	 * Gusts are lost on node re-initialization. Applied thread-safely through a queue and takes effect from the next Evaluate.
	 * Transient forces are capped at MaxTransientExternalForces (8) per node; the oldest entries are dropped beyond that.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param Strength 突風のピーク強度 / Peak gust strength.
	 * @param RiseTime 0->ピークまでの立ち上がり時間（秒） / Time in seconds to rise from zero to peak.
	 * @param DecayTime ピーク->0 までの減衰時間（秒） / Time in seconds to decay from peak to zero.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @param GustDirection 突風の方向（ワールド空間・非正規化可）。ゼロベクトルなら既存 ProceduralWind の風向き・空間・ボーンフィルタ等を継承 / Gust direction (world space; may be non-normalized). Zero vector inherits direction, space, and bone filters from an authored ProceduralWind if present.
	 * エンベロープは Rise→Hold→Decay の台形（HoldTime=0 で従来の三角波） / The envelope is a Rise-Hold-Decay trapezoid (HoldTime=0 keeps the legacy triangular shape).
	 * @param HoldTime ピーク強度を保持する時間（秒） / Time in seconds to hold the peak strength.
	 * @return 突風リクエストをキューしたノード数 / Number of nodes where gust requests were queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 TriggerProceduralWindGustOnComponent(
		USkeletalMeshComponent* MeshComp,
		float Strength,
		float RiseTime,
		float DecayTime,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false,
		FVector GustDirection = FVector(0, 0, 0),
		float HoldTime = 0.0f);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、ハンドルで停止できるランタイム専用 ProceduralWind Blow を開始する。Duration は Rise/Decay 込みの合計実秒で、TimeScale を継承せず実時間で動作する（wind 時間で動く TriggerProceduralWindGustOnComponent とは異なる）。
	 * エンベロープは Rise→Hold→Decay の台形（Hold=max(0,Duration-Rise-Decay)、Rise+Decay>Duration は比例圧縮）。同時に保持できる transient スロットは上限8で、超過時は最古が破棄される。破棄された風のハンドルは失効し Stop は no-op。URO/LOD で評価が止まると実時間が伸びる。InheritAllWinds ファンアウトが上限8を超えた場合、共通ハンドルは保持された transient のみを指す。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Start runtime-only ProceduralWind blows on target KawaiiPhysics nodes in a component that can be stopped by handle. Duration is total real seconds including Rise/Decay, does not inherit TimeScale, and runs in real time (unlike TriggerProceduralWindGustOnComponent, which runs in wind time).
	 * The envelope is a Rise-Hold-Decay trapezoid (Hold=max(0,Duration-Rise-Decay); Rise+Decay>Duration is scaled proportionally). Transient slots are capped at 8; beyond the cap, the oldest entry is evicted. Handles for evicted winds become stale and Stop is a no-op. If URO/LOD stops evaluation, real time is extended. If InheritAllWinds fan-out exceeds the cap of 8, the shared handle only points to retained transients.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param OutHandle 停止に使うハンドル / Handle used to stop the blows.
	 * @param Strength Blow のピーク強度 / Peak blow strength.
	 * @param Duration Rise/Decay 込みの合計実秒 / Total real seconds including Rise/Decay.
	 * @param RiseTime 0->ピークまでの立ち上がり時間（秒） / Time in seconds to rise from zero to peak.
	 * @param DecayTime ピーク->0 までの減衰時間（秒） / Time in seconds to decay from peak to zero.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @param GustDirection Blow の方向（ワールド空間・非正規化可）。ゼロベクトルなら既存 ProceduralWind の風向き・空間・ボーンフィルタ等を継承 / Blow direction (world space; may be non-normalized). Zero vector inherits direction, space, and bone filters from authored ProceduralWind entries if present.
	 * @return Blow リクエストをキューしたノード数 / Number of nodes where blow requests were queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags", AdvancedDisplay = "GustDirection"))
	static int32 StartProceduralWindBlowOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientForceHandle& OutHandle,
		float Strength,
		float Duration,
		float RiseTime,
		float DecayTime,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false,
		FVector GustDirection = FVector(0, 0, 0));

	/**
	 * ハンドルに一致する一時外力の停止をリクエストする。ProceduralWind は現在値から BlendOutTime（wind 時間）で線形フェードし、汎用外力は寿命短縮のみ行う。BlendOutTime=0 は即時除去。ハンドル不一致は no-op。
	 * Request stopping a transient external force that matches the handle. ProceduralWind fades linearly from the current value over BlendOutTime (wind time); generic external forces only have their lifetime shortened. BlendOutTime=0 removes immediately. Handle mismatch is a no-op.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param Handle 停止対象のハンドル / Handle to stop.
	 * @param BlendOutTime フェードアウト時間（wind 時間） / Fade-out time in wind time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference StopTransientExternalForce(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		FKawaiiPhysicsTransientForceHandle Handle,
		float BlendOutTime = 0.5f);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、ハンドルに一致する一時外力の停止をリクエストする。ProceduralWind は現在値から BlendOutTime（wind 時間）で線形フェードし、汎用外力は寿命短縮のみ行う。BlendOutTime=0 は即時除去。ハンドル不一致は no-op。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Request stopping transient external forces that match the handle on target KawaiiPhysics nodes in a component. ProceduralWind fades linearly from the current value over BlendOutTime (wind time); generic external forces only have their lifetime shortened. BlendOutTime=0 removes immediately. Handle mismatch is a no-op.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param Handle 停止対象のハンドル / Handle to stop.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @param BlendOutTime フェードアウト時間（wind 時間） / Fade-out time in wind time.
	 * @return 停止リクエストを送ったノード数 / Number of nodes where stop requests were sent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 StopTransientExternalForcesOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientForceHandle Handle,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false,
		float BlendOutTime = 0.5f);

	/**
	 * 物理設定への一時的な倍率オーバーライドを開始する。ベースの設定値は書き換えず、毎フレーム再計算される各ボーンの実効値へ倍率を乗算するため、期間終了後は常に元の挙動へ戻る。
	 * Duration は BlendInTime/BlendOutTime を含めた合計秒で、エンベロープは BlendIn→Hold→BlendOut の台形（Hold=max(0,Duration-BlendIn-BlendOut)、BlendIn+BlendOut>Duration は比例圧縮）。Duration<=0 は何もキューせず ExecResult=NotValid・ハンドル未設定で返る。
	 * 進行はアニメーションの DeltaTime ベースのため、URO や LOD で評価が止まると実時間としては長く伸びる。
	 * 同時に保持できるオーバーライドはノードあたり8件までで、超過時は最古が破棄される。破棄されたオーバーライドのハンドルは失効し Stop は no-op。ノード再初期化や BP 再コンパイルでも失われる。
	 * 複数のオーバーライドは各成分ごとに乗算で合成される。
	 * エッジケース: LimitAngle はベースが0（無制限）なら倍率に関わらず無制限のまま、ベースが0より大きいボーンは倍率0でも無制限へは反転しない。
	 * WorldDampingLocation/Rotation は実際の反映率が (1 - 値) のため意味が反転し、倍率1未満では揺れが増える。Radius の倍率0はワールドコリジョンのスイープと押し出しを実質無効にする。
	 * Start a temporary multiplier override for the physics settings. The base settings are never rewritten: the multipliers are applied to each bone's effective values, which are recomputed every frame, so the original behavior always comes back once the override ends.
	 * Duration is the total seconds including BlendInTime/BlendOutTime, and the envelope is a BlendIn-Hold-BlendOut trapezoid (Hold=max(0,Duration-BlendIn-BlendOut); BlendIn+BlendOut>Duration is scaled proportionally). Duration<=0 queues nothing and returns NotValid with an unset handle.
	 * Progress is driven by the animation DeltaTime, so if URO or LOD stops evaluation the override lasts longer in real time.
	 * Overrides are capped at 8 per node; beyond the cap, the oldest entry is evicted. Handles for evicted overrides become stale and Stop is a no-op. They are also lost on node re-initialization or BP recompile.
	 * Multiple overrides are composed by multiplying each component.
	 * Edge cases: LimitAngle stays unlimited whatever the multiplier is when the base value is 0 (unlimited), and bones with a base above 0 never flip back to unlimited even with a multiplier of 0.
	 * WorldDampingLocation/Rotation have inverted semantics because the actual reflection factor is (1 - value): a multiplier below 1 increases the sway. A Radius multiplier of 0 effectively disables the world collision sweep and push-out.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param OutHandle 停止に使うハンドル / Handle used to stop the override.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param SettingsScale 物理設定へ適用する倍率（全項目1.0で変更なし） / Multipliers applied to the physics settings (all 1.0 means no change).
	 * @param Duration BlendIn/BlendOut 込みの合計秒 / Total seconds including BlendIn/BlendOut.
	 * @param BlendInTime 倍率0%->100% へのブレンドイン時間（秒） / Time in seconds to blend in from 0% to 100% of the multipliers.
	 * @param BlendOutTime 倍率100%->0% へのブレンドアウト時間（秒） / Time in seconds to blend out from 100% to 0% of the multipliers.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference StartPhysicsSettingsOverride(
		EKawaiiPhysicsAccessResult& ExecResult,
		FKawaiiPhysicsTransientForceHandle& OutHandle,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		FKawaiiPhysicsSettingsScale SettingsScale,
		float Duration = 2.0f,
		float BlendInTime = 0.2f,
		float BlendOutTime = 0.5f);

	/**
	 * ハンドルに一致する物理設定オーバーライドの停止をリクエストする。現在の適用率から BlendOutTime で 0 へ線形フェードする。BlendOutTime=0 は即時除去。
	 * まだ評価されていない（pending の）同一ハンドルへの Stop は BlendOutTime を上書きする。ハンドル不一致・失効ハンドルは no-op。
	 * Request stopping a physics settings override that matches the handle. It fades linearly from the current applied ratio to 0 over BlendOutTime. BlendOutTime=0 removes immediately.
	 * A Stop for the same handle that is still pending (not yet evaluated) overwrites its BlendOutTime. Handle mismatch or a stale handle is a no-op.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param Handle 停止対象のハンドル / Handle to stop.
	 * @param BlendOutTime フェードアウト時間（秒） / Fade-out time in seconds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference StopPhysicsSettingsOverride(
		EKawaiiPhysicsAccessResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		FKawaiiPhysicsTransientForceHandle Handle,
		float BlendOutTime = 0.5f);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、物理設定への一時的な倍率オーバーライドを共通の1ハンドルで開始する。ベースの設定値は書き換えず、毎フレーム再計算される各ボーンの実効値へ倍率を乗算するため、期間終了後は常に元の挙動へ戻る。
	 * Duration は BlendInTime/BlendOutTime を含めた合計秒で、エンベロープは BlendIn→Hold→BlendOut の台形（Hold=max(0,Duration-BlendIn-BlendOut)、BlendIn+BlendOut>Duration は比例圧縮）。Duration<=0 は何もキューせず 0 を返す（ハンドル未設定）。
	 * 進行はアニメーションの DeltaTime ベースのため、URO や LOD で評価が止まると実時間としては長く伸びる。
	 * 同時に保持できるオーバーライドはノードあたり8件までで、超過時は最古が破棄される。破棄されたオーバーライドのハンドルは失効し Stop は no-op。ノード再初期化や BP 再コンパイルでも失われる。
	 * 複数のオーバーライドは各成分ごとに乗算で合成される。
	 * エッジケース: LimitAngle はベースが0（無制限）なら倍率に関わらず無制限のまま、ベースが0より大きいボーンは倍率0でも無制限へは反転しない。
	 * WorldDampingLocation/Rotation は実際の反映率が (1 - 値) のため意味が反転し、倍率1未満では揺れが増える。Radius の倍率0はワールドコリジョンのスイープと押し出しを実質無効にする。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Start temporary multiplier overrides for the physics settings on target KawaiiPhysics nodes in a component under one shared handle. The base settings are never rewritten: the multipliers are applied to each bone's effective values, which are recomputed every frame, so the original behavior always comes back once the override ends.
	 * Duration is the total seconds including BlendInTime/BlendOutTime, and the envelope is a BlendIn-Hold-BlendOut trapezoid (Hold=max(0,Duration-BlendIn-BlendOut); BlendIn+BlendOut>Duration is scaled proportionally). Duration<=0 queues nothing and returns 0 with an unset handle.
	 * Progress is driven by the animation DeltaTime, so if URO or LOD stops evaluation the override lasts longer in real time.
	 * Overrides are capped at 8 per node; beyond the cap, the oldest entry is evicted. Handles for evicted overrides become stale and Stop is a no-op. They are also lost on node re-initialization or BP recompile.
	 * Multiple overrides are composed by multiplying each component.
	 * Edge cases: LimitAngle stays unlimited whatever the multiplier is when the base value is 0 (unlimited), and bones with a base above 0 never flip back to unlimited even with a multiplier of 0.
	 * WorldDampingLocation/Rotation have inverted semantics because the actual reflection factor is (1 - value): a multiplier below 1 increases the sway. A Radius multiplier of 0 effectively disables the world collision sweep and push-out.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param OutHandle 停止に使うハンドル（全ノード共通） / Handle used to stop the overrides (shared by every node).
	 * @param SettingsScale 物理設定へ適用する倍率（全項目1.0で変更なし） / Multipliers applied to the physics settings (all 1.0 means no change).
	 * @param Duration BlendIn/BlendOut 込みの合計秒 / Total seconds including BlendIn/BlendOut.
	 * @param BlendInTime 倍率0%->100% へのブレンドイン時間（秒） / Time in seconds to blend in from 0% to 100% of the multipliers.
	 * @param BlendOutTime 倍率100%->0% へのブレンドアウト時間（秒） / Time in seconds to blend out from 100% to 0% of the multipliers.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @return オーバーライドをキューしたノード数 / Number of nodes where overrides were queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 StartPhysicsSettingsOverrideOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientForceHandle& OutHandle,
		FKawaiiPhysicsSettingsScale SettingsScale,
		float Duration,
		float BlendInTime,
		float BlendOutTime,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、ハンドルに一致する物理設定オーバーライドの停止をリクエストする。現在の適用率から BlendOutTime で 0 へ線形フェードする。BlendOutTime=0 は即時除去。
	 * まだ評価されていない（pending の）同一ハンドルへの Stop は BlendOutTime を上書きする。ハンドル不一致・失効ハンドルは no-op。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Request stopping physics settings overrides that match the handle on target KawaiiPhysics nodes in a component. They fade linearly from the current applied ratio to 0 over BlendOutTime. BlendOutTime=0 removes immediately.
	 * A Stop for the same handle that is still pending (not yet evaluated) overwrites its BlendOutTime. Handle mismatch or a stale handle is a no-op.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param Handle 停止対象のハンドル / Handle to stop.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @param BlendOutTime フェードアウト時間（秒） / Fade-out time in seconds.
	 * @return 停止リクエストを送ったノード数 / Number of nodes where stop requests were sent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 StopPhysicsSettingsOverridesOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientForceHandle Handle,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false,
		float BlendOutTime = 0.5f);

	/**
	 * Id が設定済みかだけを返す。対象の風が現在も生存しているかの確認ではない（ノード再初期化・上限超過破棄後も true のまま。その場合 Stop は何もしない）。
	 * Returns only whether Id is set. This does not check whether the target wind is still alive (it remains true after node re-init or cap eviction; Stop then does nothing).
	 */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool IsTransientForceHandleSet(const FKawaiiPhysicsTransientForceHandle& Handle);

	/**
	 * Component 内の ProceduralWind へ動的パラメータ更新を一括リクエストする。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * Request dynamic parameter updates for ProceduralWind entries in a component. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param Params ProceduralWind に適用する動的パラメータ / Dynamic parameters to apply to ProceduralWind.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 SetProceduralWindParametersOnComponent(
		USkeletalMeshComponent* MeshComp,
		const FKawaiiProceduralWindDynamicParams& Params,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false);

	/**
	 * DataAsset のプリセットから ProceduralWind の動的パラメータ更新をリクエストする。プリセットの調整可能な12項目のみ上書きし、
	 * TimeScale, WindDirection, RipplePhaseOffset, StrengthCyclePhaseOffset, WindDirectionNoisePeriod は維持する。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * PresetDataAsset が null または Presets 配列が空の場合は、組み込み既定プリセット3件（KawaiiPhysics.WindPreset.Breeze / Strong / Storm）からタグ照合する。
	 * 呼び出し側で DataAsset を事前ロードし、シミュレーション中に編集または再ロードしないこと。
	 * Request a ProceduralWind dynamic parameter update from a DataAsset preset. Only the preset's 12 tunable fields are overwritten;
	 * TimeScale, WindDirection, RipplePhaseOffset, StrengthCyclePhaseOffset, and WindDirectionNoisePeriod are left untouched. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 * When PresetDataAsset is null or its Presets array is empty, the tag is matched against the 3 built-in default presets (KawaiiPhysics.WindPreset.Breeze / Strong / Storm).
	 * The DataAsset must be pre-loaded by the caller and must not be edited or reloaded while simulation is running.
	 * @param ExecResult 対象 ProceduralWind へのアクセス結果 / Result of accessing the target ProceduralWind.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param ExternalForceIndex 対象の ExternalForces インデックス / Target ExternalForces index.
	 * @param PresetDataAsset 参照する風プリセット DataAsset（null なら組み込みプリセット） / Wind preset DataAsset to resolve; null uses built-in presets.
	 * @param PresetTag 適用するプリセットタグ / Preset tag to apply.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference ApplyProceduralWindPreset(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		int32 ExternalForceIndex,
		const UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset,
		FGameplayTag PresetTag);

	/**
	 * DataAsset のプリセットから Component 内の ProceduralWind へ動的パラメータ更新を一括リクエストする。プリセットの調整可能な12項目のみ上書きし、
	 * TimeScale, WindDirection, RipplePhaseOffset, StrengthCyclePhaseOffset, WindDirectionNoisePeriod は維持する。PendingRequest 経由でスレッドセーフ。次フレームの PreApply で反映。
	 * PresetDataAsset が null または Presets 配列が空の場合は、組み込み既定プリセット3件（KawaiiPhysics.WindPreset.Breeze / Strong / Storm）からタグ照合する。
	 * 呼び出し側で DataAsset を事前ロードし、シミュレーション中に編集または再ロードしないこと。
	 * Request dynamic parameter updates for ProceduralWind entries in a component from a DataAsset preset. Only the preset's 12 tunable fields are overwritten;
	 * TimeScale, WindDirection, RipplePhaseOffset, StrengthCyclePhaseOffset, and WindDirectionNoisePeriod are left untouched. Thread-safe via PendingRequest. Applied in the next frame's PreApply.
	 * When PresetDataAsset is null or its Presets array is empty, the tag is matched against the 3 built-in default presets (KawaiiPhysics.WindPreset.Breeze / Strong / Storm).
	 * The DataAsset must be pre-loaded by the caller and must not be edited or reloaded while simulation is running.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param PresetDataAsset 参照する風プリセット DataAsset（null なら組み込みプリセット） / Wind preset DataAsset to resolve; null uses built-in presets.
	 * @param PresetTag 適用するプリセットタグ / Preset tag to apply.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 ApplyProceduralWindPresetOnComponent(
		USkeletalMeshComponent* MeshComp,
		const UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset,
		FGameplayTag PresetTag,
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

	/**
	 * ExternalForces 配列から指定型（派生型含む）の index を検索する。SetExternalForce*Property / TriggerProceduralWindGust 等の ExternalForceIndex 指定に使える。
	 * スレッド契約は他のランタイムアクセス API と同じ（ノード評価文脈の BlueprintThreadSafe、または非評価中の GameThread）。
	 * Finds indices in the ExternalForces array by the specified type, including derived types. Useful for ExternalForceIndex parameters in SetExternalForce*Property, TriggerProceduralWindGust, and similar APIs.
	 * The threading contract is the same as other runtime access APIs: BlueprintThreadSafe in a node evaluation context, or GameThread while not evaluating.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param StructType 検索する外力型 / External force struct type to find.
	 * @param bEnabledOnly true の場合は bIsEnabled の外力のみ / If true, only forces with bIsEnabled are returned.
	 * @return 一致した ExternalForces index の配列 / Matching ExternalForces indices.
	 */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static TArray<int32> FindExternalForceIndicesByStruct(
		const FKawaiiPhysicsReference& KawaiiPhysics,
		UScriptStruct* StructType,
		bool bEnabledOnly = false);

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
