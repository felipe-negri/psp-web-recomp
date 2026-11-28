// Virtual timers (sceKernel*VTimer*) as stopwatches: a timer counts
// microseconds while started. Handlers that fire on a schedule are not
// implemented. Tekken 6 times its loading screen with one.

#include "kernel.hpp"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>

namespace pspweb {

namespace {
struct VTimer {
    bool running{};
    std::uint64_t base_us{};    // when it was started
    std::uint64_t elapsed_us{}; // accumulated while stopped
};
constexpr std::uint32_t kErrorUnknownVTimer = 0x800201A9u;
} // namespace

void Kernel::install_vtimers() {
    auto &m = rt_.memory();
    auto timers = std::make_shared<std::map<std::uint32_t, VTimer>>();
    vtimers_ = timers;
    auto add = [this](std::uint32_t nid, const char *name, std::function<std::uint64_t(Ctx &)> body, bool wide = false) {
        rt_.register_hle("ThreadManForUser", nid, [body, wide](psprecomp::Runtime &, Ctx &ctx) {
            const std::uint64_t result = body(ctx);
            if (wide) finish64(ctx, result);
            else finish(ctx, static_cast<std::uint32_t>(result));
        });
        rt_.nids().add("ThreadManForUser", nid, name);
        char key[48];
        std::snprintf(key, sizeof key, "ThreadManForUser:0x%08X", nid);
        registered_.insert(key);
    };
    auto time_of = [this](const VTimer &t) { return t.elapsed_us + (t.running ? now_us() - t.base_us : 0u); };
    auto store64 = [&m](std::uint32_t address, std::uint64_t value) {
        if (address == 0u) return;
        m.store32(address, static_cast<std::uint32_t>(value));
        m.store32(address + 4u, static_cast<std::uint32_t>(value >> 32u));
    };

    add(0x20FFF560u, "sceKernelCreateVTimer", [this, timers](Ctx &) {
        const std::uint32_t uid = static_cast<std::uint32_t>(new_uid());
        (*timers)[uid] = VTimer{};
        return std::uint64_t{uid};
    });
    add(0x328F9E52u, "sceKernelDeleteVTimer", [timers](Ctx &ctx) {
        return std::uint64_t{timers->erase(ctx.gpr[4]) != 0u ? 0u : kErrorUnknownVTimer};
    });
    add(0xC68D9437u, "sceKernelStartVTimer", [this, timers](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        if (it == timers->end()) return kErrorUnknownVTimer;
        if (it->second.running) return 1u; // already running
        it->second.running = true;
        it->second.base_us = now_us();
        return 0u;
    });
    add(0xD0AEEE87u, "sceKernelStopVTimer", [timers, time_of](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        if (it == timers->end()) return kErrorUnknownVTimer;
        if (!it->second.running) return 0u;
        it->second.elapsed_us = time_of(it->second);
        it->second.running = false;
        return 1u;
    });
    add(0x034A921Fu, "sceKernelGetVTimerTime", [timers, time_of, store64](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        if (it == timers->end()) return kErrorUnknownVTimer;
        store64(ctx.gpr[5], time_of(it->second));
        return 0u;
    });
    add(0xC0B3FFD2u, "sceKernelGetVTimerTimeWide", [timers, time_of](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        return it == timers->end() ? 0u : time_of(it->second);
    }, true);
    add(0xB3A59970u, "sceKernelGetVTimerBase", [timers, store64](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        if (it == timers->end()) return kErrorUnknownVTimer;
        store64(ctx.gpr[5], it->second.base_us);
        return 0u;
    });
    add(0xB7C18B77u, "sceKernelGetVTimerBaseWide", [timers](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        return it == timers->end() ? 0u : it->second.base_us;
    }, true);
    // Set*: (uid, u64 *time) and (uid, u64 time in a2:a3); both return the previous time.
    add(0x542AD630u, "sceKernelSetVTimerTime", [this, &m, timers, time_of, store64](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        if (it == timers->end() || ctx.gpr[5] == 0u) return kErrorUnknownVTimer;
        const std::uint64_t wanted = m.load32(ctx.gpr[5]) | (static_cast<std::uint64_t>(m.load32(ctx.gpr[5] + 4u)) << 32u);
        store64(ctx.gpr[5], time_of(it->second));
        it->second.elapsed_us = wanted;
        it->second.base_us = now_us();
        return 0u;
    });
    add(0xFB6425C3u, "sceKernelSetVTimerTimeWide", [this, timers, time_of](Ctx &ctx) -> std::uint64_t {
        const auto it = timers->find(ctx.gpr[4]);
        if (it == timers->end()) return 0u;
        const std::uint64_t previous = time_of(it->second);
        it->second.elapsed_us = ctx.gpr[6] | (static_cast<std::uint64_t>(ctx.gpr[7]) << 32u);
        it->second.base_us = now_us();
        return previous;
    }, true);
    add(0xD2D615EFu, "sceKernelCancelVTimerHandler", [](Ctx &) { return std::uint64_t{0u}; });
}

} // namespace pspweb
