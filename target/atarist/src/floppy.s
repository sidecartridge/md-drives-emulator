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
COMMAND_TIMEOUT             equ $0006FFFF ; Timeout for the simple command
COMMAND_WRITE_TIMEOUT       equ $0006FFFF ; Timeout for track/sector write on slow SD cards (~800 ms)

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
CMD_FORMAT_TRACK    equ ($8 + APP_FLOPPYEMUL)     ; Flopfmt on an emulated drive: refused
CMD_SHOW_VECTOR_CALL equ ($B + APP_FLOPPYEMUL)    ; Command code to show the XBIOS vector call 
CMD_DEBUG            equ ($C + APP_FLOPPYEMUL)    ; Command code to send to the RP2040 the debug command

FLOPPY_SHARED_VARIABLES             equ (RANDOM_TOKEN_SEED_ADDR + 4)        ; ROM EXCHANGE BUFFER address
FLOPPYEMUL_SHARED_VARIABLE_SIZE     equ (FLOPPYEMUL_GAP_SIZE / 4) ;  6KB gap divided by 4 bytes per longword
FLOPPYEMUL_SHARED_VARIABLES_COUNT   equ 32  ; Size of the shared variables of the shared functions

SVAR_XBIOS_TRAP_ENABLED:    equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 0)      ; XBIOS trap enabled
SVAR_BOOT_ENABLED:          equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 1)      ; Boot sector enabled
SVAR_EMULATION_MODE:        equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 2)      ; Emulation mode
SVAR_ENABLED:               equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 3)      ; Enabled flag
SVAR_MEDIA_CHANGED_A:       equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 4)      ; Media change state A
SVAR_MEDIA_CHANGED_B:       equ (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 5)      ; Media change state B

MED_NOCHANGE:               equ 0
MED_CHANGED:                equ 2

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

; What the last read or write answered: 0, or the BIOS error TOS's own floppy
; driver gives for that failure. The RP writes it before the token.
transfer_status:       equ (disk_number_B + 2) ; disk_number_B + 2 bytes

; After the variables, allocate the buffer to read/write sectors
sidecart_read_buf:     equ (FLOPPYEMUL_VARIABLES_OFFSET + 256)

; CONSTANTS
SECTOR_SIZE     equ 512     ; A .ST image is 512-byte sectors, whatever its boot
                            ; sector claims: the BPB is only what GEMDOS is told
_hdv_bpb        equ $472    ; The disk vectors every TOS disk driver hooks
_hdv_rw         equ $476
_hdv_mediach    equ $47e
XBIOS_trap      equ $b8     ; TRAP #14 Handler (XBIOS)
_membot         equ $432    ; This value represents last memory used by the TOS, and start of the heap area available
_bootdev        equ $446    ; This value represents the device from which the system was booted (0 = A:, 1 = B:, etc.)

_nflops         equ $4a6    ; This value indicates the number of floppy drives currently connected to the system
_drvbits        equ $4c2    ; Each of 32 bits in this longword represents a drive connected to the system. Bit #0 is A, Bit #1 is B and so on.
_dskbufp        equ $4c6    ; Address of the disk buffer pointer    
etv_critic      equ $404    ; The critical error handler
_longframe      equ $59e    ; Address of the long frame flag. If this value is 0 then the processor uses short stack frames, otherwise it uses long stack frames.


; Macros should be included before any function code
    include inc/tos.s
    include inc/sidecart_macros.s

    
    org $FA2A00         ; Start of the code. First 4KB bytes are reserved for the terminal.

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

    bsr set_vectors_hdv

    bsr set_vector_xbios

    bra boot_disk

.exit_graciouslly:
    rts


; Save the three disk vectors and install ours. Each old vector goes to the RP
; first, into its XBRA slot in the read-only window, and ours are installed
; only once all three are there: until then a call this driver passes on would
; jump to 0. These are the vectors every TOS disk driver hooks, not the BIOS
; trap in front of them, so that what wraps them sees the emulated drives as
; TOS's own. The desktop's Esc does: it wraps them to force a media change on
; the window's drive and counts on GEMDOS's Getbpb for that drive going
; through its wrapper to put them back. Answered at the trap, it never did, and
; the next Esc wrapped the wrappers around themselves: every disk call behind
; them - B:, a physical drive, ACSI - then looped for ever.
set_vectors_hdv:
    move.l _hdv_bpb.w, d3
    move.l #old_hdv_bpb, d4
    bsr.s save_vector
    bne.s _dont_set_hdv
    move.l _hdv_mediach.w, d3
    move.l #old_hdv_mediach, d4
    bsr.s save_vector
    bne.s _dont_set_hdv
    move.l _hdv_rw.w, d3
    move.l #old_hdv_rw, d4
    bsr.s save_vector
    bne.s _dont_set_hdv
    move.l #floppy_hdv_bpb, _hdv_bpb.w
    move.l #floppy_hdv_mediach, _hdv_mediach.w
    move.l #floppy_hdv_rw, _hdv_rw.w
_dont_set_hdv:
    rts

; The RP writes vector d3 into slot d4. Z set once the slot holds it.
save_vector:
    send_sync CMD_SAVE_BIOS_VECTOR, 12  ; d5, the third long, is not used
    bne.s _save_vector_done             ; no answer
    move.l d4, a0
    cmp.l (a0), d3                      ; send_sync keeps d3 and d4
_save_vector_done:
    rts



; Save the old xbios vectors and install
set_vector_xbios:
    move.l XBIOS_trap.w,d3              ; Payload is the old XBIOS_trap
    send_sync CMD_SAVE_VECTORS, 4
    bne.s _dont_set_xbios_trap          ; no answer
    cmp.l old_XBIOS_trap, d3            ; answered, and the old vector is there?
    bne.s _dont_set_xbios_trap
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
    ; And the current drive, which GEMDOS took from _bootdev when it started -
    ; before this runs, and _bootdev survives a reset: it would still be the
    ; drive of the session before, and TOS looks for \AUTO\ on the current
    ; drive. A driver that sets _bootdev after this one sets both too.
    clr.w -(sp)
    gemdos Dsetdrv, 4
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
    moveq #0, d4            ; Read from drive A
    move.l #SECTOR_SIZE, d2 ; Sector size
    move.l _membot.w,a4        ; Start reading at $2000
    bsr read_sector_from_sidecart
    tst.w d0
    bne.s _dont_boot        ; nothing was read: _membot holds no boot sector

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

; New XBIOS: Floprd, Flopwr and Flopver for an emulated drive are served from
; the Sidecart, and Flopfmt is refused there. Every other call, and those four
; for a drive that is not emulated, go to TOS untouched: a hook on the XBIOS
; trap sees every program's XBIOS calls, not only the floppy ones.
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
    move.w 6(a0), d0                ; get XBIOS call number
    cmp.w #Floprd, d0               ; is it XBIOS call Floprd?
    beq.s _floppy_xbios_call
    cmp.w #Flopwr, d0               ; is it XBIOS call Flopwr?
    beq.s _floppy_xbios_call
    ; Flopver is 19, $13 in hex. XBIOS 13 decimal is Mfpint, and taking it
    ; here kept programs from installing MFP interrupt handlers.
    cmp.w #Flopver, d0              ; is it XBIOS call Flopver?
    beq.s _floppy_xbios_call
    cmp.w #Flopfmt, d0              ; is it XBIOS call Flopfmt?
    beq.s _floppy_xbios_call
_floppy_xbios_not_ours:
    move.l old_XBIOS_trap, -(sp)    ; if not, continue with XBIOS call
    rts

    ;
    ; Floprd, Flopwr and Flopver take the same arguments: buf, filler, devno,
    ; sectno, trackno, sideno, count; Flopfmt has devno in the same place. d0
    ; says which of the four this is.
    ;
_floppy_xbios_call:
    move.w 16(a0), d1               ; devno
    beq _floppy_xbios_a
    cmp.w #1, d1
    bne.s _floppy_xbios_not_ours    ; neither A: nor B:
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _floppy_xbios_not_ours    ; a B: that is not emulated is TOS's
    movem.l d3-d7/a3-a6, -(sp)
    move.w #SECTOR_SIZE, d2         ; Sector size
    move.l #$10001, d4              ; B:, and d4.h 1: a sector of the disk as it is
    moveq #0, d6
    move.w 20(a0),d6                ; track number
    mulu secpcyl_B,d6               ; times the sectors per cylinder
    moveq #0, d3
    move.w 22(a0),d3                ; side number
    mulu secptrack_B,d3             ; times the sectors per track
    bra _floppy_xbios_emulated

_floppy_xbios_a:
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq _floppy_xbios_not_ours      ; an A: that is not emulated is TOS's
    movem.l d3-d7/a3-a6, -(sp)
    move.w #SECTOR_SIZE, d2         ; Sector size
    move.l #$10000, d4              ; A:, and d4.h 1: a sector of the disk as it is
    moveq #0, d6
    move.w 20(a0),d6                ; track number
    mulu secpcyl_A,d6               ; times the sectors per cylinder
    moveq #0, d3
    move.w 22(a0),d3                ; side number
    mulu secptrack_A,d3             ; times the sectors per track

; d4 is the drive number do_transfer_sidecart sends, so the side goes through
; d3. It used to go through d4, which sent every side-1 transfer to B:. Its
; high word 1 tells the RP this is a sector of the disk as it physically is,
; placed with the image's geometry (secpcyl, secptrack); Rwabs sends 0 there,
; a record, which the RP places with the boot sector's own geometry as TOS's
; floprw does.
_floppy_xbios_emulated:
    cmp.w #Flopfmt, d0
    beq.s _floppy_xbios_format
    add.l d3, d6               ; d6 = track number * sec/cyl + side number * sec/track
    add.w 18(a0),d6            ; + the sector number
    subq.w #1,d6               ; sectors are numbered from 1: d6 = logical sector
    move.w 24(a0),d1           ; number of sectors to read/write
    move.l 8(a0),a4            ; buffer address
    cmp.w #Flopver, d0
    beq.s _floppy_xbios_verify
    moveq #0, d5               ; Floprd reads
    cmp.w #Flopwr, d0
    bne.s _floppy_xbios_transfer
    moveq #1, d5               ; Flopwr writes
_floppy_xbios_transfer:
    bsr do_transfer_sidecart   ; d0: 0, or the error, which Floprd and Flopwr
                               ; return as TOS's do, without etv_critic
    movem.l (sp)+,d3-d7/a3-a6
    rte

; Flopver reads each sector into the start of the caller's buffer and then
; leaves there the list of the sectors that failed, ended by a zero word, as
; TOS does. One sector per transfer: the buffer need only hold 1024 bytes,
; whatever the count, and is never written past its first sector.
_floppy_xbios_verify:
    move.l a4, a5              ; where each sector is read, and the list goes
    move.w 18(a0), -(sp)       ; the sector being verified, as the caller counts
    move.w d1, -(sp)           ; and how many are left
_floppy_xbios_verify_next:
    tst.w (sp)
    beq.s _floppy_xbios_verified
    moveq #1, d1               ; one sector
    moveq #0, d5               ; read
    move.l a5, a4
    bsr do_transfer_sidecart   ; moves d6 on to the next sector when it succeeds
    tst.w d0
    bne.s _floppy_xbios_verify_failed
    subq.w #1, (sp)
    addq.w #1, 2(sp)
    bra.s _floppy_xbios_verify_next
_floppy_xbios_verified:
    addq.l #4, sp
    clr.w (a5)                 ; no bad sector
    moveq #0, d0
    movem.l (sp)+,d3-d7/a3-a6
    rte
_floppy_xbios_verify_failed:
    move.w 2(sp), (a5)+        ; the sector that failed
    clr.w (a5)                 ; and the end of the list
    addq.l #4, sp
    movem.l (sp)+,d3-d7/a3-a6
    rte                        ; with the error in d0


; Flopfmt on an emulated drive formats nothing. Handed to the ROM it formatted
; whatever disk was in the physical drive, and the desktop's writes that follow
; it landed on the image. The RP answers as TOS answers a disk it cannot
; format: write protected for a read-only image, the general error otherwise.
_floppy_xbios_format:
    moveq #0, d3
    move.w d4, d3              ; the drive
    send_sync CMD_FORMAT_TRACK, 4
    tst.w d0
    beq.s _floppy_xbios_format_answered
    moveq #-1, d0              ; no answer: the general error
    bra.s _floppy_xbios_format_done
_floppy_xbios_format_answered:
    move.l transfer_status, d0
_floppy_xbios_format_done:
    movem.l (sp)+,d3-d7/a3-a6
    rte


    ds.b ((4 - (* & 3)) & 3)            ; the XBRA header on a long boundary
    dc.l 'XBRA'
    dc.l 'SDFE'                         ; SidecarTridge Floppy Emulator
old_hdv_bpb:
    dc.l 0                              ; written by the RP (read-only window)
; hdv_bpb is called with its arguments at 4(sp); with a0 4 bytes below them they
; are at the offsets the BIOS trap's frame gave this code.
floppy_hdv_bpb:
    lea -4(sp), a0
    cmp.w #0,8(a0)              ; Is this the disk_number we are emulating? 
    beq.s _bios_get_bpb_load_emul_bpp_A      ; If is the disk A to emulate, load the BPB A built 
    cmp.w #1,8(a0)              ; Is it the Drive B?
    beq.s _bios_get_bpb_load_emul_bpp_B      ; If is the disk B to emulate, load the BPB B built 
_bios_get_bpb_not_emul_bpp:
    move.l old_hdv_bpb, -(sp)         ; not ours: the vector it replaced
    rts

_bios_get_bpb_load_emul_bpp_A:
    ; Test Drive A
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq.s _bios_get_bpb_not_emul_bpp
    ; Emulate A
    move.l #BPB_data_A,d0         ; Load the emulated BPP A
    bra.s _bios_get_bpb_check

_bios_get_bpb_load_emul_bpp_B:
    ; Test Drive B
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _bios_get_bpb_not_emul_bpp
    move.l #BPB_data_B,d0         ; Load the emulated BPP B
; No BPB, as TOS answers, for a boot sector whose sector size is not positive
; or whose cluster size is 0: a disk with no file system on it, whose numbers
; GEMDOS would otherwise divide by.
_bios_get_bpb_check:
    move.l d0, a1
    tst.w (a1)                    ; recsiz, as a signed word
    ble.s _bios_get_bpb_none
    tst.w 2(a1)                   ; clsiz
    beq.s _bios_get_bpb_none
    ; As TOS's getbpb: answering a BPB ends a media change on that drive.
    ; GEMDOS asks for it right after Mediach or Rwabs told it of the change.
    moveq #0, d2
    move.w 8(a0), d2              ; the drive
    move.w d2, d1
    lsl.w #2, d1                  ; SVAR_MEDIA_CHANGED_B follows A's
    lea (FLOPPY_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_A * 4)), a1
    cmp.l #MED_CHANGED, 0(a1, d1.w)
    bne.s _bios_get_bpb_done
    move.l d0, -(sp)              ; the BPB, which the send does not keep
    move.l d4, -(sp)
    moveq #MED_NOCHANGE, d4
    bsr set_media_change
    move.l (sp)+, d4
    move.l (sp)+, d0
    rts
_bios_get_bpb_none:
    moveq #0, d0
_bios_get_bpb_done:
    rts

    ds.b ((4 - (* & 3)) & 3)            ; the XBRA header on a long boundary
    dc.l 'XBRA'
    dc.l 'SDFE'                         ; SidecarTridge Floppy Emulator
old_hdv_mediach:
    dc.l 0                              ; written by the RP (read-only window)
; hdv_mediach is called with its arguments at 4(sp); with a0 4 bytes below them they
; are at the offsets the BIOS trap's frame gave this code.
floppy_hdv_mediach:
    lea -4(sp), a0
    cmp.w #0,8(a0)              ; Is this the disk_number we are emulating? 
    beq.s _bios_mediach_changed_A      ; If is the disk A to emulate, media changed A
    cmp.w #1,8(a0)              ; Is it the Drive B?
    beq.s _bios_mediach_changed_B      ; If is the disk B to emulate, media changed B
_bios_mediach_continue:
    move.l old_hdv_mediach, -(sp)         ; not ours: the vector it replaced
    rts

_bios_mediach_changed_A:
    ; Test Drive A
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq.s _bios_mediach_continue
    move.l (FLOPPY_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_A * 4)),d0
    rts

_bios_mediach_changed_B:
    ; Test Drive B
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _bios_mediach_continue
    move.l (FLOPPY_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_B * 4)),d0
    rts


    ds.b ((4 - (* & 3)) & 3)            ; the XBRA header on a long boundary
    dc.l 'XBRA'
    dc.l 'SDFE'                         ; SidecarTridge Floppy Emulator
old_hdv_rw:
    dc.l 0                              ; written by the RP (read-only window)
; hdv_rw is called with its arguments at 4(sp); with a0 4 bytes below them they
; are at the offsets the BIOS trap's frame gave this code.
floppy_hdv_rw:
    lea -4(sp), a0
    cmp.w #0, 18(a0)         ; Is this the disk_number we are emulating?
    beq.s _bios_rwabs_a      ; If is the disk A to emulate, load the BPB A built
    cmp.w #1, 18(a0)         ; Is it the Drive B?
    beq.s _bios_rwabs_b      ; If is the disk B to emulate, load the BPB B built
_bios_rwabs_continue:
    move.l old_hdv_rw, -(sp)         ; not ours: the vector it replaced
    rts
_bios_rwabs_a:
    ; Test Drive A
    btst   #0, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 0: Emulate A
    beq.s _bios_rwabs_continue
    moveq #0, d2               ; Use A:
    bra.s _bios_rwabs_emulated
_bios_rwabs_b:
    ; Test Drive B
    btst   #1, (FLOPPY_SHARED_VARIABLES + (SVAR_EMULATION_MODE * 4) + 3) ; Bit 1: Emulate B
    beq.s _bios_rwabs_continue
    moveq #1, d2               ; Use B:
; As TOS's floppy Rwabs: a NULL buffer sets the drive's media-change state to
; the count, and modes 0 and 1 answer E_CHNG on a changed disk without a
; transfer, so that GEMDOS logs the drive in again - its Getbpb ends the change.
_bios_rwabs_emulated:
    tst.l 10(a0)               ; buffer
    bne.s _bios_rwabs_check_change
    move.l d4, -(sp)
    moveq #MED_NOCHANGE, d4    ; a count of 1, "may have changed", is not
    cmp.w #MED_CHANGED, 14(a0) ; changed here: TOS would find the same serial
    bne.s _bios_rwabs_set_change
    moveq #MED_CHANGED, d4
_bios_rwabs_set_change:
    bsr set_media_change
    move.l (sp)+, d4
    moveq #0, d0
    rts
_bios_rwabs_check_change:
    cmp.w #2, 8(a0)            ; TOS asks only in modes 0 and 1
    bge.s _bios_rwabs_go
    move.w d2, d1
    lsl.w #2, d1               ; SVAR_MEDIA_CHANGED_B follows A's
    lea (FLOPPY_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_A * 4)), a1
    cmp.l #MED_CHANGED, 0(a1, d1.w)
    bne.s _bios_rwabs_go
    moveq #E_CHNG, d0
    rts
_bios_rwabs_go:
    movem.l d3-d7/a3-a6, -(sp)
    move.l d2, d4              ; the drive
    move.w #SECTOR_SIZE, d2    ; Sector size
    move.l a0, a5              ; the arguments: a send does not keep a0
_bios_rwabs_transfer:
    move.w 16(a5),d6           ; start sect no
    move.w 14(a5),d1           ; number of sectors to read/write
    move.l 10(a5),a4           ; buffer address
    move.w 8(a5), d5           ; rwflag
    and.l #%1, d5              ; only rw bit
    bsr do_transfer_sidecart
    tst.l d0
    beq.s _bios_rwabs_done
    ; A failure goes to the critical error handler, as TOS's floppy driver
    ; sends it: that is the desktop's alert. Its answer is what Rwabs returns,
    ; unless it is $10000, Retry, and the transfer is made again.
    move.l d2, -(sp)           ; the handler may use d0-d2 and a0-a2
    move.w d4, -(sp)           ; 6(sp) for the handler: the drive
    move.w d0, -(sp)           ; 4(sp): the error
    move.l etv_critic.w, a0
    jsr (a0)
    addq.l #4, sp
    move.l (sp)+, d2
    cmp.l #$10000, d0
    bne.s _bios_rwabs_done
    ; Retry on a disk that was changed meanwhile - a slot cycled - is E_CHNG,
    ; as in TOS, so GEMDOS rereads the new disk instead of having the old one's
    ; sectors written on it. TOS asks only in modes 0 and 1.
    cmp.w #2, 8(a5)
    bge.s _bios_rwabs_transfer
    move.w d4, d0
    lsl.w #2, d0               ; SVAR_MEDIA_CHANGED_B follows A's
    lea (FLOPPY_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_A * 4)), a0
    cmp.l #MED_CHANGED, 0(a0, d0.w)
    bne.s _bios_rwabs_transfer
    moveq #E_CHNG, d0
_bios_rwabs_done:
    movem.l (sp)+,d3-d7/a3-a6
    rts




; Set drive d2's media-change state to d4. The RP owns the state: it raises
; it when drive A's slot is cycled, and is told here when TOS's rules end or
; set it. Keeps every register but d0.
set_media_change:
    movem.l d3/d7/a0-a3, -(sp)         ; a send uses a0-a3, and send_sync d7
    move.l #SVAR_MEDIA_CHANGED_A, d3
    add.l d2, d3                       ; SVAR_MEDIA_CHANGED_B follows A's
    send_sync CMD_SET_SHARED_VAR, 8
    movem.l (sp)+, d3/d7/a0-a3
    rts

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
    moveq #0, d0                ; a count of zero is answered with 0 at once, as
    tst.w d1                    ; TOS does: subq then dbf made it 65536 sectors
    beq.s _read_sectors_from_sidecart_done
    subq.w #1,d1                ; one less
_sectors_to_read:
    bsr.s read_sector_from_sidecart
    tst.w d0
    bne.s _read_sectors_from_sidecart_error
    addq #1,d6
    dbf d1, _sectors_to_read
_read_sectors_from_sidecart_error:
_read_sectors_from_sidecart_done:
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
    moveq #0, d0                ; a count of zero is answered with 0 at once, as
    tst.w d1                    ; TOS does: subq then dbf made it 65536 sectors
    beq.s _no_sectors_to_write
    subq.w #1,d1                ; one less
_sectors_to_write:
    bsr.s write_sector_to_sidecart
    tst.w d0
    bne.s _error_sectors_to_write
    addq #1,d6
    dbf d1, _sectors_to_write
_error_sectors_to_write:
_no_sectors_to_write:
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
    move.l transfer_status, d0          ; the RP's answer: 0, or the error
    bne.s _read_sector_failed           ; and the buffer is not this sector
    move.w d2, d5                       ; Save in d5 the number of bytes to copy
    move.l #sidecart_read_buf, a1
    lsr.w #2, d5                        ; four bytes a turn: d2 is SECTOR_SIZE
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
_read_sector_failed:
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
    move.l transfer_status, d0              ; the RP's answer: 0, or the error
    bne.s _error_writing_sector
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
