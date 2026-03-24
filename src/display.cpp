#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <cmath>
#include <roboto64.h>
#include <tinos24.h>
#include <types.h>
using namespace std;

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(/*CS*/ 0, /*DC*/ D3, /*RST*/ D7,
                   /*BUSY*/ D6)); // GDEH0213B72 122x250, SSD1675A (IL3897)

namespace screen {

// global defs

const MeasureMode *measure_mode = nullptr;
float cur_measurement = 0.00;

// internal

const int n_light_levels = 5;
int light_level() { return 3; }

const int n_batt_cells = 4;

int batt_level() { return 2; }

std::string_view get_header() { return measure_mode->name; }

std::string_view get_unit() { return measure_mode->unit; }

// DISPLAY

const int h = display.width();
const int w = display.height();
const int wh = 1;
const int bl = 0;
const int header_size = 32;
const int side_size = 30;
const int corner_size = header_size + 10;

struct vec2 {
  float x, y;

  vec2 operator+(const vec2 &v2) const { return {x + v2.x, y + v2.y}; };
  vec2 operator-(const vec2 &v2) const { return {x - v2.x, y - v2.y}; };
  vec2 operator-() const { return {-x, -y}; };
  vec2 operator*(const float mul) const { return {x * mul, y * mul}; };
  vec2 operator/(const float mul) const { return {x / mul, y / mul}; };

  vec2 rotate(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    float new_x = x * c - y * s;
    float new_y = x * s + y * c;
    x = new_x;
    y = new_y;

    return *this;
  }

  vec2 rotated(float angle) const {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(x * c - y * s, x * s + y * c);
  }
};

void fast_draw(const int window[], void(draw_func)()) {
  display.setPartialWindow(window[0], window[1], window[2], window[3]);
  display.firstPage();

  do {
    // display.fillRect(window[0], window[1], window[2], window[3],
    // GxEPD_WHITE);
    draw_func();
  } while (display.nextPage());
}

const char *str_high = "HI";
const char *str_low = "LOW";
void draw_reading() {
  display.setFont(&Roboto_Mono_64);
  display.setTextColor(GxEPD_BLACK);
  int x = 45;
  int y = h - 20;

  display.setCursor(x, y);
  string to_print;
  display.print(cur_measurement, 2);
}

const int r_wind[] = {45, 38, 160, 64};
auto draw_reading_fast = []() { fast_draw(r_wind, draw_reading); };

void draw_bg() {
  display.fillScreen(GxEPD_WHITE);

  // sides
  display.drawFastHLine(0, header_size, w, bl);
  display.drawFastVLine(side_size, 0, h, bl);
  display.drawFastVLine(0, 0, h, bl);
  display.drawFastHLine(0, 0, w, bl);

  // corner
  display.fillRoundRect(0, 0, corner_size, corner_size, 10, bl);
  display.fillRect(0, 0, corner_size, 10, bl);
  display.fillRect(0, 0, 10, corner_size, bl);
}

void draw_triangle_circ(int num, vec2 center_pos, int rad, float len,
                        int base_len, float start_ang_deg, bool fill,
                        int color) {
  float start_rot = (start_ang_deg * 3.14f / 180.0f);
  float step_rot = (2.0f * PI / num);

  vec2 dir = vec2(1.0f, 0.0f).rotate(start_rot);
  for (int i = 0; i < num; i++) {
    vec2 left = dir.rotated(PI * 0.5f);
    vec2 bpos = center_pos + dir * rad;
    vec2 v0 = bpos + vec2(left * base_len);
    vec2 v1 = bpos - vec2(left * base_len);
    vec2 v2 = bpos + vec2(dir * len);

    if (!fill) {
      display.drawTriangle(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, color);
    } else {
      display.fillTriangle(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, color);
    }

    dir.rotate(step_rot);
  }
}

void draw_sun() {
  const int cx = side_size / 2 + 5;
  const int cy = side_size / 2 + 5;
  const int cr = corner_size * 0.3f;
  display.fillCircle(cx, cy, cr, wh);

  // draw_triangle_circ(4, vec2(cx, cy), cr - 3, 10, 8, 90);
  // draw_triangle_circ(4, vec2(cx, cy), cr + 2, 10, 6, 45, true, wh);
  // draw_triangle_circ(4, vec2(cx, cy), cr + 2, 5, 12, 45, true, bl);
  draw_triangle_circ(4, vec2(cx, cy), cr + 2, 5, 12, 45, false, wh);
}

void draw_light_bar() {
  // left bar

  int level = light_level();
  const int boxh = (h - corner_size) / n_light_levels;

  // bottom up
  for (int i = 0; i < n_light_levels; i++) {
    int b_top = h - (boxh * i);
    display.drawFastHLine(0, b_top - boxh, side_size, bl);

    if (i < level) {
      // fill
      int pad = 2;
      display.fillRect(pad, b_top + pad - boxh, side_size - 2 * pad + 1,
                       boxh - 2 * pad + 1, bl);
    }
  }
}

void draw_battery() {
  int level = batt_level();
  const int pad = 10;
  const int cap_w = 5;
  const int cap_h = 10;
  const int cell_w = 10;
  const int cell_h = 20;
  const float ell_w_ratio = 0.4;
  constexpr int cell_r = cell_h / 2;
  constexpr int cap_vbuf = (header_size - cap_h) / 2;
  constexpr int cell_vbuf = (header_size - cell_h) / 2;

  // right to left
  // cap
  display.drawEllipse(w - pad, header_size / 2, ell_w_ratio * float(cap_h) / 2,
                      cap_h / 2, bl);
  display.fillEllipse(w - pad - 2, header_size / 2,
                      ell_w_ratio * float(cap_h) / 2, cap_h / 2, wh);
  display.drawFastHLine(w - pad - cap_w, cap_vbuf, cap_w, bl);
  display.drawFastHLine(w - pad - cap_w, cap_h + cap_vbuf, cap_w, bl);

  // cells
  for (int i = 0; i < n_batt_cells; i++) {
    int front = w - pad - cap_w - i * cell_w;
    display.drawEllipse(front, header_size / 2, ell_w_ratio * cell_r, cell_r,
                        bl);

    // fill in
    if (4 - i <= level) {
      display.drawEllipse(front + 1, header_size / 2, ell_w_ratio * cell_r,
                          cell_r, wh);
      display.fillEllipse(front - 1, header_size / 2, ell_w_ratio * cell_r,
                          cell_r, bl);
      display.fillRect(front - cell_w, cell_vbuf, cell_w, cell_h, bl);
    } else {
      // empty/white
      display.fillEllipse(front - 2, header_size / 2, ell_w_ratio * cell_r,
                          cell_r, wh);
    }
  }

  // end
  display.fillEllipse(w - pad - cap_w - 4 * cell_w, header_size / 2,
                      ell_w_ratio * cell_r, cell_r, bl);
  display.drawEllipse(w - pad - cap_w - 4 * cell_w, header_size / 2,
                      ell_w_ratio * cell_r, cell_r, wh);

  // top/bott
  display.drawFastHLine(w - pad - cap_w - cell_w * 4,
                        (header_size - cell_h) / 2, cell_w * 4, bl);
  display.drawFastHLine(w - pad - cap_w - cell_w * 4,
                        cell_h + (header_size - cell_h) / 2, cell_w * 4, bl);
}

const int head_v_pad = 8;
const int head_h_pad = 20;
constexpr int head_x = (corner_size + head_h_pad);
constexpr int head_y = (header_size - head_v_pad);
void draw_header() {
  std::string_view txt = get_header();
  display.setFont(&Tinos_Italic_24);
  display.setTextColor(GxEPD_BLACK);

  display.setCursor(head_x, head_y);
  display.write(txt.data(), txt.length());
}

void redraw_header() { // re-add header line that gets erased due to 8-px
                       // alignment
  draw_header();
  display.drawFastHLine(0, header_size, w, bl);
}
const int h_wind[] = {head_x - 2, head_y - 18, 120, 21};

const int u_x = w - 40;
const int u_y = h - 35;
void draw_unit() {
  display.setCursor(u_x, u_y);
  display.setFont(&Tinos_Italic_24);
  display.print(get_unit().data());
}
const int u_wind[] = {u_x, u_y - 24, 30, 24};

auto draw_measure_mode_fast = []() {
  fast_draw(h_wind, redraw_header);
  fast_draw(u_wind, draw_unit);
};

// interface

void update_state(const MeasureMode *mode, float reading) {
  bool mode_changed = measure_mode != mode;

  measure_mode = mode;
  cur_measurement = reading;

  draw_reading_fast();

  if (mode_changed) {
    draw_measure_mode_fast();
  }
}

void init_display() {
  display.init(115200, true, 2, false);
  display.setRotation(1);
  display.setFullWindow();

  draw_bg();
  draw_sun();
  draw_reading();
  draw_light_bar();
  draw_battery();
  draw_header();
  draw_unit();
  display.display(true);
  display.display(true);
}

void wake() { init_display(); }

void sleep() {
  display.clearScreen();
  display.hibernate();
}

} // namespace screen
