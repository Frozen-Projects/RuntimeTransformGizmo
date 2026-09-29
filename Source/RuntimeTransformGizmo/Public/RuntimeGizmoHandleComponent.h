#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "RuntimeGizmoTypes.h"
#include "RuntimeGizmoHandleComponent.generated.h"

UCLASS(ClassGroup = (RuntimeGizmo), meta = (BlueprintSpawnableComponent))
class RUNTIMETRANSFORMGIZMO_API URuntimeGizmoHandleComponent : public UStaticMeshComponent
{
    GENERATED_BODY()

public:
    URuntimeGizmoHandleComponent();
    FBox GetInteractionBounds() const;
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual void SendRenderDynamicData_Concurrent() override;
    void SetOverlayAppearance(FLinearColor Color, int32 PlayerIndex);
    FLinearColor GetOverlayColor() const { return OverlayColor; }
    int32 GetOverlayPlayerIndex() const { return OverlayPlayerIndex; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
    ERuntimeGizmoMode Mode = ERuntimeGizmoMode::Translate;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
    ERuntimeGizmoAxis Axis = ERuntimeGizmoAxis::X;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo", meta = (ClampMin = "1.0"))
    float PickRadiusPixels = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
    bool bHandleEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
    FLinearColor AxisColor = FLinearColor::Red;

private:
    FLinearColor OverlayColor = FLinearColor::Red;
    int32 OverlayPlayerIndex = 0;
};
