#include "GbSignature.h"
#include "Sanity.h"

void signature_verification_failed() {
	gbshutdownCorrupted();
}
