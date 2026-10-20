/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Copyright 1995 Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/bitops.h>
#include <linux/hardirq.h> /* for in_interrupt() */
#include <linux/hugetlb_inline.h>

struct pagevec;

/*
 * Bits in mapping->flags.
 */
enum mapping_flags {
	AS_EIO		= 0,	/* IO error on async write */
	AS_ENOSPC	= 1,	/* ENOSPC on async write */
	AS_MM_ALL_LOCKS	= 2,	/* under mm_take_all_locks() */
	AS_UNEVICTABLE	= 3,	/* e.g., ramdisk, SHM_LOCK */
	AS_EXITING	= 4, 	/* final truncate in progress */
	/* writeback related tags are not used */
	AS_NO_WRITEBACK_TAGS = 5,
#if defined(CONFIG_SDP)
	AS_SENSITIVE = __GFP_BITS_SHIFT + 5, /* Group of sensitive pages to be cleaned up */
#endif
};

/**
 * mapping_set_error - record a writeback error in the address_space
 * @mapping - the mapping in which an error should be set
 * @error - the error to set in the mapping
 *
 * When writeback fails in some way, we must record that error so that
 * userspace can be informed when fsync and the like are called.  We endeavor
 * to report errors on any file that was open at the time of the error.  Some
 * internal callers also need to know when writeback errors have occurred.
 *
 * When a writeback error occurs, most filesystems will want to call
 * mapping_set_error to record the error in the mapping so that it can be
 * reported when the application calls fsync(2).
 */
static inline void mapping_set_error(struct address_space *mapping, int error)
{
	if (likely(!error))
		return;

	/* Record in wb_err for checkers using errseq_t based tracking */
	filemap_set_wb_err(mapping, error);

	/* Record it in flags for now, for legacy callers */
	if (error == -ENOSPC)
		set_bit(AS_ENOSPC, &mapping->flags);
	else
		set_bit(AS_EIO, &mapping->flags);
}

static inline void mapping_set_unevictable(struct address_space *mapping)
{
	set_bit(AS_UNEVICTABLE, &mapping->flags);
}

static inline void mapping_clear_unevictable(struct address_space *mapping)
{
	clear_bit(AS_UNEVICTABLE, &mapping->flags);
}

static inline int mapping_unevictable(struct address_space *mapping)
{
	if (mapping)
		return test_bit(AS_UNEVICTABLE, &mapping->flags);
	return !!mapping;
}

static inline void mapping_set_exiting(struct address_space *mapping)
{
	set_bit(AS_EXITING, &mapping->flags);
}

static inline int mapping_exiting(struct address_space *mapping)
{
	return test_bit(AS_EXITING, &mapping->flags);
}

static inline void mapping_set_no_writeback_tags(struct address_space *mapping)
{
	set_bit(AS_NO_WRITEBACK_TAGS, &mapping->flags);
}

static inline int mapping_use_writeback_tags(struct address_space *mapping)
{
	return !test_bit(AS_NO_WRITEBACK_TAGS, &mapping->flags);
}

static inline gfp_t mapping_gfp_mask(struct address_space * mapping)
{
	return mapping->gfp_mask;
}

/* Restricts the given gfp_mask to what the mapping allows. */
static inline gfp_t mapping_gfp_constraint(struct address_space *mapping,
		gfp_t gfp_mask)
{
	return mapping_gfp_mask(mapping) & gfp_mask;
}

/*
 * This is non-atomic.  Only to be used before the mapping is activated.
 * Probably needs a barrier...
 */
static inline void mapping_set_gfp_mask(struct address_space *m, gfp_t mask)
{
	m->gfp_mask = mask;
}

#if defined(CONFIG_SDP)
static inline void mapping_set_sensitive(struct address_space *mapping)
{
	set_bit(AS_SENSITIVE, &mapping->flags);
}

static inline void mapping_clear_sensitive(struct address_space *mapping)
{
	clear_bit(AS_SENSITIVE, &mapping->flags);
}

static inline int mapping_sensitive(struct address_space *mapping)
{
	if (mapping)
		return test_bit(AS_SENSITIVE, &mapping->flags);
	return !!mapping;
}
#endif

void release_pages(struct page **pages, int nr);

static inline bool page_cache_add_speculative(struct page *page, int count)
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	return folio_ref_try_add_rcu((struct folio *)page, count);
}

static inline bool page_cache_get_speculative(struct page *page)
{
	return page_cache_add_speculative(page, 1);
}

#ifdef CONFIG_NUMA
extern struct page *__page_cache_alloc(gfp_t gfp);
#else
static inline struct page *__page_cache_alloc(gfp_t gfp)
{
	if ((gfp & GFP_HIGHUSER_MOVABLE) == GFP_HIGHUSER_MOVABLE)
		return alloc_pages(gfp | __GFP_CMA, 0);
	else
		return alloc_pages(gfp, 0);
}
#endif

static inline struct page *page_cache_alloc(struct address_space *x)
{
	return __page_cache_alloc(mapping_gfp_mask(x));
}

static inline gfp_t readahead_gfp_mask(struct address_space *x)
{
	gfp_t gfp_mask = mapping_gfp_mask(x) |
				__GFP_NORETRY | __GFP_NOWARN;

	if (gfp_mask & __GFP_MOVABLE)
		gfp_mask |= __GFP_CMA;

	return gfp_mask;
}

typedef int filler_t(void *, struct page *);

pgoff_t page_cache_next_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan);
pgoff_t page_cache_prev_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan);

#define FGP_ACCESSED		0x00000001
#define FGP_LOCK		0x00000002
#define FGP_CREAT		0x00000004
#define FGP_WRITE		0x00000008
#define FGP_NOFS		0x00000010
#define FGP_NOWAIT		0x00000020
#define FGP_FOR_MMAP		0x00000040

struct page *pagecache_get_page(struct address_space *mapping, pgoff_t offset,
		int fgp_flags, gfp_t cache_gfp_mask);

/**
 * find_get_page - find and get a page reference
 * @mapping: the address_space to search
 * @offset: the page index
 *
 * Looks up the page cache slot at @mapping & @offset.  If there is a
 * page cache page, it is returned with an increased refcount.
 *
 * Otherwise, %NULL is returned.
 */
static inline struct page *find_get_page(struct address_space *mapping,
					pgoff_t offset)
{
	return pagecache_get_page(mapping, offset, 0, 0);
}

static inline struct page *find_get_page_flags(struct address_space *mapping,
					pgoff_t offset, int fgp_flags)
{
	return pagecache_get_page(mapping, offset, fgp_flags, 0);
}

/**
 * find_lock_page - locate, pin and lock a pagecache page
 * @mapping: the address_space to search
 * @offset: the page index
 *
 * Looks up the page cache slot at @mapping & @offset.  If there is a
 * page cache page, it is returned locked and with an increased
 * refcount.
 *
 * Otherwise, %NULL is returned.
 *
 * find_lock_page() may sleep.
 */
static inline struct page *find_lock_page(struct address_space *mapping,
					pgoff_t offset)
{
	return pagecache_get_page(mapping, offset, FGP_LOCK, 0);
}

/**
 * find_or_create_page - locate or add a pagecache page
 * @mapping: the page's address_space
 * @index: the page's index into the mapping
 * @gfp_mask: page allocation mode
 *
 * Looks up the page cache slot at @mapping & @offset.  If there is a
 * page cache page, it is returned locked and with an increased
 * refcount.
 *
 * If the page is not present, a new page is allocated using @gfp_mask
 * and added to the page cache and the VM's LRU list.  The page is
 * returned locked and with an increased refcount.
 *
 * On memory exhaustion, %NULL is returned.
 *
 * find_or_create_page() may sleep, even if @gfp_flags specifies an
 * atomic allocation!
 */
static inline struct page *find_or_create_page(struct address_space *mapping,
					pgoff_t offset, gfp_t gfp_mask)
{
	return pagecache_get_page(mapping, offset,
					FGP_LOCK|FGP_ACCESSED|FGP_CREAT,
					gfp_mask);
}

/**
 * grab_cache_page_nowait - returns locked page at given index in given cache
 * @mapping: target address_space
 * @index: the page index
 *
 * Same as grab_cache_page(), but do not wait if the page is unavailable.
 * This is intended for speculative data generators, where the data can
 * be regenerated if the page couldn't be grabbed.  This routine should
 * be safe to call while holding the lock for another page.
 *
 * Clear __GFP_FS when allocating the page to avoid recursion into the fs
 * and deadlock against the caller's locked page.
 */
static inline struct page *grab_cache_page_nowait(struct address_space *mapping,
				pgoff_t index)
{
	return pagecache_get_page(mapping, index,
			FGP_LOCK|FGP_CREAT|FGP_NOFS|FGP_NOWAIT,
			mapping_gfp_mask(mapping));
}

static inline struct page *find_subpage(struct page *page, pgoff_t offset)
{
	if (PageHuge(page))
		return page;

	VM_BUG_ON_PAGE(PageTail(page), page);

	return page + (offset & (compound_nr(page) - 1));
}

struct page *find_get_entry(struct address_space *mapping, pgoff_t offset);
struct page *find_lock_entry(struct address_space *mapping, pgoff_t offset);
unsigned find_get_entries(struct address_space *mapping, pgoff_t start,
			  unsigned int nr_entries, struct page **entries,
			  pgoff_t *indices);
unsigned find_get_pages_range(struct address_space *mapping, pgoff_t *start,
			pgoff_t end, unsigned int nr_pages,
			struct page **pages);
static inline unsigned find_get_pages(struct address_space *mapping,
			pgoff_t *start, unsigned int nr_pages,
			struct page **pages)
{
	return find_get_pages_range(mapping, start, (pgoff_t)-1, nr_pages,
				    pages);
}
unsigned find_get_pages_contig(struct address_space *mapping, pgoff_t start,
			       unsigned int nr_pages, struct page **pages);
unsigned find_get_pages_range_tag(struct address_space *mapping, pgoff_t *index,
			pgoff_t end, xa_mark_t tag, unsigned int nr_pages,
			struct page **pages);
static inline unsigned find_get_pages_tag(struct address_space *mapping,
			pgoff_t *index, xa_mark_t tag, unsigned int nr_pages,
			struct page **pages)
{
	return find_get_pages_range_tag(mapping, index, (pgoff_t)-1, tag,
					nr_pages, pages);
}

struct page *grab_cache_page_write_begin(struct address_space *mapping,
			pgoff_t index, unsigned flags);

/*
 * Returns locked page at given index in given cache, creating it if needed.
 */
static inline struct page *grab_cache_page(struct address_space *mapping,
								pgoff_t index)
{
	return find_or_create_page(mapping, index, mapping_gfp_mask(mapping));
}

extern struct page * read_cache_page(struct address_space *mapping,
				pgoff_t index, filler_t *filler, void *data);
extern struct page * read_cache_page_gfp(struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);
extern int read_cache_pages(struct address_space *mapping,
		struct list_head *pages, filler_t *filler, void *data);

static inline struct page *read_mapping_page(struct address_space *mapping,
				pgoff_t index, void *data)
{
	return read_cache_page(mapping, index, NULL, data);
}

/*
 * Get index of the page within radix-tree (but not for hugetlb pages).
 * (TODO: remove once hugetlb pages will have ->index in PAGE_SIZE)
 */
static inline pgoff_t page_to_index(struct page *page)
{
	pgoff_t pgoff;

	if (likely(!PageTransTail(page)))
		return page->index;

	/*
	 *  We don't initialize ->index for tail pages: calculate based on
	 *  head page
	 */
	pgoff = compound_head(page)->index;
	pgoff += page - compound_head(page);
	return pgoff;
}

extern pgoff_t hugetlb_basepage_index(struct page *page);

/*
 * Get the offset in PAGE_SIZE (even for hugetlb pages).
 * (TODO: hugetlb pages should have ->index in PAGE_SIZE)
 */
static inline pgoff_t page_to_pgoff(struct page *page)
{
	if (unlikely(PageHuge(page)))
		return hugetlb_basepage_index(page);
	return page_to_index(page);
}

#define swapcache_index(folio)	__page_file_index(&(folio)->page)
/**
 * folio_index - File index of a folio.
 * @folio: The folio.
 *
 * For a folio which is either in the page cache or the swap cache,
 * return its index within the address_space it belongs to.  If you know
 * the page is definitely in the page cache, you can look at the folio's
 * index directly.
 *
 * Return: The index (offset in units of pages) of a folio in its file.
 */
static inline pgoff_t folio_index(struct folio *folio)
{
	if (unlikely(folio_test_swapcache(folio)))
		return swapcache_index(folio);
	return folio->index;
}

/**
 * folio_file_page - The page for a particular index.
 * @folio: The folio which contains this index.
 * @index: The index we want to look up.
 *
 * Sometimes after looking up a folio in the page cache, we need to
 * obtain the specific page for an index (eg a page fault).
 *
 * Return: The page containing the file data for this index.
 */
static inline struct page *folio_file_page(struct folio *folio, pgoff_t index)
{
	/* HugeTLBfs indexes the page cache in units of hpage_size */
	if (folio_test_hugetlb(folio))
		return &folio->page;
	return folio_page(folio, index & (folio_nr_pages(folio) - 1));
}

/**
 * folio_contains - Does this folio contain this index?
 * @folio: The folio.
 * @index: The page index within the file.
 *
 * Context: The caller should have the page locked in order to prevent
 * (eg) shmem from moving the page between the page cache and swap cache
 * and changing its index in the middle of the operation.
 * Return: true or false.
 */
static inline bool folio_contains(struct folio *folio, pgoff_t index)
{
	/* HugeTLBfs indexes the page cache in units of hpage_size */
	if (folio_test_hugetlb(folio))
		return folio->index == index;
	return index - folio_index(folio) < folio_nr_pages(folio);
}

/*
 * Return byte-offset into filesystem object for page.
 */
static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_SHIFT;
}

static inline loff_t page_file_offset(struct page *page)
{
	return ((loff_t)page_index(page)) << PAGE_SHIFT;
}

/**
 * folio_pos - Returns the byte position of this folio in its file.
 * @folio: The folio.
 */
static inline loff_t folio_pos(struct folio *folio)
{
	return page_offset(&folio->page);
}

/**
 * folio_file_pos - Returns the byte position of this folio in its file.
 * @folio: The folio.
 *
 * This differs from folio_pos() for folios which belong to a swap file.
 * NFS is the only filesystem today which needs to use folio_file_pos().
 */
static inline loff_t folio_file_pos(struct folio *folio)
{
	return page_file_offset(&folio->page);
}

/**
 * folio_attach_private - Attach private data to a folio.
 * @folio: Folio to attach data to.
 * @data: Data to attach to folio.
 *
 * Attaching private data to a folio increments the page's reference count.
 * The data must be detached before the folio will be freed.
 */
static inline void folio_attach_private(struct folio *folio, void *data)
{
	folio_get(folio);
	folio->private = data;
	folio_set_private(folio);
}

/**
 * folio_detach_private - Detach private data from a folio.
 * @folio: Folio to detach data from.
 *
 * Removes the data that was previously attached to the folio and decrements
 * the refcount.
 *
 * Return: Data that was attached to the folio.
 */
static inline void *folio_detach_private(struct folio *folio)
{
	void *data = folio_get_private(folio);

	if (!folio_test_private(folio))
		return data;

	folio_clear_private(folio);
	folio->private = NULL;
	folio_put(folio);
	return data;
}

static inline void attach_page_private(struct page *page, void *data)
{
	folio_attach_private(page_folio(page), data);
}

static inline void *detach_page_private(struct page *page)
{
	return folio_detach_private(page_folio(page));
}

struct address_space *page_mapping(struct page *);
struct address_space *folio_mapping(struct folio *);
struct address_space *swapcache_mapping(struct folio *);
/**
 * folio_file_mapping - Find the mapping this folio belongs to.
 * @folio: The folio.
 *
 * For folios which are in the page cache, return the mapping that this
 * page belongs to.  Folios in the swap cache return the mapping of the
 * swap file or swap device where the data is stored.  This is different
 * from the mapping returned by folio_mapping().  The only reason to
 * use it is if, like NFS, you return 0 from ->activate_swapfile.
 *
 * Do not call this for folios which aren't in the page cache or swap cache.
 */
static inline struct address_space *folio_file_mapping(struct folio *folio)
{
	if (unlikely(folio_test_swapcache(folio)))
		return swapcache_mapping(folio);
	return folio->mapping;
}
static inline struct address_space *page_file_mapping(struct page *page)
{
	return folio_file_mapping(page_folio(page));
}

extern pgoff_t linear_hugepage_index(struct vm_area_struct *vma,
				     unsigned long address);

static inline pgoff_t linear_page_index(struct vm_area_struct *vma,
					unsigned long address)
{
	pgoff_t pgoff;
	if (unlikely(is_vm_hugetlb_page(vma)))
		return linear_hugepage_index(vma, address);
	pgoff = (address - READ_ONCE(vma->vm_start)) >> PAGE_SHIFT;
	pgoff += READ_ONCE(vma->vm_pgoff);
	return pgoff;
}

extern void __lock_page(struct page *page);
extern int __lock_page_killable(struct page *page);
void __folio_lock(struct folio *folio);
int __folio_lock_killable(struct folio *folio);
int __lock_page_or_retry(struct page *page, struct mm_struct *mm,
				unsigned int flags);
bool __folio_lock_or_retry(struct folio *folio, struct mm_struct *mm,
				unsigned int flags);
extern void unlock_page(struct page *page);

/*
 * Return true if the folio was successfully locked
 */
static inline bool folio_trylock(struct folio *folio)
{
	return likely(!test_and_set_bit_lock(PG_locked, folio_flags(folio, 0)));
}

/*
 * Return true if the page was successfully locked
 */
static inline int trylock_page(struct page *page)
{
	page = compound_head(page);
	return (likely(!test_and_set_bit_lock(PG_locked, &page->flags)));
}

/*
 * folio_lock may only be called if we have the folio's
 * inode pinned.
 */
static inline void folio_lock(struct folio *folio)
{
	might_sleep();
	if (!folio_trylock(folio))
		__folio_lock(folio);
}

/*
 * folio_lock_killable is like folio_lock, but can be interrupted by fatal
 * signals.  It returns 0 if it locked the folio and -EINTR if it was
 * killed while waiting.
 */
static inline int folio_lock_killable(struct folio *folio)
{
	might_sleep();
	if (!folio_trylock(folio))
		return __folio_lock_killable(folio);
	return 0;
}

/*
 * lock_page may only be called if we have the page's inode pinned.
 */
static inline void lock_page(struct page *page)
{
	might_sleep();
	if (!trylock_page(page))
		__lock_page(page);
}

/*
 * lock_page_killable is like lock_page but can be interrupted by fatal
 * signals.  It returns 0 if it locked the page and -EINTR if it was
 * killed while waiting.
 */
static inline int lock_page_killable(struct page *page)
{
	might_sleep();
	if (!trylock_page(page))
		return __lock_page_killable(page);
	return 0;
}

/*
 * Return value and mmap_sem implications depend on flags; see
 * __folio_lock_or_retry().
 */
static inline int lock_page_or_retry(struct page *page, struct mm_struct *mm,
				     unsigned int flags)
{
	might_sleep();
	return trylock_page(page) || __lock_page_or_retry(page, mm, flags);
}

/**
 * folio_lock_or_retry - Lock the folio, unless this would block.
 * @folio: The folio to lock.
 * @mm: The faulting task's memory map.
 * @flags: Fault method flags.
 *
 * Locking may only be performed if we have the folio's inode pinned.
 *
 * Returns true if the folio is locked; false if we were blocked and
 * the caller should retry.
 */
static inline bool folio_lock_or_retry(struct folio *folio,
		struct mm_struct *mm, unsigned int flags)
{
	might_sleep();
	return folio_trylock(folio) || __folio_lock_or_retry(folio, mm, flags);
}

/*
 * This is exported only for folio_wait_locked/folio_wait_writeback, etc.,
 * and should not be used directly.
 */
extern void wait_on_page_bit(struct page *page, int bit_nr);
extern int wait_on_page_bit_killable(struct page *page, int bit_nr);
void folio_wait_bit(struct folio *folio, int bit_nr);
int folio_wait_bit_killable(struct folio *folio, int bit_nr);

/**
 * folio_wait_locked - Wait for a folio to be unlocked.
 * @folio: The folio.
 *
 * If the folio is currently being written back to disk, you might
 * want to wait for writeback instead.
 */
static inline void folio_wait_locked(struct folio *folio)
{
	if (folio_test_locked(folio))
		folio_wait_bit(folio, PG_locked);
}

static inline int folio_wait_locked_killable(struct folio *folio)
{
	if (!folio_test_locked(folio))
		return 0;
	return folio_wait_bit_killable(folio, PG_locked);
}

/* 
 * Wait for a page to be unlocked.
 *
 * This must be called with the caller "holding" the page,
 * ie with increased "page->count" so that the page won't
 * go away during the wait..
 */
static inline void wait_on_page_locked(struct page *page)
{
	if (PageLocked(page))
		wait_on_page_bit(compound_head(page), PG_locked);
}

static inline int wait_on_page_locked_killable(struct page *page)
{
	if (!PageLocked(page))
		return 0;
	return wait_on_page_bit_killable(compound_head(page), PG_locked);
}

extern void put_and_wait_on_page_locked(struct page *page);

void wait_on_page_writeback(struct page *page);
void folio_wait_writeback(struct folio *folio);
int folio_wait_writeback_killable(struct folio *folio);
extern void end_page_writeback(struct page *page);
void folio_end_writeback(struct folio *folio);
void wait_for_stable_page(struct page *page);
void folio_wait_stable(struct folio *folio);

void page_endio(struct page *page, bool is_write, int err);

/*
 * Add an arbitrary waiter to a page's wait queue
 */
extern void add_page_wait_queue(struct page *page, wait_queue_entry_t *waiter);

/*
 * Fault everything in given userspace address range in.
 */
static inline int fault_in_pages_writeable(char __user *uaddr, int size)
{
	char __user *end = uaddr + size - 1;

	if (unlikely(size == 0))
		return 0;

	if (unlikely(uaddr > end))
		return -EFAULT;
	/*
	 * Writing zeroes into userspace here is OK, because we know that if
	 * the zero gets there, we'll be overwriting it.
	 */
	do {
		if (unlikely(__put_user(0, uaddr) != 0))
			return -EFAULT;
		uaddr += PAGE_SIZE;
	} while (uaddr <= end);

	/* Check whether the range spilled into the next page. */
	if (((unsigned long)uaddr & PAGE_MASK) ==
			((unsigned long)end & PAGE_MASK))
		return __put_user(0, end);

	return 0;
}

static inline int fault_in_pages_readable(const char __user *uaddr, int size)
{
	volatile char c;
	const char __user *end = uaddr + size - 1;

	if (unlikely(size == 0))
		return 0;

	if (unlikely(uaddr > end))
		return -EFAULT;

	do {
		if (unlikely(__get_user(c, uaddr) != 0))
			return -EFAULT;
		uaddr += PAGE_SIZE;
	} while (uaddr <= end);

	/* Check whether the range spilled into the next page. */
	if (((unsigned long)uaddr & PAGE_MASK) ==
			((unsigned long)end & PAGE_MASK)) {
		return __get_user(c, end);
	}

	(void)c;
	return 0;
}

int add_to_page_cache_locked(struct page *page, struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);
int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);
extern void delete_from_page_cache(struct page *page);
extern void __delete_from_page_cache(struct page *page, void *shadow);
int replace_page_cache_page(struct page *old, struct page *new, gfp_t gfp_mask);
void delete_from_page_cache_batch(struct address_space *mapping,
				  struct pagevec *pvec);

/*
 * Like add_to_page_cache_locked, but used to add newly allocated pages:
 * the page is new, so we can just run __SetPageLocked() against it.
 */
static inline int add_to_page_cache(struct page *page,
		struct address_space *mapping, pgoff_t offset, gfp_t gfp_mask)
{
	int error;

	__SetPageLocked(page);
	error = add_to_page_cache_locked(page, mapping, offset, gfp_mask);
	if (unlikely(error))
		__ClearPageLocked(page);
	return error;
}

static inline unsigned long dir_pages(struct inode *inode)
{
	return (unsigned long)(inode->i_size + PAGE_SIZE - 1) >>
			       PAGE_SHIFT;
}

#endif /* _LINUX_PAGEMAP_H */
