/*
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NonnullOwnPtr.h>
#include <AK/RefPtr.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/SkiaBackendContext.h>

#include <core/SkSurface.h>
#include <gpu/ganesh/GrDirectContext.h>

#ifdef USE_VULKAN
#    include <gpu/ganesh/vk/GrVkDirectContext.h>
#    include <gpu/vk/VulkanBackendContext.h>
#    include <gpu/vk/VulkanExtensions.h>
#endif

#ifdef AK_MACOS_HAS_METAL
#    include <gpu/ganesh/GrBackendSurface.h>
#    include <gpu/ganesh/mtl/GrMtlBackendContext.h>
#    include <gpu/ganesh/mtl/GrMtlBackendSurface.h>
#    include <gpu/ganesh/mtl/GrMtlDirectContext.h>
#endif

#ifdef LADYBIRD_LEGACY_MACOS
#    include <OpenGL/OpenGL.h>
#    include <gpu/ganesh/GrContextOptions.h>
#    include <gpu/ganesh/gl/GrGLDirectContext.h>
#    include <gpu/ganesh/gl/GrGLInterface.h>
#    include <gpu/ganesh/gl/mac/GrGLMakeMacInterface.h>
#endif

namespace Gfx {

static RefPtr<SkiaBackendContext> s_the;

void SkiaBackendContext::initialize_gpu_backend()
{
    VERIFY(!s_the);

#ifdef AK_MACOS_HAS_METAL
    auto metal_context = get_metal_context();
    s_the = create_metal_context(*metal_context);
#elif defined(LADYBIRD_LEGACY_MACOS)
    s_the = create_opengl_context();
#elif USE_VULKAN
    auto maybe_vulkan_context = Gfx::create_vulkan_context();
    if (maybe_vulkan_context.is_error()) {
        dbgln("Vulkan context creation failed: {}", maybe_vulkan_context.error());
        return;
    }
    auto vulkan_context = maybe_vulkan_context.release_value();
    s_the = create_vulkan_context(vulkan_context);
#endif
}

RefPtr<SkiaBackendContext> SkiaBackendContext::the()
{
    return s_the;
}

#ifdef USE_VULKAN
class SkiaVulkanBackendContext final : public SkiaBackendContext {
    AK_MAKE_NONCOPYABLE(SkiaVulkanBackendContext);
    AK_MAKE_NONMOVABLE(SkiaVulkanBackendContext);

public:
    SkiaVulkanBackendContext(sk_sp<GrDirectContext> context, VulkanContext const& vulkan_context, NonnullOwnPtr<skgpu::VulkanExtensions> extensions)
        : m_context(move(context))
        , m_extensions(move(extensions))
        , m_vulkan_context(vulkan_context)
    {
    }

    ~SkiaVulkanBackendContext() override { }

    void flush_and_submit(SkSurface* surface) override
    {
        GrFlushInfo const flush_info {};
        m_context->flush(surface, SkSurfaces::BackendSurfaceAccess::kPresent, flush_info);
        m_context->submit(GrSyncCpu::kYes);
    }

    skgpu::VulkanExtensions const* extensions() const { return m_extensions.ptr(); }

    GrDirectContext* sk_context() const override { return m_context.get(); }

    VulkanContext const& vulkan_context() override { return m_vulkan_context; }

    MetalContext& metal_context() override { VERIFY_NOT_REACHED(); }

private:
    sk_sp<GrDirectContext> m_context;
    NonnullOwnPtr<skgpu::VulkanExtensions> m_extensions;
    VulkanContext const m_vulkan_context;
};

RefPtr<SkiaBackendContext> SkiaBackendContext::create_vulkan_context(VulkanContext const& vulkan_context)
{
    skgpu::VulkanBackendContext backend_context;

    backend_context.fInstance = vulkan_context.instance;
    backend_context.fDevice = vulkan_context.logical_device;
    backend_context.fQueue = vulkan_context.graphics_queue;
    backend_context.fGraphicsQueueIndex = vulkan_context.graphics_queue_family;
    backend_context.fPhysicalDevice = vulkan_context.physical_device;
    backend_context.fMaxAPIVersion = vulkan_context.api_version;
    backend_context.fGetProc = [](char const* proc_name, VkInstance instance, VkDevice device) {
        if (device != VK_NULL_HANDLE) {
            return vkGetDeviceProcAddr(device, proc_name);
        }
        return vkGetInstanceProcAddr(instance, proc_name);
    };

    auto extensions = make<skgpu::VulkanExtensions>();
    backend_context.fVkExtensions = extensions.ptr();

    sk_sp<GrDirectContext> ctx = GrDirectContexts::MakeVulkan(backend_context);
    VERIFY(ctx);
    return adopt_ref(*new SkiaVulkanBackendContext(ctx, vulkan_context, move(extensions)));
}
#endif

#ifdef AK_MACOS_HAS_METAL
class SkiaMetalBackendContext final : public SkiaBackendContext {
    AK_MAKE_NONCOPYABLE(SkiaMetalBackendContext);
    AK_MAKE_NONMOVABLE(SkiaMetalBackendContext);

public:
    SkiaMetalBackendContext(sk_sp<GrDirectContext> context, NonnullRefPtr<MetalContext> metal_context)
        : m_context(move(context))
        , m_metal_context(move(metal_context))
    {
    }

    ~SkiaMetalBackendContext() override { }

    void flush_and_submit(SkSurface* surface) override
    {
        GrFlushInfo const flush_info {};
        m_context->flush(surface, SkSurfaces::BackendSurfaceAccess::kPresent, flush_info);
        m_context->submit(GrSyncCpu::kYes);
    }

    GrDirectContext* sk_context() const override { return m_context.get(); }

    VulkanContext const& vulkan_context() override { VERIFY_NOT_REACHED(); }

    MetalContext& metal_context() override { return m_metal_context; }

private:
    sk_sp<GrDirectContext> m_context;
    NonnullRefPtr<MetalContext> m_metal_context;
};

RefPtr<SkiaBackendContext> SkiaBackendContext::create_metal_context(NonnullRefPtr<MetalContext> metal_context)
{
    GrMtlBackendContext backend_context;
    backend_context.fDevice.retain(metal_context->device());
    backend_context.fQueue.retain(metal_context->queue());
    sk_sp<GrDirectContext> ctx = GrDirectContexts::MakeMetal(backend_context);
    return adopt_ref(*new SkiaMetalBackendContext(move(ctx), move(metal_context)));
}
#endif

#ifdef LADYBIRD_LEGACY_MACOS
class SkiaOpenGLBackendContext final : public SkiaBackendContext {
    AK_MAKE_NONCOPYABLE(SkiaOpenGLBackendContext);
    AK_MAKE_NONMOVABLE(SkiaOpenGLBackendContext);

public:
    SkiaOpenGLBackendContext(sk_sp<GrDirectContext> context, CGLContextObj cgl_context)
        : m_context(move(context))
        , m_cgl_context(cgl_context)
    {
    }

    // Runs at process exit, possibly while the rendering thread still draws: no OpenGL and no Skia call here.
    // The context and its resources go with the process.
    ~SkiaOpenGLBackendContext() override { (void)m_context.release(); }

    void flush_and_submit(SkSurface* surface) override
    {
        // The picture is read back right after this, and that waits for the drawing by itself.
        GrFlushInfo const flush_info {};
        m_context->flush(surface, SkSurfaces::BackendSurfaceAccess::kNoAccess, flush_info);
        m_context->submit(GrSyncCpu::kNo);
    }

    GrDirectContext* sk_context() const override { return m_context.get(); }

    VulkanContext const& vulkan_context() override { VERIFY_NOT_REACHED(); }

    MetalContext& metal_context() override { VERIFY_NOT_REACHED(); }

private:
    // Skia runs on the main thread (canvas, snapshots) and on the rendering thread (display lists); ANGLE keeps
    // its own CGL context current on the main thread between WebGL calls. A CGL context is current on one
    // thread at a time, so the previous context always comes back, also when it is none.
    void did_lock() override
    {
        m_saved_context = CGLGetCurrentContext();
        if (m_saved_context != m_cgl_context)
            CGLSetCurrentContext(m_cgl_context);
    }

    void will_unlock() override
    {
        if (m_saved_context != m_cgl_context)
            CGLSetCurrentContext(m_saved_context);
    }

    sk_sp<GrDirectContext> m_context;
    CGLContextObj m_cgl_context { nullptr };
    CGLContextObj m_saved_context { nullptr }; // written only by the thread that holds the lock
};

RefPtr<SkiaBackendContext> SkiaBackendContext::create_opengl_context()
{
    // The first pixel format is the one Skia drew right with on a GeForce 6600 under the OpenGL of Mac OS X 10.5.8;
    // the others take whatever accelerated renderer there is. Apple's software renderer is not asked for: Skia's
    // own raster backend is faster than that.
    auto attribute = [](int value) { return static_cast<CGLPixelFormatAttribute>(value); };
    CGLPixelFormatAttribute const accelerated_with_alpha[] = { kCGLPFAAccelerated, kCGLPFANoRecovery, kCGLPFAColorSize, attribute(24), kCGLPFAAlphaSize, attribute(8), attribute(0) };
    CGLPixelFormatAttribute const accelerated_without_recovery[] = { kCGLPFAAccelerated, kCGLPFANoRecovery, attribute(0) };
    CGLPixelFormatAttribute const accelerated[] = { kCGLPFAAccelerated, attribute(0) };
    CGLPixelFormatAttribute const* const attempts[] = { accelerated_with_alpha, accelerated_without_recovery, accelerated };

    CGLContextObj cgl_context = nullptr;
    char const* reason = "no accelerated pixel format";
    for (auto const* attributes : attempts) {
        CGLPixelFormatObj pixel_format = nullptr;
        GLint number_of_pixel_formats = 0;
        auto error = CGLChoosePixelFormat(attributes, &pixel_format, &number_of_pixel_formats);
        if (error != kCGLNoError || !pixel_format) {
            if (error != kCGLNoError)
                reason = CGLErrorString(error);
            continue;
        }
        error = CGLCreateContext(pixel_format, nullptr, &cgl_context);
        CGLDestroyPixelFormat(pixel_format);
        if (error == kCGLNoError && cgl_context)
            break;
        reason = CGLErrorString(error);
        dbgln("Unable to create an OpenGL context: {}", reason);
        cgl_context = nullptr;
    }
    if (!cgl_context) {
        dbgln("Painting with the CPU: no accelerated OpenGL context ({})", reason);
        return nullptr;
    }

    // Nothing of ours stays current outside the lock (see did_lock()).
    auto* previous_context = CGLGetCurrentContext();
    auto give_up = [&](auto message) {
        dbgln("Painting with the CPU: {}", message);
        CGLSetCurrentContext(previous_context);
        CGLDestroyContext(cgl_context);
        return nullptr;
    };

    if (auto error = CGLSetCurrentContext(cgl_context); error != kCGLNoError)
        return give_up(CGLErrorString(error));

    auto renderer = reinterpret_cast<char const*>(glGetString(GL_RENDERER));
    auto version = reinterpret_cast<char const*>(glGetString(GL_VERSION));

    auto gl_interface = GrGLInterfaces::MakeMac();
    if (!gl_interface)
        return give_up("Skia did not find the functions of OpenGL");

    GrContextOptions const options;
    auto context = GrDirectContexts::MakeGL(move(gl_interface), options);
    if (!context) {
        dbgln("Skia does not accept this OpenGL: {} {}", renderer, version);
        return give_up("no Skia context");
    }

    // Skia's default of 256 MiB is the whole video memory of a GeForce 6600, and every WebContent process has a
    // context of its own.
    static constexpr size_t resource_cache_limit = 64 * MiB;
    context->setResourceCacheLimit(resource_cache_limit);

    dbgln("Painting through OpenGL: {} {}", renderer, version);
    CGLSetCurrentContext(previous_context);
    return adopt_ref(*new SkiaOpenGLBackendContext(move(context), cgl_context));
}
#endif

}
