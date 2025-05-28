/* gemdrive.ld — absolute layout for GEMDRIVE modules */
SECTIONS
{
    /* Place main.s .text at 0x000000 */
    .text_main 0x000000 : { 
        main.o(.text)
    }

    /* Place gemdrive.s .text at 0x001000 */
    .text_gemdrive 0x001000 : { 
        gemdrive.o(.text)
    }

    /* Place floppy.s .text at 0x002800 */
    .text_floppy 0x002800 : { 
        floppy.o(.text)
    }

}
