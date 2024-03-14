#pragma once

#include "CoreMinimal.h"
#include "UnrealWidget.h"
#include "KawaiiPhysicsEditModeBase.h"
#include "AnimNodeEditMode.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysics.h"

#if	ENGINE_MAJOR_VERSION == 5
#define UE_WIDGET UE::Widget
#endif

class FEditorViewportClient;
class FPrimitiveDrawInterface;
class USkeletalMeshComponent;
struct FViewportClick;

#if	ENGINE_MAJOR_VERSION == 5
class FKawaiiPhysicsEditMode : public FAnimNodeEditMode
#else
class FKawaiiPhysicsEditMode : public FKawaiiPhysicsEditModeBase
#endif
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
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override;
	virtual bool ShouldDrawWidget() const override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;


protected:
	void OnExternalNodePropertyChange(FPropertyChangedEvent& InPropertyEvent);
	FDelegateHandle NodePropertyDelegateHandle;
	void OnObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent) const;

private:

	/** Render each collisions */
	void RenderSphericalLimits(FPrimitiveDrawInterface* PDI) const;
	void RenderCapsuleLimit(FPrimitiveDrawInterface* PDI) const;
	void RenderPlanerLimit(FPrimitiveDrawInterface* PDI);

	void RenderBoneConstraint(FPrimitiveDrawInterface* PDI) const;

	/** Helper function for GetWidgetLocation() and joint rendering */
	FVector GetWidgetLocation(ECollisionLimitType CollisionType, int32 Index) const;

	// methods to find a valid widget mode for gizmo because doesn't need to show gizmo when the mode is "Ignore"
	UE_WIDGET::EWidgetMode FindValidWidgetMode(UE_WIDGET::EWidgetMode InWidgetMode) const;

	/** Checking if a collision is selected and the collision is valid */
	bool IsValidSelectCollision() const;

	// Get Select Colliison Info
	FCollisionLimitBase* GetSelectCollisionLimitRuntime() const;
	FCollisionLimitBase* GetSelectCollisionLimitGraph() const;

	/** Draw text func for DrawHUD */
	void DrawTextItem(FText Text, FCanvas* Canvas, float X, float& Y, float FontHeight);
	void Draw3DTextItem(FText Text, FCanvas* Canvas, const FSceneView* View, const FViewport* Viewport, FVector Location);

private:
	/** Cache the typed nodes */
	struct FAnimNode_KawaiiPhysics* RuntimeNode;
	UAnimGraphNode_KawaiiPhysics* GraphNode;

	/** The current bone selection mode */
	ECollisionLimitType SelectCollisionType = ECollisionLimitType::None;
	int32 SelectCollisionIndex = -1;
	bool SelectCollisionIsFromDataAsset;

	// storing current widget mode 
	mutable UE_WIDGET::EWidgetMode CurWidgetMode;
};