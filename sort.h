#ifndef GBSORT_H_
#define GBSORT_H_

//*****************************************************************************
//
//	very efficient sorting routines from OpenBSD.
//
//	use gbqsort for generalized sorting tasks.
//	this version of qsort is guaranteed to not devolve into O(N^2) behavior.
//
//	use gbmergesort for sorting tasks where partial ordering is present.
//	gbmergesort approaches O(n) when elements are already partially sorted.
//
//*****************************************************************************

#define gbsort gbmergesort

extern void gbqsort(void *aa, size_t n, size_t es,
			int (*cmp)(const void *, const void *));
extern void gbmergesort(void *base, size_t nmemb, size_t size,
			int (*cmp)(const void *, const void *),
			char* bufSpace = NULL, int32_t bufSpaceSize = 0);

#endif
