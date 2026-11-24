; Піксель у лівому верхньому куті БЕЗ PLOT: екран — це пам'ять,
; тож досить покласти байт 0x80 за адресою 0xA00 (ISA.uk.md §9).
        CLS
        LOADI A, 0x80
        LOADH H, 0x0A00
        STORE [H], A
        SHOW
        HALT
