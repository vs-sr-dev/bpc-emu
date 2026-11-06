// bpc-emu's oracle: wskit's WonderSwan interpreter (same author, ../wskit) run as a black box,
// writing at every frame the line bpc-cli --log writes, from the same start and the same
// button script. Not part of the emulator: built against wskit's runtime by build.sh.
//
//   oracle ROM.pc2 --frames N [--keys FILE] [--log FILE] [--ws-boot]
//
// The start is made the same as bpc-emu's: wskit's reset writes the ports the WonderSwan's
// boot leaves; here they are written back to 0 (the cartridge's excepted), the speaker is
// what is connected, and in the PCV2's boot the CPU starts at 4000:0000 with DS = FF00.
// The key matrix word (row $10 in bits 0-3, $20 in 4-7, $40 in 8-11) becomes wskit's pad;
// row $40's bit 0 (the PCV2's Left) has no WonderSwan button and is lost.
#include "ws.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::vector<std::pair<long, uint16_t>> g_keys;
static FILE* g_log;
static long g_frames = 600;

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static uint16_t keys_at(long f) {
    uint16_t k = 0;
    for (auto& e : g_keys) if (e.first <= f) k = e.second;
    return k;
}

static uint16_t to_ws(uint16_t m) {
    return (uint16_t)(((m & 0xF) << 4) | ((m >> 4) & 0xF) | (((m >> 9) & 7) << 8));
}

static bool parse(const char* s, uint16_t& out) {
    static const struct { const char* n; uint16_t b; } names[] = {
        { "up", 0x800 }, { "down", 0x400 }, { "left", 0x100 }, { "right", 0x080 }, { "circle", 0x004 },
        { "clear", 0x001 }, { "pass", 0x008 }, { "view", 0x010 }, { "esc", 0x040 }, { "none", 0 } };
    char* end;
    unsigned long v = std::strtoul(s, &end, 16);
    if (*end == 0 && end != s) { out = (uint16_t)v; return true; }
    std::string t = s; out = 0;
    size_t p = 0;
    while (p <= t.size()) {
        size_t q = t.find('+', p); if (q == std::string::npos) q = t.size();
        std::string w = t.substr(p, q - p); bool ok = false;
        for (auto& n : names) if (w == n.n) { out |= n.b; ok = true; }
        if (!ok) return false;
        p = q + 1;
    }
    return true;
}

static void hook(WS& c) {
    long f = (long)c.frame;
    if (g_log) {
        uint64_t h0 = 1469598103934665603ull;
        std::fprintf(g_log, "%llu %llu", (unsigned long long)c.frame, (unsigned long long)c.cyc);
        for (int i = 0; i < 8; i++) std::fprintf(g_log, " %04X", c.r.w[i]);
        std::fprintf(g_log, " %04X %04X %04X %04X %04X %04X", c.sr[ES], c.sr[CS], c.sr[SS], c.sr[DS], c.ip, ws_flags(c));
        static uint8_t fb[VIS_H][VIS_W];
        for (int y = 0; y < VIS_H; y++) for (int x = 0; x < VIS_W; x++) fb[y][x] = (uint8_t)(c.fb[y][x] & 15);
        std::fprintf(g_log, " %016llx %016llx %016llx\n", (unsigned long long)fnv(h0, c.ram, 0x4000),
                     (unsigned long long)fnv(h0, c.nv, c.nv_size), (unsigned long long)fnv(h0, fb, sizeof fb));
    }
    c.keys = to_ws(keys_at(f));
    if (f >= g_frames) throw WSStop();
}

int main(int argc, char** argv) {
    const char *rom_path = nullptr, *keys = nullptr, *log = nullptr;
    bool ws_boot = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : "";
        if (a == "--frames") { g_frames = std::atol(v); i++; }
        else if (a == "--keys") { keys = v; i++; }
        else if (a == "--log") { log = v; i++; }
        else if (a == "--ws-boot") ws_boot = true;
        else rom_path = argv[i];
    }
    if (!rom_path) { std::fprintf(stderr, "oracle ROM --frames N [--keys F] [--log F] [--ws-boot]\n"); return 2; }
    if (keys) {
        FILE* f = std::fopen(keys, "r");
        if (!f) { std::fprintf(stderr, "cannot read %s\n", keys); return 1; }
        char line[256], b[128]; long fr;
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#' || std::sscanf(line, "%ld %127s", &fr, b) != 2) continue;
            uint16_t k;
            if (!parse(b, k)) { std::fprintf(stderr, "unknown buttons %s\n", b); return 1; }
            g_keys.push_back({ fr, k });
        }
        std::fclose(f);
    }
    FILE* f = std::fopen(rom_path, "rb");
    if (!f) return 1;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(n);
    if (std::fread(rom.data(), 1, n, f) != (size_t)n) return 1;
    std::fclose(f);
    const uint8_t* foot = rom.data() + n - 16;
    uint32_t nv = 0;
    switch (foot[11]) {
    case 0x01: nv = 0x2000; break; case 0x02: nv = 0x8000; break; case 0x03: nv = 0x20000; break;
    case 0x04: nv = 0x40000; break; case 0x05: nv = 0x10000; break;
    }
    static WS c;
    ws_load(c, rom.data(), (uint32_t)n, false, nv ? SLOT_SRAM : SLOT_ROM, nv, false);
    ws_reset(c);
    for (int p = 0; p < 0xC0; p++) ws_out8(c, (uint16_t)p, 0);
    c.ser_data = 0;
    c.cyc = 0;
    c.s.hp_conn = 0;
    if (!ws_boot) { c.sr[CS] = 0x4000; c.ip = 0; c.sr[DS] = 0xFF00; }
    ws_recalc(c);
    g_log = log ? std::fopen(log, "w") : nullptr;
    c.natives = false;
    c.flat_stop = true;
    c.frame_hook = hook;
    try { for (;;) if (!ws_flat_frame(c) && c.jammed) break; } catch (WSStop&) {}
    if (g_log) std::fclose(g_log);
    std::printf("frames %llu cycles %llu  CS:IP %04X:%04X\n", (unsigned long long)c.frame,
                (unsigned long long)c.cyc, c.sr[CS], c.ip);
    return 0;
}
