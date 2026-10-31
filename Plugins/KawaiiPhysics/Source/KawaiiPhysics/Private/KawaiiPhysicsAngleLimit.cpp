// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsAngleLimit.h"

namespace KawaiiPhysics::AngleLimit
{
	namespace
	{
		// これ以下の範囲はロック扱い。極端に細い楕円での数値不安定を避ける
		constexpr float LockedExtentDegrees = 0.01f;

		// 倍精度なら細い楕円（180°/0.02°）や軸すれすれの点でも10回以内に収束する。円は初回で終わる
		constexpr int32 MaxClosestPointIterations = 12;
		constexpr double ClosestPointTolerance = 1.0e-9;

		// 前方軸とポーズ方向の許容ずれは45°
		constexpr double MinForwardAxisAlignment = 0.70710678;

		// 反平行の付近では外積の向きが丸め誤差で決まるため、回転軸を信用しない範囲（sin）
		constexpr double AntiParallelSine = 1.0e-4;

		bool IsLockedAxis(const float Min, const float Max)
		{
			return Max <= LockedExtentDegrees && -Min <= LockedExtentDegrees;
		}

		bool IsFreeExtent(const float Extent)
		{
			return Extent >= 180.0f - LockedExtentDegrees;
		}

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
		 * 外側の点の根は正で、開始点はその下限なので分母が0に近づかない。関数は凸なので単調に根へ近づき行き過ぎない
		 * Finds the closest point on an ellipse for a point outside it in the first quadrant (Newton's method on Eberly's root function).
		 * The root of an outside point is positive and the start is a lower bound of it, so the denominators stay away from 0.
		 * The function is convex, so it approaches monotonically without overshooting.
		 */
		FVector2f FindClosestPointOnEllipse(const float InPointX, const float InPointY,
		                                    const float InExtentX, const float InExtentY)
		{
			// 単精度だと軸すれすれの点で桁落ちするため倍精度で解く
			double PointX = InPointX;
			double PointY = InPointY;
			double ExtentX = InExtentX;
			double ExtentY = InExtentY;

			const bool bSwapAxes = ExtentX < ExtentY;
			if (bSwapAxes)
			{
				Swap(PointX, PointY);
				Swap(ExtentX, ExtentY);
			}

			double ClosestX;
			double ClosestY;
			if (PointY <= 0.0)
			{
				ClosestX = ExtentX;
				ClosestY = 0.0;
			}
			else if (PointX <= 0.0)
			{
				ClosestX = 0.0;
				ClosestY = ExtentY;
			}
			else
			{
				const double NormalizedX = PointX / ExtentX;
				const double NormalizedY = PointY / ExtentY;
				const double ExtentRatioSquared = FMath::Square(ExtentX / ExtentY);

				double Root = FMath::Max3(0.0, NormalizedY - 1.0, ExtentRatioSquared * (NormalizedX - 1.0));
				for (int32 Iteration = 0; Iteration < MaxClosestPointIterations; ++Iteration)
				{
					const double DenominatorX = Root + ExtentRatioSquared;
					const double DenominatorY = Root + 1.0;
					const double TermX = ExtentRatioSquared * NormalizedX / DenominatorX;
					const double TermY = NormalizedY / DenominatorY;
					const double Value = TermX * TermX + TermY * TermY - 1.0;
					if (Value <= ClosestPointTolerance)
					{
						break;
					}

					const double Slope = -2.0 * (TermX * TermX / DenominatorX + TermY * TermY / DenominatorY);
					Root -= Value / Slope;
				}

				ClosestX = ExtentRatioSquared * PointX / (Root + ExtentRatioSquared);
				ClosestY = PointY / (Root + 1.0);

				// 残差を除いて境界上へ載せる
				const double BoundaryScale = 1.0 / FMath::Sqrt(
					FMath::Square(ClosestX / ExtentX) + FMath::Square(ClosestY / ExtentY));
				ClosestX *= BoundaryScale;
				ClosestY *= BoundaryScale;
			}

			return bSwapAxes
				       ? FVector2f(static_cast<float>(ClosestY), static_cast<float>(ClosestX))
				       : FVector2f(static_cast<float>(ClosestX), static_cast<float>(ClosestY));
		}

		/**
		 * 1軸だけ回るヒンジの制限。方向をヒンジ面へ投影して面内の角度で判定する。
		 * 回転ベクトルに分解しないため、ポーズの真逆付近でも回転軸の向きに左右されず、回転角がそのまま保たれる
		 * Limit for a hinge that rotates around a single axis. Projects the direction onto the hinge plane and works on the in-plane angle.
		 * It avoids the rotation vector, so the angle holds near the opposite of the pose, where the rotation axis is unreliable.
		 */
		bool ClampHingeDirection(const FVector& HingeAxis, const FVector& PoseDirection, const float Min, const float Max,
		                         FVector& InOutBoneDirection)
		{
			const FVector Tangent = FVector::CrossProduct(HingeAxis, PoseDirection);
			const double Angle = FMath::RadiansToDegrees(FMath::Atan2(
				FVector::DotProduct(InOutBoneDirection, Tangent),
				FVector::DotProduct(InOutBoneDirection, PoseDirection)));

			// 角度は ±180 で一周するため、範囲外なら円周上で近いほうの端へ戻す（-180 と +180 は同じ向き）
			double ClampedAngle = Angle;
			if (Angle < Min || Angle > Max)
			{
				const double ToMin = FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, static_cast<double>(Min)));
				const double ToMax = FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, static_cast<double>(Max)));
				ClampedAngle = ToMin < ToMax ? Min : Max;
			}
			const double ClampedRadians = FMath::DegreesToRadians(ClampedAngle);

			const FVector Clamped = PoseDirection * FMath::Cos(ClampedRadians) + Tangent * FMath::Sin(ClampedRadians);
			if (Clamped.Equals(InOutBoneDirection, UE_SMALL_NUMBER))
			{
				return false;
			}

			InOutBoneDirection = Clamped;
			return true;
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

	FVector2f DecomposeSwing(const FSwingFrame& Frame, const FVector& PoseDirection, const FVector& BoneDirection,
	                         const FSwingLimit& Limit)
	{
		const FVector Cross = FVector::CrossProduct(PoseDirection, BoneDirection);
		const double SinAngle = Cross.Size();
		const double CosAngle = FVector::DotProduct(PoseDirection, BoneDirection);

		if (CosAngle >= 0.0 && SinAngle <= UE_SMALL_NUMBER)
		{
			return FVector2f::ZeroVector;
		}

		const double AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(SinAngle, CosAngle));

		if (CosAngle < 0.0 && SinAngle <= AntiParallelSine)
		{
			// 真逆へはどの回転軸でも届くため、制限で最も広く動ける向きとして扱う
			const float Angle = static_cast<float>(AngleDegrees);
			const FVector2f Candidates[] = {
				{Angle, 0.0f}, {-Angle, 0.0f}, {0.0f, Angle}, {0.0f, -Angle}
			};
			const float Extents[] = {Limit.MaxA, -Limit.MinA, Limit.MaxB, -Limit.MinB};

			int32 WidestIndex = 0;
			for (int32 Index = 1; Index < UE_ARRAY_COUNT(Extents); ++Index)
			{
				if (Extents[Index] > Extents[WidestIndex])
				{
					WidestIndex = Index;
				}
			}
			return Candidates[WidestIndex];
		}

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

	bool ClampDirection(const FSwingFrame& Frame, const FVector& PoseDirection, const FSwingLimit& Limit,
	                    FVector& InOutBoneDirection)
	{
		const bool bLockedA = IsLockedAxis(Limit.MinA, Limit.MaxA);
		const bool bLockedB = IsLockedAxis(Limit.MinB, Limit.MaxB);
		if (bLockedA != bLockedB)
		{
			return bLockedA
				       ? ClampHingeDirection(Frame.AxisB, PoseDirection, Limit.MinB, Limit.MaxB, InOutBoneDirection)
				       : ClampHingeDirection(Frame.AxisA, PoseDirection, Limit.MinA, Limit.MaxA, InOutBoneDirection);
		}

		const FVector2f Swing = DecomposeSwing(Frame, PoseDirection, InOutBoneDirection, Limit);
		const FVector2f ClampedSwing = ClampSwing(Swing, Limit);
		if (ClampedSwing == Swing)
		{
			return false;
		}

		InOutBoneDirection = ComposeSwing(Frame, PoseDirection, ClampedSwing);
		return true;
	}

	bool IsUnlimited(const FSwingLimit& Limit)
	{
		return IsFreeExtent(Limit.MaxA) && IsFreeExtent(-Limit.MinA) &&
			IsFreeExtent(Limit.MaxB) && IsFreeExtent(-Limit.MinB);
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
