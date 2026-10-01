/*
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/Noncopyable.h>
#include <LibThreading/Mutex.h>

#ifdef USE_VULKAN
#    include <LibGfx/VulkanContext.h>
#endif

#ifdef AK_MACOS_HAS_METAL
#    include <LibGfx/MetalContext.h>
#endif

class GrDirectContext;
class SkSurface;

namespace Gfx {

struct VulkanContext;
class MetalContext;

class SkiaBackendContext : public AtomicRefCounted<SkiaBackendContext> {
    AK_MAKE_NONCOPYABLE(SkiaBackendContext);
    AK_MAKE_NONMOVABLE(SkiaBackendContext);

public:
#ifdef USE_VULKAN
    static RefPtr<SkiaBackendContext> create_vulkan_context(const VulkanContext& vulkan_context);
#endif

#ifdef AK_MACOS_HAS_METAL
    static RefPtr<SkiaBackendContext> create_metal_context(NonnullRefPtr<MetalContext>);
#endif

    static void initialize_gpu_backend();
    static RefPtr<SkiaBackendContext> the();

    SkiaBackendContext() { }
    virtual ~SkiaBackendContext() { }

    virtual void flush_and_submit(SkSurface*) { }
    virtual GrDirectContext* sk_context() const = 0;

    virtual MetalContext& metal_context() = 0;
    virtual VulkanContext const& vulkan_context() = 0;

    // The mutex is recursive. The hooks run with it held, on the outermost lock() and before the outermost
    // unlock(): an OpenGL context belongs to one thread at a time, so an OpenGL backend makes its context
    // current there and puts the previous one back.
    void lock()
    {
        m_mutex.lock();
        if (m_lock_depth++ == 0)
            did_lock();
    }
    void unlock()
    {
        VERIFY(m_lock_depth > 0);
        if (--m_lock_depth == 0)
            will_unlock();
        m_mutex.unlock();
    }

protected:
    virtual void did_lock() { }
    virtual void will_unlock() { }

private:
    Threading::Mutex m_mutex;
    unsigned m_lock_depth { 0 }; // written only by the thread that holds m_mutex
};

}
