// UTF-8 і рядки.
//
// Головна думка: C-рядок гостя — це просто байти до '\0'. Український символ
// у UTF-8 займає 2 байти, тож «довжина в байтах» і «довжина в символах» — різні
// числа. ember нічого не перекодовує: байти йдуть у пам'ять і з пам'яті як є,
// а термінал сам збирає їх назад у літери.

#include "ember.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void console_init() {
#ifdef _WIN32
    // Без цього консоль Windows читає й пише байти як cp866/cp1251, а не UTF-8.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

bool utf8_valid(const std::string& s) {
    std::size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t len;
        std::uint32_t cp;
        if ((c >> 5) == 0x6) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c >> 4) == 0xE) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c >> 3) == 0x1E) {
            len = 4;
            cp = c & 0x07;
        } else {
            return false;  // продовжувальний байт без початкового, або 0xF8+
        }
        if (i + len > n) return false;
        for (std::size_t k = 1; k < len; ++k) {
            unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;  // надмірне кодування, сурогат або за межами Unicode
        i += len;
    }
    return true;
}

std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;  // продовжувальні байти (10xxxxxx) не рахуємо
    return n;
}

// Якщо рядок обрізано посеред символу, повертає довжину без цього хвоста.
// Потрібно OUTS: стеля в 256 байтів може впасти всередину літери, і напіврядок
// з половиною літери зіпсував би вивід у терміналі.
std::size_t utf8_complete_prefix(const std::string& s) {
    std::size_t n = s.size();
    std::size_t i = n;
    for (int back = 0; i > 0 && back < 4; ++back) {
        unsigned char c = static_cast<unsigned char>(s[i - 1]);
        if ((c & 0xC0) == 0x80) {  // продовжувальний байт — шукаємо початковий
            --i;
            continue;
        }
        std::size_t need = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        std::size_t have = n - (i - 1);
        return have < need ? i - 1 : n;
    }
    return n;
}

bool unescape(const std::string& in, std::string& out, std::string& err) {
    out.clear();
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i >= in.size()) {
            err = "рядок закінчується на '\\'";
            return false;
        }
        switch (in[i]) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '0': out.push_back('\0'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case '\'': out.push_back('\''); break;
            case 'x': {
                auto hexval = [](char h) -> int {
                    if (h >= '0' && h <= '9') return h - '0';
                    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                    return -1;
                };
                int h1 = i + 1 < in.size() ? hexval(in[i + 1]) : -1;
                int h2 = i + 2 < in.size() ? hexval(in[i + 2]) : -1;
                if (h1 < 0 || h2 < 0) {
                    err = "після \\x потрібні дві шістнадцяткові цифри";
                    return false;
                }
                out.push_back(static_cast<char>(h1 * 16 + h2));
                i += 2;
                break;
            }
            default:
                err = std::string("невідома послідовність '\\") + in[i] + "'";
                return false;
        }
    }
    return true;
}
