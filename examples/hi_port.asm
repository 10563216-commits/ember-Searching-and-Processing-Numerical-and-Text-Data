; Порт виводу (opt): запис у 0xB00 друкує символ без жодного OUT.
        LOADI A, 'H'
        STORE [0x0B00], A
        LOADI A, 'I'
        STORE [0x0B00], A
        HALT
