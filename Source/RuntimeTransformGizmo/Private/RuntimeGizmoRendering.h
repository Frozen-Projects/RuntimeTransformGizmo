#pragma once

#include "CoreMinimal.h"

class FPrimitiveSceneProxy;
class URuntimeGizmoHandleComponent;

namespace RuntimeGizmoRendering
{
    void Startup();
    void Shutdown();
    FPrimitiveSceneProxy* CreateProxy(URuntimeGizmoHandleComponent* Component);
    void UpdateProxy(FPrimitiveSceneProxy* Proxy, FLinearColor Color, int32 PlayerIndex);
}
