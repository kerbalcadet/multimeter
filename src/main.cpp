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

const MeasureMode MEASURE_VOLTAGE = MeasureMode("Voltage", "V");
const MeasureMode MEASURE_CONTINUITY = MeasureMode("Continuity", "");
const MeasureMode MEASURE_MODES[] = {MEASURE_VOLTAGE, MEASURE_CONTINUITY};
const int N_MEASURE_MODES = 2;
int cur_measure_mode_idx = 0;

void cycle_measure_mode(bool forward) {
  int d_idx = forward ? 1 : -1;
  cur_measure_mode_idx += d_idx;
  cur_measure_mode_idx = cur_measure_mode_idx % N_MEASURE_MODES;
}

MeasureMode cur_measure_mode() { return MEASURE_MODES[cur_measure_mode_idx]; }

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

void start_measurement() { adc.setConfig(cur_measure_mode().cfg); }

// here, a pcf pin value of 0 (grounded) is what enables that optorelay. Therefore 1 = disabled, 0 = enabled
const float measure_voltage() {
  float v_reading = 0.0;
  float v_corr = 0.0;

  for (int range = 0; range < V_NUM_RANGES; range++) {
    pcf.write(PCF_RANGE_PINS[range], 1);
  }

  for (int range = 0; range < V_NUM_RANGES; range++) {
    start_measurement();

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

  float v = roundf(v_corr * 100.0f) / 100.0f; // rounded to 2 decimal places (up or down)
  return v;
}

const int CONT_MEASURE_PIN = PCF_RANGE_PINS[1];
bool measure_continuity() {
  start_measurement();

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

bool process_input() {
  extern u_long frame_time;

  // main button
  static bool last_button_state = 1; // 0 = pressed
  static int button_hold_time = 0;
  static int idle_time = 0;
  bool button_state = pcf.read(PCF_BUTTON_PIN);

  if (button_state == 0) {
    button_hold_time += frame_time;
    idle_time = 0;

    if (last_button_state == 1) {
      cycle_measure_mode(true);
      last_button_state = button_state;
      return true;
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

  // serial commands
  static char cmd_buffer[MAX_CMD_SIZE];
  static int cmd_len = 0;

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

  return false;
}

// ARDUINO CALLS

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
  screen::update_state(cur_measure_mode(), true, "0.00");
  screen::init_display();

  delay(200);
  device_state = DeviceState::RUNNING;
}

u_long frame_time = 0;
u_long last_millis = 0;
char last_reading[8] = "";
void loop() {
  if (device_state == DeviceState::SETUP_FAILED || device_state == DeviceState::SETUP) {
    return;
  }

  auto new_millis = millis();
  frame_time = new_millis - last_millis;
  last_millis = new_millis;

  bool mode_changed = process_input();

  if (device_state != DeviceState::RUNNING) {
    delay(ASLEEP_FRAME_TIME);
    return;
  }

  char reading_buf[8];
  const MeasureMode cur_mode = cur_measure_mode();
  if (cur_mode == MEASURE_CONTINUITY) {
    bool cont = measure_continuity();

    // TODO: RM
    digitalWrite(BUZZER_PIN, LOW);
    if (cont) {
      // tone(BUZZER_PIN, 1000, AWAKE_FRAME_TIME + 200); // using timer because noTone causes electrical noise to be audible somehow
      strcpy(reading_buf, "----");
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      strcpy(reading_buf, "-/ -");
    }

  } else if (cur_mode == MEASURE_VOLTAGE) {
    float v = measure_voltage();
    snprintf(reading_buf, 8, "%.2f", v);
  }

  if (strcmp(reading_buf, last_reading) != 0 || mode_changed) {
    strcpy(last_reading, reading_buf);
    screen::update_state(cur_measure_mode(), mode_changed, reading_buf);
  } else {
    delay(AWAKE_FRAME_TIME);
  }
}
