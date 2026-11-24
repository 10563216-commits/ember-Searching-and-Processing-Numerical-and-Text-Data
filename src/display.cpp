// Екран 64 × 32, по біту на піксель. Це не окремий масив: це mem.data[0xA00 .. 0xAFF].
//
//   адреса байта = 0xA00 + y * 8 + (x / 8)
//   номер біта   = 7 - (x % 8)          (біт 7 — найлівіший піксель)
//   горить       = (mem[адреса] >> номер) & 1

#include "ember.hpp"

namespace {
Style g_style = Style::Ascii;

// Рядки записані шістнадцятковими байтами, щоб файл збирався однаково
// незалежно від того, у якій кодуванні його зберіг редактор.
const char* const BLOCK_ON = "\xE2\x96\x88";   // █  U+2588
const char* const BLOCK_OFF = "\xC2\xB7";      // ·  U+00B7
}  // namespace

void set_style(Style s) { g_style = s; }

// show() — читаємо формулу «байт і біт» у бік «байт → піксель».
void show(const Memory& mem, std::ostream& out) {
    const char* on = (g_style == Style::Block) ? BLOCK_ON : "#";
    const char* off = (g_style == Style::Block) ? BLOCK_OFF : ".";
    for (int y = 0; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            Byte cell = mem.data[VRAM_LO + y * ROW_BYTES + (x / 8)];
            bool lit = (cell >> (7 - (x % 8))) & 1;
            out << (lit ? on : off);
        }
        out << '\n';
    }
}

// plot() — та сама формула навпаки: не читаємо біт, а виставляємо.
bool plot(Memory& mem, int x, int y) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return false;
    int addr = VRAM_LO + y * ROW_BYTES + (x / 8);
    mem.data[addr] |= static_cast<Byte>(1u << (7 - (x % 8)));
    return true;
}

bool unplot(Memory& mem, int x, int y) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return false;
    int addr = VRAM_LO + y * ROW_BYTES + (x / 8);
    mem.data[addr] &= static_cast<Byte>(~(1u << (7 - (x % 8))));
    return true;
}

void clear(Memory& mem) {
    for (int i = 0; i < SCREEN_H * ROW_BYTES; ++i) mem.data[VRAM_LO + i] = 0;
}

void draw_frame(Memory& mem) {
    for (int x = 0; x < SCREEN_W; ++x) {
        plot(mem, x, 0);
        plot(mem, x, SCREEN_H - 1);
    }
    for (int y = 0; y < SCREEN_H; ++y) {
        plot(mem, 0, y);
        plot(mem, SCREEN_W - 1, y);
    }
}
