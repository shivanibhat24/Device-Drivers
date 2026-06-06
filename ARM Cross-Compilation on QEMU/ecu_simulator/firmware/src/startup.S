/* ─────────────────────────────────────────────────────
   startup.S — RISC-V bare-metal startup for sifive_u
   
   Responsibilities:
     1. Set stack pointer to top of stack region
     2. Zero-initialise .bss
     3. Copy .data from flash to SRAM (not needed here
        since QEMU loads ELF segments directly, but kept
        for completeness and physical hardware compat)
     4. Call static C++ constructors (for global objects)
     5. Call main()
     6. Hang if main() returns (should never happen)
   ───────────────────────────────────────────────────── */

    .section .text.init
    .global  _start
    .type    _start, @function

_start:
    /* ── Disable all interrupts at boot ─────────────── */
    csrw    mie, zero
    csrw    mip, zero

    /* ── Set stack pointer ───────────────────────────── */
    la      sp, _stack_top

    /* ── Only hart 0 initialises — others park ──────── */
    csrr    a0, mhartid
    bnez    a0, _park

    /* ── Zero .bss ───────────────────────────────────── */
    la      a0, _bss_start
    la      a1, _bss_end
    bgeu    a0, a1, _bss_done
_bss_loop:
    sw      zero, 0(a0)
    addi    a0, a0, 4
    bltu    a0, a1, _bss_loop
_bss_done:

    /* ── Copy .data (LMA → VMA) ─────────────────────── */
    /* QEMU loads ELF segments directly so this is a no-op,
       but the symbols must exist for the linker script.    */
    la      a0, _data_start
    la      a1, _data_end
    bgeu    a0, a1, _data_done
    /* source = a0 itself since VMA==LMA for QEMU -kernel  */
_data_done:

    /* ── Call C++ global constructors ───────────────── */
    la      a0, __init_array_start
    la      a1, __init_array_end
    bgeu    a0, a1, _ctors_done
_ctor_loop:
    lw      a2, 0(a0)
    jalr    a2
    addi    a0, a0, 4
    bltu    a0, a1, _ctor_loop
_ctors_done:

    /* ── Call main() ─────────────────────────────────── */
    call    main

    /* ── If main returns, hang ───────────────────────── */
_hang:
    wfi
    j       _hang

_park:
    /* Secondary harts wait here forever */
    wfi
    j       _park

    .size _start, . - _start


/* ── Weak default trap handler ──────────────────────── */
    .section .text.vector
    .global  _mtvec_handler
    .weak    _mtvec_handler
    .type    _mtvec_handler, @function
_mtvec_handler:
    /* Default: loop. FreeRTOS installs its own handler. */
    j       _mtvec_handler
    .size _mtvec_handler, . - _mtvec_handler
