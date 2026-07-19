#pragma once
#include <cstdint>
#include <Arduino.h>
#include <sys/types.h>

uint16_t get_adc_config(uint8_t analog_in, uint8_t v_range);

enum class PGA_VOLTAGE : uint8_t {
  V0_256 = 0b101,
  V0_512 = 0b100,
  V1_024 = 0b011,
  V2_048 = 0b010,
  V4_096 = 0b001,
  V6_144 = 0b000,
};

struct MeasureMode {
  uint16_t cfg = 0;
  const char* name = "";
  const char* unit = "";

  MeasureMode(bool a0_or_a2, const char* _name, const char* _unit);
};

struct ScopedTimer {
  private :
    uint32_t m_start_time;
    const char* label;
  public :
    ScopedTimer(const char* name) : m_start_time(millis()), label(name) {};
    ~ScopedTimer() {
      Serial.write(label);
      Serial.write(' ');
      Serial.println(millis() - m_start_time);
    }
};
