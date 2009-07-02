/*
 * fs/sakura_umount.c
 *
 * Implementation of the Domain-Free Mandatory Access Control.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 1.7.0-pre   2009/05/28
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include <linux/ccs_common.h>
#include <linux/sakura.h>
#include <linux/realpath.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
#include <linux/mount.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
#include <linux/namespace.h>
#endif

/**
 * ccs_audit_umount_log - Audit unmount log.
 *
 * @r:          Pointer to "struct ccs_request_info".
 * @dir:        Mount point.
 * @is_granted: True if this is a granted log.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_umount_log(struct ccs_request_info *r,
				const char *dir, const bool is_granted)
{
	return ccs_write_audit_log(is_granted, r, KEYWORD_ALLOW_UNMOUNT
				   "%s\n", dir);
}

/**
 * ccs_update_umount_acl - Update "struct ccs_umount_acl_record" list.
 *
 * @dir:       The name of directrory.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_update_umount_acl(const char *dir,
				 struct ccs_domain_info *domain,
				 struct ccs_condition *condition,
				 const bool is_delete)
{
	struct ccs_umount_acl_record *entry = NULL;
	struct ccs_acl_info *ptr;
	const struct ccs_path_info *saved_dir;
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!ccs_is_correct_path(dir, 1, 0, 1))
		return -EINVAL;
	saved_dir = ccs_get_name(dir);
	if (!saved_dir)
		return -ENOMEM;
	if (!is_delete)
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	mutex_lock(&ccs_policy_lock);
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct ccs_umount_acl_record *acl;
		if (ccs_acl_type1(ptr) != TYPE_UMOUNT_ACL)
			continue;
		if (ptr->cond != condition)
			continue;
		acl = container_of(ptr, struct ccs_umount_acl_record, head);
		if (acl->dir != saved_dir)
			continue;
		if (is_delete)
			error = ccs_del_domain_acl(ptr);
		else
			error = ccs_add_domain_acl(NULL, ptr);
		break;
	}
	if (!is_delete && error && ccs_memory_ok(entry, sizeof(*entry))) {
		entry->head.type = TYPE_UMOUNT_ACL;
		entry->head.cond = condition;
		entry->dir = saved_dir;
		saved_dir = NULL;
		error = ccs_add_domain_acl(domain, &entry->head);
		entry = NULL;
	}
	mutex_unlock(&ccs_policy_lock);
	ccs_put_name(saved_dir);
	kfree(entry);
	return error;
}

/**
 * ccs_may_umount2 - Check permission for unmount.
 *
 * @mnt: Pointer to "struct vfsmount".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds srcu_read_lock(&ccs_ss).
 */
static int ccs_may_umount2(struct vfsmount *mnt)
{
	struct ccs_request_info r;
	int error;
	const char *dir0;
	bool is_enforce;
	struct ccs_acl_info *ptr;
	struct ccs_path_info dir;
	if (!ccs_can_sleep())
		return 0;
	ccs_init_request_info(&r, NULL, CCS_MAC_FOR_NAMESPACE);
	is_enforce = (r.mode == 3);
	if (!r.mode)
		return 0;
 retry:
	error = -EPERM;
	dir0 = ccs_realpath_from_dentry(mnt->mnt_root, mnt);
	if (!dir0)
		goto out;
	dir.name = dir0;
	ccs_fill_path_info(&dir);
	list_for_each_entry_rcu(ptr, &r.domain->acl_info_list, list) {
		struct ccs_umount_acl_record *acl;
		if (ccs_acl_type2(ptr) != TYPE_UMOUNT_ACL)
			continue;
		acl = container_of(ptr, struct ccs_umount_acl_record, head);
		if (!ccs_path_matches_pattern(&dir, acl->dir))
			continue;
		error = 0;
		break;
	}
	ccs_audit_umount_log(&r, dir0, !error);
	if (!error)
		goto out;
	if (ccs_verbose_mode(r.domain))
		printk(KERN_WARNING "SAKURA-%s: umount %s denied for %s\n",
		       ccs_get_msg(is_enforce), dir0,
		       ccs_get_last_name(r.domain));
	if (is_enforce)
		error = ccs_check_supervisor(&r, KEYWORD_ALLOW_UNMOUNT "%s",
					     dir0);
	else if (r.mode == 1)
		ccs_update_umount_acl(dir0, r.domain, NULL, false);
 out:
	kfree(dir0);
	if (!is_enforce)
		error = 0;
	if (error == 1)
		goto retry;
	return error;
}

/**
 * ccs_may_umount - Check permission for unmount.
 *
 * @mnt: Pointer to "struct vfsmount".
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_may_umount(struct vfsmount *mnt)
{
	const int idx = srcu_read_lock(&ccs_ss);
	const int error = ccs_may_umount2(mnt);
	srcu_read_unlock(&ccs_ss, idx);
	return error;
}

/**
 * ccs_write_umount_policy - Write "struct ccs_umount_acl_record" list.
 *
 * @data:      String to parse.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". May be NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on sucess, negative value otherwise.
 */
int ccs_write_umount_policy(char *data, struct ccs_domain_info *domain,
			    struct ccs_condition *condition,
			    const bool is_delete)
{
	return ccs_update_umount_acl(data, domain, condition, is_delete);
}
