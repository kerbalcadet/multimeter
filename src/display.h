#include <GxEPD2_BW.h>
#include <types.h>

namespace screen {

extern const MeasureMode measure_mode;
extern float cur_measurement;

void init_display();
void update_state(MeasureMode mode, bool mode_changed, const char *reading, int batt_level);
void wake();
void sleep();
int batt_level(float batt_voltage);

} // namespace screen
