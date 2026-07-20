#include <GxEPD2_BW.h>
#include <types.h>

namespace screen {

extern const MeasureMode measure_mode;
extern float cur_measurement;

void init_display();
void update_state(const MeasureMode mode, bool mode_changed, const char *reading);
void wake();
void sleep();

} // namespace screen
