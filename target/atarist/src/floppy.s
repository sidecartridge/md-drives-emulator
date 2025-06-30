; SidecarTridge Multidevice Floppy Disk Drive (FDD) Emulator
; (C) 2023-25 by GOODDATA LABS SL
; License: GPL v3

; Emulate a Floppy Disk Drive (FDD) from the SidecarT

; Bootstrap the code in ASM
ROM4_START_ADDR:         equ $FA0000 ; ROM4 start address
ROM3_START_ADDR:         equ $FB0000 ; ROM3 start address

FLOPPYEMUL_GAP_SIZE:     equ $1800   ; 6KB gap

ROM_EXCHG_BUFFER_ADDR:    equ (ROM4_START_ADDR + $8200)     ; ROM4 buffer address + lower memory offset
RANDOM_TOKEN_ADDR:        equ (ROM_EXCHG_BUFFER_ADDR)
RANDOM_TOKEN_SEED_ADDR:   equ (RANDOM_TOKEN_ADDR + 4)     ; RANDOM_TOKEN_ADDR + 4 bytes
RANDOM_TOKEN_POST_WAIT:   equ $1                          ; Wait this cycles after the random number generator is ready
COMMAND_TIMEOUT           equ $00000FFF                   ; Timeout for the command

ROMCMD_START_ADDR:  equ (ROM3_START_ADDR)	      ; We are going to use ROM3 address
CMD_MAGIC_NUMBER    equ ($ABCD) 			  	  ; Magic number header to identify a command
CMD_RETRIES_COUNT   equ 5                         ; Number of retries to send the command
APP_FLOPPYEMUL      equ $0200                     ; MSB is the app code. Floppy emulator is $02
CMD_SAVE_VECTORS    equ ($0 + APP_FLOPPYEMUL)     ; Command code to save the old vectors
CMD_READ_SECTOR     equ ($1 + APP_FLOPPYEMUL)     ; Command code to read a sector from the emulated disk
CMD_WRITE_SECTOR    equ ($2 + APP_FLOPPYEMUL)     ; Command code to write a sector to the emulated disk
CMD_SAVE_HARDWARE   equ ($4 + APP_FLOPPYEMUL)     ; Command code to save the hardware type of the ATARI computer
CMD_SET_SHARED_VAR  equ ($5 + APP_FLOPPYEMUL)     ; Command code to set a shared variable
CMD_RESET           equ ($6 + APP_FLOPPYEMUL)     ; Command code to reset the floppy emulator before starting the boot process
CMD_SAVE_BIOS_VECTOR equ ($7 + APP_FLOPPYEMUL)     ; Command code to save the old BIOS vector
CMD_SHOW_VECTOR_CALL equ ($B + APP_FLOPPYEMUL)    ; Command code to show the XBIOS vector call 
CMD_DEBUG            equ ($C + APP_FLOPPYEMUL)    ; Command code to send to the RP2040 the debug command

FLOPPY_SHARED_VARIABLES             equ (RANDOM_TOKEN_SEED_ADDR + 4)        ; ROM EXCHANGE BUFFER address
FLOPPYEMUL_SHARED_VARIABLE_SIZE     equ (FLOPPYEMUL_GAP_SIZE / 4) ;  6KB gap divided by 4 bytes per longword
FLOPPYEMUL_SHARED_VARIABLES_COUNT   equ 32  ; Size of the shared variables of the shared functions

SVAR_XBIOS_TRAP_ENABLED:    equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 0)      ; XBIOS trap enabled
SVAR_BOOT_ENABLED:          equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 1)      ; Boot sector enabled
SVAR_EMULATION_MODE:        equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 2)      ; Emulation mode
SVAR_ENABLED:               equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 3)      ; Enabled flag

; We will need 32 bytes extra for the variables of the floppy emulator
FLOPPYEMUL_VARIABLES_OFFSET equ (ROM_EXCHG_BUFFER_ADDR + FLOPPYEMUL_GAP_SIZE + FLOPPYEMUL_SHARED_VARIABLES_COUNT)


old_XBIOS_trap:      equ FLOPPYEMUL_VARIABLES_OFFSET ; Old XBIOS trap address

BPB_data_A:            equ (old_XBIOS_trap + 4) ; ; old_XBIOS_trap + 4 bytes
trackcnt_A:            equ BPB_data_A + 18 ; BPB_data + 18 bytes
sidecnt_A:             equ trackcnt_A + 2 ; trackcnt + 2 bytes
secpcyl_A:             equ sidecnt_A + 2 ; sidecnt + 2 bytes
secptrack_A:           equ secpcyl_A + 2 ; secpcyl + 2 bytes
disk_number_A:         equ secptrack_A + 8 ; secptrack + 8 bytes

BPB_data_B:            equ (disk_number_A + 2) ; disk_number_A + 2 bytes
trackcnt_B:            equ BPB_data_B + 18 ; BPB_data + 18 bytes
sidecnt_B:             equ trackcnt_B + 2 ; trackcnt + 2 bytes
secpcyl_B:             equ sidecnt_B + 2 ; sidecnt + 2 bytes
secptrack_B:           equ secpcyl_B + 2 ; secpcyl + 2 bytes
disk_number_B:         equ secptrack_B + 8 ; secptrack + 8 bytes

; After the variables, allocate the buffer to read/write sectors
sidecart_read_buf:     equ (FLOPPYEMUL_VARIABLES_OFFSET + 256)

; CONSTANTS
VEC_BIOS        equ $2D     ; BIOS vector
XBIOS_trap      equ $b8     ; TRAP #14 Handler (XBIOS)
_membot         equ $432    ; This value represents last memory used by the TOS, and start of the heap area available
_bootdev        equ $446    ; This value represents the device from which the system was booted (0 = A:, 1 = B:, etc.)

_nflops         equ $4a6    ; This value indicates the number of floppy drives currently connected to the system
_drvbits        equ $4c2    ; Each of 32 bits in this longword represents a drive connected to the system. Bit #0 is A, Bit #1 is B and so on.
_dskbufp        equ $4c6    ; Address of the disk buffer pointer    
_longframe      equ $59e    ; Address of the long frame flag. If this value is 0 then the processor uses short stack frames, otherwise it uses long stack frames.


; Macros should be included before any function code
    include inc/tos.s
    include inc/sidecart_macros.s

    
    org $FA2800         ; Start of the code. First 4KB bytes are reserved for the terminal.

floppy_start:
    tst.l (FLOPPY_SHARED_VARIABLES + (SVAR_ENABLED * 4))
    beq .exit_graciouslly ; If the Floppy emulation is not enabled

; Disable the MegaSTE cache and 16Mhz
    jsr set_8mhz_megaste

; A little delay to let the rp2040 breathe
;	wait_sec

; Get the hardware version 
    bsr detect_ms16
; Figure out the TOS version
    bsr get_tos_version

    bsr set_vector_bios

    bsr set_vector_xbios

    bra boot_disk

.exit_graciouslly:
    rts


; Save the old BIOS vectors and install
set_vector_bios:
    move.l #bios_trap,-(sp)             ; Otherwise, use the standard entry point
    move.w #VEC_BIOS,-(sp)
    move.w #Setexc,-(sp)                     ; Setexc() modify BIOS vector and add our trap
    trap #13
    addq.l #8,sp
    move.l d0, d3                       ; Address of the old BIOS vector
    move.l #old_bios_handler, d4        ; Address of the old handler
    move.l #bios_trap, d5               ; Address of the new handler
    send_sync CMD_SAVE_BIOS_VECTOR, 12   ; Send the command to the Sidecart. 12 bytes of payload
    tst.w d0                            ; 0 if no error
    rts

    ds.b ((4 - (* & 3)) & 3)                ; bump to the next 4-byte boundary 
    dc.l 'XBRA'                             ; XBRA structure
    dc.l 'SDFE'                             ; Put your cookie here (SidecarTridge Floppy Emulator)
old_bios_handler:
    dc.l 0                                  ; We can't modify this address because it's in ROM, but we can modify it in the RP2040 memory



; Save the old xbios vectors and install
set_vector_xbios:
    move.l XBIOS_trap.w,d3              ; Payload is the old XBIOS_trap
    send_sync CMD_SAVE_VECTORS, 4
    tst.l   (FLOPPY_SHARED_VARIABLES + (SVAR_XBIOS_TRAP_ENABLED * 4))  ; 0: XBIOS trap disabled, Not 0: XBIOS trap enabled
    beq.s _dont_set_xbios_trap 
    move.l  #new_XBIOS_trap_routine,XBIOS_trap.w
_dont_set_xbios_trap:
    rts

boot_disk:
    btst #0, (_nflops + 1)
    beq.s _boot_disk_no_floppy
    ; Let's initialize to help configure the drives
    clr.w _bootdev.w
    or.l #%00000011, _drvbits.w ; Force both drive A and B bits

_boot_disk_no_floppy:
;    movem.l d0-d7/a0-a6, -(sp)
;    bios Drvmap, 2
;    move.l d0, d3
;    move.l _drvbits.w, d4
;    clr.l d5
;    move.w _nflops.w, d5
;    clr.l d6
;    move.b (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3), d6
;    send_sync CMD_DEBUG, 16
;    movem.l (sp)+,d0-d7/a0-a6

    clr.w _bootdev.w                ; Set emulated A as bootdevice
    move.w #2, _nflops.w            ; Set the number of drives to 2. Always
    ; Configure drive A
    btst   #0, (_drvbits + 3).w     ; Check if drive A exists
    beq.s _boot_disk_try_emulate_a  ; If not, go to drive A emulation
    or.l #%00000001,_drvbits.w      ; Force the drive A bit
_boot_disk_try_emulate_a:
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq.s _boot_disk_drive_b
    ; Emulate A
    or.l #%00000001,_drvbits.w              ; Force the drive A bit

_boot_disk_drive_b:
    ; Configure drive B
    btst   #1, (_drvbits + 3).w             ; Check if drive B exists
    beq.s _boot_disk_try_emulate_b          ; If not, go to drive B emulation
    or.l #%00000010,_drvbits.w              ; Force the drive B bit
_boot_disk_try_emulate_b:
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _start_boot                       ; If not, go start booting
    ; Emulate B
    or.l #%00000010,_drvbits.w              ; Create the drive B bit

    ; load bootsector and execute it if checksum is $1234
_start_boot:
; Check if there is drive A
    btst #0, (_drvbits + 3).w
    beq _dont_boot
; Check if there is a physical drive A and not boot if so
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq _dont_boot

; Read sectors from the sidecart. Don't use XBIOS call
    tst.l (FLOPPY_SHARED_VARIABLES + (SVAR_BOOT_ENABLED * 4))  ; Not 0: Boot sector enabled,  0: Boot sector disabled
    beq.s _dont_boot

    moveq #0, d6            ; Start reading at sector 0
    move.l d0, d4           ; Read from drive A
    move.l d0, d2           ; clear d2.l 
    move.w BPB_data_A, d2   ; Sector size of the emulated drive A
    move.l _membot.w,a4        ; Start reading at $2000
    bsr read_sector_from_sidecart

    ; Test checksum

    move.l _membot.w,a1        ; Start reading at $2000
    move.l a1,a0
    move.w #255,d1          ; Read 512 bytes
    clr.l d2
_checksum_loop:             ; Calculate checksum
    add.w (a1)+,d2  
    dbf d1,_checksum_loop

.boot_sector_enabled:
    cmp.w #$1234,d2         ; Compare to the magic numnber
    bne.s _dont_boot        ; If equal, boot at $2000
    jmp (a0)                

_dont_boot:
    rts

detect_ms16:
    bsr detect_hw
    move.l d0, d3                       ; hardware type
    move.l #do_transfer_sidecart, d4    ; Address of the start function to overwrite the speed change
    move.l #exit_transfer_sidecart, d5  ; Address of the end function to overwrite the speed change
    send_sync CMD_SAVE_HARDWARE, 12
    rts

; New XBIOS map A calls to Sidecart,
; B to physical A if it exists
; The XBIOS, like the BIOS may utilize registers D0-D2 and A0-A2 as scratch registers and their
; contents should not be depended upon at the completion of a call. In addition, the function
; opcode placed on the stack will be modified.
new_XBIOS_trap_routine:
    btst #5, (sp)                         ; Check if called from user mode
    beq.s _user_mode                      ; if so, do correct stack pointer
_not_user_mode:
    move.l sp,a0                          ; Move stack pointer to a0
    bra.s _check_cpu
_user_mode:
    move.l usp,a0                          ; if user mode, correct stack pointer
    subq.l #6,a0
;
; This code checks if the CPU is a 68000 or not
;
_check_cpu:
    tst.w _longframe                          ; Check if the CPU is a 68000 or not
    beq.s _notlong
_long:
    addq.w #2, a0                             ; Correct the stack pointer parameters for long frames 
_notlong:

;   For debugging purposes
;    movem.l d0-d7/a0-a6,-(sp)
;    move.w 6(a0), d3                     ; get XBIOS call number
;    send_sync CMD_SHOW_VECTOR_CALL, 2    ; Send the command to the Sidecart. 2 bytes of payload
;    movem.l (sp)+, d0-d7/a0-a6
                                    ; get XBIOS call number
    cmp.w #8,6(a0)                  ; is it XBIOS call Flopwr?
    beq _floppy_read                ; if yes, go to flopppy read
    cmp.w #9,6(a0)                  ; is it XBIOS call Floprd?
    beq _floppy_write               ; if yes, go to flopppy write
    cmp.w #13,6(a0)                 ; is it XBIOS call Flopver?
    beq.s _floppy_verify            ; if yes, go to flopppy verify
    cmp.w #10,6(a0)                 ; is it XBIOS call Flopfmt?
    beq.s _floppy_format            ; if yes, go to flopppy format
    move.l old_XBIOS_trap, -(sp)    ; if not, continue with XBIOS call
    rts 

    ;
    ; Trapped XBIOS call 13 with the floppy disk drive verify function
    ; Return always verified successfully
    ;
_floppy_verify:
    cmp.w #1,16(a0)
    bne.s _floppy_verify_emulated_a   ; if is not B then is A
    clr.w 16(a0)                      ; Map B drive to physical A
    move.l old_XBIOS_trap, -(sp)      ; continue with XBIOS call
    rts 

_floppy_verify_emulated_a:
    clr.l d0                           ; If SidecarT, always verified successfully
    move.l  d0, 8(a0)                  ; Set the error code to 0 in buff
    rte

    ;
    ; Trapped XBIOS call 10 with the floppy disk drive format function
    ; Return always formatted with errors
_floppy_format:
;    movem.l d0-d7/a0-a6,-(sp)
;    send_sync CMD_DEBUG, 16             ; Send the command to the Sidecart. 2 bytes of payload
;    movem.l (sp)+, d0-d7/a0-a6
    move.l old_XBIOS_trap, -(sp)      ; continue with XBIOS call
    rts

    cmp.w #1,16(a0)
    bne.s _floppy_format_emulated_a   ; if is not B then is A
    clr.w 16(a0)                      ; Map B drive to physical A
    move.l old_XBIOS_trap, -(sp)      ; continue with XBIOS call
    rts
_floppy_format_emulated_a:
    moveq #-1, d0                     ; If SidecarT, always formatted with errors
    clr.l 8(a0)                       ; Set the error code to 0 in the buff
    rte

    ;
    ; Trapped XBIOS calls 9 with the floppy disk drive write functions
    ;
_floppy_write:
    moveq #1, d0                       ; d0 = write flag
    tst.w 16(a0)
    beq.s _floppy_xbios_emulated_a     ; if is not B then is A
    bra.s _floppy_xbios_emulated_b

    ;
    ; Trapped XBIOS calls 8 with the floppy disk drive read functions
    ;
_floppy_read:
    moveq #0, d0                       ; d0 = read flag
    tst.w 16(a0)
    beq.s _floppy_xbios_emulated_a     ; if is not B then is A

_floppy_xbios_emulated_b:
    movem.l d3-d7/a3-a6, -(sp)
    move.w BPB_data_B, d2      ; Sector size of the emulated drive B
    moveq #1, d4               ; Use B:
    moveq #0, d6
    move.w 20(a0),d6           ; track number
    mulu secpcyl_B,d6          ; calculate sectors per cylinder
    move.w 22(a0),d4           ; Get the side number
    mulu secptrack_B,d4        ; calculate sectors per track
    bra.s _floppy_xbios_emulated

_floppy_xbios_emulated_a:
    movem.l d3-d7/a3-a6, -(sp)
    move.w BPB_data_A, d2      ; Sector size of the emulated drive A
    moveq #0, d4               ; Use A:
    moveq #0, d6
    move.w 20(a0),d6           ; track number
    mulu secpcyl_A,d6          ; calculate sectors per cylinder
    move.w 22(a0),d4           ; Get the side number
    mulu secptrack_A,d4        ; calculate sectors per track

_floppy_xbios_emulated:
    add.l d4, d6               ; d6 = track number * sec/cyl + side number * sec/track
    add.w 18(a0),d6            ; d6 = side no * sec/cyl + track no * sec/track + start sect no
    subq.w #1,d6               ; d6 = logical sector number to start the transfer
    
    move.w 24(a0),d1           ; number of sectors to read/write
    move.l 8(a0),a4            ; buffer address
    move.l d0, d5              ; rwflag
    bsr do_transfer_sidecart

    movem.l (sp)+,d3-d7/a3-a6
    clr.l d0
    rte


bios_trap:
    btst #5, (sp)                         ; Check if called from user mode
    beq.s _bios_trap_user_mode            ; if so, do correct stack pointer
_bios_trap_not_user_mode:
    move.l sp,a0                          ; Move stack pointer to a0
    bra.s _bios_trap_check_cpu
_bios_trap_user_mode:
    move.l usp,a0                         ; if user mode, correct stack pointer
    subq.l #6,a0
;
; This code checks if the CPU is a 68000 or not
;
_bios_trap_check_cpu:
    tst.w _longframe                          ; Check if the CPU is a 68000 or not
    beq.s _bios_trap_notlong
_bios_trap_long:
    addq.w #2, a0                             ; Correct the stack pointer parameters for long frames
_bios_trap_notlong:

;
; Handler goes here
;

;    movem.l d0-d7/a0-a6,-(sp)
;    move.w 6(a0), d3                     ; get BIOS call number
;    send_sync CMD_DEBUG, 2    ; Send the command to the Sidecart. 2 bytes of payload
;    movem.l (sp)+, d0-d7/a0-a6

    cmp.w #Getbpb,6(a0)                  ; is it BIOS call Getbpb?
    beq.s bios_getbpb                    ; if yes, go to getbpb
    cmp.w #Mediach,6(a0)                 ; is it BIOS call Mediach?
    beq.s bios_mediach                   ; if yes, go to media change
    cmp.w #Rwabs,6(a0)                   ; is it BIOS call Rwabs?
    beq.s bios_rwabs                       ; if yes, go to rwabs


    move.l old_bios_handler, -(sp) ; Save the old BIOS handler
    rts


bios_getbpb:
    cmp.w #0,8(a0)              ; Is this the disk_number we are emulating? 
    beq.s _bios_get_bpb_load_emul_bpp_A      ; If is the disk A to emulate, load the BPB A built 
    cmp.w #1,8(a0)              ; Is it the Drive B?
    beq.s _bios_get_bpb_load_emul_bpp_B      ; If is the disk B to emulate, load the BPB B built 
_bios_get_bpb_not_emul_bpp:
    move.l old_bios_handler,-(sp)
    rts

_bios_get_bpb_load_emul_bpp_A:
    ; Test Drive A
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq.s _bios_get_bpb_not_emul_bpp
    ; Emulate A
    move.l #BPB_data_A,d0         ; Load the emulated BPP A
    rte  

_bios_get_bpb_load_emul_bpp_B:
    ; Test Drive B
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _bios_get_bpb_not_emul_bpp
    move.l #BPB_data_B,d0         ; Load the emulated BPP B
    rte

bios_mediach:
    cmp.w #0,8(a0)              ; Is this the disk_number we are emulating? 
    beq.s _bios_mediach_changed_A      ; If is the disk A to emulate, media changed A
    cmp.w #1,8(a0)              ; Is it the Drive B?
    beq.s _bios_mediach_changed_B      ; If is the disk B to emulate, media changed B
_bios_mediach_continue:
    move.l old_bios_handler, -(sp) ; Save the old BIOS handler
    rts

_bios_mediach_changed_A:
    ; Test Drive A
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq.s _bios_mediach_continue
;    move.l (FLOPPY_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_A * 4)),d0
    clr.l d0
    rte

_bios_mediach_changed_B:
    ; Test Drive B
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _bios_mediach_continue
;    move.l (FLOPPY_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_B * 4)),d0
    clr.l d0
    rte


bios_rwabs:
    cmp.w #0, 18(a0)         ; Is this the disk_number we are emulating?
    beq.s _bios_rwabs_a      ; If is the disk A to emulate, load the BPB A built
    cmp.w #1, 18(a0)         ; Is it the Drive B?
    beq.s _bios_rwabs_b      ; If is the disk B to emulate, load the BPB B built
_bios_rwabs_continue:
    move.l old_bios_handler, -(sp) ; Save the old BIOS handler
    rts
_bios_rwabs_a:
    ; Test Drive A
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq.s _bios_rwabs_continue
    ; Emulate A
    tst.l 10(a0)             ; Check if buffer address is 0
    bne.s _bios_rwabs_a_continue
    moveq #0, d0               ; return ok  
    rte
_bios_rwabs_a_continue:
    movem.l d3-d7/a3-a6, -(sp)
    moveq #0, d4               ; Use A:
    move.w BPB_data_A, d2      ; Sector size of the emulated drive A
    move.w 16(a0),d6           ; start sect no
    move.w 14(a0),d1           ; number of sectors to read/write
    move.l 10(a0),a4           ; buffer address
    move.w 8(a0), d5           ; rwflag
    and.l #%1, d5              ; only rw bit
    bsr.s do_transfer_sidecart
    movem.l (sp)+,d3-d7/a3-a6
    rte
_bios_rwabs_b:
    ; Test Drive B
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _bios_rwabs_continue
    ; Emulate B
    tst.l 10(a0)             ; Check if buffer address is 0
    bne.s _bios_rwabs_b_continue
    moveq #0, d0               ; return ok
    rte
_bios_rwabs_b_continue:
    movem.l d3-d7/a3-a6, -(sp)
    moveq #1, d4               ; Use B:
    move.w BPB_data_B, d2      ; Sector size of the emulated drive B
    move.w 16(a0),d6           ; start sect no
    move.w 14(a0),d1           ; number of sectors to read/write
    move.l 10(a0),a4           ; buffer address
    move.w 8(a0), d5           ; rwflag
    and.l #%1, d5              ; only rw bit
    bsr.s do_transfer_sidecart
    movem.l (sp)+,d3-d7/a3-a6
    clr.l d0
    rte




; Perform the transference of the data from/to the emulated disk in RP2040 
; to the computer
; Input registers;
;  d1: number of sectors to read/write
;  d2: sector size in bytes
;  d4: disk drive number to read (0 = A:, 1 = B:)
;  d5: rwflag for read/write
;  d6: logical sector number to start the transfer
;  a4: buffer address in the computer memory
; Output registers:
;  none
do_transfer_sidecart:
    ; START: WE MUST 'NOP' 16 BYTES HERE
    move.b MEGASTE_SPEED_CACHE_REG.w, -(sp)        ; Save the old value of cpu speed. 4 BYTES
	and.b #%00000001,MEGASTE_SPEED_CACHE_REG.w     ; disable MSTe cache. 6 BYTES
	bclr.b #0,MEGASTE_SPEED_CACHE_REG.w            ; set CPU speed at 8mhz. 6 BYTES
    ; END: WE MUST 'NOP' 16 BYTES HERE
    tst.w d5                    ; test rwflag
    bne write_sidecart          ; if not, write
read_sidecart:
    bsr.s read_sectors_from_sidecart
    bra.s exit_transfer_sidecart
write_sidecart:
    bsr.s write_sectors_from_sidecart

exit_transfer_sidecart:
    ; START: WE MUST 'NOP' 4 BYTES HERE
    move.b (sp)+, MEGASTE_SPEED_CACHE_REG.w   ; Restore the old value of cpu speed. 4 BYTES
    ; END: WE MUST 'NOP' 4 BYTES HERE
    rts

; Read sectors from the sidecart
; Input registers:
;  d1: number of sectors to read
;  d2: sector size in bytes
;  d4: disk drive number to read (0 = A:, 1 = B:)
;  d6: logical sector number to start the transfer
;  a4: address in the computer memory to store the data
; Output registers:
;  d0: error code, 0 if no error
;  a4: next address in the computer memory to store the data
read_sectors_from_sidecart:
    subq.w #1,d1                ; one less
_sectors_to_read:
    bsr.s read_sector_from_sidecart
    tst.w d0
    bne.s _read_sectors_from_sidecart_error
    addq #1,d6
    dbf d1, _sectors_to_read
_read_sectors_from_sidecart_error:
    rts

; Write sectors from the sidecart
; Input registers:
;  d1: number of sectors to write
;  d2: sector size in bytes
;  d4: disk drive number to read (0 = A:, 1 = B:)
;  d6: logical sector number to start the transfer
;  a4: address in the computer memory to retrieve the data
; Output registers:
;  d0: error code, 0 if no error
;  a4: next address in the computer memory to retrieve the data
write_sectors_from_sidecart:
    subq.w #1,d1                ; one less
_sectors_to_write:
    bsr.s write_sector_to_sidecart
    tst.w d0
    bne.s _error_sectors_to_write
    addq #1,d6
    dbf d1, _sectors_to_write
_error_sectors_to_write:
    rts

; Read a sector from the sidecart
; Input registers:
;  d2: sector size in bytes
;  d4: disk drive number to read (0 = A:, 1 = B:)
;  d6: logical sector number to start the transfer
;  a4: address in the computer memory to write
; Output registers:
;  d0: error code, 0 if no error
;  a0: next address in the computer memory to store
read_sector_from_sidecart:
    ; Implement the code to read a sector from the sidecart
    move.w #CMD_RETRIES_COUNT, d5        ; Set the number of retries
.read_sector_from_sidecart_retry:
    movem.l d1-d2, -(sp)                 ; Save the registers
    move.w d6,d3                         ; Payload is the logical sector number
    swap d3
    move.w d2,d3                         ; Payload is the sector size
    moveq.l #8, d1                       ; Set the payload size of the command
    move.l #CMD_READ_SECTOR,d0           ; Command code
    bsr send_sync_command_to_sidecart    ; Send the command to the Multi-device
    swap d3                              ; We can restore the register if swapping h and l   
    swap d4                              ; We can restore the register if swapping h and l
    movem.l (sp)+, d1-d2                 ; Restore the registers
    tst.w d0                             ; Check the result of the command
    beq.s .read_sector_from_sidecart_ok  ; If the command was ok, exit
    dbf d5, .read_sector_from_sidecart_retry    ;If the command failed, retry
.read_sector_from_sidecart_ok:
    tst.w d0
    bne.s _error_reading_sector
    clr.l d0                            ; Clear the error code
    move.w d2, d5                       ; Save in d5 the number of bytes to copy
    move.l #sidecart_read_buf, a1
    lsr.w #2, d5
    subq.w #1,d5                        ; one less
    move.l a4, d3
    btst #0,d3                          ; If it's even, take the fast lane. If it's odd, take the slow lane
    bne.s _copy_sector_byte_odd
_copy_sector_byte_even:
    move.l (a1)+, (a4)+
    dbf d5, _copy_sector_byte_even
    moveq #0, d0
    rts
_error_reading_sector:
    moveq #-1, d0
    rts
_copy_sector_byte_odd:
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    dbf d5, _copy_sector_byte_odd
    moveq #0, d0
    rts


; Write a sector to the sidecart
; Input registers:
;  d2: sector size in bytes
;  d4: disk drive number to read (0 = A:, 1 = B:)
;  d6: logical sector number to start the transfer
;  a4: address in the computer memory to write
; Output registers:
;  d0: error code, 0 if no error
;  a4: next address in the computer memory to retrieve
write_sector_to_sidecart:
    ; Implement the code to write a sector to the sidecart
    move.w #CMD_RETRIES_COUNT, d5        ; Set the number of retries
_write_sector_to_sidecart_retry:
    movem.l d1-d6/a4, -(sp)                 ; Save the registers
    move.w d6,d3                            ; Payload is the logical sector number
    swap d3
    move.w d2,d3                            ; Payload is the sector size
    move.l a4, d5                           ; Payload is the address in the computer memory
    moveq.l #0, d6
    move.w d2,d6                            ; Number of bytes to send
    move.l #CMD_WRITE_SECTOR,d0             ; Command code
    bsr send_sync_write_command_to_sidecart ; Send the command to the Multi-device
    movem.l (sp)+, d1-d6/a4                 ; Restore the registers
    tst.w d0                                ; Check the result of the command
    ; If the command was ok, exit. Otherwise, retry
    beq.s _write_sector_to_sidecart_once_ok 
    dbf d5, _write_sector_to_sidecart_retry
_error_writing_sector:
    rts
_write_sector_to_sidecart_once_ok:
    clr.l d0                                ; Clear the error code
    and.l #$0000FFFF,d2                     ; limit the size of the sectors to 65535
    add.l d2, a4                            ; Move the address to the next sector
    rts

; Shared functions included at the end of the file
; Don't forget to include the macros for the shared functions at the top of file
    include "inc/sidecart_functions.s"

        even
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
floppy_end: