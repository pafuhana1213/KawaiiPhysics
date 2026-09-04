// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "Curves/CurveFloat.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsSettings.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

#include "KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"

#if ENABLE_ANIM_DEBUG
void FAnimNode_KawaiiPhysics::AnimDrawDebug(FComponentSpacePoseContext& Output)
{
	if (const UWorld* World = Output.AnimInstanceProxy->GetSkelMeshComponent()->GetWorld(); !World->IsPreviewWorld())
	{
		if (Output.AnimInstanceProxy->GetSkelMeshComponent()->bRecentlyRendered)
		{
			if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
			{
				const auto AnimInstanceProxy = Output.AnimInstanceProxy;
				const float LineThickness = FMath::Max(
					0.0f, CVarAnimNodeKawaiiPhysicsDebugDrawThickness.GetValueOnAnyThread());

				// Modify Bones
				for (const auto& ModifyBone : ModifyBones)
				{
					const FVector LocationWS =
						ConvertSimulationSpaceLocation(Output, SimulationSpace,
						                               EKawaiiPhysicsSimulationSpace::WorldSpace, ModifyBone.Location);

					auto Color = ModifyBone.bBridgeDummy
					             ? FColor::Green
					             : (ModifyBone.bInterBoneDummy ? FColor::Cyan : (ModifyBone.bDummy ? FColor::Red : FColor::Yellow));
					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, ModifyBone.PhysicsSettings.Radius, 8,
					                                       Color, false, -1, LineThickness, SDPG_Foreground);

					AnimInstanceProxy->AnimDrawDebugInWorldMessage(
						FString::Printf(TEXT("%.2f"), ModifyBone.LengthRateFromRoot),
						ModifyBone.Location, FColor::White, 1.0f);

				}
				// Sphere limit
				for (const auto& SphericalLimit : SphericalLimits)
				{
					const FVector LocationWS =
						ConvertSimulationSpaceLocation(Output, SimulationSpace,
						                               EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SphericalLimit.Location);

					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Orange,
					                                       false, -1, LineThickness, SDPG_Foreground);
				}
				for (const auto& SphericalLimit : SphericalLimitsData)
				{
					const FVector LocationWS =
						ConvertSimulationSpaceLocation(Output, SimulationSpace,
						                               EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SphericalLimit.Location);
					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Blue,
					                                       false, -1, LineThickness, SDPG_Foreground);
				}

				// Box limit
				for (const auto& BoxLimit : BoxLimits)
				{
					this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
					                       FColor::Orange, LineThickness);
				}
				for (const auto& BoxLimit : BoxLimitsData)
				{
					this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
					                       FColor::Blue, LineThickness);
				}

				// Planar limit
				for (const auto& PlanarLimit : PlanarLimits)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(PlanarLimit.Rotation, PlanarLimit.Location));
					AnimInstanceProxy->AnimDrawDebugPlane(TransformWS, 50.0f,
					                                      FColor::Orange, false, -1, LineThickness, SDPG_Foreground);
				}
				for (const auto& PlanarLimit : PlanarLimitsData)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(PlanarLimit.Rotation, PlanarLimit.Location));
					AnimInstanceProxy->AnimDrawDebugPlane(TransformWS, 50.0f,
					                                      FColor::Blue, false, -1, LineThickness, SDPG_Foreground);
				}

				// テーパードカプセルコリジョン
				for (const auto& TaperedCapsuleLimit : TaperedCapsuleLimits)
				{
					this->AnimDrawDebugTaperedCapsule(Output, TaperedCapsuleLimit.Location,
					                                  TaperedCapsuleLimit.Rotation, TaperedCapsuleLimit.Radius0,
					                                  TaperedCapsuleLimit.Radius1, TaperedCapsuleLimit.Length,
					                                  FColor::Orange, LineThickness);
				}
				for (const auto& TaperedCapsuleLimit : TaperedCapsuleLimitsData)
				{
					this->AnimDrawDebugTaperedCapsule(Output, TaperedCapsuleLimit.Location,
					                                  TaperedCapsuleLimit.Rotation, TaperedCapsuleLimit.Radius0,
					                                  TaperedCapsuleLimit.Radius1, TaperedCapsuleLimit.Length,
					                                  FColor::Blue, LineThickness);
				}

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
				// Capsule limit
				for (const auto& CapsuleLimit : CapsuleLimits)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(CapsuleLimit.Rotation, CapsuleLimit.Location));

					AnimInstanceProxy->AnimDrawDebugCapsule(TransformWS.GetTranslation(), CapsuleLimit.Length * 0.5f,
					                                        CapsuleLimit.Radius, TransformWS.GetRotation().Rotator(),
					                                        FColor::Orange, false, -1, LineThickness, SDPG_Foreground);
				}
				for (const auto& CapsuleLimit : CapsuleLimitsData)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(CapsuleLimit.Rotation, CapsuleLimit.Location));

					AnimInstanceProxy->AnimDrawDebugCapsule(TransformWS.GetTranslation(), CapsuleLimit.Length * 0.5f,
					                                        CapsuleLimit.Radius, TransformWS.GetRotation().Rotator(),
					                                        FColor::Blue, false, -1, LineThickness, SDPG_Foreground);
				}
#endif

				// 共有コリジョン（緑）
				if (bUseSharedCollision && !bSharedCollisionSource)
				{
					for (const auto& SphericalLimit : SharedSphericalLimits)
					{
						const FVector LocationWS =
							ConvertSimulationSpaceLocation(Output, SimulationSpace,
							                               EKawaiiPhysicsSimulationSpace::WorldSpace,
							                               SphericalLimit.Location);
						AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Green,
						                                       false, -1, LineThickness, SDPG_Foreground);
					}

					for (const auto& BoxLimit : SharedBoxLimits)
					{
						this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
						                       FColor::Green, LineThickness);
					}

					for (const auto& PlanarLimit : SharedPlanarLimits)
					{
						FTransform PlanarTransformWS =
							ConvertSimulationSpaceTransform(Output, SimulationSpace,
							                                EKawaiiPhysicsSimulationSpace::WorldSpace,
							                                FTransform(PlanarLimit.Rotation, PlanarLimit.Location));
						AnimInstanceProxy->AnimDrawDebugPlane(PlanarTransformWS, 50.0f,
						                                      FColor::Green, false, -1, LineThickness, SDPG_Foreground);
					}

					for (const auto& TaperedCapsuleLimit : SharedTaperedCapsuleLimits)
					{
						this->AnimDrawDebugTaperedCapsule(Output, TaperedCapsuleLimit.Location,
						                                  TaperedCapsuleLimit.Rotation, TaperedCapsuleLimit.Radius0,
						                                  TaperedCapsuleLimit.Radius1, TaperedCapsuleLimit.Length,
						                                  FColor::Green, LineThickness);
					}

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
					for (const auto& CapsuleLimit : SharedCapsuleLimits)
					{
						FTransform CapsuleTransformWS =
							ConvertSimulationSpaceTransform(Output, SimulationSpace,
							                                EKawaiiPhysicsSimulationSpace::WorldSpace,
							                                FTransform(CapsuleLimit.Rotation, CapsuleLimit.Location));
						AnimInstanceProxy->AnimDrawDebugCapsule(CapsuleTransformWS.GetTranslation(),
						                                        CapsuleLimit.Length * 0.5f,
						                                        CapsuleLimit.Radius,
						                                        CapsuleTransformWS.GetRotation().Rotator(),
						                                        FColor::Green, false, -1, LineThickness,
						                                        SDPG_Foreground);
					}
#endif
				}

				// シンプルワールドコリジョン（水色）
				if (bUseSimpleWorldCollision)
				{
					for (const auto& SphericalLimit : SimpleWorldSphericalLimits)
					{
						const FVector LocationWS =
							ConvertSimulationSpaceLocation(Output, SimulationSpace,
							                               EKawaiiPhysicsSimulationSpace::WorldSpace,
							                               SphericalLimit.Location);
						AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Cyan,
						                                       false, -1, LineThickness, SDPG_Foreground);
					}

					for (const auto& BoxLimit : SimpleWorldBoxLimits)
					{
						this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
						                       FColor::Cyan, LineThickness);
					}

					for (const auto& BoxLimit : SimpleWorldGroundBoxLimits)
					{
						this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
						                       FColor::Cyan, LineThickness);
					}

					for (const auto& ConvexLimit : SimpleWorldConvexLimits)
					{
#if !UE_BUILD_SHIPPING
						if (!ConvexLimit.LocalVertices.IsEmpty() && !ConvexLimit.LocalEdges.IsEmpty())
						{
							const FTransform ConvexTransformWS =
								ConvertSimulationSpaceTransform(Output, SimulationSpace,
								                                EKawaiiPhysicsSimulationSpace::WorldSpace,
								                                FTransform(ConvexLimit.Rotation,
								                                           ConvexLimit.Location));
							for (int32 EdgeIndex = 0; EdgeIndex + 1 < ConvexLimit.LocalEdges.Num(); EdgeIndex += 2)
							{
								const int32 IndexA = ConvexLimit.LocalEdges[EdgeIndex];
								const int32 IndexB = ConvexLimit.LocalEdges[EdgeIndex + 1];
								if (!ConvexLimit.LocalVertices.IsValidIndex(IndexA) ||
									!ConvexLimit.LocalVertices.IsValidIndex(IndexB))
								{
									continue;
								}

								const FVector LocationAWS =
									ConvexTransformWS.TransformPosition(ConvexLimit.LocalVertices[IndexA]);
								const FVector LocationBWS =
									ConvexTransformWS.TransformPosition(ConvexLimit.LocalVertices[IndexB]);
								AnimInstanceProxy->AnimDrawDebugLine(LocationAWS, LocationBWS, FColor::Cyan,
								                                     false, -1.0f, LineThickness, SDPG_Foreground);
							}
							continue;
						}
#endif

						// DebugDraw CVar を後から有効にした場合、次回収集まで頂点/エッジが空のため LocalBounds で近似表示する。
						this->AnimDrawDebugBox(Output, ConvexLimit.Location, ConvexLimit.Rotation,
						                       ConvexLimit.LocalBounds.GetExtent(), FColor::Cyan, LineThickness);
					}

					for (const auto& TaperedCapsuleLimit : SimpleWorldTaperedCapsuleLimits)
					{
						this->AnimDrawDebugTaperedCapsule(Output, TaperedCapsuleLimit.Location,
						                                  TaperedCapsuleLimit.Rotation, TaperedCapsuleLimit.Radius0,
						                                  TaperedCapsuleLimit.Radius1, TaperedCapsuleLimit.Length,
						                                  FColor::Cyan, LineThickness);
					}

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
					for (const auto& CapsuleLimit : SimpleWorldCapsuleLimits)
					{
						FTransform CapsuleTransformWS =
							ConvertSimulationSpaceTransform(Output, SimulationSpace,
							                                EKawaiiPhysicsSimulationSpace::WorldSpace,
							                                FTransform(CapsuleLimit.Rotation, CapsuleLimit.Location));
						AnimInstanceProxy->AnimDrawDebugCapsule(CapsuleTransformWS.GetTranslation(),
						                                        CapsuleLimit.Length * 0.5f,
						                                        CapsuleLimit.Radius,
						                                        CapsuleTransformWS.GetRotation().Rotator(),
						                                        FColor::Cyan, false, -1, LineThickness,
						                                        SDPG_Foreground);
					}
#endif
				}
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AnimDrawDebugBox(FComponentSpacePoseContext& Output, const FVector& CenterLocationSim,
                                               const FQuat& RotationSim, const FVector& Extent,
                                               const FColor& Color, float LineThickness) const
{
	const auto AnimInstanceProxy = Output.AnimInstanceProxy;
	if (!AnimInstanceProxy)
	{
		return;
	}

	const FVector LocationWS =
		ConvertSimulationSpaceLocation(Output, SimulationSpace,
		                               EKawaiiPhysicsSimulationSpace::WorldSpace, CenterLocationSim);
	const FQuat RotationWS =
		ConvertSimulationSpaceRotation(Output, SimulationSpace,
		                               EKawaiiPhysicsSimulationSpace::WorldSpace, RotationSim);

	const FTransform BoxTransformWS(RotationWS, LocationWS);
	const FVector E(FMath::Abs(Extent.X), FMath::Abs(Extent.Y), FMath::Abs(Extent.Z));

	auto DrawFaceRect = [&](const FVector& FaceCenterLS, const FVector& FaceNormalLS, float HalfWidth, float HalfHeight)
	{
		const FVector FaceCenterWS = BoxTransformWS.TransformPosition(FaceCenterLS);
		const FVector NormalWS = RotationWS.RotateVector(FaceNormalLS).GetSafeNormal();

		const FVector AnyUpWS = (FMath::Abs(NormalWS.Z) < 0.999f) ? FVector::UpVector : FVector::RightVector;
		const FVector XAxisWS = FVector::CrossProduct(AnyUpWS, NormalWS).GetSafeNormal();
		const FVector YAxisWS = FVector::CrossProduct(NormalWS, XAxisWS).GetSafeNormal();

		const FVector P0 = FaceCenterWS + (XAxisWS * HalfWidth) + (YAxisWS * HalfHeight);
		const FVector P1 = FaceCenterWS - (XAxisWS * HalfWidth) + (YAxisWS * HalfHeight);
		const FVector P2 = FaceCenterWS - (XAxisWS * HalfWidth) - (YAxisWS * HalfHeight);
		const FVector P3 = FaceCenterWS + (XAxisWS * HalfWidth) - (YAxisWS * HalfHeight);

		AnimInstanceProxy->AnimDrawDebugLine(P0, P1, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
		AnimInstanceProxy->AnimDrawDebugLine(P1, P2, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
		AnimInstanceProxy->AnimDrawDebugLine(P2, P3, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
		AnimInstanceProxy->AnimDrawDebugLine(P3, P0, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
	};

	// +X / -X 面：YZ平面をカバー
	DrawFaceRect(FVector(E.X, 0, 0), FVector(1, 0, 0), E.Y, E.Z);
	DrawFaceRect(FVector(-E.X, 0, 0), FVector(-1, 0, 0), E.Y, E.Z);

	// +Y / -Y 面：XZ平面をカバー
	DrawFaceRect(FVector(0, E.Y, 0), FVector(0, 1, 0), E.X, E.Z);
	DrawFaceRect(FVector(0, -E.Y, 0), FVector(0, -1, 0), E.X, E.Z);

	// +Z / -Z 面：XY平面をカバー
	DrawFaceRect(FVector(0, 0, E.Z), FVector(0, 0, 1), E.X, E.Y);
	DrawFaceRect(FVector(0, 0, -E.Z), FVector(0, 0, -1), E.X, E.Y);
}

void FAnimNode_KawaiiPhysics::AnimDrawDebugTaperedCapsule(FComponentSpacePoseContext& Output,
                                                          const FVector& CenterLocationSim,
                                                          const FQuat& RotationSim, float Radius0, float Radius1,
                                                          float Length, const FColor& Color,
                                                          float LineThickness) const
{
	const auto AnimInstanceProxy = Output.AnimInstanceProxy;
	if (!AnimInstanceProxy)
	{
		return;
	}

	FTransform TransformWS =
		ConvertSimulationSpaceTransform(Output, SimulationSpace,
		                                EKawaiiPhysicsSimulationSpace::WorldSpace,
		                                FTransform(RotationSim, CenterLocationSim));
	const FVector CenterWS = TransformWS.GetLocation();
	const FQuat RotationWS = TransformWS.GetRotation();
	const float HalfLength = FMath::Max(Length, 0.0f) * 0.5f;
	const float R0 = FMath::Max(Radius0, 0.0f);
	const float R1 = FMath::Max(Radius1, 0.0f);

	const FVector AxisX = RotationWS.GetAxisX();
	const FVector AxisY = RotationWS.GetAxisY();
	const FVector AxisZ = RotationWS.GetAxisZ();
	const FVector StartPoint = CenterWS + AxisZ * HalfLength;
	const FVector EndPoint = CenterWS - AxisZ * HalfLength;

	AnimInstanceProxy->AnimDrawDebugSphere(StartPoint, R0, 8, Color, false, -1,
	                                       LineThickness, SDPG_Foreground);
	AnimInstanceProxy->AnimDrawDebugSphere(EndPoint, R1, 8, Color, false, -1,
	                                       LineThickness, SDPG_Foreground);

	AnimInstanceProxy->AnimDrawDebugLine(StartPoint + AxisX * R0, EndPoint + AxisX * R1, Color, false, -1.0f,
	                                     LineThickness, SDPG_Foreground);
	AnimInstanceProxy->AnimDrawDebugLine(StartPoint - AxisX * R0, EndPoint - AxisX * R1, Color, false, -1.0f,
	                                     LineThickness, SDPG_Foreground);
	AnimInstanceProxy->AnimDrawDebugLine(StartPoint + AxisY * R0, EndPoint + AxisY * R1, Color, false, -1.0f,
	                                     LineThickness, SDPG_Foreground);
	AnimInstanceProxy->AnimDrawDebugLine(StartPoint - AxisY * R0, EndPoint - AxisY * R1, Color, false, -1.0f,
	                                     LineThickness, SDPG_Foreground);
}
#endif
