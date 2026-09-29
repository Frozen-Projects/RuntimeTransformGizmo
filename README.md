# Runtime Transform Gizmo — UE 5.8.3

A C++ runtime plugin centered on `ARuntimeTransformGizmoActor`. It provides XYZ translation, XYZ rotation, XYZ scale, uniform scale, world/local axes, snapping, hover highlighting, cancellation, and replaceable static-mesh handles. Handles render through a packaged-runtime GPU overlay after scene postprocessing. No Post Process Volume or Custom Depth project setting is required.

## Version 2.0.1 crash fix

Fixes the D3D12/SM6 pipeline-creation crash when the project enables **Instanced Stereo**, including ordinary PIE without a headset. Version 2.0 paired the engine's screen-pass vertex shader with a pixel shader whose input signature did not match its stereo permutation. The composite now uses a dedicated fullscreen-triangle vertex shader with a matching pixel shader signature.

Close the editor, replace the plugin's **Source**, **Shaders**, and `.uplugin` files, rebuild your editor configuration, and restart. Live Coding alone is insufficient because a new global shader type is registered. Recook packaged builds. No Blueprint/API migration is needed from 2.0, and no change to Instanced Stereo, SM6, or postprocess settings is required. This fixes compatibility with the project setting; headset rendering remains unvalidated and stereo texture arrays remain unsupported.

## Install

1. Copy this `RuntimeTransformGizmo` folder into your project's `Plugins` folder.
2. Enable **Runtime Transform Gizmo** and build your project with UE 5.8.3. For a Blueprint-only project, add a C++ class first so Unreal can compile the source plugin.
3. Create a Blueprint subclass of **RuntimeTransformGizmoActor**, or spawn the C++ actor directly.
4. Set the selected scene component mobility to **Movable**, then call `SetTargetComponent`. Check the returned boolean and `LastError` if selection fails.
5. Call `SetPlayerController` with the local player controller driving the viewport. Player 0 is the BeginPlay default.

The plugin is a Runtime module. Arrow/ring/cube meshes are hard-referenced from `/Engine/InteractiveToolsFramework` and `/Engine/BasicShapes`. A compact scale-axis mesh is bundled in the plugin's Content folder. The cooker discovers these assets from the actor's components. Copy **Content**, **Shaders**, and **Source**. The module loads at **PostConfigInit** so its global shaders compile and cook correctly. No editor gizmo or UnrealEd dependency is used. Keep custom mesh references on your gizmo Blueprint or in another cooked asset.

## Blueprint setup

On BeginPlay in your PlayerController:

- Spawn your gizmo Blueprint and keep the returned reference.
- Call **Set Player Controller**, passing Self.
- Call **Set Target Component**, passing the selected CAD component, or an actor's root component for the whole actor.
- Set **Show Mouse Cursor** to true and configure your game's input mode to expose the mouse.
- Call **Set Mode** with Translate, Rotate, or Scale.
- Call **Set Coordinate Space** with World or Local.

With **Auto Handle Mouse** enabled, left mouse drags a handle; release commits; Escape cancels. Losing window focus, losing the cursor, deleting the target, or disabling interaction cancels the drag. A cancelled drag restores its starting transform. Changing target, mode, controller, or coordinate space also cancels any current drag.

The gizmo follows the selected component's world pivot. Its size stays approximately constant in screen pixels as the camera moves or zooms. It does not inherit the target's scale.

The gizmo does not automatically select scene objects. Use your existing selection trace, then call `SetTargetComponent`; pass null to clear selection.

## C++ setup

Add `RuntimeTransformGizmo` to your game's module dependencies, then include `RuntimeTransformGizmoActor.h`.

```cpp
ARuntimeTransformGizmoActor* Gizmo = GetWorld()->SpawnActor<ARuntimeTransformGizmoActor>();
Gizmo->SetPlayerController(PlayerController);
Gizmo->SetTargetComponent(SelectedComponent);
// To manipulate the whole actor:
// Gizmo->SetTargetComponent(SelectedActor->GetRootComponent());
Gizmo->SetMode(ERuntimeGizmoMode::Translate);
Gizmo->SetCoordinateSpace(ERuntimeGizmoSpace::Local);

PlayerController->SetShowMouseCursor(true);
FInputModeGameAndUI InputMode;
InputMode.SetHideCursorDuringCapture(false);
InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
PlayerController->SetInputMode(InputMode);
```

Store the gizmo reference in a `UPROPERTY` on your controller/manager. Configure camera controls in your application; the gizmo reads the camera's current view and does not take ownership of it. If mouse-look warps or recenters the cursor, finish/cancel the current drag before entering that input mode.

## UI / Enhanced Input integration

For UMG-heavy interfaces, set **Auto Handle Mouse** to false and route accepted pointer events yourself. Automatic polling cannot know whether your UI consumed a click.

| Event | Call |
| --- | --- |
| Left button pressed over the game viewport | `PointerDown(ScreenPosition)`; true means a handle accepted the drag |
| Pointer moved | `PointerMove(ScreenPosition)` |
| Left button released | `PointerUp()` |
| Escape, capture lost, input mode changed | `CancelDrag()` |
| Explicit X/Y/Z command from a toolbar or key | `BeginAxisDrag(Axis, ScreenPosition)` |

Pass raw game-viewport pixel coordinates in the same coordinate system as `PlayerController.GetMousePosition` and `ProjectWorldLocationToScreen(..., false)`. Do not pass desktop coordinates or DPI-scaled UMG coordinates. For manual input, capture pointer events during a drag and deliver release/cancel even outside the widget. Do not combine manual events with automatic polling.

`BeginAxisDrag` also lets you select an axis when several handles overlap in a head-on view.

## Replace the handle meshes

The actor contains ten inherited `URuntimeGizmoHandleComponent` static-mesh components:

- `Move_X`, `Move_Y`, `Move_Z`
- `Rotate_X`, `Rotate_Y`, `Rotate_Z`
- `Scale_X`, `Scale_Y`, `Scale_Z`, `Scale_Uniform`

Edit each component's **Static Mesh**, relative transform, **Axis Color**, **Pick Radius Pixels**, and **Handle Enabled** in your Blueprint. At runtime, obtain it with `GetHandleComponent(Mode, Axis)`, then use `SetStaticMesh` and the usual component transform functions. Include `RuntimeGizmoHandleComponent.h` for C++ component access.

Asset conventions:

- Translation and axis-scale meshes extend along their own **+X** axis, with the pivot at the gizmo origin. Existing component rotation points that canonical +X toward X, Y, or Z.
- Rotation meshes are centered on their pivot in their own **YZ plane**, with **+X** as the ring normal.
- Uniform scale uses a centered mesh.
- Default arrows are normalized to 100 gizmo units; default ring radius is 80. When replacing a mesh, adjust the component's relative scale to preserve the desired size.

Picking uses a screen-space proxy derived from the mesh's geometry bounds and component transform: a segment for an arrow, a sampled ellipse for a ring, and a center point for uniform scale. Rendering-only bounds extensions are excluded. It requires no collision and does not block gameplay traces. Keep replacements compatible with these shapes; arbitrary decorative shapes do not acquire triangle-accurate picking. Increase **Pick Radius Pixels** for a thicker selectable region. Nanite assets must have a usable conventional fallback mesh for the overlay.

## Foreground rendering and colors

`URuntimeGizmoHandleComponent` creates a custom `FStaticMeshSceneProxy`. A runtime `FSceneViewExtensionBase` gathers those proxies and schedules RDG passes from `PostRenderView_RenderThread`, after scene postprocessing and before UMG/Slate. There is no Post Process Volume, scene capture, Custom Depth requirement, editor compositor, or CPU mesh readback in the plugin.

Only the screen rectangle containing the handles is rendered. The overlay uses existing static-mesh GPU position/index buffers, a separate cleared depth buffer, and a linear color target. Scene geometry cannot occlude the handles; the handles still depth-test against one another. It uses an unjittered camera projection and 2x supersampling per dimension for antialiased edges, independently of scene TAA/TSR. Extremely large rectangles fall back to 1x to bound texture dimensions. Picking remains independent of scene occlusion.

**Axis Color** and the actor's **Highlight Color** are linear-sRGB colors, converted to the output display format when composited. Interior pixels are opaque. Scene exposure, lighting, fog, bloom, depth of field, and color grading do not recolor them. Edge pixels blend with the scene using coverage in linear light. SDR, Rec.709, explicit-gamma, PQ, and scRGB conversion paths are included; physical HDR display output has not been validated.

Static-mesh material slots do not determine overlay appearance. The handles use a flat-color shader; material textures, WPO, opacity masks, and custom lighting are not evaluated. Replace mesh geometry freely and set **Axis Color** instead. Nanite assets use their conventional fallback render mesh; use a suitable fallback mesh for the handle silhouette. CPU Access does not need to be enabled.

The pass is scoped to the target scene and local controller's view. Scene captures and reflection captures are excluded. Custom renderer/view-extension plugins that draw later must coordinate their ordering if they also overlay the scene. The extension uses priority -10000; lower-priority extensions execute later. Game UI remains above the gizmo.

## Camera behavior

Axis translation intersects mouse rays with a camera-facing plane containing the selected world-space axis. Both the previous and current cursor positions are deprojected using the **current camera** each update, preserving subpixel precision. The camera may orbit during a drag without introducing movement when the cursor is stationary.

The resulting world-axis projection reverses naturally behind the object; the same logic applies above, below, with camera roll, and with orthographic views. There is no hardcoded mouse-X-to-world-Y mapping.

At a nearly head-on angle, the ray constraint becomes ill-conditioned. The gizmo uses the projected axis with a bounded gain. If the axis projects to less than two pixels, upward mouse movement means positive movement/scale. An exactly head-on axis has no unique 2D screen direction.

Rotation uses signed angles between ray-plane intersections. The sign remains correct on either side of the rotation plane, and incremental angles support multiple revolutions. Edge-on rings use their projected tangent; at an ambiguous tangent, dragging up is the positive-angle fallback. The selected basis is locked for the duration of a drag, so local rotation does not move its own constraint frame.

## World/local scale semantics

Translation and rotation are exact in both coordinate spaces. Local scale changes the selected local scale component; uniform scale changes all three components. Existing negative scale signs are preserved; dragging does not cross through zero or introduce a new mirror.

**World-axis nonuniform scaling of an arbitrarily rotated component is an approximation.** Such a stretch normally creates shear, and one Unreal `FTransform` cannot store shear. This implementation keeps the component rotation and preserves the lengths of the stretched local basis vectors:

`newScale[i] = startScale[i] * sqrt(1 + (factor² - 1) * dot(worldAxis, rotatedLocalAxis[i])²)`

This is exact when the selected world axis aligns with a local axis, and for uniform scale. In an oblique orientation it discards shear; for example, a 45-degree rotated cube can increase two local scale components together. If exact affine world stretching is required, use a geometry-deformation or affine-matrix rendering path instead of a single component transform.

Scaling is exponential: positive movement equal to **Scale Pixels Per Doubling** doubles scale for an axis viewed perpendicular to the camera. **Minimum Scale** prevents collapsed axes. Scale sensitivity remains stable as camera distance changes.

## Snapping and change events

| Setting | Meaning |
| --- | --- |
| Translation Snap | Distance step in Unreal units; 0 disables |
| Rotation Snap Degrees | Angular step in degrees; 0 disables |
| Scale Snap | Step in the scale multiplier, e.g. 0.1; 0 disables |

Snapping applies to accumulated drag displacement from the starting transform, not to the absolute world grid. Small mouse movements accumulate instead of getting rounded away each frame.

- `OnDragStarted(Target, InitialTransform)` starts an application undo record.
- `OnTransformChanged(Target, Transform)` reports applied updates during dragging.
- `OnDragFinished(Target, InitialTransform, FinalTransform, bCancelled)` closes or discards that record. Cancellation is reported here, including the restored transform; it does not emit an additional `OnTransformChanged` event. Target is a `USceneComponent*` and can be null if destroyed. All event transforms are world transforms.

Use `IsDragging()` to prevent scene selection or conflicting manipulation. The gizmo calls `SetWorldTransform` on the selected component without collision sweeps. Its attachment is preserved; its attached descendants follow, while ancestors and siblings remain unchanged. Keep other transform-writing systems inactive for the selected target during a drag.

## Supported scope

One selected scene component and one local viewport per gizmo. The selected component must be registered and Movable. Physics simulation on the selected component or its attached descendants is rejected; unrelated sibling components are not part of that validation. Attached targets are supported when all ancestors have positive uniform scale. Nonuniform/mirrored ancestor scale is rejected with `LastError`, because relative/world conversion can introduce additional shear. Detach with Keep World Transform or select an appropriate movable parent.

The gizmo itself is not replicated. The application owns selection, undo history, multiplayer authority, target replication, UI bindings, camera controls, and any plane/free-move handles. Split-screen/stereo integration has not been validated; stereo texture arrays are not supported by this overlay.

## Upgrade from version 1

- Replace **Set Target Actor** with **Set Target Component**. Use the selected trace hit's `Component`, or `SelectedActor->GetRootComponent()` for a whole actor. Pass null to clear selection.
- Replace `GetTargetActor()` with `GetTargetComponent()`; call `GetOwner()` on that component when you need the owning actor.
- Rebind Blueprint drag-event nodes: their `Target` pin is now a scene component. The three event names and world-transform payloads remain the same.
- `HandleMaterial`, `RefreshHandleMaterials`, and `bUseAxisMaterial` were removed. Configure `AxisColor` and `HighlightColor` for the overlay shader.
- Replace the complete plugin folder, including **Shaders**, and restart/rebuild the editor. The new global shaders require the earlier module load phase; Live Coding alone is insufficient for this upgrade.

## Validation

See `VALIDATION.md` for actual build/run results and test scope. Runtime automation tests are included under `Private/Tests` and compile only when `WITH_DEV_AUTOMATION_TESTS` is enabled. Their prefix is `RuntimeTransformGizmo`.

Engine API references: [PlayerController screen projection](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/APlayerController/ProjectWorldLocationToScreen) and [runtime InteractiveToolsFramework](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/InteractiveToolsFramework). The implementation uses engine math and actor components directly; it does not require initializing an Interactive Tools context.
