#ifndef GB_SANITY_H
#define GB_SANITY_H

#define GBASSERT(c)            (gb_sanityCheck((c),__FILE__,__FUNCTION__,__LINE__))
#define GBASSERTMSG(c, msg)    (gb_sanityCheckMsg((c),(msg),__FILE__,__FUNCTION__,__LINE__))

// Ugly - but so is lots of code in .h files
extern void gbshutdownAbort( bool save_on_abort );


inline void gb_sanityCheck ( bool cond, 
			     const char *file, const char *func, const int line ) {
	if ( ! cond ) {
		log( LOG_LOGIC, "SANITY CHECK FAILED /%s:%s:%d/", 
		     file, func, line );
		gbshutdownAbort(true);
	}
}

inline void gb_sanityCheckMsg ( bool cond, char *msg, 
				const char *file, const char *func, const int line ) {
	if ( ! cond ) {
		log( LOG_LOGIC, "SANITY CHECK FAILED: %s /%s:%s:%d/", 
		     msg, 
		     file, func, line );
		gbshutdownAbort(true);
	}
}


#endif // GB_SANITY_H
