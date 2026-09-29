#include "RuntimeGizmoRendering.h"
#include "RuntimeGizmoHandleComponent.h"
#include "CommonRenderResources.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GlobalShader.h"
#include "HDRHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneViewExtension.h"
#include "ShaderParameterStruct.h"
#include "StaticMeshResources.h"
#include "StaticMeshSceneProxy.h"
#include "UnrealClient.h"

namespace
{
    class FRuntimeGizmoSceneProxy;
    TArray<FRuntimeGizmoSceneProxy*> GizmoProxies;

    struct FHandleDraw
    {
        FBufferRHIRef Positions;
        FBufferRHIRef Indices;
        TArray<FStaticMeshSection> Sections;
        FMatrix44f LocalToClip;
        FLinearColor Color;
        uint32 Stride = 0;
    };

    class FRuntimeGizmoSceneProxy final : public FStaticMeshSceneProxy
    {
    public:
        explicit FRuntimeGizmoSceneProxy(URuntimeGizmoHandleComponent* Component)
            : FStaticMeshSceneProxy(Component, false), Color(Component->GetOverlayColor()), PlayerIndex(Component->GetOverlayPlayerIndex()) {}

        virtual SIZE_T GetTypeHash() const override { static size_t Unique; return reinterpret_cast<size_t>(&Unique); }
        virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override {}
        virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& Family, uint32 VisibilityMap, FMeshElementCollector& Collector) const override {}
        virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override { return FPrimitiveViewRelevance(); }
        virtual bool CanBeOccluded() const override { return false; }
        virtual bool HasDistanceFieldRepresentation() const override { return false; }
#if RHI_RAYTRACING
        virtual bool IsRayTracingRelevant() const override { return false; }
        virtual bool HasRayTracingRepresentation() const override { return false; }
#endif

        virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
        {
            FStaticMeshSceneProxy::CreateRenderThreadResources(RHICmdList);
            GizmoProxies.Add(this);
        }

        virtual void DestroyRenderThreadResources() override
        {
            GizmoProxies.RemoveSingleSwap(this);
            FStaticMeshSceneProxy::DestroyRenderThreadResources();
        }

        void Update(FLinearColor NewColor, int32 NewPlayerIndex)
        {
            Color = NewColor;
            PlayerIndex = NewPlayerIndex;
        }

        bool Gather(const FSceneView& View, FHandleDraw& Draw, FIntRect& ScreenBounds) const
        {
            if (&GetScene() != View.Family->Scene || PlayerIndex != View.PlayerIndex || !IsShown(&View)) return false;
            const int32 LODIndex = FMath::Max(static_cast<int32>(GetCurrentFirstLODIdx_RenderThread()), ClampedMinLOD);
            if (!RenderData || !RenderData->LODResources.IsValidIndex(LODIndex)) return false;
            const FStaticMeshLODResources& LOD = RenderData->LODResources[LODIndex];
            Draw.Positions = LOD.VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
            Draw.Indices = LOD.IndexBuffer.IndexBufferRHI;
            if (!Draw.Positions || !Draw.Indices || LOD.GetNumVertices() == 0) return false;
            Draw.Sections = LOD.Sections;
            Draw.Stride = LOD.VertexBuffers.PositionVertexBuffer.GetStride();
            Draw.Color = Color;
            const FMatrix Matrix = GetLocalToWorld() * View.ViewMatrices.GetWorldToView() * View.ViewMatrices.GetViewToClipNoAA();
            Draw.LocalToClip = FMatrix44f(Matrix);
            const FIntRect ViewRect = View.UnscaledViewRect;
            const FBox Box = GetLocalBounds().GetBox();
            FVector2D Min(DBL_MAX, DBL_MAX), Max(-DBL_MAX, -DBL_MAX);
            for (int32 Corner = 0; Corner < 8; ++Corner)
            {
                const FVector Point(Corner & 1 ? Box.Max.X : Box.Min.X, Corner & 2 ? Box.Max.Y : Box.Min.Y, Corner & 4 ? Box.Max.Z : Box.Min.Z);
                const FVector4 Clip = Matrix.TransformFVector4(FVector4(Point, 1.0));
                if (Clip.W <= 0.001) { ScreenBounds = ViewRect; return true; }
                const FVector2D Pixel(ViewRect.Min.X + (Clip.X / Clip.W * 0.5 + 0.5) * ViewRect.Width(), ViewRect.Min.Y + (0.5 - Clip.Y / Clip.W * 0.5) * ViewRect.Height());
                Min.X = FMath::Min(Min.X, Pixel.X); Min.Y = FMath::Min(Min.Y, Pixel.Y);
                Max.X = FMath::Max(Max.X, Pixel.X); Max.Y = FMath::Max(Max.Y, Pixel.Y);
            }
            Min.X = FMath::Clamp(Min.X - 3, static_cast<double>(ViewRect.Min.X), static_cast<double>(ViewRect.Max.X));
            Min.Y = FMath::Clamp(Min.Y - 3, static_cast<double>(ViewRect.Min.Y), static_cast<double>(ViewRect.Max.Y));
            Max.X = FMath::Clamp(Max.X + 3, static_cast<double>(ViewRect.Min.X), static_cast<double>(ViewRect.Max.X));
            Max.Y = FMath::Clamp(Max.Y + 3, static_cast<double>(ViewRect.Min.Y), static_cast<double>(ViewRect.Max.Y));
            ScreenBounds = FIntRect(FMath::FloorToInt(Min.X), FMath::FloorToInt(Min.Y), FMath::CeilToInt(Max.X), FMath::CeilToInt(Max.Y));
            return ScreenBounds.Width() > 0 && ScreenBounds.Height() > 0;
        }

    private:
        FLinearColor Color;
        int32 PlayerIndex;
    };

    class FRuntimeGizmoVS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FRuntimeGizmoVS);
        SHADER_USE_PARAMETER_STRUCT(FRuntimeGizmoVS, FGlobalShader);
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FMatrix44f, LocalToClip)
            SHADER_PARAMETER(FVector4f, ClipScaleBias)
        END_SHADER_PARAMETER_STRUCT()
    };

    class FRuntimeGizmoPS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FRuntimeGizmoPS);
        SHADER_USE_PARAMETER_STRUCT(FRuntimeGizmoPS, FGlobalShader);
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FVector4f, HandleColor)
        END_SHADER_PARAMETER_STRUCT()
    };

    class FRuntimeGizmoCompositeVS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FRuntimeGizmoCompositeVS);
        SHADER_USE_PARAMETER_STRUCT(FRuntimeGizmoCompositeVS, FGlobalShader);
        using FParameters = FEmptyShaderParameters;
    };

    class FRuntimeGizmoCompositePS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FRuntimeGizmoCompositePS);
        SHADER_USE_PARAMETER_STRUCT(FRuntimeGizmoCompositePS, FGlobalShader);
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OverlayTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BackgroundTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, OverlaySampler)
            SHADER_PARAMETER(FVector2f, OutputOrigin)
            SHADER_PARAMETER(FVector2f, InverseOutputSize)
            SHADER_PARAMETER(FMatrix44f, ColorToOutputGamut)
            SHADER_PARAMETER(uint32, OutputFormat)
            SHADER_PARAMETER(uint32, HardwareSRGB)
            SHADER_PARAMETER(float, DisplayGamma)
            SHADER_PARAMETER(float, PaperWhiteNits)
            RENDER_TARGET_BINDING_SLOTS()
        END_SHADER_PARAMETER_STRUCT()
    };

    IMPLEMENT_GLOBAL_SHADER(FRuntimeGizmoVS, "/RuntimeTransformGizmo/Private/RuntimeGizmo.usf", "HandleVS", SF_Vertex);
    IMPLEMENT_GLOBAL_SHADER(FRuntimeGizmoPS, "/RuntimeTransformGizmo/Private/RuntimeGizmo.usf", "HandlePS", SF_Pixel);
    IMPLEMENT_GLOBAL_SHADER(FRuntimeGizmoCompositeVS, "/RuntimeTransformGizmo/Private/RuntimeGizmo.usf", "CompositeVS", SF_Vertex);
    IMPLEMENT_GLOBAL_SHADER(FRuntimeGizmoCompositePS, "/RuntimeTransformGizmo/Private/RuntimeGizmo.usf", "CompositePS", SF_Pixel);

    class FRuntimeGizmoViewExtension final : public FSceneViewExtensionBase
    {
    public:
        explicit FRuntimeGizmoViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}
        virtual void SetupViewFamily(FSceneViewFamily& Family) override {}
        virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override {}
        virtual void BeginRenderViewFamily(FSceneViewFamily& Family) override {}
        virtual int32 GetPriority() const override { return -10000; }

        virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& View) override
        {
            if (!View.bIsGameView || View.bIsSceneCapture || View.bIsReflectionCapture || !View.Family->RenderTarget || GizmoProxies.IsEmpty()) return;
            FRDGTextureRef Output = View.Family->RenderTarget->GetRenderTargetTexture(GraphBuilder);
            if (!Output || Output->Desc.IsTextureArray()) return;
            const FIntRect ViewRect = View.UnscaledViewRect;
            if (ViewRect.Width() <= 0 || ViewRect.Height() <= 0) return;
            TArray<FHandleDraw>* Draws = GraphBuilder.AllocObject<TArray<FHandleDraw>>();
            FIntRect Crop;
            for (const FRuntimeGizmoSceneProxy* Proxy : GizmoProxies)
            {
                FHandleDraw Draw;
                FIntRect Bounds;
                if (Proxy->Gather(View, Draw, Bounds))
                {
                    Crop = Draws->IsEmpty() ? Bounds : FIntRect(Crop.Min.ComponentMin(Bounds.Min), Crop.Max.ComponentMax(Bounds.Max));
                    Draws->Add(MoveTemp(Draw));
                }
            }
            Crop.Clip(FIntRect(FIntPoint::ZeroValue, Output->Desc.Extent));
            if (Draws->IsEmpty() || Crop.Width() <= 0 || Crop.Height() <= 0) return;

            const int32 Supersample = FMath::Max(Crop.Width(), Crop.Height()) <= 4096 ? 2 : 1;
            const FIntPoint Extent = Crop.Size() * Supersample;
            const FVector4f ClipScaleBias(static_cast<float>(ViewRect.Width()) / Crop.Width(), static_cast<float>(ViewRect.Height()) / Crop.Height(),
                static_cast<float>(ViewRect.Width() - 2 * (Crop.Min.X - ViewRect.Min.X)) / Crop.Width() - 1.0f,
                1.0f - static_cast<float>(ViewRect.Height() - 2 * (Crop.Min.Y - ViewRect.Min.Y)) / Crop.Height());
            FRDGTextureRef Color = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Extent, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("Gizmo.Color"));
            FRDGTextureRef Depth = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable), TEXT("Gizmo.Depth"));
            FRenderTargetParameters* Pass = GraphBuilder.AllocParameters<FRenderTargetParameters>();
            Pass->RenderTargets[0] = FRenderTargetBinding(Color, ERenderTargetLoadAction::EClear);
            Pass->RenderTargets.DepthStencil = FDepthStencilBinding(Depth, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);
            const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
            TShaderMapRef<FRuntimeGizmoVS> VertexShader(ShaderMap);
            TShaderMapRef<FRuntimeGizmoPS> PixelShader(ShaderMap);
            GraphBuilder.AddPass(RDG_EVENT_NAME("RuntimeGizmo.Handles"), Pass, ERDGPassFlags::Raster,
                [Draws, Extent, ClipScaleBias, VertexShader, PixelShader](FRHICommandList& RHICmdList)
                {
                    RHICmdList.SetViewport(0, 0, 0, Extent.X, Extent.Y, 1);
                    FGraphicsPipelineStateInitializer Pipeline;
                    RHICmdList.ApplyCachedRenderTargets(Pipeline);
                    Pipeline.BlendState = TStaticBlendState<>::GetRHI();
                    Pipeline.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
                    Pipeline.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
                    Pipeline.PrimitiveType = PT_TriangleList;
                    Pipeline.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
                    Pipeline.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
                    for (const FHandleDraw& Draw : *Draws)
                    {
                        FVertexDeclarationElementList Elements;
                        Elements.Add(FVertexElement(0, 0, VET_Float3, 0, Draw.Stride));
                        Pipeline.BoundShaderState.VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
                        SetGraphicsPipelineState(RHICmdList, Pipeline, 0);
                        FRuntimeGizmoVS::FParameters VS;
                        VS.LocalToClip = Draw.LocalToClip;
                        VS.ClipScaleBias = ClipScaleBias;
                        SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VS);
                        FRuntimeGizmoPS::FParameters PS;
                        PS.HandleColor = FVector4f(Draw.Color.R, Draw.Color.G, Draw.Color.B, 1);
                        SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PS);
                        RHICmdList.SetStreamSource(0, Draw.Positions, 0);
                        for (const FStaticMeshSection& Section : Draw.Sections)
                        {
                            if (Section.NumTriangles > 0) RHICmdList.DrawIndexedPrimitive(Draw.Indices, 0, Section.MinVertexIndex, Section.MaxVertexIndex - Section.MinVertexIndex + 1, Section.FirstIndex, Section.NumTriangles, 1);
                        }
                    }
                });

            FRDGTextureDesc BackgroundDesc = FRDGTextureDesc::Create2D(Crop.Size(), Output->Desc.Format, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable);
            BackgroundDesc.Flags |= Output->Desc.Flags & TexCreate_SRGB;
            FRDGTextureRef Background = GraphBuilder.CreateTexture(BackgroundDesc, TEXT("Gizmo.Background"));
            FRHICopyTextureInfo Copy;
            Copy.SourcePosition = FIntVector(Crop.Min.X, Crop.Min.Y, 0);
            Copy.Size = FIntVector(Crop.Width(), Crop.Height(), 1);
            AddCopyTexturePass(GraphBuilder, Output, Background, Copy);

            const FRenderTarget* Target = View.Family->RenderTarget;
            const EDisplayOutputFormat Format = Target->GetDisplayOutputFormat();
            const bool bScRGB = Format == EDisplayOutputFormat::HDR_ACES_1000nit_ScRGB || Format == EDisplayOutputFormat::HDR_ACES_2000nit_ScRGB;
            FRuntimeGizmoCompositePS::FParameters* Composite = GraphBuilder.AllocParameters<FRuntimeGizmoCompositePS::FParameters>();
            Composite->OverlayTexture = Color;
            Composite->BackgroundTexture = Background;
            Composite->OverlaySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
            Composite->OutputOrigin = FVector2f(Crop.Min.X, Crop.Min.Y);
            Composite->InverseOutputSize = FVector2f(1.0f / Crop.Width(), 1.0f / Crop.Height());
            Composite->ColorToOutputGamut = bScRGB ? FMatrix44f::Identity : GamutToXYZMatrix(EDisplayColorGamut::sRGB_D65) * XYZToGamutMatrix(Target->GetDisplayColorGamut());
            Composite->OutputFormat = static_cast<uint32>(Format);
            Composite->HardwareSRGB = EnumHasAnyFlags(Output->Desc.Flags, TexCreate_SRGB) ? 1u : 0u;
            Composite->DisplayGamma = FMath::Max(Target->GetDisplayGamma(), 0.01f);
            Composite->PaperWhiteNits = FMath::Max(Target->GetHDRPaperWhiteInNits(), 1.0f);
            Composite->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ELoad);
            TShaderMapRef<FRuntimeGizmoCompositeVS> CompositeVertexShader(ShaderMap);
            TShaderMapRef<FRuntimeGizmoCompositePS> CompositeShader(ShaderMap);
            // Keep both shader signatures independent of engine screen-pass stereo permutations.
            GraphBuilder.AddPass(RDG_EVENT_NAME("RuntimeGizmo.Composite"), Composite, ERDGPassFlags::Raster,
                [Crop, CompositeVertexShader, CompositeShader, Composite](FRHICommandList& RHICmdList)
                {
                    RHICmdList.SetViewport(Crop.Min.X, Crop.Min.Y, 0, Crop.Max.X, Crop.Max.Y, 1);
                    FGraphicsPipelineStateInitializer Pipeline;
                    RHICmdList.ApplyCachedRenderTargets(Pipeline);
                    Pipeline.BlendState = TStaticBlendState<>::GetRHI();
                    Pipeline.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
                    Pipeline.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
                    Pipeline.PrimitiveType = PT_TriangleList;
                    Pipeline.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
                    Pipeline.BoundShaderState.VertexShaderRHI = CompositeVertexShader.GetVertexShader();
                    Pipeline.BoundShaderState.PixelShaderRHI = CompositeShader.GetPixelShader();
                    SetGraphicsPipelineState(RHICmdList, Pipeline, 0);
                    SetShaderParameters(RHICmdList, CompositeShader, CompositeShader.GetPixelShader(), *Composite);
                    RHICmdList.DrawPrimitive(0, 1, 1);
                });
        }
    };

    TSharedPtr<FRuntimeGizmoViewExtension, ESPMode::ThreadSafe> ViewExtension;
    FDelegateHandle EngineInitHandle;

    void RegisterExtension()
    {
        if (!ViewExtension && FApp::CanEverRender()) ViewExtension = FSceneViewExtensions::NewExtension<FRuntimeGizmoViewExtension>();
    }
}

void RuntimeGizmoRendering::Startup()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RuntimeTransformGizmo"));
    check(Plugin);
    AddShaderSourceDirectoryMapping(TEXT("/RuntimeTransformGizmo"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    EngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddStatic(&RegisterExtension);
    if (GEngine) RegisterExtension();
}

void RuntimeGizmoRendering::Shutdown()
{
    FCoreDelegates::GetOnPostEngineInit().Remove(EngineInitHandle);
    ViewExtension.Reset();
    if (IsInGameThread()) FlushRenderingCommands();
}

FPrimitiveSceneProxy* RuntimeGizmoRendering::CreateProxy(URuntimeGizmoHandleComponent* Component)
{
    UStaticMesh* Mesh = Component->GetStaticMesh();
    if (!Mesh || !Mesh->GetRenderData() || !Mesh->GetRenderData()->IsInitialized() || Mesh->GetRenderData()->LODResources.IsEmpty()) return nullptr;
    return new FRuntimeGizmoSceneProxy(Component);
}

void RuntimeGizmoRendering::UpdateProxy(FPrimitiveSceneProxy* Proxy, FLinearColor Color, int32 PlayerIndex)
{
    ENQUEUE_RENDER_COMMAND(UpdateGizmoOverlay)([Proxy, Color, PlayerIndex](FRHICommandListImmediate& RHICmdList)
    {
        static_cast<FRuntimeGizmoSceneProxy*>(Proxy)->Update(Color, PlayerIndex);
    });
}
