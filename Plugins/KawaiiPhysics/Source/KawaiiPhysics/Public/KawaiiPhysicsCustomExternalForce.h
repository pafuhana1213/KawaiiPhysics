// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsCustomExternalForce.generated.h"


UCLASS(Abstract, Blueprintable, EditInlineNew, CollapseCategories)
class KAWAIIPHYSICS_API UKawaiiPhysics_CustomExternalForce : public UObject
{
	GENERATED_BODY()

public:
	/** CustomExternalForceを有効にするフラグ / Whether the custom external force is enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="Kawaii Physics|CustomExternalForce")
	bool bIsEnabled = true;

	/** CustomExternalForceのデバッグ表示を行うフラグ / Whether to draw debug information for the custom external force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="Kawaii Physics|CustomExternalForce")
	bool bDrawDebug = false;

public:
	// 重要 / IMPORTANT (Thread-safety):
	// PreApply / Apply は EvaluateSkeletalControl_AnyThread から呼ばれ、アニメーション・ワーカースレッドで動きうる。
	// 実装（特に BP）は GameThread 専用 API（アクター/コンポーネントの破壊・スポーン、レイキャスト、UObject ライフサイクル等）に触れないこと。必要な値は事前にキャッシュし、ここでは読み取りのみ。
	// PreApply / Apply may run on the animation worker thread (called from EvaluateSkeletalControl_AnyThread).
	// Implementations (especially Blueprint) must NOT touch game-thread-only APIs (spawn/destroy actors/components, raycasts, UObject lifecycle); cache needed values beforehand and only read them here.
	UFUNCTION(BlueprintNativeEvent)
	void PreApply(UPARAM(ref) FAnimNode_KawaiiPhysics& Node,
	              const USkeletalMeshComponent* SkelComp);

	virtual void PreApply_Implementation(
		UPARAM(ref) FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp)PURE_VIRTUAL(,);

	UFUNCTION(BlueprintNativeEvent)
	void Apply(UPARAM(ref) FAnimNode_KawaiiPhysics& Node, int32 ModifyBoneIndex,
	           const USkeletalMeshComponent* SkelComp, const FTransform& BoneTransform);

	virtual void Apply_Implementation(
		UPARAM(ref) FAnimNode_KawaiiPhysics& Node, int32 ModifyBoneIndex, const USkeletalMeshComponent* SkelComp,
		const FTransform& BoneTransform)
	{
	}

	UFUNCTION(BlueprintCallable, Category="Kawaii Physics|CustomExternalForce")
	virtual bool IsDebugEnabled()
	{
#if ENABLE_ANIM_DEBUG
		if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
		{
			return bDrawDebug;
		}
#endif

		return false;
	}
};
