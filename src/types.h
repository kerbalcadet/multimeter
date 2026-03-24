#pragma once
#include <cstdint>
#include <stdexcept>
#include <Arduino.h>

extern const std::unordered_map<float, uint8_t> pga_to_bin;
uint16_t get_adc_config(uint8_t analog_in, uint8_t v_range);

struct MeasureMode {
  uint16_t cfg;
  float v_range;
  float significant_voltage;
  std::string_view name;
  std::string_view unit;

  MeasureMode(float _v_range_float, bool a0_or_a2, std::string_view _name, std::string_view _unit);
};

struct ScopedTimer {
  private :
    uint32_t m_start_time;
    const std::string_view label;
  public :
    ScopedTimer(std::string_view name) : m_start_time(millis()), label(name) {};
    ~ScopedTimer() {
      Serial.write(label.data(), label.size());
      Serial.write(' ');
      Serial.println(millis() - m_start_time);
    }
};