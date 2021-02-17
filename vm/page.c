#include "vm/page.h"
#include "userprog/syscall.h"

static unsigned spte_hash_func (const struct hash_elem *e, void *aux);
static bool spte_less_func (const struct hash_elem *a, 
                            const struct hash_elem *b,
                            void *aux UNUSED);

static unsigned 
spte_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
    struct spte *spte = hash_entry (e, struct spte, elem);
    return (unsigned) hash_int (spte->id);
}

bool spte_less_func (const struct hash_elem *a, 
                     const struct hash_elem *b,
                     void *aux UNUSED)
{
    struct spte *spte_a = hash_entry(a, struct spte, elem);
    struct spte *spte_b = hash_entry(b, struct spte, elem);
    return spte_a->id < spte_b->id;
}

void 
init_spt (struct hash *hash_table)
{
    bool success = hash_init (hash_table, spte_hash_func, 
                              spte_less_func, NULL);

    if (!success)
      exit (-1);
}