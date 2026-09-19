; SidecarTridge Multidevice GEMDOS pool fix for TOS 1.04 and 1.06
; (C) 2026 by GOODDATA LABS SL
; License: GPL v3

; GEMDOS 0.21 in TOS 1.04 and 1.06 compacts its internal memory pool (the "OS
; pool" of folder records, open files and memory descriptors) with a broken
; routine. Once enough folders are in use and enough descriptors have been
; freed, it corrupts the pool: drives stop answering and the desktop hangs.
; Atari's answer was POOLFIX3.PRG, which refuses to install once anything has
; hooked the GEMDOS trap, and the cartridge drivers hook it before the AUTO
; folder runs. This module does the same job, installed first:
;
;   - before every GEMDOS call, if the previous call freed memory descriptors
;     (Mfree, Mshrink or a Pterm), compact the pool with a correct routine, so
;     the pool never holds 4 free descriptor slots and GEMDOS's own compaction
;     never runs;
;   - then pass the call on to GEMDOS.
;
; Only installed on GEMDOS $1500 with TOS 1.04 or 1.06, and only when the
; GEMDOS entry still is the ROM's and carries the instructions the pool
; addresses are read from. Later TOS versions do not have the bug.

; Bootstrap the code in ASM
ROM4_START_ADDR:         equ $FA0000 ; ROM4 start address
ROM3_START_ADDR:         equ $FB0000 ; ROM3 start address

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
APP_POOLFIX             equ $0600                 ; MSB is the app code. The pool fix is $06
CMD_SET_LONG            equ ($0 + APP_POOLFIX)    ; d3 = address in this module, d4 = value to store there
CMD_COMPACTED           equ ($1 + APP_POOLFIX)    ; Pool compacted: d3 = &pf_flag (cleared by the RP), d4 = blocks released, d5 = free slots left
; Referenced by helpers in inc/sidecart_functions.s that this module never calls
CMD_SET_SHARED_VAR      equ ($2 + APP_POOLFIX)

_sysbase        equ $4f2    ; Pointer to the OS header
_dskbufp        equ $4c6    ; Address of the disk buffer pointer
_longframe      equ $59e    ; Not 0: long exception stack frames (not a 68000)
VEC_GEMDOS      equ 33      ; Trap #1 exception vector number

; Offsets from the ROM's GEMDOS entry point, TOS 1.04 and 1.06:
PF_PMD_OFFSET   equ -1796   ; "move.l #_pmd,(a7)" inside xmalloc()
PF_PMD_OPCODE   equ $2EBC
PF_OFD_OFFSET   equ -1102   ; "movea.l _ofdlist,a5" inside mgetmd()
PF_OFD_OPCODE   equ $2A79

; OS pool layout (GEMDOS 0.21):
; MDBLOCK: 0 o_link, 4 x_flag (> 0 descriptors, 0 free, < 0 folder/file record),
;          5 x_user, 6 four MDs
; MD:      0 m_link, 4 m_start, 8 m_length, 12 m_own (1 = free slot)
; MPB:     0 mp_mfl (free list), 4 mp_mal (allocated list), 8 mp_rover
MDB_FLAG        equ 4
MDB_MDS         equ 6
MD_SIZE         equ 16
MD_OWN          equ 12
MD_FREE         equ 1
MDS_PER_BLOCK   equ 4
MPB_MFL         equ 0
MPB_MAL         equ 4
MPB_ROVER       equ 8

; The pool fix works on its own stack. A GEMDOS call can arrive with the
; supervisor stack only a few hundred bytes above data that matters: TOS 1.04
; starts GEM with its stack inside GEM's own basepage, and GEMDRIVE nests its
; GEMDOS calls on that stack. Saving registers and talking to the RP there
; overwrote GEM's standard handles, and every program lost its console output
; (it went to MIDI). Interrupts also land on this stack while it is in use.
PF_STACK_SIZE   equ 1024

; Run \1 on the pool fix's stack, using 4 bytes of the caller's.
pf_own_stack    macro
    move.l a6, -(sp)
    move.l pf_stack, a6
    move.l sp, -(a6)                    ; the caller's stack pointer, on ours
    move.l a6, sp
    movem.l d0-d7/a0-a5, -(sp)
    bsr \1
    movem.l (sp)+, d0-d7/a0-a5
    move.l (sp), sp
    move.l (sp)+, a6
    endm

; Macros should be included before any function code
    include inc/tos.s
    include inc/sidecart_macros.s

    org $FA4C00

    bra.w poolfix_start                 ; main.s jumps here ($FA4C00)
pf_enabled:
    dc.l 0                              ; $FA4C04, set by the RP from the POOLFIX_ENABLED setting

poolfix_start:
    tst.l pf_enabled
    beq .pf_exit                        ; turned off in the setup menu
    gemdos Sversion, 2
    cmp.w #$1500, d0                    ; GEMDOS 0.21
    bne .pf_exit
    move.l _sysbase.w, a0
    move.w 2(a0), d0                    ; os_version
    cmp.w #$0104, d0
    beq.s .pf_tos_ok
    cmp.w #$0106, d0
    bne .pf_exit
.pf_tos_ok:
    move.l gemdos.vector.w, a1          ; GEMDOS entry: nothing has hooked it yet
    move.l a1, d0
    sub.l 8(a0), d0                     ; must lie inside the ROM (os_beg)
    cmp.l #$20000, d0
    bhi .pf_exit
    cmp.w #PF_PMD_OPCODE, PF_PMD_OFFSET(a1)
    bne .pf_exit
    cmp.w #PF_OFD_OPCODE, PF_OFD_OFFSET(a1)
    bne .pf_exit

    move.l a1, -(sp)
    move.l #PF_STACK_SIZE, -(sp)        ; the pool fix's stack, owned by the
    gemdos Malloc, 6                    ; initial process, which never ends
    move.l (sp)+, a1
    tst.l d0
    ble.s .pf_exit
    add.l #PF_STACK_SIZE, d0
    move.l #pf_stack, d3
    move.l d0, d4
    bsr pf_set_long
    bne.s .pf_exit
    move.l #pf_flag, d3                 ; a flag left set before an ST reset
    moveq #0, d4
    bsr pf_set_long
    bne.s .pf_exit
    move.l #pf_next, d3                 ; the RP stores the GEMDOS entry in pf_next
    move.l a1, d4
    bsr pf_set_long
    bne.s .pf_exit                      ; the RP did not answer: do not install

    move.l #pf_trap, -(sp)
    move.w #VEC_GEMDOS, -(sp)
    move.w #5, -(sp)                    ; Setexc()
    trap #13
    addq.l #8, sp
.pf_exit:
    rts

; Store d4 at the address d3 through the RP. Out: Z set when it answered.
pf_set_long:
    move.l a1, -(sp)
    send_sync CMD_SET_LONG, 8
    move.l (sp)+, a1
    tst.w d0
    rts

; These longs live in the cartridge window, which the ST cannot write: the RP
; stores them through CMD_SET_LONG and CMD_COMPACTED.
    ds.b ((4 - (* & 3)) & 3)            ; bump to the next 4-byte boundary
pf_flag:
    dc.l 0                              ; not 0: compact before the next GEMDOS call
pf_stack:
    dc.l 0                              ; top of the pool fix's stack
    dc.l 'XBRA'
    dc.l 'SDPF'
pf_next:
    dc.l 0                              ; the ROM's GEMDOS entry

pf_trap:
    tst.l pf_flag
    beq.s .pf_check_call
    pf_own_stack pf_compact_report

.pf_check_call:
    ; GEMDOS leaves d0-d2/a0-a2 undefined, so a0 and d0 are free here
    move.l usp, a0
    btst #5, (sp)                       ; called from supervisor mode?
    beq.s .pf_have_args
    lea 6(sp), a0
    tst.w _longframe.w
    beq.s .pf_have_args
    addq.l #2, a0
.pf_have_args:
    move.w (a0), d0                     ; GEMDOS function number
    beq.s .pf_frees                     ; Pterm0
    cmp.w #$31, d0                      ; Ptermres
    beq.s .pf_frees
    cmp.w #$49, d0                      ; Mfree
    beq.s .pf_frees
    cmp.w #$4A, d0                      ; Mshrink
    beq.s .pf_frees
    cmp.w #$4C, d0                      ; Pterm
    bne.s .pf_pass
.pf_frees:
    tst.l pf_flag
    bne.s .pf_pass
    pf_own_stack pf_raise_flag
.pf_pass:
    move.l pf_next, -(sp)
    rts

pf_raise_flag:
    move.l #pf_flag, d3
    moveq #1, d4
    send_sync CMD_SET_LONG, 8
    rts

pf_compact_report:
    bsr pf_compact
    move.l d4, d5
    move.l d3, d4
    move.l #pf_flag, d3
    send_sync CMD_COMPACTED, 12
    rts

; Compact the pool until fewer than MDS_PER_BLOCK descriptor slots are free.
; Out: d3 = descriptor blocks released, d4 = free slots left.
; Uses: a5 = the MPB, a6 = the first pool block.
pf_compact:
    move.l pf_next, a1
    move.l PF_PMD_OFFSET+2(a1), a5      ; a5 = &pmd
    move.l PF_OFD_OFFSET+2(a1), a1
    move.l (a1), a6                     ; a6 = ofdlist
    moveq #0, d3
    moveq #0, d7                        ; free slots in partly used descriptor blocks

    ; Give back the blocks whose four descriptors are all free
    move.l a6, a0
.pf_count_loop:
    move.l a0, d0
    beq.s .pf_count_done
    tst.b MDB_FLAG(a0)
    ble.s .pf_count_next
    bsr pf_free_slots
    cmp.w #MDS_PER_BLOCK, d0
    bne.s .pf_count_partial
    clr.b MDB_FLAG(a0)
    addq.w #1, d3
    bra.s .pf_count_next
.pf_count_partial:
    add.w d0, d7
.pf_count_next:
    move.l (a0), a0
    bra.s .pf_count_loop
.pf_count_done:

    ; While at least MDS_PER_BLOCK slots are free, empty the descriptor block
    ; with the most free slots into free slots of the other blocks.
.pf_empty_loop:
    cmp.w #MDS_PER_BLOCK, d7
    blt.s .pf_done
    move.l a6, a0
    suba.l a2, a2                       ; a2 = block to empty
    moveq #0, d6                        ; its free slots
.pf_pick_loop:
    move.l a0, d0
    beq.s .pf_pick_done
    tst.b MDB_FLAG(a0)
    ble.s .pf_pick_next
    bsr pf_free_slots
    cmp.w d6, d0
    ble.s .pf_pick_next
    move.w d0, d6
    move.l a0, a2
.pf_pick_next:
    move.l (a0), a0
    bra.s .pf_pick_loop
.pf_pick_done:
    move.l a2, d0
    beq.s .pf_done
    lea MDB_MDS(a2), a3
    moveq #MDS_PER_BLOCK-1, d5
.pf_move_loop:
    cmp.l #MD_FREE, MD_OWN(a3)
    beq.s .pf_move_next
    bsr pf_find_free_slot               ; a0 = free slot in another block
    move.l a0, d0
    beq.s .pf_done
    bsr pf_move_md
    tst.w d0
    bne.s .pf_done                      ; descriptor not on a list: leave the pool alone
.pf_move_next:
    lea MD_SIZE(a3), a3
    dbf d5, .pf_move_loop
    clr.b MDB_FLAG(a2)                  ; the block is empty: give it back
    addq.w #1, d3
    subq.w #MDS_PER_BLOCK, d7           ; its free slots and the ones just filled
    bra.s .pf_empty_loop
.pf_done:
    move.l d7, d4
    ext.l d3
    ext.l d4
    rts

; In: a0 = descriptor block. Out: d0.w = its free slots. Uses a1, d1.
pf_free_slots:
    moveq #0, d0
    lea MDB_MDS(a0), a1
    moveq #MDS_PER_BLOCK-1, d1
.pf_free_slots_loop:
    cmp.l #MD_FREE, MD_OWN(a1)
    bne.s .pf_free_slots_next
    addq.w #1, d0
.pf_free_slots_next:
    lea MD_SIZE(a1), a1
    dbf d1, .pf_free_slots_loop
    rts

; In: a2 = block being emptied, a6 = first pool block.
; Out: a0 = a free slot in another descriptor block, or 0. Uses a1, d1.
pf_find_free_slot:
    move.l a6, a1
.pf_find_block:
    move.l a1, d1
    beq.s .pf_find_none
    cmp.l a1, a2
    beq.s .pf_find_next_block
    tst.b MDB_FLAG(a1)
    ble.s .pf_find_next_block
    lea MDB_MDS(a1), a0
    moveq #MDS_PER_BLOCK-1, d1
.pf_find_slot:
    cmp.l #MD_FREE, MD_OWN(a0)
    beq.s .pf_find_found
    lea MD_SIZE(a0), a0
    dbf d1, .pf_find_slot
.pf_find_next_block:
    move.l (a1), a1
    bra.s .pf_find_block
.pf_find_none:
    suba.l a0, a0
.pf_find_found:
    rts

; Move the descriptor a3 into the free slot a0, relinking it on the free or
; allocated list and in the rover, then mark a3's slot free.
; In: a5 = the MPB. Out: d0.w = 0 moved, 1 not found on a list. Uses a1, d1.
pf_move_md:
    lea MPB_MFL(a5), a1
    bsr.s .pf_move_find
    beq.s .pf_move_found
    lea MPB_MAL(a5), a1
    bsr.s .pf_move_find
    beq.s .pf_move_found
    moveq #1, d0
    rts
.pf_move_found:                         ; a1 = link that points at a3
    move.l (a3), (a0)
    move.l 4(a3), 4(a0)
    move.l 8(a3), 8(a0)
    move.l MD_OWN(a3), MD_OWN(a0)
    move.l a0, (a1)
    cmp.l MPB_ROVER(a5), a3
    bne.s .pf_move_rover_ok
    move.l a0, MPB_ROVER(a5)
.pf_move_rover_ok:
    move.l #MD_FREE, MD_OWN(a3)
    moveq #0, d0
    rts

; In: a1 = list head (its link is at offset 0, like m_link), a3 = descriptor.
; Out: Z set and a1 = the link pointing at a3, or Z clear if not found.
.pf_move_find:
    move.l (a1), d1
    beq.s .pf_move_find_none
    cmp.l d1, a3
    beq.s .pf_move_find_hit
    move.l d1, a1
    bra.s .pf_move_find
.pf_move_find_none:
    moveq #1, d1                        ; clears Z
    rts
.pf_move_find_hit:
    moveq #0, d1                        ; sets Z
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
        nop
        nop
poolfix_end:
