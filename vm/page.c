#include "threads/malloc.h"
#include "userprog/syscall.h"
#include "vm/page.h"

static unsigned spte_hash_func (const struct hash_elem *e, void *aux);
static bool spte_less_func (const struct hash_elem *a,
                            const struct hash_elem *b, void *aux UNUSED);

static unsigned 
spte_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct spte *spte = hash_entry (e, struct spte, elem);
  return (unsigned) hash_bytes (spte->page_uaddr, sizeof (void *));
}

bool
spte_less_func (const struct hash_elem *a,
                const struct hash_elem *b, void *aux UNUSED)
{
  struct spte *spte_a = hash_entry (a, struct spte, elem);
  struct spte *spte_b = hash_entry (b, struct spte, elem);
  return spte_a->page_uaddr < spte_b->page_uaddr;
}

void 
spt_init (struct hash *hash_table)
{
  bool success = hash_init (hash_table, spte_hash_func, spte_less_func, NULL);
  if (!success)
    exit_error (-1);
}

struct spte 
*spte_create (void *page_uaddr, enum location loc,
              struct file *file, off_t offset)
{
  struct spte *spte = malloc (sizeof (struct spte));
  spte->page_uaddr = page_uaddr;
  spte->loc = loc;
  spte->file = file;
  spte->offset = offset;

  return spte;
}

void
spt_insert (struct hash *spt, struct hash_elem *he)
{
  hash_insert (spt, he);
}

void
spt_delete (struct hash *spt, struct hash_elem *he)
{
  hash_delete (spt, he);
}

void 
spte_free (struct hash_elem *he, void *aux UNUSED)
{
  struct spte *spte = hash_entry (he, struct spte, elem);
  free (spte);
}

void 
spt_free_table (struct hash *spt)
{
  hash_destroy (spt, spte_free);
}