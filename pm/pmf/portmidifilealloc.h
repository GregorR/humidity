#ifndef PORTMIDIFILEALLOC_H
#define PORTMIDIFILEALLOC_H

/* this is an internal header */
#include <stdlib.h>

/* pluggable allocators */
typedef struct __PmfAllocators PmfAllocators;
struct __PmfAllocators {
    void *(*malloc)(size_t);
    const char *(*strerror)();
    void (*free)(void *);
};
extern PmfAllocators Pmf_Allocators;
#define AL Pmf_Allocators

void *Pmf_Malloc(size_t sz);
void *Pmf_Calloc(size_t sz);

/* calloc of a type */
#define Pmf_New(tp) (Pmf_Calloc(sizeof(tp)))

#endif
