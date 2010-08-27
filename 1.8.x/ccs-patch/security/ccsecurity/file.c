/*
 * security/ccsecurity/file.c
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 * Version: 1.8.0-pre   2010/08/01
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include "internal.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
#include <linux/mount.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
#include <linux/namespace.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
#include <linux/dcache.h>
#include <linux/namei.h>
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 33)
/*
 * ACC_MODE() in this file uses old definition because may_open() receives
 * open flags modified by open_to_namei_flags() until 2.6.33.
 * may_open() receives unmodified flags after 2.6.34.
 */
#undef ACC_MODE
#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])
#endif

const char *ccs_path_keyword[CCS_MAX_PATH_OPERATION] = {
	[CCS_TYPE_EXECUTE]    = "execute",
	[CCS_TYPE_READ]       = "read",
	[CCS_TYPE_WRITE]      = "write",
	[CCS_TYPE_APPEND]     = "append",
	[CCS_TYPE_UNLINK]     = "unlink",
	[CCS_TYPE_RMDIR]      = "rmdir",
	[CCS_TYPE_TRUNCATE]   = "truncate",
	[CCS_TYPE_SYMLINK]    = "symlink",
	[CCS_TYPE_CHROOT]     = "chroot",
	[CCS_TYPE_UMOUNT]     = "unmount",
	//[CCS_TYPE_TRANSIT]    = "transit",
};

const char *ccs_path2_keyword[CCS_MAX_PATH2_OPERATION] = {
	[CCS_TYPE_LINK]       = "link",
	[CCS_TYPE_RENAME]     = "rename",
	[CCS_TYPE_PIVOT_ROOT] = "pivot_root",
};

const char *ccs_path_number_keyword[CCS_MAX_PATH_NUMBER_OPERATION] = {
	[CCS_TYPE_CREATE] = "create",
	[CCS_TYPE_MKDIR]  = "mkdir",
	[CCS_TYPE_MKFIFO] = "mkfifo",
	[CCS_TYPE_MKSOCK] = "mksock",
	[CCS_TYPE_IOCTL]  = "ioctl",
	[CCS_TYPE_CHMOD]  = "chmod",
	[CCS_TYPE_CHOWN]  = "chown",
	[CCS_TYPE_CHGRP]  = "chgrp",
};

const char *ccs_mkdev_keyword[CCS_MAX_MKDEV_OPERATION] = {
	[CCS_TYPE_MKBLOCK]    = "mkblock",
	[CCS_TYPE_MKCHAR]     = "mkchar",
};

static const u8 ccs_p2mac[CCS_MAX_PATH_OPERATION] = {
	[CCS_TYPE_EXECUTE]    = CCS_MAC_FILE_EXECUTE,
	[CCS_TYPE_READ]       = CCS_MAC_FILE_OPEN,
	[CCS_TYPE_WRITE]      = CCS_MAC_FILE_OPEN,
	[CCS_TYPE_APPEND]     = CCS_MAC_FILE_OPEN,
	[CCS_TYPE_UNLINK]     = CCS_MAC_FILE_UNLINK,
	[CCS_TYPE_RMDIR]      = CCS_MAC_FILE_RMDIR,
	[CCS_TYPE_TRUNCATE]   = CCS_MAC_FILE_TRUNCATE,
	[CCS_TYPE_SYMLINK]    = CCS_MAC_FILE_SYMLINK,
	[CCS_TYPE_CHROOT]     = CCS_MAC_FILE_CHROOT,
	[CCS_TYPE_UMOUNT]     = CCS_MAC_FILE_UMOUNT,
	//[CCS_TYPE_TRANSIT]    = CCS_MAC_FILE_TRANSIT,
};

static const u8 ccs_pnnn2mac[CCS_MAX_MKDEV_OPERATION] = {
	[CCS_TYPE_MKBLOCK] = CCS_MAC_FILE_MKBLOCK,
	[CCS_TYPE_MKCHAR]  = CCS_MAC_FILE_MKCHAR,
};

static const u8 ccs_pp2mac[CCS_MAX_PATH2_OPERATION] = {
	[CCS_TYPE_LINK]       = CCS_MAC_FILE_LINK,
	[CCS_TYPE_RENAME]     = CCS_MAC_FILE_RENAME,
	[CCS_TYPE_PIVOT_ROOT] = CCS_MAC_FILE_PIVOT_ROOT,
};

static const u8 ccs_pn2mac[CCS_MAX_PATH_NUMBER_OPERATION] = {
	[CCS_TYPE_CREATE] = CCS_MAC_FILE_CREATE,
	[CCS_TYPE_MKDIR]  = CCS_MAC_FILE_MKDIR,
	[CCS_TYPE_MKFIFO] = CCS_MAC_FILE_MKFIFO,
	[CCS_TYPE_MKSOCK] = CCS_MAC_FILE_MKSOCK,
	[CCS_TYPE_IOCTL]  = CCS_MAC_FILE_IOCTL,
	[CCS_TYPE_CHMOD]  = CCS_MAC_FILE_CHMOD,
	[CCS_TYPE_CHOWN]  = CCS_MAC_FILE_CHOWN,
	[CCS_TYPE_CHGRP]  = CCS_MAC_FILE_CHGRP,
};

/* Main functions. */

void ccs_put_name_union(struct ccs_name_union *ptr)
{
	if (!ptr)
		return;
	if (ptr->is_group)
		ccs_put_group(ptr->group);
	else
		ccs_put_name(ptr->filename);
}

void ccs_put_number_union(struct ccs_number_union *ptr)
{
	if (ptr && ptr->is_group)
		ccs_put_group(ptr->group);
}

bool ccs_compare_number_union(const unsigned long value,
			      const struct ccs_number_union *ptr)
{
	if (ptr->is_group)
		return ccs_number_matches_group(value, value, ptr->group);
	return value >= ptr->values[0] && value <= ptr->values[1];
}

const struct ccs_path_info *
ccs_compare_name_union(const struct ccs_path_info *name,
		       const struct ccs_name_union *ptr)
{
	if (ptr->is_group)
		return ccs_path_matches_group(name, ptr->group);
	if (ccs_path_matches_pattern(name, ptr->filename))
		return ptr->filename;
	return NULL;
}

static void ccs_add_slash(struct ccs_path_info *buf)
{
	if (buf->is_dir)
		return;
	/* This is OK because ccs_encode() reserves space for appending "/". */
	strcat((char *) buf->name, "/");
	ccs_fill_path_info(buf);
}

/**
 * ccs_strendswith - Check whether the token ends with the given token.
 *
 * @name: The token to check.
 * @tail: The token to find.
 *
 * Returns true if @name ends with @tail, false otherwise.
 */
static bool ccs_strendswith(const char *name, const char *tail)
{
	int len;
	if (!name || !tail)
		return false;
	len = strlen(name) - strlen(tail);
	return len >= 0 && !strcmp(name + len, tail);
}

/**
 * ccs_get_realpath - Get realpath.
 *
 * @buf:    Pointer to "struct ccs_path_info".
 * @dentry: Pointer to "struct dentry".
 * @mnt:    Pointer to "struct vfsmount".
 *
 * Returns true success, false otherwise.
 */
static bool ccs_get_realpath(struct ccs_path_info *buf, struct dentry *dentry,
			     struct vfsmount *mnt)
{
	struct path path = { mnt, dentry };
	buf->name = ccs_realpath_from_path(&path);
	if (buf->name) {
		ccs_fill_path_info(buf);
		return true;
	}
	return false;
}

/**
 * ccs_audit_path_log - Audit path request log.
 *
 * @r: Pointer to "struct ccs_request_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_path_log(struct ccs_request_info *r)
{
	const char *operation = ccs_path_keyword[r->param.path.operation];
	const struct ccs_path_info *filename = r->param.path.filename;
	ccs_write_log(r, "file %s %s\n", operation, filename->name);
	if (r->granted)
		return 0;
	ccs_warn_log(r, "file %s %s", operation, filename->name);
	return ccs_supervisor(r, "file %s %s\n", operation,
			      ccs_file_pattern(filename));
}

/**
 * ccs_audit_path2_log - Audit path/path request log.
 *
 * @r: Pointer to "struct ccs_request_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_path2_log(struct ccs_request_info *r)
{
	const char *operation = ccs_path2_keyword[r->param.path2.operation];
	const struct ccs_path_info *filename1 = r->param.path2.filename1;
	const struct ccs_path_info *filename2 = r->param.path2.filename2;
	ccs_write_log(r, "file %s %s %s\n", operation, filename1->name,
		      filename2->name);
	if (r->granted)
		return 0;
	ccs_warn_log(r, "file %s %s %s", operation, filename1->name,
		     filename2->name);
	return ccs_supervisor(r, "file %s %s %s\n", operation,
			      ccs_file_pattern(filename1),
			      ccs_file_pattern(filename2));
}

/**
 * ccs_audit_mkdev_log - Audit path/number/number/number request log.
 *
 * @r: Pointer to "struct ccs_request_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_mkdev_log(struct ccs_request_info *r)
{
	const char *operation = ccs_mkdev_keyword[r->param.mkdev.operation];
	const struct ccs_path_info *filename = r->param.mkdev.filename;
	const unsigned int major = r->param.mkdev.major;
	const unsigned int minor = r->param.mkdev.minor;
	const unsigned int mode = r->param.mkdev.mode;
	ccs_write_log(r, "file %s %s 0%o %u %u\n", operation, filename->name,
		      mode, major, minor);
	if (r->granted)
		return 0;
	ccs_warn_log(r, "file %s %s 0%o %u %u", operation, filename->name, mode,
		     major, minor);
	return ccs_supervisor(r, "file %s %s 0%o %u %u\n", operation,
			      ccs_file_pattern(filename), mode, major, minor);
}

/**
 * ccs_audit_path_number_log - Audit path/number request log.
 *
 * @r:     Pointer to "struct ccs_request_info".
 * @error: Error code.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_path_number_log(struct ccs_request_info *r)
{
	const u8 type = r->param.path_number.operation;
	u8 radix;
	const struct ccs_path_info *filename = r->param.path_number.filename;
	const char *operation = ccs_path_number_keyword[type];
	char buffer[64];
	switch (type) {
	case CCS_TYPE_CREATE:
	case CCS_TYPE_MKDIR:
	case CCS_TYPE_MKFIFO:
	case CCS_TYPE_MKSOCK:
	case CCS_TYPE_CHMOD:
		radix = CCS_VALUE_TYPE_OCTAL;
		break;
	case CCS_TYPE_IOCTL:
		radix = CCS_VALUE_TYPE_HEXADECIMAL;
		break;
	default:
		radix = CCS_VALUE_TYPE_DECIMAL;
		break;
	}
	ccs_print_ulong(buffer, sizeof(buffer), r->param.path_number.number,
			radix);
	ccs_write_log(r, "file %s %s %s\n", operation, filename->name,
		      buffer);
	if (r->granted)
		return 0;
	ccs_warn_log(r, "file %s %s %s", operation, filename->name, buffer);
	return ccs_supervisor(r, "file %s %s %s\n", operation,
			      ccs_file_pattern(filename), buffer);
}

/**
 * ccs_file_pattern - Get patterned pathname.
 *
 * @filename: Pointer to "struct ccs_path_info".
 *
 * Returns pointer to patterned pathname.
 *
 * Caller holds ccs_read_lock().
 */
const char *ccs_file_pattern(const struct ccs_path_info *filename)
{
	struct ccs_pattern *ptr;
	const struct ccs_path_info *pattern = NULL;
	list_for_each_entry_rcu(ptr, &ccs_policy_list[CCS_ID_PATTERN],
				head.list) {
		if (ptr->head.is_deleted)
			continue;
		if (!ccs_path_matches_pattern(filename, ptr->pattern))
			continue;
		pattern = ptr->pattern;
		if (ccs_strendswith(pattern->name, "/\\*")) {
			/* Do nothing. Try to find the better match. */
		} else {
			/* This would be the better match. Use this. */
			break;
		}
	}
	return pattern ? pattern->name : filename->name;
}

static bool ccs_same_pattern(const struct ccs_acl_head *a,
			     const struct ccs_acl_head *b)
{
	return container_of(a, struct ccs_pattern, head)->pattern ==
		container_of(b, struct ccs_pattern, head)->pattern;
}

/**
 * ccs_write_pattern - Write "struct ccs_pattern" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_write_pattern(char *data, const bool is_delete)
{
	struct ccs_pattern e = { };
	int error;
	if (!ccs_correct_word(data))
		return -EINVAL;
	e.pattern = ccs_get_name(data);
	if (!e.pattern)
		return -ENOMEM;
	error = ccs_update_policy(&e.head, sizeof(e), is_delete,
				  &ccs_policy_list[CCS_ID_PATTERN],
				  ccs_same_pattern);
	ccs_put_name(e.pattern);
	return error;
}

static bool ccs_check_path_acl(struct ccs_request_info *r,
			       const struct ccs_acl_info *ptr)
{
	const struct ccs_path_acl *acl = container_of(ptr, typeof(*acl), head);
	if (acl->perm & (1 << r->param.path.operation)) {
		r->param.path.matched_path =
			ccs_compare_name_union(r->param.path.filename,
					       &acl->name);
		return r->param.path.matched_path != NULL;
	}
	return false;
}

static bool ccs_check_path_number_acl(struct ccs_request_info *r,
				      const struct ccs_acl_info *ptr)
{
	const struct ccs_path_number_acl *acl =
		container_of(ptr, typeof(*acl), head);
	return (acl->perm & (1 << r->param.path_number.operation)) &&
		ccs_compare_number_union(r->param.path_number.number,
					 &acl->number) &&
		ccs_compare_name_union(r->param.path_number.filename,
				       &acl->name);
}

static bool ccs_check_path2_acl(struct ccs_request_info *r,
				const struct ccs_acl_info *ptr)
{
	const struct ccs_path2_acl *acl =
		container_of(ptr, typeof(*acl), head);
	return (acl->perm & (1 << r->param.path2.operation)) &&
		ccs_compare_name_union(r->param.path2.filename1, &acl->name1)
		&& ccs_compare_name_union(r->param.path2.filename2,
					  &acl->name2);
}

static bool ccs_check_mkdev_acl(struct ccs_request_info *r,
				const struct ccs_acl_info *ptr)
{
	const struct ccs_mkdev_acl *acl =
		container_of(ptr, typeof(*acl), head);
	return (acl->perm & (1 << r->param.mkdev.operation)) &&
		ccs_compare_number_union(r->param.mkdev.mode, &acl->mode) &&
		ccs_compare_number_union(r->param.mkdev.major, &acl->major) &&
		ccs_compare_number_union(r->param.mkdev.minor, &acl->minor) &&
		ccs_compare_name_union(r->param.mkdev.filename, &acl->name);
}

static bool ccs_same_path_acl(const struct ccs_acl_info *a,
			      const struct ccs_acl_info *b)
{
	const struct ccs_path_acl *p1 = container_of(a, typeof(*p1), head);
	const struct ccs_path_acl *p2 = container_of(b, typeof(*p2), head);
	return ccs_same_acl_head(&p1->head, &p2->head) &&
		ccs_same_name_union(&p1->name, &p2->name);
}

static bool ccs_merge_path_acl(struct ccs_acl_info *a, struct ccs_acl_info *b,
			       const bool is_delete)
{
	u16 * const a_perm = &container_of(a, struct ccs_path_acl, head)->perm;
	u16 perm = *a_perm;
	const u16 b_perm = container_of(b, struct ccs_path_acl, head)->perm;
	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
}

/**
 * ccs_update_path_acl - Update "struct ccs_path_acl" list.
 *
 * @perm:      Permission.
 * @filename:  Filename.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". Maybe NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static int ccs_update_path_acl(const u16 perm, const char *filename,
			       struct ccs_domain_info * const domain,
			       struct ccs_condition *condition,
			       const bool is_delete)
{
	struct ccs_path_acl e = {
		.head.type = CCS_TYPE_PATH_ACL,
		.head.cond = condition,
		.perm = perm
	};
	int error;
	if (!ccs_parse_name_union(filename, &e.name))
		return -EINVAL;
	error = ccs_update_domain(&e.head, sizeof(e), is_delete, domain,
				  ccs_same_path_acl, ccs_merge_path_acl);
	ccs_put_name_union(&e.name);
	return error;
}

static bool ccs_same_mkdev_acl(const struct ccs_acl_info *a,
			       const struct ccs_acl_info *b)
{
	const struct ccs_mkdev_acl *p1 = container_of(a, typeof(*p1), head);
	const struct ccs_mkdev_acl *p2 = container_of(b, typeof(*p2), head);
	return ccs_same_acl_head(&p1->head, &p2->head)
		&& ccs_same_name_union(&p1->name, &p2->name)
		&& ccs_same_number_union(&p1->mode, &p2->mode)
		&& ccs_same_number_union(&p1->major, &p2->major)
		&& ccs_same_number_union(&p1->minor, &p2->minor);
}

static bool ccs_merge_mkdev_acl(struct ccs_acl_info *a, struct ccs_acl_info *b,
				const bool is_delete)
{
	u8 *const a_perm = &container_of(a, struct ccs_mkdev_acl, head)->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct ccs_mkdev_acl, head)->perm;
	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
}

/**
 * ccs_update_mkdev_acl - Update "struct ccs_mkdev_acl" list.
 *
 * @perm:      Permission.
 * @filename:  Filename.
 * @mode:      Create mode.
 * @major:     Device major number.
 * @minor:     Device minor number.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". Maybe NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static int ccs_update_mkdev_acl(const u8 perm, const char *filename,
				char *mode, char *major, char *minor,
				struct ccs_domain_info * const domain,
				struct ccs_condition *condition,
				const bool is_delete)
{
	struct ccs_mkdev_acl e = {
		.head.type = CCS_TYPE_MKDEV_ACL,
		.head.cond = condition,
		.perm = perm
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!ccs_parse_name_union(filename, &e.name) ||
	    !ccs_parse_number_union(mode, &e.mode) ||
	    !ccs_parse_number_union(major, &e.major) ||
	    !ccs_parse_number_union(minor, &e.minor))
		goto out;
	error = ccs_update_domain(&e.head, sizeof(e), is_delete, domain,
				  ccs_same_mkdev_acl,
				  ccs_merge_mkdev_acl);
 out:
	ccs_put_name_union(&e.name);
	ccs_put_number_union(&e.mode);
	ccs_put_number_union(&e.major);
	ccs_put_number_union(&e.minor);
	return error;
}

static bool ccs_same_path2_acl(const struct ccs_acl_info *a,
			       const struct ccs_acl_info *b)
{
	const struct ccs_path2_acl *p1 = container_of(a, typeof(*p1), head);
	const struct ccs_path2_acl *p2 = container_of(b, typeof(*p2), head);
	return ccs_same_acl_head(&p1->head, &p2->head)
		&& ccs_same_name_union(&p1->name1, &p2->name1)
		&& ccs_same_name_union(&p1->name2, &p2->name2);
}

static bool ccs_merge_path2_acl(struct ccs_acl_info *a, struct ccs_acl_info *b,
				const bool is_delete)
{
	u8 * const a_perm = &container_of(a, struct ccs_path2_acl, head)->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct ccs_path2_acl, head)->perm;
	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
}

/**
 * ccs_update_path2_acl - Update "struct ccs_path2_acl" list.
 *
 * @perm:      Permission.
 * @filename1: First filename.
 * @filename2: Second filename.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". Maybe NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static int ccs_update_path2_acl(const u8 perm, const char *filename1,
				const char *filename2,
				struct ccs_domain_info * const domain,
				struct ccs_condition *condition,
				const bool is_delete)
{
	struct ccs_path2_acl e = {
		.head.type = CCS_TYPE_PATH2_ACL,
		.head.cond = condition,
		.perm = perm
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!ccs_parse_name_union(filename1, &e.name1) ||
	    !ccs_parse_name_union(filename2, &e.name2))
		goto out;
	error = ccs_update_domain(&e.head, sizeof(e), is_delete, domain,
				  ccs_same_path2_acl, ccs_merge_path2_acl);
 out:
	ccs_put_name_union(&e.name1);
	ccs_put_name_union(&e.name2);
	return error;
}

static bool ccs_same_mount_acl(const struct ccs_acl_info *a,
			       const struct ccs_acl_info *b)
{
	const struct ccs_mount_acl *p1 = container_of(a, typeof(*p1), head);
	const struct ccs_mount_acl *p2 = container_of(b, typeof(*p2), head);
	return ccs_same_acl_head(&p1->head, &p2->head) &&
		ccs_same_name_union(&p1->dev_name, &p2->dev_name) &&
		ccs_same_name_union(&p1->dir_name, &p2->dir_name) &&
		ccs_same_name_union(&p1->fs_type, &p2->fs_type) &&
		ccs_same_number_union(&p1->flags, &p2->flags);
}

/**
 * ccs_update_mount_acl - Write "struct ccs_mount_acl" list.
 *
 * @dev:       Device name.
 * @dir:       Mount point.
 * @fs:        Filesystem type.
 * @flags:     Mount flags.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". Maybe NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static int ccs_update_mount_acl(char *dev, char *dir, char *fs, char *flags,
				struct ccs_domain_info *domain,
				struct ccs_condition *condition,
				const bool is_delete)
{
	struct ccs_mount_acl e = { .head.type = CCS_TYPE_MOUNT_ACL,
				   .head.cond = condition };
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!ccs_parse_name_union(dev, &e.dev_name) ||
	    !ccs_parse_name_union(dir, &e.dir_name) ||
	    !ccs_parse_name_union(fs, &e.fs_type) ||
	    !ccs_parse_number_union(flags, &e.flags))
		goto out;
	error = ccs_update_domain(&e.head, sizeof(e), is_delete, domain,
				  ccs_same_mount_acl, NULL);
 out:
	ccs_put_name_union(&e.dev_name);
	ccs_put_name_union(&e.dir_name);
	ccs_put_name_union(&e.fs_type);
	ccs_put_number_union(&e.flags);
	return error;
}

/**
 * ccs_path_permission - Check permission for path operation.
 *
 * @r:         Pointer to "struct ccs_request_info".
 * @operation: Type of operation.
 * @filename:  Filename to check.
 *
 * Returns 0 on success, CCS_RETRY_REQUEST on retry, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
int ccs_path_permission(struct ccs_request_info *r, u8 operation,
			const struct ccs_path_info *filename)
{
	int error;
	r->type = ccs_p2mac[operation];
	r->mode = ccs_get_mode(r->profile, r->type);
	if (r->mode == CCS_CONFIG_DISABLED)
		return 0;
	r->param_type = CCS_TYPE_PATH_ACL;
	r->param.path.filename = filename;
	r->param.path.operation = operation;
	do {
		ccs_check_acl(r, ccs_check_path_acl);
		error = ccs_audit_path_log(r);
		/*
		 * Do not retry for execute request, for aggregator may have
		 * changed.
		 */
	} while (error == CCS_RETRY_REQUEST && operation != CCS_TYPE_EXECUTE);
	return error;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
/*
 * Save original flags passed to sys_open().
 *
 * TOMOYO does not check "file write" if open(path, O_TRUNC | O_RDONLY) was
 * requested because write() is not permitted. Instead, TOMOYO checks
 * "file truncate" if O_TRUNC is passed.
 *
 * TOMOYO does not check "file read" and "file write" if open(path, 3) was
 * requested because read()/write() are not permitted. Instead, TOMOYO checks
 * "file ioctl" when ioctl() is requested.
 */
static void __ccs_save_open_mode(int mode)
{
	if ((mode & 3) == 3)
		current->ccs_flags |= CCS_OPEN_FOR_IOCTL_ONLY;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 14)
	/* O_TRUNC passes MAY_WRITE to ccs_open_permission(). */
	else if (!(mode & 3) && (mode & O_TRUNC))
		current->ccs_flags |= CCS_OPEN_FOR_READ_TRUNCATE;
#endif
}

static void __ccs_clear_open_mode(void)
{
	current->ccs_flags &= ~(CCS_OPEN_FOR_IOCTL_ONLY |
				CCS_OPEN_FOR_READ_TRUNCATE);
}
#endif

/**
 * ccs_open_permission - Check permission for "read" and "write".
 *
 * @dentry: Pointer to "struct dentry".
 * @mnt:    Pointer to "struct vfsmount".
 * @flag:   Flags for open().
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_open_permission(struct dentry *dentry, struct vfsmount *mnt,
				 const int flag)
{
	struct ccs_request_info r;
	struct ccs_obj_info obj = {
		.path1.dentry = dentry,
		.path1.mnt = mnt
	};
	struct task_struct * const task = current;
	const u32 ccs_flags = task->ccs_flags;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
	const u8 acc_mode = (flag & 3) == 3 ? 0 : ACC_MODE(flag);
#else
	const u8 acc_mode = (ccs_flags & CCS_OPEN_FOR_IOCTL_ONLY) ? 0 :
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 14)
		(ccs_flags & CCS_OPEN_FOR_READ_TRUNCATE) ? 4 :
#endif
		ACC_MODE(flag);
#endif
	int error = 0;
	struct ccs_path_info buf;
	int idx;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30)
	if (task->in_execve && !(ccs_flags & CCS_TASK_IS_IN_EXECVE))
		return 0;
#endif
	if (!mnt || (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)))
		return 0;
	buf.name = NULL;
	r.mode = CCS_CONFIG_DISABLED;
	idx = ccs_read_lock();
	if (acc_mode && ccs_init_request_info(&r, CCS_MAC_FILE_OPEN)
	    != CCS_CONFIG_DISABLED) {
		if (!buf.name && !ccs_get_realpath(&buf, dentry, mnt)) {
			error = -ENOMEM;
			goto out;
		}
		r.obj = &obj;
		if (acc_mode & MAY_READ)
			error = ccs_path_permission(&r, CCS_TYPE_READ, &buf);
		if (!error && (acc_mode & MAY_WRITE))
			error = ccs_path_permission(&r, (flag & O_APPEND) ?
						    CCS_TYPE_APPEND :
						    CCS_TYPE_WRITE, &buf);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)
	if (!error && (flag & O_TRUNC) &&
	    ccs_init_request_info(&r, CCS_MAC_FILE_TRUNCATE)
	    != CCS_CONFIG_DISABLED) {
		if (!buf.name && !ccs_get_realpath(&buf, dentry, mnt)) {
			error = -ENOMEM;
			goto out;
		}
		r.obj = &obj;
		error = ccs_path_permission(&r, CCS_TYPE_TRUNCATE, &buf);
	}
#endif
 out:
	kfree(buf.name);
	ccs_read_unlock(idx);
	if (r.mode != CCS_CONFIG_ENFORCING)
		error = 0;
	return error;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
static int ccs_new_open_permission(struct file *filp)
{
	return __ccs_open_permission(filp->f_path.dentry, filp->f_path.mnt,
				     filp->f_flags);
}
#endif

/**
 * ccs_path_perm - Check permission for "unlink", "rmdir", "truncate", "symlink", "chroot" and "unmount".
 *
 * @operation: Type of operation.
 * @dir:       Pointer to "struct inode". Maybe NULL.
 * @dentry:    Pointer to "struct dentry".
 * @mnt:       Pointer to "struct vfsmount".
 * @target:    Symlink's target if @operation is CCS_TYPE_SYMLINK.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_path_perm(const u8 operation, struct inode *dir,
			 struct dentry *dentry, struct vfsmount *mnt,
			 const char *target)
{
	struct ccs_request_info r;
	struct ccs_obj_info obj = {
		.path1.dentry = dentry,
		.path1.mnt = mnt
	};
	int error = 0;
	struct ccs_path_info buf;
	bool is_enforce = false;
	struct ccs_path_info symlink_target;
	int idx;
	if (!mnt)
		return 0;
	buf.name = NULL;
	symlink_target.name = NULL;
	idx = ccs_read_lock();
	if (ccs_init_request_info(&r, ccs_p2mac[operation])
	    == CCS_CONFIG_DISABLED)
		goto out;
	is_enforce = (r.mode == CCS_CONFIG_ENFORCING);
	error = -ENOMEM;
	if (!ccs_get_realpath(&buf, dentry, mnt))
		goto out;
	r.obj = &obj;
	switch (operation) {
	case CCS_TYPE_RMDIR:
	case CCS_TYPE_CHROOT:
	case CCS_TYPE_UMOUNT:
		ccs_add_slash(&buf);
		break;
	case CCS_TYPE_SYMLINK:
		symlink_target.name = ccs_encode(target);
		if (!symlink_target.name)
			goto out;
		ccs_fill_path_info(&symlink_target);
		obj.symlink_target = &symlink_target;
		break;
	}
	error = ccs_path_permission(&r, operation, &buf);
	if (operation == CCS_TYPE_SYMLINK)
		kfree(symlink_target.name);
 out:
	kfree(buf.name);
	ccs_read_unlock(idx);
	if (!is_enforce)
		error = 0;
	return error;
}

/**
 * ccs_mkdev_perm - Check permission for "mkblock" and "mkchar".
 *
 * @operation: Type of operation. (CCS_TYPE_MKCHAR or CCS_TYPE_MKBLOCK)
 * @dir:       Pointer to "struct inode".
 * @dentry:    Pointer to "struct dentry".
 * @mnt:       Pointer to "struct vfsmount".
 * @mode:      Create mode.
 * @dev:       Device number.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_mkdev_perm(const u8 operation, struct inode *dir,
			  struct dentry *dentry, struct vfsmount *mnt,
			  const unsigned int mode, unsigned int dev)
{
	struct ccs_request_info r;
	struct ccs_obj_info obj = {
		.path1.dentry = dentry,
		.path1.mnt = mnt
	};
	int error = 0;
	struct ccs_path_info buf;
	bool is_enforce = false;
	int idx;
	if (!mnt)
		return 0;
	idx = ccs_read_lock();
	if (ccs_init_request_info(&r, ccs_pnnn2mac[operation])
	    == CCS_CONFIG_DISABLED)
		goto out;
	is_enforce = (r.mode == CCS_CONFIG_ENFORCING);
	error = -EPERM;
	if (!capable(CAP_MKNOD))
		goto out;
	error = -ENOMEM;
	if (!ccs_get_realpath(&buf, dentry, mnt))
		goto out;
	r.obj = &obj;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
	dev = new_decode_dev(dev);
#endif
	r.param_type = CCS_TYPE_MKDEV_ACL;
	r.param.mkdev.filename = &buf;
	r.param.mkdev.operation = operation;
	r.param.mkdev.mode = mode;
	r.param.mkdev.major = MAJOR(dev);
	r.param.mkdev.minor = MINOR(dev);
	do {
		ccs_check_acl(&r, ccs_check_mkdev_acl);
		error = ccs_audit_mkdev_log(&r);
	} while (error == CCS_RETRY_REQUEST);
	kfree(buf.name);
 out:
	ccs_read_unlock(idx);
	if (!is_enforce)
		error = 0;
	return error;
}

/**
 * ccs_path2_perm - Check permission for "rename", "link" and "pivot_root".
 *
 * @operation: Type of operation.
 * @dir1:      Pointer to "struct inode". Maybe NULL.
 * @dentry1:   Pointer to "struct dentry".
 * @mnt1:      Pointer to "struct vfsmount".
 * @dir2:      Pointer to "struct inode". Maybe NULL.
 * @dentry2:   Pointer to "struct dentry".
 * @mnt2:      Pointer to "struct vfsmount".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_path2_perm(const u8 operation, struct inode *dir1,
			  struct dentry *dentry1, struct vfsmount *mnt1,
			  struct inode *dir2, struct dentry *dentry2,
			  struct vfsmount *mnt2)
{
	struct ccs_request_info r;
	int error = 0;
	struct ccs_path_info buf1;
	struct ccs_path_info buf2;
	bool is_enforce = false;
	struct ccs_obj_info obj = {
		.path1.dentry = dentry1,
		.path1.mnt = mnt1,
		.path2.dentry = dentry2,
		.path2.mnt = mnt2
	};
	int idx;
	if (!mnt1 || !mnt2)
		return 0;
	buf1.name = NULL;
	buf2.name = NULL;
	idx = ccs_read_lock();
	if (ccs_init_request_info(&r, ccs_pp2mac[operation])
	    == CCS_CONFIG_DISABLED)
		goto out;
	is_enforce = (r.mode == CCS_CONFIG_ENFORCING);
	error = -ENOMEM;
	if (!ccs_get_realpath(&buf1, dentry1, mnt1) ||
	    !ccs_get_realpath(&buf2, dentry2, mnt2))
		goto out;
	switch (operation) {
	case CCS_TYPE_RENAME:
	case CCS_TYPE_LINK:
		if (!dentry1->d_inode || !S_ISDIR(dentry1->d_inode->i_mode))
			break;
		/* fall through */
	case CCS_TYPE_PIVOT_ROOT:
		ccs_add_slash(&buf1);
		ccs_add_slash(&buf2);
		break;
	}
	r.obj = &obj;
	r.param_type = CCS_TYPE_PATH2_ACL;
	r.param.path2.operation = operation;
	r.param.path2.filename1 = &buf1;
	r.param.path2.filename2 = &buf2;
	do {
		ccs_check_acl(&r, ccs_check_path2_acl);
		error = ccs_audit_path2_log(&r);
	} while (error == CCS_RETRY_REQUEST);
 out:
	kfree(buf1.name);
	kfree(buf2.name);
	ccs_read_unlock(idx);
	if (!is_enforce)
		error = 0;
	return error;
}

static bool ccs_same_path_number_acl(const struct ccs_acl_info *a,
				     const struct ccs_acl_info *b)
{
	const struct ccs_path_number_acl *p1 = container_of(a, typeof(*p1),
							    head);
	const struct ccs_path_number_acl *p2 = container_of(b, typeof(*p2),
							    head);
	return ccs_same_acl_head(&p1->head, &p2->head)
		&& ccs_same_name_union(&p1->name, &p2->name)
		&& ccs_same_number_union(&p1->number, &p2->number);
}

static bool ccs_merge_path_number_acl(struct ccs_acl_info *a,
				      struct ccs_acl_info *b,
				      const bool is_delete)
{
	u8 * const a_perm = &container_of(a, struct ccs_path_number_acl, head)
		->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct ccs_path_number_acl, head)
		->perm;
	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
}

/**
 * ccs_update_path_number_acl - Update ioctl/chmod/chown/chgrp ACL.
 *
 * @perm:      Permission.
 * @filename:  Filename.
 * @number:    Number.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". Maybe NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_update_path_number_acl(const u8 perm, const char *filename,
				      char *number,
				      struct ccs_domain_info * const domain,
				      struct ccs_condition *condition,
				      const bool is_delete)
{
	struct ccs_path_number_acl e = {
		.head.type = CCS_TYPE_PATH_NUMBER_ACL,
		.head.cond = condition,
		.perm = perm
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!ccs_parse_name_union(filename, &e.name))
		return -EINVAL;
	if (!ccs_parse_number_union(number, &e.number))
		goto out;
	error = ccs_update_domain(&e.head, sizeof(e), is_delete, domain,
				  ccs_same_path_number_acl,
				  ccs_merge_path_number_acl);
 out:
	ccs_put_name_union(&e.name);
	ccs_put_number_union(&e.number);
	return error;
}

/**
 * ccs_path_number_perm - Check permission for "create", "mkdir", "mkfifo", "mksock", "ioctl", "chmod", "chown", "chgrp".
 *
 * @type:   Type of operation.
 * @dir:    Pointer to "struct inode". Maybe NULL.
 * @dentry: Pointer to "struct dentry".
 * @vfsmnt: Pointer to "struct vfsmount".
 * @number: Number.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_path_number_perm(const u8 type, struct inode *dir,
				struct dentry *dentry, struct vfsmount *vfsmnt,
				unsigned long number)
{
	struct ccs_request_info r;
	struct ccs_obj_info obj = {
		.path1.dentry = dentry,
		.path1.mnt = vfsmnt
	};
	int error = 0;
	struct ccs_path_info buf;
	int idx;
	if (!vfsmnt || !dentry)
		return 0;
	idx = ccs_read_lock();
	if (ccs_init_request_info(&r, ccs_pn2mac[type]) == CCS_CONFIG_DISABLED)
		goto out;
	error = -ENOMEM;
	if (!ccs_get_realpath(&buf, dentry, vfsmnt))
		goto out;
	r.obj = &obj;
	if (type == CCS_TYPE_MKDIR)
		ccs_add_slash(&buf);
	r.param_type = CCS_TYPE_PATH_NUMBER_ACL;
	r.param.path_number.operation = type;
	r.param.path_number.filename = &buf;
	r.param.path_number.number = number;
	do {
		ccs_check_acl(&r, ccs_check_path_number_acl);
		error = ccs_audit_path_number_log(&r);
	} while (error == CCS_RETRY_REQUEST);
	kfree(buf.name);
 out:
	ccs_read_unlock(idx);
	if (r.mode != CCS_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * ccs_ioctl_permission - Check permission for "ioctl".
 *
 * @file: Pointer to "struct file".
 * @cmd:  Ioctl command number.
 * @arg:  Param for @cmd .
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_ioctl_permission(struct file *filp, unsigned int cmd,
				  unsigned long arg)
{
	return ccs_path_number_perm(CCS_TYPE_IOCTL, NULL, filp->f_dentry,
				    filp->f_vfsmnt, cmd);
}

/**
 * ccs_chmod_permission - Check permission for "chmod".
 *
 * @dentry: Pointer to "struct dentry".
 * @vfsmnt: Pointer to "struct vfsmount".
 * @mode:   Mode.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_chmod_permission(struct dentry *dentry,
				  struct vfsmount *vfsmnt, mode_t mode)
{
	if (mode == (mode_t) -1)
		return 0;
	return ccs_path_number_perm(CCS_TYPE_CHMOD, NULL, dentry, vfsmnt,
				    mode & S_IALLUGO);
}

/**
 * ccs_chown_permission - Check permission for "chown/chgrp".
 *
 * @dentry: Pointer to "struct dentry".
 * @vfsmnt: Pointer to "struct vfsmount".
 * @user:   User ID.
 * @group:  Group ID.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_chown_permission(struct dentry *dentry,
				  struct vfsmount *vfsmnt, uid_t user,
				  gid_t group)
{
	int error = 0;
	if (user == (uid_t) -1 && group == (gid_t) -1)
		return 0;
	if (user != (uid_t) -1)
		error = ccs_path_number_perm(CCS_TYPE_CHOWN, NULL, dentry,
					     vfsmnt, user);
	if (!error && group != (gid_t) -1)
		error = ccs_path_number_perm(CCS_TYPE_CHGRP, NULL, dentry,
					     vfsmnt, group);
	return error;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
static int __ccs_fcntl_permission(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	if (cmd == F_SETFL && ((arg ^ file->f_flags) & O_APPEND))
		/* 00 means "write". */
		return __ccs_open_permission(file->f_dentry, file->f_vfsmnt, 00);
	return 0;
}
#else
static int __ccs_rewrite_permission(struct file *filp)
{
	/* 00 means "write". */
	return __ccs_open_permission(filp->f_dentry, filp->f_vfsmnt, 00);
}
#endif

/**
 * ccs_pivot_root_permission - Check permission for pivot_root().
 *
 * @old_path: Pointer to "struct path".
 * @new_path: Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_pivot_root_permission(struct path *old_path,
				       struct path *new_path)
{
	return ccs_path2_perm(CCS_TYPE_PIVOT_ROOT, NULL, new_path->dentry,
			      new_path->mnt, NULL, old_path->dentry,
			      old_path->mnt);
}

/**
 * ccs_chroot_permission - Check permission for chroot().
 *
 * @path: Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_chroot_permission(struct path *path)
{
	return ccs_path_perm(CCS_TYPE_CHROOT, NULL, path->dentry, path->mnt,
			     NULL);
}

/**
 * ccs_umount_permission - Check permission for unmount.
 *
 * @mnt:   Pointer to "struct vfsmount".
 * @flags: Umount flags.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_umount_permission(struct vfsmount *mnt, int flags)
{
	return ccs_path_perm(CCS_TYPE_UMOUNT, NULL, mnt->mnt_root, mnt, NULL);
}

/**
 * ccs_write_file - Update file related list.
 *
 * @data:      String to parse.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". Maybe NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_write_file(char *data, struct ccs_domain_info *domain,
		   struct ccs_condition *condition, const bool is_delete)
{
	char *w[5];
	u16 perm = 0;
	u8 type;
	if (!ccs_tokenize(data, w, sizeof(w)) || !w[1][0])
		return -EINVAL;
	for (type = 0; type < CCS_MAX_PATH_OPERATION; type++)
		if (ccs_permstr(w[0], ccs_path_keyword[type]))
			perm |= 1 << type;
	if (perm)
		return ccs_update_path_acl(perm, w[1], domain, condition,
					   is_delete);
	if (!w[2][0])
		goto out;
	for (type = 0; type < CCS_MAX_PATH2_OPERATION; type++)
		if (ccs_permstr(w[0], ccs_path2_keyword[type]))
			perm |= 1 << type;
	if (perm)
		return ccs_update_path2_acl(perm, w[1], w[2], domain,
					    condition, is_delete);
	for (type = 0; type < CCS_MAX_PATH_NUMBER_OPERATION; type++)
		if (ccs_permstr(w[0], ccs_path_number_keyword[type]))
			perm |= 1 << type;
	if (perm)
		return ccs_update_path_number_acl(perm, w[1], w[2], domain,
						  condition, is_delete);
	if (!w[3][0] || !w[4][0])
		goto out;
	for (type = 0; type < CCS_MAX_MKDEV_OPERATION; type++)
		if (ccs_permstr(w[0], ccs_mkdev_keyword[type]))
			perm |= 1 << type;
	if (perm)
		return ccs_update_mkdev_acl(perm, w[1], w[2], w[3], w[4],
					    domain, condition, is_delete);
	if (ccs_permstr(w[0], "mount"))
		return ccs_update_mount_acl(w[1], w[2], w[3], w[4], domain,
					    condition, is_delete);
 out:
	return -EINVAL;
}

/*
 * Permission checks from vfs_mknod().
 *
 * This function is exported because
 * vfs_mknod() is called from net/unix/af_unix.c.
 */
static int __ccs_mknod_permission(struct inode *dir, struct dentry *dentry,
				  struct vfsmount *mnt,
				  const unsigned int mode, unsigned int dev)
{
	int error = 0;
	const unsigned int perm = mode & S_IALLUGO;
	switch (mode & S_IFMT) {
	case S_IFCHR:
		error = ccs_mkdev_perm(CCS_TYPE_MKCHAR, dir, dentry, mnt, perm,
				       dev);
		break;
	case S_IFBLK:
		error = ccs_mkdev_perm(CCS_TYPE_MKBLOCK, dir, dentry, mnt,
				       perm, dev);
		break;
	case S_IFIFO:
		error = ccs_path_number_perm(CCS_TYPE_MKFIFO, dir, dentry, mnt,
					     perm);
		break;
	case S_IFSOCK:
		error = ccs_path_number_perm(CCS_TYPE_MKSOCK, dir, dentry, mnt,
					     perm);
		break;
	case 0:
	case S_IFREG:
		error = ccs_path_number_perm(CCS_TYPE_CREATE, dir, dentry, mnt,
					     perm);
		break;
	}
	return error;
}

/* Permission checks for vfs_mkdir(). */
static int __ccs_mkdir_permission(struct inode *dir, struct dentry *dentry,
				  struct vfsmount *mnt, unsigned int mode)
{
	return ccs_path_number_perm(CCS_TYPE_MKDIR, dir, dentry, mnt, mode);
}

/* Permission checks for vfs_rmdir(). */
static int __ccs_rmdir_permission(struct inode *dir, struct dentry *dentry,
				  struct vfsmount *mnt)
{
	return ccs_path_perm(CCS_TYPE_RMDIR, dir, dentry, mnt, NULL);
}

/* Permission checks for vfs_unlink(). */
static int __ccs_unlink_permission(struct inode *dir, struct dentry *dentry,
				   struct vfsmount *mnt)
{
	return ccs_path_perm(CCS_TYPE_UNLINK, dir, dentry, mnt, NULL);
}

/* Permission checks for vfs_symlink(). */
static int __ccs_symlink_permission(struct inode *dir, struct dentry *dentry,
				    struct vfsmount *mnt, const char *from)
{
	return ccs_path_perm(CCS_TYPE_SYMLINK, dir, dentry, mnt, from);
}

/* Permission checks for notify_change(). */
static int __ccs_truncate_permission(struct dentry *dentry,
				     struct vfsmount *mnt)
{
	return ccs_path_perm(CCS_TYPE_TRUNCATE, NULL, dentry, mnt, NULL);
}

/* Permission checks for vfs_rename(). */
static int __ccs_rename_permission(struct inode *old_dir,
				   struct dentry *old_dentry,
				   struct inode *new_dir,
				   struct dentry *new_dentry,
				   struct vfsmount *mnt)
{
	return ccs_path2_perm(CCS_TYPE_RENAME, old_dir, old_dentry, mnt,
			      new_dir, new_dentry, mnt);
}

/* Permission checks for vfs_link(). */
static int __ccs_link_permission(struct dentry *old_dentry,
				 struct inode *new_dir,
				 struct dentry *new_dentry,
				 struct vfsmount *mnt)
{
	return ccs_path2_perm(CCS_TYPE_LINK, NULL, old_dentry, mnt,
			      new_dir, new_dentry, mnt);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 30)
/* Permission checks for open_exec(). */
static int __ccs_open_exec_permission(struct dentry *dentry,
				      struct vfsmount *mnt)
{
	return (current->ccs_flags & CCS_TASK_IS_IN_EXECVE) ?
		/* 01 means "read". */
		__ccs_open_permission(dentry, mnt, 01) : 0;
}

/* Permission checks for sys_uselib(). */
static int __ccs_uselib_permission(struct dentry *dentry, struct vfsmount *mnt)
{
	/* 01 means "read". */
	return __ccs_open_permission(dentry, mnt, 01);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18) || defined(CONFIG_SYSCTL_SYSCALL)

#include <linux/sysctl.h>

/* Permission checks for parse_table(). */
static int __ccs_parse_table(int __user *name, int nlen, void __user *oldval,
			     void __user *newval, struct ctl_table *table)
{
	int n;
	int error = -ENOMEM;
	int op = 0;
	struct ccs_path_info buf;
	char *buffer = NULL;
	struct ccs_request_info r;
	int idx;
	if (oldval)
		op |= 004;
	if (newval)
		op |= 002;
	if (!op) /* Neither read nor write */
		return 0;
	idx = ccs_read_lock();
	if (ccs_init_request_info(&r, CCS_MAC_FILE_OPEN)
	    == CCS_CONFIG_DISABLED) {
		error = 0;
		goto out;
	}
	buffer = kmalloc(PAGE_SIZE, CCS_GFP_FLAGS);
	if (!buffer)
		goto out;
	snprintf(buffer, PAGE_SIZE - 1, "proc:/sys");
 repeat:
	if (!nlen) {
		error = -ENOTDIR;
		goto out;
	}
	if (get_user(n, name)) {
		error = -EFAULT;
		goto out;
	}
	for ( ; table->ctl_name
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 21)
		      || table->procname
#endif
		      ; table++) {
		int pos;
		const char *cp;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 21)
		if (n != table->ctl_name && table->ctl_name != CTL_ANY)
			continue;
#else
		if (!n || n != table->ctl_name)
			continue;
#endif
		pos = strlen(buffer);
		cp = table->procname;
		error = -ENOMEM;
		if (cp) {
			int len = strlen(cp);
			if (len + 2 > PAGE_SIZE - 1)
				goto out;
			buffer[pos++] = '/';
			memmove(buffer + pos, cp, len + 1);
		} else {
			/* Assume nobody assigns "=\$=" for procname. */
			snprintf(buffer + pos, PAGE_SIZE - pos - 1,
				 "/=%d=", table->ctl_name);
			if (!memchr(buffer, '\0', PAGE_SIZE - 2))
				goto out;
		}
		if (!table->child)
			goto no_child;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 21)
		if (!table->strategy)
			goto no_strategy;
		/* printk("sysctl='%s'\n", buffer); */
		buf.name = ccs_encode(buffer);
		if (!buf.name)
			goto out;
		ccs_fill_path_info(&buf);
		if (op & MAY_READ)
			error = ccs_path_permission(&r, CCS_TYPE_READ, &buf);
		else
			error = 0;
		if (!error && (op & MAY_WRITE))
			error = ccs_path_permission(&r, CCS_TYPE_WRITE, &buf);
		kfree(buf.name);
		if (error)
			goto out;
no_strategy:
#endif
		name++;
		nlen--;
		table = table->child;
		goto repeat;
no_child:
		/* printk("sysctl='%s'\n", buffer); */
		buf.name = ccs_encode(buffer);
		if (!buf.name)
			goto out;
		ccs_fill_path_info(&buf);
		if (op & MAY_READ)
			error = ccs_path_permission(&r, CCS_TYPE_READ, &buf);
		else
			error = 0;
		if (!error && (op & MAY_WRITE))
			error = ccs_path_permission(&r, CCS_TYPE_WRITE, &buf);
		kfree(buf.name);
		goto out;
	}
	error = -ENOTDIR;
 out:
	ccs_read_unlock(idx);
	kfree(buffer);
	return error;
}
#endif
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 24)
static int ccs_old_pivot_root_permission(struct nameidata *old_nd,
					 struct nameidata *new_nd)
{
	struct path old_path = { old_nd->mnt, old_nd->dentry };
	struct path new_path = { new_nd->mnt, new_nd->dentry };
	return __ccs_pivot_root_permission(&old_path, &new_path);
}

static int ccs_old_chroot_permission(struct nameidata *nd)
{
	struct path path = { nd->mnt, nd->dentry };
	return __ccs_chroot_permission(&path);
}
#endif

void __init ccs_file_init(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
	ccsecurity_ops.save_open_mode = __ccs_save_open_mode;
	ccsecurity_ops.clear_open_mode = __ccs_clear_open_mode;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
	ccsecurity_ops.open_permission = ccs_new_open_permission;
#else
	ccsecurity_ops.open_permission = __ccs_open_permission;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	ccsecurity_ops.fcntl_permission = __ccs_fcntl_permission;
#else
	ccsecurity_ops.rewrite_permission = __ccs_rewrite_permission;
#endif
	ccsecurity_ops.ioctl_permission = __ccs_ioctl_permission;
	ccsecurity_ops.chmod_permission = __ccs_chmod_permission;
	ccsecurity_ops.chown_permission = __ccs_chown_permission;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	ccsecurity_ops.pivot_root_permission = __ccs_pivot_root_permission;
	ccsecurity_ops.chroot_permission = __ccs_chroot_permission;
#else
	ccsecurity_ops.pivot_root_permission = ccs_old_pivot_root_permission;
	ccsecurity_ops.chroot_permission = ccs_old_chroot_permission;
#endif
	ccsecurity_ops.umount_permission = __ccs_umount_permission;
	ccsecurity_ops.mknod_permission = __ccs_mknod_permission;
	ccsecurity_ops.mkdir_permission = __ccs_mkdir_permission;
	ccsecurity_ops.rmdir_permission = __ccs_rmdir_permission;
	ccsecurity_ops.unlink_permission = __ccs_unlink_permission;
	ccsecurity_ops.symlink_permission = __ccs_symlink_permission;
	ccsecurity_ops.truncate_permission = __ccs_truncate_permission;
	ccsecurity_ops.rename_permission = __ccs_rename_permission;
	ccsecurity_ops.link_permission = __ccs_link_permission;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 30)
	ccsecurity_ops.open_exec_permission = __ccs_open_exec_permission;
	ccsecurity_ops.uselib_permission = __ccs_uselib_permission;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18) || defined(CONFIG_SYSCTL_SYSCALL)
	ccsecurity_ops.parse_table = __ccs_parse_table;
#endif
#endif
};
