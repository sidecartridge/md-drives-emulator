; SidecarTridge Multi-device shared functions

; constants
_p_cookies                              equ $5a0    ; pointer to the system Cookie-Jar
COOKIE_JAR_MEGASTE                      equ $00010010 ; Mega STE computer
SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE   equ 16      ; Size of the shared variables for the shared functions
SHARED_VARIABLE_HARDWARE_TYPE           equ 0       ; Hardware type of the Atari ST computer
SHARED_VARIABLE_SVERSION                equ 1       ; TOS version from Sversion

COMMAND_SYNC_CODE_SIZE                  equ (4 + _end_sync_code_in_stack - _start_sync_code_in_stack)

; Detect the hardware of the computer we are running on
; This code checks for the cookie-jar and reads the _MCH cookie to determine the hardware
; If the cookie-jar is not present, we assume it's an old machine before 1.06
; It writes the value in the shared variable SHARED_VARIABLE_HARDWARE_TYPE
;
; Inputs:
;   None
;
; Outputs:
;   d0.l contains the hardware type as stored in the shared variable SHARED_VARIABLE_HARDWARE_TYPE
detect_hw:
	move.l _p_cookies.w,d0      ; Check the cookie-jar to know what type of machine we are running on
	beq _old_hardware           ; No cookie-jar, so it's a TOS <= 1.04
	movea.l d0,a0               ; Get the address of the cookie-jar
_loop_cookie:
	move.l (a0)+,d0             ; The cookie jar value is zero, so old hardware again
	beq _old_hardware
	cmp.l #'_MCH',d0            ; Is it the _MCH cookie?
	beq.s _found_cookie         ; Yes, so we found the machine type
	addq.w #4,a0                ; No, so skip the cookie name
	bra.s _loop_cookie          ; And try the next cookie
_found_cookie:
	move.l	(a0)+,d4            ; Get the cookie value
	bra.s	_save_hw
_old_hardware:
    clr.l d4                    ; 0x0000	0x0000	Atari ST (260 ST,520 ST,1040 ST,Mega ST,...)
_save_hw:
    move.l d4, -(sp)            ; Save the hardware type    
    move.l #SHARED_VARIABLE_HARDWARE_TYPE, d3   ; D3 Variable index
                                                ; D4 Variable value
    send_sync CMD_SET_SHARED_VAR, 8
    move.l (sp)+, d0            ; Restore the hardware type in d0.l as result
    rts

; Get the TOS version
; This code reads the TOS version from the ROM and writes it in the shared variable SHARED_VARIABLE_SVERSION
;
; Inputs:
;   None
; Outputs:
;   None
get_tos_version:
    gemdos Sversion, 2
    and.l #$FFFF,d0
    cmp.w #$FC, $4.w            ; Check if the TOS version is a 192Kb or 256Kb
    bne.s .get_tos_version_is_256k
.get_tos_version_is_192k:
    move.w $FC0002, d1          ; Read the TOS version from the ROM
    bra.s .exit_get_tos_version
.get_tos_version_is_256k:
    move.w $E00002, d1          ; Read the TOS version from the ROM

.exit_get_tos_version:
    and.l #$FFFF,d1             ; Mask the upper word
    swap d1
    or.l d1, d0                 ; Set the TOS version in the upper word of d0
    move.l #SHARED_VARIABLE_SVERSION, d3    ; Variable index
    move.l d0, d4                           ; Variable value
    send_sync CMD_SET_SHARED_VAR, 8
    rts

; Send an sync command to the Sidecart
; Wait until the command sets a response in the memory with a random number used as a token
; Input registers:
; d0.w: command code
; d1.w: payload size
; From d3 to d6 the payload based on the size of the payload field d1.w
; Output registers:
; d0: error code, 0 if no error
; d1-d7 are modified. a0-a3 modified.
send_sync_command_to_sidecart:
    move.l (sp)+, a0                 ; Return address
    move.l #COMMAND_SYNC_CODE_SIZE, d7
    lea -(COMMAND_SYNC_CODE_SIZE)(sp), sp
    move.l sp, a2
    move.l sp, a3
    lea _start_sync_code_in_stack, a1    ; a1 points to the start of the code in ROM
    lsr.w #1, d7
    subq #1, d7
_copy_sync_code:
    move.w (a1)+, (a2)+
    dbf d7, _copy_sync_code

    move.l a0, a2                       ; Return address to a2

    ; The sync command synchronize with a random token
    move.l RANDOM_TOKEN_SEED_ADDR,d2
    addq.w #4, d1                       ; Add 4 bytes to the payload size to include the token

    ; a1 points to the addres of the token modified by the rp2040
    lea RANDOM_TOKEN_ADDR, a1

    ; For performance reasons, we will positive and negative index values to avoid some operations
    move.l #ROMCMD_START_ADDR, a0 ; Start address of the ROM3
    add.l #$8000, a0              ; Add 32Kb to the address to point to the middle of the ROM

    ; SEND HEADER WITH MAGIC NUMBER
    move.w #CMD_MAGIC_NUMBER, d7 ; Command header
    tst.b (a0, d7.w)             ; Command header

    ; Clean the CHECKSUM register in d7
    clr.l d7

    ; SEND COMMAND CODE
    add.w d0, d7                ; Add the payload size to the checksum
    tst.b (a0, d0.w)            ; Command code. d0 is a scratch register

    ; SEND PAYLOAD SIZE
    add.w d1, d7                ; Add the payload size to the checksum
    tst.b (a0, d1.w)
    tst.w d1
    beq _no_more_payload_stack  ; If the command does not have payload, we are done.

    ; SEND PAYLOAD LOW D2
    add.w d2, d7                ; Add the payload to the checksum
    tst.b (a0, d2.w)
    cmp.w #2, d1
    beq _no_more_payload_stack

    ; SEND PAYLOAD HIGH D2
    swap d2
    add.w d2, d7              ; Add the payload to the checksum
    tst.b (a0, d2.w)
    cmp.w #4, d1
    beq _no_more_payload_stack

    ; SEND PAYLOAD LOW D3
    move.w d3, d0
    add.w d3, d7              ; Add the payload to the checksum
    tst.b (a0, d3.w)
    cmp.w #6, d1
    beq _no_more_payload_stack

    ; SEND PAYLOAD HIGH D3
    swap d3
    add.w d3, d7              ; Add the payload to the checksum
    tst.b (a0, d3.w)
    cmp.w #8, d1
    beq _no_more_payload_stack

    ; SEND PAYLOAD LOW D4
    add.w d4, d7              ; Add the payload to the checksum
    tst.b (a0, d4.w)
    cmp.w #10, d1
    beq _no_more_payload_stack

    ; SEND PAYLOAD HIGH D4
    swap d4
    add.w d4, d7              ; Add the payload to the checksum
    tst.b (a0, d4.w)
    cmp.w #12, d1
    beq.s _no_more_payload_stack

    ; SEND PAYLOAD LOW D5
    add.w d5, d7              ; Add the payload to the checksum
    tst.b (a0, d5.w)
    cmp.w #14, d1
    beq.s _no_more_payload_stack

    ; SEND PAYLOAD HIGH D5
    swap d5
    add.w d5, d7              ; Add the payload to the checksum
    tst.b (a0, d5.w)
    cmp.w #16, d1
    beq.s _no_more_payload_stack

    ; SEND PAYLOAD LOW D6
    add.w d6, d7              ; Add the payload to the checksum
    tst.b (a0, d6.w)
    cmp.w #18, d1
    beq.s _no_more_payload_stack

    ; SEND PAYLOAD HIGH D6
    swap d6
    add.w d6, d7              ; Add the payload to the checksum
    tst.b (a0, d6.w)

_no_more_payload_stack:
    ; SEND CHECKSUM
    nop
    nop
    tst.b (a0, d7.w)

    ; End of the command loop. Now we need to wait for the token
    swap d2                   ; D2 is the only register that is not used as a scratch register
    move.l #$000FFFFF, d7     ; Most significant word is the inner loop, least significant word is the outer loop
    moveq #0, d0              ; No Timeout

    jmp (a3)                  ; Jump to the code in the stack
; This is the code that cannot run in ROM while waiting for the command to complete
_start_sync_code_in_stack:
    cmp.l (a1), d2                           ; Compare the random number with the token
    beq.s _sync_token_found                  ; Token found, we can finish succesfully
    subq.l #1, d7                            ; Decrement the inner loop
    bne.s _start_sync_code_in_stack          ; If the inner loop is not finished, continue

    ; Sync token not found, timeout
    subq.l #1, d0                            ; Timeout
_sync_token_found:

    move.l #RANDOM_TOKEN_POST_WAIT, d7
_wait_me:
    subq.l #1, d7                            ; Decrement the outer loop
    bne.s _wait_me                           ; Wait for the timeout

_no_wait_me:
    lea (COMMAND_SYNC_CODE_SIZE)(sp), sp
    jmp (a2)                                 ; Return to the code in the ROM
    nop
_end_sync_code_in_stack:

; Send an sync write command to the Sidecart
; Wait until the command sets a response in the memory with a random number used as a token
; Input registers:
; d0.w: command code
; d3.l: long word to send to the sidecart
; d4.l: long word to send to the sidecart
; d5.l: long word to send to the sidecart
; d6.w: number of bytes to write to the sidecart starting in a4 address
; a4: address of the buffer to write in the sidecart
; Output registers:
; d0: error code, 0 if no error
; d7: 16 bit checksum of the data written from address from a4 to a4 + d6
; a4: next address in the computer memory to retrieve
; d1-d6 are modified. a0-a3 modified.
send_sync_write_command_to_sidecart:
    move.l (sp)+, a0                 ; Return address
    move.l #_end_sync_write_code_in_stack - _start_sync_write_code_in_stack, d7
    lea -(_end_sync_write_code_in_stack - _start_sync_write_code_in_stack)(sp), sp
    move.l sp, a2

.send_sync_write_command_to_sidecart_continue:
    move.l a2, a3
    lea _start_sync_write_code_in_stack, a1    ; a1 points to the start of the code in ROM
    lsr.w #2, d7
    subq #5, d7                        ; unroll + 1
    move.l (a1)+, (a2)+                ; Unroll a little...
    move.l (a1)+, (a2)+
    move.l (a1)+, (a2)+
    move.l (a1)+, (a2)+
_copy_write_sync_code:
    move.l (a1)+, (a2)+
    dbf d7, _copy_write_sync_code

    move.l a0, a2                       ; Return address to a2

    ; The sync write command synchronize with a random token
    move.l RANDOM_TOKEN_SEED_ADDR,d2
    moveq #18, d1                       ; We are going to send the data in d2.l (random token), d3.l, d4.l, d5.l  and CHCK.w at the end. ALWAYS!!!!
    add.w d6, d1                        ; Add the number of bytes to write to the sidecart
    addq.w #1, d1                       ; Add one byte to the payload before rounding to the next word
    lsr.w #1, d1                        ; Round to the next word
    lsl.w #1, d1                        ; Multiply by 2 because we are sending two bytes each iteration

_start_async_write_code_in_stack:   
    move.l #ROMCMD_START_ADDR, a0 ; We have to keep in A0 the address of the ROM3 because we need to read from it

    ; SEND HEADER WITH MAGIC NUMBER
    swap d0                     ; Save the command code in the high word of d0         
    move.b CMD_MAGIC_NUMBER, d0; Command header. d0 is a scratch register

    ; SEND COMMAND CODE
    swap d0                     ; Recover the command code
    move.l a0, a1               ; Address of the ROM3
    add.w d0, a1                ; We can use add because the command code msb is 0 and there is no sign extension            
    move.b (a1), d0             ; Command code. d0 is a scratch register

    ; SEND PAYLOAD SIZE
    move.l a0, d0               ; Address of the ROM3 in d0    
    move.w d1, d0               ; OR high and low words in d0
    move.l d0, a1               ; move to a1 ready to read from this address
    move.b (a1), d1             ; Command payload size. d1 is a scratch register

    ; SEND PAYLOAD
    move.w d2, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload low d2

    swap d2
    move.w d2, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload high d2

    move.w d3, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload low d3

    swap d3
    move.w d3, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload high d3

    move.w d4, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload low d4

    swap d4
    move.w d4, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload high d4

    move.w d5, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload low d5

    swap d5
    move.w d5, d0
    move.l d0, a1
    move.b (a1), d1           ; Command payload high d5

    ;
    ; SEND MEMORY BUFFER TO WRITE
    ;
    lsr.w #1, d6              ; Copy two bytes each iteration
    subq.w #1, d6             ; one less

    clr.l d7                  ; Use D7 as the CHECKSUM store registry

    ; Test if the address in A4 is even or odd
    move.l a4, d0
    btst #0, d0
    beq.s _write_to_sidecart_even_loop
_write_to_sidecart_odd_loop:
    move.l a0, d0
_write_to_sidecart_odd_loop2:
    move.b  (a4)+, d3       ; Load the high byte
    lsl.w   #8, d3          ; Shift it to the high part of the word
    move.b  (a4)+, d3       ; Load the low byte
    move.w d3, d0
    move.l d0, a1
    move.b (a1), d1           ; Write the memory to the sidecart

    add.w d3, d7             ; Add the word to the checksum

    dbf d6, _write_to_sidecart_odd_loop2
    bra.s _write_to_sidecart_end_loop

 _write_to_sidecart_even_loop:
    move.l a0, d0
    cmp.l #4, d6
    blt _write_to_sidecart_even_loop2

    move.l d6, d1                        ; Use D1 as loop counter for the unrolled amount
    lsr.l #2, d1                         ; Divide the number of words by 4
    and.l #$3, d6                        ; remaining amount of words in d6
    subq.l #1, d1                        ; We need to copy one byte less because dbf counts 0

 _write_to_sidecart_even_loop_unroll_by4:        ; 4x unrolled loop
    move.w (a4)+, d0          ; Load the word
    add.w d0, d7             ; Add the word to the checksum
    move.l d0, a1
    move.b (a1), d0           ; Write the memory to the sidecart

    move.w (a4)+, d0          ; Load the word
    add.w d0, d7             ; Add the word to the checksum
    move.l d0, a1
    move.b (a1), d0           ; Write the memory to the sidecart

    move.w (a4)+, d0          ; Load the word
    add.w d0, d7             ; Add the word to the checksum
    move.l d0, a1
    move.b (a1), d0           ; Write the memory to the sidecart

    move.w (a4)+, d0          ; Load the word
    add.w d0, d7             ; Add the word to the checksum    
    move.l d0, a1
    move.b (a1), d0           ; Write the memory to the sidecart
    dbf d1,_write_to_sidecart_even_loop_unroll_by4
    
 _write_to_sidecart_even_loop2:
    move.w (a4)+, d0          ; Load the word
    add.w d0, d7              ; Add the word to the checksum
    move.l d0, a1
    move.b (a1), d1           ; Write the memory to the sidecart
    dbf d6, _write_to_sidecart_even_loop2

_write_to_sidecart_end_loop:
    ; We have to send the checksum in the payload
    move.w d7, d0
    move.l d0, a1
    move.b (a1), d0           ; Command payload low d7
    
    ; End of the command loop. Now we need to wait for the token
    swap d2                   ; D2 is the only register that is not used as a scratch register
    move.l #$000FFFFF, d6     ; Most significant word is the inner loop, least significant word is the outer loop
    moveq #0, d0              ; Timeout
    jmp (a3)                  ; Jump to the code in the stack

; This is the code that cannot run in ROM while waiting for the command to complete
_start_sync_write_code_in_stack:
    cmp.l RANDOM_TOKEN_ADDR, d2                    ; Compare the random number with the token
    beq.s _sync_write_token_found                  ; Token found, we can finish succesfully
    subq.l #1, d6                                  ; Decrement the inner loop
    bne.s _start_sync_write_code_in_stack          ; If the inner loop is not finished, continue

    ; Sync token not found, timeout
    subq.l #1, d0                                  ; Timeout

_sync_write_token_found:

    move.l #RANDOM_TOKEN_POST_WAIT, d6
_wait_me_write:
    subq.l #1, d6                            ; Decrement the outer loop
    bne.s _wait_me_write                     ; Wait for the timeout


    lea (_end_sync_write_code_in_stack - _start_sync_write_code_in_stack)(sp), sp
    jmp (a2)                                 ; Return to the code in the ROM

    even    ; Do not remove this line
    nop     ; Do not remove this line
    nop     ; Do not remove this line
_end_sync_write_code_in_stack: