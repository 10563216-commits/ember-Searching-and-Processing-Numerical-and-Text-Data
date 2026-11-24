#!/bin/sh
# Перевірки основної стежки Lab 05 і UTF-8. Запуск: make test
EMBER=${1:-build/ember}
T=$(mktemp -d)
fails=0
pass() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
check() { # check "назва" рядок-який-має-бути-у-виводі
    if printf '%s' "$OUT" | grep -qF -- "$2"; then pass "$1"; else fail "$1 (немає: $2)"; fi
}
run() { printf '%s\n' "$1" > "$T/in"; OUT=$($EMBER -q "$T/in" 2>&1); }

# 1. порт виводу: програма з лаби друкує HI без OUT, а get 2816 показує 73
run 'load 0 20 48 24 00 0B 20 49 24 00 0B 00
run
get 2816'
check "порт виводу друкує HI" "HI"
check "get 2816 = 73" "= 73 (0x49)"

# 2. піксель без PLOT: байт 0x80 на 0xA00 -> '#' у куті
run 'asm examples/pixel_no_plot.asm
run
dump 0xA00 8'
check "піксель без PLOT: перший рядок" "#...............................................................
"
check "піксель без PLOT: байт 80 на 0xA00" "0A00: 80 00 00 00 00 00 00 00"

# 3. три пікселі через PLOT: (1,1) (2,2) (3,3) -> байти 0xA08=40, 0xA10=20, 0xA18=10
run 'asm examples/three_pixels.asm
run
dump 0xA08 1
dump 0xA10 1
dump 0xA18 1'
check "PLOT(1,1) -> 0xA08 = 40" "0A08: 40"
check "PLOT(2,2) -> 0xA10 = 20" "0A10: 20"
check "PLOT(3,3) -> 0xA18 = 10" "0A18: 10"

# 4. рамка з C++: перший рядок FF, другий — 80 ... 01
run 'frame
dump 0xA00 16'
check "рамка: рядок 0 = FF*8" "0A00: FF FF FF FF FF FF FF FF"
check "рамка: рядок 1 = 80 ... 01" "80 00 00 00 00 00 00 01"

# 5. поза екраном
run 'plot 64 0
plot 0 32'
check "plot x=64 відхилено" "поза екраном"
run 'load 0 20 40 21 00 41 00
run
regs'
check "PLOT поза екраном виставляє C" "C=1"
run 'load 0 20 3F 21 1F 41 00
run
regs
find nz vram'
check "PLOT (63,31) у межах: C=0" "C=0"
check "(63,31) = байт 0xAFF, піксель праворуч внизу" "0xAFF = 1 (0x01); найлівіший піксель (63, 31)"

# 6. UTF-8: український текст через OUTS
run 'asm examples/hello_uk.asm
run
str 0x800'
check "UTF-8: OUTS друкує літери" "Привіт, світе! Їжак і Єнот — друзі 🙂"
check "UTF-8: байти і символи — різні числа" "67 байт(ів), 37 символ(ів)"

# 7. puts з UTF-8 у REPL
run 'puts 0x800 "Ґанок\n"
load 0 04 00 08 00
run'
check "puts: UTF-8 у пам'яті і назад" "Ґанок"

# 8. стеля OUTS: 256 байтів не розрізають символ (тире = 3 байти, 256 = 85*3 + 1)
run 'fill 0x800 0xA00 E2 80 94
load 0 04 00 08 00
run'
printf '%s\n' "$OUT" > "$T/ceil"
n=$(grep -o '—' "$T/ceil" | wc -l | tr -d ' ')
if [ "$n" = "85" ]; then pass "стеля: рівно 85 цілих символів"; else fail "стеля: символів $n, очікувалось 85"; fi
if iconv -f UTF-8 -t UTF-8 "$T/ceil" > /dev/null 2>&1; then pass "стеля: вивід — коректний UTF-8"; else fail "стеля: зіпсований UTF-8"; fi

# 9. рядок без нуля: str показує, що нуля немає
run 'fill 0x800 0xA00 41
str 0x800'
check "рядок без '\\0' виявлено" "БЕЗ '\\0'"

# 10. find по даних
run 'puts 0x800 "abc"
find 99 data
find nz data
find 7 data'
check "find по даних: значення" "знайдено: 0x802 = 99"
check "find nz по даних" "знайдено: 0x800 = 97"
check "find: немає" "не знайдено"

# 11. сортування бульбашкою
run 'set 0x800 4 1 3 2
sort 0x800 0x804
dump 0x800 4'
check "sort: 4 обміни" "обмінів: 4"
check "sort: 01 02 03 04" "0800: 01 02 03 04"

# 12. файл із BOM і CRLF збирається; не-UTF-8 дає попередження
printf '\357\273\277OUTS msg\r\nHALT\r\n.org 0x800\r\nmsg: .str "Ok\\n"\r\n' > "$T/bom.asm"
run "asm $T/bom.asm
run"
check "BOM + CRLF збираються" "Ok"
printf 'OUTS msg\nHALT\n.org 0x800\nmsg: .str "\317\360"\n' > "$T/cp1251.asm"
run "asm $T/cp1251.asm"
check "не-UTF-8 рядок дає попередження" "не є коректним UTF-8"

# 13. помилки асемблера не змінюють пам'ять
printf 'LOADI A, 300\nFOO\n' > "$T/bad.asm"
run "asm $T/bad.asm
dump 0 4"
check "помилка: не вміщується в байт" "не вміщується в байт"
check "помилка: невідома інструкція" "невідома інструкція 'FOO'"
check "помилка: пам'ять не змінено" "0000: 00 00 00 00"

rm -rf "$T"
if [ "$fails" -eq 0 ]; then echo "усі перевірки пройдено"; else echo "провалено: $fails"; exit 1; fi
