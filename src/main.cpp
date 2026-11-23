#include "HardwareSerial.h"
#include "driver/gpio.h"
#include "esp32-hal-adc.h"
#include "esp32-hal-gpio.h"
#include "esp_sleep.h"
#include "hal/gpio_types.h"
#include <ADC_SGM58031.h>
#include <Arduino.h>
#include <PCF8574.h>
#include <Wire.h>
#include <cmath>
#include <cstdint>
#include <display.h>
#include <types.h>

const bool DEBUG = true;

// DEVICES

PCF8574 pcf(0x20);
RAK_ADC_SGM58031 adc(0x48);

// Global defs

const int FRAME_TIME = 200;                        // ms if display does not update, ~450 ms if display updates
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
const int BUTTON_PIN = D9;
const float BATT_CORR_FACTOR = 2.00f;
const int BUZZER_PIN = D6;
const int BATT_MEASURE_PIN = D0;
const int V_NUM_RANGES = 3;
// const float RANGE_RESISTANCES[] = {19900.0, 218800.0, 1208000.0, 1307000.0}; // resistance from bottom of ladder to measurement points
const float V_RANGE_CORR_FACTORS[] = {72.5, 6.06, 1.082}; // ladder: gnd -> 20k (0) -> 200k (1) -> 1M (2) -> 100k -> V+
const float MIN_SIGNIFICANT_VOLTAGE = 0.01f;
const float MAX_SIGNIFICANT_VOLTAGE = 1.0f;

enum class DeviceState { SETUP, SETUP_FAILED, RUNNING, ALL_ASLEEP, AWAITING_SLEEP, SCREEN_ASLEEP } device_state;

// HARDWARE INTERFACES

void on_button_pressed();

void wake() {
  screen::wake();
  device_state = DeviceState::RUNNING;
}

void light_sleep(uint64_t ms = 0, bool interruptable = true) {
  if (ms > 0) {
    esp_sleep_enable_timer_wakeup(ms * 1000);
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

  if (interruptable) {
    esp_sleep_enable_gpio_wakeup();
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  }

  esp_light_sleep_start();

  // after wake

  if (device_state == DeviceState::ALL_ASLEEP) {
    wake();
  }
}

void long_sleep() {
  device_state = DeviceState::ALL_ASLEEP;
  light_sleep();
}

void start_measurement() { adc.setConfig(cur_measure_mode().cfg); }

// pcf pin value of 0 (grounded) enables optical relay
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
    // Serial.println(v_reading, 4);
    if (v_reading > 1.7) {
      return -1.0f;
    }
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
  light_sleep(150, false);

  bool cont = adc.getVoltage() < 0.02;

  pcf.write(CONT_MEASURE_PIN, 1);
  pcf.write(PCF_POWER_INJECT_PIN, 1);

  return cont;
}

float measure_battery() { return analogReadMilliVolts(D0) * BATT_CORR_FACTOR / 1000.0f; }

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

volatile bool button_pressed = false;
void IRAM_ATTR on_button_pressed() { button_pressed = true; }

const uint8_t MAX_CMD_SIZE = 32;
void submit_command(char *user_cmd) {
  Serial.println(user_cmd);
  if (strcmp(user_cmd, "reset") == 0) {
    Serial.println("restarting");
    ESP.restart();

  } else if (strcmp(user_cmd, "wake") == 0) {
    wake();

  } else if (strcmp(user_cmd, "sleep") == 0) {
    long_sleep();
  }
}

void process_input() {
  extern u_long frame_time;
  bool button_pressed = !digitalRead(BUTTON_PIN);

  if (device_state == DeviceState::AWAITING_SLEEP) {
    screen::sleep();
    device_state = DeviceState::SCREEN_ASLEEP;
    return;

  } else if (device_state == DeviceState::SCREEN_ASLEEP) {
    if (!button_pressed) {
      long_sleep(); // sleep CPU after button is released to avoid immediate wakeup trigger
    }

    return;
  }

  if (button_pressed) {
    cycle_measure_mode(true);
    button_pressed = false;
  }

  // main button
  static int button_hold_time = 0;
  static int idle_time = 0;

  if (button_pressed) {
    button_hold_time += frame_time;
    idle_time = 0;

  } else {
    button_hold_time = 0;

    if (device_state == DeviceState::RUNNING) {
      idle_time += frame_time;
    }
  }

  if (device_state == DeviceState::RUNNING && (button_hold_time > POWER_OFF_HOLD_TIME || idle_time > POWER_OFF_IDLE_TIME)) {
    device_state = DeviceState::AWAITING_SLEEP;
    button_hold_time = 0;
    idle_time = 0;
  }

  if (!DEBUG) {
    return;
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
}

// ARDUINO CALLS

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA, SCL);

  if (DEBUG) {
    delay(3000); // allow serial to catch up
    scanI2C();
  }

  // initialize devices

  if (!pcf.begin()) {
    Serial.print("no pcf connection");
    device_state = DeviceState::SETUP_FAILED;
  }
  delay(100);

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

  // set up pins

  pinMode(D1, OUTPUT);
  pinMode(D3, INPUT_PULLDOWN);
  pinMode(D7, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);

  // sleep / interrupts

  attachInterrupt(BUTTON_PIN, on_button_pressed, FALLING);
  // gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);

  // display (SPI)
  screen::update_state(cur_measure_mode(), true, "0.00", screen::batt_level(measure_battery()));
  screen::init_display();

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

  // interaction
  bool cycled = button_pressed;
  process_input();

  // get measurement
  char reading_buf[8];
  const MeasureMode cur_mode = cur_measure_mode();
  if (cur_mode == MEASURE_CONTINUITY) {
    bool cont = measure_continuity();

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
    if (v >= 0) {
      snprintf(reading_buf, 8, "%.2f", v);
    } else {
      snprintf(reading_buf, 8, " NEG");
    }
  }

  // update screen
  if (cycled || strcmp(reading_buf, last_reading) != 0) {
    strcpy(last_reading, reading_buf);

    float batt_volt = measure_battery();
    int batt_level = screen::batt_level(batt_volt);
    screen::update_state(cur_measure_mode(), cycled, reading_buf, batt_level);

    if (DEBUG) {
      Serial.print("Battery Voltage: ");
      Serial.println(batt_volt);
    }
  } else {
    light_sleep(FRAME_TIME, !button_pressed);
  }
}
