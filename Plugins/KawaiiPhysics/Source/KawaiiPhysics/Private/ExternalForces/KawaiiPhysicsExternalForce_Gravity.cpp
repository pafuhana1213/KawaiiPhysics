// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"

#include "KawaiiPhysics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include <atomic>

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Gravity)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Gravity_Apply"), STAT_KawaiiPhysics_ExternalForce_Gravity_Apply,
                   STATGROUP_Anim);

// bUseCharacterGravityScale の向きの修正前の挙動に戻すための互換用 CVar。将来のバージョンで削除する
static TAutoConsoleVariable<bool> CVarKawaiiPhysicsGravityLegacyCharacterGravityScaleSign(
	TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), false,
	TEXT("1で、Gravity外力のbUseCharacterGravityScaleが上向きにかかっていた旧挙動に戻す。"
		"移行用の一時的な設定で、将来のバージョンで削除予定 / "
		"1 restores the legacy behavior where the Gravity external force with bUseCharacterGravityScale pushed upward. "
		"Temporary migration setting; deprecated and will be removed in a future version."));

void FKawaiiPhysics_ExternalForce_Gravity::Initialize(const FAnimationInitializeContext& Context)
{
	Super::Initialize(Context);

	// SkeletalMeshComponentがnullの場合のクラッシュを回避
	const USkeletalMeshComponent* SkelComp = Context.AnimInstanceProxy->GetSkelMeshComponent();
	OwnerCharacter = SkelComp ? Cast<ACharacter>(SkelComp->GetOwner()) : nullptr;
}

void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                    FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

	Force = bUseOverrideGravityDirection ? OverrideGravityDirection.GetSafeNormal() : FVector(0, 0, -1.0f);

	// Characterのカスタム重力方向に対応
	if (OwnerCharacter)
	{
#if !UE_VERSION_OLDER_THAN(5, 3, 0)
		if (bUseCharacterGravityDirection)
		{
			Force = OwnerCharacter->GetGravityDirection();
		}
#endif

		if (bUseCharacterGravityScale)
		{
			if (const UCharacterMovementComponent* CharacterMovementComponent = OwnerCharacter->GetCharacterMovement())
			{
				// GetGravityZは下向きが負の値。下向きの方向ベクトルに掛けるので符号を反転する
				// (Absにしないのは、負のGravityScaleなどで重力が反転している場合に向きを保つため)
				if (CVarKawaiiPhysicsGravityLegacyCharacterGravityScaleSign.GetValueOnAnyThread())
				{
					static std::atomic<bool> bWarnedLegacySign{false};
					if (!bWarnedLegacySign.exchange(true))
					{
						UE_LOG(LogKawaiiPhysics, Warning,
						       TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign is enabled: "
							       "the Gravity external force with bUseCharacterGravityScale pushes upward (legacy behavior). "
							       "This setting is deprecated and will be removed in a future version."));
					}
					Force *= CharacterMovementComponent->GetGravityZ();
				}
				else
				{
					Force *= -CharacterMovementComponent->GetGravityZ();
				}
			}
		}
	}

	Force *= RandomizedForceScale;
	Force = Node.ConvertSimulationSpaceVector(PoseContext, EKawaiiPhysicsSimulationSpace::WorldSpace,
	                                          Node.SimulationSpace, Force);
}

void FKawaiiPhysics_ExternalForce_Gravity::ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone,
                                                           FAnimNode_KawaiiPhysics& Node,
                                                           FComponentSpacePoseContext& PoseContext,
                                                           FVector& InOutVelocity)
{
	if (!CanApply(Bone))
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Gravity_Apply);

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot, 1.0f);
	}

	InOutVelocity += Force * ForceRate * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(GetBoneForceKey(Bone), Force * ForceRate);
#endif
}

void FKawaiiPhysics_ExternalForce_Gravity::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 FComponentSpacePoseContext& PoseContext,
                                                 const FTransform& BoneTM)
{
}
