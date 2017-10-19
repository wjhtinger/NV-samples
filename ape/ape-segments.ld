OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

ENTRY(_start)
SECTIONS
{
	. = 0x02993700;
	.text.vectortable : {
		KEEP(*(.text.vectortable))
	}

	. = 0x5FD00000;
	.mbox_shared_data : ALIGN(4096) {
		__mbox_shared_data = .;
		KEEP(*(.mbox_shared_data))
		__mbox_shared_end = .;
	}

	. = 0x80100000;
	_start_of_section_layout = .;
	/* text/read-only data */
	.text : {
		KEEP(*(.text.boot.vectab1))
		KEEP(*(.text.boot.vectab2))
		KEEP(*(.text.boot))
		*(.text* .sram.text.glue_7* .gnu.linkonce.t.*)
	}

	.interp : { *(.interp) }
	.hash : { *(.hash) }
	.dynsym : { *(.dynsym) }
	.dynstr : { *(.dynstr) }
	.rel.text : { *(.rel.text) *(.rel.gnu.linkonce.t*) }
	.rela.text : { *(.rela.text) *(.rela.gnu.linkonce.t*) }
	.rel.data : { *(.rel.data) *(.rel.gnu.linkonce.d*) }
	.rela.data : { *(.rela.data) *(.rela.gnu.linkonce.d*) }
	.rel.rodata : { *(.rel.rodata) *(.rel.gnu.linkonce.r*) }
	.rela.rodata : { *(.rela.rodata) *(.rela.gnu.linkonce.r*) }
	.rel.got : { *(.rel.got) }
	.rela.got : { *(.rela.got) }
	.rel.ctors : { *(.rel.ctors) }
	.rela.ctors : { *(.rela.ctors) }
	.rel.dtors : { *(.rel.dtors) }
	.rela.dtors : { *(.rela.dtors) }
	.rel.init : { *(.rel.init) }
	.rela.init : { *(.rela.init) }
	.rel.fini : { *(.rel.fini) }
	.rela.fini : { *(.rela.fini) }
	.rel.bss : { *(.rel.bss) }
	.rela.bss : { *(.rela.bss) }
	.rel.plt : { *(.rel.plt) }
	.rela.plt : { *(.rela.plt) }
	.init : { *(.init) } =0x9090
	.plt : { *(.plt) }

	/* .ARM.exidx is sorted, so has to go in its own output section.  */
	__exidx_start = .;
	.ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) }
	__exidx_end = .;

	.rodata : ALIGN(4) {
		__rodata_start = .;
		*(.rodata .rodata.* .gnu.linkonce.r.*)
INCLUDE "arch/shared_rodata_sections.ld"
		. = ALIGN(4);
		__rodata_end = .;
	}

	.data : ALIGN(4) {
		/* writable data  */
		__data_start_rom = .;
		/* in one segment binaries, the rom data address is on top of the ram data address */
		__data_start = .;
		*(.data .data.* .gnu.linkonce.d.*)
INCLUDE "arch/shared_data_sections.ld"
	}

	.ctors : ALIGN(4) {
		__ctor_list = .;
		KEEP(*(.ctors .init_array))
		__ctor_end = .;
	}
	.dtors : ALIGN(4) {
		__dtor_list = .;
		KEEP(*(.dtors .fini_array))
		__dtor_end = .;
	}
	.got : { *(.got.plt) *(.got) }
	.dynamic : { *(.dynamic) }

	__data_end = .;

	.data_uncached : ALIGN(4096) {
		__data_uncached_start = .;
		KEEP(*(.data_uncached))
		__data_uncached_end = .;
	}

	.dram_data : ALIGN(4096) {
		__dram_app_data_start = .;
		KEEP(*(.dram_data))
		__dram_app_data_end = .;
	}

	.dram_shared_data : ALIGN(4096) {
		__dram_shared_app_data_start = .;
		KEEP(*(.dram_shared))
		__dram_shared_app_data_end = .;
	}

	.dram_shared_wc_data : ALIGN(4096) {
		__dram_shared_wc_app_data_start = .;
		KEEP(*(.dram_shared_wc))
		__dram_shared_wc_app_data_end = .;
	}

	.data_tests_uncached : ALIGN(4096) {
		__data_tests_uncached_start = .;
		KEEP(*(.data_tests_uncached))
		__data_tests_uncached_end = .;
	}

	/* unintialized data (in same segment as writable data) */
	.bss : ALIGN(16318) {
		__bss_prebss_start = .;
		KEEP(*(.bss.prebss.*))
		__bss_prebss_end = .;

		. = ALIGN(4096);
		__bss_start = .;
		*(.bss .bss.*)
		*(.gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	}

	. = ALIGN(4096);
	_end = .;
	. = 0x80100000 + 0x400000	;
	_end_of_ram = .;

	_end_of_section_layout = .;

	/* Strip unnecessary stuff */
	/DISCARD/ : { *(.comment .note .eh_frame) }
}
