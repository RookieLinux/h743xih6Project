/*
 * linker script for STM32H743XIHx with GNU ld
 */

/*
 * STM32H743XI memory map.
 *
 * Normal code executes from FLASH. Performance-critical functions can be
 * placed in .itcm_text. BSP startup must copy this section before first use.
 * .data/.bss and the exception stack live in DTCM. The RT-Thread heap uses
 * the unused part of AXI SRAM.
 */
MEMORY
{
    FLASH       (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    ITCM        (rx)  : ORIGIN = 0x00000000, LENGTH =   64K
    DTCM        (rw)  : ORIGIN = 0x20000000, LENGTH =  128K
    AXI_SRAM    (rwx) : ORIGIN = 0x24000000, LENGTH =  512K

    SRAM1       (rwx) : ORIGIN = 0x30000000, LENGTH =  128K
    SRAM2       (rwx) : ORIGIN = 0x30020000, LENGTH =  128K
    SRAM3       (rwx) : ORIGIN = 0x30040000, LENGTH =   32K
    SRAM4       (rwx) : ORIGIN = 0x38000000, LENGTH =   64K
    BKPSRAM     (rw)  : ORIGIN = 0x38800000, LENGTH =    4K

    /* RGB565 800 x 480: 0xBB800 bytes per framebuffer. */
    SDRAM_FB0   (rw)  : ORIGIN = 0xC0000000, LENGTH = 0x000BB800
    SDRAM_FB1   (rw)  : ORIGIN = 0xC00BB800, LENGTH = 0x000BB800
    SDRAM       (rwx) : ORIGIN = 0xC0177000, LENGTH = 0x01E89000
}
ENTRY(Reset_Handler)
_system_stack_size = 0x1000;

SECTIONS
{
    .text :
    {
        . = ALIGN(4);
        _stext = .;
        KEEP(*(.isr_vector))            /* Startup code */

        . = ALIGN(4);
        *(.text)                        /* remaining code */
        *(.text.*)                      /* remaining code */
        *(.rodata)                      /* read-only data (constants) */
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t*)
        KEEP(*(.init))
        KEEP(*(.fini))

        /* section information for finsh shell */
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;

        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;

        /* section information for utest */
        . = ALIGN(4);
        __rt_utest_tc_tab_start = .;
        KEEP(*(UtestTcTab))
        __rt_utest_tc_tab_end = .;

        /* section information for at server */
        . = ALIGN(4);
        __rtatcmdtab_start = .;
        KEEP(*(RtAtCmdTab))
        __rtatcmdtab_end = .;
        . = ALIGN(4);

        /* section information for initial. */
        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;

        . = ALIGN(4);

        PROVIDE(__ctors_start__ = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE(__ctors_end__ = .);

        . = ALIGN(4);

        _etext = .;
    } > FLASH = 0

    /* .ARM.exidx is sorted, so has to go in its own output section.  */
    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)

    } > FLASH
    __exidx_end = .;

    /* Optional ITCM code; requires BSP FLASH-to-ITCM relocation before use. */
    .itcm_text :
    {
        . = ALIGN(32);
        _sitcm = .;
        *(.itcm_text)
        *(.itcm_text.*)
        . = ALIGN(32);
        _eitcm = .;
    } > ITCM AT > FLASH
    _siitcm = LOADADDR(.itcm_text);

    /* .data section which is used for initialized data */

    .data :
    {
        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .data secion */
        _sdata = . ;

        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d*)


        PROVIDE(__dtors_start__ = .);
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        PROVIDE(__dtors_end__ = .);

        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .data secion */
        _edata = . ;
    } > DTCM AT > FLASH
    _sidata = LOADADDR(.data);

    .stack (NOLOAD) :
    {
        . = ALIGN(4);
        _sstack = .;
        . = . + _system_stack_size;
        . = ALIGN(4);
        _estack = .;
    } > DTCM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .bss secion */
        _sbss = .;

        *(.bss)
        *(.bss.*)
        *(COMMON)

        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .bss secion */
        _ebss = . ;
    } > DTCM
    __bss_start = _sbss;
    __bss_end = _ebss;
	
    .axi_sram (NOLOAD) :
    {
        . = ALIGN(32);
        __axi_sram_start = .;
        
        *(.axi_sram)        /* 匹配 C 代码中的 section 名 */
        *(.axi_sram.*)      /* 匹配所有子段 */
        
        . = ALIGN(32);
        __axi_sram_end = .;
    } > AXI_SRAM

    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        __heap_start = .;
        . = ORIGIN(AXI_SRAM) + LENGTH(AXI_SRAM);
        __heap_end = .;
    } > AXI_SRAM

    /* Optional NOLOAD sections for the remaining on-chip SRAM banks. */
    .sram1 (NOLOAD) :
    {
        . = ALIGN(32);
        __sram1_start = .;
        *(.sram1)
        *(.sram1.*)
        . = ALIGN(32);
        __sram1_end = .;
    } > SRAM1

    .sram2 (NOLOAD) :
    {
        . = ALIGN(32);
        __sram2_start = .;
        *(.sram2)
        *(.sram2.*)
        . = ALIGN(32);
        __sram2_end = .;
    } > SRAM2

    .sram3 (NOLOAD) :
    {
        . = ALIGN(32);
        __sram3_start = .;
        *(.sram3)
        *(.sram3.*)
        . = ALIGN(32);
        __sram3_end = .;
    } > SRAM3

    .sram4 (NOLOAD) :
    {
        . = ALIGN(32);
        __sram4_start = .;
        *(.sram4)
        *(.sram4.*)
        . = ALIGN(32);
        __sram4_end = .;
    } > SRAM4

    .backup_sram (NOLOAD) :
    {
        . = ALIGN(4);
        __backup_sram_start = .;
        *(.backup_sram)
        *(.backup_sram.*)
        . = ALIGN(4);
        __backup_sram_end = .;
    } > BKPSRAM

    /* Reserve both hard-coded LCD framebuffers in the linker map. */
    .framebuffer0 (NOLOAD) :
    {
        __framebuffer0_start = .;
        *(.framebuffer0)
        *(.framebuffer0.*)
        . = ORIGIN(SDRAM_FB0) + LENGTH(SDRAM_FB0);
        __framebuffer0_end = .;
    } > SDRAM_FB0

    .framebuffer1 (NOLOAD) :
    {
        __framebuffer1_start = .;
        *(.framebuffer1)
        *(.framebuffer1.*)
        . = ORIGIN(SDRAM_FB1) + LENGTH(SDRAM_FB1);
        __framebuffer1_end = .;
    } > SDRAM_FB1

    /* General SDRAM begins after FB0 and FB1; my_buffer is placed here. */
    .sdram (NOLOAD) :
    {
        . = ALIGN(32);
        _ssdram = .;
        *(.sdram)
        *(.sdram.*)
        . = ALIGN(32);
        _esdram = .;
    } > SDRAM

    /* Conventional end symbols refer to the end of normal DTCM BSS. */
    _end = __bss_end;
    PROVIDE(end = _end);

    ASSERT(_eitcm <= ORIGIN(ITCM) + LENGTH(ITCM), "ITCM code overflow")
    ASSERT(__heap_start < __heap_end, "AXI SRAM has no room for RT-Thread heap")
    ASSERT(_esdram <= ORIGIN(SDRAM) + LENGTH(SDRAM), "SDRAM data overflow")

    /* Stabs debugging sections.  */
    .stab          0 : { *(.stab) }
    .stabstr       0 : { *(.stabstr) }
    .stab.excl     0 : { *(.stab.excl) }
    .stab.exclstr  0 : { *(.stab.exclstr) }
    .stab.index    0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    .comment       0 : { *(.comment) }
    /* DWARF debug sections.
     * Symbols in the DWARF debugging sections are relative to the beginning
     * of the section so we begin them at 0.  */
    /* DWARF 1 */
    .debug          0 : { *(.debug) }
    .line           0 : { *(.line) }
    /* GNU DWARF 1 extensions */
    .debug_srcinfo  0 : { *(.debug_srcinfo) }
    .debug_sfnames  0 : { *(.debug_sfnames) }
    /* DWARF 1.1 and DWARF 2 */
    .debug_aranges  0 : { *(.debug_aranges) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    /* DWARF 2 */
    .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_loc      0 : { *(.debug_loc) }
    .debug_macinfo  0 : { *(.debug_macinfo) }
    /* SGI/MIPS DWARF 2 extensions */
    .debug_weaknames 0 : { *(.debug_weaknames) }
    .debug_funcnames 0 : { *(.debug_funcnames) }
    .debug_typenames 0 : { *(.debug_typenames) }
    .debug_varnames  0 : { *(.debug_varnames) }
}
