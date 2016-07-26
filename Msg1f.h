// Zak Betz, copyright Gigablast Jan 2005

//Get stuff from remote hosts...
//

#ifndef GB_MSG1F_H
#define GB_MSG1F_H

#include <stdint.h>

class UdpSlot;

class Msg1f {
public:
	Msg1f();
	~Msg1f();
	
	static bool init();

	static bool getLog(int32_t hostId, int32_t numBytes, void *state, void ( *callback) (void *state, UdpSlot* slot));
};

#endif // GB_MSG1F_H
