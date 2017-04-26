// Gigablast, Inc., copyright Jul 2007

// like Msg1.h but buffers up the add requests to avoid packet storms

#ifndef GB_MSG4IN_H
#define GB_MSG4IN_H

namespace Msg4In {

bool registerHandler();

bool initializeIncomingThread();
void finalizeIncomingThread();

}

#endif // GB_MSG4IN_H
