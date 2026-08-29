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

namespace KawaiiPhysics
{
	// 一時外力 struct がキュー可能か検証する（IsValid / FKawaiiPhysics_ExternalForce 派生 / live UObject 参照なし）。拒否理由を Warning ログに出す。
	// Validate that a transient external-force struct can be queued (IsValid / derives from FKawaiiPhysics_ExternalForce / no live UObject references). Logs a warning with the reason when rejected.
	KAWAIIPHYSICS_API bool CanQueueTransientExternalForce(const FInstancedStruct& ExternalForce, const TCHAR* ContextName);

	// 複数ノードへ同一ハンドルで一時外力をキューする。ノードごとに struct をコピーする。OutHandle は呼び出しごとに未設定（Id=0）へリセットされ、1 件以上適用時のみ設定される。検証（CanQueueTransientExternalForce）を内包する。
	// Queue a transient external force to multiple nodes under one shared handle, copying the struct per node. OutHandle is reset to unset (Id=0) on every call and set only when at least one node is applied. Performs CanQueueTransientExternalForce validation internally.
	KAWAIIPHYSICS_API int32 QueueTransientExternalForceToNodes(TArrayView<FAnimNode_KawaiiPhysics* const> Nodes,
	                                                          const FInstancedStruct& ExternalForce,
	                                                          float LifetimeSeconds,
	                                                          FKawaiiPhysicsTransientHandle& OutHandle);
}

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
			TEXT("SetBoneSubdivisionCollisionOnly"),
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
			TEXT("SetSkipMirroredBoneWithExistingCollision"),
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

	/**
	 * 非推奨。AddExternalForceWithExecResult を使ってください。
	 * Deprecated. Use AddExternalForceWithExecResult.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, DeprecatedFunction, DeprecationMessage = "Use AddExternalForceWithExecResult instead. This node will keep working but is no longer recommended."))
	static bool AddExternalForce(const FKawaiiPhysicsReference& KawaiiPhysics,
	                             UPARAM(meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) FInstancedStruct& ExternalForce,
	                             UObject* Owner, bool bIsOneShot = false);

	/** Add ExternalForces to SkeletalMeshComponent */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool AddExternalForcesOnComponent(USkeletalMeshComponent* MeshComp,
	                                         UPARAM(ref, meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) TArray<FInstancedStruct>& ExternalForces,
	                                         UObject* Owner,
	                                         UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                         bool bFilterExactMatch = false,
	                                         bool bIsOneShot = false);

	/**
	 * 非推奨（v1.22.0）: AddExternalForcesOnComponent を使う / Deprecated (v1.22.0): use AddExternalForcesOnComponent.
	 * Add ExternalForces to SkeletalMeshComponent
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, DeprecatedFunction, DeprecationMessage = "Use AddExternalForcesOnComponent"))
	static bool AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
	                                         UPARAM(ref, meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) TArray<FInstancedStruct>& ExternalForces,
	                                         UObject* Owner,
	                                         UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                         bool bFilterExactMatch = false,
	                                         bool bIsOneShot = false);

	/** Remove ExternalForces from SkeletalMeshComponent (by Owner) */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool RemoveExternalForcesOnComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
	                                              UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                              bool bFilterExactMatch = false);

	/**
	 * 非推奨（v1.22.0）: RemoveExternalForcesOnComponent を使う / Deprecated (v1.22.0): use RemoveExternalForcesOnComponent.
	 * Remove ExternalForces from SkeletalMeshComponent (by Owner)
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, DeprecatedFunction, DeprecationMessage = "Use RemoveExternalForcesOnComponent"))
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
		FKawaiiPhysicsTransientHandle& OutHandle,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		UPARAM(meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) FInstancedStruct ExternalForce,
		float LifetimeSeconds = 3.0f);

	/**
	 * Component 内の Kawaii Physics ノード（Linked / PostProcess 含む）へ、Tag フィルタ付きで一時外力をキューする。全ノードで同一ハンドルを共有し、StopTransientExternalForceOnComponent で一括停止できる。
	 * 戻り値は適用したノード数。OutHandle は呼び出しごとに未設定（Id=0）へリセットされ、1 件以上適用時のみ設定される。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Queues a transient external force to every Kawaii Physics node in the component (including linked / post-process instances) that passes the tag filter. All nodes share one handle so StopTransientExternalForceOnComponent can stop them together.
	 * Returns the number of nodes applied. OutHandle is reset to unset (Id=0) on every call and set only when at least one node is applied.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param OutHandle 早期除去に使うハンドル / Handle used for early removal.
	 * @param ExternalForce 追加する FKawaiiPhysics_ExternalForce 派生外力 / FKawaiiPhysics_ExternalForce-derived force to add.
	 * @param LifetimeSeconds 自動除去までの寿命（秒） / Lifetime in seconds before automatic removal.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @return 一時外力リクエストをキューしたノード数 / Number of nodes where transient force requests were queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 AddTransientExternalForceOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientHandle& OutHandle,
		UPARAM(meta=(BaseStruct="/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct)) FInstancedStruct ExternalForce,
		float LifetimeSeconds = 3.0f,
		const FGameplayTagContainer& FilterTags = FGameplayTagContainer(),
		bool bFilterExactMatch = false);

	/**
	 * ハンドルで停止できるランタイム専用 ProceduralWind Gust を開始する。Duration は Rise + Hold + Decay の合計実秒で、Hold = max(0, Duration - RiseTime - DecayTime)（KawaiiPhysics::ResolveWindGustEnvelope）。
	 * bRealTimeEnvelope: true = 実秒で進行（TimeScale を 1 に強制）/ false = wind 時間で進行（ProceduralWind の TimeScale の影響を受ける。旧 Trigger API 相当）。同時に保持できる transient スロットは上限8で、超過時は最古が破棄される。破棄された風のハンドルは失効し Stop は no-op。URO/LOD で評価が止まると実時間が伸びる。
	 * Start a runtime-only ProceduralWind gust that can be stopped by handle. Duration is the total real seconds of Rise + Hold + Decay, where Hold = max(0, Duration - RiseTime - DecayTime) (KawaiiPhysics::ResolveWindGustEnvelope).
	 * bRealTimeEnvelope: true = progress in real seconds (forces TimeScale to 1) / false = progress in wind time (affected by ProceduralWind TimeScale; equivalent to the old Trigger API). Transient slots are capped at 8; beyond the cap, the oldest entry is evicted. Handles for evicted winds become stale and Stop is a no-op. If URO/LOD stops evaluation, real time is extended.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param OutHandle 停止に使うハンドル / Handle used to stop the gust.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param Strength Gust のピーク強度 / Peak gust strength.
	 * @param Duration Rise + Hold + Decay の合計実秒。Hold = max(0, Duration - RiseTime - DecayTime) / Total real seconds of Rise + Hold + Decay. Hold = max(0, Duration - RiseTime - DecayTime).
	 * @param RiseTime 0->ピークまでの立ち上がり時間（秒） / Time in seconds to rise from zero to peak.
	 * @param DecayTime ピーク->0 までの減衰時間（秒） / Time in seconds to decay from peak to zero.
	 * @param GustDirection Gust の方向（ワールド空間・非正規化可）。ゼロベクトルなら既存 ProceduralWind の風向き・空間・ボーンフィルタ等を継承 / Gust direction (world space; may be non-normalized). Zero vector inherits direction, space, and bone filters from an authored ProceduralWind if present.
	 * @param ExternalForceIndex 方向継承元の ExternalForces インデックス（-1なら最初の有効な ProceduralWind から継承） / ExternalForces index used as the inheritance source; -1 falls back to the first enabled ProceduralWind.
	 * @param bRealTimeEnvelope true = 実秒で進行（TimeScale を 1 に強制）/ false = wind 時間で進行（ProceduralWind の TimeScale の影響を受ける） / true = progress in real seconds (forces TimeScale to 1) / false = progress in wind time (affected by ProceduralWind TimeScale).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", AdvancedDisplay = "GustDirection,ExternalForceIndex,bRealTimeEnvelope"))
	static FKawaiiPhysicsReference StartProceduralWindGust(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult,
		FKawaiiPhysicsTransientHandle& OutHandle,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		float Strength = 100.0f,
		float Duration = 3.0f,
		float RiseTime = 0.5f,
		float DecayTime = 1.0f,
		FVector GustDirection = FVector(0, 0, 0),
		int32 ExternalForceIndex = -1,
		bool bRealTimeEnvelope = true);

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
	 * Component 内の対象 KawaiiPhysics ノードへ、ハンドルで停止できるランタイム専用 ProceduralWind Gust を開始する。Duration は Rise + Hold + Decay の合計実秒で、Hold = max(0, Duration - RiseTime - DecayTime)（KawaiiPhysics::ResolveWindGustEnvelope）。
	 * bRealTimeEnvelope: true = 実秒で進行（TimeScale を 1 に強制）/ false = wind 時間で進行（ProceduralWind の TimeScale の影響を受ける。旧 Trigger API 相当）。同時に保持できる transient スロットは上限8で、超過時は最古が破棄される。破棄された風のハンドルは失効し Stop は no-op。URO/LOD で評価が止まると実時間が伸びる。InheritAllWinds ファンアウトが上限8を超えた場合、共通ハンドルは保持された transient のみを指す。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Start runtime-only ProceduralWind gusts on target KawaiiPhysics nodes in a component that can be stopped by handle. Duration is the total real seconds of Rise + Hold + Decay, where Hold = max(0, Duration - RiseTime - DecayTime) (KawaiiPhysics::ResolveWindGustEnvelope).
	 * bRealTimeEnvelope: true = progress in real seconds (forces TimeScale to 1) / false = progress in wind time (affected by ProceduralWind TimeScale; equivalent to the old Trigger API). Transient slots are capped at 8; beyond the cap, the oldest entry is evicted. Handles for evicted winds become stale and Stop is a no-op. If URO/LOD stops evaluation, real time is extended. If InheritAllWinds fan-out exceeds the cap of 8, the shared handle only points to retained transients.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param OutHandle 停止に使うハンドル / Handle used to stop the gusts.
	 * @param Strength Gust のピーク強度 / Peak gust strength.
	 * @param Duration Rise + Hold + Decay の合計実秒。Hold = max(0, Duration - RiseTime - DecayTime) / Total real seconds of Rise + Hold + Decay. Hold = max(0, Duration - RiseTime - DecayTime).
	 * @param RiseTime 0->ピークまでの立ち上がり時間（秒） / Time in seconds to rise from zero to peak.
	 * @param DecayTime ピーク->0 までの減衰時間（秒） / Time in seconds to decay from peak to zero.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @param GustDirection Gust の方向（ワールド空間・非正規化可）。ゼロベクトルなら既存 ProceduralWind の風向き・空間・ボーンフィルタ等を継承 / Gust direction (world space; may be non-normalized). Zero vector inherits direction, space, and bone filters from authored ProceduralWind entries if present.
	 * @param bRealTimeEnvelope true = 実秒で進行（TimeScale を 1 に強制）/ false = wind 時間で進行（ProceduralWind の TimeScale の影響を受ける） / true = progress in real seconds (forces TimeScale to 1) / false = progress in wind time (affected by ProceduralWind TimeScale).
	 * @return Gust リクエストをキューしたノード数 / Number of nodes where gust requests were queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags", AdvancedDisplay = "GustDirection,bRealTimeEnvelope"))
	static int32 StartProceduralWindGustOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientHandle& OutHandle,
		float Strength,
		float Duration,
		float RiseTime,
		float DecayTime,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false,
		FVector GustDirection = FVector(0, 0, 0),
		bool bRealTimeEnvelope = true);

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
		FKawaiiPhysicsTransientHandle Handle,
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
	static int32 StopTransientExternalForceOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientHandle Handle,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false,
		float BlendOutTime = 0.5f);

	// Start 要求を Duration/BlendIn/BlendOut から組み立てる純粋ヘルパー。Duration==0 は false（no-op） / Pure helper building a Start request; Duration==0 returns false (no-op)
	static bool BuildSettingsMultiplierStartRequest(const FKawaiiPhysicsSettingsMultiplier& Scale, float Duration,
	                                                float BlendInTime, float BlendOutTime,
	                                                FKawaiiPhysicsSettingsMultiplierRequest& OutRequest);

	/**
	 * 物理設定への一時的な倍率を開始する。ベースの設定値は書き換えず、毎フレーム再計算される各ボーンの実効値へ倍率を乗算するため、期間終了後は常に元の挙動へ戻る。
	 * Duration は BlendInTime/BlendOutTime を含めた合計秒で、エンベロープは BlendIn→Hold→BlendOut の台形（Hold=max(0,Duration-BlendIn-BlendOut)、BlendIn+BlendOut>Duration は比例圧縮）。Duration < 0: Stop まで保持。解放時のフェード時間は StopPhysicsSettingsMultiplier 側の BlendOutTime で指定し、この呼び出しの BlendOutTime は無視される。Duration == 0: no-op。
	 * 進行はアニメーションの DeltaTime ベースのため、URO や LOD で評価が止まると実時間としては長く伸びる。
	 * 同一ハンドルの既存の倍率がある場合は置換される。Stop は外部駆動倍率にも有効で、現在の Alpha から BlendOutTime で 0 へフェードする。
	 * 同時に保持できる倍率はノードあたり8件までで、超過時は最古が破棄される。破棄された倍率のハンドルは失効し Stop は no-op。ノード再初期化や BP 再コンパイルでも失われる。
	 * 複数の倍率は各成分ごとに乗算で合成される。
	 * エッジケース: LimitAngle はベースが0（無制限）なら倍率に関わらず無制限のまま、ベースが0より大きいボーンは倍率0でも無制限へは反転しない。
	 * WorldDampingLocation/Rotation は実際の反映率が (1 - 値) のため意味が反転し、倍率1未満では揺れが増える。Radius の倍率0はワールドコリジョンのスイープと押し出しを実質無効にする。
	 * Start a temporary multiplier for the physics settings. The base settings are never rewritten: the multipliers are applied to each bone's effective values, which are recomputed every frame, so the original behavior always comes back once the multiplier ends.
	 * Duration is the total seconds including BlendInTime/BlendOutTime, and the envelope is a BlendIn-Hold-BlendOut trapezoid (Hold=max(0,Duration-BlendIn-BlendOut); BlendIn+BlendOut>Duration is scaled proportionally). Duration < 0: hold until Stop. The release fade is taken from StopPhysicsSettingsMultiplier's BlendOutTime; this call's BlendOutTime is ignored. Duration == 0: no-op.
	 * Progress is driven by the animation DeltaTime, so if URO or LOD stops evaluation the multiplier lasts longer in real time.
	 * An existing multiplier with the same handle is replaced. Stop also works on externally driven multipliers and fades from the current Alpha to 0 over BlendOutTime.
	 * Multipliers are capped at 8 per node; beyond the cap, the oldest entry is evicted. Handles for evicted multipliers become stale and Stop is a no-op. They are also lost on node re-initialization or BP recompile.
	 * Multiple multipliers are composed by multiplying each component.
	 * Edge cases: LimitAngle stays unlimited whatever the multiplier is when the base value is 0 (unlimited), and bones with a base above 0 never flip back to unlimited even with a multiplier of 0.
	 * WorldDampingLocation/Rotation have inverted semantics because the actual reflection factor is (1 - value): a multiplier below 1 increases the sway. A Radius multiplier of 0 effectively disables the world collision sweep and push-out.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param OutHandle 停止に使うハンドル / Handle used to stop the multiplier.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param SettingsScale 物理設定へ適用する倍率（全項目1.0で変更なし） / Multipliers applied to the physics settings (all 1.0 means no change).
	 * @param Duration BlendIn/BlendOut 込みの合計秒。Duration < 0: Stop まで保持。解放時のフェード時間は StopPhysicsSettingsMultiplier 側の BlendOutTime で指定し、この呼び出しの BlendOutTime は無視される。Duration == 0: no-op. / Total seconds including BlendIn/BlendOut. Duration < 0: hold until Stop. The release fade is taken from StopPhysicsSettingsMultiplier's BlendOutTime; this call's BlendOutTime is ignored. Duration == 0: no-op.
	 * @param BlendInTime 倍率0%->100% へのブレンドイン時間（秒） / Time in seconds to blend in from 0% to 100% of the multipliers.
	 * @param BlendOutTime 倍率100%->0% へのブレンドアウト時間（秒）（Duration < 0 では無視） / Time in seconds to blend out from 100% to 0% of the multipliers (ignored when Duration < 0).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference StartPhysicsSettingsMultiplier(
		EKawaiiPhysicsAccessResult& ExecResult,
		FKawaiiPhysicsTransientHandle& OutHandle,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		FKawaiiPhysicsSettingsMultiplier SettingsScale,
		float Duration = 2.0f,
		float BlendInTime = 0.2f,
		float BlendOutTime = 0.5f);

	/**
	 * ハンドルに一致する物理設定倍率の停止をリクエストする。現在の適用率から BlendOutTime で 0 へ線形フェードする。BlendOutTime=0 は即時除去。
	 * 外部駆動倍率にも有効で、その時点の Alpha を起点にフェードアウトする。
	 * まだ評価されていない（pending の）同一ハンドルへの Stop は BlendOutTime を上書きする。ハンドル不一致・失効ハンドルは no-op。
	 * Request stopping a physics settings multiplier that matches the handle. It fades linearly from the current applied ratio to 0 over BlendOutTime. BlendOutTime=0 removes immediately.
	 * Also works on externally driven multipliers and fades out from the Alpha at the time of Stop.
	 * A Stop for the same handle that is still pending (not yet evaluated) overwrites its BlendOutTime. Handle mismatch or a stale handle is a no-op.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param Handle 停止対象のハンドル / Handle to stop.
	 * @param BlendOutTime フェードアウト時間（秒） / Fade-out time in seconds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference StopPhysicsSettingsMultiplier(
		EKawaiiPhysicsAccessResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		FKawaiiPhysicsTransientHandle Handle,
		float BlendOutTime = 0.5f);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、物理設定への一時的な倍率を共通の1ハンドルで開始する。ベースの設定値は書き換えず、毎フレーム再計算される各ボーンの実効値へ倍率を乗算するため、期間終了後は常に元の挙動へ戻る。
	 * Duration は BlendInTime/BlendOutTime を含めた合計秒で、エンベロープは BlendIn→Hold→BlendOut の台形（Hold=max(0,Duration-BlendIn-BlendOut)、BlendIn+BlendOut>Duration は比例圧縮）。Duration < 0: Stop まで保持。解放時のフェード時間は StopPhysicsSettingsMultiplier 側の BlendOutTime で指定し、この呼び出しの BlendOutTime は無視される。Duration == 0: no-op。
	 * 進行はアニメーションの DeltaTime ベースのため、URO や LOD で評価が止まると実時間としては長く伸びる。
	 * 同一ハンドルの既存の倍率がある場合は置換される。Stop は外部駆動倍率にも有効で、現在の Alpha から BlendOutTime で 0 へフェードする。
	 * 同時に保持できる倍率はノードあたり8件までで、超過時は最古が破棄される。破棄された倍率のハンドルは失効し Stop は no-op。ノード再初期化や BP 再コンパイルでも失われる。
	 * 複数の倍率は各成分ごとに乗算で合成される。
	 * エッジケース: LimitAngle はベースが0（無制限）なら倍率に関わらず無制限のまま、ベースが0より大きいボーンは倍率0でも無制限へは反転しない。
	 * WorldDampingLocation/Rotation は実際の反映率が (1 - 値) のため意味が反転し、倍率1未満では揺れが増える。Radius の倍率0はワールドコリジョンのスイープと押し出しを実質無効にする。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Start temporary multipliers for the physics settings on target KawaiiPhysics nodes in a component under one shared handle. The base settings are never rewritten: the multipliers are applied to each bone's effective values, which are recomputed every frame, so the original behavior always comes back once the multiplier ends.
	 * Duration is the total seconds including BlendInTime/BlendOutTime, and the envelope is a BlendIn-Hold-BlendOut trapezoid (Hold=max(0,Duration-BlendIn-BlendOut); BlendIn+BlendOut>Duration is scaled proportionally). Duration < 0: hold until Stop. The release fade is taken from StopPhysicsSettingsMultiplier's BlendOutTime; this call's BlendOutTime is ignored. Duration == 0: no-op.
	 * Progress is driven by the animation DeltaTime, so if URO or LOD stops evaluation the multiplier lasts longer in real time.
	 * An existing multiplier with the same handle is replaced. Stop also works on externally driven multipliers and fades from the current Alpha to 0 over BlendOutTime.
	 * Multipliers are capped at 8 per node; beyond the cap, the oldest entry is evicted. Handles for evicted multipliers become stale and Stop is a no-op. They are also lost on node re-initialization or BP recompile.
	 * Multiple multipliers are composed by multiplying each component.
	 * Edge cases: LimitAngle stays unlimited whatever the multiplier is when the base value is 0 (unlimited), and bones with a base above 0 never flip back to unlimited even with a multiplier of 0.
	 * WorldDampingLocation/Rotation have inverted semantics because the actual reflection factor is (1 - value): a multiplier below 1 increases the sway. A Radius multiplier of 0 effectively disables the world collision sweep and push-out.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param OutHandle 停止に使うハンドル（全ノード共通） / Handle used to stop the multipliers (shared by every node).
	 * @param SettingsScale 物理設定へ適用する倍率（全項目1.0で変更なし） / Multipliers applied to the physics settings (all 1.0 means no change).
	 * @param Duration BlendIn/BlendOut 込みの合計秒。Duration < 0: Stop まで保持。解放時のフェード時間は StopPhysicsSettingsMultiplier 側の BlendOutTime で指定し、この呼び出しの BlendOutTime は無視される。Duration == 0: no-op. / Total seconds including BlendIn/BlendOut. Duration < 0: hold until Stop. The release fade is taken from StopPhysicsSettingsMultiplier's BlendOutTime; this call's BlendOutTime is ignored. Duration == 0: no-op.
	 * @param BlendInTime 倍率0%->100% へのブレンドイン時間（秒） / Time in seconds to blend in from 0% to 100% of the multipliers.
	 * @param BlendOutTime 倍率100%->0% へのブレンドアウト時間（秒）（Duration < 0 では無視） / Time in seconds to blend out from 100% to 0% of the multipliers (ignored when Duration < 0).
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @return 倍率をキューしたノード数 / Number of nodes where multipliers were queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, AutoCreateRefTerm = "FilterTags"))
	static int32 StartPhysicsSettingsMultiplierOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientHandle& OutHandle,
		FKawaiiPhysicsSettingsMultiplier SettingsScale,
		float Duration,
		float BlendInTime,
		float BlendOutTime,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、ハンドルに一致する物理設定倍率の停止をリクエストする。現在の適用率から BlendOutTime で 0 へ線形フェードする。BlendOutTime=0 は即時除去。
	 * 外部駆動倍率にも有効で、その時点の Alpha を起点にフェードアウトする。
	 * まだ評価されていない（pending の）同一ハンドルへの Stop は BlendOutTime を上書きする。ハンドル不一致・失効ハンドルは no-op。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Request stopping physics settings multipliers that match the handle on target KawaiiPhysics nodes in a component. They fade linearly from the current applied ratio to 0 over BlendOutTime. BlendOutTime=0 removes immediately.
	 * Also works on externally driven multipliers and fades out from the Alpha at the time of Stop.
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
	static int32 StopPhysicsSettingsMultiplierOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientHandle Handle,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false,
		float BlendOutTime = 0.5f);

	/**
	 * 一時外力ハンドルを新規発行する。PushPhysicsSettingsMultiplier など、呼び出し側が同じハンドルで継続更新する API に渡す。
	 * BlueprintPure にはしない。Pure ノードにするとピン参照のたびに別ハンドルが発行され、Stop や同一項目更新が成立しないため。
	 * C++ 専用（Blueprint 非公開）。Blueprint からは Duration < 0 の Start を使う。
	 * Generate a new transient force handle. Pass it to APIs such as PushPhysicsSettingsMultiplier where the caller keeps updating with the same handle.
	 * This is intentionally not BlueprintPure: a Pure node would issue another handle every time a pin is read, breaking Stop and same-entry updates.
	 * C++ only (not exposed to Blueprint); Blueprint uses Start with Duration < 0.
	 */
	static FKawaiiPhysicsTransientHandle GenerateTransientHandle();

	/**
	 * 外部駆動の物理設定倍率を設定する。AnimNotifyState や Sequencer など、呼び出し側が毎フレームまたは値が変化した時に Alpha を押し込む用途向け。
	 * 内部時間では消えないため、不要になったら必ず StopPhysicsSettingsMultiplier で解放する。Stop は現在の Alpha から BlendOutTime で 0 へフェードし、BlendOutTime=0 なら即時除去する。
	 * GenerateTransientHandle などで事前発行した設定済みハンドルが必要。未設定ハンドルは no-op で ExecResult=NotValid。同一ハンドルの Push は同じ項目を更新し、Stop 済みでフェード中の項目に Push すると再び外部駆動へ戻る。同一ハンドルの時間型 Start は置換される。
	 * URO/LOD などで評価が止まっている間に同一ハンドルで連続呼び出しされた場合、未評価キューには最新値だけが保持される。
	 * 同時に保持できる倍率はノードあたり8件までで、時間型 Start と共有される。超過時は最古が破棄される。破棄・ノード再初期化・BP再コンパイルで失われ、その後の Push は新規作成、Stop は no-op になる。
	 * Alpha は 0..1 にクランプされる。Alpha=0 は「項目は存在するが効果なし」で、毎フレーム設定更新コストは掛かるため不要なら Stop する。
	 * C++ 専用（Blueprint 非公開）。Blueprint からは Duration < 0 の Start を使う。
	 * エッジケース: LimitAngle はベースが0（無制限）なら倍率に関わらず無制限のまま、ベースが0より大きいボーンは倍率0でも無制限へは反転しない。
	 * WorldDampingLocation/Rotation は実際の反映率が (1 - 値) のため意味が反転し、倍率1未満では揺れが増える。Radius の倍率0はワールドコリジョンのスイープと押し出しを実質無効にする。
	 * Set an externally driven multiplier for the physics settings. Intended for AnimNotifyState, Sequencer, and similar callers that push Alpha every frame or when the value changes.
	 * It does not disappear by internal time, so always release it with StopPhysicsSettingsMultiplier when it is no longer needed. Stop fades from the current Alpha to 0 over BlendOutTime, or removes immediately when BlendOutTime=0.
	 * Requires a set handle issued beforehand, for example by GenerateTransientHandle. An unset handle is a no-op with ExecResult=NotValid. Sets with the same handle update the same entry; setting a stopped/fading entry drives it again. A timed Start with the same handle replaces it.
	 * If evaluation is stopped by URO/LOD and the same handle is called repeatedly, only the latest pending value is kept.
	 * Multipliers are capped at 8 per node and share the cap with timed Starts. Beyond the cap, the oldest entry is evicted. Entries are lost on eviction, node re-initialization, or BP recompile; a later Set recreates the entry and Stop is a no-op until then.
	 * Alpha is clamped to 0..1. Alpha=0 means the entry exists but has no effect; it still costs a settings update every frame, so Stop it when unnecessary.
	 * C++ only (not exposed to Blueprint); Blueprint uses Start with Duration < 0.
	 * Edge cases: LimitAngle stays unlimited whatever the multiplier is when the base value is 0 (unlimited), and bones with a base above 0 never flip back to unlimited even with a multiplier of 0.
	 * WorldDampingLocation/Rotation have inverted semantics because the actual reflection factor is (1 - value): a multiplier below 1 increases the sway. A Radius multiplier of 0 effectively disables the world collision sweep and push-out.
	 * @param ExecResult ノード参照の解決結果 / Result of resolving the node reference.
	 * @param KawaiiPhysics 対象の KawaiiPhysics ノード参照 / Target KawaiiPhysics node reference.
	 * @param Handle 更新・停止対象のハンドル / Handle to update and stop.
	 * @param SettingsScale 物理設定へ適用する倍率（全項目1.0で変更なし） / Multipliers applied to the physics settings (all 1.0 means no change).
	 * @param Alpha 倍率の適用率 0..1 / Applied ratio of the multipliers, 0..1.
	 */
	static FKawaiiPhysicsReference PushPhysicsSettingsMultiplier(
		EKawaiiPhysicsAccessResult& ExecResult,
		const FKawaiiPhysicsReference& KawaiiPhysics,
		FKawaiiPhysicsTransientHandle Handle,
		FKawaiiPhysicsSettingsMultiplier SettingsScale,
		float Alpha = 1.0f);

	/**
	 * Component 内の対象 KawaiiPhysics ノードへ、外部駆動の物理設定倍率を共通ハンドルで設定する。AnimNotifyState や Sequencer など、呼び出し側が毎フレームまたは値が変化した時に Alpha を押し込む用途向け。
	 * 内部時間では消えないため、不要になったら必ず StopPhysicsSettingsMultiplierOnComponent で解放する。Stop は現在の Alpha から BlendOutTime で 0 へフェードし、BlendOutTime=0 なら即時除去する。
	 * GenerateTransientHandle などで事前発行した設定済みハンドルが必要。未設定ハンドルは no-op で 0 を返す。同一ハンドルの Push は同じ項目を更新し、Stop 済みでフェード中の項目に Push すると再び外部駆動へ戻る。同一ハンドルの時間型 Start は置換される。
	 * URO/LOD などで評価が止まっている間に同一ハンドルで連続呼び出しされた場合、未評価キューには最新値だけが保持される。
	 * 同時に保持できる倍率はノードあたり8件までで、時間型 Start と共有される。超過時は最古が破棄される。破棄・ノード再初期化・BP再コンパイルで失われ、その後の Push は新規作成、Stop は no-op になる。
	 * Alpha は 0..1 にクランプされる。Alpha=0 は「項目は存在するが効果なし」で、毎フレーム設定更新コストは掛かるため不要なら Stop する。
	 * C++ 専用（Blueprint 非公開）。Blueprint からは Duration < 0 の Start を使う。
	 * エッジケース: LimitAngle はベースが0（無制限）なら倍率に関わらず無制限のまま、ベースが0より大きいボーンは倍率0でも無制限へは反転しない。
	 * WorldDampingLocation/Rotation は実際の反映率が (1 - 値) のため意味が反転し、倍率1未満では揺れが増える。Radius の倍率0はワールドコリジョンのスイープと押し出しを実質無効にする。
	 * Component / Linked AnimInstance / PostProcess から KawaiiPhysics ノード参照を収集する（FilterTags が空なら全件）。
	 * AnimGraph の BlueprintThreadSafe 文脈から呼ぶ場合、対象は呼び出し元（呼び出しノード自身）の Component に限ります。
	 * それ以外のオブジェクトから収集する場合は、そのオブジェクトが評価中でない GameThread で呼び出してください。
	 * Set externally driven multipliers for the physics settings on target KawaiiPhysics nodes in a component under one shared handle. Intended for AnimNotifyState, Sequencer, and similar callers that push Alpha every frame or when the value changes.
	 * They do not disappear by internal time, so always release them with StopPhysicsSettingsMultiplierOnComponent when no longer needed. Stop fades from the current Alpha to 0 over BlendOutTime, or removes immediately when BlendOutTime=0.
	 * Requires a set handle issued beforehand, for example by GenerateTransientHandle. An unset handle is a no-op and returns 0. Sets with the same handle update the same entry; setting a stopped/fading entry drives it again. A timed Start with the same handle replaces it.
	 * If evaluation is stopped by URO/LOD and the same handle is called repeatedly, only the latest pending value is kept.
	 * Multipliers are capped at 8 per node and share the cap with timed Starts. Beyond the cap, the oldest entry is evicted. Entries are lost on eviction, node re-initialization, or BP recompile; a later Set recreates the entry and Stop is a no-op until then.
	 * Alpha is clamped to 0..1. Alpha=0 means the entry exists but has no effect; it still costs a settings update every frame, so Stop it when unnecessary.
	 * C++ only (not exposed to Blueprint); Blueprint uses Start with Duration < 0.
	 * Edge cases: LimitAngle stays unlimited whatever the multiplier is when the base value is 0 (unlimited), and bones with a base above 0 never flip back to unlimited even with a multiplier of 0.
	 * WorldDampingLocation/Rotation have inverted semantics because the actual reflection factor is (1 - value): a multiplier below 1 increases the sway. A Radius multiplier of 0 effectively disables the world collision sweep and push-out.
	 * When called from an AnimGraph BlueprintThreadSafe context, the target must be the caller's (the calling node's) own Component. Collecting from any other object must be done from the GameThread while that object is not being evaluated.
	 * @param MeshComp 対象の SkeletalMeshComponent / Target SkeletalMeshComponent.
	 * @param Handle 更新・停止対象のハンドル（全ノード共通） / Handle to update and stop (shared by every node).
	 * @param SettingsScale 物理設定へ適用する倍率（全項目1.0で変更なし） / Multipliers applied to the physics settings (all 1.0 means no change).
	 * @param Alpha 倍率の適用率 0..1 / Applied ratio of the multipliers, 0..1.
	 * @param FilterTags ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty matches all nodes.
	 * @param bFilterExactMatch タグを完全一致で比較するか / Whether tags must match exactly.
	 * @return 倍率をキューしたノード数 / Number of nodes where multipliers were queued.
	 */
	static int32 PushPhysicsSettingsMultiplierOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientHandle Handle,
		FKawaiiPhysicsSettingsMultiplier SettingsScale,
		float Alpha,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false);

	static int32 PushPhysicsSettingsMultiplierOnComponent(
		USkeletalMeshComponent* MeshComp,
		FKawaiiPhysicsTransientHandle Handle,
		const FKawaiiPhysicsSettingsMultiplier& SettingsScale,
		float Alpha,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch,
		int32 LeaseEvaluations,
		float LeaseExpireBlendOutTime);

	/**
	 * Id が設定済みかだけを返す。対象の風が現在も生存しているかの確認ではない（ノード再初期化・上限超過破棄後も true のまま。その場合 Stop は何もしない）。
	 * Returns only whether Id is set. This does not check whether the target wind is still alive (it remains true after node re-init or cap eviction; Stop then does nothing).
	 */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool IsTransientHandleSet(const FKawaiiPhysicsTransientHandle& Handle);

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
	static bool SetAlphaOnComponent(USkeletalMeshComponent* MeshComp, float Alpha,
	                                UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                bool bFilterExactMatch = false);

	/**
	 * 非推奨（v1.22.0）: SetAlphaOnComponent を使う / Deprecated (v1.22.0): use SetAlphaOnComponent.
	 * Set alpha (input) to all KawaiiPhysics nodes in the component (and linked/post-process instances).
	 * This is intended for AnimNotifyState usage.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, DeprecatedFunction, DeprecationMessage = "Use SetAlphaOnComponent"))
	static bool SetAlphaToComponent(USkeletalMeshComponent* MeshComp, float Alpha,
	                                UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                bool bFilterExactMatch = false);

	/** Get current alpha (input) from the first matched KawaiiPhysics node in the component. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool GetAlphaOnComponent(USkeletalMeshComponent* MeshComp, float& OutAlpha,
	                                  UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                  bool bFilterExactMatch = false);

	/**
	 * 非推奨（v1.22.0）: GetAlphaOnComponent を使う / Deprecated (v1.22.0): use GetAlphaOnComponent.
	 * Get current alpha (input) from the first matched KawaiiPhysics node in the component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta=(BlueprintThreadSafe, DeprecatedFunction, DeprecationMessage = "Use GetAlphaOnComponent"))
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
			TEXT("SetSharedCollisionSource"),
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
			TEXT("SetUseSharedCollision"),
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

	// --- Simple World Collision ---

	/**
	 * シンプルワールドコリジョン（Subsystemが収集したレベル上のsimple collision）を使用するかを設定
	 * Set whether to use Simple World Collision (level simple collision gathered by the subsystem)
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Simple World Collision", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetUseSimpleWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                          bool bUseSimpleWorldCollision)
	{
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetUseSimpleWorldCollision"),
			[bUseSimpleWorldCollision](FAnimNode_KawaiiPhysics& InKawaiiPhysics) {
				InKawaiiPhysics.bUseSimpleWorldCollision = bUseSimpleWorldCollision;
				InKawaiiPhysics.RequestSimpleWorldCollisionReinit();
			});
		return KawaiiPhysics;
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Simple World Collision", meta=(BlueprintThreadSafe))
	static bool GetUseSimpleWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(bool, bUseSimpleWorldCollision);
	}

	/**
	 * シンプルワールドコリジョンの収集間隔（秒）を設定。Desc差分検知で自動追従するため再初期化は不要
	 * Set the Simple World Collision gather interval (seconds). No reinitialization is needed; the Desc diff check picks it up automatically
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Simple World Collision", meta=(BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetSimpleWorldCollisionGatherInterval(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                                    float SimpleWorldCollisionGatherInterval)
	{
		KAWAIIPHYSICS_VALUE_SETTER(float, SimpleWorldCollisionGatherInterval);
	}

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Simple World Collision", meta=(BlueprintThreadSafe))
	static float GetSimpleWorldCollisionGatherInterval(const FKawaiiPhysicsReference& KawaiiPhysics)
	{
		KAWAIIPHYSICS_VALUE_GETTER(float, SimpleWorldCollisionGatherInterval);
	}

	static bool IsNodePropertyAccessible(const FProperty* Property);
	static bool IsNodePropertyAccessible(FName PropertyName);
	static bool DoesNodePropertyRequireModifyBonesReinit(FName PropertyName);
	static bool DoesNodePropertyRequireSharedCollisionReinit(FName PropertyName);
	static bool DoesNodePropertyRequireSimpleWorldCollisionReinit(FName PropertyName);
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
	 * ExternalForces 配列から指定型（派生型含む）の index を検索する。SetExternalForce*Property / StartProceduralWindGust 等の ExternalForceIndex 指定に使える。
	 * スレッド契約は他のランタイムアクセス API と同じ（ノード評価文脈の BlueprintThreadSafe、または非評価中の GameThread）。
	 * Finds indices in the ExternalForces array by the specified type, including derived types. Useful for ExternalForceIndex parameters in SetExternalForce*Property, StartProceduralWindGust, and similar APIs.
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
		if (DoesNodePropertyRequireSimpleWorldCollisionReinit(PropertyName))
		{
			Node.RequestSimpleWorldCollisionReinit();
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
		if (DoesNodePropertyRequireSimpleWorldCollisionReinit(PropertyName))
		{
			Node.RequestSimpleWorldCollisionReinit();
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
