; SidecarTridge Multidevice ACSI Hard Disk Emulator
; (C) 2026 by GOODDATA LABS SL
; License: GPL v3

; Placeholder for future ACSI block-device emulation.

ROM4_START_ADDR         equ $FA0000 ; ROM4 start address
ROM3_START_ADDR         equ $FB0000 ; ROM3 start address

ACSIEMUL_GAP_SIZE       equ $2200

; Reservation carved below _membot by the bit-26 cart hook in main.s.
; Holds the BCB pool (8 BCBs × 4096 B on stock TOS 2.06/1.62) plus a
; 2 KB safety margin. Must match the same constant in main.s.
ACSIEMUL_BCB_POOL_BYTES    equ (32*1024+2*1024)

ROM_EXCHG_BUFFER_ADDR   equ (ROM4_START_ADDR + $8200)
RANDOM_TOKEN_ADDR       equ (ROM_EXCHG_BUFFER_ADDR)
RANDOM_TOKEN_SEED_ADDR  equ (RANDOM_TOKEN_ADDR + 4)
COMMAND_TIMEOUT         equ $0006FFFF
COMMAND_WRITE_TIMEOUT   equ $0006FFFF

ROMCMD_START_ADDR       equ (ROM3_START_ADDR)
CMD_MAGIC_NUMBER        equ ($ABCD)
CMD_RETRIES_COUNT       equ 5

APP_ACSIEMUL            equ $0500
CMD_SET_SHARED_VAR      equ ($0 + APP_ACSIEMUL)
CMD_READ_SECTOR         equ ($1 + APP_ACSIEMUL)
CMD_READ_SECTOR_BATCH   equ ($2 + APP_ACSIEMUL)
CMD_WRITE_SECTOR        equ ($3 + APP_ACSIEMUL)
CMD_WRITE_SECTOR_BATCH  equ ($4 + APP_ACSIEMUL)
CMD_DEBUG               equ ($C + APP_ACSIEMUL)

ACSI_WRITE_CHUNK_SIZE   equ 1024

; Toggle batched sector reads: 1 = single RP round-trip for all sectors,
; 0 = one round-trip per logical sector (original path).
USE_BATCH_READ          equ 1

; Toggle batched sector writes: 1 = pack up to BATCH_MAX_BYTES worth of
; sectors into a single RP flush (one f_sync for the whole batch),
; 0 = original single-sector write path (one f_sync per logical sector).
USE_BATCH_WRITE         equ 1

; Conservative byte limit for a single batch read/write — must be ≤ the RP's
; ACSIEMUL_IMAGE_BUFFER_SIZE (22624). Rounded down for alignment.
ACSIEMUL_BATCH_MAX_BYTES equ $5800

DEBUG_HDV_INIT           equ 0
DEBUG_HDV_BPB            equ 1
DEBUG_HDV_BOOT           equ 3
DEBUG_HDV_BPB_MATCH      equ 6
DEBUG_DRVBITS            equ 9
DEBUG_FIRST_VOLUME_DRIVE equ 10
DEBUG_LAST_VOLUME_DRIVE  equ 11
DEBUG_DRVBITS_MASK       equ 12
DEBUG_HDV_MEDIACH_STATUS equ 13
DEBUG_HDV_RW_OLD_HANDLER equ 14
DEBUG_HDV_BPB_DATA       equ 15
DEBUG_MEM_MAP_A          equ $13
DEBUG_MEM_MAP_B          equ $14
DEBUG_REBIND_DECISION    equ $15

REBIND_PLACED               equ 0
REBIND_SKIP_NO_PUN          equ 2
REBIND_SKIP_MAX_SECTOR      equ 3
REBIND_SKIP_NO_BCBS         equ 4
REBIND_SKIP_NEED_EXCEEDS    equ 5
REBIND_SKIP_MEMTOP_ZERO     equ 6

_membot                 equ $432
_v_bas_ad               equ $44E

ACSIEMUL_SHARED_VARIABLES       equ (RANDOM_TOKEN_SEED_ADDR + 4)
ACSIEMUL_SHARED_VARIABLE_SIZE   equ (ACSIEMUL_GAP_SIZE / 4)
ACSIEMUL_VARIABLES_OFFSET       equ (ROM_EXCHG_BUFFER_ADDR + ACSIEMUL_GAP_SIZE + 64)
ACSIEMUL_PUN_INFO               equ (ACSIEMUL_VARIABLES_OFFSET)
ACSIEMUL_BPB_PTR_TABLE          equ (ACSIEMUL_PUN_INFO + 160)
ACSIEMUL_BPB_PTR_TABLE_SIZE     equ 64
ACSIEMUL_BPB_DATA_TOTAL_SIZE    equ 640
ACSIEMUL_IMAGE_BUFFER           equ (ACSIEMUL_BPB_PTR_TABLE + ACSIEMUL_BPB_PTR_TABLE_SIZE + ACSIEMUL_BPB_DATA_TOTAL_SIZE)

SVAR_ENABLED            equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 0)
SVAR_FIRST_UNIT         equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 1)
SVAR_HOOKS_INSTALLED    equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 2)
SVAR_OLD_HDV_INIT       equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 3)
SVAR_OLD_HDV_BPB        equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 4)
SVAR_OLD_HDV_RW         equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 5)
SVAR_OLD_HDV_BOOT       equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 6)
SVAR_OLD_HDV_MEDIACH    equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 7)
SVAR_FIRST_VOLUME_DRIVE equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 8)
SVAR_LAST_VOLUME_DRIVE  equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 9)
SVAR_PUN_INFO_PTR       equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 10)
SVAR_RW_STATUS          equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 11)
SVAR_MEDIA_CHANGED_MASK equ (ACSIEMUL_SHARED_VARIABLE_SIZE + 12)

MED_NOCHANGE            equ 0
MED_UNKNOWN             equ 1
MED_CHANGED             equ 2
BCON_DEVICE_CONSOLE     equ 2

_hdv_init               equ $46A                            ; Address of the HDV_INIT structure
_hdv_bpb                equ $472                            ; Address of the HDV_BPB structure
_hdv_rw                 equ $476                            ; Address of the HDV_RW structure
_hdv_boot               equ $47a                            ; Address of the HDV_BOOT structure
_hdv_mediach            equ $47e                            ; Address of the HDV_MEDIACH structure
_bootdev                equ $446                            ; BIOS boot device number
_dskbufp                equ $4c6                            ; Address of the disk buffer pointer

PUN_INFO_P_MAX_SECTOR   equ 94

BCB_LINK                equ 0
BCB_BUFDRV              equ 4
BCB_BUFTYP              equ 6
BCB_BUFREC              equ 8
BCB_DIRTY               equ 10
BCB_DM                  equ 12
BCB_BUFR                equ 16

    include inc/tos.s
    include inc/sidecart_macros.s

; Default _DEBUG to 0 if the build didn't pass -D_DEBUG=… (the Makefile
; always does, but this keeps the file assemblable stand-alone and lets
; `ifne _DEBUG` uniformly mean "debug mode on").
    ifnd _DEBUG
_DEBUG equ 0
    endif

chain_saved_hdv macro
    tst.l (ACSIEMUL_SHARED_VARIABLES + (\1 * 4))
    beq.s .\@chain_saved_hdv_none
    move.l (ACSIEMUL_SHARED_VARIABLES + (\1 * 4)), -(sp)
    rts
.\@chain_saved_hdv_none:
    clr.l d0
    rts
    endm

set_shared_var_long macro
    move.l #\1, d3
    move.l \2, d4
    send_sync CMD_SET_SHARED_VAR, 8
    endm

trace_hook_none macro
    movem.l d0-d7/a0-a6, -(sp)
    moveq #\1, d3
    moveq #-1, d4
    send_sync CMD_DEBUG, 8
    movem.l (sp)+, d0-d7/a0-a6
    endm

trace_hook_word macro
    movem.l d0-d7/a0-a6, -(sp)
    moveq #\1, d3
    clr.l d4
    move.w \2, d4
    send_sync CMD_DEBUG, 8
    movem.l (sp)+, d0-d7/a0-a6
    endm

trace_hook_long macro
    movem.l d0-d7/a0-a6, -(sp)
    moveq #\1, d3
    move.l \2, d4
    send_sync CMD_DEBUG, 8
    movem.l (sp)+, d0-d7/a0-a6
    endm

trace_hdv_mediach_status macro
    movem.l d0-d7/a0-a6, -(sp)
    moveq #DEBUG_HDV_MEDIACH_STATUS, d3
    clr.l d4
    move.w \1, d4
    swap d4
    move.w \2, d4
    send_sync CMD_DEBUG, 8
    movem.l (sp)+, d0-d7/a0-a6
    endm

trace_mem_map macro
    movem.l d0-d7/a0-a6, -(sp)
    moveq #DEBUG_MEM_MAP_A, d3
    move.l phystop.w, d4
    move.l _membot.w, d5
    send_sync CMD_DEBUG, 12
    movem.l (sp)+, d0-d7/a0-a6
    movem.l d0-d7/a0-a6, -(sp)
    moveq #DEBUG_MEM_MAP_B, d3
    move.l memtop.w, d4
    move.l _v_bas_ad.w, d5
    send_sync CMD_DEBUG, 12
    movem.l (sp)+, d0-d7/a0-a6
    endm

; \1=decision immediate/reg, \2=need value, \3=reserve value
trace_rebind_decision macro
    movem.l d0-d7/a0-a6, -(sp)
    moveq #DEBUG_REBIND_DECISION, d3
    move.l \1, d4
    move.l \2, d5
    move.l \3, d6
    send_sync CMD_DEBUG, 16
    movem.l (sp)+, d0-d7/a0-a6
    endm

    org $FA5400

acsi_start:
    cmp.l #$FFFFFFFF, (ACSIEMUL_SHARED_VARIABLES + (SVAR_ENABLED * 4))
    bne acsi_exit_graciously

    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_PUN_INFO_PTR * 4)), d0
    beq.s .acsi_start_skip_pun_ptr
    move.l d0, pun_ptr.w
.acsi_start_skip_pun_ptr:
    bsr acsi_rebind_bcb_buffers
    bsr install_hdv_hooks

acsi_exit_graciously:
    rts

install_hdv_hooks:
    move.l _hdv_init.w, d0
    cmp.l #acsi_hdv_init, d0
    bne.s .check_saved_vectors
    move.l _hdv_bpb.w, d0
    cmp.l #acsi_hdv_bpb, d0
    bne.s .check_saved_vectors
    move.l _hdv_rw.w, d0
    cmp.l #acsi_hdv_rw, d0
    bne.s .check_saved_vectors
    move.l _hdv_boot.w, d0
    cmp.l #acsi_hdv_boot, d0
    bne.s .check_saved_vectors
    move.l _hdv_mediach.w, d0
    cmp.l #acsi_hdv_mediach, d0
    bne.s .check_saved_vectors
    set_shared_var_long SVAR_HOOKS_INSTALLED, #1
    rts

.check_saved_vectors:
    tst.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_HOOKS_INSTALLED * 4))
    bne .patch_vectors

    set_shared_var_long SVAR_OLD_HDV_INIT, _hdv_init.w
    set_shared_var_long SVAR_OLD_HDV_BPB, _hdv_bpb.w
    set_shared_var_long SVAR_OLD_HDV_RW, _hdv_rw.w
    set_shared_var_long SVAR_OLD_HDV_BOOT, _hdv_boot.w
    set_shared_var_long SVAR_OLD_HDV_MEDIACH, _hdv_mediach.w

.patch_vectors:
    move.l #acsi_hdv_init, _hdv_init.w
    move.l #acsi_hdv_bpb, _hdv_bpb.w
    move.l #acsi_hdv_rw, _hdv_rw.w
    move.l #acsi_hdv_boot, _hdv_boot.w
    move.l #acsi_hdv_mediach, _hdv_mediach.w

    set_shared_var_long SVAR_HOOKS_INSTALLED, #1
    rts

acsi_hdv_init:
    trace_hook_none DEBUG_HDV_INIT
    move.l a0, -(sp)
    move.l d6, -(sp)
    clr.l d6
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_PUN_INFO_PTR * 4)), d0
    beq.s .acsi_hdv_init_chain_old
    move.l d0, pun_ptr.w
    bsr update_drvbits_from_shared_range
.acsi_hdv_init_chain_old:
    tst.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_INIT * 4))
    beq.s .acsi_hdv_init_old_done
    movea.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_INIT * 4)), a0
    jsr (a0)
    move.l d0, d6
.acsi_hdv_init_old_done:
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_PUN_INFO_PTR * 4)), d0
    beq.s .acsi_hdv_init_return_old
    move.l d0, pun_ptr.w
    bsr update_drvbits_from_shared_range
    trace_hook_long DEBUG_DRVBITS, drvbits.w
    move.l d6, d0
    move.l (sp)+, d6
    move.l (sp)+, a0
    rts
.acsi_hdv_init_return_old:
    trace_hook_long DEBUG_DRVBITS, drvbits.w
    move.l d6, d0
    move.l (sp)+, d6
    move.l (sp)+, a0
    rts

update_drvbits_from_shared_range:
    movem.l d1-d4, -(sp)
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_FIRST_VOLUME_DRIVE * 4)), d1
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_LAST_VOLUME_DRIVE * 4)), d2
    trace_hook_long DEBUG_FIRST_VOLUME_DRIVE, d1
    trace_hook_long DEBUG_LAST_VOLUME_DRIVE, d2
    cmp.l #2, d1
    blt.s .update_drvbits_invalid
    cmp.l d1, d2
    blt.s .update_drvbits_invalid
    clr.l d3
.update_drvbits_loop:
    bset d1, d3
    cmp.l d1, d2
    beq.s .update_drvbits_apply
    addq.l #1, d1
    bra.s .update_drvbits_loop
.update_drvbits_apply:
    move.l d3, d4
    trace_hook_long DEBUG_DRVBITS_MASK, d4
    or.l d3, drvbits.w
    bra.s .update_drvbits_done
.update_drvbits_invalid:
    clr.l d3
    move.l d3, d4
    trace_hook_long DEBUG_DRVBITS_MASK, d4
.update_drvbits_done:
    movem.l (sp)+, d1-d4
    rts

acsi_rebind_bcb_buffers:
    movem.l d0-d7/a0-a6, -(sp)
    trace_mem_map
    movea.l pun_ptr.w, a0
    move.l a0, d0
    bne .acsi_rebind_have_pun
    trace_rebind_decision #REBIND_SKIP_NO_PUN, #0, #ACSIEMUL_BCB_POOL_BYTES
    bra .acsi_rebind_bcb_buffers_done
.acsi_rebind_have_pun:
    clr.l d7
    move.w PUN_INFO_P_MAX_SECTOR(a0), d7
    cmp.w #512, d7
    bhi .acsi_rebind_sector_ok
    trace_rebind_decision #REBIND_SKIP_MAX_SECTOR, d7, #ACSIEMUL_BCB_POOL_BYTES
    bra .acsi_rebind_bcb_buffers_done
.acsi_rebind_sector_ok:
    clr.l d6
    movea.l bufl.w, a5
    bsr acsi_count_bcb_list
    movea.l (bufl+4).w, a5
    bsr acsi_count_bcb_list
    tst.l d6
    bne .acsi_rebind_have_bcbs
    trace_rebind_decision #REBIND_SKIP_NO_BCBS, #0, #ACSIEMUL_BCB_POOL_BYTES
    bra .acsi_rebind_bcb_buffers_done
.acsi_rebind_have_bcbs:
    move.l d6, d0
    mulu.w d7, d0
    move.l d0, d1                   ; d1 = need bytes, preserved across trace
    cmp.l #ACSIEMUL_BCB_POOL_BYTES, d1
    bls .acsi_rebind_fits
    trace_rebind_decision #REBIND_SKIP_NEED_EXCEEDS, d1, #ACSIEMUL_BCB_POOL_BYTES
    bra .acsi_rebind_bcb_buffers_done
.acsi_rebind_fits:
    ; The BCB pool is the region [_membot-POOL, _membot) carved out by the
    ; bit-26 cart hook in main.s (raised _membot before GEMDOS built its
    ; free pool, so the range is genuinely out of TPA).
    move.l _membot.w, d0
    tst.l d0
    beq .acsi_rebind_base_bad
    cmp.l #ACSIEMUL_BCB_POOL_BYTES, d0
    bls .acsi_rebind_base_bad
    sub.l #ACSIEMUL_BCB_POOL_BYTES, d0
    bra .acsi_rebind_base_ok
.acsi_rebind_base_bad:
    trace_rebind_decision #REBIND_SKIP_MEMTOP_ZERO, d1, #ACSIEMUL_BCB_POOL_BYTES
    bra .acsi_rebind_bcb_buffers_done
.acsi_rebind_base_ok:
    movea.l d0, a4
    movea.l bufl.w, a5
    bsr acsi_assign_bcb_list_buffers
    movea.l (bufl+4).w, a5
    bsr acsi_assign_bcb_list_buffers
    trace_rebind_decision #REBIND_PLACED, d1, #ACSIEMUL_BCB_POOL_BYTES

.acsi_rebind_bcb_buffers_done:
    movem.l (sp)+, d0-d7/a0-a6
    rts

acsi_count_bcb_list:
.acsi_count_bcb_list_loop:
    move.l a5, d0
    beq.s .acsi_count_bcb_list_done
    addq.l #1, d6
    movea.l BCB_LINK(a5), a5
    bra.s .acsi_count_bcb_list_loop
.acsi_count_bcb_list_done:
    rts

acsi_assign_bcb_list_buffers:
.acsi_assign_bcb_list_buffers_loop:
    move.l a5, d0
    beq.s .acsi_assign_bcb_list_buffers_done
    move.l a4, BCB_BUFR(a5)
    move.w #-1, BCB_BUFDRV(a5)
    clr.w BCB_BUFREC(a5)
    clr.w BCB_DIRTY(a5)
    clr.l BCB_DM(a5)
    adda.l d7, a4
    movea.l BCB_LINK(a5), a5
    bra.s .acsi_assign_bcb_list_buffers_loop
.acsi_assign_bcb_list_buffers_done:
    rts

acsi_hdv_bpb:
    ifne _DEBUG
    trace_hook_word DEBUG_HDV_BPB, 64(sp)
    endif
    clr.l d0
    move.w 4(sp), d0
    cmp.l #15, d0
    bhi.s .acsi_hdv_bpb_chain_old
    add.w d0, d0
    add.w d0, d0
    move.l a0, -(sp)
    movea.l #ACSIEMUL_BPB_PTR_TABLE, a0
    move.l 0(a0, d0.w), d0
    move.l (sp)+, a0
    bne.s .acsi_hdv_bpb_owned
.acsi_hdv_bpb_chain_old:
    tst.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_BPB * 4))
    beq.s .acsi_hdv_bpb_none
    movea.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_BPB * 4)), a0
    jmp (a0)
.acsi_hdv_bpb_none:
    clr.l d0
.acsi_hdv_bpb_return:
    rts
.acsi_hdv_bpb_owned:
    ifne _DEBUG
    trace_hook_word DEBUG_HDV_BPB_MATCH, 64(sp)
    move.w 4(sp), d7
    bsr.s acsi_trace_hdv_bpb_data
    endif
    bra.s .acsi_hdv_bpb_return

    ifne _DEBUG
; Dump the Atari-visible BPB struct to the RP log in four CMD_DEBUG slices.
; Present only in debug builds — release builds drop the function entirely
; (not referenced once the caller above is also gated out).
acsi_trace_hdv_bpb_data:
    movem.l d0-d7/a0-a6, -(sp)
    movea.l d0, a6
    clr.l d0
    move.w d7, d0
    movea.l d0, a5

    moveq #DEBUG_HDV_BPB_DATA, d3
    clr.l d4
    move.l a5, d4
    swap d4
    clr.l d5
    move.l a6, d5
    clr.l d6
    move.w (a6), d6
    swap d6
    move.w 2(a6), d6
    send_sync CMD_DEBUG, 16

    moveq #DEBUG_HDV_BPB_DATA, d3
    clr.l d4
    move.l a5, d4
    swap d4
    move.w #1, d4
    clr.l d5
    move.w 4(a6), d5
    swap d5
    move.w 6(a6), d5
    clr.l d6
    move.w 8(a6), d6
    swap d6
    move.w 10(a6), d6
    send_sync CMD_DEBUG, 16

    moveq #DEBUG_HDV_BPB_DATA, d3
    clr.l d4
    move.l a5, d4
    swap d4
    move.w #2, d4
    clr.l d5
    move.w 12(a6), d5
    swap d5
    move.w 14(a6), d5
    clr.l d6
    move.w 16(a6), d6
    swap d6
    move.w 18(a6), d6
    send_sync CMD_DEBUG, 16

    moveq #DEBUG_HDV_BPB_DATA, d3
    clr.l d4
    move.l a5, d4
    swap d4
    move.w #3, d4
    clr.l d5
    move.w 20(a6), d5
    swap d5
    move.w 22(a6), d5
    clr.l d6
    move.w 24(a6), d6
    swap d6
    move.w 32(a6), d6
    send_sync CMD_DEBUG, 16

    movem.l (sp)+, d0-d7/a0-a6
    rts
    endif

acsi_hdv_rw:
    move.w 14(sp), d0                        ; d0.w = logical drive number (0=A, 1=B, 2=C...).
    cmp.w #15, d0                            ; The BPB ownership table only covers A:..P:.
    bhi .acsi_hdv_rw_chain_old               ; Out-of-range drives must fall through to the old handler.
    lsl.w #2, d0                             ; Convert drive number into a longword table offset.
    movea.l #ACSIEMUL_BPB_PTR_TABLE, a0      ; a0 = table of ST-visible BPB pointers built by RP.
    move.l 0(a0, d0.w), d0                   ; Non-zero entry means this logical drive belongs to ACSI.
    beq .acsi_hdv_rw_chain_old               ; Zero BPB pointer means "not our drive": use previous hdv_rw.
    movea.l d0, a1                           ; a1 = BPB pointer for this logical drive.
    tst.w 10(sp)                             ; Sector count requested by TOS.
    beq.s .acsi_hdv_rw_success               ; A zero-sector request is a no-op and therefore succeeds.
    tst.l 6(sp)                              ; The buffer pointer must be valid.
    beq.s .acsi_hdv_rw_read_fault            ; Null buffer: fail.
    movem.l d1-d7/a0-a4, -(sp)              ; Preserve caller registers (a5/a6 untouched by our code + send_sync).
    move.w 58(sp), d1                        ; d1 = sector count after the register save frame.
    move.w (a1), d2                          ; d2 = logical sector size announced in the BPB.
    bne.s .acsi_hdv_rw_have_recsize
    move.w #512, d2
.acsi_hdv_rw_have_recsize:
    movea.l 54(sp), a4                       ; a4 = buffer in ST RAM.
    moveq #0, d6                             ; d6 = starting logical sector within the emulated partition.
    move.w 60(sp), d6
    move.w 62(sp), d4                        ; d4 = logical drive number used by the RP-side mapper.
    btst #0, 53(sp)                          ; rwflag bit 0: 1=write, 0=read (low byte of word at 4+48=52(sp)).
    bne.s .acsi_hdv_rw_write
    ; Read path
    clr.w d5
    bsr.s acsi_do_transfer_sidecart
    bra.s .acsi_hdv_rw_done
.acsi_hdv_rw_write:
    ifne USE_BATCH_WRITE
    bsr acsi_write_sectors_batch
    else
    bsr acsi_write_sectors_to_sidecart
    endif
.acsi_hdv_rw_done:
    movem.l (sp)+, d1-d7/a0-a4              ; Restore the caller context and keep d0 as the BIOS status.
    rts                                      ; Return success or BIOS error code from the transfer helper.
.acsi_hdv_rw_read_fault:
    moveq #EREADF, d0
    rts
.acsi_hdv_rw_success:
    clr.l d0
    rts
.acsi_hdv_rw_chain_old:
    tst.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_RW * 4))
    beq.s .acsi_hdv_rw_none
    movea.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_RW * 4)), a0
    jmp (a0)
.acsi_hdv_rw_none:
    clr.l d0
    rts

acsi_do_transfer_sidecart:
    tst.w d5
    bne.s .acsi_do_transfer_write
    bsr.s acsi_validate_sector_size
    bne.s .acsi_do_transfer_exit             ; CCR set by validate's clr.l/moveq exit
    ifne USE_BATCH_READ
    bsr acsi_read_sectors_batch
    else
    bsr.s acsi_read_sectors_from_sidecart
    endif
.acsi_do_transfer_exit:
    rts
.acsi_do_transfer_write:
    clr.l d0
    rts

acsi_validate_sector_size:
    cmp.w #4096, d2
    beq.s .acsi_validate_sector_size_ok
    cmp.w #8192, d2
    beq.s .acsi_validate_sector_size_ok
    cmp.w #512, d2
    beq.s .acsi_validate_sector_size_ok
    cmp.w #1024, d2
    beq.s .acsi_validate_sector_size_ok
    cmp.w #2048, d2
    beq.s .acsi_validate_sector_size_ok
    moveq #EREADF, d0
    rts
.acsi_validate_sector_size_ok:
    clr.l d0
    rts

acsi_read_sectors_from_sidecart:
    subq.w #1, d1
.acsi_read_sectors_loop:
    bsr.s acsi_read_sector_from_sidecart
    bne.s .acsi_read_sectors_done            ; CCR set by sector read's clr.l/error exit
    addq.l #1, d6
    dbf d1, .acsi_read_sectors_loop
.acsi_read_sectors_done:
    rts

acsi_read_sector_from_sidecart:
    movem.l d1/d2/d4/d6, -(sp)
    move.w d6, d3
    swap d3
    move.w d4, d3
    moveq.l #4, d1
    move.w #CMD_READ_SECTOR, d0
    bsr send_sync_command_to_sidecart
    movem.l (sp)+, d1/d2/d4/d6          ; CCR preserved — Z reflects send_sync result
    bne.s .acsi_read_sector_transport_error_trace
.acsi_read_sector_transport_ok:
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_RW_STATUS * 4)), d0
    tst.l d0
    bne.s .acsi_read_sector_exit
    move.w d2, d5
    move.l #ACSIEMUL_IMAGE_BUFFER, a1
    move.l a4, d3
    btst #0, d3
    bne.s .acsi_copy_sector_odd
    cmp.w #32, d5
    blo.s .acsi_copy_sector_odd
    lsr.w #5, d5                             ; d5 = sector_size / 32
    subq.w #1, d5
.acsi_copy_sector_even_32:
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    dbf d5, .acsi_copy_sector_even_32
    clr.l d0
    rts
.acsi_copy_sector_odd:
    lsr.w #4, d5                             ; d5 = sector_size / 16
    subq.w #1, d5
.acsi_copy_sector_odd_loop:
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    dbf d5, .acsi_copy_sector_odd_loop
    clr.l d0
    rts
.acsi_read_sector_transport_error_trace:
.acsi_read_sector_transport_error:
.acsi_read_sector_exit:
    rts

; Batched sector read: single RP round-trip for all logical sectors in the
; Rwabs request. Eliminates (count-1) command handshakes vs the per-sector
; path. The RP reads count*physicalSectorCount physical sectors into
; IMAGE_BUFFER, byte-swaps the whole block, and signals once.
; Entry: d1.w = count, d2.w = recsize, d4.w = drive, d6.l = starting recno,
;        a4 = destination buffer.
; Exit:  d0 = 0 on success, negative BIOS error on failure. a4 advanced.
acsi_read_sectors_batch:
    ; Compute max sectors per batch: floor(BATCH_MAX_BYTES / recsize).
    ; Done once before the loop; d7 holds it throughout.
    move.l #ACSIEMUL_BATCH_MAX_BYTES, d7
    divu.w d2, d7                            ; d7.w = max sectors per batch

.acsi_batch_loop:
    ; batch_count = min(remaining, max_count)
    move.w d1, d5
    cmp.w d7, d5
    bls.s .acsi_batch_no_clamp
    move.w d7, d5
.acsi_batch_no_clamp:
    ; d5.w = batch_count for this iteration. Send to RP.
    movem.l d1/d2/d4/d5/d6/d7, -(sp)
    move.w d6, d3                            ; d3 = recno
    swap d3
    move.w d4, d3                            ; d3 = recno:drive
    clr.l d4
    move.w d5, d4                            ; d4 = batch_count
    moveq.l #8, d1                           ; payload = 8 bytes (d3 + d4)
    move.w #CMD_READ_SECTOR_BATCH, d0
    bsr send_sync_command_to_sidecart
    movem.l (sp)+, d1/d2/d4/d5/d6/d7        ; CCR preserved
    bne.s .acsi_batch_error
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_RW_STATUS * 4)), d0
    tst.l d0
    bne.s .acsi_batch_exit
    ; Copy batch_count * recsize bytes from IMAGE_BUFFER to dest.
    ; d0.w = batch_count (saved before copy overwrites d5).
    move.w d5, d0
    mulu.w d2, d5                            ; d5 = total bytes for this batch
    move.l #ACSIEMUL_IMAGE_BUFFER, a1
    move.l a4, d3
    btst #0, d3
    bne.s .acsi_batch_copy_odd
    cmp.w #32, d5
    blo.s .acsi_batch_copy_odd
    lsr.w #5, d5
    subq.w #1, d5
.acsi_batch_copy_even_32:
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    move.l (a1)+, (a4)+
    dbf d5, .acsi_batch_copy_even_32
    bra.s .acsi_batch_advance
.acsi_batch_copy_odd:
    lsr.w #4, d5
    subq.w #1, d5
.acsi_batch_copy_odd_loop:
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    move.b (a1)+, (a4)+
    dbf d5, .acsi_batch_copy_odd_loop
.acsi_batch_advance:
    add.w d0, d6                             ; recno += batch_count
    sub.w d0, d1                             ; remaining -= batch_count
    bgt .acsi_batch_loop                     ; more sectors to read
    clr.l d0
    rts
.acsi_batch_error:
.acsi_batch_exit:
    rts

; Write logical sectors to the RP. Sends sector data in ACSI_WRITE_CHUNK_SIZE
; chunks via send_sync_write_command_to_sidecart. Each chunk carries:
;   d3 = recno:drive (packed)
;   d4 = byte offset inside the sector (0, chunk, 2*chunk, ...)
;   d6 = chunk size in bytes, plus d6 bytes streamed from a4.
; The RP writes each chunk directly at IMAGE_BUFFER[d4] and flushes to SD
; when d4 + chunkSize reaches recsize. Because d4 is declared by the caller,
; retries land at the same offset and are inherently idempotent.
; Entry: d1.w = count, d2.w = recsize, d4.w = drive, d6.l = starting recno,
;        a4 = source buffer in Atari RAM.
; Exit:  d0 = 0 on success, negative BIOS error on failure.
acsi_write_sectors_to_sidecart:
    subq.w #1, d1
.acsi_write_sectors_loop:
    bsr.s acsi_write_sector_to_sidecart
    tst.w d0
    bne.s .acsi_write_sectors_done
    addq.l #1, d6
    dbf d1, .acsi_write_sectors_loop
.acsi_write_sectors_done:
    rts

; Write one logical sector in chunks. Each chunk self-addresses via d4 so the
; RP placement is independent of previous chunks; timeout-triggered retries
; overwrite the same bytes at the same offset.
; Entry: d2.l = recsize, d3 pre-packed = recno:drive (set below from d4/d6),
;        a4 = source buffer.
; Exit:  d0 = 0 on success, negative on error. a4 advanced by recsize.
acsi_write_sector_to_sidecart:
    ; Pack d3 = recno:drive for all chunks of this sector
    move.w d6, d3
    swap d3
    move.w d4, d3                            ; d3 = recno:drive (constant for all chunks)
    moveq #0, d4                             ; d4 = byte offset inside sector
.acsi_write_chunk_loop:
    ; Zero-extend recsize into d5 so garbage in the caller's d2.high never
    ; leaks into the long arithmetic (TOS's hdv_rw convention does not
    ; guarantee a clean d2.high; trusting move.l d2,... corrupts the chunk
    ; size and walks a4 into unrelated RAM).
    moveq #0, d5
    move.w d2, d5                            ; d5 = recsize (zero-extended)
    sub.l d4, d5                             ; d5 = remaining = recsize - offset
    cmp.l #ACSI_WRITE_CHUNK_SIZE, d5
    ble.s .acsi_write_chunk_custom
    move.l #ACSI_WRITE_CHUNK_SIZE, d5
.acsi_write_chunk_custom:
    ; d5 = bytes this chunk. d4 = offset in sector (sent to RP).
    move.w #CMD_RETRIES_COUNT, d7
.acsi_write_chunk_retry:
    movem.l d1-d7/a4, -(sp)
    move.w #CMD_WRITE_SECTOR, d0
    move.l d5, d6                            ; d6 = bytes to stream from a4
    bsr send_sync_write_command_to_sidecart
    movem.l (sp)+, d1-d7/a4
    tst.w d0
    beq.s .acsi_write_chunk_ok
    dbf d7, .acsi_write_chunk_retry
    moveq #EWRITF, d0
    rts
.acsi_write_chunk_ok:
    add.l d5, a4                             ; advance source pointer
    add.l d5, d4                             ; offset += chunk
    cmp.w d2, d4                             ; offset < recsize? (word compare: recsize ≤ 8192 fits)
    blt.s .acsi_write_chunk_loop
.acsi_write_sector_done:
    ; All chunks sent. Check RP status.
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_RW_STATUS * 4)), d0
    tst.l d0
    rts

; Batched write path. Splits a multi-sector Rwabs request into back-to-back
; batches of up to ACSIEMUL_BATCH_MAX_BYTES / recsize sectors. Each batch
; is sent as a stream of CMD_WRITE_SECTOR_BATCH chunks; the RP buffers all
; sectors of the batch in IMAGE_BUFFER and commits them with a single
; f_write + f_sync. Compared to the single-sector path this collapses N
; directory-entry syncs per Rwabs into ⌈totalBytes / BATCH_MAX_BYTES⌉.
; Entry: d1.w = count, d2.w = recsize, d4.w = drive, d6.l = starting recno,
;        a4 = source buffer in Atari RAM.
; Exit:  d0 = 0 on success, negative BIOS error on failure.
acsi_write_sectors_batch:
    move.l #ACSIEMUL_BATCH_MAX_BYTES, d7
    divu.w d2, d7                            ; d7.w = max sectors per batch
.acsi_write_batch_outer:
    move.w d1, d5                            ; d5.low = remaining count
    cmp.w d7, d5
    bls.s .acsi_write_batch_no_clamp
    move.w d7, d5                            ; clamp to max per batch
.acsi_write_batch_no_clamp:
    ; d5.w = this batch's sector count. Save outer state (NOT a4 — inner
    ; advances it across the batch like the single-sector path).
    movem.l d1/d2/d4/d5/d6/d7, -(sp)
    bsr acsi_write_one_batch
    movem.l (sp)+, d1/d2/d4/d5/d6/d7
    tst.w d0
    bne.s .acsi_write_batch_done
    add.w d5, d6                             ; startRecno += batch count
    sub.w d5, d1                             ; remaining -= batch count
    bgt.s .acsi_write_batch_outer
    clr.l d0
.acsi_write_batch_done:
    rts

; Send one batch: d5.w sectors starting at recno d6.l, recsize d2.w,
; drive d4.w, source a4. Streams the entire batch in ACSI_WRITE_CHUNK_SIZE
; chunks; each chunk carries d3 = startRecno:drive, d4 = byte offset inside
; the batch, d5 = totalBytes (= count × recsize). The RP flushes when the
; last chunk arrives.
; Exit: d0 = 0 on success, negative on error. a4 advanced by totalBytes.
acsi_write_one_batch:
    ; Compute totalBytes via mulu.w — produces a clean 32-bit product in
    ; d5, wiping the garbage high word left over from the outer's move.w.
    mulu.w d2, d5                            ; d5 = totalBytes
    move.w d6, d3
    swap d3
    move.w d4, d3                            ; d3 = startRecno:drive
    moveq #0, d4                             ; d4 = offset inside batch
.acsi_write_batch_chunk_loop:
    ; d6 = chunk size = min(totalBytes - offset, CHUNK_SIZE).
    move.l d5, d6
    sub.l d4, d6                             ; d6 = remaining in batch
    cmp.l #ACSI_WRITE_CHUNK_SIZE, d6
    ble.s .acsi_write_batch_chunk_custom
    move.l #ACSI_WRITE_CHUNK_SIZE, d6
.acsi_write_batch_chunk_custom:
    move.w #CMD_RETRIES_COUNT, d7
.acsi_write_batch_chunk_retry:
    movem.l d1-d7/a4, -(sp)
    move.w #CMD_WRITE_SECTOR_BATCH, d0
    bsr send_sync_write_command_to_sidecart
    movem.l (sp)+, d1-d7/a4
    tst.w d0
    beq.s .acsi_write_batch_chunk_ok
    dbf d7, .acsi_write_batch_chunk_retry
    moveq #EWRITF, d0
    rts
.acsi_write_batch_chunk_ok:
    add.l d6, a4                             ; advance source pointer
    add.l d6, d4                             ; offset += chunk
    cmp.l d5, d4                             ; offset < totalBytes?
    blt.s .acsi_write_batch_chunk_loop
.acsi_write_batch_flush_done:
    ; All chunks sent. Check RP status (set by the RP after its flush).
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_RW_STATUS * 4)), d0
    tst.l d0
    rts

acsi_hdv_boot:
    trace_hook_word DEBUG_HDV_BOOT, _bootdev.w
    chain_saved_hdv SVAR_OLD_HDV_BOOT

acsi_hdv_mediach:
    clr.l d0
    move.w 4(sp), d0
    cmp.l #15, d0
    bhi .acsi_hdv_mediach_chain_old
    move.l d0, d2
    add.w d0, d0
    add.w d0, d0
    move.l a0, -(sp)
    movea.l #ACSIEMUL_BPB_PTR_TABLE, a0
    move.l 0(a0, d0.w), d1
    move.l (sp)+, a0
    beq .acsi_hdv_mediach_chain_old
    move.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_MEDIA_CHANGED_MASK * 4)), d1
    btst d2, d1
    beq.s .acsi_hdv_mediach_nochange
    bclr d2, d1
    movem.l d1-d7/a0-a6, -(sp)
    move.l #SVAR_MEDIA_CHANGED_MASK, d3
    move.l d1, d4
    send_sync CMD_SET_SHARED_VAR, 8
    movem.l (sp)+, d1-d7/a0-a6
    trace_hdv_mediach_status d2, #MED_CHANGED
    moveq #MED_CHANGED, d0
    rts
.acsi_hdv_mediach_nochange:
    moveq #MED_NOCHANGE, d0
    rts
.acsi_hdv_mediach_chain_old:
    tst.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_MEDIACH * 4))
    beq.s .acsi_hdv_mediach_none
    movea.l (ACSIEMUL_SHARED_VARIABLES + (SVAR_OLD_HDV_MEDIACH * 4)), a0
    jmp (a0)
.acsi_hdv_mediach_none:
    clr.l d0
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
acsi_end:
