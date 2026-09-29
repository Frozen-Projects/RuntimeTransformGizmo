#include "Modules/ModuleManager.h"
#include "RuntimeGizmoRendering.h"

class FRuntimeTransformGizmoModule : public IModuleInterface
{
public:
    virtual void StartupModule() override { RuntimeGizmoRendering::Startup(); }
    virtual void ShutdownModule() override { RuntimeGizmoRendering::Shutdown(); }
};

IMPLEMENT_MODULE(FRuntimeTransformGizmoModule, RuntimeTransformGizmo)
