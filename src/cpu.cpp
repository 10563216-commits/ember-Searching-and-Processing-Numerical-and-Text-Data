// Процесор ember: регістри, прапорці й step(). Опкоди — з ISA.uk.md (до Lab 5).

#include <cstdio>
#include <sstream>

#include "ember.hpp"

namespace {
std::string hex(unsigned v, int width) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*X", width, v);
    return buf;
}
}  // namespace

Cpu::Cpu(Memory& m, std::ostream& o) : mem(m), out(o) {}

void Cpu::reset() {
    A = B = 0;
    H = 0;
    PC = 0;
    SP = 0xFFF;
    Z = N = C = false;
    halted = false;
    steps = 0;
    error.clear();
}

void Cpu::put(const char* p, std::size_t n) {
    if (n == 0) return;
    out.write(p, static_cast<std::streamsize>(n));
    at_line_start = (p[n - 1] == '\n');
}

Byte Cpu::fetch() {
    if (PC >= MEM_SIZE) throw Fault{"PC = 0x" + hex(PC, 4) + " поза пам'яттю"};
    return mem.data[PC++];
}

Word Cpu::fetch16() {
    Byte lo = fetch();
    Byte hi = fetch();
    return static_cast<Word>(lo | (hi << 8));  // молодший байт уперед
}

Byte Cpu::rd(Word addr) {
    if (addr >= MEM_SIZE) throw Fault{"читання з 0x" + hex(addr, 4) + " поза пам'яттю"};
    if (addr == TICKS) return static_cast<Byte>(steps & 0xFF);  // з урахуванням цієї інструкції
    return mem.data[addr];
}

void Cpu::wr(Word addr, Byte v) {
    if (addr >= MEM_SIZE) throw Fault{"запис у 0x" + hex(addr, 4) + " поза пам'яттю"};
    mem.data[addr] = v;
    if (addr == PORT_OUT) {  // порт виводу: сам запис є дією
        char c = static_cast<char>(v);
        put(&c, 1);
    }
}

// OUTS addr: друкує байти до '\0' АБО до стелі в 256 байтів.
// Байти йдуть у термінал як є, тож UTF-8 «просто працює». Єдина турбота —
// стеля не повинна розрізати символ навпіл.
void Cpu::do_outs(Word addr) {
    if (addr >= MEM_SIZE) throw Fault{"OUTS з 0x" + hex(addr, 4) + " поза пам'яттю"};
    std::string s;
    bool terminated = false;
    for (int i = 0; i < OUTS_CEILING; ++i) {
        unsigned a = static_cast<unsigned>(addr) + static_cast<unsigned>(i);
        if (a >= MEM_SIZE) break;  // край пам'яті — теж край рядка
        Byte b = mem.data[a];
        if (b == 0) {
            terminated = true;
            break;
        }
        s.push_back(static_cast<char>(b));
    }
    if (!terminated) s.resize(utf8_complete_prefix(s));
    put(s);
}

bool Cpu::step() {
    if (halted) return false;
    ++steps;
    Word at = PC;
    try {
        exec();
    } catch (const Fault& f) {
        error = f.msg + " (інструкція на 0x" + hex(at, 4) + ")";
        halted = true;
        return false;
    }
    return true;
}

unsigned long long Cpu::run(unsigned long long max_steps) {
    unsigned long long n = 0;
    while (n < max_steps && step()) ++n;
    return n;
}

void Cpu::exec() {
    auto zn = [this](Byte r) {
        Z = (r == 0);
        N = (r & 0x80) != 0;
    };

    Byte op = fetch();
    switch (op) {
        // ---- 0x0_ керування й вивід ----
        case 0x00: halted = true; break;
        case 0x01: break;
        case 0x02: {
            char c = static_cast<char>(A);
            put(&c, 1);
            break;
        }
        case 0x03: put(std::to_string(A) + " "); break;
        case 0x04: do_outs(fetch16()); break;

        // ---- 0x1_ ALU ----
        case 0x10: {
            unsigned r = A + B;
            C = r > 0xFF;
            A = static_cast<Byte>(r);
            zn(A);
            break;
        }
        case 0x11: {
            C = A < B;
            A = static_cast<Byte>(A - B);
            zn(A);
            break;
        }
        case 0x12: A &= B; C = false; zn(A); break;
        case 0x13: A |= B; C = false; zn(A); break;
        case 0x14: A ^= B; C = false; zn(A); break;
        case 0x15: A = static_cast<Byte>(~A); C = false; zn(A); break;
        case 0x16: C = (A & 0x80) != 0; A = static_cast<Byte>(A << 1); zn(A); break;
        case 0x17: C = (A & 0x01) != 0; A = static_cast<Byte>(A >> 1); zn(A); break;
        case 0x18: A = static_cast<Byte>(A + 1); zn(A); break;
        case 0x19: A = static_cast<Byte>(A - 1); zn(A); break;
        case 0x1A: {
            C = A < B;
            zn(static_cast<Byte>(A - B));  // результат викидаємо
            break;
        }

        // ---- 0x2_ пересилання даних ----
        case 0x20: A = fetch(); break;
        case 0x21: B = fetch(); break;
        case 0x22: A = rd(fetch16()); break;
        case 0x23: B = rd(fetch16()); break;
        case 0x24: wr(fetch16(), A); break;
        case 0x25: wr(fetch16(), B); break;
        case 0x26: A = B; break;
        case 0x27: B = A; break;
        case 0x28: H = fetch16(); break;
        case 0x29: A = rd(H); break;
        case 0x2A: wr(H, A); break;
        case 0x2B: H = static_cast<Word>(H + 1); break;
        case 0x2C: H = static_cast<Word>(H - 1); break;
        case 0x2D: A = static_cast<Byte>(H & 0xFF); break;

        // ---- 0x3_ стрибки: операнд читаємо завжди, PC міняємо лише коли спрацювало ----
        case 0x30: PC = fetch16(); break;
        case 0x31: { Word t = fetch16(); if (Z) PC = t; break; }
        case 0x32: { Word t = fetch16(); if (!Z) PC = t; break; }
        case 0x33: { Word t = fetch16(); if (C) PC = t; break; }
        case 0x34: { Word t = fetch16(); if (!C) PC = t; break; }

        // ---- 0x4_ екран ----
        case 0x40: clear(mem); break;
        case 0x41: C = !plot(mem, A, B); break;    // x = A, y = B
        case 0x42: C = !unplot(mem, A, B); break;
        case 0x43: {
            std::ostringstream ss;
            show(mem, ss);
            put(ss.str());
            break;
        }

        default:
            throw Fault{"невідомий опкод 0x" + hex(op, 2)};
    }
}
