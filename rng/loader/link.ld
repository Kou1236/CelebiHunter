/* Research ELF layout derived from the release 3gx.ld. No 3GX is produced. */
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
PHDRS {
  code PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
}
SECTIONS {
  __start__ = 0x07000100;
  . = __start__;
  .text ALIGN(4) : {
    KEEP(*(.crt0))
    KEEP(*(.init))
    *(.text .text.* .glue_7 .glue_7t .stub .gnu.linkonce.t*)
    KEEP(*(.fini))
    . = ALIGN(4);
  } : code
  .rodata ALIGN(4) : {
    *(.rodata .rodata.* .roda .gnu.linkonce.r*)
    . = ALIGN(4);
    /* libctru declares this as `extern const size_t`, and dereferences its
     * address in threadCreate and __system_initSyscalls. It is a stored word,
     * not an absolute numeric linker symbol. Keep the separate numeric value
     * for linker arithmetic below (devkitARM's 3dsx.ld uses the same ABI). */
    __tdata_align = .;
    LONG(__tls_alignment_value);
  } : rodata
  .ARM.extab ALIGN(4) : { *(.ARM.extab* .gnu.linkonce.armextab.*) } : rodata
  __exidx_start = .;
  .ARM.exidx ALIGN(4) : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } : rodata
  __exidx_end = .;
  .data ALIGN(4) : { *(.data .data.* .gnu.linkonce.d*) . = ALIGN(4); } : data
  .tdata ALIGN(8) : {
    __tdata_lma = .;
    *(.tdata .tdata.* .gnu.linkonce.td.*)
    __tdata_lma_end = .;
  } : data
  .tbss ALIGN(8) : { *(.tbss .tbss.* .gnu.linkonce.tb.* .tcommon) } : data
  __tls_alignment_value = MAX(8,MAX(ALIGNOF(.tdata),ALIGNOF(.tbss)));
  .preinit_array ALIGN(4) : {
    __preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
  } : data
  .init_array ALIGN(4) : {
    __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
  } : data
  .fini_array ALIGN(4) : {
    __fini_array_start = .; KEEP(*(.fini_array)) KEEP(*(SORT(.fini_array.*))) __fini_array_end = .;
  } : data
  . = ALIGN(8);
  __bss_start__ = .;
  .bss (NOLOAD) : {
    *(.bss .bss.* .dynbss .gnu.linkonce.b* COMMON)
    /* ARM TLS has an 8-byte header. The thread pointer is __tls_start - 8;
     * align that pointer, then include .tbss's padding in the reserved size. */
    . = 8 + ABSOLUTE(ALIGN(ABSOLUTE(. - 8),__tls_alignment_value));
    __tls_start = .;
    . += SIZEOF(.tdata);
    . = ALIGN(ALIGNOF(.tbss));
    . += SIZEOF(.tbss);
    __tls_end = .;
    . = ALIGN(8);
  } : data
  __bss_end__ = .;
  __end__ = ABSOLUTE(.);
  ASSERT(_start == 0x07000100,"pinned Luma entry must be 0x07000100")
  ASSERT((__bss_start__ & 3) == 0 && (__bss_end__ & 3) == 0,"word aligned BSS")
  ASSERT(__tdata_align >= ADDR(.rodata) && __tdata_align + 4 <= ADDR(.rodata) + SIZEOF(.rodata),"libctru TLS alignment must be a mapped data word")
  ASSERT(((__tls_start - 8) & (__tls_alignment_value - 1)) == 0,"ARM TLS header alignment")
  ASSERT(__end__ <= 0x08000000,"executable exceeds loader virtual range")
  /DISCARD/ : { *(.comment .note .note.* .group) }
}
