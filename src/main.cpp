#include "HardwareSerial.h"
#include "esp32-hal-gpio.h"
#include <ADC_SGM58031.h>
#include <Arduino.h>
#include <PCF8574.h>
#include <Wire.h>
#include <cmath>
#include <cstdint>
#include <display.h>
#include <types.h>

// DEVICES

PCF8574 pcf(0x20);
RAK_ADC_SGM58031 adc(0x48);

// Global defs

const int AWAKE_FRAME_TIME = 200;                  // ms if display does not update, ~450 ms if display updates
const int ASLEEP_FRAME_TIME = 1000;                // ms
const int POWER_ON_HOLD_TIME = 100;                // ms
const int POWER_OFF_HOLD_TIME = 3000;              // ms
constexpr int POWER_OFF_IDLE_TIME = 5 * 60 * 1000; // mins to ms

const MeasureMode MEASURE_VOLTAGE = MeasureMode(true, "Voltage", "V");
const MeasureMode MEASURE_CONTINUITY = MeasureMode(false, "Continuity", "");
const MeasureMode *p_cur_measure_mode = &MEASURE_VOLTAGE;

const int PCF_RANGE_PINS[] = {1, 3, 5};
const int PCF_POWER_INJECT_PIN = 7;
const int PCF_BUTTON_PIN = 0;
const int BUZZER_PIN = D6;
const int V_NUM_RANGES = 3;
// const float RANGE_RESISTANCES[] = {19900.0, 218800.0, 1208000.0, 1307000.0}; // resistance from bottom of ladder to measurement points
const float V_RANGE_CORR_FACTORS[] = {65.678, 5.973, 1.082}; // ladder: gnd -> 20k (0) -> 200k (1) -> 1M (2) -> 100k -> V+
const float MIN_SIGNIFICANT_VOLTAGE = 0.01f;
const float MAX_SIGNIFICANT_VOLTAGE = 1.0f;

enum class DeviceState { SETUP, SETUP_FAILED, RUNNING, ASLEEP } device_state;

// HARDWARE INTERFACES

volatile bool mode_changed = false;
void switch_measure_mode() {
  mode_changed = true;
  p_cur_measure_mode = (p_cur_measure_mode == &MEASURE_VOLTAGE) ? &MEASURE_CONTINUITY : &MEASURE_VOLTAGE;
}

volatile bool button_pressed = false;
void pcf_interrupt() { button_pressed = true; }

// here, a pcf pin value of 0 (grounded) is what enables that optorelay. Therefore 1 = disabled, 0 = enabled
float measure_voltage() {
  float v_reading = 0.0;
  float v_corr = 0.0;

  for (int range = 0; range < V_NUM_RANGES; range++) {
    pcf.write(PCF_RANGE_PINS[range], 1);
  }

  for (int range = 0; range < V_NUM_RANGES; range++) {
    adc.setConfig(p_cur_measure_mode->cfg);
    const int pin = PCF_RANGE_PINS[range];
    pcf.write(pin, 0);
    delay(150);

    v_reading = adc.getVoltage();
    v_corr = v_reading * V_RANGE_CORR_FACTORS[range];

    pcf.write(pin, 1);

    if (v_reading > MIN_SIGNIFICANT_VOLTAGE || v_reading * 10.0 > 3.3) {
      break;
    }
  }

  float v = roundf(v_corr * 100.0f) / 100.0f; // 2 decimal places
  return v;
}

const int CONT_MEASURE_PIN = PCF_RANGE_PINS[1];
bool measure_continuity() {
  adc.setConfig(p_cur_measure_mode->cfg);
  pcf.write(PCF_POWER_INJECT_PIN, 0);
  pcf.write(CONT_MEASURE_PIN, 0);
  delay(150);

  bool cont = adc.getVoltage() < 0.02;

  pcf.write(CONT_MEASURE_PIN, 1);
  pcf.write(PCF_POWER_INJECT_PIN, 1);

  return cont;
}

void scanI2C() {
  for (uint8_t addr = 0x07; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
      Serial.print("Device at: 0x");
      Serial.println(addr, HEX);
    }
  }
}

// USER INTERACT

void set_sleep(bool asleep) {
  if (asleep) {
    screen::sleep();
    device_state = DeviceState::ASLEEP;
  } else {
    screen::wake();
    device_state = DeviceState::RUNNING;
  }
}
void toggle_sleep() { set_sleep(device_state == DeviceState::RUNNING); }

const uint8_t MAX_CMD_SIZE = 32;
void submit_command(char *user_cmd) {
  Serial.println(user_cmd);
  if (strcmp(user_cmd, "reset") == 0) {
    Serial.println("restarting");
    ESP.restart();

  } else if (strcmp(user_cmd, "wake") == 0) {
    set_sleep(false);

  } else if (strcmp(user_cmd, "sleep") == 0) {
    set_sleep(true);
  }
}

char cmd_buffer[MAX_CMD_SIZE];
int cmd_len = 0;
bool last_button_state = 1; // 0 = pressed
int button_hold_time = 0;
int idle_time = 0;
extern u_long frame_time;
void process_input() {
  // process button
  bool button_state = pcf.read(PCF_BUTTON_PIN);

  if (button_state == 0) {
    button_hold_time += frame_time;
    idle_time = 0;

    if (last_button_state == 1) {
      // TODO: switch measure mode
    }
  } else {
    button_hold_time = 0;

    if (device_state == DeviceState::RUNNING) {
      idle_time += frame_time;
    }
  }
  last_button_state = button_state;

  if ((device_state == DeviceState::RUNNING && button_hold_time > POWER_OFF_HOLD_TIME) ||
      (device_state == DeviceState::ASLEEP && button_hold_time > POWER_ON_HOLD_TIME)) {
    toggle_sleep();
    button_hold_time = 0;

  } else if (idle_time > POWER_OFF_IDLE_TIME) {
    set_sleep(true);
    idle_time = 0;
  }
  Serial.println(idle_time);

  // process serial commands
  while (Serial.available() && (cmd_len < MAX_CMD_SIZE)) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmd_len > 0) {
        cmd_buffer[cmd_len] = '\0';
        submit_command(cmd_buffer);
        cmd_len = 0;
      }
      cmd_len = 0;
      continue;

    } else {
      cmd_buffer[cmd_len++] = c;
    }
  }
}

// ARDUINO CALLS

float voltage = 0; // V
void setup() {
  Serial.begin(115200);
  pinMode(0, OUTPUT);

  Wire.begin(SDA, SCL);
  delay(3000); // allow serial to catch up

  scanI2C();

  if (!pcf.begin()) {
    Serial.print("no pcf connection");
    device_state = DeviceState::SETUP_FAILED;
  }
  delay(100);

  // adc (sgm58031)

  adc.begin();

  if (adc.getChipID() != DEVICE_ID) {
    Serial.println("no adc connection");
    device_state = DeviceState::SETUP_FAILED;
  } else {
    adc.setVoltageResolution(2.048);
    delay(100);
  }

  if (device_state == DeviceState::SETUP_FAILED) {
    Serial.println("Setup failed!");
    return;
  }

  pinMode(2, OUTPUT);
  pinMode(D0, OUTPUT);
  pinMode(D3, INPUT_PULLDOWN);
  pinMode(D7, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // display (SPI)
  screen::update_state(&MEASURE_VOLTAGE, 0.0f);
  screen::init_display();

  delay(200);
  device_state = DeviceState::RUNNING;
}

u_long frame_time = 0;
u_long last_millis = 0;
void loop() {
  if (device_state == DeviceState::SETUP_FAILED || device_state == DeviceState::SETUP) {
    return;
  }

  auto new_millis = millis();
  frame_time = new_millis - last_millis;
  last_millis = new_millis;

  process_input();

  if (device_state != DeviceState::RUNNING) {
    delay(ASLEEP_FRAME_TIME);
    return;
  }

  if (false) {
    bool cont = measure_continuity();

    if (cont) {
      tone(BUZZER_PIN, 1000, AWAKE_FRAME_TIME + 200); // using timer because noTone causes electrical noise to be audible somehow
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }

    delay(AWAKE_FRAME_TIME);
    return;
  }

  // measurement
  float new_voltage = measure_voltage();
  if (new_voltage != voltage || mode_changed) {
    voltage = new_voltage;
    mode_changed = false;
    screen::update_state(p_cur_measure_mode, voltage);
  } else {
    delay(AWAKE_FRAME_TIME);
  }
};
