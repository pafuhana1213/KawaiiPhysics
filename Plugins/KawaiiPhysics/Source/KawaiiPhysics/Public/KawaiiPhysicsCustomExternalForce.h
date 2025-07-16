// Copyright 2019-2025 pafuhana1213. All Rights Reserved.

#pragma once
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsCustomExternalForce.generated.h"


UCLASS(Abstract, Blueprintable, EditInlineNew, CollapseCategories)
class KAWAIIPHYSICS_API UKawaiiPhysics_CustomExternalForce : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|CustomExternalForce")
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|CustomExternalForce")
	bool bDrawDebug = false;

public:
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

	UFUNCTION(BlueprintCallable, Category="KawaiiPhysics|CustomExternalForce")
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
