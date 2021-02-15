#include <debug.h>
#include <stdio.h>
#include <stddef.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "vm/frame.h"

static void *user_pool_base;
static struct frame *frame_table_base;

/* Translates address returned by palloc_get_page() into
   address of corresponding frame in frame_table. */
static inline struct frame *page_addr_to_frame_addr (void * page_addr)
{
    ASSERT (user_pool_base != NULL && page_addr != NULL);
    uint32_t index = ((uintptr_t) page_addr - (uintptr_t) user_pool_base) >> 12;
    return frame_table_base + index;
}

/* Mallocs memory for frame table and gets user_pool_base
   address for use in page addr -> frame addr translation. */
void frame_table_init (size_t num_frames)
{
    frame_table_base = malloc (sizeof(struct frame) * num_frames);
    user_pool_base = palloc_get_user_pool_base ();
}

void *frame_alloc (enum palloc_flags flags)
{
    ASSERT (flags & PAL_USER);
    void *page_addr = palloc_get_page (flags);

    struct frame *f = page_addr_to_frame_addr (page_addr);
    f->page_addr = page_addr;
    
    return page_addr;
}

void frame_free (void *page_addr)
{
    struct frame *f = page_addr_to_frame_addr (page_addr);
    f->page_addr = NULL;
    palloc_free_page (page_addr);
}