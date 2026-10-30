; PRU0 firmware: drive 4 independently-settable output pins (P9_31/29/30/28,
; i.e. pr1_pru0_pru_r30 bits 0-3) from a Linux-writable shared-memory word.
;
; Loop body is LBBO + AND + MOV(r30) + SBBO + QBA. The MOV into r30 is the
; single PRU instruction that actually changes the pin levels and is the
; only timing-critical step: at the PRU's 200 MHz clock (5 ns/instruction),
; that instruction is single-cycle, so pin state changes are exactly 5 ns
; deterministic once the desired value is in a register. The LBBO/SBBO
; shared-RAM accesses add a few extra cycles per full poll iteration
; (tens of ns total), which only matters for how fast Linux-driven changes
; are picked up -- it does not add jitter to the r30 write itself.
;
; Shared RAM layout (PRUSS shared RAM, local PRU address 0x00010000,
; physical address 0x4a310000, mapped from Linux via /dev/mem):
;   offset 0: desired pin state, bits 0-3 (written by Linux)
;   offset 4: applied pin state, bits 0-3 (written by this firmware, for
;             Linux-side readback -- see pru4pin.py)

    .text
    .global START

START:
    ; Clear SYSCFG STANDBY_INIT so the PRU's OCP master port is enabled.
    ; (Not required for shared-RAM access, which is always local, but kept
    ; as standard boilerplate in case this firmware is extended to also
    ; touch broader SoC peripherals via the OCP master port.)
    LBCO  &r0, c4, 4, 4
    CLR   r0, r0, 4
    SBCO  &r0, c4, 4, 4

    LDI32 r1, 0x00010000       ; r1 = base of PRUSS shared RAM

POLL:
    LBBO  &r2, r1, 0, 4        ; r2 = desired state word from offset 0
    AND   r2, r2, 0x0F         ; keep only the 4 bits we use
    MOV   r30, r2               ; <-- single-cycle: drives the 4 pins
    SBBO  &r2, r1, 4, 4        ; mirror applied state to offset 4
    QBA   POLL
