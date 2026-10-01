#include "chirpmotion.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef union { struct { size_t bytes; CMHeap *heap; } record; max_align_t alignment; } Block;
void cm_error(CMError *error, const char *format, ...) {
    if (!error) return;
    va_list args; va_start(args,format);
    (void)vsnprintf(error->message,sizeof error->message,format,args);
    va_end(args);
}
void *cm_alloc(CMHeap *heap, size_t count, size_t size, CMError *error) {
    if (!heap || !size || count>(SIZE_MAX-sizeof(Block))/size) {
        cm_error(error,"allocation size overflow"); return NULL;
    }
    size_t bytes=count*size+sizeof(Block);
    if (heap->live>SIZE_MAX-bytes) { cm_error(error,"allocation accounting overflow"); return NULL; }
    Block *block=calloc(1,bytes);
    if (!block) { cm_error(error,"cannot allocate processing buffer"); return NULL; }
    block->record.bytes=bytes; block->record.heap=heap;
    heap->live+=bytes;
    if (heap->live>heap->peak) heap->peak=heap->live;
    return block+1;
}
void cm_free(void *pointer) {
    if (pointer) {
        Block *block=(Block *)pointer-1;
        block->record.heap->live-=block->record.bytes;
        free(block);
    }
}
