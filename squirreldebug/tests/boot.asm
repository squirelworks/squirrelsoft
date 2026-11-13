; A boot sector for qemu_smoke.sh: real mode, then protected mode. It
; counts in memory so the test can watch it run. Original code.
bits 16
org 0x7c00
start:  cli
        xor ax, ax
        mov ds, ax
        mov ss, ax
        mov sp, 0x7c00
        mov word [0x500], 0
real:   inc word [0x500]
        call bump
        cmp word [0x500], 3
        jnz real
        lgdt [gdtr]
        mov eax, cr0
        or al, 1
        mov cr0, eax
        jmp 0x08:pm32
bump:   add word [0x502], 3
        ret

bits 32
pm32:   mov ax, 0x10
        mov ds, ax
        mov ss, ax
        mov esp, 0x90000
        xor ecx, ecx
count:  inc ecx
        mov [0x600], ecx
        jmp count

align 8
gdt:    dq 0
        dq 0x00cf9a000000ffff   ; code: base 0, 4G, 32-bit
        dq 0x00cf92000000ffff   ; data
gdtr:   dw gdtr - gdt - 1
        dd gdt

times 510 - ($ - $$) db 0
        dw 0xaa55
