// ember — REPL. Команди читаються зі stdin або зі скрипта: ./build/ember [-q] [скрипт]

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "ember.hpp"

#ifdef _WIN32
#include <io.h>
#define EMBER_ISATTY() _isatty(_fileno(stdin))
#else
#include <unistd.h>
#define EMBER_ISATTY() isatty(0)
#endif

namespace {

std::string hex(unsigned v, int width) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*X", width, v);
    return buf;
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Десяткове або 0x-шістнадцяткове число, можливо з мінусом.
bool parse_num(const std::string& s, long& v) {
    if (s.empty()) return false;
    const char* p = s.c_str();
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    if (!*p) return false;
    char* end = nullptr;
    errno = 0;
    long r = std::strtol(p, &end, base);
    if (*end || errno) return false;
    v = neg ? -r : r;
    return true;
}

// Байт у шістнадцятковому записі: «48» або «0x48».
bool parse_hexbyte(const std::string& s, Byte& b) {
    const char* p = s.c_str();
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!*p) return false;
    char* end = nullptr;
    long r = std::strtol(p, &end, 16);
    if (*end || r < 0 || r > 255) return false;
    b = static_cast<Byte>(r);
    return true;
}

class Repl {
public:
    Repl() : cpu(mem, std::cout) {}

    // false — вийти
    bool exec(const std::string& raw_line) {
        std::string line = trim(raw_line);
        if (line.empty() || line[0] == '#') return true;
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        std::vector<std::string> args;
        for (std::string a; in >> a;) args.push_back(a);
        std::string rest = trim(line.substr(cmd.size()));

        if (cmd == "quit" || cmd == "exit" || cmd == "q") return false;
        if (cmd == "help" || cmd == "?") help();
        else if (cmd == "regs") regs();
        else if (cmd == "dump") dump(args);
        else if (cmd == "get") get(args);
        else if (cmd == "set") set(args);
        else if (cmd == "load") load(args);
        else if (cmd == "puts") puts_cmd(rest);
        else if (cmd == "str") str_cmd(args);
        else if (cmd == "fill") fill(args);
        else if (cmd == "asm") asm_cmd(rest);
        else if (cmd == "run") run(args);
        else if (cmd == "step") step(args);
        else if (cmd == "reset") { cpu.reset(); std::cout << "процесор скинуто (пам'ять не чіпали)\n"; }
        else if (cmd == "wipe") { mem = Memory{}; cpu.reset(); std::cout << "пам'ять і процесор очищено\n"; }
        else if (cmd == "show") show(mem);
        else if (cmd == "cls") { clear(mem); std::cout << "екран очищено\n"; }
        else if (cmd == "plot" || cmd == "unplot") pixel(cmd == "plot", args);
        else if (cmd == "frame") { draw_frame(mem); show(mem); }
        else if (cmd == "find") find(args);
        else if (cmd == "sort") sort_cmd(args);
        else if (cmd == "style") style(args);
        else std::cout << "невідома команда '" << cmd << "' (help — список)\n";
        return true;
    }

private:
    Memory mem;
    Cpu cpu;

    static void err(const std::string& m) { std::cout << "помилка: " << m << '\n'; }

    bool addr_arg(const std::string& s, long& a) {
        if (!parse_num(s, a) || a < 0 || a >= MEM_SIZE) {
            err("адреса '" + s + "' має бути від 0 до 4095 (0xFFF)");
            return false;
        }
        return true;
    }

    // Діапазон [lo, hi): два числа або назва регіону code/data/vram.
    bool range_args(const std::vector<std::string>& a, std::size_t i, long& lo, long& hi) {
        if (i >= a.size()) { lo = 0; hi = MEM_SIZE; return true; }
        if (a[i] == "code") { lo = 0; hi = 0x800; return true; }
        if (a[i] == "data") { lo = 0x800; hi = 0xA00; return true; }
        if (a[i] == "vram") { lo = 0xA00; hi = 0xB00; return true; }
        if (i + 1 >= a.size() || !parse_num(a[i], lo) || !parse_num(a[i + 1], hi)) {
            err("діапазон: два числа 'lo hi' (hi не входить) або code | data | vram");
            return false;
        }
        if (lo < 0 || hi > MEM_SIZE || lo >= hi) {
            err("діапазон має задовольняти 0 <= lo < hi <= 4096");
            return false;
        }
        return true;
    }

    void help() {
        std::cout <<
            "Пам'ять:\n"
            "  dump [addr [len]]          шістнадцятковий дамп (за замовчуванням 0, 64 байти)\n"
            "  get addr                   один байт\n"
            "  set addr v [v ...]         записати байти (десяткові або 0x..)\n"
            "  load addr b b b ...        записати байти, ШІСТНАДЦЯТКОВІ: load 0 20 48 24 00 0B\n"
            "  puts addr \"текст\"          записати рядок у UTF-8 разом із '\\0' (escape: \\n \\t \\\\ \\\" \\xHH)\n"
            "  str addr                   прочитати C-рядок: текст, байти, символи\n"
            "  fill lo hi b [b ...]       заповнити [lo, hi) шаблоном із шістнадцяткових байтів\n"
            "  wipe                       очистити пам'ять і процесор\n"
            "Програми:\n"
            "  asm файл.asm               зібрати програму з файлу (UTF-8) у пам'ять\n"
            "  run [N]   step [N]         виконати до HALT (ліміт N кроків) / N кроків\n"
            "  regs   reset               регістри / скинути процесор\n"
            "Екран (це пам'ять 0xA00..0xAFF):\n"
            "  show   cls   frame         намалювати / очистити / рамка з C++\n"
            "  plot x y   unplot x y      ввімкнути / вимкнути піксель\n"
            "  style ascii|block          '#' і '.'  або  '█' і '·'\n"
            "Пошук і сортування (hi не входить):\n"
            "  find v|nz [lo hi|code|data|vram]   перший байт зі значенням v або ненульовий\n"
            "  sort lo hi                 сортування бульбашкою на місці\n"
            "  quit\n";
    }

    void regs() {
        std::cout << "A=" << hex(cpu.A, 2) << " B=" << hex(cpu.B, 2) << " H=" << hex(cpu.H, 4)
                  << " PC=" << hex(cpu.PC, 4) << " SP=" << hex(cpu.SP, 4)
                  << " Z=" << cpu.Z << " N=" << cpu.N << " C=" << cpu.C
                  << " кроків=" << cpu.steps << (cpu.halted ? " [зупинено]" : "") << '\n';
        if (!cpu.error.empty()) std::cout << "помилка процесора: " << cpu.error << '\n';
    }

    void dump(const std::vector<std::string>& a) {
        long addr = 0, len = 64;
        if (a.size() >= 1 && !addr_arg(a[0], addr)) return;
        if (a.size() >= 2 && (!parse_num(a[1], len) || len <= 0)) { err("довжина має бути додатною"); return; }
        if (addr + len > MEM_SIZE) len = MEM_SIZE - addr;
        for (long row = 0; row < len; row += 16) {
            std::string hexpart, ascii;
            for (long i = row; i < std::min(row + 16, len); ++i) {
                Byte b = mem.data[addr + i];
                hexpart += hex(b, 2) + " ";
                ascii += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
            }
            hexpart.resize(16 * 3, ' ');
            std::cout << hex(static_cast<unsigned>(addr + row), 4) << ": " << hexpart << " |" << ascii << "|\n";
        }
    }

    void get(const std::vector<std::string>& a) {
        long addr;
        if (a.size() != 1 || !addr_arg(a[0], addr)) { if (a.size() != 1) err("get addr"); return; }
        Byte b = mem.data[addr];
        std::cout << "0x" << hex(static_cast<unsigned>(addr), 4) << " = " << int(b) << " (0x" << hex(b, 2) << ")\n";
    }

    void set(const std::vector<std::string>& a) {
        long addr;
        if (a.size() < 2 || !addr_arg(a[0], addr)) { if (a.size() < 2) err("set addr v [v ...]"); return; }
        std::vector<Byte> vals;
        for (std::size_t i = 1; i < a.size(); ++i) {
            long v;
            if (!parse_num(a[i], v) || v < -128 || v > 255) { err("'" + a[i] + "' не байт"); return; }
            vals.push_back(static_cast<Byte>(v & 0xFF));
        }
        if (addr + static_cast<long>(vals.size()) > MEM_SIZE) { err("вихід за межі пам'яті"); return; }
        for (std::size_t i = 0; i < vals.size(); ++i) mem.data[addr + i] = vals[i];
        std::cout << "записано " << vals.size() << " байт(ів) з 0x" << hex(static_cast<unsigned>(addr), 4) << '\n';
    }

    void load(const std::vector<std::string>& a) {
        long addr;
        if (a.size() < 2 || !addr_arg(a[0], addr)) { if (a.size() < 2) err("load addr b b b ..."); return; }
        std::vector<Byte> vals;
        for (std::size_t i = 1; i < a.size(); ++i) {
            Byte b;
            if (!parse_hexbyte(a[i], b)) { err("'" + a[i] + "' не шістнадцятковий байт"); return; }
            vals.push_back(b);
        }
        if (addr + static_cast<long>(vals.size()) > MEM_SIZE) { err("вихід за межі пам'яті"); return; }
        for (std::size_t i = 0; i < vals.size(); ++i) mem.data[addr + i] = vals[i];
        std::cout << "записано " << vals.size() << " байт(ів) з 0x" << hex(static_cast<unsigned>(addr), 4) << '\n';
    }

    void puts_cmd(const std::string& rest) {
        std::size_t sp = rest.find_first_of(" \t");
        long addr;
        if (sp == std::string::npos || !addr_arg(rest.substr(0, sp), addr)) {
            if (sp == std::string::npos) err("puts addr \"текст\"");
            return;
        }
        std::string text = trim(rest.substr(sp));
        std::size_t q1 = text.find('"'), q2 = text.rfind('"');
        if (q1 != 0 || q2 == 0 || q2 == std::string::npos) { err("текст має бути в подвійних лапках"); return; }
        std::string bytes, e;
        if (!unescape(text.substr(1, q2 - 1), bytes, e)) { err(e); return; }
        if (!utf8_valid(bytes))
            std::cout << "увага: введений текст не є коректним UTF-8 — перевірте кодування терміналу\n";
        if (addr + static_cast<long>(bytes.size()) + 1 > MEM_SIZE) { err("рядок не вміщується в пам'ять"); return; }
        for (std::size_t i = 0; i < bytes.size(); ++i) mem.data[addr + i] = static_cast<Byte>(bytes[i]);
        mem.data[addr + bytes.size()] = 0;
        std::cout << "записано " << bytes.size() << " байт(ів) + '\\0' = " << bytes.size() + 1
                  << " з 0x" << hex(static_cast<unsigned>(addr), 4) << " (символів: " << utf8_length(bytes) << ")\n";
    }

    void str_cmd(const std::vector<std::string>& a) {
        long addr;
        if (a.size() != 1 || !addr_arg(a[0], addr)) { if (a.size() != 1) err("str addr"); return; }
        std::string s;
        bool terminated = false;
        for (int i = 0; i < OUTS_CEILING && addr + i < MEM_SIZE; ++i) {
            Byte b = mem.data[addr + i];
            if (b == 0) { terminated = true; break; }
            s.push_back(static_cast<char>(b));
        }
        std::cout << '"' << s << "\"  — " << s.size() << " байт(ів), " << utf8_length(s) << " символ(ів)"
                  << (terminated ? "" : ", БЕЗ '\\0' (обірвано стелею або краєм пам'яті)")
                  << (utf8_valid(s) ? "" : ", НЕ коректний UTF-8") << '\n';
    }

    void fill(const std::vector<std::string>& a) {
        long lo, hi;
        if (a.size() < 3 || !parse_num(a[0], lo) || !parse_num(a[1], hi) || lo < 0 || hi > MEM_SIZE || lo >= hi) {
            err("fill lo hi b [b ...]   (0 <= lo < hi <= 4096, hi не входить)");
            return;
        }
        std::vector<Byte> pat;
        for (std::size_t i = 2; i < a.size(); ++i) {
            Byte b;
            if (!parse_hexbyte(a[i], b)) { err("'" + a[i] + "' не шістнадцятковий байт"); return; }
            pat.push_back(b);
        }
        for (long i = lo; i < hi; ++i) mem.data[i] = pat[(i - lo) % pat.size()];
        std::cout << "заповнено " << (hi - lo) << " байт(ів)\n";
    }

    void asm_cmd(const std::string& path) {
        if (path.empty()) { err("asm файл.asm"); return; }
        std::ifstream f(path, std::ios::binary);
        if (!f) { err("не вдалося відкрити '" + path + "'"); return; }
        std::stringstream ss;
        ss << f.rdbuf();
        AsmResult r = assemble(ss.str(), mem);
        for (const auto& w : r.warnings) std::cout << "увага: " << w << '\n';
        if (!r.ok) {
            for (const auto& e : r.errors) std::cout << "помилка: " << e << '\n';
            std::cout << "не зібрано (пам'ять не змінено)\n";
            return;
        }
        std::cout << "зібрано " << r.bytes << " байт(ів)";
        if (!r.labels.empty()) {
            std::cout << "; мітки:";
            for (const auto& [name, a] : r.labels) std::cout << ' ' << name << "=0x" << hex(static_cast<unsigned>(a), 3);
        }
        std::cout << '\n';
    }

    void finish_line() {
        if (!cpu.at_line_start) { std::cout << '\n'; cpu.at_line_start = true; }
    }

    void run(const std::vector<std::string>& a) {
        long limit = 100000;
        if (!a.empty() && (!parse_num(a[0], limit) || limit <= 0)) { err("run [N]: N має бути додатним"); return; }
        if (cpu.halted) { std::cout << "процесор зупинено (reset, щоб почати спочатку)\n"; return; }
        unsigned long long n = cpu.run(static_cast<unsigned long long>(limit));
        finish_line();
        if (!cpu.error.empty()) std::cout << "[ПОМИЛКА: " << cpu.error << "]\n";
        else if (cpu.halted) std::cout << "[HALT після " << n << " крок(ів)]\n";
        else std::cout << "[ліміт " << n << " кроків; PC=0x" << hex(cpu.PC, 4) << " — програма не зупинилась]\n";
    }

    void step(const std::vector<std::string>& a) {
        long n = 1;
        if (!a.empty() && (!parse_num(a[0], n) || n <= 0)) { err("step [N]"); return; }
        if (cpu.halted) { std::cout << "процесор зупинено (reset, щоб почати спочатку)\n"; return; }
        for (long i = 0; i < n && cpu.step(); ++i) {}
        finish_line();
        regs();
    }

    void pixel(bool on, const std::vector<std::string>& a) {
        long x, y;
        if (a.size() != 2 || !parse_num(a[0], x) || !parse_num(a[1], y)) { err("plot x y"); return; }
        bool ok = on ? plot(mem, static_cast<int>(x), static_cast<int>(y))
                     : unplot(mem, static_cast<int>(x), static_cast<int>(y));
        if (!ok) { err("піксель (" + std::to_string(x) + ", " + std::to_string(y) + ") поза екраном 64×32"); return; }
        int addr = VRAM_LO + static_cast<int>(y) * ROW_BYTES + static_cast<int>(x) / 8;
        std::cout << "піксель (" << x << ", " << y << ") -> байт 0x" << hex(addr, 3) << ", біт " << 7 - static_cast<int>(x) % 8
                  << (on ? " виставлено\n" : " скинуто\n");
    }

    void find(const std::vector<std::string>& a) {
        if (a.empty()) { err("find v|nz [lo hi|code|data|vram]"); return; }
        bool nonzero = (a[0] == "nz");
        long want = 0;
        if (!nonzero && (!parse_num(a[0], want) || want < -128 || want > 255)) { err("find: значення має бути байтом або nz"); return; }
        long lo, hi;
        if (!range_args(a, 1, lo, hi)) return;
        for (long i = lo; i < hi; ++i) {  // лінійний пошук
            Byte b = mem.data[i];
            if (nonzero ? b != 0 : b == static_cast<Byte>(want & 0xFF)) {
                std::cout << "знайдено: 0x" << hex(static_cast<unsigned>(i), 3) << " = " << int(b) << " (0x" << hex(b, 2) << ")";
                if (i >= VRAM_LO && i <= VRAM_HI) {
                    int off = static_cast<int>(i - VRAM_LO);
                    int hb = 7;
                    while (hb > 0 && !((b >> hb) & 1)) --hb;
                    if (b != 0) std::cout << "; найлівіший піксель (" << (off % ROW_BYTES) * 8 + (7 - hb) << ", " << off / ROW_BYTES << ")";
                }
                std::cout << '\n';
                return;
            }
        }
        std::cout << "не знайдено в [0x" << hex(static_cast<unsigned>(lo), 3) << ", 0x" << hex(static_cast<unsigned>(hi), 3) << ")\n";
    }

    void sort_cmd(const std::vector<std::string>& a) {
        long lo, hi;
        if (a.size() != 2 || !range_args(a, 0, lo, hi)) { if (a.size() != 2) err("sort lo hi"); return; }
        long n = hi - lo, swaps = 0;
        for (long i = 0; i < n; ++i)
            for (long j = 0; j < n - i - 1; ++j)
                if (mem.data[lo + j] > mem.data[lo + j + 1]) {
                    std::swap(mem.data[lo + j], mem.data[lo + j + 1]);
                    ++swaps;
                }
        std::cout << "відсортовано [0x" << hex(static_cast<unsigned>(lo), 3) << ", 0x" << hex(static_cast<unsigned>(hi), 3) << "), обмінів: " << swaps << '\n';
    }

    void style(const std::vector<std::string>& a) {
        if (a.size() == 1 && a[0] == "ascii") { set_style(Style::Ascii); std::cout << "стиль: # і .\n"; }
        else if (a.size() == 1 && a[0] == "block") { set_style(Style::Block); std::cout << "стиль: \xE2\x96\x88 і \xC2\xB7\n"; }
        else err("style ascii|block");
    }
};

}  // namespace

int main(int argc, char** argv) {
    console_init();
    bool quiet = false;
    std::string script;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-q") quiet = true;
        else script = a;
    }

    std::ifstream file;
    if (!script.empty()) {
        file.open(script, std::ios::binary);
        if (!file) {
            std::cerr << "не вдалося відкрити скрипт '" << script << "'\n";
            return 1;
        }
    }
    std::istream& in = script.empty() ? std::cin : file;
    bool interactive = script.empty() && EMBER_ISATTY();

    Repl repl;
    if (interactive) std::cout << "ember — введіть help\n";
    std::string line;
    while (true) {
        if (interactive) { std::cout << "ember> " << std::flush; }
        if (!std::getline(in, line)) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF
        if (!interactive && !quiet) {
            std::string t = trim(line);
            if (!t.empty() && t[0] != '#') std::cout << "ember> " << t << '\n';
        }
        if (!repl.exec(line)) break;
    }
    return 0;
}
