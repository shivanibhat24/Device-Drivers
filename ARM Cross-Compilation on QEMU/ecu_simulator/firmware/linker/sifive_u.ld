/* ─────────────────────────────────────────────────────
   sifive_u.ld — Linker script for QEMU sifive_u machine
   
   QEMU sifive_u memory layout:
     MROM   0x00001000  8 KB   (reset vector, read-only)
     FLASH  0x20000000  32 MB  (our code goes here — loaded by QEMU -kernel)
     SRAM   0x80000000  128 MB (stack + heap + data)
   
   When QEMU loads an ELF with -kernel and -bios none, it copies
   each loadable segment to its VMA and jumps to the entry point.
   We place code at 0x80000000 (DRAM) so QEMU can load it directly
   without a bootloader.

   FreeRTOS heap is placed in .heap section; stack grows downward
   from the top of SRAM.
   ───────────────────────────────────────────────────── */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
    /* QEMU sifive_u DRAM: 256 MB starting at 0x80000000             */
    /* We use the first 4 MB for our firmware (more than enough)     */
    DRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 4M
}

/* ── Stack and heap sizes ─────────────────────────── */
_stack_size = 8K;    /* Per-hart stack (hart 0 only in our case)     */
_heap_size  = 64K;   /* FreeRTOS heap (configTOTAL_HEAP_SIZE matches) */

SECTIONS {

    /* ── Startup + interrupt vector table ────────── */
    .text.init ORIGIN(DRAM) : {
        KEEP(*(.text.init))        /* _start must be first              */
        KEEP(*(.text.vector))
    } > DRAM

    /* ── Code ─────────────────────────────────────── */
    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > DRAM

    /* ── C++ constructor/destructor tables ─────────── */
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > DRAM

    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > DRAM

    /* ── Initialised data (copied from LMA to VMA at boot) ── */
    .data : {
        _data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _data_end = .;
    } > DRAM

    /* ── Zero-initialised data ────────────────────── */
    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > DRAM

    /* ── FreeRTOS heap ────────────────────────────── */
    .heap : {
        . = ALIGN(8);
        _heap_start = .;
        . += _heap_size;
        _heap_end = .;
    } > DRAM

    /* ── Stack (grows downward) ───────────────────── */
    .stack : {
        . = ALIGN(16);
        _stack_top = . + _stack_size;   /* Stack pointer initialised here */
    } > DRAM

    _end = .;    /* sbrk() uses this as the heap base for newlib */
}
