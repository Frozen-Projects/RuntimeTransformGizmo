#pragma once

#include "CoreMinimal.h"
#include "RuntimeGizmoTypes.generated.h"

UENUM(BlueprintType)
enum class ERuntimeGizmoMode : uint8
{
    Translate,
    Rotate,
    Scale
};

UENUM(BlueprintType)
enum class ERuntimeGizmoSpace : uint8
{
    World,
    Local
};

UENUM(BlueprintType)
enum class ERuntimeGizmoAxis : uint8
{
    None,
    X,
    Y,
    Z,
    Uniform
};
