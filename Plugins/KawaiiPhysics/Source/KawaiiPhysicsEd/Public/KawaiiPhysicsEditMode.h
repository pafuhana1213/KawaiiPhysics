// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimNodeEditMode.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysics.h"

#define UE_WIDGET UE::Widget

class FEditorViewportClient;
class FPrimitiveDrawInterface;
class USkeletalMeshComponent;
class UKawaiiPhysicsLimitsDataAsset;
struct FViewportClick;

class FKawaiiPhysicsEditMode : public FAnimNodeEditMode
{
public:
	FKawaiiPhysicsEditMode();

	/** IAnimNodeEditMode interface */
	virtual void EnterMode(class UAnimGraphNode_Base* InEditorNode, struct FAnimNode_Base* InRuntimeNode) override;
	virtual void ExitMode() override;
	virtual FVector GetWidgetLocation() const override;
	virtual UE_WIDGET::EWidgetMode GetWidgetMode() const override;
	virtual ECoordSystem GetWidgetCoordinateSystem() const override;
	virtual void DoTranslation(FVector& InTranslation) override;
	virtual void DoRotation(FRotator& InRotation) override;
	virtual void DoScale(FVector& InScale) override;

	/** FEdMode interface */
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy,
	                         const FViewportClick& Click) override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey,
	                      EInputEvent InEvent) override;
	virtual bool ShouldDrawWidget() const override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View,
	                     FCanvas* Canvas) override;

protected:
	void OnExternalNodePropertyChange(FPropertyChangedEvent& InPropertyEvent);
	FDelegateHandle NodePropertyDelegateHandle;

	void OnLimitDataAssetPropertyChange(FPropertyChangedEvent& InPropertyEvent);
	bool IsSelectAnimNodeCollision() const;
	FDelegateHandle LimitsDataAssetPropertyDelegateHandle;
	/** OnLimitsChangedをbind中のDataAsset（差し替え時に旧アセットからRemoveするため保持） */
	TWeakObjectPtr<UKawaiiPhysicsLimitsDataAsset> BoundLimitsDataAsset;

private:
	void RenderModifyBones(FPrimitiveDrawInterface* PDI) const;
	void RenderLimitAngle(FPrimitiveDrawInterface* PDI) const;
	void RenderSyncBone(FPrimitiveDrawInterface* PDI) const;

	/** Render each collision limit */
	void RenderSphericalLimits(FPrimitiveDrawInterface* PDI) const;
	void RenderCapsuleLimit(FPrimitiveDrawInterface* PDI) const;
	void RenderTaperedCapsuleLimit(FPrimitiveDrawInterface* PDI) const;
	void RenderBoxLimit(FPrimitiveDrawInterface* PDI) const;
	void RenderPlanarLimit(FPrimitiveDrawInterface* PDI) const;

	void RenderBoneConstraint(FPrimitiveDrawInterface* PDI) const;
	void RenderExternalForces(FPrimitiveDrawInterface* PDI) const;

	/** Helper function for GetWidgetLocation() and joint rendering */
	FVector GetWidgetLocation(ECollisionLimitType CollisionType, int32 Index) const;

	// methods to find a valid widget mode for the gizmo, because we don't need to show the gizmo when the mode is "Ignore"
	UE_WIDGET::EWidgetMode FindValidWidgetMode(UE_WIDGET::EWidgetMode InWidgetMode) const;

	/** Checking if a collision is selected and the collision is valid */
	bool IsValidSelectCollision() const;

	// Get selected collision info
	FCollisionLimitBase* GetSelectCollisionLimitRuntime() const;
	FCollisionLimitBase* GetSelectCollisionLimitGraph() const;

	/** Draw text func for DrawHUD */
	void DrawTextItem(const FText& Text, FCanvas* Canvas, float X, float& Y, float FontHeight);
	void Draw3DTextItem(const FText& Text, FCanvas* Canvas, const FSceneView* View, const FViewport* Viewport,
	                    FVector Location);

	/** Cache the typed nodes */
	struct FAnimNode_KawaiiPhysics* RuntimeNode;
	UAnimGraphNode_KawaiiPhysics* GraphNode;

	/** The current bone selection mode */
	ECollisionLimitType SelectCollisionType = ECollisionLimitType::None;
	int32 SelectCollisionIndex = -1;
	ECollisionSourceType SelectCollisionSourceType = ECollisionSourceType::AnimNode;
	/** 選択確定時のコリジョンの安定識別子。削除はindexでなくこのGuidで対象を引き、stale indexによる誤削除を防ぐ */
	FGuid SelectedCollisionGuid;

	// storing current widget mode 
	mutable UE_WIDGET::EWidgetMode CurWidgetMode;

	// physics asset body material
	TObjectPtr<UMaterialInstanceDynamic> PhysicsAssetBodyMaterial;
};
