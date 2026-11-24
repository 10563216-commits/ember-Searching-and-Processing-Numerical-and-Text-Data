// Мінімальний двопрохідний асемблер для ember.
//
//   мітка:  MNEMONIC  операнди   ; коментар
//   .org  адреса            поставити «курсор» запису
//   .byte 1, 0x2A, 'A'      сирі байти
//   .word 0x0A00            16 біт, молодший байт уперед
//   .str  "Привіт\n"        байти рядка + завершальний '\0'
//   .ascii "без нуля"       байти рядка БЕЗ '\0' (щоб побачити стелю OUTS)
//
// Вихідний файл має бути в UTF-8: байти рядка потрапляють у пам'ять як є.

#include <cctype>
#include <cstdlib>

#include "ember.hpp"

namespace {

struct Op {
    const char* mnem;
    const char* ops;  // шаблон операндів: imm8, imm16, [imm16], [H] або точна назва регістра
    Byte code;
};

const Op TABLE[] = {
    {"HALT", "", 0x00}, {"NOP", "", 0x01}, {"OUT", "", 0x02}, {"OUTN", "", 0x03},
    {"OUTS", "imm16", 0x04},
    {"ADD", "A,B", 0x10}, {"SUB", "A,B", 0x11}, {"AND", "A,B", 0x12}, {"OR", "A,B", 0x13},
    {"XOR", "A,B", 0x14}, {"NOT", "A", 0x15}, {"SHL", "A", 0x16}, {"SHR", "A", 0x17},
    {"INC", "A", 0x18}, {"DEC", "A", 0x19}, {"CMP", "A,B", 0x1A},
    {"LOADI", "A,imm8", 0x20}, {"LOADI", "B,imm8", 0x21},
    {"LOAD", "A,[imm16]", 0x22}, {"LOAD", "B,[imm16]", 0x23},
    {"STORE", "[imm16],A", 0x24}, {"STORE", "[imm16],B", 0x25},
    {"MOV", "A,B", 0x26}, {"MOV", "B,A", 0x27},
    {"LOADH", "H,imm16", 0x28}, {"LOAD", "A,[H]", 0x29}, {"STORE", "[H],A", 0x2A},
    {"INCH", "", 0x2B}, {"DECH", "", 0x2C}, {"HLOW", "", 0x2D},
    {"JMP", "imm16", 0x30}, {"JZ", "imm16", 0x31}, {"JNZ", "imm16", 0x32},
    {"JC", "imm16", 0x33}, {"JNC", "imm16", 0x34},
    {"CLS", "", 0x40}, {"PLOT", "", 0x41}, {"UNPLOT", "", 0x42}, {"SHOW", "", 0x43},
};

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool iequals(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

bool is_reg(const std::string& s) { return iequals(s, "A") || iequals(s, "B") || iequals(s, "H"); }

// Відрізає коментар `;`, але не чіпає `;` усередині "..." чи '...'.
std::string strip_comment(const std::string& s) {
    bool dq = false, sq = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if ((dq || sq) && c == '\\') {
            ++i;
            continue;
        }
        if (!sq && c == '"') dq = !dq;
        else if (!dq && c == '\'') sq = !sq;
        else if (!dq && !sq && c == ';') return s.substr(0, i);
    }
    return s;
}

// Ділить за комами верхнього рівня (не всередині [..] і лапок).
std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    if (trim(s).empty()) return out;
    std::string cur;
    int depth = 0;
    bool dq = false, sq = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if ((dq || sq) && c == '\\' && i + 1 < s.size()) {
            cur.push_back(c);
            cur.push_back(s[++i]);
            continue;
        }
        if (!sq && c == '"') dq = !dq;
        else if (!dq && c == '\'') sq = !sq;
        else if (!dq && !sq) {
            if (c == '[') ++depth;
            else if (c == ']') --depth;
            else if (c == ',' && depth == 0) {
                out.push_back(trim(cur));
                cur.clear();
                continue;
            }
        }
        cur.push_back(c);
    }
    out.push_back(trim(cur));
    return out;
}

struct Ctx {
    std::map<std::string, int> labels;
    bool strict = false;  // у першому проході невідомі мітки вважаються нулем
};

// Вираз: доданки через + і -, доданок — число, 'c' або мітка.
bool eval(const std::string& s, const Ctx& c, long& result, std::string& err) {
    std::size_t i = 0;
    auto skip = [&] {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    };
    long total = 0;
    int pending = 1;
    skip();
    if (i >= s.size()) {
        err = "порожній вираз";
        return false;
    }
    while (true) {
        skip();
        while (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            if (s[i] == '-') pending = -pending;
            ++i;
            skip();
        }
        if (i >= s.size()) {
            err = "очікувалось число або мітка";
            return false;
        }
        long v = 0;
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch == '\'') {
            if (i + 2 < s.size() && s[i + 1] == '\\' && s[i + 3] == '\'') {
                switch (s[i + 2]) {
                    case 'n': v = '\n'; break;
                    case 'r': v = '\r'; break;
                    case 't': v = '\t'; break;
                    case '0': v = 0; break;
                    case '\\': v = '\\'; break;
                    case '\'': v = '\''; break;
                    case '"': v = '"'; break;
                    default: err = "невідомий символ-escape"; return false;
                }
                i += 4;
            } else if (i + 2 < s.size() && s[i + 2] == '\'') {
                if (static_cast<unsigned char>(s[i + 1]) >= 0x80) {
                    err = "символ не з ASCII не вміщується в один байт: використайте .str або числа";
                    return false;
                }
                v = static_cast<unsigned char>(s[i + 1]);
                i += 3;
            } else {
                err = "некоректний символьний літерал";
                return false;
            }
        } else if (std::isdigit(ch)) {
            std::size_t j = i;
            while (j < s.size() && std::isalnum(static_cast<unsigned char>(s[j]))) ++j;
            std::string tok = s.substr(i, j - i);
            char* end = nullptr;
            if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
                v = std::strtol(tok.c_str() + 2, &end, 16);
            } else if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'b' || tok[1] == 'B')) {
                v = std::strtol(tok.c_str() + 2, &end, 2);
            } else {
                v = std::strtol(tok.c_str(), &end, 10);
            }
            if (*end != '\0') {
                err = "некоректне число '" + tok + "'";
                return false;
            }
            i = j;
        } else if (std::isalpha(ch) || ch == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            std::string name = s.substr(i, j - i);
            auto it = c.labels.find(name);
            if (it != c.labels.end()) {
                v = it->second;
            } else if (c.strict) {
                err = "невідома мітка '" + name + "'";
                return false;
            }
            i = j;
        } else {
            err = std::string("несподіваний символ у виразі: '") + s[i] + "'";
            return false;
        }
        total += pending * v;
        skip();
        if (i >= s.size()) break;
        if (s[i] == '+') pending = 1;
        else if (s[i] == '-') pending = -1;
        else {
            err = "несподіваний символ у виразі: '" + s.substr(i, 1) + "'";
            return false;
        }
        ++i;
    }
    result = total;
    return true;
}

// 0 — шаблон не підійшов, 1 — закодовано, 2 — форма підійшла, але значення погане.
int try_encode(const Op& e, const std::vector<std::string>& ops, const Ctx& c,
               std::vector<Byte>& out, std::string& err) {
    std::vector<std::string> pat = split_commas(e.ops);
    if (pat.size() != ops.size()) return 0;
    out.clear();
    out.push_back(e.code);
    for (std::size_t i = 0; i < pat.size(); ++i) {
        const std::string& p = pat[i];
        const std::string& g = ops[i];
        long v = 0;
        if (p == "imm8" || p == "imm16") {
            if (g.empty() || g[0] == '[' || is_reg(g)) return 0;
            if (!eval(g, c, v, err)) return 2;
            if (p == "imm8") {
                if (v < -128 || v > 255) {
                    err = "число " + std::to_string(v) + " не вміщується в байт";
                    return 2;
                }
                out.push_back(static_cast<Byte>(v & 0xFF));
            } else {
                if (v < 0 || v > 0xFFFF) {
                    err = "адреса " + std::to_string(v) + " поза 16 бітами";
                    return 2;
                }
                out.push_back(static_cast<Byte>(v & 0xFF));
                out.push_back(static_cast<Byte>((v >> 8) & 0xFF));
            }
        } else if (p == "[imm16]") {
            if (g.size() < 3 || g.front() != '[' || g.back() != ']') return 0;
            std::string inner = trim(g.substr(1, g.size() - 2));
            if (iequals(inner, "H")) return 0;  // це форма [H]
            if (!eval(inner, c, v, err)) return 2;
            if (v < 0 || v > 0xFFFF) {
                err = "адреса " + std::to_string(v) + " поза 16 бітами";
                return 2;
            }
            out.push_back(static_cast<Byte>(v & 0xFF));
            out.push_back(static_cast<Byte>((v >> 8) & 0xFF));
        } else {
            if (!iequals(p, g)) return 0;
        }
    }
    return 1;
}

// Рядок у лапках: повертає вміст між лапками (ще з escape-ами).
bool parse_quoted(const std::string& s, std::string& raw, std::string& err) {
    if (s.empty() || s[0] != '"') {
        err = "очікувався рядок у подвійних лапках";
        return false;
    }
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
            continue;
        }
        if (s[i] == '"') {
            if (!trim(s.substr(i + 1)).empty()) {
                err = "зайві символи після закритої лапки";
                return false;
            }
            raw = s.substr(1, i - 1);
            return true;
        }
    }
    err = "не закрито лапки";
    return false;
}

std::vector<std::string> split_lines(std::string src) {
    if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF &&
        static_cast<unsigned char>(src[1]) == 0xBB && static_cast<unsigned char>(src[2]) == 0xBF)
        src.erase(0, 3);  // BOM, який любить додавати Блокнот Windows
    std::vector<std::string> lines;
    std::string cur;
    for (char ch : src) {
        if (ch == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();  // CRLF
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        lines.push_back(cur);
    }
    return lines;
}

}  // namespace

AsmResult assemble(const std::string& source, Memory& mem) {
    AsmResult r;
    std::vector<std::string> lines = split_lines(source);
    Ctx ctx;
    std::vector<std::pair<int, Byte>> image;

    for (int pass = 1; pass <= 2; ++pass) {
        ctx.strict = (pass == 2);
        int addr = 0;
        bool out_of_range_reported = false;

        for (std::size_t ln = 0; ln < lines.size(); ++ln) {
            std::string where = "рядок " + std::to_string(ln + 1) + ": ";
            auto report = [&](const std::string& m) {
                if (pass == 2) r.errors.push_back(where + m);
            };
            auto emit = [&](Byte b) {
                if (pass == 2) {
                    if (addr < 0 || addr >= MEM_SIZE) {
                        if (!out_of_range_reported) {
                            report("запис за межами пам'яті (адреса " + std::to_string(addr) + ")");
                            out_of_range_reported = true;
                        }
                    } else {
                        image.push_back({addr, b});
                    }
                }
                ++addr;
            };

            std::string line = trim(strip_comment(lines[ln]));

            // мітки: `name:` на початку рядка (їх може бути кілька)
            while (!line.empty()) {
                std::size_t k = 0;
                unsigned char c0 = static_cast<unsigned char>(line[0]);
                if (!(std::isalpha(c0) || c0 == '_')) break;
                while (k < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[k])) || line[k] == '_'))
                    ++k;
                if (k >= line.size() || line[k] != ':') break;
                std::string name = line.substr(0, k);
                if (pass == 1) {
                    if (is_reg(name)) {
                        r.errors.push_back(where + "мітка '" + name + "' збігається з назвою регістра");
                    } else if (ctx.labels.count(name)) {
                        r.errors.push_back(where + "мітку '" + name + "' уже визначено");
                    } else {
                        ctx.labels[name] = addr;
                    }
                }
                line = trim(line.substr(k + 1));
            }
            if (line.empty()) continue;

            std::size_t sp = line.find_first_of(" \t");
            std::string head = line.substr(0, sp);
            std::string rest = (sp == std::string::npos) ? "" : trim(line.substr(sp));

            // ---- директиви ----
            if (head[0] == '.') {
                std::string d = lower(head);
                std::string err;
                long v = 0;
                if (d == ".org") {
                    if (!eval(rest, ctx, v, err)) { report(err); continue; }
                    if (v < 0 || v >= MEM_SIZE) { report(".org поза пам'яттю"); continue; }
                    addr = static_cast<int>(v);
                } else if (d == ".byte" || d == ".word") {
                    for (const std::string& item : split_commas(rest)) {
                        if (!eval(item, ctx, v, err)) { report(err); break; }
                        if (d == ".byte") {
                            if (v < -128 || v > 255) { report("число " + std::to_string(v) + " не вміщується в байт"); break; }
                            emit(static_cast<Byte>(v & 0xFF));
                        } else {
                            if (v < 0 || v > 0xFFFF) { report("число поза 16 бітами"); break; }
                            emit(static_cast<Byte>(v & 0xFF));
                            emit(static_cast<Byte>((v >> 8) & 0xFF));
                        }
                    }
                } else if (d == ".str" || d == ".ascii") {
                    std::string raw, bytes;
                    if (!parse_quoted(rest, raw, err)) { report(err); continue; }
                    if (!unescape(raw, bytes, err)) { report(err); continue; }
                    if (pass == 2 && !utf8_valid(bytes))
                        r.warnings.push_back(where + "рядок не є коректним UTF-8 — файл збережено не в UTF-8?");
                    for (char ch : bytes) emit(static_cast<Byte>(ch));
                    if (d == ".str") emit(0);
                } else {
                    report("невідома директива '" + head + "'");
                }
                continue;
            }

            // ---- інструкції ----
            std::string m = upper(head);
            std::vector<std::string> ops = split_commas(rest);
            bool known = false, done = false;
            std::string err;
            std::vector<Byte> bytes;
            for (const Op& e : TABLE) {
                if (m != e.mnem) continue;
                known = true;
                int res = try_encode(e, ops, ctx, bytes, err);
                if (res == 1) { done = true; break; }
                if (res == 2) { report(err); done = true; bytes.clear(); break; }
            }
            if (!known) { report("невідома інструкція '" + head + "'"); continue; }
            if (!done) { report("невірні операнди для " + m); continue; }
            for (Byte b : bytes) emit(b);
        }
    }

    if (r.errors.empty()) {
        for (auto& [a, b] : image) mem.data[a] = b;
        r.ok = true;
        r.bytes = static_cast<int>(image.size());
        r.labels = ctx.labels;
    }
    return r;
}
