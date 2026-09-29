#pragma once

#include "CoreMinimal.h"

namespace RuntimeGizmoMath
{
    struct FRay
    {
        FVector Origin = FVector::ZeroVector;
        FVector Direction = FVector::ForwardVector;
    };

    RUNTIMETRANSFORMGIZMO_API bool IntersectPlane(const FRay& Ray, const FVector& Pivot, const FVector& Normal, FVector& Hit, double MinCosine = 0.05);
    RUNTIMETRANSFORMGIZMO_API double ScreenAxisDelta(const FVector2D& MouseDelta, const FVector2D& ProjectedAxis, double AxisWorldLength, double UnitsPerPixel);
    RUNTIMETRANSFORMGIZMO_API double AxisDelta(const FRay& Previous, const FRay& Current, const FVector& Pivot, const FVector& Axis, const FVector& ViewDirection,
        const FVector2D& MouseDelta, const FVector2D& ProjectedAxis, double AxisWorldLength, double UnitsPerPixel);
    RUNTIMETRANSFORMGIZMO_API bool RotationDelta(const FRay& Previous, const FRay& Current, const FVector& Pivot, const FVector& Axis, double& Radians);
    RUNTIMETRANSFORMGIZMO_API double Snap(double Value, double Step);
    RUNTIMETRANSFORMGIZMO_API FVector ScaleAlongAxis(const FVector& StartScale, const FQuat& Rotation, const FVector& WorldAxis, int32 LocalAxis, double Factor, double Minimum);
}
