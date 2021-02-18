#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/syscall.h"

static unsigned spte_hash_func (const struct hash_elem *e, void *aux);
static bool spte_less_func (const struct hash_elem *a,
                            const struct hash_elem *b, void *aux UNUSED);
static void spte_free (struct hash_elem *he, void *aux UNUSED);

/* Returns a hash of the user virtual address for the page,
   which is the key for supplemental page table entries. */
static unsigned 
spte_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct spte *spte = hash_entry (e, struct spte, elem);
  return (unsigned) hash_bytes (&spte->page_uaddr, sizeof (void *));
}

/* Comparison function for supplemental page table entries.
   Compares by user virtual address in ascending order. */
bool
spte_less_func (const struct hash_elem *a,
                const struct hash_elem *b, void *aux UNUSED)
{
  struct spte *spte_a = hash_entry (a, struct spte, elem);
  struct spte *spte_b = hash_entry (b, struct spte, elem);
  return spte_a->page_uaddr < spte_b->page_uaddr;
}

/* Initialize the supplemental page table for a process. Terminates
   the process if initialization is unsuccessful. */
void 
spt_init (struct hash *hash_table)
{
  bool success = hash_init (hash_table, spte_hash_func, spte_less_func, NULL);
  if (!success)
    exit_error (-1);
}

/* Insert a supplemental page table entry into the SPT. */
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

/* Frees the supplemental page table of a process by deallocating
   all SPT entries and then deallocating the table itself. */
void 
spt_free_table (struct hash *spt)
{
  hash_destroy (spt, spte_free);
}

/* Creates a supplemental page table entry and initializes it's
   fields. Returns a reference to the newly allocated entry. */
struct spte *
spte_create (void *page_uaddr, enum location loc,
             struct file *file, off_t ofs,
             size_t page_bytes, bool writable)
{
  struct spte *spte = malloc (sizeof (struct spte));
  spte->page_uaddr = page_uaddr;
  spte->loc = loc;
  spte->file = file;
  spte->ofs = ofs;
  spte->page_bytes = page_bytes;
  spte->writable = writable;

  return spte;
}

/* Returns the supplemental page table entry containing the
   provided user virtual address, or a null pointer if no such
   entry exists. */
struct spte *
spte_lookup (void *page_uaddr)
{
  struct spte spte;
  struct hash_elem *he;

  spte.page_uaddr = page_uaddr;
  he = hash_find (&thread_current ()->spt, &spte.elem);
  return he != NULL ? hash_entry (he, struct spte, elem) : NULL;
}

/* Deallocates the supplemental page table entry linked to the
   hash element in the SPT. */
static void 
spte_free (struct hash_elem *he, void *aux UNUSED)
{
  struct spte *spte = hash_entry (he, struct spte, elem);
  free (spte);
}