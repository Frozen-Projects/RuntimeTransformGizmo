#include "RuntimeGizmoMath.h"

namespace RuntimeGizmoMath
{
    bool IntersectPlane(const FRay& Ray, const FVector& Pivot, const FVector& Normal, FVector& Hit, double MinCosine)
    {
        const double Denominator = FVector::DotProduct(Ray.Direction, Normal);
        if (FMath::Abs(Denominator) < MinCosine)
        {
            return false;
        }
        const double Distance = FVector::DotProduct(Pivot - Ray.Origin, Normal) / Denominator;
        if (!FMath::IsFinite(Distance) || Distance < 0.0)
        {
            return false;
        }
        Hit = Ray.Origin + Distance * Ray.Direction;
        return !Hit.ContainsNaN();
    }

    double ScreenAxisDelta(const FVector2D& MouseDelta, const FVector2D& ProjectedAxis, double AxisWorldLength, double UnitsPerPixel)
    {
        const double PixelLength = ProjectedAxis.Size();
        if (PixelLength < 2.0)
        {
            // A head-on axis has no observable screen direction: dragging up means positive.
            return -MouseDelta.Y * UnitsPerPixel;
        }
        const double Gain = FMath::Min(AxisWorldLength / PixelLength, UnitsPerPixel * 8.0);
        return FVector2D::DotProduct(MouseDelta, ProjectedAxis / PixelLength) * Gain;
    }

    double AxisDelta(const FRay& Previous, const FRay& Current, const FVector& Pivot, const FVector& Axis, const FVector& ViewDirection,
        const FVector2D& MouseDelta, const FVector2D& ProjectedAxis, double AxisWorldLength, double UnitsPerPixel)
    {
        const FVector PlaneNormal = ViewDirection - Axis * FVector::DotProduct(ViewDirection, Axis);
        FVector PreviousHit, CurrentHit;
        if (PlaneNormal.SizeSquared() > 0.04 && IntersectPlane(Previous, Pivot, PlaneNormal.GetSafeNormal(), PreviousHit)
            && IntersectPlane(Current, Pivot, PlaneNormal.GetSafeNormal(), CurrentHit))
        {
            const double Delta = FVector::DotProduct(CurrentHit - PreviousHit, Axis);
            const double Limit = MouseDelta.Size() * UnitsPerPixel * 8.0;
            return FMath::Clamp(Delta, -Limit, Limit);
        }
        return ScreenAxisDelta(MouseDelta, ProjectedAxis, AxisWorldLength, UnitsPerPixel);
    }

    bool RotationDelta(const FRay& Previous, const FRay& Current, const FVector& Pivot, const FVector& Axis, double& Radians)
    {
        FVector PreviousHit, CurrentHit;
        if (!IntersectPlane(Previous, Pivot, Axis, PreviousHit, 0.15) || !IntersectPlane(Current, Pivot, Axis, CurrentHit, 0.15))
        {
            return false;
        }
        const FVector A = (PreviousHit - Pivot).GetSafeNormal();
        const FVector B = (CurrentHit - Pivot).GetSafeNormal();
        if (A.IsNearlyZero() || B.IsNearlyZero())
        {
            return false;
        }
        Radians = FMath::Atan2(FVector::DotProduct(Axis, FVector::CrossProduct(A, B)), FVector::DotProduct(A, B));
        return FMath::IsFinite(Radians);
    }

    double Snap(double Value, double Step)
    {
        return Step > UE_DOUBLE_SMALL_NUMBER ? FMath::GridSnap(Value, Step) : Value;
    }

    FVector ScaleAlongAxis(const FVector& StartScale, const FQuat& Rotation, const FVector& WorldAxis, int32 LocalAxis, double Factor, double Minimum)
    {
        FVector Result = StartScale;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            double Multiplier = 1.0;
            if (LocalAxis >= 0)
            {
                Multiplier = Index == LocalAxis ? Factor : 1.0;
            }
            else if (WorldAxis.IsNearlyZero())
            {
                Multiplier = Factor;
            }
            else
            {
                const FVector LocalBasis = Index == 0 ? Rotation.GetAxisX() : Index == 1 ? Rotation.GetAxisY() : Rotation.GetAxisZ();
                const double Alignment = FVector::DotProduct(LocalBasis, WorldAxis);
                // Keep transformed basis lengths, discarding shear that FTransform cannot represent.
                Multiplier = FMath::Sqrt(FMath::Max(0.0, 1.0 + (Factor * Factor - 1.0) * Alignment * Alignment));
            }
            Result[Index] = FMath::Sign(StartScale[Index]) * FMath::Max(Minimum, FMath::Abs(StartScale[Index]) * Multiplier);
        }
        return Result;
    }
}
