// ember — маленька вигадана машина. Lab 05: масиви, рядки, екран.
// Усе, що стосується карти пам'яті, лежить тут, щоб не розходилось між файлами.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using Byte = std::uint8_t;
using Word = std::uint16_t;

// ---- карта пам'яті (ISA.uk.md §2) -----------------------------------------
constexpr int  MEM_SIZE = 4096;
constexpr Word CODE_LO = 0x000, CODE_HI = 0x7FF;
constexpr Word DATA_LO = 0x800, DATA_HI = 0x9FF;
constexpr Word VRAM_LO = 0xA00, VRAM_HI = 0xAFF;
constexpr Word PORT_OUT = 0xB00;  // opt: запис гостя одразу друкує символ
constexpr Word TICKS = 0xB01;     // opt: читання гостя дає лічильник інструкцій

constexpr int SCREEN_W = 64;
constexpr int SCREEN_H = 32;
constexpr int ROW_BYTES = SCREEN_W / 8;  // крок рядка: 8 байтів

constexpr int OUTS_CEILING = 256;  // стеля OUTS

// Уся пам'ять машини — один масив. Екран — це data[0xA00 .. 0xAFF].
struct Memory {
    Byte data[MEM_SIZE] = {};
};

// ---- display.cpp -----------------------------------------------------------
enum class Style { Ascii, Block };  // '#' і '.'  або  '█' і '·' (UTF-8)
void set_style(Style s);
void show(const Memory& mem, std::ostream& out = std::cout);
bool plot(Memory& mem, int x, int y);    // false, якщо поза екраном
bool unplot(Memory& mem, int x, int y);  // false, якщо поза екраном
void clear(Memory& mem);
void draw_frame(Memory& mem);  // рамка по краях екрана, малюється з C++

// ---- text.cpp: UTF-8 і рядки -----------------------------------------------
void console_init();                                  // Windows: кодова сторінка UTF-8
bool utf8_valid(const std::string& s);                // чи це коректний UTF-8
std::size_t utf8_length(const std::string& s);        // кількість символів (не байтів)
std::size_t utf8_complete_prefix(const std::string& s);  // без обрізаного символу в кінці
bool unescape(const std::string& in, std::string& out, std::string& err);  // \n \t \\ \" \0 \xHH

// ---- asm.cpp: мінімальний асемблер -----------------------------------------
struct AsmResult {
    bool ok = false;
    int bytes = 0;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::map<std::string, int> labels;
};
// Збирає текст програми й, лише якщо помилок немає, кладе байти в mem.
AsmResult assemble(const std::string& source, Memory& mem);

// ---- cpu.cpp ---------------------------------------------------------------
struct Fault {
    std::string msg;
};

struct Cpu {
    Memory& mem;
    std::ostream& out;
    Byte A = 0, B = 0;
    Word H = 0, PC = 0, SP = 0xFFF;
    bool Z = false, N = false, C = false;
    bool halted = false;
    unsigned long long steps = 0;
    std::string error;          // непорожній, якщо процесор зупинила помилка
    bool at_line_start = true;  // чи закінчився останній вивід переносом рядка

    Cpu(Memory& m, std::ostream& o);
    void reset();
    bool step();  // true, якщо інструкція виконана; false, якщо вже зупинено
    unsigned long long run(unsigned long long max_steps);

private:
    void exec();
    Byte fetch();
    Word fetch16();
    Byte rd(Word addr);           // читання гостя (тут живе TICKS)
    void wr(Word addr, Byte v);   // запис гостя (тут живе PORT_OUT)
    void put(const char* p, std::size_t n);
    void put(const std::string& s) { put(s.data(), s.size()); }
    void do_outs(Word addr);
};
