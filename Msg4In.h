// Gigablast, Inc., copyright Jul 2007

// like Msg1.h but buffers up the add requests to avoid packet storms

#ifndef GB_MSG4IN_H
#define GB_MSG4IN_H

bool registerMsg4Handler();

bool initializeMsg4IncomingThread();
void finalizeMsg4IncomingThread();

#endif // GB_MSG4IN_H
