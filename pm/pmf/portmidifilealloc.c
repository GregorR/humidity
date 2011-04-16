#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portmidifilealloc.h"

PmfAllocators Pmf_Allocators;
#define AL Pmf_Allocators

/* internal malloc-wrapper with error checking */
void *Pmf_Malloc(size_t sz)
{
    void *ret = AL.malloc(sz);
    if (ret == NULL) {
        fprintf(stderr, "Error while allocating memory: %s\n", AL.strerror());
        exit(1);
    }
    return ret;
}

/* same for calloc */
void *Pmf_Calloc(size_t sz)
{
    void *ret = Pmf_Malloc(sz);
    memset(ret, 0, sz);
    return ret;
}
