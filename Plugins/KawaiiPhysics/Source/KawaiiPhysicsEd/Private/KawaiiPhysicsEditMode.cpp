// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsEditMode.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "IPersonaPreviewScene.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "SceneManagement.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

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
	GraphNode->Node.BoxLimitsData = RuntimeNode->BoxLimitsData;
	GraphNode->Node.PlanarLimitsData = RuntimeNode->PlanarLimitsData;
	GraphNode->Node.BoneConstraintsData = RuntimeNode->BoneConstraintsData;
	GraphNode->Node.MergedBoneConstraints = RuntimeNode->MergedBoneConstraints;
	GraphNode->Node.QuadCollisionBoneConstraintsData = RuntimeNode->QuadCollisionBoneConstraintsData;
	GraphNode->Node.MergedQuadCollisionBoneConstraints = RuntimeNode->MergedQuadCollisionBoneConstraints;
	GraphNode->Node.CachedQuads = RuntimeNode->CachedQuads;

	NodePropertyDelegateHandle = GraphNode->OnNodePropertyChanged().AddSP(
		this, &FKawaiiPhysicsEditMode::OnExternalNodePropertyChange);
	if (RuntimeNode->LimitsDataAsset != nullptr)
	{
		LimitsDataAssetPropertyDelegateHandle =
			RuntimeNode->LimitsDataAsset->OnLimitsChanged.AddRaw(
				this, &FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange);
	}

	UMaterialInterface* BaseElemSelectedMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EditorMaterials/PhAT_UnselectedMaterial.PhAT_UnselectedMaterial"), nullptr,
		LOAD_None, nullptr);
	PhysicsAssetBodyMaterial = UMaterialInstanceDynamic::Create(
		BaseElemSelectedMaterial, GetTransientPackage());
	PhysicsAssetBodyMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.2f);

	FKawaiiPhysicsEditModeBase::EnterMode(InEditorNode, InRuntimeNode);
}

void FKawaiiPhysicsEditMode::ExitMode()
{
	GraphNode->OnNodePropertyChanged().Remove(NodePropertyDelegateHandle);
	if (RuntimeNode->LimitsDataAsset != nullptr)
	{
		RuntimeNode->LimitsDataAsset->OnLimitsChanged.Remove(LimitsDataAssetPropertyDelegateHandle);
	}

	GraphNode = nullptr;
	RuntimeNode = nullptr;

	FKawaiiPhysicsEditModeBase::ExitMode();
}

void FKawaiiPhysicsEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const USkeletalMeshComponent* SkelMeshComp = GetAnimPreviewScene().GetPreviewMeshComponent();

	if (SkelMeshComp && SkelMeshComp->SkeletalMesh && SkelMeshComp->SkeletalMesh->GetSkeleton())
	{
		RenderModifyBones(PDI);
		RenderLimitAngle(PDI);
		RenderSphericalLimits(PDI);
		RenderCapsuleLimit(PDI);
		RenderBoxLimit(PDI);
		RenderPlanerLimit(PDI);
		RenderBoneConstraint(PDI);
		RenderBoneConstraint(PDI);
		RenderQuadCollision(PDI);
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

	FKawaiiPhysicsEditModeBase::Render(View, Viewport, PDI);
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
				auto Color = Bone.bDummy ? FColor::Red : FColor::Yellow;
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

void FKawaiiPhysicsEditMode::RenderSphericalLimits(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawSphereLimit)
	{
		for (int32 i = 0; i < RuntimeNode->SphericalLimits.Num(); i++)
		{
			auto& Sphere = RuntimeNode->SphericalLimits[i];
			if (Sphere.Radius > 0)
			{
				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Spherical, i));
				DrawSphere(PDI, Sphere.Location, FRotator::ZeroRotator, FVector(Sphere.Radius), 24, 6,
					GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
				DrawWireSphere(PDI, Sphere.Location, FLinearColor::Black, Sphere.Radius, 24, SDPG_World);
				DrawCoordinateSystem(PDI, Sphere.Location, Sphere.Rotation.Rotator(), Sphere.Radius, SDPG_World + 1);
			}
		}

		for (int32 i = 0; i < RuntimeNode->SphericalLimitsData.Num(); i++)
		{
			auto& Sphere = RuntimeNode->SphericalLimitsData[i];
			if (Sphere.Radius > 0)
			{
				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Spherical, i, Sphere.SourceType));
				DrawSphere(PDI, Sphere.Location, FRotator::ZeroRotator, FVector(Sphere.Radius), 24, 6,
					GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
				DrawWireSphere(PDI, Sphere.Location, FLinearColor::Black, Sphere.Radius, 24, SDPG_World);
				DrawCoordinateSystem(PDI, Sphere.Location, Sphere.Rotation.Rotator(), Sphere.Radius, SDPG_World + 1);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderCapsuleLimit(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawCapsuleLimit)
	{
		for (int32 i = 0; i < RuntimeNode->CapsuleLimits.Num(); i++)
		{
			auto& Capsule = RuntimeNode->CapsuleLimits[i];
			if (Capsule.Radius > 0 && Capsule.Length > 0)
			{
				FVector XAxis = Capsule.Rotation.GetAxisX();
				FVector YAxis = Capsule.Rotation.GetAxisY();
				FVector ZAxis = Capsule.Rotation.GetAxisZ();

				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Capsule, i));
				DrawCylinder(PDI, Capsule.Location, XAxis, YAxis, ZAxis, Capsule.Radius, 0.5f* Capsule.Length, 25,
					GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
				DrawSphere(PDI, Capsule.Location + ZAxis * Capsule.Length * 0.5f, Capsule.Rotation.Rotator(), FVector(Capsule.Radius),
					24, 6, GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
				DrawSphere(PDI, Capsule.Location - ZAxis * Capsule.Length * 0.5f, Capsule.Rotation.Rotator(), FVector(Capsule.Radius),
					24, 6, GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);

				DrawWireCapsule(PDI, Capsule.Location, XAxis, YAxis, ZAxis,
					FLinearColor::Black, Capsule.Radius, 0.5f* Capsule.Length + Capsule.Radius, 25, SDPG_World);

				DrawCoordinateSystem(PDI, Capsule.Location, Capsule.Rotation.Rotator(), Capsule.Radius, SDPG_World + 1);

			}
		}

		for (int32 i = 0; i < RuntimeNode->CapsuleLimitsData.Num(); i++)
		{
			auto& Capsule = RuntimeNode->CapsuleLimitsData[i];
			if (Capsule.Radius > 0 && Capsule.Length > 0)
			{
				FVector XAxis = Capsule.Rotation.GetAxisX();
				FVector YAxis = Capsule.Rotation.GetAxisY();
				FVector ZAxis = Capsule.Rotation.GetAxisZ();

				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Capsule, i, Capsule.SourceType));
				DrawCylinder(PDI, Capsule.Location, XAxis, YAxis, ZAxis, Capsule.Radius, 0.5f* Capsule.Length, 25,
					GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
				DrawSphere(PDI, Capsule.Location + ZAxis * Capsule.Length * 0.5f, Capsule.Rotation.Rotator(), FVector(Capsule.Radius),
					24, 6, GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
				DrawSphere(PDI, Capsule.Location - ZAxis * Capsule.Length * 0.5f, Capsule.Rotation.Rotator(), FVector(Capsule.Radius),
					24, 6, GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);

				DrawWireCapsule(PDI, Capsule.Location, XAxis, YAxis, ZAxis,
					FLinearColor::Black, Capsule.Radius, 0.5f * Capsule.Length + Capsule.Radius, 25, SDPG_World);

				DrawCoordinateSystem(PDI, Capsule.Location, Capsule.Rotation.Rotator(), Capsule.Radius, SDPG_World + 1);

			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderBoxLimit(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawBoxLimit)
	{
		for (int32 i = 0; i < RuntimeNode->BoxLimits.Num(); i++)
		{
			auto& Box = RuntimeNode->BoxLimits[i];
			if (Box.Extent.Size() > 0)
			{
				FTransform BoxTransform(Box.Rotation, Box.Location);

				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Box, i));
				DrawBox(PDI, BoxTransform.ToMatrixWithScale(), Box.Extent,
					GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
				DrawWireBox(PDI, BoxTransform.ToMatrixWithScale(), FBox(-Box.Extent, Box.Extent), FLinearColor::Black,
					SDPG_World);
				DrawCoordinateSystem(PDI, BoxTransform.GetLocation(), BoxTransform.Rotator(), Box.Extent.Size(), SDPG_World + 1);
			}
		}

		for (int32 i = 0; i < RuntimeNode->BoxLimitsData.Num(); i++)
		{
			auto& Box = RuntimeNode->BoxLimitsData[i];
			if (Box.Extent.Size() > 0)
			{
				FTransform BoxTransform(Box.Rotation, Box.Location);

				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Box, i, Box.SourceType));
				DrawBox(PDI, BoxTransform.ToMatrixWithScale(), Box.Extent,
					GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
				DrawWireBox(PDI, BoxTransform.ToMatrixWithScale(), FBox(-Box.Extent, Box.Extent), FLinearColor::Black,
					SDPG_World);
				DrawCoordinateSystem(PDI, BoxTransform.GetLocation(), BoxTransform.Rotator(), Box.Extent.Size(), SDPG_World + 1);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderPlanerLimit(FPrimitiveDrawInterface* PDI)
{
	if (GraphNode->bEnableDebugDrawPlanerLimit)
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
			DrawPlanarLimit(RuntimeNode->PlanarLimitsData[i], i, GEngine->ConstraintLimitMaterialZ->GetRenderProxy());
		}
	}
}

void FKawaiiPhysicsEditMode::RenderBoneConstraint(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawBoneConstraint)
	{
		for (const FModifyBoneConstraint& BoneConstraint : RuntimeNode->MergedBoneConstraints)
		{
			if (BoneConstraint.IsBoneReferenceValid() && RuntimeNode->ModifyBones.Num() > 0)
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

void FKawaiiPhysicsEditMode::RenderQuadCollision(FPrimitiveDrawInterface* PDI) const
{
	// Only check the debug draw toggle - don't require the feature to be enabled
	// This allows debugging visualization even when the feature is disabled
	if (!GraphNode || !RuntimeNode || !GraphNode->bEnableDebugDrawQuadCollision)
	{
		return;
	}

	// Early out if no quads to draw or no bones to reference
	if (RuntimeNode->CachedQuads.Num() == 0 || RuntimeNode->ModifyBones.Num() == 0)
	{
		return;
	}

	const FLinearColor QuadColor(0.0f, 1.0f, 1.0f); // Cyan for quad edges

	for (const FQuadCollisionLimit& Quad : RuntimeNode->CachedQuads)
	{
		if (!Quad.IsValid())
		{
			continue;
		}

		// Get quad corner locations
		FVector Corners[4];
		bool bValidQuad = true;
		for (int32 i = 0; i < 4; ++i)
		{
			if (Quad.BoneIndices[i] >= 0 && Quad.BoneIndices[i] < RuntimeNode->ModifyBones.Num())
			{
				Corners[i] = RuntimeNode->ModifyBones[Quad.BoneIndices[i]].Location;

				// Transform if in BaseBoneSpace
				if (RuntimeNode->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
				{
					const FTransform& BaseBoneSpace2ComponentSpace = RuntimeNode->GetBaseBoneSpace2ComponentSpace();
					Corners[i] = BaseBoneSpace2ComponentSpace.TransformPosition(Corners[i]);
				}
			}
			else
			{
				bValidQuad = false;
				break;
			}
		}

		if (!bValidQuad)
		{
			continue;
		}

		// Draw all 4 edges of the quad
		// Top edge (0 -> 1)
		PDI->DrawLine(Corners[0], Corners[1], QuadColor, SDPG_Foreground);
		// Bottom edge (2 -> 3)
		PDI->DrawLine(Corners[2], Corners[3], QuadColor, SDPG_Foreground);
		// Left edge (0 -> 2)
		PDI->DrawLine(Corners[0], Corners[2], QuadColor, SDPG_Foreground);
		// Right edge (1 -> 3)
		PDI->DrawLine(Corners[1], Corners[3], QuadColor, SDPG_Foreground);
	}
}

void FKawaiiPhysicsEditMode::RenderExternalForces(FPrimitiveDrawInterface* PDI) const
{
	if (GraphNode->bEnableDebugDrawExternalForce)
	{
		for (const auto& Bone : RuntimeNode->ModifyBones)
		{
			for (auto& Force : RuntimeNode->ExternalForces)
			{
				if (Force != nullptr)
				{
					Force->AnimDrawDebugForEditMode(Bone, *RuntimeNode, PDI);
				}
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

	FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime();
	if (Collision)
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

	FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime();
	if (Collision)
	{
		Rotation = Collision->Rotation;
	}

	InMatrix = FTransform(Rotation).ToMatrixNoScale();
	return true;
}

UE_WIDGET::EWidgetMode FKawaiiPhysicsEditMode::GetWidgetMode() const
{
	FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime();
	if (Collision)
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

bool FKawaiiPhysicsEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	bool bResult = FKawaiiPhysicsEditModeBase::HandleClick(InViewportClient, HitProxy, Click);

	if (HitProxy != nullptr && HitProxy->IsA(HKawaiiPhysicsHitProxy::StaticGetType()))
	{
		HKawaiiPhysicsHitProxy* KawaiiPhysicsHitProxy = static_cast<HKawaiiPhysicsHitProxy*>(HitProxy);
		SelectCollisionType = KawaiiPhysicsHitProxy->CollisionType;
		SelectCollisionIndex = KawaiiPhysicsHitProxy->CollisionIndex;
		SelectCollisionSourceType = KawaiiPhysicsHitProxy->SourceType;
		bResult = true;
	}
	else
	{
		SelectCollisionType = ECollisionLimitType::None;
		SelectCollisionIndex = -1;
	}

	return bResult;
}

bool FKawaiiPhysicsEditMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)
{
	bool bHandled = false;

	if ((InEvent == IE_Pressed)) //&& !bManipulating)
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
		else if (InKey == EKeys::Delete && SelectCollisionSourceType != ECollisionSourceType::PhysicsAsset && IsValidSelectCollision())
		{
			switch (SelectCollisionType)
			{
			case ECollisionLimitType::Spherical:
				if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
				{
					RuntimeNode->LimitsDataAsset->SphericalLimits.RemoveAt(SelectCollisionIndex);
					RuntimeNode->LimitsDataAsset->MarkPackageDirty();
				}
				else
				{
					RuntimeNode->SphericalLimits.RemoveAt(SelectCollisionIndex);
					GraphNode->Node.SphericalLimits.RemoveAt(SelectCollisionIndex);
				}
				break;
			case ECollisionLimitType::Capsule:
				if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
				{
					RuntimeNode->LimitsDataAsset->CapsuleLimits.RemoveAt(SelectCollisionIndex);
					RuntimeNode->LimitsDataAsset->MarkPackageDirty();
				}
				else
				{
					RuntimeNode->CapsuleLimits.RemoveAt(SelectCollisionIndex);
					GraphNode->Node.CapsuleLimits.RemoveAt(SelectCollisionIndex);
				}
				break;
			case ECollisionLimitType::Box:
				if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
				{
					RuntimeNode->LimitsDataAsset->BoxLimits.RemoveAt(SelectCollisionIndex);
					RuntimeNode->LimitsDataAsset->MarkPackageDirty();
				}
				else
				{
					RuntimeNode->BoxLimits.RemoveAt(SelectCollisionIndex);
					GraphNode->Node.BoxLimits.RemoveAt(SelectCollisionIndex);
				}
				break;

			case ECollisionLimitType::Planar:
				if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
				{
					RuntimeNode->LimitsDataAsset->PlanarLimits.RemoveAt(SelectCollisionIndex);
					RuntimeNode->LimitsDataAsset->MarkPackageDirty();
				}
				else
				{
					RuntimeNode->PlanarLimits.RemoveAt(SelectCollisionIndex);
					GraphNode->Node.PlanarLimits.RemoveAt(SelectCollisionIndex);
				}
				break;
			case ECollisionLimitType::None: break;
			default: ;
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

	if (InPropertyEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset))
	{
		if (RuntimeNode->LimitsDataAsset != nullptr)
		{
			RuntimeNode->LimitsDataAsset->OnLimitsChanged.AddRaw(
				this, &FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange);
		}
	}
}

void FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange(FPropertyChangedEvent& InPropertyEvent)
{
	GraphNode->Node.SphericalLimitsData = RuntimeNode->SphericalLimitsData;
	GraphNode->Node.CapsuleLimitsData = RuntimeNode->CapsuleLimitsData;
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

	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Fail to edit limit." ));
		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Please try saving the DataAsset (%s) and compile this ABP." ),
			       *RuntimeNode->LimitsDataAsset->GetName());
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

	if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
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

	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Fail to edit limit." ));
		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT( "Please try saving the DataAsset (%s) and compile this ABP." ),
			       *RuntimeNode->LimitsDataAsset->GetName());
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

	if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
	{
		RuntimeNode->LimitsDataAsset->UpdateLimit(CollisionRuntime);
	}
}

void FKawaiiPhysicsEditMode::DoScale(FVector& InScale)
{
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
			       *RuntimeNode->LimitsDataAsset->GetName());
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

		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
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

		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
		{
			RuntimeNode->LimitsDataAsset->UpdateLimit(&CapsuleLimitRuntime);
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

		if (SelectCollisionSourceType == ECollisionSourceType::DataAsset)
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
	float FontWidth, FontHeight;
	GEngine->GetSmallFont()->GetCharSize(TEXT('L'), FontWidth, FontHeight);
	constexpr float XOffset = 5.0f;
	float DrawPositionY = Viewport->GetSizeXY().Y / Canvas->GetDPIScale() - (3 + FontHeight) - 100 / Canvas->
		GetDPIScale();

	if (!RuntimeNode->IsRecentlyEvaluated())
	{
		DrawTextItem(
			LOCTEXT("", "This node does not evaluate recently."), Canvas, XOffset, DrawPositionY,
			FontHeight);
		FKawaiiPhysicsEditModeBase::DrawHUD(ViewportClient, Viewport, View, Canvas);
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

	FKawaiiPhysicsEditModeBase::DrawHUD(ViewportClient, Viewport, View, Canvas);
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
