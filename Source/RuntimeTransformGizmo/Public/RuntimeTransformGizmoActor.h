#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

#include "RuntimeGizmoMath.h"
#include "RuntimeGizmoTypes.h"
#include "RuntimeGizmoSubsystem.h"
#include "RuntimeGizmoHandleComponent.h"

#include "RuntimeTransformGizmoActor.generated.h"

class APlayerController;
class URuntimeGizmoHandleComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRuntimeGizmoBegin, USceneComponent*, Target, FTransform, InitialTransform);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRuntimeGizmoChanged, USceneComponent*, Target, FTransform, Transform);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FRuntimeGizmoEnd, USceneComponent*, Target, FTransform, InitialTransform, FTransform, FinalTransform, bool, bCancelled);

UCLASS(Blueprintable)
class RUNTIMETRANSFORMGIZMO_API ARuntimeTransformGizmoActor : public AActor
{
    GENERATED_BODY()

public:
    ARuntimeTransformGizmoActor();
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    bool SetTargetComponent(USceneComponent* NewTarget);

    UFUNCTION(BlueprintPure, Category = "Frozen Forest | Runtime Gizmo")
    USceneComponent* GetTargetComponent() const { return TargetComponent.Get(); }

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    void SetPlayerController(APlayerController* NewController);

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    void SetMode(ERuntimeGizmoMode NewMode);

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    void SetCoordinateSpace(ERuntimeGizmoSpace NewSpace);

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    void SetInteractionEnabled(bool bEnabled);

    UFUNCTION(BlueprintPure, Category = "Frozen Forest | Runtime Gizmo")
    bool IsDragging() const;

    UFUNCTION(BlueprintPure, Category = "Frozen Forest | Runtime Gizmo")
    ERuntimeGizmoAxis GetHoveredAxis() const;

    UFUNCTION(BlueprintPure, Category = "Frozen Forest | Runtime Gizmo")
    URuntimeGizmoHandleComponent* GetHandleComponent(ERuntimeGizmoMode HandleMode, ERuntimeGizmoAxis Axis) const;

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    bool PointerDown(FVector2D ScreenPosition);

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    bool BeginAxisDrag(ERuntimeGizmoAxis Axis, FVector2D ScreenPosition);

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    void PointerMove(FVector2D ScreenPosition);

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    void PointerUp();

    UFUNCTION(BlueprintCallable, Category = "Frozen Forest | Runtime Gizmo")
    void CancelDrag();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frozen Forest | Runtime Gizmo")
    ERuntimeGizmoMode Mode = ERuntimeGizmoMode::Translate;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frozen Forest | Runtime Gizmo")
    ERuntimeGizmoSpace CoordinateSpace = ERuntimeGizmoSpace::World;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Input")
    bool bAutoHandleMouse = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frozen Forest | Runtime Gizmo|Input")
    bool bInteractionEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Appearance", meta = (ClampMin = "32.0"))
    float SizePixels = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Appearance")
    FLinearColor HighlightColor = FLinearColor(1.0f, 0.8f, 0.05f);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Frozen Forest | Runtime Gizmo|Appearance")
    TArray<TObjectPtr<URuntimeGizmoHandleComponent>> Handles;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Snapping", meta = (ClampMin = "0.0"))
    double TranslationSnap = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Snapping", meta = (ClampMin = "0.0"))
    double RotationSnapDegrees = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Snapping", meta = (ClampMin = "0.0"))
    double ScaleSnap = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Scale", meta = (ClampMin = "0.0001"))
    double MinimumScale = 0.001;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frozen Forest | Runtime Gizmo|Scale", meta = (ClampMin = "16.0"))
    double ScalePixelsPerDoubling = 120.0;

    UPROPERTY(BlueprintReadOnly, Category = "Frozen Forest | Runtime Gizmo")
    FString LastError;

    UPROPERTY(BlueprintAssignable, Category = "Frozen Forest | Runtime Gizmo|Events")
    FRuntimeGizmoBegin OnDragStarted;

    UPROPERTY(BlueprintAssignable, Category = "Frozen Forest | Runtime Gizmo|Events")
    FRuntimeGizmoChanged OnTransformChanged;

    UPROPERTY(BlueprintAssignable, Category = "Frozen Forest | Runtime Gizmo|Events")
    FRuntimeGizmoEnd OnDragFinished;

private:

    UPROPERTY(Transient)
    TWeakObjectPtr<USceneComponent> TargetComponent;

    UPROPERTY(Transient)
    TWeakObjectPtr<APlayerController> Controller;

    UPROPERTY(Transient)
    TObjectPtr<URuntimeGizmoHandleComponent> HoveredHandle;

    UPROPERTY(Transient)
    TObjectPtr<URuntimeGizmoHandleComponent> ActiveHandle;

    bool bDragging = false;
    bool bViewValid = false;
    FTransform InitialTransform;
    FQuat DragBasis = FQuat::Identity;
    FVector DragAxis = FVector::ForwardVector;
    FVector DragRadial = FVector::RightVector;
    FVector2D PreviousMouse = FVector2D::ZeroVector;
    FVector ViewDirection = FVector::ForwardVector;
    double UnitsPerPixel = 1.0;
    double AccumulatedDelta = 0.0;

    bool ValidateTarget(USceneComponent* Candidate);
    bool MakeRay(const FVector2D& ScreenPosition, RuntimeGizmoMath::FRay& Ray) const;
    bool Project(const FVector& Location, FVector2D& ScreenPosition) const;
    void UpdatePlacement();
    void UpdateAppearance();
    void FinishDrag(bool bCancelled);
    bool StartDrag(URuntimeGizmoHandleComponent* Handle, const FVector2D& ScreenPosition, const FVector& PickPosition);
    URuntimeGizmoHandleComponent* PickHandle(const FVector2D& ScreenPosition, FVector& PickWorldPosition) const;
};
