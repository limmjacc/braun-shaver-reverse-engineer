// SWD bit-bang dump for Arduino Nano (5 V) to PSoC 4125 (3.3 V).
// D2 (SWDCLK) and D3 (SWDIO) go through 1k series / 2k to GND dividers. D4 (XRES) is direct. See README.md.
#include <stdint.h>

#define SWCLK_MASK  _BV(PD2)   // D2 -> chip pin 13 (SWDCLK)
#define SWDIO_MASK  _BV(PD3)   // D3 -> chip pin 12 (SWDIO)
#define XRES_MASK   _BV(PD4)   // D4 -> chip pin 30 (XRES)

#define DLY() __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t")

#define ACK_OK      1u
#define ACK_WAIT    2u
#define ERR_PARITY  0xF0u
#define ERR_ARG     0xF1u
#define ERR_TIMEOUT 0xF2u

#define CSW_WORD_INC 0x23000012ul  // verify against ADIv5 spec

static inline void clk_hi(void)  { PORTD |= SWCLK_MASK; }
static inline void clk_lo(void)  { PORTD &= (uint8_t)~SWCLK_MASK; }
static inline void dio_out(void) { DDRD |= SWDIO_MASK; }
static inline void dio_in(void)  { DDRD &= (uint8_t)~SWDIO_MASK; PORTD &= (uint8_t)~SWDIO_MASK; }
static inline void dio_set(uint8_t b) {
  if (b) { PORTD |= SWDIO_MASK; } else { PORTD &= (uint8_t)~SWDIO_MASK; }
}
static inline uint8_t dio_get(void) { return (PIND & SWDIO_MASK) ? 1u : 0u; }

static void swd_write(uint32_t v, uint8_t n) {
  dio_out();
  while (n--) {
    clk_lo(); dio_set((uint8_t)(v & 1u)); DLY();
    clk_hi(); DLY();
    v >>= 1;
  }
}

static uint32_t swd_read(uint8_t n) {
  uint32_t v = 0;
  dio_in();
  for (uint8_t i = 0; i < n; i++) {
    clk_lo(); DLY();
    if (dio_get()) { v |= (uint32_t)1u << i; }
    clk_hi(); DLY();
  }
  return v;
}

static void swd_turn(void) { dio_in(); clk_lo(); DLY(); clk_hi(); DLY(); }

static uint8_t parity32(uint32_t v) {
  v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
  return (uint8_t)(v & 1u);
}

static void swd_line_reset(void) {
  swd_write(0xFFFFFFFFul, 32); swd_write(0xFFFFFFFFul, 32);
  swd_write(0xE79Eul, 16);                       // JTAG to SWD, harmless on SW-DP
  swd_write(0xFFFFFFFFul, 32); swd_write(0xFFFFFFFFul, 32);
  swd_write(0, 8);
}

static uint8_t swd_xfer(uint8_t ap, uint8_t rd, uint8_t addr, uint32_t *data) {
  if (data == NULL) { return ERR_ARG; }
  const uint8_t a2  = (addr >> 2) & 1u;
  const uint8_t a3  = (addr >> 3) & 1u;
  const uint8_t par = (uint8_t)(ap ^ rd ^ a2 ^ a3) & 1u;
  const uint8_t req = (uint8_t)(0x81u | (ap << 1) | (rd << 2) | (a2 << 3) | (a3 << 4) | (par << 5));

  swd_write(req, 8);
  swd_turn();
  const uint8_t ack = (uint8_t)swd_read(3);
  if (ack != ACK_OK) { swd_turn(); swd_write(0, 8); return ack; }

  if (rd) {
    const uint32_t v = swd_read(32);
    const uint8_t  p = (uint8_t)swd_read(1);
    swd_turn(); swd_write(0, 8);
    if (p != parity32(v)) { return ERR_PARITY; }
    *data = v;
  } else {
    swd_turn();
    swd_write(*data, 32);
    swd_write(parity32(*data), 1);
    swd_write(0, 8);
  }
  return ACK_OK;
}

static uint8_t xfer_retry(uint8_t ap, uint8_t rd, uint8_t addr, uint32_t *data) {
  uint8_t ack = ACK_WAIT;
  for (uint8_t i = 0; (i < 16u) && (ack == ACK_WAIT); i++) { ack = swd_xfer(ap, rd, addr, data); }
  return ack;
}

static uint8_t link_up(uint32_t *idcode) {
  uint32_t v = 0;
  uint8_t ack;
  if (idcode == NULL) { return ERR_ARG; }
  swd_line_reset();
  ack = xfer_retry(0, 1, 0x0, &v);               // DPIDR
  if (ack != ACK_OK) { return ack; }
  *idcode = v;
  v = 0x1Eu;                                     // ABORT: clear sticky flags
  ack = xfer_retry(0, 0, 0x0, &v);
  if (ack != ACK_OK) { return ack; }
  v = 0x50000000ul;                              // CTRL/STAT: request debug and system power
  ack = xfer_retry(0, 0, 0x4, &v);
  if (ack != ACK_OK) { return ack; }
  for (uint8_t i = 0; i < 50u; i++) {
    ack = xfer_retry(0, 1, 0x4, &v);
    if (ack != ACK_OK) { return ack; }
    if ((v & 0xA0000000ul) == 0xA0000000ul) { return ACK_OK; }
  }
  return ERR_TIMEOUT;
}

static void print_hex32(uint32_t v) {
  for (int8_t s = 28; s >= 0; s -= 4) { Serial.print((uint8_t)((v >> s) & 0xFu), HEX); }
}

static uint8_t dump_mem(uint32_t base, uint16_t words) {
  uint32_t v = 0;
  uint8_t ack;
  v = 0;                  ack = xfer_retry(0, 0, 0x8, &v); if (ack != ACK_OK) { return ack; }  // SELECT
  v = CSW_WORD_INC;       ack = xfer_retry(1, 0, 0x0, &v); if (ack != ACK_OK) { return ack; }  // CSW
  while (words > 0) {
    const uint16_t n = (words > 256u) ? 256u : words;   // TAR auto-increment wraps at 1 KB
    v = base;             ack = xfer_retry(1, 0, 0x4, &v); if (ack != ACK_OK) { return ack; }  // TAR
    ack = xfer_retry(1, 1, 0xC, &v);                     if (ack != ACK_OK) { return ack; }  // posted read, discard
    for (uint16_t i = 0; i < n; i++) {
      ack = ((uint16_t)(i + 1u) < n) ? xfer_retry(1, 1, 0xC, &v)   // DRW
                                     : xfer_retry(0, 1, 0xC, &v);  // RDBUFF
      if (ack != ACK_OK) { return ack; }
      print_hex32(base + 4ul * i); Serial.print(' '); print_hex32(v); Serial.println();
    }
    base += 4ul * n;
    words -= n;
  }
  return ACK_OK;
}

void setup() {
  uint32_t id = 0;
  uint8_t ack = ERR_TIMEOUT;
  Serial.begin(115200);
  PORTD &= (uint8_t)~(SWCLK_MASK | SWDIO_MASK | XRES_MASK);
  DDRD  |= SWCLK_MASK;
  DDRD  |= XRES_MASK; delay(5); DDRD &= (uint8_t)~XRES_MASK;   // pulse XRES low, then release

  for (uint8_t i = 0; (i < 20u) && (ack != ACK_OK); i++) { ack = link_up(&id); }
  Serial.print(F("link ack=")); Serial.print(ack); Serial.print(F(" id=")); print_hex32(id); Serial.println();
  if (ack == ACK_OK) {
    ack = dump_mem(0x20000000ul, 1024u);
    Serial.print(F("dump ack=")); Serial.println(ack);
  }
}

void loop(void) {}
