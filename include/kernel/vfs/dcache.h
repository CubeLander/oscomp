#include "forward_declarations.h"
#include <kernel/types.h>

extern struct hashtable dentry_hashtable;

struct dentry* dcache_lookup(struct dentry* parent, const struct qstr* name);

static inline int32 dcache_insert(struct dentry* dentry) { return hashtable_insert(&dentry_hashtable, &dentry->d_hashNode); }

int32 dcache_init(void);
