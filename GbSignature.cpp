#include "GbSignature.h"
#include "Sanity.h"

[[ noreturn ]] void signature_verification_failed() {
	gbshutdownCorrupted();
}
