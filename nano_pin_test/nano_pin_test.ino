// Pin test for the SWD wiring: steps D2 and D3 through fixed levels so the chip pads can be probed with a multimeter.
// Expected at the chip pads: about 3.3 V for a Nano high (through the 1k/2k divider), 0 V for a Nano low. See README.md.
#define SWCLK 2   // D2 -> chip pin 13 (SWDCLK)
#define SWDIO 3   // D3 -> chip pin 12 (SWDIO)
#define XRES  4   // D4 -> chip pin 30 (XRES), left as an input here

static void phase(const __FlashStringHelper *name, uint8_t clk, uint8_t dio) {
  digitalWrite(SWCLK, clk);
  digitalWrite(SWDIO, dio);
  Serial.print(name);
  Serial.print(F("  XRES reads "));
  Serial.println(digitalRead(XRES));
  delay(3000);
}

void setup() {
  Serial.begin(115200);
  pinMode(SWCLK, OUTPUT);
  pinMode(SWDIO, OUTPUT);
  pinMode(XRES, INPUT);
}

void loop() {
  phase(F("D2 HIGH, D3 LOW "), HIGH, LOW);
  phase(F("D2 LOW,  D3 HIGH"), LOW, HIGH);
  phase(F("D2 LOW,  D3 LOW "), LOW, LOW);
}
