; HELLO\0 лежить у регіоні даних на 0x800; OUTS друкує його до нуля.
        OUTS msg
        HALT

        .org 0x800
msg:    .str "HELLO\n"
