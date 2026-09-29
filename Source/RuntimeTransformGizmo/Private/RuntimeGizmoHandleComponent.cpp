#include "RuntimeGizmoHandleComponent.h"
#include "Engine/StaticMesh.h"
#include "RuntimeGizmoRendering.h"

URuntimeGizmoHandleComponent::URuntimeGizmoHandleComponent()
{
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCastShadow(false);
    SetCanEverAffectNavigation(false);
    SetMobility(EComponentMobility::Movable);
    bDisallowNanite = true;
    bAffectDistanceFieldLighting = false;
    bUseAsOccluder = false;
    SetVisibleInRayTracing(false);
}

FPrimitiveSceneProxy* URuntimeGizmoHandleComponent::CreateSceneProxy()
{
    return RuntimeGizmoRendering::CreateProxy(this);
}

void URuntimeGizmoHandleComponent::SendRenderDynamicData_Concurrent()
{
    Super::SendRenderDynamicData_Concurrent();
    if (SceneProxy) RuntimeGizmoRendering::UpdateProxy(SceneProxy, OverlayColor, OverlayPlayerIndex);
}

void URuntimeGizmoHandleComponent::SetOverlayAppearance(FLinearColor Color, int32 PlayerIndex)
{
    if (OverlayColor != Color || OverlayPlayerIndex != PlayerIndex)
    {
        OverlayColor = Color;
        OverlayPlayerIndex = PlayerIndex;
        MarkRenderDynamicDataDirty();
    }
}

FBox URuntimeGizmoHandleComponent::GetInteractionBounds() const
{
    const UStaticMesh* Mesh = GetStaticMesh();
    if (!Mesh)
    {
        return FBox(ForceInit);
    }
    const FBox MeshBounds = Mesh->GetBoundingBox();
    return FBox(MeshBounds.Min + Mesh->GetNegativeBoundsExtension(), MeshBounds.Max - Mesh->GetPositiveBoundsExtension());
}
