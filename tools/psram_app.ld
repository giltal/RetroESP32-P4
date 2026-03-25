/*
 * PSRAM App Linker Script
 *
 * Produces a flat binary suitable for loading into PSRAM and executing
 * via MMU mapping. Code is position-independent (compiled with -mcmodel=medany)
 * so no fixed base address is required.
 *
 * Layout:
 *   [.text + .rodata]  → loaded into executable PSRAM region
 *   [.data]            → loaded into data PSRAM region (optional)
 *   [.bss]             → zeroed at load time (not in binary)
 */

OUTPUT_FORMAT("elf32-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(app_entry)

SECTIONS
{
    /*
     * Base address = PSRAM exec mapping virtual address on ESP32-P4.
     *
     * The heap maps physical PSRAM at 0x48000000 (32 MB = 0x2000000).
     * esp_mmu_map() allocates the exec mapping immediately after,
     * starting at 0x4A000000.  On P4 the I-bus and D-bus share the
     * same virtual range (0x48000000-0x4C000000), so the exec mapping
     * is also readable/writable via the data bus — no separate data
     * mapping is needed for .data / .bss.
     */
    . = 0x4A000000;

    /* Executable code — entry point function placed first */
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    /* Read-only data (string literals, const arrays, etc.) */
    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    }

    /* Mark end of loadable code+rodata segment */
    _text_end = .;

    /* Alignment for data segment */
    . = ALIGN(16);

    /* Initialized data */
    _data_start = .;
    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }
    _data_end = .;

    /* Uninitialized data (zeroed at load time, not stored in binary) */
    . = ALIGN(16);
    _bss_start = .;
    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
    }
    _bss_end = .;

    /* Discard debug and metadata sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.riscv.attributes)
        *(.debug*)
    }
}
