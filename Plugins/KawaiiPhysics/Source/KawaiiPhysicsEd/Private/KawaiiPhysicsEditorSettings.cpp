// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEditorSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsEditorSettings)

const FLinearColor& FKawaiiPhysicsCollisionDrawStyle::GetColor(const ECollisionSourceType SourceType) const
{
	switch (SourceType)
	{
	case ECollisionSourceType::DataAsset:
		return DataAssetColor;
	case ECollisionSourceType::PhysicsAsset:
		return PhysicsAssetColor;
	case ECollisionSourceType::Mirror:
		return MirrorColor;
	default:
		return AnimNodeColor;
	}
}

UKawaiiPhysicsEditorSettings::UKawaiiPhysicsEditorSettings()
{
	// 平面は立体と違い奥の面が重ならないので、不透明度を1層分に下げて同じ濃さに見せる
	Planar.AnimNodeColor.A = 0.3f;
	Planar.DataAssetColor.A = 0.3f;
	Planar.MirrorColor.A = 0.3f;
	Planar.bDrawBorder = false;
}

const FKawaiiPhysicsCollisionDrawStyle& UKawaiiPhysicsEditorSettings::GetCollisionDrawStyle(
	const ECollisionLimitType LimitType) const
{
	switch (LimitType)
	{
	case ECollisionLimitType::Capsule:
		return Capsule;
	case ECollisionLimitType::TaperedCapsule:
		return TaperedCapsule;
	case ECollisionLimitType::Box:
		return Box;
	case ECollisionLimitType::Planar:
		return Planar;
	default:
		return Sphere;
	}
}
