/* .ld for relocatable (base-0) mobile-app builds.
   Sections are laid out contiguously so the HRL1 span stays within the
   deferred stage's 16-bit immediates (currently ~64K). */
SECTIONS {
    . = 0;
    .text : {
        __text_start__ = .;
        *(.text.entry)
        *(.text*)
        *(.rodata*)
        *(.data*)
        . = ALIGN(32);
        __text_end__ = .;
    }
    .persistent : {
        *(.persistent*)
    }
    .bss (NOLOAD) : {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
    }
    .noinit (NOLOAD) : {
        *(.noinit*)
    }
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.note*)
        *(.comment)
    }
}
