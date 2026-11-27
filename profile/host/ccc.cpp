// sceCcc for the web profile: the firmware's UTF-8/UTF-16 helpers. Games walk
// strings with the Decode calls, which advance a pointer held in guest memory;
// left unimplemented, that pointer never moves and the caller loops forever.
// Shift-JIS conversions are not implemented (fallback returns 0).

#include "kernel.hpp"

#include <cstdio>
#include <functional>

namespace pspweb {

void Kernel::install_ccc() {
    auto &m = rt_.memory();
    auto add = [this](std::uint32_t nid, const char *name, std::function<std::uint32_t(Ctx &)> body) {
        rt_.register_hle("sceCcc", nid, [body](psprecomp::Runtime &, Ctx &ctx) { finish(ctx, body(ctx)); });
        rt_.nids().add("sceCcc", nid, name);
        char key[48];
        std::snprintf(key, sizeof key, "sceCcc:0x%08X", nid);
        registered_.insert(key);
    };
    // One UTF-8 sequence at address; returns the code point and its length.
    auto decode8 = [&m](std::uint32_t address, std::uint32_t &length) -> std::uint32_t {
        const std::uint32_t lead = m.load8(address);
        length = lead < 0x80u ? 1u : lead >= 0xF0u ? 4u : lead >= 0xE0u ? 3u : lead >= 0xC0u ? 2u : 1u;
        if (length == 1u) return lead;
        std::uint32_t code = lead & (0x7Fu >> length);
        for (std::uint32_t i = 1; i < length; ++i) {
            const std::uint32_t next = m.load8(address + i);
            if ((next & 0xC0u) != 0x80u) {
                length = i;
                return 0xFFFDu;
            }
            code = (code << 6u) | (next & 0x3Fu);
        }
        return code;
    };
    auto decode16 = [&m](std::uint32_t address, std::uint32_t &units) -> std::uint32_t {
        const std::uint32_t high = m.load16(address);
        units = 1u;
        if (high < 0xD800u || high > 0xDBFFu) return high;
        const std::uint32_t low = m.load16(address + 2u);
        if (low < 0xDC00u || low > 0xDFFFu) return high;
        units = 2u;
        return 0x10000u + ((high - 0xD800u) << 10u) + (low - 0xDC00u);
    };
    auto encode8 = [&m](std::uint32_t address, std::uint32_t code) -> std::uint32_t {
        if (code < 0x80u) {
            m.store8(address, static_cast<std::uint8_t>(code));
            return 1u;
        }
        const std::uint32_t length = code < 0x800u ? 2u : code < 0x10000u ? 3u : 4u;
        static constexpr std::uint8_t kLead[] = {0, 0, 0xC0u, 0xE0u, 0xF0u};
        for (std::uint32_t i = length - 1u; i > 0u; --i, code >>= 6u)
            m.store8(address + i, static_cast<std::uint8_t>(0x80u | (code & 0x3Fu)));
        m.store8(address, static_cast<std::uint8_t>(kLead[length] | code));
        return length;
    };
    auto encode16 = [&m](std::uint32_t address, std::uint32_t code) -> std::uint32_t {
        if (code < 0x10000u) {
            m.store16(address, static_cast<std::uint16_t>(code));
            return 1u;
        }
        code -= 0x10000u;
        m.store16(address, static_cast<std::uint16_t>(0xD800u + (code >> 10u)));
        m.store16(address + 2u, static_cast<std::uint16_t>(0xDC00u + (code & 0x3FFu)));
        return 2u;
    };

    add(0xB4D1CBBFu, "sceCccSetTable", [](Ctx &) { return 0u; });
    add(0xC6A8BEE2u, "sceCccDecodeUTF8", [&m, decode8](Ctx &ctx) {
        const std::uint32_t at = m.load32(ctx.gpr[4]);
        if (at == 0u) return 0u;
        std::uint32_t length = 0u;
        const std::uint32_t code = decode8(at, length);
        m.store32(ctx.gpr[4], at + length);
        return code;
    });
    add(0xE0CF8091u, "sceCccDecodeUTF16", [&m, decode16](Ctx &ctx) {
        const std::uint32_t at = m.load32(ctx.gpr[4]);
        if (at == 0u) return 0u;
        std::uint32_t units = 0u;
        const std::uint32_t code = decode16(at, units);
        m.store32(ctx.gpr[4], at + units * 2u);
        return code;
    });
    add(0x92C05851u, "sceCccEncodeUTF8", [&m, encode8](Ctx &ctx) {
        const std::uint32_t at = m.load32(ctx.gpr[4]);
        if (at != 0u) m.store32(ctx.gpr[4], at + encode8(at, ctx.gpr[5]));
        return 0u;
    });
    add(0x8406F469u, "sceCccEncodeUTF16", [&m, encode16](Ctx &ctx) {
        const std::uint32_t at = m.load32(ctx.gpr[4]);
        if (at != 0u) m.store32(ctx.gpr[4], at + encode16(at, ctx.gpr[5]) * 2u);
        return 0u;
    });
    add(0xB7D3C112u, "sceCccStrlenUTF8", [&m, decode8](Ctx &ctx) {
        std::uint32_t count = 0u, length = 0u;
        for (std::uint32_t at = ctx.gpr[4]; at != 0u && m.load8(at) != 0u; at += length) {
            decode8(at, length);
            ++count;
        }
        return count;
    });
    add(0x4BDEB2A8u, "sceCccStrlenUTF16", [&m, decode16](Ctx &ctx) {
        std::uint32_t count = 0u, units = 0u;
        for (std::uint32_t at = ctx.gpr[4]; at != 0u && m.load16(at) != 0u; at += units * 2u) {
            decode16(at, units);
            ++count;
        }
        return count;
    });
    // (dst, dst size in bytes, src): converts up to the terminator, terminates
    // the output when it fits and returns the characters converted.
    add(0x00D1378Fu, "sceCccUTF8toUTF16", [&m, decode8, encode16](Ctx &ctx) {
        const std::uint32_t dst = ctx.gpr[4], end = dst + (ctx.gpr[5] & ~1u);
        std::uint32_t out = dst, count = 0u, length = 0u;
        for (std::uint32_t at = ctx.gpr[6]; at != 0u && m.load8(at) != 0u; at += length) {
            const std::uint32_t code = decode8(at, length);
            if (out + (code < 0x10000u ? 2u : 4u) > end) break;
            out += encode16(out, code) * 2u;
            ++count;
        }
        if (out + 2u <= end) m.store16(out, 0u);
        return count;
    });
    add(0x41B724A5u, "sceCccUTF16toUTF8", [&m, decode16, encode8](Ctx &ctx) {
        const std::uint32_t dst = ctx.gpr[4], end = dst + ctx.gpr[5];
        std::uint32_t out = dst, count = 0u, units = 0u;
        for (std::uint32_t at = ctx.gpr[6]; at != 0u && m.load16(at) != 0u; at += units * 2u) {
            const std::uint32_t code = decode16(at, units);
            if (out + (code < 0x80u ? 1u : code < 0x800u ? 2u : code < 0x10000u ? 3u : 4u) > end) break;
            out += encode8(out, code);
            ++count;
        }
        if (out < end) m.store8(out, 0u);
        return count;
    });
}

} // namespace pspweb
