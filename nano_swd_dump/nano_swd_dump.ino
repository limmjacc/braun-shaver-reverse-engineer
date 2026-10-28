// SWD bit-bang SRAM + flash dump for Arduino Nano (5 V) -> PSoC 4125 (3.3 V).
// D2 (SWDCLK) and D3 (SWDIO) go through 1k series / 2k to GND dividers. D4 (XRES) is direct.
//
// History (see docs/swd-read-methods-research.md for the full writeup):
//   - A loop-based bit-bang (DLY() with NOPs) never got past DPIDR: measured ~330 kHz-1 MHz,
//     under the datasheet's 1.5-14 MHz SWDCLK floor (Table 3-4 of the programming spec), so it
//     could never finish the acquire sequence before this chip's real post-reset acquire window
//     (empirically ~6.7-7.4 ms after XRES here, well past the generic ~400 us the datasheet
//     figure implies -- that bound depends on this firmware's actual boot time) closed.
//   - A fully unrolled (loop-free, branch-free where possible), macro-based bit-bang reliably
//     enters Test mode on every attempt once the search window was extended to cover the real
//     acquire timing. This is that version, extended to the full dump.
//
// Acquire sequence and register values: Infineon "CY8C4xxx, CYBLxxxx Programming specification",
// doc 002-22325 Rev. *I, 2024-07-26, section 4.3 (Step 1A). Local copy:
// docs/datasheets/psoc4-programming-spec.pdf.
//
// Output: printed as line-oriented text ("ADDR VALUE" in hex) between BEGIN/END markers, one
// section per region. scripts/parse_dump.py turns a captured log into real .bin and .hex files.
#include <stdint.h>

#define SWCLK_MASK _BV(PD2)
#define SWDIO_MASK _BV(PD3)
#define XRES_MASK  _BV(PD4)

#define CLK_HI()   do { PORTD |= SWCLK_MASK; } while (0)
#define CLK_LO()   do { PORTD &= (uint8_t)~SWCLK_MASK; } while (0)
#define DIO_OUT()  do { DDRD |= SWDIO_MASK; } while (0)
#define DIO_IN()   do { DDRD &= (uint8_t)~SWDIO_MASK; PORTD &= (uint8_t)~SWDIO_MASK; } while (0)
#define DIO_SET(b) do { if (b) { PORTD |= SWDIO_MASK; } else { PORTD &= (uint8_t)~SWDIO_MASK; } } while (0)
#define DIO_GET()  ((PIND & SWDIO_MASK) ? 1u : 0u)

#define ACK_OK      1u
#define ACK_WAIT    2u
#define ERR_PARITY  0xF0u
#define ERR_ARG     0xF1u
#define ERR_TIMEOUT 0xF2u

#define CSW_WORD_ONLY 0x00000002ul // word size, no auto-increment -- the exact value proven to work
                                   // during acquire() on this target. An earlier CSW_WORD_INC
                                   // (0x23000012, word + auto-increment + implementation HPROT
                                   // bits) ACKed on write but then faulted the very next AP
                                   // transfer (sticky error), so every word below gets its own
                                   // TAR write instead of relying on auto-increment.
#define ADDR_TEST_MODE    0x40030014ul
#define ADDR_CPUSS_SYSREQ 0x40100004ul
#define ADDR_CPUSS_SYSARG 0x40100008ul
#define SROM_SYSREQ_BIT     0x80000000ul
#define SROM_PRIVILEGED_BIT 0x10000000ul
#define SROM_STATUS_SUCCEEDED 0xA0000000ul
#define SROM_KEY1 0xB6ul
#define SROM_KEY2 0xD3ul
#define SROM_CMD_GET_SILICON_ID 0x00ul

// Region sizes per the datasheet ordering table (Table 39, 4125 family): 32 KB flash, 4 KB SRAM.
// SRAM base 0x20000000 is standard Cortex-M0+; flash base 0x00000000 likewise. Both unverified
// against the exact S425 suffix, which does not appear in the ordering table (see README.md).
#define SRAM_BASE  0x20000000ul
#define SRAM_WORDS 1024u            // 4 KB / 4
#define FLASH_BASE 0x00000000ul
#define FLASH_WORDS 8192u           // 32 KB / 4

// ---- Fully unrolled bit-bang primitives (see docs/swd-read-methods-research.md) ----
static void swd_send8(uint8_t v) {
  DIO_OUT();
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
  CLK_LO(); DIO_SET(v&1u); CLK_HI(); v>>=1;
}

static void swd_send32(uint32_t v) {
  DIO_OUT();
#define S() CLK_LO(); DIO_SET((uint8_t)(v&1u)); CLK_HI(); v>>=1;
  S() S() S() S() S() S() S() S()
  S() S() S() S() S() S() S() S()
  S() S() S() S() S() S() S() S()
  S() S() S() S() S() S() S() S()
#undef S
}

static uint32_t swd_recv3(void) {
  uint32_t v = 0;
  DIO_IN();
  CLK_LO(); if (DIO_GET()) { v |= 1u; } CLK_HI();
  CLK_LO(); if (DIO_GET()) { v |= 2u; } CLK_HI();
  CLK_LO(); if (DIO_GET()) { v |= 4u; } CLK_HI();
  return v;
}

static uint32_t swd_recv32(void) {
  uint32_t v = 0;
  DIO_IN();
#define R(i) CLK_LO(); if (DIO_GET()) { v |= (uint32_t)1u << i; } CLK_HI();
  R(0) R(1) R(2) R(3) R(4) R(5) R(6) R(7)
  R(8) R(9) R(10) R(11) R(12) R(13) R(14) R(15)
  R(16) R(17) R(18) R(19) R(20) R(21) R(22) R(23)
  R(24) R(25) R(26) R(27) R(28) R(29) R(30) R(31)
#undef R
  return v;
}

static uint32_t swd_recv1(void) {
  uint32_t v = 0;
  DIO_IN();
  CLK_LO(); if (DIO_GET()) { v |= 1u; } CLK_HI();
  return v;
}

static void swd_turn(void) { DIO_IN(); CLK_LO(); CLK_HI(); }

static void swd_ones32(void) {
  DIO_OUT();
#define O() CLK_LO(); DIO_SET(1); CLK_HI();
  O() O() O() O() O() O() O() O()
  O() O() O() O() O() O() O() O()
  O() O() O() O() O() O() O() O()
  O() O() O() O() O() O() O() O()
#undef O
}

static void swd_jtag2swd(void) {
  DIO_OUT();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
  CLK_LO(); DIO_SET(1); CLK_HI();
}

static void swd_zeros8(void) {
  DIO_OUT();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
  CLK_LO(); DIO_SET(0); CLK_HI();
}

static void fast_line_reset(void) {
  swd_ones32(); swd_ones32();
  swd_jtag2swd();                 // JTAG-to-SWD. Classic Cortex-M0/M0+ SW-DP, no dormant wake-up needed.
  swd_ones32(); swd_ones32();
  swd_zeros8();
}

static uint8_t parity32(uint32_t v) {
  v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
  return (uint8_t)(v & 1u);
}

// Single-shot transfer, no internal WAIT retry -- used only for the time-critical acquire race.
static uint8_t fast_xfer(uint8_t ap, uint8_t rd, uint8_t addr, uint32_t *data) {
  const uint8_t a2 = (addr >> 2) & 1u, a3 = (addr >> 3) & 1u;
  const uint8_t par = (uint8_t)(ap ^ rd ^ a2 ^ a3) & 1u;
  const uint8_t req = (uint8_t)(0x81u | (ap << 1) | (rd << 2) | (a2 << 3) | (a3 << 4) | (par << 5));
  swd_send8(req);
  swd_turn();
  const uint8_t ack = (uint8_t)swd_recv3();
  if (ack != 1u) { swd_turn(); swd_zeros8(); return ack; }
  if (rd) {
    const uint32_t v = swd_recv32();
    const uint8_t p = (uint8_t)swd_recv1();
    swd_turn(); swd_zeros8();
    if (p != parity32(v)) { return ERR_PARITY; }
    *data = v;
  } else {
    swd_turn();
    swd_send32(*data);
    swd_send8(parity32(*data));   // only the low bit matters; the rest run into idle cycles.
    swd_zeros8();
  }
  return ACK_OK;
}

// Same transfer, but retries on WAIT -- used for everything after the chip is halted in Test
// mode, where there is no more timing race to protect.
static uint8_t xfer_retry(uint8_t ap, uint8_t rd, uint8_t addr, uint32_t *data) {
  uint8_t ack = ACK_WAIT;
  for (uint8_t i = 0; (i < 32u) && (ack == ACK_WAIT); i++) { ack = fast_xfer(ap, rd, addr, data); }
  return ack;
}

static void print_hex32(uint32_t v) {
  for (int8_t s = 28; s >= 0; s -= 4) { Serial.print((uint8_t)((v >> s) & 0xFu), HEX); }
}

static void pulse_xres(void) { DDRD |= XRES_MASK; delay(5); DDRD &= (uint8_t)~XRES_MASK; }

// Programming spec Step 1A (section 4.3): race the post-reset acquire window to set TEST_MODE.
// Returns ACK_OK once TEST_MODE readback confirms bit 31 set.
static uint8_t acquire(void) {
  pulse_xres();
  const unsigned long t0 = micros();
  uint32_t id = 0;
  uint8_t ack = 0xFFu;

  while ((micros() - t0) < 20000ul) {        // see header: real window measured ~6.7-7.4 ms here
    fast_line_reset();
    ack = fast_xfer(0, 1, 0x0, &id);          // DPIDR
    if (ack == ACK_OK) { break; }
  }
  if (ack != ACK_OK) { return ERR_TIMEOUT; }
  if ((id != 0x0BC11477ul) && (id != 0x0BB11477ul)) { return ERR_ARG; }

  uint32_t v;
  v = 0x54000000ul; if (fast_xfer(0, 0, 0x4, &v) != ACK_OK) { return ERR_TIMEOUT; } // CTRL/STAT
  v = 0x00000000ul; if (fast_xfer(0, 0, 0x8, &v) != ACK_OK) { return ERR_TIMEOUT; } // SELECT
  v = 0x00000002ul; if (fast_xfer(1, 0, 0x0, &v) != ACK_OK) { return ERR_TIMEOUT; } // AP CSW
  v = ADDR_TEST_MODE; if (fast_xfer(1, 0, 0x4, &v) != ACK_OK) { return ERR_TIMEOUT; } // AP TAR
  v = 0x80000000ul;  if (fast_xfer(1, 0, 0xC, &v) != ACK_OK) { return ERR_TIMEOUT; } // AP DRW

  // Not time-critical from here: verify, then poll SROM_PRIVILEGED_BIT per spec.
  v = ADDR_TEST_MODE; if (xfer_retry(1, 0, 0x4, &v) != ACK_OK) { return ERR_TIMEOUT; }
  uint32_t check = 0;
  if (xfer_retry(1, 1, 0xC, &check) != ACK_OK) { return ERR_TIMEOUT; }   // posted, discard
  if (xfer_retry(0, 1, 0xC, &check) != ACK_OK) { return ERR_TIMEOUT; }   // RDBUFF, latched
  if ((check & 0x80000000ul) == 0ul) { return ERR_ARG; }

  for (uint16_t i = 0; i < 5000u; i++) {
    v = ADDR_CPUSS_SYSREQ;
    if (xfer_retry(1, 0, 0x4, &v) != ACK_OK) { return ERR_TIMEOUT; }
    if (xfer_retry(1, 1, 0xC, &check) != ACK_OK) { return ERR_TIMEOUT; }
    if (xfer_retry(0, 1, 0xC, &check) != ACK_OK) { return ERR_TIMEOUT; }
    if ((check & SROM_PRIVILEGED_BIT) == 0ul) { break; }
    if (i == 4999u) { return ERR_TIMEOUT; }
  }

  // TEST_MODE and CPUSS_SYSREQ are "always on" debug-domain registers and answered fine above
  // without this, but general memory (SRAM, flash) sits on the main system bus, which needs the
  // CDBGPWRUPACK/CSYSPWRUPACK power-up actually acknowledged, not just requested. The first real
  // dump attempt faulted on the very first SRAM access until this was added.
  for (uint16_t i = 0; i < 2000u; i++) {
    if (xfer_retry(0, 1, 0x4, &check) != ACK_OK) { return ERR_TIMEOUT; }    // CTRL/STAT
    if ((check & 0xA0000000ul) == 0xA0000000ul) { return ACK_OK; }
  }
  return ERR_TIMEOUT;
}

static uint8_t dump_mem(const __FlashStringHelper *label, uint32_t base, uint32_t words) {
  uint32_t v = 0x1Eul;
  uint8_t ack = xfer_retry(0, 0, 0x0, &v);              if (ack != ACK_OK) { return ack; }  // ABORT: clear sticky flags
  v = 0;             ack = xfer_retry(0, 0, 0x8, &v);   if (ack != ACK_OK) { return ack; }  // SELECT
  v = CSW_WORD_ONLY; ack = xfer_retry(1, 0, 0x0, &v);   if (ack != ACK_OK) { return ack; }  // AP CSW

  Serial.print(F("BEGIN_")); Serial.println(label);
  for (uint32_t i = 0; i < words; i++) {
    const uint32_t addr = base + 4ul * i;
    v = addr; ack = xfer_retry(1, 0, 0x4, &v); if (ack != ACK_OK) { Serial.print(F("ERR ")); Serial.println(ack); return ack; } // TAR
    ack = xfer_retry(1, 1, 0xC, &v);           if (ack != ACK_OK) { Serial.print(F("ERR ")); Serial.println(ack); return ack; } // DRW, posted (discard)
    ack = xfer_retry(0, 1, 0xC, &v);           if (ack != ACK_OK) { Serial.print(F("ERR ")); Serial.println(ack); return ack; } // RDBUFF, latched value
    print_hex32(addr); Serial.print(' '); print_hex32(v); Serial.println();
  }
  Serial.print(F("END_")); Serial.println(label);
  return ACK_OK;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  PORTD &= (uint8_t)~(SWCLK_MASK | SWDIO_MASK | XRES_MASK);
  DDRD  |= SWCLK_MASK;

  uint8_t ack = ERR_TIMEOUT;
  for (uint8_t i = 0; (i < 5u) && (ack != ACK_OK); i++) { ack = acquire(); }
  Serial.print(F("acquire ack=")); Serial.println(ack);
  if (ack != ACK_OK) { return; }

  // Diagnostic history: an earlier version of this sketch probed here (CTRL/STAT, several CSW
  // variants, a GET_SILICON_ID SROM call, SET_IMO_48MHz, a guessed CPUSS_PROTECTION address)
  // to localize why the dump below faults. Conclusion, with evidence, is in
  // docs/swd-read-methods-research.md: the chip's silicon ID reads fine via SROM, but every
  // direct AP memory access (SRAM, flash, and even an unrelated guessed peripheral address)
  // hard-faults -- the exact behaviour the programming spec's Appendix A describes for
  // PROTECTED chip-level protection. The full diagnostic sequence that established this is
  // preserved as nano_protection_probe/nano_protection_probe.ino. dump_mem() below is kept
  // because it is the correct, spec-compliant way to read memory and will work immediately if
  // the chip is ever moved to OPEN mode (which the spec says erases all flash first -- see the
  // docs for why that is a real tradeoff, not a formality).

  ack = dump_mem(F("SRAM"), SRAM_BASE, SRAM_WORDS);
  Serial.print(F("sram dump ack=")); Serial.println(ack);

  ack = dump_mem(F("FLASH"), FLASH_BASE, FLASH_WORDS);
  Serial.print(F("flash dump ack=")); Serial.println(ack);

  Serial.println(F("done"));
}

void loop(void) {}
