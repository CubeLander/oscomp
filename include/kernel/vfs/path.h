#ifndef _PATH_H
#define _PATH_H

#include "forward_declarations.h"
#include <kernel/types.h>

struct vfsmount;
struct dentry;
struct fcontext;
/**
 * File path representation
 */
struct path {
	struct dentry* dentry; /* Directory entry */
	struct vfsmount* mnt;  /* Mount information */
};

/* Path lookup and traversal */
int32 path_create(const char* name, uint32 flags, struct path* result);
// int32 kern_path_qstr(const struct qstr *name, uint32 flags, struct path *result);
void path_destroy(struct path* path);

int32 filename_lookup(int32 dfd, const char* name, uint32 flags, struct path* path, struct path* started);
struct vfsmount* path_lookupMount(struct path* path);
int32 resolve_path_parent(const char* path_str, struct path* out_parent);

int32 path_monkey(struct fcontext* fctx);




#endif