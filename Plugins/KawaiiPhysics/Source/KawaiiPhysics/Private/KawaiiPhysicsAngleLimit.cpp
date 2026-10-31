// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsAngleLimit.h"

namespace KawaiiPhysics::AngleLimit
{
	namespace
	{
		// これ以下の範囲はロック扱い。極端に細い楕円での数値不安定を避ける
		constexpr float LockedExtentDegrees = 0.01f;

		// 12回あれば細い楕円（180°/0.1°）でも単精度で収束する。円は初回で終わる
		constexpr int32 MaxClosestPointIterations = 12;
		constexpr float ClosestPointTolerance = 1.0e-6f;

		// 前方軸とポーズ方向の許容ずれは45°
		constexpr double MinForwardAxisAlignment = 0.70710678;

		FVector GetRotationAxis(const FQuat& Rotation, const EAxis::Type Axis)
		{
			switch (Axis)
			{
			case EAxis::Y:
				return Rotation.GetAxisY();
			case EAxis::Z:
				return Rotation.GetAxisZ();
			default:
				return Rotation.GetAxisX();
			}
		}

		const FKawaiiPhysicsAxisAngleRange& GetAxisRange(const FKawaiiPhysicsPerAxisLimitAngle& Limit,
		                                                 const EAxis::Type Axis)
		{
			switch (Axis)
			{
			case EAxis::Y:
				return Limit.Y;
			case EAxis::Z:
				return Limit.Z;
			default:
				return Limit.X;
			}
		}

		FKawaiiPhysicsAxisAngleRange ScaleAxisRange(const FKawaiiPhysicsAxisAngleRange& Range, const float Scale)
		{
			FKawaiiPhysicsAxisAngleRange Scaled;
			Scaled.Min = FMath::Clamp(Range.Min * Scale, -180.0f, 0.0f);
			Scaled.Max = FMath::Clamp(Range.Max * Scale, 0.0f, 180.0f);
			return Scaled;
		}

		/**
		 * 第1象限の楕円外の点から楕円上の最近傍点を求める（Eberly の根関数を Newton 法で解く）。
		 * 開始点は根の左側で関数は凸なので、単調に根へ近づき行き過ぎない
		 * Finds the closest point on an ellipse for a point outside it in the first quadrant (Newton's method on Eberly's root function).
		 * The start is left of the root and the function is convex, so it approaches monotonically without overshooting.
		 */
		FVector2f FindClosestPointOnEllipse(float PointX, float PointY, float ExtentX, float ExtentY)
		{
			const bool bSwapAxes = ExtentX < ExtentY;
			if (bSwapAxes)
			{
				Swap(PointX, PointY);
				Swap(ExtentX, ExtentY);
			}

			float ClosestX;
			float ClosestY;
			if (PointY <= 0.0f)
			{
				ClosestX = ExtentX;
				ClosestY = 0.0f;
			}
			else if (PointX <= 0.0f)
			{
				ClosestX = 0.0f;
				ClosestY = ExtentY;
			}
			else
			{
				const float NormalizedX = PointX / ExtentX;
				const float NormalizedY = PointY / ExtentY;
				const float ExtentRatioSquared = FMath::Square(ExtentX / ExtentY);

				float Root = NormalizedY - 1.0f;
				for (int32 Iteration = 0; Iteration < MaxClosestPointIterations; ++Iteration)
				{
					const float DenominatorX = Root + ExtentRatioSquared;
					const float DenominatorY = Root + 1.0f;
					const float TermX = ExtentRatioSquared * NormalizedX / DenominatorX;
					const float TermY = NormalizedY / DenominatorY;
					const float Value = TermX * TermX + TermY * TermY - 1.0f;
					if (Value <= ClosestPointTolerance)
					{
						break;
					}

					const float Slope = -2.0f * (TermX * TermX / DenominatorX + TermY * TermY / DenominatorY);
					Root -= Value / Slope;
				}

				ClosestX = ExtentRatioSquared * PointX / (Root + ExtentRatioSquared);
				ClosestY = PointY / (Root + 1.0f);

				// 残差を除いて境界上へ載せる
				const float BoundaryScale = 1.0f / FMath::Sqrt(
					FMath::Square(ClosestX / ExtentX) + FMath::Square(ClosestY / ExtentY));
				ClosestX *= BoundaryScale;
				ClosestY *= BoundaryScale;
			}

			return bSwapAxes ? FVector2f(ClosestY, ClosestX) : FVector2f(ClosestX, ClosestY);
		}
	}

	EAxis::Type GetTwistAxis(const EBoneForwardAxis ForwardAxis)
	{
		switch (ForwardAxis)
		{
		case EBoneForwardAxis::Y_Positive:
		case EBoneForwardAxis::Y_Negative:
			return EAxis::Y;
		case EBoneForwardAxis::Z_Positive:
		case EBoneForwardAxis::Z_Negative:
			return EAxis::Z;
		default:
			return EAxis::X;
		}
	}

	void GetSwingAxes(const EBoneForwardAxis ForwardAxis, EAxis::Type& OutAxisA, EAxis::Type& OutAxisB)
	{
		switch (GetTwistAxis(ForwardAxis))
		{
		case EAxis::Y:
			OutAxisA = EAxis::X;
			OutAxisB = EAxis::Z;
			break;
		case EAxis::Z:
			OutAxisA = EAxis::X;
			OutAxisB = EAxis::Y;
			break;
		default:
			OutAxisA = EAxis::Y;
			OutAxisB = EAxis::Z;
			break;
		}
	}

	FSwingLimit MakeSwingLimit(const FKawaiiPhysicsPerAxisLimitAngle& Limit, const EBoneForwardAxis ForwardAxis)
	{
		EAxis::Type AxisA;
		EAxis::Type AxisB;
		GetSwingAxes(ForwardAxis, AxisA, AxisB);

		const FKawaiiPhysicsAxisAngleRange& RangeA = GetAxisRange(Limit, AxisA);
		const FKawaiiPhysicsAxisAngleRange& RangeB = GetAxisRange(Limit, AxisB);

		FSwingLimit SwingLimit;
		SwingLimit.MinA = RangeA.Min;
		SwingLimit.MaxA = RangeA.Max;
		SwingLimit.MinB = RangeB.Min;
		SwingLimit.MaxB = RangeB.Max;
		return SwingLimit;
	}

	FKawaiiPhysicsPerAxisLimitAngle ScalePerAxisLimitAngle(const FKawaiiPhysicsPerAxisLimitAngle& Limit,
	                                                       const float Scale)
	{
		const float ClampedScale = FMath::Max(Scale, 0.0f);

		FKawaiiPhysicsPerAxisLimitAngle Scaled;
		Scaled.X = ScaleAxisRange(Limit.X, ClampedScale);
		Scaled.Y = ScaleAxisRange(Limit.Y, ClampedScale);
		Scaled.Z = ScaleAxisRange(Limit.Z, ClampedScale);
		return Scaled;
	}

	FSwingFrame MakeSwingFrame(const FQuat& ParentPoseRotation, const EBoneForwardAxis ForwardAxis,
	                           const FVector& PoseDirection)
	{
		FVector Forward = GetRotationAxis(ParentPoseRotation, GetTwistAxis(ForwardAxis));
		if (FVector::DotProduct(Forward, PoseDirection) < 0.0)
		{
			// 反平行だと FindBetween の回転軸が定まらないため、近い側の向きを使う
			Forward = -Forward;
		}

		const FQuat Alignment = FQuat::FindBetweenNormals(Forward, PoseDirection);

		EAxis::Type AxisA;
		EAxis::Type AxisB;
		GetSwingAxes(ForwardAxis, AxisA, AxisB);

		FSwingFrame Frame;
		Frame.AxisA = Alignment.RotateVector(GetRotationAxis(ParentPoseRotation, AxisA));
		Frame.AxisB = Alignment.RotateVector(GetRotationAxis(ParentPoseRotation, AxisB));
		return Frame;
	}

	bool IsForwardAxisAligned(const FQuat& ParentPoseRotation, const EBoneForwardAxis ForwardAxis,
	                          const FVector& PoseDirection)
	{
		const FVector Forward = GetRotationAxis(ParentPoseRotation, GetTwistAxis(ForwardAxis));
		return FMath::Abs(FVector::DotProduct(Forward, PoseDirection)) >= MinForwardAxisAlignment;
	}

	FVector2f DecomposeSwing(const FSwingFrame& Frame, const FVector& PoseDirection, const FVector& BoneDirection)
	{
		const FVector Cross = FVector::CrossProduct(PoseDirection, BoneDirection);
		const double SinAngle = Cross.Size();
		const double CosAngle = FVector::DotProduct(PoseDirection, BoneDirection);

		if (SinAngle <= UE_SMALL_NUMBER)
		{
			// 反平行は回転軸が定まらないため AxisA まわりの180°として扱う
			return CosAngle >= 0.0 ? FVector2f::ZeroVector : FVector2f(180.0f, 0.0f);
		}

		const double AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(SinAngle, CosAngle));
		const FVector RotationVector = Cross / SinAngle * AngleDegrees;
		return FVector2f(static_cast<float>(FVector::DotProduct(RotationVector, Frame.AxisA)),
		                 static_cast<float>(FVector::DotProduct(RotationVector, Frame.AxisB)));
	}

	FVector ComposeSwing(const FSwingFrame& Frame, const FVector& PoseDirection, const FVector2f& Swing)
	{
		const FVector RotationVector = Frame.AxisA * Swing.X + Frame.AxisB * Swing.Y;
		const double AngleDegrees = RotationVector.Size();
		if (AngleDegrees <= UE_SMALL_NUMBER)
		{
			return PoseDirection;
		}

		return PoseDirection.RotateAngleAxis(AngleDegrees, RotationVector / AngleDegrees);
	}

	FVector2f ClampSwing(const FVector2f& Swing, const FSwingLimit& Limit)
	{
		// 象限ごとに使う範囲が決まる。軸上で滑らかにつながるため象限をまたいでも飛ばない
		const float ExtentA = Swing.X >= 0.0f ? Limit.MaxA : -Limit.MinA;
		const float ExtentB = Swing.Y >= 0.0f ? Limit.MaxB : -Limit.MinB;
		const float AbsA = FMath::Abs(Swing.X);
		const float AbsB = FMath::Abs(Swing.Y);

		FVector2f Clamped;
		if (ExtentA <= LockedExtentDegrees)
		{
			Clamped = FVector2f(0.0f, FMath::Min(AbsB, ExtentB));
		}
		else if (ExtentB <= LockedExtentDegrees)
		{
			Clamped = FVector2f(FMath::Min(AbsA, ExtentA), 0.0f);
		}
		else
		{
			if (FMath::Square(AbsA / ExtentA) + FMath::Square(AbsB / ExtentB) <= 1.0f)
			{
				return Swing;
			}

			// 中心へ縮めると細い楕円で大きく引き戻されるため、最近傍点を使う
			Clamped = FindClosestPointOnEllipse(AbsA, AbsB, ExtentA, ExtentB);
		}

		return FVector2f(FMath::Sign(Swing.X) * Clamped.X, FMath::Sign(Swing.Y) * Clamped.Y);
	}

	FVector2f EvaluateLimitBoundary(const FSwingLimit& Limit, const float Alpha)
	{
		const float Angle = Alpha * UE_TWO_PI;
		const float Cos = FMath::Cos(Angle);
		const float Sin = FMath::Sin(Angle);
		const float ExtentA = Cos >= 0.0f ? Limit.MaxA : -Limit.MinA;
		const float ExtentB = Sin >= 0.0f ? Limit.MaxB : -Limit.MinB;
		return FVector2f(ExtentA * Cos, ExtentB * Sin);
	}
}
