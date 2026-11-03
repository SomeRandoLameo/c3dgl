#include <stddef.h>
#include <stdlib.h>

void *c3dglPlatformAllocate(size_t size, size_t alignment) {
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    
    void *pointer = NULL;
    
    return posix_memalign(&pointer, alignment, size) == 0 ? pointer : NULL;
}

void c3dglPlatformFree(void *pointer) { 
    free(pointer); 
}
