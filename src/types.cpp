#include <unordered_map>
#include <cstdint>
#include <stdexcept>
#include <types.h>

const std::unordered_map<float, uint8_t> pga_to_bin{
    {0.256, 0b101},
    {0.512, 0b100},
    {1.024, 0b011},
    {2.048, 0b010},
    {4.096, 0b001},
    {6.144, 0b000}};

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
//config int16 : OS | MUX | PGA |M| DR |CM CP CL CQ
uint16_t get_adc_config(uint8_t analog_in, uint8_t v_range) {
  uint16_t cfg = 0;
  cfg |= (1 << 15); //os=1 (start conversion)
  cfg |= (analog_in << 12);
  cfg |= (v_range << 9);
  cfg |= (1 << 8);  //single shot
  cfg |= (0b010 << 5); //data rate = 25 samp/sec
  cfg |= (0b0011); // disable comparator

  return cfg;

}

const float significant_voltage_ratio = 0.05; //discard measurement if within 5% of max/min
MeasureMode::MeasureMode(float _v_range_float, bool a0_or_a2, std::string_view _name, std::string_view _unit) : 
    v_range(_v_range_float), 
    significant_voltage(significant_voltage_ratio * _v_range_float),
    name(_name),
    unit(_unit)
  {
    if (pga_to_bin.find(_v_range_float) == pga_to_bin.end()) {
      throw std::invalid_argument("invalid voltage range, check pga_to_bin keys");
    }
    uint8_t v_range_bin = pga_to_bin.at(_v_range_float);
    uint8_t mux = a0_or_a2 ? 0b000 : 0b011;
    cfg = get_adc_config(mux, v_range_bin);
  }