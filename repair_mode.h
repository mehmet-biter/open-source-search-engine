#ifndef REPAIR_MODE_H_
#define REPAIR_MODE_H_

enum repair_mode_t {
	REPAIR_MODE_NONE = 0,
	REPAIR_MODE_1 = 1,
	REPAIR_MODE_2 = 2,
	REPAIR_MODE_3 = 3,
	REPAIR_MODE_4 = 4,
	REPAIR_MODE_5 = 5,
	REPAIR_MODE_6 = 6,
	REPAIR_MODE_7 = 7,
	REPAIR_MODE_8 = 8
};

extern repair_mode_t g_repairMode;

#endif
