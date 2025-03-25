
#include <kernel/mm/page.h>
#include <kernel/types.h>
#include <util/radix_tree.h>
#include <util/spinlock.h>
#include <kernel/fs/vfs/vfs.h>

int __addrSpace_writeback(struct addrSpace *mapping, struct writeback_control *wbc);
static void __init_writeback_control(struct writeback_control *wbc, unsigned int sync_mode);
/**
 * addrSpace_create - Create a new address space for an inode
 * @inode: Inode to associate with the address space
 *
 * This function creates and initializes a new address space structure
 * for the given inode. The address space is used to manage the page cache
 * for the inode's file data.
 *
 * Returns: Pointer to the newly created address space, or NULL on failure
 */
struct addrSpace* addrSpace_create(struct inode* inode)
{
    struct addrSpace* mapping;
    
    // Allocate memory for the address space structure
    mapping = (struct addrSpace*)kmalloc(sizeof(struct addrSpace));
    if (!mapping)
        return NULL;
    
    // Initialize the address space structure
    mapping->nrpages = 0;
    radix_tree_init(&mapping->page_tree);
    spinlock_init(&mapping->tree_lock);
    
    // // Get appropriate address_space_ops based on inode type and filesystem
    // const struct addrSpace_ops* a_ops = NULL;
    
    // if (inode && inode->i_superblock) {
    //     struct superblock* sb = inode->i_superblock;
    //     struct fsType* fs_type = sb->s_fsType;
        
    //     // 假设每个文件系统类型提供了一个获取 address_space_ops 的函数
    //     // 实际实现中，这可能是文件系统特定的逻辑
    //     if (fs_type && fs_type->fs_get_aops) {
    //         a_ops = fs_type->fs_get_aops(inode);
    //     } else {
    //         // 如果文件系统没有提供获取函数，可以尝试使用通用操作
    //         // 或者根据文件类型选择默认操作
    //         if (S_ISREG(inode->i_mode)) {
    //             a_ops = &default_file_aops;
    //         } else if (S_ISDIR(inode->i_mode)) {
    //             a_ops = &default_dir_aops;
    //         } else {
    //             a_ops = &default_aops;
    //         }
    //     }
    // }
    
    //mapping->a_ops = a_ops;
    
    // Connect the address space to the inode
    if (inode) {
        inode->i_mapping = mapping;
    }
    
    return mapping;
}
/**
 * addrSpace_getPage - Find a page in the addrSpace
 * @mapping: The addrSpace to search
 * @index: The page index to find
 * 这个index不是全局的page index，是addrspace中页的index
 * Returns the page if found, NULL otherwise
 */
struct page* addrSpace_getPage(struct addrSpace* mapping, unsigned long index) {
	struct page* page = NULL;

	spinlock_lock(&mapping->tree_lock);
	page = radix_tree_lookup(&mapping->page_tree, index);
	if (page)
		get_page(page); /* Increment the reference count */
	spinlock_unlock(&mapping->tree_lock);

	return page;
}

/**
 * Add a page to the addrSpace
 * @mapping: The addrSpace
 * @page: The page to add
 * @index: The index at which to add the page
 *
 * Returns 0 on success, negative error code on failure
 */
int addrSpace_addPage(struct addrSpace* mapping, struct page* page, unsigned long index) {
	int ret;

	get_page(page); /* Increment ref count before adding */

	spinlock_lock(&mapping->tree_lock);
	ret = radix_tree_insert(&mapping->page_tree, index, page);
	if (ret == 0) {
		page->mapping = mapping;
		page->index = index;
		mapping->nrpages++;
		radix_tree_tag_set(&mapping->page_tree, index, RADIX_TREE_TAG_ACCESSED);
	} else {
		put_page(page); /* Decrement ref count on failure */
	}
	spinlock_unlock(&mapping->tree_lock);

	return ret;
}

/**
 * Remove a page from the addrSpace
 * @mapping: The addrSpace
 * @page: The page to remove
 *
 * Returns 1 if the page was found and removed, 0 otherwise
 */
int addrSpace_putPage(struct addrSpace* mapping, struct page* page) {
	int ret = 0;

	spinlock_lock(&mapping->tree_lock);
	if (radix_tree_delete(&mapping->page_tree, page->index) == page) {
		mapping->nrpages--;
		page->mapping = NULL;
		ret = 1;
	}
	spinlock_unlock(&mapping->tree_lock);

	if (ret)
		put_page(page); /* Decrement ref count after removing */

	return ret;
}

/**
 * Mark a page dirty in the addrSpace
 * @mapping: The addrSpace
 * @page: The page to mark dirty
 *
 * Returns 1 if the page was successfully marked dirty, 0 otherwise
 */
int addrSpace_setPageDirty(struct addrSpace* mapping, struct page* page) {
	if (!mapping || !page)
		return 0;

	spinlock_lock(&mapping->tree_lock);
	if (page->mapping == mapping) {
		set_page_dirty(page);
		radix_tree_tag_set(&mapping->page_tree, page->index, RADIX_TREE_TAG_DIRTY);
		spinlock_unlock(&mapping->tree_lock);
		return 1;
	}
	spinlock_unlock(&mapping->tree_lock);

	return 0;
}

/**
 * addrSpace_getDirtyPages - Find and get multiple dirty pages from the addrSpace
 * @mapping: The addrSpace to search
 * @pages: Array to store found pages
 * @nr_pages: Maximum number of pages to retrieve
 * @start: Starting index
 *
 * Returns the number of pages found
 */
unsigned int addrSpace_getDirtyPages(struct addrSpace* mapping, struct page** pages, unsigned int nr_pages, unsigned long start) {
	unsigned int found;
	unsigned int i;

	spinlock_lock(&mapping->tree_lock);
	found = radix_tree_gang_lookup_tag(&mapping->page_tree, (void**)pages, start, nr_pages, RADIX_TREE_TAG_DIRTY);

	/* Increment reference counts for the found pages */
	for (i = 0; i < found; i++)
		get_page(pages[i]);

	spinlock_unlock(&mapping->tree_lock);

	return found;
}

/**
 * addrSpace_removeDirtyTag - Clear the dirty tag from a page in the addrSpace
 * @mapping: The addrSpace
 * @page: The page to clear
 *
 * Returns 1 if successful, 0 otherwise
 */
int addrSpace_removeDirtyTag(struct addrSpace* mapping, struct page* page) {
	if (!mapping || !page)
		return 0;

	spinlock_lock(&mapping->tree_lock);
	if (page->mapping == mapping) {
		clear_page_dirty(page);
		radix_tree_tag_clear(&mapping->page_tree, page->index, RADIX_TREE_TAG_DIRTY);
		spinlock_unlock(&mapping->tree_lock);
		return 1;
	}
	spinlock_unlock(&mapping->tree_lock);

	return 0;
}

/**
 * Modified addrSpace_writeBack function to use the internal implementation
 * @mapping: The addrSpace to write back
 *
 * Returns 0 on success, negative error code on failure
 */
int addrSpace_writeBack(struct addrSpace *mapping) {
    struct writeback_control wbc;
    
    init_writeback_control(&wbc, WB_SYNC_ALL);
    wbc.reason = WB_REASON_SYNC;
    
    return __addrSpace_writeback(mapping, &wbc);
}


/**
 * Invalidate a single page in the addrSpace
 * @mapping: The addrSpace
 * @page: The page to invalidate
 *
 * Removes a page from the page cache if it's clean,
 * or fails if the page is dirty.
 *
 * Returns 0 on success, -EBUSY if the page is dirty
 */
int addrSpace_invalidate(struct addrSpace* mapping, struct page* page)
{
	int ret = 0;

	spinlock_lock(&mapping->tree_lock);

	/* Don't invalidate dirty pages */
	if (test_page_dirty(page)) {
		ret = -EBUSY;
	} else if (page->mapping == mapping) {
		/* Remove only if it's in this mapping */
		radix_tree_delete(&mapping->page_tree, page->index);
		mapping->nrpages--;
		page->mapping = NULL;
	}

	spinlock_unlock(&mapping->tree_lock);

	if (ret == 0)
		put_page(page); /* Decrement ref count after removing */

	return ret;
}

/**
 * Find or create a page at a specific index
 * @mapping: The addrSpace
 * @index: The index to find or create a page for
 * @gfp_mask: Allocation flags for new page
 *
 * Returns the found or created page, or NULL on failure
 */
struct page* addrSpace_acquirePage(struct addrSpace* mapping, unsigned long index, unsigned int gfp_mask) {
	struct page* page;

	/* First try to find the page */
	page = addrSpace_getPage(mapping, index);
	if (page)
		return page;

	/* Page not found, allocate a new one */
	page = alloc_page();
	if (!page)
		return NULL;

	/* Initialize the new page */
	page->mapping = NULL;
	page->flags = 0;
	atomic_set(&page->_refcount, 1);

	/* Try to add the page to the cache */
	if (addrSpace_addPage(mapping, page, index) < 0) {
		/* Failed to add, might already exist now */
		put_page(page);

		/* Try to find it again */
		page = addrSpace_getPage(mapping, index);
	}

	return page;
}

/**
 * Read a page into the addrSpace at the specified index
 * @mapping: The addrSpace
 * @index: The index to read
 *
 * Returns the read page, or NULL on failure
 */
struct page* addrSpace_readPage(struct addrSpace* mapping, unsigned long index) {
	struct page* page;
	int ret;

	/* Try to find the page in the cache first */
	page = addrSpace_getPage(mapping, index);
	if (page) {
		if (page_uptodate(page))
			return page;

		/* Page exists but isn't up to date - need to read it */
		if (mapping->a_ops && mapping->a_ops->readpage) {
			struct inode dummy_inode = {.i_mapping = mapping};
			struct file dummy_file = {.f_inode = &dummy_inode};

			lock_page(page);
			ret = mapping->a_ops->readpage(&dummy_file, page);
			unlock_page(page);

			if (ret == 0 && page_uptodate(page))
				return page;
		}

		/* Reading failed or no readpage operation */
		put_page(page);
		return NULL;
	}

	/* Page not in cache, create a new one */
	page = addrSpace_acquirePage(mapping, index, 0);
	if (!page)
		return NULL;

	/* Read the page data */
	if (mapping->a_ops && mapping->a_ops->readpage) {
		struct inode dummy_inode = {.i_mapping = mapping};
		struct file dummy_file = {.f_inode = &dummy_inode};

		lock_page(page);
		ret = mapping->a_ops->readpage(&dummy_file, page);
		unlock_page(page);

		if (ret == 0 && page_uptodate(page))
			return page;
	}

	/* Reading failed */
	put_page(page);
	return NULL;
}


/**
 * Internal function to perform page writeback with a specific control struct
 * @mapping: The addrSpace to write back
 * @wbc: Writeback control parameters
 *
 * Returns 0 on success, negative error code on failure
 */
int __addrSpace_writeback(struct addrSpace *mapping, struct writeback_control *wbc) {
    struct page *pages[16]; /* Process 16 pages in each batch */
    unsigned int nr_pages;
    unsigned long index = 0;
    int ret = 0;
    long nr_to_write = wbc->nr_to_write;

    if (!mapping || !mapping->a_ops || !mapping->a_ops->writepage)
        return -EINVAL;

    /* Process batches of dirty pages until no more are found or quota reached */
    do {
        unsigned int i;

        nr_pages = addrSpace_getDirtyPages(mapping, pages, 16, index);

        for (i = 0; i < nr_pages; i++) {
            struct page *page = pages[i];

            /* Update the next index to search from */
            if (page->index > index)
                index = page->index;

            /* Skip this page if it's no longer dirty */
            if (!test_page_dirty(page)) {
                put_page(page);
                continue;
            }
            
            /* Skip if outside the requested range */
            loff_t page_offset = (loff_t)page->index << PAGE_SHIFT;
            if (page_offset >= wbc->range_end || 
                (page_offset + PAGE_SIZE) <= wbc->range_start) {
                put_page(page);
                continue;
            }

            /* Lock the page for writeback */
            if (trylock_page(page)) {
                /* Write the page */
                ret = mapping->a_ops->writepage(page, wbc);

                /* Clear the dirty tag if successful */
                if (ret == 0)
                    addrSpace_removeDirtyTag(mapping, page);

                unlock_page(page);
            }

            put_page(page);

            if (ret < 0)
                break; /* Stop on error */
                
            /* Count this page against our quota */
            if (--nr_to_write <= 0)
                break;
        }

        index++; /* Move to the next index */
        
        /* Stop if we've reached our quota or encountered an error */
        if (nr_to_write <= 0 || ret < 0)
            break;
            
    } while (nr_pages > 0);

    /* Update how many pages we still need to write */
    wbc->nr_to_write = nr_to_write;
    
    return ret;
}


/**
 * Write back dirty pages in a specific range
 * @mapping: The addrSpace to write back
 * @start: Start offset
 * @end: End offset
 * @sync_mode: Whether to wait for I/O completion
 *
 * Returns 0 on success, negative error code on failure
 */
int addrSpace_writeback_range(struct addrSpace *mapping, loff_t start, loff_t end, int sync_mode) {
    struct writeback_control wbc;
    
    // Initialize writeback control
    init_writeback_control(&wbc, sync_mode);
    wbc.range_start = start;
    wbc.range_end = end;
    
    return __addrSpace_writeback(mapping, &wbc);
}

/**
 * Initialize a writeback_control structure with default values
 * @wbc: The writeback_control structure to initialize
 * @sync_mode: Synchronization mode (WB_SYNC_ALL or WB_SYNC_NONE)
 */
static void __init_writeback_control(struct writeback_control *wbc, unsigned int sync_mode) {
    memset(wbc, 0, sizeof(struct writeback_control));
    wbc->nr_to_write = INT32_MAX;  // Write as many pages as possible
    wbc->sync_mode = sync_mode;   // Set synchronization mode
    wbc->range_start = 0;         // Start from beginning
    wbc->range_end = INT64_MAX;   // To the end
}