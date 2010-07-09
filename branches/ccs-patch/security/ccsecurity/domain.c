/*
 * security/ccsecurity/domain.c
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 * Version: 1.7.2+   2010/06/04
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
#include <linux/namei.h>
#include <linux/mount.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30)
#include <linux/fs_struct.h>
#endif
#include "internal.h"

/* Variables definitions.*/

/* The global domain. */
struct ccs_domain_info ccs_global_domain;

/* The initial domain. */
struct ccs_domain_info ccs_kernel_domain;

/* The list for "struct ccs_domain_info". */
LIST_HEAD(ccs_domain_list);

struct list_head ccs_policy_list[CCS_MAX_POLICY];
struct list_head ccs_group_list[CCS_MAX_GROUP];
struct list_head ccs_shared_list[CCS_MAX_LIST];

/**
 * ccs_audit_execute_handler_log - Audit execute_handler log.
 *
 * @ee:         Pointer to "struct ccs_execve".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_execute_handler_log(struct ccs_execve *ee)
{
	struct ccs_request_info *r = &ee->r;
	const char *handler = ee->handler->name;
	r->type = CCS_MAC_FILE_EXECUTE;
	r->mode = ccs_get_mode(r->profile, CCS_MAC_FILE_EXECUTE);
	r->granted = true;
	return ccs_write_log(r, "%s %s\n", ee->handler_type ==
			     CCS_TYPE_DENIED_EXECUTE_HANDLER ?
			     CCS_KEYWORD_DENIED_EXECUTE_HANDLER :
			     CCS_KEYWORD_EXECUTE_HANDLER, handler);
}

/**
 * ccs_audit_domain_creation_log - Audit domain creation log.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_domain_creation_log(void)
{
	struct ccs_request_info r;
	ccs_init_request_info(&r, CCS_MAC_FILE_EXECUTE);
	r.granted = false;
	return ccs_write_log(&r, "use_profile %u\n", r.profile);
}

/**
 * ccs_update_policy - Update an entry for exception policy.
 *
 * @new_entry:       Pointer to "struct ccs_acl_info".
 * @size:            Size of @new_entry in bytes.
 * @is_delete:       True if it is a delete request.
 * @list:            Pointer to "struct list_head".
 * @check_duplicate: Callback function to find duplicated entry.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
int ccs_update_policy(struct ccs_acl_head *new_entry, const int size,
		      bool is_delete, struct list_head *list,
		      bool (*check_duplicate) (const struct ccs_acl_head *,
					       const struct ccs_acl_head *))
{
	int error = is_delete ? -ENOENT : -ENOMEM;
	struct ccs_acl_head *entry;
	if (mutex_lock_interruptible(&ccs_policy_lock))
		return -ENOMEM;
	list_for_each_entry_rcu(entry, list, list) {
		if (!check_duplicate(entry, new_entry))
			continue;
		entry->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (error && !is_delete) {
		entry = ccs_commit_ok(new_entry, size);
		if (entry) {
			list_add_tail_rcu(&entry->list, list);
			error = 0;
		}
	}
	mutex_unlock(&ccs_policy_lock);
	return error;
}

static void ccs_delete_type(struct ccs_domain_info *domain, u8 type)
{
	struct ccs_acl_info *ptr;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		if (ptr->type == type)
			ptr->is_deleted = true;
	}
}

/**
 * ccs_update_domain - Update an entry for domain policy.
 *
 * @new_entry:       Pointer to "struct ccs_acl_info".
 * @size:            Size of @new_entry in bytes.
 * @is_delete:       True if it is a delete request.
 * @domain:          Pointer to "struct ccs_domain_info".
 * @check_duplicate: Callback function to find duplicated entry.
 * @merge_duplicate: Callback function to merge duplicated entry.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
int ccs_update_domain(struct ccs_acl_info *new_entry, const int size,
		      bool is_delete, struct ccs_domain_info *domain,
		      bool (*check_duplicate) (const struct ccs_acl_info *,
					       const struct ccs_acl_info *),
		      bool (*merge_duplicate) (struct ccs_acl_info *,
					       struct ccs_acl_info *,
					       const bool))
{
	int error = is_delete ? -ENOENT : -ENOMEM;
	struct ccs_acl_info *entry;
	/*
	 * Only one "execute_handler" and "denied_execute_handler" can exist
	 * in a domain.
	 */
	const u8 type = new_entry->type;
	const bool exclusive = !is_delete &&
		(type == CCS_TYPE_EXECUTE_HANDLER ||
		 type == CCS_TYPE_DENIED_EXECUTE_HANDLER);
	if (mutex_lock_interruptible(&ccs_policy_lock))
		return error;
	list_for_each_entry_rcu(entry, &domain->acl_info_list, list) {
		if (!check_duplicate(entry, new_entry))
			continue;
		if (exclusive)
			ccs_delete_type(domain, type);
		if (merge_duplicate)
			entry->is_deleted = merge_duplicate(entry, new_entry,
							    is_delete);
		else
			entry->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (error && !is_delete) {
		entry = ccs_commit_ok(new_entry, size);
		if (entry) {
			if (exclusive)
				ccs_delete_type(domain, type);
			if (entry->cond)
				atomic_inc(&entry->cond->head.users);
			list_add_tail_rcu(&entry->list,
					  &domain->acl_info_list);
			error = 0;
		}
	}
	mutex_unlock(&ccs_policy_lock);
	return error;
}

void ccs_check_acl(struct ccs_request_info *r,
		   bool (*check_entry) (const struct ccs_request_info *,
					const struct ccs_acl_info *))
{
	const struct ccs_domain_info *domain = ccs_current_domain();
	struct ccs_acl_info *ptr;
 retry:
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		if (ptr->is_deleted || ptr->type != r->param_type)
			continue;
		if (check_entry(r, ptr) && ccs_condition(r, ptr->cond)) {
			r->cond = ptr->cond;
			r->granted = true;
			return;
		}
	}
	if (domain != &ccs_global_domain &&
	    !domain->flags[CCS_DIF_IGNORE_GLOBAL]) {
		domain = &ccs_global_domain;
		goto retry;
	}
	r->granted = false;
}

static bool ccs_same_transition_control(const struct ccs_acl_head *a,
					const struct ccs_acl_head *b)
{
	const struct ccs_transition_control *p1 = container_of(a, typeof(*p1),
							       head);
	const struct ccs_transition_control *p2 = container_of(b, typeof(*p2),
							       head);
	return p1->type == p2->type && p1->is_last_name == p2->is_last_name
		&& p1->domainname == p2->domainname
		&& p1->program == p2->program;
}

/**
 * ccs_update_transition_control_entry - Update "struct ccs_transition_control" list.
 *
 * @domainname: The name of domain. Maybe NULL.
 * @program:    The name of program. Maybe NULL.
 * @type:       Type of transition.
 * @is_delete:  True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_update_transition_control_entry(const char *domainname,
					       const char *program,
					       const u8 type,
					       const bool is_delete)
{
	struct ccs_transition_control e = { .type = type };
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (program && strcmp(program, "any")) {
		if (!ccs_correct_path(program))
			return -EINVAL;
		e.program = ccs_get_name(program);
		if (!e.program)
			goto out;
	}
	if (domainname && strcmp(domainname, "any")) {
		if (!ccs_correct_domain(domainname)) {
			if (!ccs_correct_path(domainname))
				goto out;
			e.is_last_name = true;
		}
		e.domainname = ccs_get_name(domainname);
		if (!e.domainname)
			goto out;
	}
	error = ccs_update_policy(&e.head, sizeof(e), is_delete,
				  &ccs_policy_list[CCS_ID_TRANSITION_CONTROL],
				  ccs_same_transition_control);
 out:
	ccs_put_name(e.domainname);
	ccs_put_name(e.program);
	return error;
}

/**
 * ccs_write_transition_control - Write "struct ccs_transition_control" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 * @type:      Type of this entry.
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_write_transition_control(char *data, const bool is_delete,
				 const u8 type)
{
	char *domainname = strstr(data, " from ");
	if (domainname) {
		*domainname = '\0';
		domainname += 6;
	} else if (type == CCS_TRANSITION_CONTROL_NO_KEEP ||
		   type == CCS_TRANSITION_CONTROL_KEEP) {
		domainname = data;
		data = NULL;
	}
	return ccs_update_transition_control_entry(domainname, data, type,
						   is_delete);
}

/**
 * ccs_transition_type - Get domain transition type.
 *
 * @domainname: The name of domain.
 * @program:    The name of program.
 *
 * Returns CCS_TRANSITION_CONTROL_INITIALIZE if executing @program
 * reinitializes domain transition, CCS_TRANSITION_CONTROL_KEEP if executing
 * @program suppresses domain transition, others otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static u8 ccs_transition_type(const struct ccs_path_info *domainname,
			      const struct ccs_path_info *program)
{
	const struct ccs_transition_control *ptr;
	const char *last_name = ccs_last_word(domainname->name);
	u8 type;
	for (type = 0; type < CCS_MAX_TRANSITION_TYPE; type++) {
 next:
		list_for_each_entry_rcu(ptr, &ccs_policy_list
					[CCS_ID_TRANSITION_CONTROL],
					head.list) {
			if (ptr->head.is_deleted || ptr->type != type)
				continue;
			if (ptr->domainname) {
				if (!ptr->is_last_name) {
					if (ptr->domainname != domainname)
						continue;
				} else {
					/*
					 * Use direct strcmp() since this is
					 * unlikely used.
					 */
					if (strcmp(ptr->domainname->name,
						   last_name))
						continue;
				}
			}
			if (ptr->program && ccs_pathcmp(ptr->program, program))
				continue;
			if (type == CCS_TRANSITION_CONTROL_NO_INITIALIZE) {
				/*
				 * Do not check for initialize_domain if
				 * no_initialize_domain matched.
				 */
				type = CCS_TRANSITION_CONTROL_NO_KEEP;
				goto next;
			}
			goto done;
		}
	}
 done:
	return type;
}

static bool ccs_same_aggregator(const struct ccs_acl_head *a,
				const struct ccs_acl_head *b)
{
	const struct ccs_aggregator *p1 = container_of(a, typeof(*p1), head);
	const struct ccs_aggregator *p2 = container_of(b, typeof(*p2), head);
	return p1->original_name == p2->original_name &&
		p1->aggregated_name == p2->aggregated_name;
}

/**
 * ccs_update_aggregator_entry - Update "struct ccs_aggregator" list.
 *
 * @original_name:   The original program's name.
 * @aggregated_name: The aggregated program's name.
 * @is_delete:       True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_update_aggregator_entry(const char *original_name,
				       const char *aggregated_name,
				       const bool is_delete)
{
	struct ccs_aggregator e = { };
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!ccs_correct_path(original_name) ||
	    !ccs_correct_path(aggregated_name))
		return -EINVAL;
	e.original_name = ccs_get_name(original_name);
	e.aggregated_name = ccs_get_name(aggregated_name);
	if (!e.original_name || !e.aggregated_name ||
	    e.aggregated_name->is_patterned) /* No patterns allowed. */
		goto out;
	error = ccs_update_policy(&e.head, sizeof(e), is_delete,
				  &ccs_policy_list[CCS_ID_AGGREGATOR],
				  ccs_same_aggregator);
 out:
	ccs_put_name(e.original_name);
	ccs_put_name(e.aggregated_name);
	return error;
}

/**
 * ccs_write_aggregator - Write "struct ccs_aggregator" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_write_aggregator(char *data, const bool is_delete)
{
	char *w[2];
	if (!ccs_tokenize(data, w, sizeof(w)) || !w[1][0])
		return -EINVAL;
	return ccs_update_aggregator_entry(w[0], w[1], is_delete);
}

/* Domain create/delete handler. */

/**
 * ccs_delete_domain - Delete a domain.
 *
 * @domainname: The name of domain.
 *
 * Returns 0.
 */
int ccs_delete_domain(char *domainname)
{
	struct ccs_domain_info *domain;
	struct ccs_path_info name;
	name.name = domainname;
	ccs_fill_path_info(&name);
	if (mutex_lock_interruptible(&ccs_policy_lock))
		return 0;
	/* Is there an active domain? */
	list_for_each_entry_rcu(domain, &ccs_domain_list, list) {
		/* Never delete ccs_kernel_domain */
		if (domain == &ccs_kernel_domain)
			continue;
		if (domain->is_deleted ||
		    ccs_pathcmp(domain->domainname, &name))
			continue;
		domain->is_deleted = true;
		break;
	}
	mutex_unlock(&ccs_policy_lock);
	return 0;
}

/**
 * ccs_assign_domain - Create a domain.
 *
 * @domainname: The name of domain.
 * @profile:    Profile number to assign if the domain was newly created.
 *
 * Returns pointer to "struct ccs_domain_info" on success, NULL otherwise.
 */
struct ccs_domain_info *ccs_assign_domain(const char *domainname,
					  const u8 profile)
{
	struct ccs_domain_info e = { };
	struct ccs_domain_info *entry = NULL;
	bool found = false;

	if (!ccs_correct_domain(domainname))
		return NULL;
	e.profile = profile;
	e.domainname = ccs_get_name(domainname);
	if (!e.domainname)
		return NULL;
	if (mutex_lock_interruptible(&ccs_policy_lock))
		goto out;
	list_for_each_entry_rcu(entry, &ccs_domain_list, list) {
		if (entry->is_deleted ||
		    ccs_pathcmp(e.domainname, entry->domainname))
			continue;
		found = true;
		break;
	}
	if (!found) {
		entry = ccs_commit_ok(&e, sizeof(e));
		if (entry) {
			INIT_LIST_HEAD(&entry->acl_info_list);
			list_add_tail_rcu(&entry->list, &ccs_domain_list);
			found = true;
		}
	}
	mutex_unlock(&ccs_policy_lock);
 out:
	ccs_put_name(e.domainname);
	return found ? entry : NULL;
}

/**
 * ccs_find_next_domain - Find a domain.
 *
 * @ee: Pointer to "struct ccs_execve".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static int ccs_find_next_domain(struct ccs_execve *ee)
{
	struct ccs_request_info *r = &ee->r;
	const struct ccs_path_info *handler = ee->handler;
	struct ccs_domain_info *domain = NULL;
	struct ccs_domain_info * const old_domain = ccs_current_domain();
	struct linux_binprm *bprm = ee->bprm;
	struct task_struct *task = current;
	const u32 ccs_flags = task->ccs_flags;
	struct ccs_path_info rn = { }; /* real name */
	int retval;
	bool need_kfree = false;
	bool domain_created = false;
 retry:
	current->ccs_flags = ccs_flags;
	r->cond = NULL;
	if (need_kfree) {
		kfree(rn.name);
		need_kfree = false;
	}

	/* Get symlink's pathname of program. */
	retval = ccs_symlink_path(bprm->filename, &rn);
	if (retval < 0)
		goto out;
	need_kfree = true;

	if (handler) {
		if (ccs_pathcmp(&rn, handler)) {
			/* Failed to verify execute handler. */
			static u8 counter = 20;
			if (counter) {
				counter--;
				printk(KERN_WARNING "Failed to verify: %s\n",
				       handler->name);
			}
			goto out;
		}
	} else {
		struct ccs_aggregator *ptr;
		/* Check 'aggregator' directive. */
		list_for_each_entry_rcu(ptr,
					&ccs_policy_list[CCS_ID_AGGREGATOR],
					head.list) {
			if (ptr->head.is_deleted ||
			    !ccs_path_matches_pattern(&rn, ptr->original_name))
				continue;
			kfree(rn.name);
			need_kfree = false;
			/* This is OK because it is read only. */
			rn = *ptr->aggregated_name;
			break;
		}

		/* Check execute permission. */
		retval = ccs_path_permission(r, CCS_TYPE_EXECUTE, &rn);
		if (retval == CCS_RETRY_REQUEST)
			goto retry;
		if (retval < 0)
			goto out;
	}

	/* Calculate domain to transit to. */
	switch (ccs_transition_type(old_domain->domainname, &rn)) {
	case CCS_TRANSITION_CONTROL_INITIALIZE:
		/* Transit to the child of ccs_kernel_domain domain. */
		snprintf(ee->tmp, CCS_EXEC_TMPSIZE - 1, CCS_ROOT_NAME " " "%s",
			 rn.name);
		break;
	case CCS_TRANSITION_CONTROL_KEEP:
		/* Keep current domain. */
		domain = old_domain;
		break;
	default:
		if (old_domain == &ccs_kernel_domain && !ccs_policy_loaded) {
			/*
			 * Needn't to transit from kernel domain before
			 * starting /sbin/init. But transit from kernel domain
			 * if executing initializers because they might start
			 * before /sbin/init.
			 */
			domain = old_domain;
		} else {
			/* Normal domain transition. */
			snprintf(ee->tmp, CCS_EXEC_TMPSIZE - 1, "%s %s",
				 old_domain->domainname->name, rn.name);
		}
		break;
	}
	if (domain || strlen(ee->tmp) >= CCS_EXEC_TMPSIZE - 10)
		goto done;
	domain = ccs_find_domain(ee->tmp);
	if (domain)
		goto done;
	if (r->mode == CCS_CONFIG_ENFORCING) {
		int error = ccs_supervisor(r, "# wants to create domain\n"
					   "%s\n", ee->tmp);
		if (error == CCS_RETRY_REQUEST)
			goto retry;
		if (error < 0)
			goto done;
	}
	domain = ccs_assign_domain(ee->tmp, r->profile);
	if (domain)
		domain_created = true;
 done:
	if (!domain) {
		retval = (r->mode == CCS_CONFIG_ENFORCING) ? -EPERM : 0;
		if (!old_domain->flags[CCS_DIF_TRANSITION_FAILED]) {
			old_domain->flags[CCS_DIF_TRANSITION_FAILED] = true;
			r->granted = false;
			ccs_write_log(r, CCS_KEYWORD_TRANSITION_FAILED "\n");
			printk(KERN_WARNING
			       "ERROR: Domain '%s' not defined.\n", ee->tmp);
		}
	} else {
		retval = 0;
	}
	if (!retval && handler)
		ccs_audit_execute_handler_log(ee);
	/*
	 * Tell GC that I started execve().
	 * Also, tell open_exec() to check read permission.
	 */
	task->ccs_flags |= CCS_TASK_IS_IN_EXECVE;
	/*
	 * Make task->ccs_flags visible to GC before changing
	 * task->ccs_domain_info .
	 */
	smp_mb();
	/*
	 * Proceed to the next domain in order to allow reaching via PID.
	 * It will be reverted if execve() failed. Reverting is not good.
	 * But it is better than being unable to reach via PID in interactive
	 * enforcing mode.
	 */
	if (domain)
		task->ccs_domain_info = domain;
	if (domain_created)
		ccs_audit_domain_creation_log();
 out:
	if (need_kfree)
		kfree(rn.name);
	return retval;
}

/**
 * ccs_environ - Check permission for environment variable names.
 *
 * @ee: Pointer to "struct ccs_execve".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_environ(struct ccs_execve *ee)
{
	struct ccs_request_info *r = &ee->r;
	struct linux_binprm *bprm = ee->bprm;
	/* env_page->data is allocated by ccs_dump_page(). */
	struct ccs_page_dump env_page = { };
	char *arg_ptr; /* Size is CCS_EXEC_TMPSIZE bytes */
	int arg_len = 0;
	unsigned long pos = bprm->p;
	int offset = pos % PAGE_SIZE;
	int argv_count = bprm->argc;
	int envp_count = bprm->envc;
	/* printk(KERN_DEBUG "start %d %d\n", argv_count, envp_count); */
	int error = -ENOMEM;
	ee->r.type = CCS_MAC_ENVIRON;
	ee->r.mode = ccs_get_mode(ccs_current_domain()->profile,
				  CCS_MAC_ENVIRON);
	if (!r->mode || !envp_count)
		return 0;
	arg_ptr = kzalloc(CCS_EXEC_TMPSIZE, CCS_GFP_FLAGS);
	if (!arg_ptr)
		goto out;
	while (error == -ENOMEM) {
		if (!ccs_dump_page(bprm, pos, &env_page))
			goto out;
		pos += PAGE_SIZE - offset;
		/* Read. */
		while (argv_count && offset < PAGE_SIZE) {
			if (!env_page.data[offset++])
				argv_count--;
		}
		if (argv_count) {
			offset = 0;
			continue;
		}
		while (offset < PAGE_SIZE) {
			const unsigned char c = env_page.data[offset++];
			if (c && arg_len < CCS_EXEC_TMPSIZE - 10) {
				if (c == '=') {
					arg_ptr[arg_len++] = '\0';
				} else if (c == '\\') {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = '\\';
				} else if (c > ' ' && c < 127) {
					arg_ptr[arg_len++] = c;
				} else {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = (c >> 6) + '0';
					arg_ptr[arg_len++]
						= ((c >> 3) & 7) + '0';
					arg_ptr[arg_len++] = (c & 7) + '0';
				}
			} else {
				arg_ptr[arg_len] = '\0';
			}
			if (c)
				continue;
			if (ccs_env_perm(r, arg_ptr)) {
				error = -EPERM;
				break;
			}
			if (!--envp_count) {
				error = 0;
				break;
			}
			arg_len = 0;
		}
		offset = 0;
	}
 out:
	if (r->mode != 3)
		error = 0;
	kfree(env_page.data);
	kfree(arg_ptr);
	return error;
}

/**
 * ccs_unescape - Unescape escaped string.
 *
 * @dest: String to unescape.
 *
 * Returns nothing.
 */
static void ccs_unescape(unsigned char *dest)
{
	unsigned char *src = dest;
	unsigned char c;
	unsigned char d;
	unsigned char e;
	while (1) {
		c = *src++;
		if (!c)
			break;
		if (c != '\\') {
			*dest++ = c;
			continue;
		}
		c = *src++;
		if (c == '\\') {
			*dest++ = c;
			continue;
		}
		if (c < '0' || c > '3')
			break;
		d = *src++;
		if (d < '0' || d > '7')
			break;
		e = *src++;
		if (e < '0' || e > '7')
			break;
		*dest++ = ((c - '0') << 6) + ((d - '0') << 3) + (e - '0');
	}
	*dest = '\0';
}

/**
 * ccs_root_depth - Get number of directories to strip.
 *
 * @dentry: Pointer to "struct dentry".
 * @vfsmnt: Pointer to "struct vfsmount".
 *
 * Returns number of directories to strip.
 */
static int ccs_root_depth(struct dentry *dentry, struct vfsmount *vfsmnt)
{
	int depth = 0;
	ccs_realpath_lock();
	for (;;) {
		if (dentry == vfsmnt->mnt_root || IS_ROOT(dentry)) {
			/* Global root? */
			if (vfsmnt->mnt_parent == vfsmnt)
				break;
			dentry = vfsmnt->mnt_mountpoint;
			vfsmnt = vfsmnt->mnt_parent;
			continue;
		}
		dentry = dentry->d_parent;
		depth++;
	}
	ccs_realpath_unlock();
	return depth;
}

/**
 * ccs_get_root_depth - return the depth of root directory.
 *
 * Returns number of directories to strip.
 */
static int ccs_get_root_depth(void)
{
	int depth;
	struct dentry *dentry;
	struct vfsmount *vfsmnt;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	struct path root;
#endif
	read_lock(&current->fs->lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	root = current->fs->root;
	path_get(&current->fs->root);
	dentry = root.dentry;
	vfsmnt = root.mnt;
#else
	dentry = dget(current->fs->root);
	vfsmnt = mntget(current->fs->rootmnt);
#endif
	read_unlock(&current->fs->lock);
	depth = ccs_root_depth(dentry, vfsmnt);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	path_put(&root);
#else
	dput(dentry);
	mntput(vfsmnt);
#endif
	return depth;
}

/**
 * ccs_try_alt_exec - Try to start execute handler.
 *
 * @ee: Pointer to "struct ccs_execve".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_try_alt_exec(struct ccs_execve *ee)
{
	/*
	 * Contents of modified bprm.
	 * The envp[] in original bprm is moved to argv[] so that
	 * the alternatively executed program won't be affected by
	 * some dangerous environment variables like LD_PRELOAD.
	 *
	 * modified bprm->argc
	 *    = original bprm->argc + original bprm->envc + 7
	 * modified bprm->envc
	 *    = 0
	 *
	 * modified bprm->argv[0]
	 *    = the program's name specified by execute_handler
	 * modified bprm->argv[1]
	 *    = ccs_current_domain()->domainname->name
	 * modified bprm->argv[2]
	 *    = the current process's name
	 * modified bprm->argv[3]
	 *    = the current process's information (e.g. uid/gid).
	 * modified bprm->argv[4]
	 *    = original bprm->filename
	 * modified bprm->argv[5]
	 *    = original bprm->argc in string expression
	 * modified bprm->argv[6]
	 *    = original bprm->envc in string expression
	 * modified bprm->argv[7]
	 *    = original bprm->argv[0]
	 *  ...
	 * modified bprm->argv[bprm->argc + 6]
	 *     = original bprm->argv[bprm->argc - 1]
	 * modified bprm->argv[bprm->argc + 7]
	 *     = original bprm->envp[0]
	 *  ...
	 * modified bprm->argv[bprm->envc + bprm->argc + 6]
	 *     = original bprm->envp[bprm->envc - 1]
	 */
	struct linux_binprm *bprm = ee->bprm;
	struct file *filp;
	int retval;
	const int original_argc = bprm->argc;
	const int original_envc = bprm->envc;
	struct task_struct *task = current;

	/* Close the requested program's dentry. */
	ee->obj.path1.dentry = NULL;
	ee->obj.path1.mnt = NULL;
	ee->obj.validate_done = false;
	allow_write_access(bprm->file);
	fput(bprm->file);
	bprm->file = NULL;

	/* Invalidate page dump cache. */
	ee->dump.page = NULL;

	/* Move envp[] to argv[] */
	bprm->argc += bprm->envc;
	bprm->envc = 0;

	/* Set argv[6] */
	{
		char *cp = ee->tmp;
		snprintf(ee->tmp, CCS_EXEC_TMPSIZE - 1, "%d", original_envc);
		retval = copy_strings_kernel(1, &cp, bprm);
		if (retval < 0)
			goto out;
		bprm->argc++;
	}

	/* Set argv[5] */
	{
		char *cp = ee->tmp;
		snprintf(ee->tmp, CCS_EXEC_TMPSIZE - 1, "%d", original_argc);
		retval = copy_strings_kernel(1, &cp, bprm);
		if (retval < 0)
			goto out;
		bprm->argc++;
	}

	/* Set argv[4] */
	{
		retval = copy_strings_kernel(1, &bprm->filename, bprm);
		if (retval < 0)
			goto out;
		bprm->argc++;
	}

	/* Set argv[3] */
	{
		char *cp = ee->tmp;
		const u32 ccs_flags = task->ccs_flags;
		snprintf(ee->tmp, CCS_EXEC_TMPSIZE - 1,
			 "pid=%d uid=%d gid=%d euid=%d egid=%d suid=%d "
			 "sgid=%d fsuid=%d fsgid=%d state[0]=%u "
			 "state[1]=%u state[2]=%u",
			 (pid_t) ccsecurity_exports.sys_getpid(),
			 current_uid(), current_gid(), current_euid(),
			 current_egid(), current_suid(), current_sgid(),
			 current_fsuid(), current_fsgid(),
			 (u8) (ccs_flags >> 24), (u8) (ccs_flags >> 16),
			 (u8) (ccs_flags >> 8));
		retval = copy_strings_kernel(1, &cp, bprm);
		if (retval < 0)
			goto out;
		bprm->argc++;
	}

	/* Set argv[2] */
	{
		char *exe = (char *) ccs_get_exe();
		if (exe) {
			retval = copy_strings_kernel(1, &exe, bprm);
			kfree(exe);
		} else {
			exe = ee->tmp;
			snprintf(ee->tmp, CCS_EXEC_TMPSIZE - 1, "<unknown>");
			retval = copy_strings_kernel(1, &exe, bprm);
		}
		if (retval < 0)
			goto out;
		bprm->argc++;
	}

	/* Set argv[1] */
	{
		char *cp = ee->tmp;
		snprintf(ee->tmp, CCS_EXEC_TMPSIZE - 1, "%s",
			 ccs_current_domain()->domainname->name);
		retval = copy_strings_kernel(1, &cp, bprm);
		if (retval < 0)
			goto out;
		bprm->argc++;
	}

	/* Set argv[0] */
	{
		int depth = ccs_get_root_depth();
		int len = ee->handler->total_len + 1;
		char *cp = kmalloc(len, CCS_GFP_FLAGS);
		if (!cp) {
			retval = -ENOMEM;
			goto out;
		}
		ee->handler_path = cp;
		memmove(cp, ee->handler->name, len);
		ccs_unescape(cp);
		retval = -ENOENT;
		if (!*cp || *cp != '/')
			goto out;
		/* Adjust root directory for open_exec(). */
		while (depth) {
			cp = strchr(cp + 1, '/');
			if (!cp)
				goto out;
			depth--;
		}
		memmove(ee->handler_path, cp, strlen(cp) + 1);
		cp = ee->handler_path;
		retval = copy_strings_kernel(1, &cp, bprm);
		if (retval < 0)
			goto out;
		bprm->argc++;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 24)
	bprm->argv_len = bprm->exec - bprm->p;
#endif
#endif

	/*
	 * OK, now restart the process with execute handler program's dentry.
	 */
	filp = open_exec(ee->handler_path);
	if (IS_ERR(filp)) {
		retval = PTR_ERR(filp);
		goto out;
	}
	ee->obj.path1.dentry = filp->f_dentry;
	ee->obj.path1.mnt = filp->f_vfsmnt;
	bprm->file = filp;
	bprm->filename = ee->handler_path;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
	bprm->interp = bprm->filename;
#endif
	retval = prepare_binprm(bprm);
	if (retval < 0)
		goto out;
	task->ccs_flags |= CCS_DONT_SLEEP_ON_ENFORCE_ERROR;
	retval = ccs_find_next_domain(ee);
	task->ccs_flags &= ~CCS_DONT_SLEEP_ON_ENFORCE_ERROR;
 out:
	return retval;
}

/**
 * ccs_find_execute_handler - Find an execute handler.
 *
 * @ee:   Pointer to "struct ccs_execve".
 * @type: Type of execute handler.
 *
 * Returns true if found, false otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static bool ccs_find_execute_handler(struct ccs_execve *ee, const u8 type)
{
	struct ccs_request_info *r = &ee->r;
	const struct ccs_domain_info *domain = ccs_current_domain();
	struct ccs_acl_info *ptr;
	/*
	 * To avoid infinite execute handler loop, don't use execute handler
	 * if the current process is marked as execute handler .
	 */
	if (current->ccs_flags & CCS_TASK_IS_EXECUTE_HANDLER)
		return false;
 retry:
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct ccs_execute_handler *acl;
		if (ptr->type != type || !ccs_condition(r, ptr->cond))
			continue;
		acl = container_of(ptr, struct ccs_execute_handler, head);
		ee->handler = acl->handler;
		ee->handler_type = type;
		r->cond = ptr->cond;
		return true;
	}
	if (domain != &ccs_global_domain &&
	    !domain->flags[CCS_DIF_IGNORE_GLOBAL]) {
		domain = &ccs_global_domain;
		goto retry;
	}
	return false;
}

#ifdef CONFIG_MMU
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
#define CCS_BPRM_MMU
#elif defined(RHEL_MAJOR) && RHEL_MAJOR == 5 && defined(RHEL_MINOR) && RHEL_MINOR >= 3
#define CCS_BPRM_MMU
#elif defined(AX_MAJOR) && AX_MAJOR == 3 && defined(AX_MINOR) && AX_MINOR >= 2
#define CCS_BPRM_MMU
#endif
#endif

/**
 * ccs_dump_page - Dump a page to buffer.
 *
 * @bprm: Pointer to "struct linux_binprm".
 * @pos:  Location to dump.
 * @dump: Poiner to "struct ccs_page_dump".
 *
 * Returns true on success, false otherwise.
 */
bool ccs_dump_page(struct linux_binprm *bprm, unsigned long pos,
		   struct ccs_page_dump *dump)
{
	struct page *page;
	/* dump->data is released by ccs_finish_execve(). */
	if (!dump->data) {
		dump->data = kzalloc(PAGE_SIZE, CCS_GFP_FLAGS);
		if (!dump->data)
			return false;
	}
	/* Same with get_arg_page(bprm, pos, 0) in fs/exec.c */
#ifdef CCS_BPRM_MMU
	if (get_user_pages(current, bprm->mm, pos, 1, 0, 1, &page, NULL) <= 0)
		return false;
#else
	page = bprm->page[pos / PAGE_SIZE];
#endif
	if (page != dump->page) {
		const unsigned int offset = pos % PAGE_SIZE;
		/*
		 * Maybe kmap()/kunmap() should be used here.
		 * But remove_arg_zero() uses kmap_atomic()/kunmap_atomic().
		 * So do I.
		 */
		char *kaddr = kmap_atomic(page, KM_USER0);
		dump->page = page;
		memcpy(dump->data + offset, kaddr + offset,
		       PAGE_SIZE - offset);
		kunmap_atomic(kaddr, KM_USER0);
	}
	/* Same with put_arg_page(page) in fs/exec.c */
#ifdef CCS_BPRM_MMU
	put_page(page);
#endif
	return true;
}

/**
 * ccs_start_execve - Prepare for execve() operation.
 *
 * @bprm: Pointer to "struct linux_binprm".
 * @eep:  Pointer to "struct ccs_execve *".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_start_execve(struct linux_binprm *bprm,
			    struct ccs_execve **eep)
{
	int retval;
	struct task_struct *task = current;
	struct ccs_execve *ee;
	*eep = NULL;
	ee = kzalloc(sizeof(*ee), CCS_GFP_FLAGS);
	if (!ee)
		return -ENOMEM;
	ee->tmp = kzalloc(CCS_EXEC_TMPSIZE, CCS_GFP_FLAGS);
	if (!ee->tmp) {
		kfree(ee);
		return -ENOMEM;
	}
	ee->reader_idx = ccs_read_lock();
	/* ee->dump->data is allocated by ccs_dump_page(). */
	ee->previous_domain = task->ccs_domain_info;
	/* Clear manager flag. */
	task->ccs_flags &= ~CCS_TASK_IS_MANAGER;
	*eep = ee;
	ccs_init_request_info(&ee->r, CCS_MAC_FILE_EXECUTE);
	ee->r.ee = ee;
	ee->bprm = bprm;
	ee->r.obj = &ee->obj;
	ee->obj.path1.dentry = bprm->file->f_dentry;
	ee->obj.path1.mnt = bprm->file->f_vfsmnt;
	/*
	 * No need to call ccs_environ() for execute handler because envp[] is
	 * moved to argv[].
	 */
	if (ccs_find_execute_handler(ee, CCS_TYPE_EXECUTE_HANDLER))
		return ccs_try_alt_exec(ee);
	retval = ccs_find_next_domain(ee);
	if (retval == -EPERM) {
		if (ccs_find_execute_handler(ee,
					     CCS_TYPE_DENIED_EXECUTE_HANDLER))
			return ccs_try_alt_exec(ee);
	}
 	if (!retval)
		retval = ccs_environ(ee);
	return retval;
}

/**
 * ccs_finish_execve - Clean up execve() operation.
 *
 * @retval: Return code of an execve() operation.
 * @ee:     Pointer to "struct ccs_execve".
 *
 * Caller holds ccs_read_lock().
 */
static void ccs_finish_execve(int retval, struct ccs_execve *ee)
{
	struct task_struct *task = current;
	if (!ee)
		return;
	if (retval < 0) {
		task->ccs_domain_info = ee->previous_domain;
		/*
		 * Make task->ccs_domain_info visible to GC before changing
		 * task->ccs_flags .
		 */
		smp_mb();
	} else {
		/* Mark the current process as execute handler. */
		if (ee->handler)
			task->ccs_flags |= CCS_TASK_IS_EXECUTE_HANDLER;
		/* Mark the current process as normal process. */
		else
			task->ccs_flags &= ~CCS_TASK_IS_EXECUTE_HANDLER;
	}
	/* Tell GC that I finished execve(). */
	task->ccs_flags &= ~CCS_TASK_IS_IN_EXECVE;
	ccs_read_unlock(ee->reader_idx);
	kfree(ee->handler_path);
	kfree(ee->tmp);
	kfree(ee->dump.data);
	kfree(ee);
}

/**
 * ccs_may_transit - Check permission and do domain transition without execve().
 *
 * @domainname: Domainname to transit to.
 * @pathname: Pathname to check.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
int ccs_may_transit(const char *domainname, const char *pathname)
{
	struct ccs_path_info name;
	struct ccs_request_info r;
	struct ccs_domain_info *domain;
	int error;
	bool domain_created = false;
	name.name = pathname;
	ccs_fill_path_info(&name);
	/* Check "file transit" permission. */
	ccs_init_request_info(&r, CCS_MAC_FILE_TRANSIT);
	error = ccs_path_permission(&r, CCS_TYPE_TRANSIT, &name);
	if (error)
		return error;
	/* Check destination domain. */
	domain = ccs_find_domain(domainname);
	if (!domain && r.mode != CCS_CONFIG_ENFORCING &&
	    strlen(domainname) < CCS_EXEC_TMPSIZE - 10) {
		domain = ccs_assign_domain(domainname, r.profile);
		if (domain)
			domain_created = true;
	}
	if (domain) {
		error = 0;
		current->ccs_domain_info = domain;
		if (domain_created)
			ccs_audit_domain_creation_log();
	} else {
		error = -ENOENT;
	}
	return error;
}

static int __ccs_search_binary_handler(struct linux_binprm *bprm,
				       struct pt_regs *regs)
{
	struct ccs_execve *ee;
	int retval;
	if (!ccs_policy_loaded)
		ccsecurity_exports.load_policy(bprm->filename);
	retval = ccs_start_execve(bprm, &ee);
	if (!retval)
		retval = search_binary_handler(bprm, regs);
	ccs_finish_execve(retval, ee);
	return retval;
}

void __init ccs_domain_init(void)
{
	ccsecurity_ops.search_binary_handler = __ccs_search_binary_handler;
}
