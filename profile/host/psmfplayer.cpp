// scePsmfPlayer for the web profile. Movies are not decoded: a player walks
// the states a game expects (created, standby, playing) and reports a started
// movie as already finished, so intros and cut-scenes are skipped instead of
// waiting forever for a status that never comes.

#include "kernel.hpp"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>

namespace pspweb {

namespace {
constexpr std::uint32_t kStatusNone = 0x0u;
constexpr std::uint32_t kStatusInitialized = 0x1u;
constexpr std::uint32_t kStatusStandby = 0x2u;
constexpr std::uint32_t kStatusPlayingFinished = 0x200u;
constexpr std::uint32_t kErrorNoMoreData = 0x8061600Cu;
} // namespace

void Kernel::install_psmfplayer() {
    auto &m = rt_.memory();
    auto players = std::make_shared<std::map<std::uint32_t, std::uint32_t>>(); // player address -> status
    psmfplayer_ = players;
    auto add = [this](std::uint32_t nid, const char *name, std::function<std::uint32_t(Ctx &)> body) {
        static const bool trace = std::getenv("PSPWEB_TRACE_HLE") != nullptr;
        rt_.register_hle("scePsmfPlayer", nid, [body, name](psprecomp::Runtime &, Ctx &ctx) {
            const std::uint32_t a0 = ctx.gpr[4];
            const std::uint32_t result = body(ctx);
            if (trace) std::fprintf(stderr, "[psmfplayer] %s(0x%X) -> 0x%X\n", name, a0, result);
            finish(ctx, result);
        });
        rt_.nids().add("scePsmfPlayer", nid, name);
        char key[48];
        std::snprintf(key, sizeof key, "scePsmfPlayer:0x%08X", nid);
        registered_.insert(key);
    };
    auto set = [players](std::uint32_t status) {
        return [players, status](Ctx &ctx) {
            (*players)[ctx.gpr[4]] = status;
            return 0u;
        };
    };
    auto ok = [](Ctx &) { return 0u; };

    add(0x235D8787u, "scePsmfPlayerCreate", set(kStatusInitialized));
    add(0x9B71A274u, "scePsmfPlayerDelete", [players](Ctx &ctx) {
        players->erase(ctx.gpr[4]);
        return 0u;
    });
    add(0x3D6D25A9u, "scePsmfPlayerSetPsmf", set(kStatusStandby));
    add(0x58B83577u, "scePsmfPlayerSetPsmfCB", set(kStatusStandby));
    add(0x76C0F4AEu, "scePsmfPlayerSetPsmfOffset", set(kStatusStandby));
    add(0xA72DB4F9u, "scePsmfPlayerSetPsmfOffsetCB", set(kStatusStandby));
    add(0xE792CD94u, "scePsmfPlayerReleasePsmf", set(kStatusInitialized));
    add(0x95A84EE5u, "scePsmfPlayerStart", set(kStatusPlayingFinished)); // skipped: over as soon as it starts
    add(0x1078C008u, "scePsmfPlayerStop", set(kStatusStandby));
    add(0x2BEB1569u, "scePsmfPlayerBreak", ok);
    add(0xA0B8CA55u, "scePsmfPlayerUpdate", ok);
    add(0xF8EF08A6u, "scePsmfPlayerGetCurrentStatus", [players](Ctx &ctx) {
        const auto it = players->find(ctx.gpr[4]);
        return it == players->end() ? kStatusNone : it->second;
    });
    add(0x46F61F8Bu, "scePsmfPlayerGetVideoData", [](Ctx &) { return kErrorNoMoreData; });
    add(0xB9848A74u, "scePsmfPlayerGetAudioData", [](Ctx &) { return kErrorNoMoreData; });
    add(0x3EA82A4Bu, "scePsmfPlayerGetAudioOutSize", [](Ctx &) { return 2048u * 4u; }); // 2048 stereo samples
    add(0x1E57A8E7u, "scePsmfPlayerConfigPlayer", ok);
    add(0x2D0E4E0Au, "scePsmfPlayerSetTempBuf", ok);
    add(0xA3D81169u, "scePsmfPlayerChangePlayMode", ok);
    for (const std::uint32_t nid : {0x75F03FA2u, 0x85461EFFu, 0x8A9EBDCDu, 0xB8D10C56u})
        add(nid, "scePsmfPlayerSelectStream", ok);
    // Out-parameter getters: zeroed answers (no streams, time 0).
    auto zero_out = [&m](std::uint32_t words) {
        return [&m, words](Ctx &ctx) {
            for (std::uint32_t i = 0; ctx.gpr[5] != 0u && i < words; ++i) m.store32(ctx.gpr[5] + i * 4u, 0u);
            return 0u;
        };
    };
    add(0xDF089680u, "scePsmfPlayerGetPsmfInfo", zero_out(4u));
    add(0x3ED62233u, "scePsmfPlayerGetCurrentPts", zero_out(1u));
    add(0x68F07175u, "scePsmfPlayerGetCurrentAudioStream", zero_out(1u));
    add(0x9FF2B2E7u, "scePsmfPlayerGetCurrentVideoStream", zero_out(1u));
    add(0xF3EFAA91u, "scePsmfPlayerGetCurrentPlayMode", zero_out(1u));
}

} // namespace pspweb
