#include "Sanity.h"
#include "Process.h"

void gbshutdownAbort( bool save_on_abort ) {
	g_process.shutdownAbort(save_on_abort);
}

void gbshutdownResourceError() {
	gbshutdownAbort(false);
}

void gbshutdownLogicError() {
	gbshutdownAbort(true);
}

void gbshutdownCorrupted() {
	gbshutdownAbort(false);
}
