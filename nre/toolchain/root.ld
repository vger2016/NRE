ENTRY(_start)

PHDRS
{
    text    PT_LOAD;
    data    PT_LOAD;
}

SECTIONS
{
    . = CONSTANT(MAXPAGESIZE);
    TEXT_BEGIN = .;
    
    .text : {
        *(.text.*)
        *(.rodata .rodata.*)
    } : text
    
    .ctors : {
        CTORS_BEGIN = .;
        KEEP(*(.ctors))
        KEEP(*(SORT(.ctors.*)))
        CTORS_END = .;
        CTORS_REVERSE_BEGIN = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        CTORS_REVERSE_END = .;
    } : text
    
    .dtors : {
        DTORS_BEGIN = .;
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors.*))
        DTORS_END = .;
    } : text
    
    .eh_frame_hdr : {
        *(.eh_frame_hdr)
    } : text
    
    TEXT_END = .;
    
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    
    DATA_BEGIN = .;
    
    .data : {
        *(.data .data.*)
        *(.bss .bss.*)
    } : data
    
    .eh_frame : {
        EH_FRAME_BEGIN = .;
        KEEP(*(.eh_frame))
        LONG(0)
    } : data

    .gcc_except_table : {
        KEEP(*(.gcc_except_table))
        KEEP(*(.gcc_except_table.*))
    } : data
    
    DATA_END = .;
}
