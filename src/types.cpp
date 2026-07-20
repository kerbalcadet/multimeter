#include <cstdint>
#include <types.h>
// Input Multiplexer (MUX) Configuration
// 000 = AINP = AIN0 and AINN = AIN1 (default)
// 001 = AINP = AIN0 and AINN = AIN3
// 010 = AINP = AIN1 and AINN = AIN3
// 011 = AINP = AIN2 and AINN = AIN3
// 100 = AINP = AIN0 and AINN = GND
// 101 = AINP = AIN1 and AINN = GND
// 110 = AINP = AIN2 and AINN = GND
// 111 = AINP = AIN3 and AINN = GND

// Programmable Gain Amplifier (PGA) Configuration
// 000 = FS = ±6.144V (1)
// 001 = FS = ±4.096V (1)
// 010 = FS = ±2.048V (default)
// 011 = FS = ±1.024V
// 100 = FS = ±0.512V
// 101 = FS = ±0.256V
// 110 = FS = ±0.256V
// 111 = FS = ±0.256V
// config int16 : OS | MUX | PGA |M| DR |CM CP CL CQ

// In case settings need to be adjusted again. compile-time anyway.
uint16_t get_adc_config(uint8_t analog_in, uint8_t v_range = 0b010) {
  uint16_t cfg = 0;
  cfg |= (1 << 15); // os=1 (start conversion)
  cfg |= (analog_in << 12);
  cfg |= (v_range << 9);
  cfg |= (1 << 8);     // single shot
  cfg |= (0b010 << 5); // data rate = 25 samp/sec
  cfg |= (0b0011);     // disable comparator

  return cfg;
}

MeasureMode::MeasureMode(const char *_name, const char *_unit) : name(_name), unit(_unit) { cfg = get_adc_config(0b101); }
