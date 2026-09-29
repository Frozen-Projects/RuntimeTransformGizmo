#include "RuntimeTransformGizmoActor.h"
#include "RuntimeGizmoHandleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"
#include "SceneView.h"

namespace
{
    FVector AxisVector(ERuntimeGizmoAxis Axis)
    {
        return Axis == ERuntimeGizmoAxis::X ? FVector::ForwardVector : Axis == ERuntimeGizmoAxis::Y ? FVector::RightVector : FVector::UpVector;
    }

    double SegmentDistance(const FVector2D& Point, const FVector2D& A, const FVector2D& B, double& Along)
    {
        const FVector2D Segment = B - A;
        Along = Segment.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, Segment) / Segment.SizeSquared(), 0.0, 1.0) : 0.0;
        return (Point - (A + Along * Segment)).Size();
    }
}

ARuntimeTransformGizmoActor::ARuntimeTransformGizmoActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SetReplicates(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("GizmoRoot"));
    RootComponent->SetMobility(EComponentMobility::Movable);
    RootComponent->SetAbsolute(true, true, true);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Arrow(TEXT("/Engine/InteractiveToolsFramework/Meshes/GizmoArrowHandle.GizmoArrowHandle"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Ring(TEXT("/Engine/InteractiveToolsFramework/Meshes/GizmoFullCircleHandle.GizmoFullCircleHandle"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ScaleArrow(TEXT("/RuntimeTransformGizmo/Meshes/SM_ScaleHandle.SM_ScaleHandle"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));

    for (int32 ModeIndex = 0; ModeIndex < 3; ++ModeIndex)
    {
        for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
        {
            const FName Name(*FString::Printf(TEXT("%s_%s"), ModeIndex == 0 ? TEXT("Move") : ModeIndex == 1 ? TEXT("Rotate") : TEXT("Scale"), AxisIndex == 0 ? TEXT("X") : AxisIndex == 1 ? TEXT("Y") : TEXT("Z")));
            URuntimeGizmoHandleComponent* Handle = CreateDefaultSubobject<URuntimeGizmoHandleComponent>(Name);
            Handle->SetupAttachment(RootComponent);
            Handle->Mode = static_cast<ERuntimeGizmoMode>(ModeIndex);
            Handle->Axis = static_cast<ERuntimeGizmoAxis>(AxisIndex + 1);
            Handle->AxisColor = AxisIndex == 0 ? FLinearColor(0.95f, 0.08f, 0.06f) : AxisIndex == 1 ? FLinearColor(0.12f, 0.85f, 0.08f) : FLinearColor(0.08f, 0.3f, 1.0f);
            UStaticMesh* Mesh = ModeIndex == 0 ? Arrow.Object : ModeIndex == 1 ? Ring.Object : ScaleArrow.Object;
            Handle->SetStaticMesh(Mesh);
            Handle->SetRelativeRotation(FQuat::FindBetweenNormals(FVector::ForwardVector, AxisVector(Handle->Axis)));
            if (Mesh)
            {
                const FBox Box = Handle->GetInteractionBounds();
                const double Extent = ModeIndex == 1 ? FMath::Max(Box.GetExtent().Y, Box.GetExtent().Z) : Box.Max.X;
                Handle->SetRelativeScale3D(FVector((ModeIndex == 1 ? 80.0 : 100.0) / FMath::Max(Extent, 1.0)));
            }
            Handles.Add(Handle);
        }
    }

    URuntimeGizmoHandleComponent* Uniform = CreateDefaultSubobject<URuntimeGizmoHandleComponent>(TEXT("Scale_Uniform"));
    Uniform->SetupAttachment(RootComponent);
    Uniform->Mode = ERuntimeGizmoMode::Scale;
    Uniform->Axis = ERuntimeGizmoAxis::Uniform;
    Uniform->AxisColor = FLinearColor::White;
    Uniform->SetStaticMesh(Cube.Object);
    Uniform->SetRelativeScale3D(FVector(0.12));
    Handles.Add(Uniform);
}

void ARuntimeTransformGizmoActor::BeginPlay()
{
    Super::BeginPlay();
    if (!Controller.IsValid())
    {
        SetPlayerController(UGameplayStatics::GetPlayerController(this, 0));
    }
    UpdatePlacement();
}

void ARuntimeTransformGizmoActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelDrag();
    Super::EndPlay(EndPlayReason);
}

bool ARuntimeTransformGizmoActor::ValidateTarget(USceneComponent* Candidate)
{
    LastError.Reset();
    if (!IsValid(Candidate) || Candidate->GetOwner() == this || Candidate->GetWorld() != GetWorld() || !Candidate->IsRegistered())
    {
        LastError = TEXT("Choose a registered scene component on another actor in this world.");
        return false;
    }
    if (Candidate->Mobility != EComponentMobility::Movable)
    {
        LastError = TEXT("The target component must be Movable.");
        return false;
    }
    TArray<USceneComponent*> Descendants;
    Candidate->GetChildrenComponents(true, Descendants);
    Descendants.Add(Candidate);
    for (const USceneComponent* Component : Descendants)
    {
        if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); Primitive && Primitive->IsSimulatingPhysics())
        {
            LastError = TEXT("Disable physics simulation on the target before manipulating it.");
            return false;
        }
    }
    if (Candidate->GetComponentTransform().ContainsNaN() || Candidate->GetComponentScale().GetAbsMin() < UE_DOUBLE_SMALL_NUMBER)
    {
        LastError = TEXT("The target transform must be finite with nonzero scale on every axis.");
        return false;
    }
    for (const USceneComponent* Parent = Candidate->GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
    {
        const FVector Scale = Parent->GetComponentScale();
        if (Scale.GetMin() <= 0.0 || !FMath::IsNearlyEqual(Scale.X, Scale.Y, 0.0001) || !FMath::IsNearlyEqual(Scale.X, Scale.Z, 0.0001))
        {
            LastError = TEXT("Attached targets require positive uniform scale on all ancestors; detach with Keep World Transform first.");
            return false;
        }
    }
    return true;
}

bool ARuntimeTransformGizmoActor::SetTargetComponent(USceneComponent* NewTarget)
{
    if (NewTarget && !ValidateTarget(NewTarget))
    {
        return false;
    }
    CancelDrag();
    TargetComponent = NewTarget;
    HoveredHandle = nullptr;
    LastError.Reset();
    UpdatePlacement();
    return true;
}

void ARuntimeTransformGizmoActor::SetPlayerController(APlayerController* NewController)
{
    CancelDrag();
    if (Controller.IsValid())
    {
        RemoveTickPrerequisiteActor(Controller.Get());
    }
    Controller = NewController;
    if (Controller.IsValid())
    {
        AddTickPrerequisiteActor(Controller.Get());
    }
}

void ARuntimeTransformGizmoActor::SetMode(ERuntimeGizmoMode NewMode)
{
    if (Mode != NewMode)
    {
        CancelDrag();
        Mode = NewMode;
        HoveredHandle = nullptr;
        UpdatePlacement();
    }
}

void ARuntimeTransformGizmoActor::SetCoordinateSpace(ERuntimeGizmoSpace NewSpace)
{
    if (CoordinateSpace != NewSpace)
    {
        CancelDrag();
        CoordinateSpace = NewSpace;
        UpdatePlacement();
    }
}

void ARuntimeTransformGizmoActor::SetInteractionEnabled(bool bEnabled)
{
    if (!bEnabled)
    {
        CancelDrag();
    }
    bInteractionEnabled = bEnabled;
    UpdatePlacement();
}

ERuntimeGizmoAxis ARuntimeTransformGizmoActor::GetHoveredAxis() const
{
    return HoveredHandle ? HoveredHandle->Axis : ERuntimeGizmoAxis::None;
}

URuntimeGizmoHandleComponent* ARuntimeTransformGizmoActor::GetHandleComponent(ERuntimeGizmoMode HandleMode, ERuntimeGizmoAxis Axis) const
{
    for (URuntimeGizmoHandleComponent* Handle : Handles)
    {
        if (Handle && Handle->Mode == HandleMode && Handle->Axis == Axis)
        {
            return Handle;
        }
    }
    return nullptr;
}

bool ARuntimeTransformGizmoActor::Project(const FVector& Location, FVector2D& ScreenPosition) const
{
    return Controller.IsValid() && Controller->ProjectWorldLocationToScreen(Location, ScreenPosition, false) && !ScreenPosition.ContainsNaN();
}

bool ARuntimeTransformGizmoActor::MakeRay(const FVector2D& ScreenPosition, RuntimeGizmoMath::FRay& Ray) const
{
    ULocalPlayer* Player = Controller.IsValid() ? Controller->GetLocalPlayer() : nullptr;
    FSceneViewProjectionData Projection;
    if (!Player || !Player->ViewportClient || !Player->GetProjectionData(Player->ViewportClient->Viewport, Projection))
    {
        return false;
    }
    const FIntRect Rect = Projection.GetConstrainedViewRect();
    if (Rect.Width() <= 0 || Rect.Height() <= 0)
    {
        return false;
    }
    // Preserve subpixel input; the engine's convenience deprojection truncates to integer pixels.
    const double X = 2.0 * (ScreenPosition.X - Rect.Min.X) / Rect.Width() - 1.0;
    const double Y = 1.0 - 2.0 * (ScreenPosition.Y - Rect.Min.Y) / Rect.Height();
    const FMatrix InverseProjection = Projection.ProjectionMatrix.Inverse();
    const FMatrix InverseRotation = Projection.ViewRotationMatrix.Inverse();
    const FVector4 Near = InverseProjection.TransformFVector4(FVector4(X, Y, 1.0, 1.0));
    const FVector4 Far = InverseProjection.TransformFVector4(FVector4(X, Y, 0.01, 1.0));
    if (FMath::Abs(Near.W) < UE_DOUBLE_SMALL_NUMBER || FMath::Abs(Far.W) < UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }
    const FVector Start = FVector(Near.X, Near.Y, Near.Z) / Near.W;
    const FVector End = FVector(Far.X, Far.Y, Far.Z) / Far.W;
    Ray.Origin = Projection.ViewOrigin + InverseRotation.TransformVector(Start);
    Ray.Direction = InverseRotation.TransformVector(End - Start).GetSafeNormal();
    return !Ray.Origin.ContainsNaN() && !Ray.Direction.ContainsNaN() && !Ray.Direction.IsNearlyZero();
}

void ARuntimeTransformGizmoActor::UpdatePlacement()
{
    bViewValid = false;
    if (TargetComponent.IsValid() && TargetComponent->IsRegistered() && Controller.IsValid() && Controller->IsLocalController() && bInteractionEnabled)
    {
        FVector CameraLocation;
        FRotator CameraRotation;
        Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
        ViewDirection = CameraRotation.Vector();
        const FVector Pivot = TargetComponent->GetComponentLocation();
        const double ProbeLength = FMath::Max((Pivot - CameraLocation).Size() * 0.01, 1.0);
        FVector2D ScreenPivot, ScreenRight;
        if (Project(Pivot, ScreenPivot) && Project(Pivot + CameraRotation.Quaternion().GetAxisY() * ProbeLength, ScreenRight))
        {
            const double PixelLength = (ScreenRight - ScreenPivot).Size();
            if (PixelLength > UE_DOUBLE_SMALL_NUMBER)
            {
                UnitsPerPixel = ProbeLength / PixelLength;
                const FQuat Basis = bDragging ? DragBasis : CoordinateSpace == ERuntimeGizmoSpace::Local ? TargetComponent->GetComponentQuat() : FQuat::Identity;
                SetActorTransform(FTransform(Basis, Pivot, FVector(UnitsPerPixel * FMath::Max(SizePixels, 32.0f) / 100.0)));
                bViewValid = true;
            }
        }
    }
    UpdateAppearance();
}

void ARuntimeTransformGizmoActor::UpdateAppearance()
{
    for (int32 Index = 0; Index < Handles.Num(); ++Index)
    {
        URuntimeGizmoHandleComponent* Handle = Handles[Index];
        if (!Handle)
        {
            continue;
        }
        Handle->SetVisibility(bViewValid && Handle->bHandleEnabled && Handle->Mode == Mode);
        const ULocalPlayer* Player = Controller.IsValid() ? Controller->GetLocalPlayer() : nullptr;
        Handle->SetOverlayAppearance(Handle == ActiveHandle || (!bDragging && Handle == HoveredHandle) ? HighlightColor : Handle->AxisColor, Player ? Player->GetControllerId() : 0);
    }
}

URuntimeGizmoHandleComponent* ARuntimeTransformGizmoActor::PickHandle(const FVector2D& ScreenPosition, FVector& PickWorldPosition) const
{
    URuntimeGizmoHandleComponent* Best = nullptr;
    double BestDistance = TNumericLimits<double>::Max();
    double BestDepth = TNumericLimits<double>::Max();
    FVector CameraLocation;
    FRotator CameraRotation;
    if (!bViewValid || !Controller.IsValid())
    {
        return nullptr;
    }
    Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
    auto ConsiderSegment = [&](URuntimeGizmoHandleComponent* Handle, const FVector& A, const FVector& B)
    {
        FVector2D ScreenA, ScreenB;
        if (!Project(A, ScreenA) || !Project(B, ScreenB))
        {
            return;
        }
        double Along;
        const double Distance = SegmentDistance(ScreenPosition, ScreenA, ScreenB, Along);
        const FVector WorldPoint = FMath::Lerp(A, B, Along);
        const double Depth = FVector::DotProduct(WorldPoint - CameraLocation, CameraRotation.Vector());
        if (Distance <= FMath::Max(Handle->PickRadiusPixels, 1.0f) && (Distance < BestDistance - 0.5 || (FMath::Abs(Distance - BestDistance) <= 0.5 && Depth < BestDepth)))
        {
            Best = Handle;
            BestDistance = Distance;
            BestDepth = Depth;
            PickWorldPosition = WorldPoint;
        }
    };

    for (URuntimeGizmoHandleComponent* Handle : Handles)
    {
        if (!Handle || !Handle->bHandleEnabled || Handle->Mode != Mode || !Handle->GetStaticMesh())
        {
            continue;
        }
        const FTransform Transform = Handle->GetComponentTransform();
        const FBox Box = Handle->GetInteractionBounds();
        if (Handle->Axis == ERuntimeGizmoAxis::Uniform)
        {
            const FVector Center = Transform.TransformPosition(Box.GetCenter());
            ConsiderSegment(Handle, Center, Center);
        }
        else if (Mode == ERuntimeGizmoMode::Rotate)
        {
            const FVector Center = Box.GetCenter();
            const FVector Extent = Box.GetExtent();
            constexpr int32 Segments = 96;
            for (int32 Index = 0; Index < Segments; ++Index)
            {
                const double A = UE_TWO_PI * Index / Segments;
                const double B = UE_TWO_PI * (Index + 1) / Segments;
                ConsiderSegment(Handle, Transform.TransformPosition(Center + FVector(0, FMath::Cos(A) * Extent.Y, FMath::Sin(A) * Extent.Z)),
                    Transform.TransformPosition(Center + FVector(0, FMath::Cos(B) * Extent.Y, FMath::Sin(B) * Extent.Z)));
            }
        }
        else
        {
            const double Start = FMath::Max(Box.Min.X, Box.Max.X * 0.18);
            ConsiderSegment(Handle, Transform.TransformPosition(FVector(Start, Box.GetCenter().Y, Box.GetCenter().Z)), Transform.TransformPosition(FVector(Box.Max.X, Box.GetCenter().Y, Box.GetCenter().Z)));
        }
    }
    return Best;
}

bool ARuntimeTransformGizmoActor::PointerDown(FVector2D ScreenPosition)
{
    if (bDragging || !bInteractionEnabled || ScreenPosition.ContainsNaN() || !ValidateTarget(TargetComponent.Get()))
    {
        return false;
    }
    UpdatePlacement();
    FVector PickPosition;
    URuntimeGizmoHandleComponent* Handle = PickHandle(ScreenPosition, PickPosition);
    if (!Handle)
    {
        return false;
    }
    return StartDrag(Handle, ScreenPosition, PickPosition);
}

bool ARuntimeTransformGizmoActor::BeginAxisDrag(ERuntimeGizmoAxis Axis, FVector2D ScreenPosition)
{
    if (bDragging || !bInteractionEnabled || ScreenPosition.ContainsNaN() || !ValidateTarget(TargetComponent.Get()))
    {
        return false;
    }
    UpdatePlacement();
    URuntimeGizmoHandleComponent* Handle = GetHandleComponent(Mode, Axis);
    if (!bViewValid || !Handle || !Handle->bHandleEnabled)
    {
        return false;
    }
    const FVector Normal = GetActorQuat().RotateVector(AxisVector(Axis));
    RuntimeGizmoMath::FRay Ray;
    FVector Radial, Unused;
    Normal.FindBestAxisVectors(Radial, Unused);
    FVector PickPosition = GetActorLocation() + Radial;
    if (MakeRay(ScreenPosition, Ray))
    {
        RuntimeGizmoMath::IntersectPlane(Ray, GetActorLocation(), Normal, PickPosition);
    }
    return StartDrag(Handle, ScreenPosition, PickPosition);
}

bool ARuntimeTransformGizmoActor::StartDrag(URuntimeGizmoHandleComponent* Handle, const FVector2D& ScreenPosition, const FVector& PickPosition)
{
    InitialTransform = TargetComponent->GetComponentTransform();
    DragBasis = CoordinateSpace == ERuntimeGizmoSpace::Local ? InitialTransform.GetRotation() : FQuat::Identity;
    DragAxis = DragBasis.RotateVector(AxisVector(Handle->Axis));
    const FVector FromPivot = PickPosition - InitialTransform.GetLocation();
    DragRadial = (FromPivot - DragAxis * FVector::DotProduct(FromPivot, DragAxis)).GetSafeNormal();
    if (DragRadial.IsNearlyZero())
    {
        FVector Unused;
        DragAxis.FindBestAxisVectors(DragRadial, Unused);
    }
    PreviousMouse = ScreenPosition;
    AccumulatedDelta = 0.0;
    ActiveHandle = Handle;
    HoveredHandle = Handle;
    bDragging = true;
    UpdateAppearance();
    OnDragStarted.Broadcast(TargetComponent.Get(), InitialTransform);
    return true;
}

void ARuntimeTransformGizmoActor::PointerMove(FVector2D ScreenPosition)
{
    if (ScreenPosition.ContainsNaN())
    {
        CancelDrag();
        return;
    }
    UpdatePlacement();
    if (!bDragging)
    {
        FVector PickPosition;
        HoveredHandle = PickHandle(ScreenPosition, PickPosition);
        UpdateAppearance();
        return;
    }
    if (!bInteractionEnabled || !bViewValid || !ActiveHandle || !ValidateTarget(TargetComponent.Get()))
    {
        CancelDrag();
        return;
    }
    const FVector2D MouseDelta = ScreenPosition - PreviousMouse;
    if (MouseDelta.IsNearlyZero())
    {
        return;
    }
    RuntimeGizmoMath::FRay PreviousRay, CurrentRay;
    // Reproject both cursor positions through the CURRENT camera, removing camera-only motion.
    if (!MakeRay(PreviousMouse, PreviousRay) || !MakeRay(ScreenPosition, CurrentRay))
    {
        CancelDrag();
        return;
    }
    PreviousMouse = ScreenPosition;
    const FVector Pivot = GetActorLocation();
    const double ReferenceLength = UnitsPerPixel * FMath::Max(SizePixels, 32.0f);
    FVector2D ScreenPivot, ScreenAxis;
    if (!Project(Pivot, ScreenPivot))
    {
        CancelDrag();
        return;
    }
    if (!Project(Pivot + DragAxis * ReferenceLength, ScreenAxis))
    {
        ScreenAxis = ScreenPivot;
    }
    FTransform NewTransform = InitialTransform;
    if (Mode == ERuntimeGizmoMode::Rotate)
    {
        double Radians = 0.0;
        if (!RuntimeGizmoMath::RotationDelta(PreviousRay, CurrentRay, Pivot, DragAxis, Radians))
        {
            const FVector Radial = FQuat(DragAxis, AccumulatedDelta).RotateVector(DragRadial);
            const FVector Tangent = FVector::CrossProduct(DragAxis, Radial);
            FVector2D RingPoint, TangentPoint;
            const double Radius = ReferenceLength * 0.8;
            if (!Project(Pivot + Radial * Radius, RingPoint) || !Project(Pivot + Radial * Radius + Tangent * Radius * 0.1, TangentPoint))
            {
                return;
            }
            const FVector2D ProjectedTangent = TangentPoint - RingPoint;
            Radians = ProjectedTangent.Size() > 1.0 ? FVector2D::DotProduct(MouseDelta, ProjectedTangent.GetSafeNormal()) / FMath::Max(ProjectedTangent.Size() * 10.0, 16.0)
                : -MouseDelta.Y / FMath::Max(static_cast<double>(SizePixels), 32.0);
        }
        AccumulatedDelta += Radians;
        const double Angle = RuntimeGizmoMath::Snap(AccumulatedDelta, FMath::DegreesToRadians(FMath::Max(0.0, RotationSnapDegrees)));
        NewTransform.SetRotation((FQuat(DragAxis, Angle) * InitialTransform.GetRotation()).GetNormalized());
    }
    else
    {
        const double Delta = ActiveHandle->Axis == ERuntimeGizmoAxis::Uniform ? -MouseDelta.Y * UnitsPerPixel
            : RuntimeGizmoMath::AxisDelta(PreviousRay, CurrentRay, Pivot, DragAxis, ViewDirection, MouseDelta, ScreenAxis - ScreenPivot, ReferenceLength, UnitsPerPixel);
        if (Mode == ERuntimeGizmoMode::Translate)
        {
            AccumulatedDelta += Delta;
            NewTransform.SetLocation(InitialTransform.GetLocation() + DragAxis * RuntimeGizmoMath::Snap(AccumulatedDelta, TranslationSnap));
        }
        else
        {
            AccumulatedDelta = FMath::Clamp(AccumulatedDelta + Delta / UnitsPerPixel, -2400.0, 2400.0);
            double Factor = FMath::Pow(2.0, FMath::Clamp(AccumulatedDelta / FMath::Max(ScalePixelsPerDoubling, 16.0), -20.0, 20.0));
            Factor = FMath::Max(0.000001, 1.0 + RuntimeGizmoMath::Snap(Factor - 1.0, ScaleSnap));
            const bool bUniform = ActiveHandle->Axis == ERuntimeGizmoAxis::Uniform;
            const int32 LocalAxis = !bUniform && CoordinateSpace == ERuntimeGizmoSpace::Local ? static_cast<int32>(ActiveHandle->Axis) - 1 : -1;
            NewTransform.SetScale3D(RuntimeGizmoMath::ScaleAlongAxis(InitialTransform.GetScale3D(), InitialTransform.GetRotation(), bUniform ? FVector::ZeroVector : DragAxis,
                LocalAxis, Factor, FMath::Max(0.0001, MinimumScale)));
        }
    }
    if (!NewTransform.ContainsNaN() && !NewTransform.Equals(TargetComponent->GetComponentTransform(), 0.0000001))
    {
        TargetComponent->SetWorldTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
        UpdatePlacement();
        OnTransformChanged.Broadcast(TargetComponent.Get(), TargetComponent->GetComponentTransform());
    }
}

void ARuntimeTransformGizmoActor::FinishDrag(bool bCancelled)
{
    if (!bDragging)
    {
        return;
    }
    USceneComponent* FinishedTarget = TargetComponent.Get();
    const FTransform Start = InitialTransform;
    bDragging = false;
    ActiveHandle = nullptr;
    HoveredHandle = nullptr;
    if (bCancelled && IsValid(FinishedTarget))
    {
        FinishedTarget->SetWorldTransform(Start, false, nullptr, ETeleportType::TeleportPhysics);
    }
    const FTransform Final = IsValid(FinishedTarget) ? FinishedTarget->GetComponentTransform() : Start;
    UpdatePlacement();
    OnDragFinished.Broadcast(FinishedTarget, Start, Final, bCancelled);
}

void ARuntimeTransformGizmoActor::PointerUp()
{
    FinishDrag(false);
}

void ARuntimeTransformGizmoActor::CancelDrag()
{
    FinishDrag(true);
}

void ARuntimeTransformGizmoActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdatePlacement();
    if (bDragging && (!TargetComponent.IsValid() || !bViewValid))
    {
        CancelDrag();
    }
    if (!bAutoHandleMouse || !Controller.IsValid() || !bInteractionEnabled)
    {
        return;
    }
    ULocalPlayer* Player = Controller->GetLocalPlayer();
    FViewport* Viewport = Player && Player->ViewportClient ? Player->ViewportClient->Viewport : nullptr;
    FVector2D Mouse;
    if (!Viewport || !Viewport->HasFocus() || !Viewport->IsForegroundWindow() || !Controller->GetMousePosition(Mouse.X, Mouse.Y))
    {
        CancelDrag();
        HoveredHandle = nullptr;
        UpdateAppearance();
        return;
    }
    if (Controller->WasInputKeyJustPressed(EKeys::Escape))
    {
        CancelDrag();
        return;
    }
    PointerMove(Mouse);
    if (Controller->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        PointerDown(Mouse);
    }
    if (bDragging && !Controller->IsInputKeyDown(EKeys::LeftMouseButton))
    {
        PointerUp();
    }
}
