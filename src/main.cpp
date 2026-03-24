#include <ADC_SGM58031.h>
#include <Arduino.h>
#include <PCF8574.h>
#include <Wire.h>
#include <cmath>
#include <display.h>
#include <types.h>

// DEVICES

PCF8574 pcf(0x20);
RAK_ADC_SGM58031 adc(0x48);

// Global defs

const int EXPANDER_DISABLE_PIN = D6;
const int PCF_INTERRUPT_PIN = D2;

const MeasureMode MEASURE_VOLTAGE = MeasureMode(6.144, true, "Voltage", "V");
const MeasureMode MEASURE_RESISTANCE = MeasureMode(0.512, false, "Resistance", "Ohm");
const MeasureMode MEASURE_CONTINUITY = MeasureMode(0.512, false, "Continuity", "");
const std::array<const MeasureMode *, 3> p_measure_modes = {&MEASURE_VOLTAGE, &MEASURE_RESISTANCE, &MEASURE_CONTINUITY};
const MeasureMode *p_SelectedMode = p_measure_modes[0];
int measure_idx = 0;

bool asleep = false;

// HARDWARE INTERFACES

void set_measure_mode(const MeasureMode *mode) {
  adc.setVoltageResolution(mode->v_range);
  p_SelectedMode = mode;
}

volatile bool mode_changed = false;
void cycle_mode(int dir) {
  if (dir == 0) {
    return;
  }

  mode_changed = true;
  measure_idx = (measure_idx + dir + p_measure_modes.size()) % p_measure_modes.size();
  set_measure_mode(p_measure_modes[measure_idx]);
}

void on_button_pressed() {
  bool switch_mode_forward = pcf.read(1);
  bool switch_mode_backward = pcf.read(0);

  // cycle direction
  int switch_dir = switch_mode_forward - switch_mode_backward;
  cycle_mode(switch_dir);
}

volatile bool button_pressed = false;
void pcf_interrupt() { button_pressed = true; }

float get_measurement() {
  adc.setConfig(p_SelectedMode->cfg);
  return roundf(adc.getVoltage() * 100.0f) / 100.0f;
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

const uint8_t MAX_CMD_SIZE = 32;
void submit_command(char *user_cmd) {
  Serial.println(user_cmd);
  if (strcmp(user_cmd, "reset") == 0) {
    Serial.println("restarting");
    ESP.restart();

  } else if (strcmp(user_cmd, "wake") == 0) {
    screen::wake();
    asleep = false;

  } else if (strcmp(user_cmd, "sleep") == 0) {
    screen::sleep();
    asleep = true;
  }
}

char cmd_buffer[MAX_CMD_SIZE];
uint8_t cmd_len = 0;
void process_input() {
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

// MAIN THREADS

bool setup_completed = false;
float voltage = 0; // V

int interaction_thread_time = 50; // ms
TaskHandle_t int_task;
void interaction_thread() {
  if (!button_pressed || asleep || !setup_completed) {
    return;
  }

  on_button_pressed();
  button_pressed = false;
}

auto interaction_thread_loop = [](void *parameter) {
  while (true) {
    interaction_thread();
    delay(interaction_thread_time);
  }
};

int main_thread_time = 500; // ms if display does not update
//~450 ms if display updates
TaskHandle_t main_task;
void main_thread() {
  if (!setup_completed) {
    return;
  }

  process_input();

  if (asleep) {
    return;
  }

  // measurement
  float new_voltage = get_measurement();
  if (new_voltage != voltage || mode_changed) {
    voltage = new_voltage;
    mode_changed = false;
    screen::update_state(p_SelectedMode, voltage);
  } else {
    delay(main_thread_time);
  }
}

auto main_thread_loop = [](void *parameter) {
  while (true) {
    main_thread();
  }
};

// ARDUINO CALLS

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA, SCL);
  delay(100);

  // gpio expander (pcf)

  pinMode(EXPANDER_DISABLE_PIN, OUTPUT);
  digitalWrite(EXPANDER_DISABLE_PIN, 1);

  delay(3000); // allow serial to catch up

  scanI2C();

  if (!pcf.begin()) {
    Serial.print("no pcf connection");
    while (1)
      ;
  }
  delay(100);

  // adc (sgm58031)

  adc.begin();
  delay(100);

  if (adc.getChipID() != DEVICE_ID) {
    Serial.print("no adc connection");
    return;
  }

  // user buttons

  attachInterrupt(PCF_INTERRUPT_PIN, pcf_interrupt, FALLING);

  // hand control to gpio expander once it is zeroed
  // pcf.write8(0);
  delay(100);
  // digitalWrite(EXPANDER_DISABLE_PIN, 0);

  // assign thread tasks
  xTaskCreatePinnedToCore(main_thread_loop, "Main Thread", 10000, NULL, 1, &main_task, 0);
  xTaskCreatePinnedToCore(interaction_thread_loop, "Interaction Thread", 2048, NULL, 1, &int_task, 0);

  // display (SPI)
  pinMode(D3, OUTPUT);
  pinMode(D7, OUTPUT);
  pinMode(D6, INPUT);

  screen::update_state(&MEASURE_VOLTAGE, 0.0f);
  screen::init_display();

  delay(200);
  setup_completed = true;
}

void loop() {};
