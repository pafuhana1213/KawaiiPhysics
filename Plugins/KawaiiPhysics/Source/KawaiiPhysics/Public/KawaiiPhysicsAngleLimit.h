// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KawaiiPhysicsTypes.h"

/**
 * 軸ごとの角度制限の計算。ソルバーとエディタのプレビューが同じ形状を扱うよう、ここに集約する。
 * 角度はポーズ方向から現在方向への回転を、回転ベクトルの2軸成分 (A, B) として度数で表す。
 * Per-axis angle limit math. Shared here so the solver and the editor preview work on the same shape.
 * Angles are the two components (A, B), in degrees, of the rotation vector from the pose direction to the current direction.
 */
namespace KawaiiPhysics::AngleLimit
{
	/** 有効な2軸の許容範囲（度）。Min は0以下、Max は0以上 / Allowed range of the two active axes in degrees. Min <= 0 <= Max */
	struct FSwingLimit
	{
		float MinA = 0.0f;
		float MaxA = 0.0f;
		float MinB = 0.0f;
		float MaxB = 0.0f;
	};

	/** ポーズ方向に直交する回転軸の組（シミュレーション空間） / Pair of rotation axes orthogonal to the pose direction, in simulation space */
	struct FSwingFrame
	{
		FVector AxisA = FVector::YAxisVector;
		FVector AxisB = FVector::ZAxisVector;
	};

	/**
	 * BoneForwardAxis から、ツイスト軸と制限に使う2軸を返す
	 * Returns the twist axis and the two limit axes for a BoneForwardAxis
	 */
	KAWAIIPHYSICS_API EAxis::Type GetTwistAxis(EBoneForwardAxis ForwardAxis);
	KAWAIIPHYSICS_API void GetSwingAxes(EBoneForwardAxis ForwardAxis, EAxis::Type& OutAxisA, EAxis::Type& OutAxisB);

	/**
	 * BoneForwardAxis に応じて、軸ごとの設定から有効な2軸を取り出す
	 * Extracts the two active axes from the per-axis settings according to BoneForwardAxis
	 */
	KAWAIIPHYSICS_API FSwingLimit MakeSwingLimit(const FKawaiiPhysicsPerAxisLimitAngle& Limit,
	                                             EBoneForwardAxis ForwardAxis);

	/**
	 * 倍率を掛け、範囲外の値を丸める。倍率が0なら全軸ロックになる
	 * Applies a scale and clamps out-of-range values. A scale of 0 locks every axis
	 */
	KAWAIIPHYSICS_API FKawaiiPhysicsPerAxisLimitAngle ScalePerAxisLimitAngle(
		const FKawaiiPhysicsPerAxisLimitAngle& Limit, float Scale);

	/**
	 * 親ボーンの回転を、前方軸がポーズ方向へ重なるよう補正した回転軸の組を返す。
	 * ツイスト軸の符号は結果に影響しない（骨が前方軸の負方向へ伸びるリグでも軸が反転しない）
	 * Returns the rotation axes of the parent bone after aligning its forward axis with the pose direction.
	 * The sign of the twist axis does not matter, so rigs whose bones extend along the negative forward axis keep their axes.
	 */
	KAWAIIPHYSICS_API FSwingFrame MakeSwingFrame(const FQuat& ParentPoseRotation, EBoneForwardAxis ForwardAxis,
	                                             const FVector& PoseDirection);

	/**
	 * 親ボーンの BoneForwardAxis がポーズ方向とおおむね揃っているか。ずれている場合は軸の対応が意図とずれる
	 * Whether the parent bone's BoneForwardAxis roughly follows the pose direction. When it does not, the axes will not map as intended
	 */
	KAWAIIPHYSICS_API bool IsForwardAxisAligned(const FQuat& ParentPoseRotation, EBoneForwardAxis ForwardAxis,
	                                            const FVector& PoseDirection);

	/** 現在方向を回転角 (A, B) へ分解する / Decomposes the current direction into rotation angles (A, B) */
	KAWAIIPHYSICS_API FVector2f DecomposeSwing(const FSwingFrame& Frame, const FVector& PoseDirection,
	                                           const FVector& BoneDirection);

	/** 回転角 (A, B) から方向を求める / Builds a direction from rotation angles (A, B) */
	KAWAIIPHYSICS_API FVector ComposeSwing(const FSwingFrame& Frame, const FVector& PoseDirection,
	                                       const FVector2f& Swing);

	/**
	 * 回転角を制限形状の内側へ戻す。外側なら形状上の最近傍点を返し、内側ならそのまま返す
	 * Pulls rotation angles inside the limit shape. Returns the closest point on the shape when outside, or the input when inside
	 */
	KAWAIIPHYSICS_API FVector2f ClampSwing(const FVector2f& Swing, const FSwingLimit& Limit);

	/**
	 * 制限形状の境界上の点を返す。Alpha は 0-1 で一周する
	 * Returns a point on the limit shape boundary. Alpha goes once around over 0-1
	 */
	KAWAIIPHYSICS_API FVector2f EvaluateLimitBoundary(const FSwingLimit& Limit, float Alpha);
}
