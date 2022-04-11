#include "KawaiiPhysicsEditMode.h"
#include "SceneManagement.h"
#include "EngineUtils.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "EditorModeManager.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AnimationRuntime.h"
#include "KawaiiPhysicsLimitsDataAsset.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsEditMode"

struct HKawaiiPhysicsHitProxy : public HHitProxy
{
	DECLARE_HIT_PROXY()

	HKawaiiPhysicsHitProxy(ECollisionLimitType InType, int InIndex, bool InFromDataAsset = false)
		: HHitProxy(HPP_Wireframe)
		, CollisionType(InType)
		, CollisionIndex(InIndex)
		, bFromDataAsset(InFromDataAsset)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}

	ECollisionLimitType CollisionType;
	int CollisionIndex;
	bool bFromDataAsset;
};
IMPLEMENT_HIT_PROXY(HKawaiiPhysicsHitProxy, HHitProxy);


FKawaiiPhysicsEditMode::FKawaiiPhysicsEditMode()
	: RuntimeNode(nullptr)
	, GraphNode(nullptr)
	, CurWidgetMode(UE_WIDGET::EWidgetMode::WM_Translate)
{
}

void FKawaiiPhysicsEditMode::EnterMode(UAnimGraphNode_Base* InEditorNode, FAnimNode_Base* InRuntimeNode)
{
	RuntimeNode = static_cast<FAnimNode_KawaiiPhysics*>(InRuntimeNode);
	GraphNode = CastChecked<UAnimGraphNode_KawaiiPhysics>(InEditorNode);

	GraphNode->Node.SphericalLimitsData = RuntimeNode->SphericalLimitsData;
	GraphNode->Node.CapsuleLimitsData = RuntimeNode->CapsuleLimitsData;
	GraphNode->Node.PlanarLimitsData = RuntimeNode->PlanarLimitsData;

	NodePropertyDelegateHandle = GraphNode->OnNodePropertyChanged().AddSP(this, &FKawaiiPhysicsEditMode::OnExternalNodePropertyChange);

	FKawaiiPhysicsEditModeBase::EnterMode(InEditorNode, InRuntimeNode);
}

void FKawaiiPhysicsEditMode::ExitMode()
{
	GraphNode->OnNodePropertyChanged().Remove(NodePropertyDelegateHandle);

	GraphNode = nullptr;
	RuntimeNode = nullptr;

	FKawaiiPhysicsEditModeBase::ExitMode();
}

void FKawaiiPhysicsEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	USkeletalMeshComponent* SkelMeshComp = GetAnimPreviewScene().GetPreviewMeshComponent();
	if (SkelMeshComp && SkelMeshComp->SkeletalMesh && SkelMeshComp->SkeletalMesh->GetSkeleton())
	{
		RenderSphericalLimits(PDI);
		RenderCapsuleLimit(PDI);
		RenderPlanerLimit(PDI);
		PDI->SetHitProxy(nullptr);

		if (IsValidSelectCollision())
		{
			FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime();
			if (Collision)
			{
				FTransform BoneTransform = FTransform::Identity;
				if (Collision->DrivingBone.BoneIndex >= 0)
				{
					BoneTransform = RuntimeNode->ForwardedPose.GetComponentSpaceTransform(
						Collision->DrivingBone.GetCompactPoseIndex(RuntimeNode->ForwardedPose.GetPose().GetBoneContainer()));
				}
				PDI->DrawPoint(BoneTransform.GetLocation(), FLinearColor::White, 10.0f, SDPG_Foreground);
				DrawDashedLine(PDI, Collision->Location, BoneTransform.GetLocation(),
					FLinearColor::White, 1, SDPG_Foreground);
				DrawCoordinateSystem(PDI, BoneTransform.GetLocation(), Collision->Rotation.Rotator(), 20, SDPG_World + 1);

			}
		}
	}
	FKawaiiPhysicsEditModeBase::Render(View, Viewport, PDI);
}

void FKawaiiPhysicsEditMode::RenderSphericalLimits(FPrimitiveDrawInterface* PDI)
{
	if (GraphNode->bEnableDebugDrawSphereLimit)
	{
		for( int i=0; i< RuntimeNode->SphericalLimits.Num(); i++)
		{
			auto& Sphere = RuntimeNode->SphericalLimits[i];
			if (Sphere.Radius > 0)
			{
				FTransform SphereTransform = FTransform(Sphere.Location);

				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Spherical, i));
				DrawSphere(PDI, Sphere.Location, FRotator::ZeroRotator, FVector(Sphere.Radius), 24, 6,
					GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
				DrawWireSphere(PDI, Sphere.Location, FLinearColor::Black, Sphere.Radius, 24, SDPG_World);
				DrawCoordinateSystem(PDI, Sphere.Location, Sphere.Rotation.Rotator(), Sphere.Radius, SDPG_World + 1);
			}
		}

		for (int i = 0; i < RuntimeNode->SphericalLimitsData.Num(); i++)
		{
			auto& Sphere = RuntimeNode->SphericalLimitsData[i];
			if (Sphere.Radius > 0)
			{
				FTransform SphereTransform = FTransform(Sphere.Location);

				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Spherical, i, true));
				DrawSphere(PDI, Sphere.Location, FRotator::ZeroRotator, FVector(Sphere.Radius), 24, 6,
					GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
				DrawWireSphere(PDI, Sphere.Location, FLinearColor::Black, Sphere.Radius, 24, SDPG_World);
				DrawCoordinateSystem(PDI, Sphere.Location, Sphere.Rotation.Rotator(), Sphere.Radius, SDPG_World + 1);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderCapsuleLimit(FPrimitiveDrawInterface* PDI)
{
	if (GraphNode->bEnableDebugDrawCapsuleLimit)
	{
		for (int i = 0; i < RuntimeNode->CapsuleLimits.Num(); i++)
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

		for (int i = 0; i < RuntimeNode->CapsuleLimitsData.Num(); i++)
		{
			auto& Capsule = RuntimeNode->CapsuleLimitsData[i];
			if (Capsule.Radius > 0 && Capsule.Length > 0)
			{
				FVector XAxis = Capsule.Rotation.GetAxisX();
				FVector YAxis = Capsule.Rotation.GetAxisY();
				FVector ZAxis = Capsule.Rotation.GetAxisZ();

				PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Capsule, i, true));
				DrawCylinder(PDI, Capsule.Location, XAxis, YAxis, ZAxis, Capsule.Radius, 0.5f* Capsule.Length, 25,
					GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
				DrawSphere(PDI, Capsule.Location + ZAxis * Capsule.Length * 0.5f, Capsule.Rotation.Rotator(), FVector(Capsule.Radius),
					24, 6, GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
				DrawSphere(PDI, Capsule.Location - ZAxis * Capsule.Length * 0.5f, Capsule.Rotation.Rotator(), FVector(Capsule.Radius),
					24, 6, GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);

				DrawWireCapsule(PDI, Capsule.Location, XAxis, YAxis, ZAxis,
					FLinearColor::Black, Capsule.Radius, 0.5f* Capsule.Length + Capsule.Radius, 25, SDPG_World);

				DrawCoordinateSystem(PDI, Capsule.Location, Capsule.Rotation.Rotator(), Capsule.Radius, SDPG_World + 1);

			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderPlanerLimit(FPrimitiveDrawInterface* PDI)
{
	if (GraphNode->bEnableDebugDrawPlanerLimit)
	{
		for (int i = 0; i < RuntimeNode->PlanarLimits.Num(); i++)
		{
			auto& Plane = RuntimeNode->PlanarLimits[i];
			
			FTransform PlaneTransform = FTransform(Plane.Rotation, Plane.Location);
			PlaneTransform.NormalizeRotation();

			PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Planar, i));
			DrawPlane10x10(PDI, PlaneTransform.ToMatrixWithScale(), 200.0f, FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
			DrawDirectionalArrow(PDI, FRotationMatrix(FRotator(90.0f, 0.0f, 0.0f)) * PlaneTransform.ToMatrixWithScale(), FLinearColor::Blue, 50.0f, 20.0f, SDPG_Foreground, 0.5f);
		}

		for (int i = 0; i < RuntimeNode->PlanarLimitsData.Num(); i++)
		{
			auto& Plane = RuntimeNode->PlanarLimitsData[i];

			FTransform PlaneTransform = FTransform(Plane.Rotation, Plane.Location);
			PlaneTransform.NormalizeRotation();

			PDI->SetHitProxy(new HKawaiiPhysicsHitProxy(ECollisionLimitType::Planar, i, true));
			DrawPlane10x10(PDI, PlaneTransform.ToMatrixWithScale(), 200.0f, FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), GEngine->ConstraintLimitMaterialZ->GetRenderProxy(), SDPG_World);
			DrawDirectionalArrow(PDI, FRotationMatrix(FRotator(90.0f, 0.0f, 0.0f)) * PlaneTransform.ToMatrixWithScale(), FLinearColor::Blue, 50.0f, 20.0f, SDPG_Foreground, 0.5f);
		}
	}
}

FVector FKawaiiPhysicsEditMode::GetWidgetLocation(ECollisionLimitType CollisionType, int Index) const
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
		SelectCollisionIsFromDataAsset = KawaiiPhysicsHitProxy->bFromDataAsset;
		return true;
	}

	SelectCollisionType = ECollisionLimitType::None;
	SelectCollisionIndex = -1;

	return false;
}

bool FKawaiiPhysicsEditMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)
{
	bool bHandled = false;

	if ((InEvent == IE_Pressed) && !bManipulating)
	{
		if (InKey == EKeys::SpaceBar)
		{
			GetModeManager()->SetWidgetMode(GetWidgetMode());
			bHandled = true;
			InViewportClient->Invalidate();
		}
		else if (InKey == EKeys::Q)
		{
			auto CoordSystem = GetModeManager()->GetCoordSystem();
			GetModeManager()->SetCoordSystem(CoordSystem == COORD_Local ? COORD_World : COORD_Local);
		}
		else if (InKey == EKeys::Delete && IsValidSelectCollision())
		{
			switch (SelectCollisionType)
			{
			case ECollisionLimitType::Spherical:
				RuntimeNode->SphericalLimits.RemoveAt(SelectCollisionIndex);
				GraphNode->Node.SphericalLimits.RemoveAt(SelectCollisionIndex);
				break;
			case ECollisionLimitType::Capsule:
				RuntimeNode->CapsuleLimits.RemoveAt(SelectCollisionIndex);
				GraphNode->Node.CapsuleLimits.RemoveAt(SelectCollisionIndex);
				break;
			case ECollisionLimitType::Planar:
				RuntimeNode->PlanarLimits.RemoveAt(SelectCollisionIndex);
				GraphNode->Node.PlanarLimits.RemoveAt(SelectCollisionIndex);
				break;
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
	USkeletalMeshComponent* SkelComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	if (!IsValidSelectCollision())
	{
		SelectCollisionIndex = -1;
		SelectCollisionType = ECollisionLimitType::None;
		CurWidgetMode = UE_WIDGET::EWidgetMode::WM_None;
	}
}

bool FKawaiiPhysicsEditMode::IsValidSelectCollision() const
{
	if (RuntimeNode == nullptr || GraphNode == nullptr || SelectCollisionIndex < 0 || SelectCollisionType == ECollisionLimitType::None)
	{
		return false;
	}

	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical:
		return SelectCollisionIsFromDataAsset ? RuntimeNode->SphericalLimitsData.IsValidIndex(SelectCollisionIndex)
			: RuntimeNode->SphericalLimits.IsValidIndex(SelectCollisionIndex);
	case ECollisionLimitType::Capsule:
		return SelectCollisionIsFromDataAsset ? RuntimeNode->CapsuleLimitsData.IsValidIndex(SelectCollisionIndex)
			: RuntimeNode->CapsuleLimits.IsValidIndex(SelectCollisionIndex);
	case ECollisionLimitType::Planar:
		return SelectCollisionIsFromDataAsset ? RuntimeNode->PlanarLimitsData.IsValidIndex(SelectCollisionIndex)
			: RuntimeNode->PlanarLimits.IsValidIndex(SelectCollisionIndex);
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
		return SelectCollisionIsFromDataAsset ? &(RuntimeNode->SphericalLimitsData[SelectCollisionIndex])
			: &(RuntimeNode->SphericalLimits[SelectCollisionIndex]);
	case ECollisionLimitType::Capsule:
		return SelectCollisionIsFromDataAsset ? &(RuntimeNode->CapsuleLimitsData[SelectCollisionIndex])
			: &(RuntimeNode->CapsuleLimits[SelectCollisionIndex]);
	case ECollisionLimitType::Planar:
		return SelectCollisionIsFromDataAsset ? &(RuntimeNode->PlanarLimitsData[SelectCollisionIndex])
			: &(RuntimeNode->PlanarLimits[SelectCollisionIndex]);
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
		return SelectCollisionIsFromDataAsset ? &(GraphNode->Node.SphericalLimitsData[SelectCollisionIndex])
			: &(GraphNode->Node.SphericalLimits[SelectCollisionIndex]);
	case ECollisionLimitType::Capsule:
		return SelectCollisionIsFromDataAsset ? &(GraphNode->Node.CapsuleLimitsData[SelectCollisionIndex])
			: &(GraphNode->Node.CapsuleLimits[SelectCollisionIndex]);
	case ECollisionLimitType::Planar:
		return SelectCollisionIsFromDataAsset ? &(GraphNode->Node.PlanarLimitsData[SelectCollisionIndex])
			: &(GraphNode->Node.PlanarLimits[SelectCollisionIndex]);
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
		return;
	}

	FVector Offset = FVector::ZeroVector;
	if (CollisionRuntime->DrivingBone.BoneIndex >= 0)
	{
		USkeletalMeshComponent* SkelComp = GetAnimPreviewScene().GetPreviewMeshComponent();
		Offset = ConvertCSVectorToBoneSpace(SkelComp, InTranslation, RuntimeNode->ForwardedPose, CollisionRuntime->DrivingBone.BoneName, BCS_BoneSpace);
	}
	else
	{
		Offset = InTranslation;
	}
	CollisionRuntime->OffsetLocation += Offset;
	CollisionGraph->OffsetLocation = CollisionRuntime->OffsetLocation;

	if(SelectCollisionIsFromDataAsset)
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
		return;
	}

	USkeletalMeshComponent* SkelComp = GetAnimPreviewScene().GetPreviewMeshComponent();
	FQuat DeltaQuat = ConvertCSRotationToBoneSpace(SkelComp, InRotation, RuntimeNode->ForwardedPose, CollisionRuntime->DrivingBone.BoneName, BCS_BoneSpace);
	CollisionRuntime->OffsetRotation = FRotator(DeltaQuat * CollisionRuntime->OffsetRotation.Quaternion());
	CollisionGraph->OffsetRotation = CollisionRuntime->OffsetRotation;

	if (SelectCollisionIsFromDataAsset)
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


	if (SelectCollisionType == ECollisionLimitType::Spherical)
	{
		FSphericalLimit& SphericalLimitRuntime = SelectCollisionIsFromDataAsset ? RuntimeNode->SphericalLimitsData[SelectCollisionIndex]
			: RuntimeNode->SphericalLimits[SelectCollisionIndex];

		FSphericalLimit& SphericalLimitGraph = SelectCollisionIsFromDataAsset ? (GraphNode->Node.SphericalLimitsData[SelectCollisionIndex])
			: (GraphNode->Node.SphericalLimits[SelectCollisionIndex]);

		SphericalLimitRuntime.Radius += InScale.X;
		SphericalLimitRuntime.Radius += InScale.Y;
		SphericalLimitRuntime.Radius += InScale.Z;
		SphericalLimitRuntime.Radius = FMath::Max(SphericalLimitRuntime.Radius, 0.0f);
		
		SphericalLimitGraph.Radius = SphericalLimitRuntime.Radius;

		if (SelectCollisionIsFromDataAsset)
		{
			RuntimeNode->LimitsDataAsset->UpdateLimit(&SphericalLimitRuntime);
		}
	}
	else if (SelectCollisionType == ECollisionLimitType::Capsule)
	{
		FCapsuleLimit& CapsuleLimitRuntime = SelectCollisionIsFromDataAsset ? RuntimeNode->CapsuleLimitsData[SelectCollisionIndex]
			: RuntimeNode->CapsuleLimits[SelectCollisionIndex];

		FCapsuleLimit& CapsuleLimitGraph = SelectCollisionIsFromDataAsset ? GraphNode->Node.CapsuleLimitsData[SelectCollisionIndex]
			: GraphNode->Node.CapsuleLimits[SelectCollisionIndex];

		CapsuleLimitRuntime.Radius += InScale.X;
		CapsuleLimitRuntime.Radius += InScale.Y;
		CapsuleLimitRuntime.Radius = FMath::Max(CapsuleLimitRuntime.Radius, 0.0f);

		CapsuleLimitRuntime.Length += InScale.Z;
		CapsuleLimitRuntime.Length = FMath::Max(CapsuleLimitRuntime.Length, 0.0f);

		CapsuleLimitGraph.Radius = CapsuleLimitRuntime.Radius;
		CapsuleLimitGraph.Length = CapsuleLimitRuntime.Length;

		if (SelectCollisionIsFromDataAsset)
		{
			RuntimeNode->LimitsDataAsset->UpdateLimit(&CapsuleLimitRuntime);
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

void FKawaiiPhysicsEditMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	float FontWidth, FontHeight;
	GEngine->GetSmallFont()->GetCharSize(TEXT('L'), FontWidth, FontHeight);

	const float XOffset = 5.0f;

	float DrawPositionY = Viewport->GetSizeXY().Y / Canvas->GetDPIScale() - (3 + FontHeight) - 100 / Canvas->GetDPIScale();

	DrawTextItem(LOCTEXT("", "Q : Cycle Transform Coordinate System"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("", "Space : Cycle Between Translate, Rotate and Scale"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("", "R : Scale Mode"), Canvas,XOffset, DrawPositionY, FontHeight);
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

	if (GraphNode->bEnableDebugBoneLengthRate)
	{
		UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
		if (PreviewMeshComponent != nullptr && PreviewMeshComponent->MeshObject != nullptr)
		{
			for (auto& Bone : RuntimeNode->ModifyBones)
			{
				// Refer to FAnimationViewportClient::ShowBoneNames
				const FVector BonePos = PreviewMeshComponent->GetComponentTransform().TransformPosition(Bone.Location);
				Draw3DTextItem(FText::AsNumber(Bone.LengthFromRoot / RuntimeNode->GetTotalBoneLength()), Canvas, View, Viewport, BonePos );
			}
		}
	}

	FKawaiiPhysicsEditModeBase::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

void FKawaiiPhysicsEditMode::DrawTextItem(FText Text, FCanvas* Canvas, float X, float& Y, float FontHeight)
{
	FCanvasTextItem TextItem(FVector2D::ZeroVector, Text, GEngine->GetSmallFont(), FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(TextItem, X, Y);
	Y -= (3 + FontHeight);
}

void FKawaiiPhysicsEditMode::Draw3DTextItem(FText Text, FCanvas* Canvas, const FSceneView* View, FViewport* Viewport, FVector Location)
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