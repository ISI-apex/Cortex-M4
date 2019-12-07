
SECTIONS
{
    . = 0x00000000;
    . = ALIGN(4);
    .text : { 
     	*(.__image_copy_start)
	*(.vectors)
	bld/vectors.o (.text*) 
	}

    .text_rest :
    {
	*(.text*)
    } 
    . = ALIGN(4);
    .rodata : { *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*))) } 

    . = ALIGN(4);
    .data : { *(.data*) } 

    . = ALIGN(4);

    .efi_runtime_rel_start :
    {
	*(.__efi_runtime_rel_start)
    }

    .efi_runtime_rel : {
	*(.rel*.efi_runtime)
	*(.rel*.efi_runtime.*)
    }

    .efi_runtime_rel_stop :
    {
	*(.__efi_runtime_rel_stop)
    }

    . = ALIGN(4);

    .image_copy_end :
    {
	*(.__image_copy_end)
    }

    .rel_dyn_start :
    {
	*(.__rel_dyn_start)
    }

    .rel.dyn : {
  	*(.rel*)
    }

    .rel_dyn_end : 
    {
	*(.__rel_dyn_end)
    }

    .end :
    {
	*(.__end)
    }


    .bss_start __rel_dyn_start (OVERLAY) : {
	KEEP(*(.__bss_start__));
	__bss_base = .;
    }

    .bss __bss_base (OVERLAY) : {
	*(.bss*)
	. = ALIGN(4);
	__bss_limit = .;
    }

    .bss_end __bss_limit (OVERLAY) : {
	KEEP(*(.__bss_end__));
    }

    __stacktop = 0x100000 - 0x4;
}

ENTRY(__entry)
