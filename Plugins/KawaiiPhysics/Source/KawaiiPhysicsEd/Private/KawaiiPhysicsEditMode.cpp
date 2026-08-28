// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEditMode.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "IPersonaPreviewScene.h"
#include "KawaiiPhysics.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "ScopedTransaction.h"
#include "SceneManagement.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/TaperedCapsuleElem.h"

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "SceneView.h"
#endif

#define LOCTEXT_NAMESPACE "KawaiiPhysicsEditMode"
DEFINE_LOG_CATEGORY(LogKawaiiPhysics);

struct HKawaiiPhysicsHitProxy : HHitProxy
{
	DECLARE_HIT_PROXY()

	HKawaiiPhysicsHitProxy(ECollisionLimitType InType, int32 InIndex,
	                       ECollisionSourceType InSourceType = ECollisionSourceType::AnimNode)
		: HHitProxy(HPP_Wireframe)
		  , CollisionType(InType)
		  , CollisionIndex(InIndex)
		  , SourceType(InSourceType)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}

	ECollisionLimitType CollisionType;
	int32 CollisionIndex;
	ECollisionSourceType SourceType = ECollisionSourceType::AnimNode;
};

IMPLEMENT_HIT_PROXY(HKawaiiPhysicsHitProxy, HHitProxy);


FKawaiiPhysicsEditMode::FKawaiiPhysicsEditMode()
	: RuntimeNode(nullptr)
	  , GraphNode(nullptr)
	  , SelectCollisionSourceType(ECollisionSourceType::AnimNode)
	  , CurWidgetMode(UE_WIDGET::EWidgetMode::WM_Translate)
{
}

void FKawaiiPhysicsEditMode::EnterMode(UAnimGraphNode_Base* InEditorNode, FAnimNode_Base* InRuntimeNode)
{
	RuntimeNode = static_cast<FAnimNode_KawaiiPhysics*>(InRuntimeNode);
	GraphNode = CastChecked<UAnimGraphNode_KawaiiPhysics>(InEditorNode);


	// for Sync DetailPanel
	GraphNode->Node.SphericalLimitsData = RuntimeNode->SphericalLimitsData;
	GraphNode->Node.CapsuleLimitsData = RuntimeNode->CapsuleLimitsData;
	GraphNode->Node.TaperedCapsuleLimitsData = RuntimeNode->TaperedCapsuleLimitsData;
	GraphNode->Node.BoxLimitsData = RuntimeNode->BoxLimitsData;
	GraphNode->Node.PlanarLimitsData = RuntimeNode->PlanarLimitsData;
	GraphNode->Node.BoneConstraintsData = RuntimeNode->BoneConstraintsData;
	GraphNode->Node.MergedBoneConstraints = RuntimeNode->MergedBoneConstraints;

	// SyncBone
	GraphNode->Node.SyncBones = RuntimeNode->SyncBones;

	NodePropertyDelegateHandle = GraphNode->OnNodePropertyChanged().AddSP(
		this, &FKawaiiPhysicsEditMode::OnExternalNodePropertyChange);
	if (RuntimeNode->LimitsDataAsset)
	{
		LimitsDataAssetPropertyDelegateHandle =
			RuntimeNode->LimitsDataAsset->OnLimitsChanged.AddRaw(
				this, &FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange);
		BoundLimitsDataAsset = RuntimeNode->LimitsDataAsset;
	}

	UMaterialInterface* BaseElemSelectedMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EditorMaterials/PhAT_UnselectedMaterial.PhAT_UnselectedMaterial"), nullptr,
		LOAD_None, nullptr);
	PhysicsAssetBodyMaterial = UMaterialInstanceDynamic::Create(
		BaseElemSelectedMaterial, GetTransientPackage());
	PhysicsAssetBodyMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.2f);

	FAnimNodeEditMode::EnterMode(InEditorNode, InRuntimeNode);
}

void FKawaiiPhysicsEditMode::ExitMode()
{
	GraphNode->OnNodePropertyChanged().Remove(NodePropertyDelegateHandle);
	// bind時のアセット基準で外す（実行中にLimitsDataAssetが差し替わっていても確実にRemoveできる）
	if (UKawaiiPhysicsLimitsDataAsset* BoundAsset = BoundLimitsDataAsset.Get())
	{
		BoundAsset->OnLimitsChanged.Remove(LimitsDataAssetPropertyDelegateHandle);
	}
	BoundLimitsDataAsset = nullptr;
	LimitsDataAssetPropertyDelegateHandle.Reset();

	GraphNode = nullptr;
	RuntimeNode = nullptr;

	FAnimNodeEditMode::ExitMode();
}

void FKawaiiPhysicsEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const USkeletalMeshComponent* SkelMeshComp = GetAnimPreviewScene().GetPreviewMeshComponent();

	if (!RuntimeNode || !GraphNode)
	{
		FAnimNodeEditMode::Render(View, Viewport, PDI);
		return;
	}

	if (SkelMeshComp && SkelMeshComp->GetSkeletalMeshAsset() && SkelMeshComp->GetSkeletalMeshAsset()->GetSkeleton() &&
		FAnimWeight::IsRelevant(RuntimeNode->GetAlpha()) && RuntimeNode->IsRecentlyEvaluated())
	{
		RenderModifyBones(PDI);
		RenderLimitAngle(PDI);
		RenderSyncBone(PDI);
		RenderSphericalLimits(PDI);
		RenderCapsuleLimit(PDI);
		RenderTaperedCapsuleLimit(PDI);
		RenderBoxLimit(PDI);
		RenderPlanarLimit(PDI);
		RenderBoneConstraint(PDI);
		RenderExternalForces(PDI);

		PDI->SetHitProxy(nullptr);

		if (IsValidSelectCollision())
		{
			if (const FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime())
			{
				FTransform BoneTransform = FTransform::Identity;
				if (Collision->DrivingBone.BoneIndex >= 0 && RuntimeNode->ForwardedPose.GetPose().GetNumBones() > 0)
				{
					BoneTransform = RuntimeNode->ForwardedPose.GetComponentSpaceTransform(
						Collision->DrivingBone.GetCompactPoseIndex(
							RuntimeNode->ForwardedPose.GetPose().GetBoneContainer()));
				}

				FVector CollisionLocation = Collision->Location;
				FQuat CollisionRotation = Collision->Rotation;
				if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
				{
					const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
					CollisionLocation = BaseBoneSpace2ComponentSpace.TransformPosition(CollisionLocation);
					CollisionRotation = BaseBoneSpace2ComponentSpace.TransformRotation(CollisionRotation);
				}

				PDI->DrawPoint(BoneTransform.GetLocation(), FLinearColor::White, 10.0f, SDPG_Foreground);
				DrawDashedLine(PDI, CollisionLocation, BoneTransform.GetLocation(),
				               FLinearColor::White, 1, SDPG_Foreground);
				DrawCoordinateSystem(PDI, BoneTransform.GetLocation(), CollisionRotation.Rotator(), 20,
				                     SDPG_World + 1);
			}
		}
	}

	FAnimNodeEditMode::Render(View, Viewport, PDI);
}

void FKawaiiPhysicsEditMode::RenderModifyBones(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawBone)
	{
		for (auto& Bone : RuntimeNode->ModifyBones)
		{
			FVector BoneLocation = Bone.Location;
			if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
			{
				const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
				BoneLocation = BaseBoneSpace2ComponentSpace.TransformPosition(BoneLocation);
			}

			PDI->DrawPoint(BoneLocation, FLinearColor::White, 5.0f, SDPG_Foreground);

			if (Bone.PhysicsSettings.Radius > 0)
			{
				auto Color = Bone.bBridgeDummy
					             ? FColor::Green
					             : (Bone.bInterBoneDummy ? FColor::Cyan : (Bone.bDummy ? FColor::Red : FColor::Yellow));
				DrawWireSphere(PDI, BoneLocation, Color, Bone.PhysicsSettings.Radius, 16, SDPG_Foreground);
			}

			for (const int32 ChildIndex : Bone.ChildIndices)
			{
				FVector ChildBoneLocation = RuntimeNode->ModifyBones[ChildIndex].Location;
				if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
				{
					const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
					ChildBoneLocation = BaseBoneSpace2ComponentSpace.TransformPosition(ChildBoneLocation);
				}

				DrawDashedLine(PDI, BoneLocation, ChildBoneLocation,
				               FLinearColor::White, 1, SDPG_Foreground);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderLimitAngle(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawLimitAngle)
	{
		for (auto& Bone : RuntimeNode->ModifyBones)
		{
			if (!Bone.bSkipSimulate && Bone.PhysicsSettings.LimitAngle > 0.0f && Bone.HasParent())
			{
				FTransform BoneTransform = FTransform(Bone.PrevRotation, Bone.PrevLocation);
				FTransform ParentBoneTransform = FTransform(RuntimeNode->ModifyBones[Bone.ParentIndex].PrevRotation,
				                                            RuntimeNode->ModifyBones[Bone.ParentIndex].PrevLocation);

				if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
				{
					const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
					BoneTransform = BoneTransform * BaseBoneSpace2ComponentSpace;
					ParentBoneTransform = ParentBoneTransform * BaseBoneSpace2ComponentSpace;
				}

				const float Angle = FMath::DegreesToRadians(Bone.PhysicsSettings.LimitAngle);
				DrawCone(PDI, FScaleMatrix(5.0f) * FTransform(
					         (BoneTransform.GetLocation() - ParentBoneTransform.GetLocation()).Rotation(),
					         ParentBoneTransform.GetLocation()).ToMatrixNoScale(),
				         Angle,
				         Angle, 24, true, FLinearColor::White,
				         GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderSyncBone(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawSyncBone)
	{
		return;
	}

	auto ApplyDirectionFilterAndAlpha = [&](double& Delta, const float& Alpha,
	                                        const ESyncBoneDirection Direction)
	{
		if (Direction != ESyncBoneDirection::None &&
			(Direction == ESyncBoneDirection::Both ||
				(Direction == ESyncBoneDirection::Positive && Delta > 0) ||
				(Direction == ESyncBoneDirection::Negative && Delta < 0)))
		{
			Delta = FMath::Lerp(0.0f, Delta, Alpha);
		}
		else
		{
			Delta = 0.0f;
		}
	};

	auto DrawForceArrow = [&](const FVector& Force, const FVector& Location)
	{
		const FRotator Rotation = FRotationMatrix::MakeFromX(Force.GetSafeNormal()).Rotator();
		const FMatrix TransformMatrix = FRotationMatrix(Rotation) * FTranslationMatrix(Location);
		DrawDirectionalArrow(PDI, TransformMatrix, FLinearColor::Green, Force.Length(), 2.0f, SDPG_Foreground);
	};

	for (auto& SyncBone : RuntimeNode->SyncBones)
	{
		// InitialPoseLocation
		DrawBox(PDI, FTranslationMatrix(SyncBone.InitialPoseLocation), FVector(1.0f),
		        GEngine->ConstraintLimitMaterialY->GetRenderProxy(), SDPG_World);

		// Current SyncBone Location
		DrawBox(PDI, FTranslationMatrix(SyncBone.InitialPoseLocation + SyncBone.DeltaDistance), FVector(1.0f),
		        GEngine->ConstraintLimitMaterialY->GetRenderProxy(), SDPG_World);

		// DeltaMovement
		DrawDashedLine(PDI, SyncBone.InitialPoseLocation,
		               SyncBone.InitialPoseLocation + SyncBone.DeltaDistance,
		               FLinearColor::Green, 0.1f, SDPG_World);

		// Distance attenuation radii
		if (SyncBone.bEnableDistanceAttenuation)
		{
			const FVector Center = SyncBone.InitialPoseLocation + SyncBone.DeltaDistance;
			// current location in component space
			if (SyncBone.AttenuationInnerRadius > 0.0f)
			{
				DrawWireSphere(PDI, Center, FLinearColor(0.0f, 0.8f, 1.0f), SyncBone.AttenuationInnerRadius, 24,
				               SDPG_World);
			}
			if (SyncBone.AttenuationOuterRadius > 0.0f)
			{
				DrawWireSphere(PDI, Center, FLinearColor(0.0f, 0.3f, 0.0f), SyncBone.AttenuationOuterRadius, 24,
				               SDPG_World);
			}
		}

		// Force By SyncForce
		FVector Force = SyncBone.DeltaDistance;
		ApplyDirectionFilterAndAlpha(Force.X, SyncBone.GlobalScale.X, SyncBone.ApplyDirectionX);
		ApplyDirectionFilterAndAlpha(Force.Y, SyncBone.GlobalScale.Y, SyncBone.ApplyDirectionY);
		ApplyDirectionFilterAndAlpha(Force.Z, SyncBone.GlobalScale.Z, SyncBone.ApplyDirectionZ);
		DrawForceArrow(Force, SyncBone.InitialPoseLocation);

		// Target Bone
		for (auto& TargetRoot : SyncBone.TargetRoots)
		{
			TargetRoot.DebugDraw(PDI, RuntimeNode);
			for (auto& ChildTarget : TargetRoot.ChildTargets)
			{
				ChildTarget.DebugDraw(PDI, RuntimeNode);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderSphericalLimits(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawSphereLimit)
	{
		return;
	}

	auto DrawSphereLimit = [&](const auto& Sphere, int32 Index, const FMaterialRenderProxy* MaterialProxy, bool bUseHit)
	{
		if (Sphere.bEnable && Sphere.Radius > 0)
		{
			FVector Location = Sphere.Location;
			FQuat Rotation = Sphere.Rotation;
			if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
			{
				const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
				Location = BaseBoneSpace2ComponentSpace.TransformPosition(Location);
				Rotation = BaseBoneSpace2ComponentSpace.TransformRotation(Rotation);
			}

			PDI->SetHitProxy(bUseHit
				                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Spherical, Index, Sphere.SourceType)
				                 : nullptr);
			DrawSphere(PDI, Location, FRotator::ZeroRotator, FVector(Sphere.Radius), 24, 6, MaterialProxy,
			           SDPG_World);
			DrawWireSphere(PDI, Location, FLinearColor::Black, Sphere.Radius, 24, SDPG_World);
			DrawCoordinateSystem(PDI, Location, Rotation.Rotator(), Sphere.Radius, SDPG_World + 1);
			PDI->SetHitProxy(nullptr);
		}
	};

	for (int32 i = 0; i < RuntimeNode->SphericalLimits.Num(); i++)
	{
		DrawSphereLimit(RuntimeNode->SphericalLimits[i], i,
		                GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), true);
	}

	for (int32 i = 0; i < RuntimeNode->SphericalLimitsData.Num(); i++)
	{
		if (RuntimeNode->SphericalLimitsData[i].SourceType == ECollisionSourceType::DataAsset)
		{
			DrawSphereLimit(RuntimeNode->SphericalLimitsData[i], i,
			                GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), true);
		}
		else if (RuntimeNode->SphericalLimitsData[i].SourceType == ECollisionSourceType::PhysicsAsset)
		{
			if (PhysicsAssetBodyMaterial->IsValidLowLevel())
			{
				DrawSphereLimit(RuntimeNode->SphericalLimitsData[i], i, PhysicsAssetBodyMaterial->GetRenderProxy(),
				                false);
			}
		}
		else if (RuntimeNode->SphericalLimitsData[i].SourceType == ECollisionSourceType::Mirror)
		{
			DrawSphereLimit(RuntimeNode->SphericalLimitsData[i], i,
			                GEngine->ConstraintLimitMaterialX->GetRenderProxy(), false);
		}
	}
}

void FKawaiiPhysicsEditMode::RenderCapsuleLimit(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawCapsuleLimit)
	{
		return;
	}

	auto DrawCapsule = [&](const auto& Capsule, int32 Index, const FMaterialRenderProxy* MaterialProxy,
	                       bool bUseHit)
	{
		if (Capsule.bEnable && Capsule.Radius > 0 && Capsule.Length > 0)
		{
			FVector Location = Capsule.Location;
			FQuat Rotation = Capsule.Rotation;
			if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
			{
				const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
				Location = BaseBoneSpace2ComponentSpace.TransformPosition(Location);
				Rotation = BaseBoneSpace2ComponentSpace.TransformRotation(Rotation);
			}

			FVector XAxis = Rotation.GetAxisX();
			FVector YAxis = Rotation.GetAxisY();
			FVector ZAxis = Rotation.GetAxisZ();

			PDI->SetHitProxy(bUseHit
				                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Capsule, Index, Capsule.SourceType)
				                 : nullptr);

			DrawCylinder(PDI, Location, XAxis, YAxis, ZAxis, Capsule.Radius, 0.5f * Capsule.Length, 25,
			             MaterialProxy, SDPG_World);
			DrawSphere(PDI, Location + ZAxis * Capsule.Length * 0.5f, Rotation.Rotator(),
			           FVector(Capsule.Radius), 24, 6, MaterialProxy, SDPG_World);
			DrawSphere(PDI, Location - ZAxis * Capsule.Length * 0.5f, Rotation.Rotator(),
			           FVector(Capsule.Radius), 24, 6, MaterialProxy, SDPG_World);
			DrawWireCapsule(PDI, Location, XAxis, YAxis, ZAxis, FLinearColor::Black, Capsule.Radius,
			                0.5f * Capsule.Length + Capsule.Radius, 25, SDPG_World);
			DrawCoordinateSystem(PDI, Location, Rotation.Rotator(), Capsule.Radius, SDPG_World + 1);
			PDI->SetHitProxy(nullptr);
		}
	};

	for (int32 i = 0; i < RuntimeNode->CapsuleLimits.Num(); i++)
	{
		DrawCapsule(RuntimeNode->CapsuleLimits[i], i, GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(),
		            true);
	}

	for (int32 i = 0; i < RuntimeNode->CapsuleLimitsData.Num(); i++)
	{
		if (RuntimeNode->CapsuleLimitsData[i].SourceType == ECollisionSourceType::DataAsset)
		{
			DrawCapsule(RuntimeNode->CapsuleLimitsData[i], i,
			            GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), true);
		}
		else if (RuntimeNode->CapsuleLimitsData[i].SourceType == ECollisionSourceType::PhysicsAsset)
		{
			if (PhysicsAssetBodyMaterial->IsValidLowLevel())
			{
				DrawCapsule(RuntimeNode->CapsuleLimitsData[i], i, PhysicsAssetBodyMaterial->GetRenderProxy(), false);
			}
		}
		else if (RuntimeNode->CapsuleLimitsData[i].SourceType == ECollisionSourceType::Mirror)
		{
			DrawCapsule(RuntimeNode->CapsuleLimitsData[i], i,
			            GEngine->ConstraintLimitMaterialX->GetRenderProxy(), false);
		}
	}
}

void FKawaiiPhysicsEditMode::RenderTaperedCapsuleLimit(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawTaperedCapsuleLimit)
	{
		return;
	}

	auto DrawTaperedCapsule = [&](const auto& TaperedCapsule, int32 Index, const FMaterialRenderProxy* MaterialProxy,
	                              bool bUseHit)
	{
		if (TaperedCapsule.bEnable && (TaperedCapsule.Radius0 > 0 || TaperedCapsule.Radius1 > 0))
		{
			FVector Location = TaperedCapsule.Location;
			FQuat Rotation = TaperedCapsule.Rotation;
			if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
			{
				const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
				Location = BaseBoneSpace2ComponentSpace.TransformPosition(Location);
				Rotation = BaseBoneSpace2ComponentSpace.TransformRotation(Rotation);
			}

			PDI->SetHitProxy(bUseHit
				                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::TaperedCapsule, Index,
				                                              TaperedCapsule.SourceType)
				                 : nullptr);

			const FKTaperedCapsuleElem TaperedCapsuleElem(
				TaperedCapsule.Radius0, TaperedCapsule.Radius1, TaperedCapsule.Length);
			const FTransform ElemTM(Rotation, Location);
			TaperedCapsuleElem.DrawElemSolid(PDI, ElemTM, 1.0f, MaterialProxy);
			TaperedCapsuleElem.DrawElemWire(PDI, ElemTM, 1.0f, FColor::Black);
			DrawCoordinateSystem(PDI, Location, Rotation.Rotator(),
			                     FMath::Max(TaperedCapsule.Radius0, TaperedCapsule.Radius1), SDPG_World + 1);
			PDI->SetHitProxy(nullptr);
		}
	};

	for (int32 i = 0; i < RuntimeNode->TaperedCapsuleLimits.Num(); i++)
	{
		DrawTaperedCapsule(RuntimeNode->TaperedCapsuleLimits[i], i,
		                   GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), true);
	}

	for (int32 i = 0; i < RuntimeNode->TaperedCapsuleLimitsData.Num(); i++)
	{
		if (RuntimeNode->TaperedCapsuleLimitsData[i].SourceType == ECollisionSourceType::DataAsset)
		{
			DrawTaperedCapsule(RuntimeNode->TaperedCapsuleLimitsData[i], i,
			                   GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), true);
		}
		else if (RuntimeNode->TaperedCapsuleLimitsData[i].SourceType == ECollisionSourceType::PhysicsAsset)
		{
			if (PhysicsAssetBodyMaterial->IsValidLowLevel())
			{
				DrawTaperedCapsule(RuntimeNode->TaperedCapsuleLimitsData[i], i,
				                   PhysicsAssetBodyMaterial->GetRenderProxy(), false);
			}
		}
		else if (RuntimeNode->TaperedCapsuleLimitsData[i].SourceType == ECollisionSourceType::Mirror)
		{
			DrawTaperedCapsule(RuntimeNode->TaperedCapsuleLimitsData[i], i,
			                   GEngine->ConstraintLimitMaterialX->GetRenderProxy(), false);
		}
	}
}

void FKawaiiPhysicsEditMode::RenderBoxLimit(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawBoxLimit)
	{
		return;
	}

	auto DrawBoxLimit = [&](const auto& Box, int32 Index, const FMaterialRenderProxy* MaterialProxy,
	                        bool bUseHit = true)
	{
		if (Box.bEnable && Box.Extent.Size() > 0)
		{
			FTransform BoxTransform(Box.Rotation, Box.Location);
			if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
			{
				const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
				BoxTransform = BoxTransform * BaseBoneSpace2ComponentSpace;
			}

			PDI->SetHitProxy(bUseHit
				                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Box, Index, Box.SourceType)
				                 : nullptr);

			DrawBox(PDI, BoxTransform.ToMatrixWithScale(), Box.Extent, MaterialProxy, SDPG_World);
			DrawWireBox(PDI, BoxTransform.ToMatrixWithScale(), FBox(-Box.Extent, Box.Extent), FLinearColor::Black,
			            SDPG_World);
			DrawCoordinateSystem(PDI, BoxTransform.GetLocation(), BoxTransform.Rotator(), Box.Extent.Size(),
			                     SDPG_World + 1);
			PDI->SetHitProxy(nullptr);
		}
	};

	for (int32 i = 0; i < RuntimeNode->BoxLimits.Num(); i++)
	{
		DrawBoxLimit(RuntimeNode->BoxLimits[i], i,
		             GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy());
	}

	for (int32 i = 0; i < RuntimeNode->BoxLimitsData.Num(); i++)
	{
		if (RuntimeNode->BoxLimitsData[i].SourceType == ECollisionSourceType::DataAsset)
		{
			DrawBoxLimit(RuntimeNode->BoxLimitsData[i], i,
			             GEngine->ConstraintLimitMaterialZ->GetRenderProxy());
		}
		else if (RuntimeNode->BoxLimitsData[i].SourceType == ECollisionSourceType::PhysicsAsset)
		{
			if (PhysicsAssetBodyMaterial->IsValidLowLevel())
			{
				DrawBoxLimit(RuntimeNode->BoxLimitsData[i], i, PhysicsAssetBodyMaterial->GetRenderProxy(), false);
			}
		}
		else if (RuntimeNode->BoxLimitsData[i].SourceType == ECollisionSourceType::Mirror)
		{
			DrawBoxLimit(RuntimeNode->BoxLimitsData[i], i,
			             GEngine->ConstraintLimitMaterialX->GetRenderProxy(), false);
		}
	}
}

void FKawaiiPhysicsEditMode::RenderPlanarLimit(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawPlanarLimit)
	{
		auto DrawPlanarLimit = [&](const auto& Plane, int32 Index, const FMaterialRenderProxy* MaterialProxy,
		                           bool bUseHit = true)
		{
			FTransform PlaneTransform(Plane.Rotation, Plane.Location);
			if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
			{
				const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
				PlaneTransform = PlaneTransform * BaseBoneSpace2ComponentSpace;
			}
			PlaneTransform.NormalizeRotation();

			PDI->SetHitProxy(bUseHit
				                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Planar, Index, Plane.SourceType)
				                 : nullptr);

			DrawPlane10x10(PDI, PlaneTransform.ToMatrixWithScale(), 200.0f, FVector2D(0.0f, 0.0f),
			               FVector2D(1.0f, 1.0f), MaterialProxy, SDPG_World);
			DrawDirectionalArrow(PDI, FRotationMatrix(FRotator(90.0f, 0.0f, 0.0f)) * PlaneTransform.ToMatrixWithScale(),
			                     FLinearColor::Blue, 50.0f, 20.0f, SDPG_Foreground, 0.5f);
			PDI->SetHitProxy(nullptr);
		};

		for (int32 i = 0; i < RuntimeNode->PlanarLimits.Num(); i++)
		{
			DrawPlanarLimit(RuntimeNode->PlanarLimits[i], i,
			                GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy());
		}

		for (int32 i = 0; i < RuntimeNode->PlanarLimitsData.Num(); i++)
		{
			if (RuntimeNode->PlanarLimitsData[i].SourceType == ECollisionSourceType::DataAsset)
			{
				DrawPlanarLimit(RuntimeNode->PlanarLimitsData[i], i,
				                GEngine->ConstraintLimitMaterialZ->GetRenderProxy());
			}
			else if (RuntimeNode->PlanarLimitsData[i].SourceType == ECollisionSourceType::PhysicsAsset)
			{
				if (PhysicsAssetBodyMaterial->IsValidLowLevel())
				{
					DrawPlanarLimit(RuntimeNode->PlanarLimitsData[i], i,
					                PhysicsAssetBodyMaterial->GetRenderProxy(), false);
				}
			}
			else if (RuntimeNode->PlanarLimitsData[i].SourceType == ECollisionSourceType::Mirror)
			{
				DrawPlanarLimit(RuntimeNode->PlanarLimitsData[i], i,
				                GEngine->ConstraintLimitMaterialX->GetRenderProxy(), false);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderBoneConstraint(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawBoneConstraint)
	{
		for (const FModifyBoneConstraint& BoneConstraint : RuntimeNode->MergedBoneConstraints)
		{
			if (BoneConstraint.IsBoneReferenceValid() && !RuntimeNode->ModifyBones.IsEmpty() &&
				RuntimeNode->ModifyBones.IsValidIndex(BoneConstraint.ModifyBoneIndex1) &&
				RuntimeNode->ModifyBones.IsValidIndex(BoneConstraint.ModifyBoneIndex2))
			{
				FTransform BoneTransform1 = FTransform(
					RuntimeNode->ModifyBones[BoneConstraint.ModifyBoneIndex1].PrevRotation,
					RuntimeNode->ModifyBones[BoneConstraint.ModifyBoneIndex1].PrevLocation);
				FTransform BoneTransform2 = FTransform(
					RuntimeNode->ModifyBones[BoneConstraint.ModifyBoneIndex2].PrevRotation,
					RuntimeNode->ModifyBones[BoneConstraint.ModifyBoneIndex2].PrevLocation);

				if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
				{
					const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
					BoneTransform1 = BoneTransform1 * BaseBoneSpace2ComponentSpace;
					BoneTransform2 = BoneTransform2 * BaseBoneSpace2ComponentSpace;
				}

				// 1 -> 2
				FVector Dir = (BoneTransform2.GetLocation() - BoneTransform1.GetLocation()).GetSafeNormal();
				FRotator LookAt = FRotationMatrix::MakeFromX(Dir).Rotator();
				FTransform DrawArrowTransform = FTransform(LookAt, BoneTransform1.GetLocation(),
				                                           BoneTransform1.GetScale3D());
				const float Distance = (BoneTransform1.GetLocation() - BoneTransform2.GetLocation()).Size();
				DrawDirectionalArrow(PDI, DrawArrowTransform.ToMatrixNoScale(), FLinearColor::Red,
				                     Distance, 1, SDPG_Foreground);
				// 2 -> 1
				LookAt = FRotationMatrix::MakeFromX(-Dir).Rotator();
				DrawArrowTransform = FTransform(LookAt, BoneTransform2.GetLocation(), BoneTransform2.GetScale3D());
				DrawDirectionalArrow(PDI, DrawArrowTransform.ToMatrixNoScale(), FLinearColor::Red,
				                     Distance, 1, SDPG_Foreground);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderExternalForces(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawExternalForce)
	{
		for (auto& Force : RuntimeNode->ExternalForces)
		{
			if (!Force.IsValid())
			{
				continue;
			}
			FKawaiiPhysics_ExternalForce* ForcePtr = Force.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
			if (!ForcePtr)
			{
				continue;
			}
			for (const auto& Bone : RuntimeNode->ModifyBones)
			{
				ForcePtr->AnimDrawDebugForEditMode(Bone, *RuntimeNode, PDI);
			}
		}
	}
}

FVector FKawaiiPhysicsEditMode::GetWidgetLocation(ECollisionLimitType CollisionType, int32 Index) const
{
	if (!IsValidSelectCollision())
	{
		return GetAnimPreviewScene().GetPreviewMeshComponent()->GetComponentLocation();
	}

	if (const FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime())
	{
		return Collision->Location;
	}

	return GetAnimPreviewScene().GetPreviewMeshComponent()->GetComponentLocation();
}

FVector FKawaiiPhysicsEditMode::GetWidgetLocation() const
{
	return GetWidgetLocation(SelectCollisionType, SelectCollisionIndex);
}

bool FKawaiiPhysicsEditMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (!IsValidSelectCollision())
	{
		return false;
	}

	FQuat Rotation = FQuat::Identity;
	if (FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime())
	{
		Rotation = Collision->Rotation;
	}

	InMatrix = FTransform(Rotation).ToMatrixNoScale();
	return true;
}

UE_WIDGET::EWidgetMode FKawaiiPhysicsEditMode::GetWidgetMode() const
{
	if (GetSelectCollisionLimitRuntime())
	{
		CurWidgetMode = FindValidWidgetMode(CurWidgetMode);
		return CurWidgetMode;
	}

	return UE_WIDGET::EWidgetMode::WM_Translate;
}

UE_WIDGET::EWidgetMode FKawaiiPhysicsEditMode::FindValidWidgetMode(UE_WIDGET::EWidgetMode InWidgetMode) const
{
	if (InWidgetMode == UE_WIDGET::EWidgetMode::WM_None)
	{
		return UE_WIDGET::EWidgetMode::WM_Translate;
	}

	switch (InWidgetMode)
	{
	case UE_WIDGET::EWidgetMode::WM_Translate:
		return UE_WIDGET::EWidgetMode::WM_Rotate;
	case UE_WIDGET::EWidgetMode::WM_Rotate:
		return UE_WIDGET::EWidgetMode::WM_Scale;
	case UE_WIDGET::EWidgetMode::WM_Scale:
		return UE_WIDGET::EWidgetMode::WM_Translate;
	default: ;
	}

	return UE_WIDGET::EWidgetMode::WM_None;
}

bool FKawaiiPhysicsEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy,
                                         const FViewportClick& Click)
{
	bool bResult = FAnimNodeEditMode::HandleClick(InViewportClient, HitProxy, Click);

	if (HitProxy != nullptr && HitProxy->IsA(HKawaiiPhysicsHitProxy::StaticGetType()))
	{
		HKawaiiPhysicsHitProxy* KawaiiPhysicsHitProxy = static_cast<HKawaiiPhysicsHitProxy*>(HitProxy);
		SelectCollisionType = KawaiiPhysicsHitProxy->CollisionType;
		SelectCollisionIndex = KawaiiPhysicsHitProxy->CollisionIndex;
		SelectCollisionSourceType = KawaiiPhysicsHitProxy->SourceType;
		// 選択確定時のGuidを保持する（削除はこのGuidで対象を引き、クリック後に配列が変化しても誤削除しない）
		SelectedCollisionGuid = FGuid();
		if (const FCollisionLimitBase* Selected = GetSelectCollisionLimitRuntime())
		{
			SelectedCollisionGuid = Selected->Guid;
		}
		bResult = true;
	}
	else
	{
		SelectCollisionType = ECollisionLimitType::None;
		SelectCollisionIndex = -1;
		SelectedCollisionGuid = FGuid();
	}

	return bResult;
}

bool FKawaiiPhysicsEditMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey,
                                      EInputEvent InEvent)
{
	bool bHandled = false;

	if ((InEvent == IE_Pressed) && !IsManipulatingWidget())
	{
		if (InKey == EKeys::SpaceBar)
		{
			GetModeManager()->SetWidgetMode(GetWidgetMode());
			bHandled = true;
			InViewportClient->Invalidate();
		}
		else if (InKey == EKeys::Q)
		{
			const auto CoordSystem = GetModeManager()->GetCoordSystem();
			GetModeManager()->SetCoordSystem(CoordSystem == COORD_Local ? COORD_World : COORD_Local);
		}
		else if (InKey == EKeys::Delete &&
			(SelectCollisionSourceType == ECollisionSourceType::AnimNode ||
				SelectCollisionSourceType == ECollisionSourceType::DataAsset) && IsValidSelectCollision())
		{
			const bool bFromDataAsset = (SelectCollisionSourceType == ECollisionSourceType::DataAsset);
			UKawaiiPhysicsLimitsDataAsset* LimitsDataAsset = RuntimeNode->LimitsDataAsset;

			// DataAsset由来なのにアセットが無効なら何もしない（null deref回避）
			if (!bFromDataAsset || LimitsDataAsset)
			{
				// Undo可能にするためTransactionで包み、編集対象にModify()を呼ぶ
				const FScopedTransaction Transaction(
					LOCTEXT("DeleteKawaiiCollision", "Delete KawaiiPhysics Collision"));
				if (bFromDataAsset)
				{
					LimitsDataAsset->Modify();
				}
				else
				{
					GraphNode->Modify();
				}

				// 削除対象の解決: PreferredIndex(クリック時index)がまだ選択Guidの行を指せばそれを採用し（重複Guidでも
				// クリック行を正確に特定）、stale化していればGuidで引き直す。INDEX_NONE時はGuid一致のみ。
				auto ResolveIndex = [&](auto& Array, int32 PreferredIndex, const FGuid& Guid) -> int32
				{
					if (Array.IsValidIndex(PreferredIndex) &&
						(!Guid.IsValid() || Array[PreferredIndex].Guid == Guid))
					{
						return PreferredIndex;
					}
					if (Guid.IsValid())
					{
						return Array.IndexOfByPredicate(
							[&Guid](const FCollisionLimitBase& Limit) { return Limit.Guid == Guid; });
					}
					return INDEX_NONE;
				};

				// DataAsset由来: SelectCollisionIndexはマージ済みキャッシュ(PhysicsAsset混在)上の位置。runtime/graphは
				// その位置を起点に、アセット配列はマージindexを流用できないためGuidで対応エントリを引いて各1件削除する。
				auto RemoveDataAssetLimit = [&](auto& RuntimeMerged, auto& AssetArray, auto& GraphMerged)
				{
					const int32 RuntimeIndex = ResolveIndex(RuntimeMerged, SelectCollisionIndex, SelectedCollisionGuid);
					// 対象が見つからない/DataAsset由来でなければ何もしない（source不一致のstale選択を弾く）
					if (RuntimeIndex == INDEX_NONE ||
						RuntimeMerged[RuntimeIndex].SourceType != ECollisionSourceType::DataAsset)
					{
						return;
					}
					const FGuid TargetGuid = RuntimeMerged[RuntimeIndex].Guid;

					const int32 AssetIndex = ResolveIndex(AssetArray, INDEX_NONE, TargetGuid);
					if (AssetIndex != INDEX_NONE)
					{
						AssetArray.RemoveAt(AssetIndex);
						LimitsDataAsset->MarkPackageDirty();
					}
					RuntimeMerged.RemoveAt(RuntimeIndex);
					const int32 GraphIndex = ResolveIndex(GraphMerged, SelectCollisionIndex, TargetGuid);
					if (GraphIndex != INDEX_NONE)
					{
						GraphMerged.RemoveAt(GraphIndex);
					}
				};

				// AnimNode由来: SelectCollisionIndexはそのまま*Limits配列の位置。runtime/graphとも同じ解決で1件削除。
				auto RemoveAnimNodeLimit = [&](auto& RuntimeArray, auto& GraphArray)
				{
					const int32 RuntimeIndex = ResolveIndex(RuntimeArray, SelectCollisionIndex, SelectedCollisionGuid);
					if (RuntimeIndex == INDEX_NONE)
					{
						return;
					}
					const FGuid TargetGuid = RuntimeArray[RuntimeIndex].Guid;
					RuntimeArray.RemoveAt(RuntimeIndex);
					const int32 GraphIndex = ResolveIndex(GraphArray, SelectCollisionIndex, TargetGuid);
					if (GraphIndex != INDEX_NONE)
					{
						GraphArray.RemoveAt(GraphIndex);
					}
				};

				switch (SelectCollisionType)
				{
				case ECollisionLimitType::Spherical:
					if (bFromDataAsset)
					{
						RemoveDataAssetLimit(RuntimeNode->SphericalLimitsData, LimitsDataAsset->SphericalLimits,
						                     GraphNode->Node.SphericalLimitsData);
					}
					else
					{
						RemoveAnimNodeLimit(RuntimeNode->SphericalLimits, GraphNode->Node.SphericalLimits);
					}
					break;
				case ECollisionLimitType::Capsule:
					if (bFromDataAsset)
					{
						RemoveDataAssetLimit(RuntimeNode->CapsuleLimitsData, LimitsDataAsset->CapsuleLimits,
						                     GraphNode->Node.CapsuleLimitsData);
					}
					else
					{
						RemoveAnimNodeLimit(RuntimeNode->CapsuleLimits, GraphNode->Node.CapsuleLimits);
					}
					break;
				case ECollisionLimitType::TaperedCapsule:
					if (bFromDataAsset)
					{
						RemoveDataAssetLimit(RuntimeNode->TaperedCapsuleLimitsData,
						                     LimitsDataAsset->TaperedCapsuleLimits,
						                     GraphNode->Node.TaperedCapsuleLimitsData);
					}
					else
					{
						RemoveAnimNodeLimit(RuntimeNode->TaperedCapsuleLimits,
						                    GraphNode->Node.TaperedCapsuleLimits);
					}
					break;
				case ECollisionLimitType::Box:
					if (bFromDataAsset)
					{
						RemoveDataAssetLimit(RuntimeNode->BoxLimitsData, LimitsDataAsset->BoxLimits,
						                     GraphNode->Node.BoxLimitsData);
					}
					else
					{
						RemoveAnimNodeLimit(RuntimeNode->BoxLimits, GraphNode->Node.BoxLimits);
					}
					break;

				case ECollisionLimitType::Planar:
					if (bFromDataAsset)
					{
						RemoveDataAssetLimit(RuntimeNode->PlanarLimitsData, LimitsDataAsset->PlanarLimits,
						                     GraphNode->Node.PlanarLimitsData);
					}
					else
					{
						RemoveAnimNodeLimit(RuntimeNode->PlanarLimits, GraphNode->Node.PlanarLimits);
					}
					break;
				case ECollisionLimitType::None: break;
				default: ;
				}

				// 削除後は選択indexが縮んだ配列の別要素を指すため無効化する
				SelectCollisionIndex = -1;
				SelectCollisionType = ECollisionLimitType::None;
				SelectCollisionSourceType = ECollisionSourceType::AnimNode;
				SelectedCollisionGuid = FGuid();
			}
		}
	}

	return bHandled;
}

ECoordSystem FKawaiiPhysicsEditMode::GetWidgetCoordinateSystem() const
{
	return COORD_Local;
}

void FKawaiiPhysicsEditMode::OnExternalNodePropertyChange(FPropertyChangedEvent& InPropertyEvent)
{
	if (!IsValidSelectCollision())
	{
		SelectCollisionIndex = -1;
		SelectCollisionType = ECollisionLimitType::None;
		CurWidgetMode = UE_WIDGET::EWidgetMode::WM_None;
	}

	if (InPropertyEvent.Property &&
		InPropertyEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset))
	{
		// LimitsDataAsset差し替え時は旧アセットのdelegateを外してから新アセットへbindし直す（多重bind/解放後通知を防ぐ）
		if (UKawaiiPhysicsLimitsDataAsset* OldAsset = BoundLimitsDataAsset.Get())
		{
			OldAsset->OnLimitsChanged.Remove(LimitsDataAssetPropertyDelegateHandle);
		}
		LimitsDataAssetPropertyDelegateHandle.Reset();
		BoundLimitsDataAsset = nullptr;

		if (RuntimeNode->LimitsDataAsset)
		{
			LimitsDataAssetPropertyDelegateHandle = RuntimeNode->LimitsDataAsset->OnLimitsChanged.AddRaw(
				this, &FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange);
			BoundLimitsDataAsset = RuntimeNode->LimitsDataAsset;
		}
	}
}

void FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange(FPropertyChangedEvent& InPropertyEvent)
{
	GraphNode->Node.SphericalLimitsData = RuntimeNode->SphericalLimitsData;
	GraphNode->Node.CapsuleLimitsData = RuntimeNode->CapsuleLimitsData;
	GraphNode->Node.TaperedCapsuleLimitsData = RuntimeNode->TaperedCapsuleLimitsData;
	GraphNode->Node.BoxLimitsData = RuntimeNode->BoxLimitsData;
	GraphNode->Node.PlanarLimitsData = RuntimeNode->PlanarLimitsData;
}

bool FKawaiiPhysicsEditMode::IsSelectAnimNodeCollision() const
{
	return SelectCollisionSourceType == ECollisionSourceType::AnimNode;
}

bool FKawaiiPhysicsEditMode::IsValidSelectCollision() const
{
	if (RuntimeNode == nullptr || GraphNode == nullptr || SelectCollisionIndex < 0 || SelectCollisionType ==
		ECollisionLimitType::None)
	{
		return false;
	}

	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical:
		return !IsSelectAnimNodeCollision()
			       ? RuntimeNode->SphericalLimitsData.IsValidIndex(SelectCollisionIndex)
			       : RuntimeNode->SphericalLimits.IsValidIndex(SelectCollisionIndex);
	case ECollisionLimitType::Capsule:
		return !IsSelectAnimNodeCollision()
			       ? RuntimeNode->CapsuleLimitsData.IsValidIndex(SelectCollisionIndex)
			       : RuntimeNode->CapsuleLimits.IsValidIndex(SelectCollisionIndex);
	case ECollisionLimitType::TaperedCapsule:
		return !IsSelectAnimNodeCollision()
			       ? RuntimeNode->TaperedCapsuleLimitsData.IsValidIndex(SelectCollisionIndex)
			       : RuntimeNode->TaperedCapsuleLimits.IsValidIndex(SelectCollisionIndex);
	case ECollisionLimitType::Box:
		return !IsSelectAnimNodeCollision()
			       ? RuntimeNode->BoxLimitsData.IsValidIndex(SelectCollisionIndex)
			       : RuntimeNode->BoxLimits.IsValidIndex(SelectCollisionIndex);
	case ECollisionLimitType::Planar:
		return !IsSelectAnimNodeCollision()
			       ? RuntimeNode->PlanarLimitsData.IsValidIndex(SelectCollisionIndex)
			       : RuntimeNode->PlanarLimits.IsValidIndex(SelectCollisionIndex);
	case ECollisionLimitType::None: break;
	default: ;
	}
	return false;
}

FCollisionLimitBase* FKawaiiPhysicsEditMode::GetSelectCollisionLimitRuntime() const
{
	if (!IsValidSelectCollision())
	{
		return nullptr;
	}

	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical:
		return !IsSelectAnimNodeCollision()
			       ? &(RuntimeNode->SphericalLimitsData[SelectCollisionIndex])
			       : &(RuntimeNode->SphericalLimits[SelectCollisionIndex]);
	case ECollisionLimitType::Capsule:
		return !IsSelectAnimNodeCollision()
			       ? &(RuntimeNode->CapsuleLimitsData[SelectCollisionIndex])
			       : &(RuntimeNode->CapsuleLimits[SelectCollisionIndex]);
	case ECollisionLimitType::TaperedCapsule:
		return !IsSelectAnimNodeCollision()
			       ? &(RuntimeNode->TaperedCapsuleLimitsData[SelectCollisionIndex])
			       : &(RuntimeNode->TaperedCapsuleLimits[SelectCollisionIndex]);
	case ECollisionLimitType::Box:
		return !IsSelectAnimNodeCollision()
			       ? &(RuntimeNode->BoxLimitsData[SelectCollisionIndex])
			       : &(RuntimeNode->BoxLimits[SelectCollisionIndex]);
	case ECollisionLimitType::Planar:
		return !IsSelectAnimNodeCollision()
			       ? &(RuntimeNode->PlanarLimitsData[SelectCollisionIndex])
			       : &(RuntimeNode->PlanarLimits[SelectCollisionIndex]);
	case ECollisionLimitType::None: break;
	default: ;
	}

	return nullptr;
}

FCollisionLimitBase* FKawaiiPhysicsEditMode::GetSelectCollisionLimitGraph() const
{
	if (!IsValidSelectCollision())
	{
		return nullptr;
	}

	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical:
		{
			auto& CollisionLimits = !IsSelectAnimNodeCollision()
				                        ? GraphNode->Node.SphericalLimitsData
				                        : GraphNode->Node.SphericalLimits;
			return CollisionLimits.IsValidIndex(SelectCollisionIndex)
				       ? &CollisionLimits[SelectCollisionIndex]
				       : nullptr;
		}
	case ECollisionLimitType::Capsule:
		{
			auto& CollisionLimits = !IsSelectAnimNodeCollision()
				                        ? GraphNode->Node.CapsuleLimitsData
				                        : GraphNode->Node.CapsuleLimits;
			return CollisionLimits.IsValidIndex(SelectCollisionIndex)
				       ? &CollisionLimits[SelectCollisionIndex]
				       : nullptr;
		}
	case ECollisionLimitType::TaperedCapsule:
		{
			auto& CollisionLimits = !IsSelectAnimNodeCollision()
				                        ? GraphNode->Node.TaperedCapsuleLimitsData
				                        : GraphNode->Node.TaperedCapsuleLimits;
			return CollisionLimits.IsValidIndex(SelectCollisionIndex)
				       ? &CollisionLimits[SelectCollisionIndex]
				       : nullptr;
		}
	case ECollisionLimitType::Box:
		{
			auto& CollisionLimits = !IsSelectAnimNodeCollision()
				                        ? GraphNode->Node.BoxLimitsData
				                        : GraphNode->Node.BoxLimits;
			return CollisionLimits.IsValidIndex(SelectCollisionIndex)
				       ? &CollisionLimits[SelectCollisionIndex]
				       : nullptr;
		}
	case ECollisionLimitType::Planar:
		{
			auto& CollisionLimits = !IsSelectAnimNodeCollision()
				                        ? GraphNode->Node.PlanarLimitsData
				                        : GraphNode->Node.PlanarLimits;
			return CollisionLimits.IsValidIndex(SelectCollisionIndex)
				       ? &CollisionLimits[SelectCollisionIndex]
				       : nullptr;
		}
	case ECollisionLimitType::None: break;
	default: ;
	}

	return nullptr;
}

void FKawaiiPhysicsEditMode::DoTranslation(FVector& InTranslation)
{
	if (InTranslation.IsNearlyZero())
	{
		return;
	}
	if (SelectCollisionSourceType == ECollisionSourceType::Mirror)
	{
		// Mirror由来は自動生成のため編集しない
		return;
	}

	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Fail to edit limit." ));
		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Please try saving the DataAsset (%s) and compile this ABP." ),
			       *RuntimeNode->LimitsDataAsset.GetName());
		}
		return;
	}

	FVector Offset;
	if (CollisionRuntime->DrivingBone.BoneIndex >= 0)
	{
		const USkeletalMeshComponent* SkelComp = GetAnimPreviewScene().GetPreviewMeshComponent();
		Offset = ConvertCSVectorToBoneSpace(SkelComp, InTranslation, RuntimeNode->ForwardedPose,
		                                    CollisionRuntime->DrivingBone.BoneName, BCS_BoneSpace);
	}
	else
	{
		Offset = InTranslation;
	}
	CollisionRuntime->OffsetLocation += Offset;
	CollisionGraph->OffsetLocation = CollisionRuntime->OffsetLocation;

	// DataAssetがランタイムでnull化されてもキャッシュ由来の選択は残るため、書き戻し前にnullガード
	if (SelectCollisionSourceType == ECollisionSourceType::DataAsset && RuntimeNode->LimitsDataAsset)
	{
		RuntimeNode->LimitsDataAsset->UpdateLimit(CollisionRuntime);
	}
}

void FKawaiiPhysicsEditMode::DoRotation(FRotator& InRotation)
{
	if (InRotation.IsNearlyZero())
	{
		return;
	}
	if (SelectCollisionSourceType == ECollisionSourceType::Mirror)
	{
		// Mirror由来は自動生成のため編集しない
		return;
	}

	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Fail to edit limit." ));
		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Please try saving the DataAsset (%s) and compile this ABP." ),
			       *RuntimeNode->LimitsDataAsset.GetName());
		}
		return;
	}

	FQuat DeltaQuat;
	if (CollisionRuntime->DrivingBone.BoneIndex >= 0)
	{
		const USkeletalMeshComponent* SkelComp = GetAnimPreviewScene().GetPreviewMeshComponent();
		DeltaQuat = ConvertCSRotationToBoneSpace(SkelComp, InRotation, RuntimeNode->ForwardedPose,
		                                         CollisionRuntime->DrivingBone.BoneName, BCS_BoneSpace);
	}
	else
	{
		DeltaQuat = InRotation.Quaternion();
	}

	CollisionRuntime->OffsetRotation = FRotator(DeltaQuat * CollisionRuntime->OffsetRotation.Quaternion());
	CollisionGraph->OffsetRotation = CollisionRuntime->OffsetRotation;

	// DataAssetがランタイムでnull化されてもキャッシュ由来の選択は残るため、書き戻し前にnullガード
	if (SelectCollisionSourceType == ECollisionSourceType::DataAsset && RuntimeNode->LimitsDataAsset)
	{
		RuntimeNode->LimitsDataAsset->UpdateLimit(CollisionRuntime);
	}
}

void FKawaiiPhysicsEditMode::DoScale(FVector& InScale)
{
	if (SelectCollisionSourceType == ECollisionSourceType::Mirror)
	{
		// Mirror由来は自動生成のため編集しない
		return;
	}
	if (!IsValidSelectCollision() || InScale.IsNearlyZero() || SelectCollisionType == ECollisionLimitType::Planar)
	{
		return;
	}
	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Fail to edit limit." ));
		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Please try saving the DataAsset (%s) and compile this ABP." ),
			       *RuntimeNode->LimitsDataAsset.GetName());
		}
		return;
	}

	if (SelectCollisionType == ECollisionLimitType::Spherical)
	{
		FSphericalLimit& SphericalLimitRuntime = *static_cast<FSphericalLimit*>(CollisionRuntime);
		FSphericalLimit& SphericalLimitGraph = *static_cast<FSphericalLimit*>(CollisionGraph);

		SphericalLimitRuntime.Radius += InScale.X;
		SphericalLimitRuntime.Radius += InScale.Y;
		SphericalLimitRuntime.Radius += InScale.Z;
		SphericalLimitRuntime.Radius = FMath::Max(SphericalLimitRuntime.Radius, 0.0f);

		SphericalLimitGraph.Radius = SphericalLimitRuntime.Radius;

		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset && RuntimeNode->LimitsDataAsset)
		{
			RuntimeNode->LimitsDataAsset->UpdateLimit(&SphericalLimitRuntime);
		}
	}
	else if (SelectCollisionType == ECollisionLimitType::Capsule)
	{
		FCapsuleLimit& CapsuleLimitRuntime = *static_cast<FCapsuleLimit*>(CollisionRuntime);
		FCapsuleLimit& CapsuleLimitGraph = *static_cast<FCapsuleLimit*>(CollisionGraph);

		CapsuleLimitRuntime.Radius += InScale.X;
		CapsuleLimitRuntime.Radius += InScale.Y;
		CapsuleLimitRuntime.Radius = FMath::Max(CapsuleLimitRuntime.Radius, 0.0f);

		CapsuleLimitRuntime.Length += InScale.Z;
		CapsuleLimitRuntime.Length = FMath::Max(CapsuleLimitRuntime.Length, 0.0f);

		CapsuleLimitGraph.Radius = CapsuleLimitRuntime.Radius;
		CapsuleLimitGraph.Length = CapsuleLimitRuntime.Length;

		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset && RuntimeNode->LimitsDataAsset)
		{
			RuntimeNode->LimitsDataAsset->UpdateLimit(&CapsuleLimitRuntime);
		}
	}
	else if (SelectCollisionType == ECollisionLimitType::TaperedCapsule)
	{
		FTaperedCapsuleLimit& TaperedCapsuleLimitRuntime = *static_cast<FTaperedCapsuleLimit*>(CollisionRuntime);
		FTaperedCapsuleLimit& TaperedCapsuleLimitGraph = *static_cast<FTaperedCapsuleLimit*>(CollisionGraph);

		TaperedCapsuleLimitRuntime.Radius0 += InScale.X;
		TaperedCapsuleLimitRuntime.Radius0 = FMath::Max(TaperedCapsuleLimitRuntime.Radius0, 0.0f);

		TaperedCapsuleLimitRuntime.Radius1 += InScale.Y;
		TaperedCapsuleLimitRuntime.Radius1 = FMath::Max(TaperedCapsuleLimitRuntime.Radius1, 0.0f);

		TaperedCapsuleLimitRuntime.Length += InScale.Z;
		TaperedCapsuleLimitRuntime.Length = FMath::Max(TaperedCapsuleLimitRuntime.Length, 0.0f);

		TaperedCapsuleLimitGraph.Radius0 = TaperedCapsuleLimitRuntime.Radius0;
		TaperedCapsuleLimitGraph.Radius1 = TaperedCapsuleLimitRuntime.Radius1;
		TaperedCapsuleLimitGraph.Length = TaperedCapsuleLimitRuntime.Length;

		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset && RuntimeNode->LimitsDataAsset)
		{
			RuntimeNode->LimitsDataAsset->UpdateLimit(&TaperedCapsuleLimitRuntime);
		}
	}
	else if (SelectCollisionType == ECollisionLimitType::Box)
	{
		FBoxLimit& BoxLimitRuntime = *static_cast<FBoxLimit*>(CollisionRuntime);
		FBoxLimit& BoxLimitGraph = *static_cast<FBoxLimit*>(CollisionGraph);

		BoxLimitRuntime.Extent += InScale;
		BoxLimitRuntime.Extent.X = FMath::Max(BoxLimitRuntime.Extent.X, 0.0f);
		BoxLimitRuntime.Extent.Y = FMath::Max(BoxLimitRuntime.Extent.Y, 0.0f);
		BoxLimitRuntime.Extent.Z = FMath::Max(BoxLimitRuntime.Extent.Z, 0.0f);

		BoxLimitGraph.Extent = BoxLimitRuntime.Extent;

		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset && RuntimeNode->LimitsDataAsset)
		{
			RuntimeNode->LimitsDataAsset->UpdateLimit(&BoxLimitRuntime);
		}
	}
}


bool FKawaiiPhysicsEditMode::ShouldDrawWidget() const
{
	if (IsValidSelectCollision())
	{
		return true;
	}

	return false;
}

void FKawaiiPhysicsEditMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View,
                                     FCanvas* Canvas)
{
	if (!RuntimeNode || !GraphNode)
	{
		FAnimNodeEditMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
		return;
	}

	float FontWidth, FontHeight;
	GEngine->GetSmallFont()->GetCharSize(TEXT('L'), FontWidth, FontHeight);
	constexpr float XOffset = 5.0f;
	float DrawPositionY = Viewport->GetSizeXY().Y / Canvas->GetDPIScale() - (3 + FontHeight) - 100 / Canvas->
		GetDPIScale();

	if (!FAnimWeight::IsRelevant(RuntimeNode->GetAlpha()) || !RuntimeNode->IsRecentlyEvaluated())
	{
		DrawTextItem(
			LOCTEXT("", "This node does not evaluate recently."), Canvas, XOffset, DrawPositionY,
			FontHeight);
		FAnimNodeEditMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
		return;
	}

	DrawTextItem(LOCTEXT("", "Q : Cycle Transform Coordinate System"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(
		LOCTEXT("", "Space : Cycle Between Translate, Rotate and Scale"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("", "R : Scale Mode"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("", "E : Rotate Mode"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("", "W : Translate Mode"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("", "------------------"), Canvas, XOffset, DrawPositionY, FontHeight);


	FString CollisionDebugInfo = FString(TEXT("Select Collision : "));
	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical:
		CollisionDebugInfo.Append(FString(TEXT("Spherical")));
		break;
	case ECollisionLimitType::Capsule:
		CollisionDebugInfo.Append(FString(TEXT("Capsule")));
		break;
	case ECollisionLimitType::TaperedCapsule:
		CollisionDebugInfo.Append(FString(TEXT("TaperedCapsule")));
		break;
	case ECollisionLimitType::Box:
		CollisionDebugInfo.Append(FString(TEXT("Box")));
		break;
	case ECollisionLimitType::Planar:
		CollisionDebugInfo.Append(FString(TEXT("Planar")));
		break;
	default:
		CollisionDebugInfo.Append(FString(TEXT("None")));
		break;
	}
	if (SelectCollisionIndex >= 0)
	{
		CollisionDebugInfo.Append(FString(TEXT("[")));
		CollisionDebugInfo.Append(FString::FromInt(SelectCollisionIndex));
		CollisionDebugInfo.Append(FString(TEXT("]")));
	}
	DrawTextItem(FText::FromString(CollisionDebugInfo), Canvas, XOffset, DrawPositionY, FontHeight);

	const UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
	if (GraphNode->bEnableDebugBoneLengthRate)
	{
		if (PreviewMeshComponent != nullptr && PreviewMeshComponent->MeshObject != nullptr)
		{
			for (auto& Bone : RuntimeNode->ModifyBones)
			{
				FVector BoneLocation = Bone.Location;
				if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
				{
					const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
					BoneLocation = BaseBoneSpace2ComponentSpace.TransformPosition(BoneLocation);
				}

				// Refer to FAnimationViewportClient::ShowBoneNames
				const FVector BonePos = PreviewMeshComponent->GetComponentTransform().TransformPosition(BoneLocation);
				Draw3DTextItem(FText::AsNumber(Bone.LengthRateFromRoot), Canvas, View,
				               Viewport, BonePos);
			}
		}
	}

	// SyncBone
	if (GraphNode->bEnableDebugDrawSyncBone)
	{
		for (auto& SyncBone : RuntimeNode->SyncBones)
		{
			FString LenText = FString::Printf(TEXT("%.1f / %.1f"), SyncBone.ScaledDeltaDistance.Length(),
			                                  SyncBone.DeltaDistance.Length());
			Draw3DTextItem(FText::FromString(LenText), Canvas, View,
						   Viewport, PreviewMeshComponent->GetComponentTransform().TransformPosition(SyncBone.InitialPoseLocation));
		}
	}

	FAnimNodeEditMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

void FKawaiiPhysicsEditMode::DrawTextItem(const FText& Text, FCanvas* Canvas, float X, float& Y, float FontHeight)
{
	FCanvasTextItem TextItem(FVector2D::ZeroVector, Text, GEngine->GetSmallFont(), FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(TextItem, X, Y);
	Y -= (3 + FontHeight);
}

void FKawaiiPhysicsEditMode::Draw3DTextItem(const FText& Text, FCanvas* Canvas, const FSceneView* View,
                                            const FViewport* Viewport, FVector Location)
{
	const int32 HalfX = Viewport->GetSizeXY().X / 2 / Canvas->GetDPIScale();
	const int32 HalfY = Viewport->GetSizeXY().Y / 2 / Canvas->GetDPIScale();

	const FPlane proj = View->Project(Location);
	if (proj.W > 0.f)
	{
		const int32 XPos = HalfX + (HalfX * proj.X);
		const int32 YPos = HalfY + (HalfY * (proj.Y * -1));
		FCanvasTextItem TextItem(FVector2D(XPos, YPos), Text, GEngine->GetSmallFont(), FLinearColor::White);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
	}
}

#undef LOCTEXT_NAMESPACE
