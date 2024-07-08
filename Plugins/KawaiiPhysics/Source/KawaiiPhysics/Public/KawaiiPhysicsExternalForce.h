#pragma once
#include "AnimNode_KawaiiPhysics.h"

#include "KawaiiPhysicsExternalForce.generated.h"


USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1))
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1))
	bool bDrawDebug = false;

public:
	virtual ~FKawaiiPhysics_ExternalForce() = default;

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp)
	{
	};

	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext)
	{
	};


	virtual bool IsDebugEnabled()
	{
		if (const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("a.AnimNode.KawaiiPhysics.Debug")))
		{
			return CVar->GetBool() && bDrawDebug;
		}
		return false;
	}

#if WITH_EDITOR
	virtual void AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
	                                      const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI)
	{
	}
#endif
};

USTRUCT(BlueprintType, DisplayName = "Gravity")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Gravity : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float GravityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseCharacterGravity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (editcondition = "bUseOverrideGravityDirection"))
	FVector OverrideGravityDirection = FVector::Zero();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (InlineEditConditionToggle))
	bool bUseOverrideGravityDirection = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly)
	float DebugArrowLength = 5.0f;
	UPROPERTY(EditDefaultsOnly)
	float DebugArrowSize = 1.0f;
	UPROPERTY(EditDefaultsOnly)
	FVector DebugArrowOffset = FVector::Zero();
#endif

private:
	FVector Gravity;

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext) override;

#if WITH_EDITOR
	virtual void AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
	                                      const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI) override;
#endif
};
