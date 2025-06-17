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
COMMAND_TIMEOUT           equ $0000FFFF                   ; Timeout for the command

ROMCMD_START_ADDR:  equ (ROM3_START_ADDR)	      ; We are going to use ROM3 address
CMD_MAGIC_NUMBER    equ ($ABCD) 			  	  ; Magic number header to identify a command
CMD_RETRIES_COUNT   equ 5                         ; Number of retries to send the command

; CONSTANTS
APP_RTCEMUL             equ $0300                           ; MSB is the app code. RTC is $03
CMD_TEST_NTP            equ ($0 + APP_RTCEMUL)              ; Command code to ping to the Sidecart
CMD_READ_DATETME        equ ($1 + APP_RTCEMUL)              ; Command code to read the date and time from the Sidecart
CMD_SAVE_VECTORS        equ ($2 + APP_RTCEMUL)              ; Command code to save the vectors in the Sidecart
CMD_REENTRY_LOCK        equ ($3 + APP_RTCEMUL)              ; Command code to lock the reentry to XBIOS in the Sidecart
CMD_REENTRY_UNLOCK      equ ($4 + APP_RTCEMUL)              ; Command code to unlock the reentry to XBIOS in the Sidecart
CMD_SET_SHARED_VAR      equ ($5 + APP_RTCEMUL)              ; Command code to set a shared variable in the Sidecart

RTCEMUL_SHARED_VARIABLES         equ (RANDOM_TOKEN_SEED_ADDR + 4)        ; ROM EXCHANGE BUFFER address
RTCEMUL_SHARED_VARIABLE_SIZE     equ (RTCEMUL_GAP_SIZE / 4) ;  6KB gap divided by 4 bytes per longword
RTCEMUL_SHARED_VARIABLES_COUNT   equ 32  ; Size of the shared variables of the shared functions

SVAR_ENABLED            equ (RTCEMUL_SHARED_VARIABLE_SIZE)      ; Enabled flag

; We will need 32 bytes extra for the variables of the floppy emulator
RTCEMUL_VARIABLES_OFFSET equ (ROM_EXCHG_BUFFER_ADDR + RTCEMUL_GAP_SIZE + RTCEMUL_SHARED_VARIABLES_COUNT)

RTCEMUL_DATETIME_BCD    equ (RTCEMUL_VARIABLES_OFFSET)      ; first variable in the RTC emulator
RTCEMUL_DATETIME_MSDOS  equ (RTCEMUL_DATETIME_BCD + 8)      ; datetime_bcd + 8 bytes
RTCEMUL_OLD_XBIOS       equ (RTCEMUL_DATETIME_MSDOS + 8)    ; datetime_msdos + 8 bytes
RTCEMUL_REENTRY_TRAP    equ (RTCEMUL_OLD_XBIOS + 4)         ; old_bios + 4 bytes
RTCEMUL_Y2K_PATCH       equ (RTCEMUL_REENTRY_TRAP + 4)      ; reentry_trap + 4 byte

XBIOS_TRAP_ADDR         equ $b8                             ; TRAP #14 Handler (XBIOS)
_dskbufp        equ $4c6    ; Address of the disk buffer pointer    
_longframe      equ $59e    ; Address of the long frame flag. If this value is 0 then the processor uses short stack frames, otherwise it uses long stack frames.


; Macros should be included before any function code
    include inc/tos.s
    include inc/sidecart_macros.s

    org $FA3400         ; Start of the code. First 4KB bytes are reserved for the terminal.

; Send a synchronous command to the Sidecart setting the reentry flag for the next XBIOS calls
; inside our trapped XBIOS calls. Should be always paired with reentry_xbios_unlock
reentry_xbios_lock	macro
                    movem.l d0-d7/a0-a6,-(sp)            ; Save all registers
                    send_sync CMD_REENTRY_LOCK,0         ; Command code to lock the reentry
                    movem.l (sp)+,d0-d7/a0-a6            ; Restore all registers
                	endm

; Send a synchronous command to the Sidecart clearing the reentry flag for the next XBIOS calls
; inside our trapped XBIOS calls. Should be always paired with reentry_xbios_lock
reentry_xbios_unlock  macro
                    movem.l d0-d7/a0-a6,-(sp)            ; Save all registers
                    send_sync CMD_REENTRY_UNLOCK,0       ; Command code to unlock the reentry
                    movem.l (sp)+,d0-d7/a0-a6            ; Restore all registers
                	endm

rom_function:
    tst.l (RTCEMUL_SHARED_VARIABLES + (SVAR_ENABLED * 4))
    beq _exit_graciouslly ; If the RTC emulation is not enabled

; Get information about the hardware
	wait_sec
    bsr detect_hw
    bsr get_tos_version
_ntp_ready:
    send_sync CMD_READ_DATETME,0         ; Command code to read the date and time
    tst.w d0                            ; 0 if no error
    bne _exit_timemout                   ; The RP2040 is not responding, timeout now

_set_vectors:
    tst.l RTCEMUL_Y2K_PATCH
    beq.s _set_vectors_ignore

; We don't need to fix Y2K problem in EmuTOS
; Save the old XBIOS vector in RTCEMUL_OLD_XBIOS and set our own vector
    bsr save_vectors
    tst.w d0
    bne _exit_timemout

_set_vectors_ignore:
    pea RTCEMUL_DATETIME_BCD            ; Buffer should have a valid IKBD date and time format
    move.w #6, -(sp)                    ; Six bytes plus the header = 7 bytes
    move.w #25, -(sp)                   ; 
    trap #14
    addq.l #8, sp

    move.l RTCEMUL_DATETIME_MSDOS, d0
    bsr set_datetime
    tst.w d0
    bne _exit_timemout

	move.w #23,-(sp)                    ; gettime from XBIOS
	trap #14
	addq.l #2,sp

    tst.l RTCEMUL_Y2K_PATCH
    beq.s _ignore_y2k
    add.l #$3c000000,d0                 ; +30 years to guarantee the Y2K problem works in all TOS versions
_ignore_y2k:

    move.l d0, -(sp)                    ; Save the date and time in MSDOS format
    move.w #22,-(sp)                    ; settime with XBIOS
    trap #14
    addq.l #6, sp

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
    btst #0, RTCEMUL_REENTRY_TRAP      ; Check if the reentry is locked
    beq.s _custom_bios_trapped         ; If the bit is active, we are in a reentry call. We need to exec_old_handler the code

    move.l RTCEMUL_OLD_XBIOS, -(sp) ; if not, continue with XBIOS call
    rts 

_custom_bios_trapped:
    btst #5, (sp)                    ; Check if called from user mode
    beq.s _user_mode                 ; if so, do correct stack pointer
_not_user_mode:
    move.l sp,a0                     ; Move stack pointer to a0
    bra.s _check_cpu
_user_mode:
    move.l usp,a0                    ; if user mode, correct stack pointer
    subq.l #6,a0
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
    beq.s _setdatetime              ; if yes, go to our own routine

_continue_xbios:
    move.l RTCEMUL_OLD_XBIOS, -(sp) ; if not, continue with XBIOS call
    rts 

; Adjust the time when reading to compensate for the Y2K problem
; We should not tap this call for EmuTOS
_getdatetime:
    reentry_xbios_lock
	move.w #23,-(sp)
	trap #14
	addq.l #2,sp
	add.l #$3c000000,d0 ; +30 years for all TOS except EmuTOS
    reentry_xbios_unlock
	rte

; Adjust the time when setting to compensate for the Y2K problem
; We should not tap this call for TOS 2.06 and EmuTOS
_setdatetime:
	sub.l #$3c000000,8(a0)
    bra.s _continue_xbios

; Get the date and time from the RP2040 and set the IKBD information
; d0.l : Date and time in MSDOS format
set_datetime:
    move.l d0, d7

    swap d7

	move.w d7,-(sp)
	move.w #$2d,-(sp)                   ; settime with GEMDOS
	trap #1
	addq.l #4,sp
    tst.w d0
    bne.s _exit_set_time

	swap d7

	move.w d7,-(sp)
	move.w #$2b,-(sp)                   ; settime with GEMDOS  
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