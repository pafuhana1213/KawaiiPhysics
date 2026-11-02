// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSyncBone.h"
#include "AnimNode_KawaiiPhysics.h"

#if WITH_EDITOR
#include "Engine/Engine.h" 
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneManagement.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsSyncBone)

void FKawaiiPhysicsSyncTarget::UpdateScaleByLengthRate(const FRichCurve* ScaleCurveByBoneLengthRate)
{
	if (!ScaleCurveByBoneLengthRate)
	{
		return;
	}

	ScaleByLengthRateCurve = ScaleCurveByBoneLengthRate->Eval(LengthRateFromSyncTargetRoot);
}

void FKawaiiPhysicsSyncTarget::Apply(TArray<FKawaiiPhysicsModifyBone>& ModifyBones, const FVector& Translation)
{
	if (ModifyBoneIndex < 0 || !ModifyBones.IsValidIndex(ModifyBoneIndex))
	{
		return;
	}

	FKawaiiPhysicsModifyBone& Bone = ModifyBones[ModifyBoneIndex];
	if (Bone.bSkipSimulate)
	{
		return;
	}

	// length rate 由来のスケールを並進に適用
	const FVector ScaledTranslation = Translation * ScaleByLengthRateCurve;

#if WITH_EDITORONLY_DATA
	TranslationBySyncBone = ScaledTranslation;
#endif

	if (Bone.ParentIndex >= 0 && ModifyBones.IsValidIndex(Bone.ParentIndex))
	{
		const FKawaiiPhysicsModifyBone& ParentBone = ModifyBones[Bone.ParentIndex];
		const FVector NewPoseLocation = Bone.PoseLocation + ScaledTranslation;
		if (ParentBone.bInterBoneDummy)
		{
			// 親が分割 dummy だと位置が古く（後段で再計算）、それ基準で長さ拘束すると伸縮バグが出る。
			// 対策: dummy を飛ばし祖父基準で拘束する。dummy は祖父..child を InterBoneAlpha で内分するので距離 BoneLength/(1-alpha) を使う。
			const float OneMinusAlpha = 1.0f - ParentBone.InterBoneAlpha;
			const int32 GrandParentIndex = ParentBone.InterBoneRealParentIndex;
			if (OneMinusAlpha > KINDA_SMALL_NUMBER && ModifyBones.IsValidIndex(GrandParentIndex))
			{
				const FKawaiiPhysicsModifyBone& GrandParent = ModifyBones[GrandParentIndex];
				const float TargetLength = Bone.BoneLength / OneMinusAlpha;
				FVector Dir = (NewPoseLocation - GrandParent.PoseLocation).GetSafeNormal();
				if (Dir.IsNearlyZero())
				{
					// 新位置が祖父と一致して方向が消えた場合は既存方向にフォールバック（それも退化なら長さ拘束をスキップ）
					Dir = (Bone.PoseLocation - GrandParent.PoseLocation).GetSafeNormal();
				}
				Bone.PoseLocation = Dir.IsNearlyZero()
					                    ? NewPoseLocation
					                    : Dir * TargetLength + GrandParent.PoseLocation;
			}
			else
			{
				// 退化(alpha≈1)・祖父無効時は並進のみ（長さは後段の再補間に委ねる）
				Bone.PoseLocation = NewPoseLocation;
			}
		}
		else
		{
			// 親に対するボーン長を維持する
			FVector Dir = (NewPoseLocation - ParentBone.PoseLocation).GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				// 新位置が親と一致して方向が消えた場合は既存方向にフォールバック（それも退化なら長さ拘束をスキップ）
				Dir = (Bone.PoseLocation - ParentBone.PoseLocation).GetSafeNormal();
			}
			Bone.PoseLocation = Dir.IsNearlyZero()
				                    ? NewPoseLocation
				                    : Dir * Bone.BoneLength + ParentBone.PoseLocation;
		}
	}
	else
	{
		Bone.PoseLocation += ScaledTranslation;
	}
}

#if WITH_EDITOR
void FKawaiiPhysicsSyncTarget::DebugDraw(FPrimitiveDrawInterface* PDI, const FAnimNode_KawaiiPhysics* Node) const
{
	auto DrawForceArrow = [&](const FVector& Force, const FVector& Location)
	{
		const FRotator Rotation = FRotationMatrix::MakeFromX(Force.GetSafeNormal()).Rotator();
		const FMatrix TransformMatrix = FRotationMatrix(Rotation) * FTranslationMatrix(Location);
		DrawDirectionalArrow(PDI, TransformMatrix, FLinearColor::Green, Force.Length(), 2.0f, SDPG_Foreground);
	};

	if (ModifyBoneIndex >= 0 && Node->ModifyBones.IsValidIndex(ModifyBoneIndex))
	{
		// ターゲットボーンの位置
		FVector TargetBoneLocation = Node->ModifyBones[ModifyBoneIndex].OutputLocation;
		if (Node->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			const FTransform& BaseBoneSpace2ComponentSpace = Node->GetBaseBoneSpace2ComponentSpace();
			TargetBoneLocation = BaseBoneSpace2ComponentSpace.TransformPosition(TargetBoneLocation);
		}
		DrawSphere(PDI, TargetBoneLocation, FRotator::ZeroRotator,
		           FVector(1.0f), 12, 6,
		           GEngine->ConstraintLimitMaterialY->GetRenderProxy(), SDPG_World);

		// SyncBone による力
		DrawForceArrow(TranslationBySyncBone, TargetBoneLocation);
	}
}
#endif
