#ifndef GB_SIGNATURE_H_
#define GB_SIGNATURE_H_

//Macros for inserting a signature into critical structs for verification and
//early and (relatively) cheap detection of memory clobbering

extern void signature_verification_failed();

#if 1

#define declare_signature int signature;
#define set_signature() signature = signature_init
#define clear_signature() signature = 0
#define verify_signature() if(__builtin_expect(signature!=signature_init,0)) signature_verification_failed()
#define verify_signature_at(signature) if(__builtin_expect(signature!=signature_init,0)) signature_verification_failed()

#else

#define declare_signature
#define set_signature()
#define clear_signature()
#define verify_signature()
#define verify_signature_at()

#endif

#endif //GB_SIGNATURE_H_
