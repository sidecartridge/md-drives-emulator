; SidecarTridge Multidevice Real Time Clock (RTC) Emulator
; (C) 2023-25 by GOODDATA LABS SL
; License: GPL v3

; Emulate a Real Time Clock from the SidecarT

; Bootstrap the code in ASM
ROM4_START_ADDR:         equ $FA0000 ; ROM4 start address
ROM3_START_ADDR:         equ $FB0000 ; ROM3 start address

RTCEMUL_GAP_SIZE:     	 equ $2000   ; 8KB gap

ROM_EXCHG_BUFFER_ADDR:    equ (ROM4_START_ADDR + $8200)   ; ROM4 buffer address + lower memory offset
RANDOM_TOKEN_ADDR:        equ (ROM_EXCHG_BUFFER_ADDR)
RANDOM_TOKEN_SEED_ADDR:   equ (RANDOM_TOKEN_ADDR + 4)     ; RANDOM_TOKEN_ADDR + 4 bytes
RANDOM_TOKEN_POST_WAIT:   equ $1                          ; Wait this cycles after the random number generator is ready
COMMAND_TIMEOUT           equ $00000FFF ; Timeout for the simple command
COMMAND_WRITE_TIMEOUT     equ $00001FFF ; Timeout for the command with large payload

ROMCMD_START_ADDR:  equ (ROM3_START_ADDR)	      ; We are going to use ROM3 address
CMD_MAGIC_NUMBER    equ ($ABCD) 			  	  ; Magic number header to identify a command
CMD_RETRIES_COUNT   equ 5                         ; Number of retries to send the command

; CONSTANTS
APP_RTCEMUL             equ $0300                           ; MSB is the app code. RTC is $03
CMD_READ_DATETME        equ ($1 + APP_RTCEMUL)              ; Command code to read the date and time from the Sidecart
CMD_SAVE_VECTORS        equ ($2 + APP_RTCEMUL)              ; Command code to save the vectors in the Sidecart
CMD_SET_SHARED_VAR      equ ($3 + APP_RTCEMUL)              ; Command code to set a shared variable in the Sidecart
CMD_SET_TIME            equ ($4 + APP_RTCEMUL)              ; Command code to set the Sidecart's clock from a Settime

RTCEMUL_SHARED_VARIABLES         equ (RANDOM_TOKEN_SEED_ADDR + 4)        ; ROM EXCHANGE BUFFER address
RTCEMUL_SHARED_VARIABLE_SIZE     equ (RTCEMUL_GAP_SIZE / 4) ;  6KB gap divided by 4 bytes per longword
RTCEMUL_SHARED_VARIABLES_COUNT   equ 32  ; Size of the shared variables of the shared functions

SVAR_ENABLED            equ (RTCEMUL_SHARED_VARIABLE_SIZE)      ; Enabled flag

; We will need 32 bytes extra for the variables of the floppy emulator
RTCEMUL_VARIABLES_OFFSET equ (ROM_EXCHG_BUFFER_ADDR + RTCEMUL_GAP_SIZE + RTCEMUL_SHARED_VARIABLES_COUNT)

RTCEMUL_DATETIME_BCD    equ (RTCEMUL_VARIABLES_OFFSET)      ; first variable in the RTC emulator
RTCEMUL_DATETIME_MSDOS  equ (RTCEMUL_DATETIME_BCD + 8)      ; datetime_bcd + 8 bytes
RTCEMUL_OLD_XBIOS       equ (RTCEMUL_DATETIME_MSDOS + 8)    ; datetime_msdos + 8 bytes
RTCEMUL_CLOCK_SET       equ (RTCEMUL_OLD_XBIOS + 4)      ; $FFFFFFFF once the RP's clock has a date (NTP)


XBIOS_TRAP_ADDR         equ $b8                             ; TRAP #14 Handler (XBIOS)
_dskbufp        equ $4c6    ; Address of the disk buffer pointer    
_longframe      equ $59e    ; Address of the long frame flag. If this value is 0 then the processor uses short stack frames, otherwise it uses long stack frames.


; Macros should be included before any function code
    include inc/tos.s
    include inc/sidecart_macros.s

    org $FA3400         ; Start of the code. First 4KB bytes are reserved for the terminal.

rom_function:
    tst.l (RTCEMUL_SHARED_VARIABLES + (SVAR_ENABLED * 4))
    beq _exit_graciouslly ; If the RTC emulation is not enabled

; A little delay to let the rp2040 breathe
;	wait_sec

; Get information about the hardware
    bsr detect_hw
    bsr get_tos_version
_ntp_ready:
    send_sync CMD_READ_DATETME,0         ; Command code to read the date and time
    tst.w d0                            ; 0 if no error
    bne _exit_timemout                   ; The RP2040 is not responding, timeout now

; No date to give when NTP did not answer: every clock is left as it is.
    tst.l RTCEMUL_CLOCK_SET
    beq _exit_graciouslly

    pea RTCEMUL_DATETIME_BCD            ; Buffer should have a valid IKBD date and time format
    move.w #6, -(sp)                    ; Six bytes plus the header = 7 bytes
    move.w #25, -(sp)                   ; 
    trap #14
    addq.l #8, sp

    move.l RTCEMUL_DATETIME_MSDOS, d0
    bsr set_datetime
    tst.w d0
    bne _exit_timemout

; Does TOS's own clock give the date back? A clock chip does (Mega ST from TOS
; 1.02, Mega STE, TT, Falcon), and EmuTOS does with the IKBD's. TOS 1.00-2.06
; with the IKBD's cannot: they write a year from 2000 as a byte the IKBD
; refuses. A Falcon whose clock has no valid time answers -1. Compared to the
; minute: the one case a minute can turn in between only adds the hook, which
; gives the right date anyway.
	move.w #23,-(sp)                    ; gettime from XBIOS
	trap #14
	addq.l #2,sp
    move.l RTCEMUL_DATETIME_MSDOS, d1
    swap d1                             ; the RP keeps the time in the high word
    and.w #$FFE0, d0                    ; no seconds
    and.w #$FFE0, d1
    cmp.l d1, d0
    beq.s _exit_graciouslly

; It does not: Gettime is answered from the RP's clock from now on.
    bsr save_vectors
    tst.w d0
    bne _exit_timemout

_exit_graciouslly:
    rts

_exit_timemout:
    asksil error_sidecart_comm_msg
    rts

save_vectors:
    move.l XBIOS_TRAP_ADDR.w,d3          ; Address of the old XBIOS vector
    send_sync CMD_SAVE_VECTORS,4         ; Send the command to the Sidecart
    tst.w d0                            ; 0 if no error
    bne.s _read_timeout                 ; The RP2040 is not responding, timeout now

    ; Now we have the XBIOS vector in RTCEMUL_OLD_XBIOS
    ; Now we can safely change it to our own vector
    move.l #custom_xbios,XBIOS_TRAP_ADDR.w    ; Set our own vector

    rts

_read_timeout:
    moveq #-1, d0
    rts

custom_xbios:
;    btst #0, RTCEMUL_REENTRY_TRAP      ; Check if the reentry is locked
;    beq.s _custom_bios_trapped         ; If the bit is active, we are in a reentry call. We need to exec_old_handler the code
;
;    move.l RTCEMUL_OLD_XBIOS, -(sp) ; if not, continue with XBIOS call
;    rts 

_custom_bios_trapped:
    btst #5, (sp)                    ; Check if called from user mode
    beq.s _user_mode                 ; if so, do correct stack pointer
_not_user_mode:
    move.l sp,a0                     ; Move stack pointer to a0
    bra.s _check_cpu
_user_mode:
    move.l usp,a0                    ; if user mode, correct stack pointer
    subq.l #6,a0
    bra.s _notlong                  ; a longer frame is on the supervisor stack only
;
; This code checks if the CPU is a 68000 or not
;
_check_cpu:
    tst.w _longframe                ; Check if the CPU is a 68000 or not
    beq.s _notlong
_long:
    addq.w #2, a0                   ; Correct the stack pointer parameters for long frames 
_notlong:
    cmp.w #23,6(a0)                 ; is it XBIOS call 23 / getdatetime?
    beq.s _getdatetime              ; if yes, go to our own routine
    cmp.w #22,6(a0)                 ; is it XBIOS call 22 / setdatetime?
    beq _setdatetime                ; if yes, go to our own routine:
    move.l RTCEMUL_OLD_XBIOS, -(sp) ; if not, continue with XBIOS call
    rts 

; Gettime, answered from the RP's clock: TOS never sees the call. The XBIOS
; gives a caller everything but d0-d2/a0-a2 back, and a send destroys d7 (its
; retry count) and a0-a3.
_getdatetime:
    movem.l d7/a3, -(sp)
    subq.l #2, sp                    ; a Mega STE's setting while the RP is asked
    megaste_cache_off (sp)
    send_sync CMD_READ_DATETME, 0
    ; Read before the cache is back on: it could answer from an old copy
    move.l RTCEMUL_DATETIME_MSDOS, d0
    swap d0                          ; the RP keeps the time in the high word
    megaste_cache_back (sp)
    addq.l #2, sp
    movem.l (sp)+, d7/a3
    rte

; Settime goes to TOS as it came - a Mega ST's or Falcon's chip, and GEMDOS's
; date on TOS 2.06 and 4.04, take it from there - and to the RP's clock, which
; answers Gettime.
_setdatetime:
    movem.l d3/d7/a3, -(sp)
    move.l 8(a0), d3                 ; the date and time, before a send destroys a0
    subq.l #2, sp                    ; a Mega STE's setting while the RP is told
    megaste_cache_off (sp)
    send_sync CMD_SET_TIME, 4
    megaste_cache_back (sp)
    addq.l #2, sp
    movem.l (sp)+, d3/d7/a3
    move.l RTCEMUL_OLD_XBIOS, -(sp) ; continue with the XBIOS call
    rts 

; GEMDOS's date and time, from the RP's clock. The date first: from TOS 1.02
; on, Tsetdate and Tsettime each also set the XBIOS clock, with GEMDOS's other
; half as it is at the time, and the ROM's own date set first would stay in an
; IKBD that cannot take the right year after it.
; d0.l : Date and time in MSDOS format, as the RP keeps them: time in the high word
set_datetime:
    move.l d0, d7

	move.w d7,-(sp)
	move.w #$2b,-(sp)                   ; Tsetdate
	trap #1
	addq.l #4,sp
    tst.w d0
    bne.s _exit_set_time

	swap d7

	move.w d7,-(sp)
	move.w #$2d,-(sp)                   ; Tsettime
	trap #1
	addq.l #4,sp
    tst.w d0
    bne.s _exit_set_time

    ; And we are done!
    moveq #0, d0
    rts
_exit_set_time:
    moveq #-1, d0
    rts

        even
error_sidecart_comm_msg:
        dc.b	$d,$a,"Communication error. Press reset.",$d,$a,0

        even
        dc.l $FFFFFFFF
rom_function_end:


; Shared functions included at the end of the file
; Don't forget to include the macros for the shared functions at the top of file
    include "inc/sidecart_functions.s"

end_pre_auto:
	even
	dc.l 0