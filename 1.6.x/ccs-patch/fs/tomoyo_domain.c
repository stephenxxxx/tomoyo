/*
 * fs/tomoyo_domain.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2008  NTT DATA CORPORATION
 *
 * Version: 1.6.0-pre   2008/03/11
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */
/***** TOMOYO Linux start. *****/

#include <linux/ccs_common.h>
#include <linux/tomoyo.h>
#include <linux/realpath.h>
#include <linux/highmem.h>
#include <linux/binfmts.h>

#ifndef for_each_process
#define for_each_process for_each_task
#endif

/*************************  VARIABLES  *************************/

/* The initial domain. */
struct domain_info KERNEL_DOMAIN;

/* List of domains. */
LIST1_HEAD(domain_list);

#ifdef CONFIG_TOMOYO

/* Lock for appending domain's ACL. */
DEFINE_MUTEX(domain_acl_lock);

/*************************  UTILITY FUNCTIONS  *************************/

/***** The structure for program files to force domain reconstruction. *****/

struct domain_initializer_entry {
	struct list1_head list;
	const struct path_info *domainname;    /* This may be NULL */
	const struct path_info *program;
	bool is_deleted;
	bool is_not;
	bool is_last_name;
};

/***** The structure for domains to not to transit domains. *****/

struct domain_keeper_entry {
	struct list1_head list;
	const struct path_info *domainname;
	const struct path_info *program;       /* This may be NULL */
	bool is_deleted;
	bool is_not;
	bool is_last_name;
};

/***** The structure for program files that should be aggregated. *****/

struct aggregator_entry {
	struct list1_head list;
	const struct path_info *original_name;
	const struct path_info *aggregated_name;
	bool is_deleted;
};

/***** The structure for program files that should be aliased. *****/

struct alias_entry {
	struct list1_head list;
	const struct path_info *original_name;
	const struct path_info *aliased_name;
	bool is_deleted;
};

/*************************  VARIABLES  *************************/

/* Domain creation lock. */
static DEFINE_MUTEX(new_domain_assign_lock);

/*************************  UTILITY FUNCTIONS  *************************/

void SetDomainFlag(struct domain_info *domain, const bool is_delete, const u8 flags)
{
	mutex_lock(&new_domain_assign_lock);
	if (!is_delete) domain->flags |= flags;
	else domain->flags &= ~flags;
	mutex_unlock(&new_domain_assign_lock);
}

const char *GetLastName(const struct domain_info *domain)
{
	const char *cp0 = domain->domainname->name, *cp1;
	if ((cp1 = strrchr(cp0, ' ')) != NULL) return cp1 + 1;
	return cp0;
}

int AddDomainACL(struct domain_info *domain, struct acl_info *acl)
{
	if (domain) list1_add_tail_mb(&acl->list, &domain->acl_info_list);
	else acl->type &= ~ACL_DELETED;
	UpdateCounter(CCS_UPDATES_COUNTER_DOMAIN_POLICY);
	return 0;
}

int DelDomainACL(struct acl_info *acl)
{
	if (acl) acl->type |= ACL_DELETED;
	UpdateCounter(CCS_UPDATES_COUNTER_DOMAIN_POLICY);
	return 0;
}

/*************************  DOMAIN INITIALIZER HANDLER  *************************/

static LIST1_HEAD(domain_initializer_list);

static int AddDomainInitializerEntry(const char *domainname, const char *program, const bool is_not, const bool is_delete)
{
	struct domain_initializer_entry *new_entry, *ptr;
	static DEFINE_MUTEX(lock);
	const struct path_info *saved_program, *saved_domainname = NULL;
	int error = -ENOMEM;
	bool is_last_name = false;
	if (!IsCorrectPath(program, 1, -1, -1, __FUNCTION__)) return -EINVAL; /* No patterns allowed. */
	if (domainname) {
		if (!IsDomainDef(domainname) && IsCorrectPath(domainname, 1, -1, -1, __FUNCTION__)) {
			is_last_name = true;
		} else if (!IsCorrectDomain(domainname, __FUNCTION__)) {
			return -EINVAL;
		}
		if ((saved_domainname = SaveName(domainname)) == NULL) return -ENOMEM;
	}
	if ((saved_program = SaveName(program)) == NULL) return -ENOMEM;
	mutex_lock(&lock);
	list1_for_each_entry(ptr, &domain_initializer_list, list) {
		if (ptr->is_not == is_not && ptr->domainname == saved_domainname && ptr->program == saved_program) {
			ptr->is_deleted = is_delete;
			error = 0;
			goto out;
		}
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	if ((new_entry = alloc_element(sizeof(*new_entry))) == NULL) goto out;
	new_entry->domainname = saved_domainname;
	new_entry->program = saved_program;
	new_entry->is_not = is_not;
	new_entry->is_last_name = is_last_name;
	list1_add_tail_mb(&new_entry->list, &domain_initializer_list);
	error = 0;
 out:
	mutex_unlock(&lock);
	return error;
}

int ReadDomainInitializerPolicy(struct io_buffer *head)
{
	struct list1_head *pos;
	list1_for_each_cookie(pos, head->read_var2, &domain_initializer_list) {
		struct domain_initializer_entry *ptr;
		ptr = list1_entry(pos, struct domain_initializer_entry, list);
		if (ptr->is_deleted) continue;
		if (ptr->domainname) {
			if (io_printf(head, "%s" KEYWORD_INITIALIZE_DOMAIN "%s from %s\n", ptr->is_not ? "no_" : "", ptr->program->name, ptr->domainname->name)) return -ENOMEM;
		} else {
			if (io_printf(head, "%s" KEYWORD_INITIALIZE_DOMAIN "%s\n", ptr->is_not ? "no_" : "", ptr->program->name)) return -ENOMEM;
		}
	}
	return 0;
}

int AddDomainInitializerPolicy(char *data, const bool is_not, const bool is_delete)
{
	char *cp = strstr(data, " from ");
	if (cp) {
		*cp = '\0';
		return AddDomainInitializerEntry(cp + 6, data, is_not, is_delete);
	} else {
		return AddDomainInitializerEntry(NULL, data, is_not, is_delete);
	}
}

static bool IsDomainInitializer(const struct path_info *domainname, const struct path_info *program, const struct path_info *last_name)
{
	struct domain_initializer_entry *ptr;
	bool flag = false;
	list1_for_each_entry(ptr,  &domain_initializer_list, list) {
		if (ptr->is_deleted) continue;
		if (ptr->domainname) {
			if (!ptr->is_last_name) {
				if (ptr->domainname != domainname) continue;
			} else {
				if (pathcmp(ptr->domainname, last_name)) continue;
			}
		}
		if (pathcmp(ptr->program, program)) continue;
		if (ptr->is_not) return false;
		flag = true;
	}
	return flag;
}

/*************************  DOMAIN KEEPER HANDLER  *************************/

static LIST1_HEAD(domain_keeper_list);

static int AddDomainKeeperEntry(const char *domainname, const char *program, const bool is_not, const bool is_delete)
{
	struct domain_keeper_entry *new_entry, *ptr;
	const struct path_info *saved_domainname, *saved_program = NULL;
	static DEFINE_MUTEX(lock);
	int error = -ENOMEM;
	bool is_last_name = false;
	if (!IsDomainDef(domainname) && IsCorrectPath(domainname, 1, -1, -1, __FUNCTION__)) {
		is_last_name = true;
	} else if (!IsCorrectDomain(domainname, __FUNCTION__)) {
		return -EINVAL;
	}
	if (program) {
		if (!IsCorrectPath(program, 1, -1, -1, __FUNCTION__)) return -EINVAL;
		if ((saved_program = SaveName(program)) == NULL) return -ENOMEM;
	}
	if ((saved_domainname = SaveName(domainname)) == NULL) return -ENOMEM;
	mutex_lock(&lock);
	list1_for_each_entry(ptr, &domain_keeper_list, list) {
		if (ptr->is_not == is_not && ptr->domainname == saved_domainname && ptr->program == saved_program) {
			ptr->is_deleted = is_delete;
			error = 0;
			goto out;
		}
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	if ((new_entry = alloc_element(sizeof(*new_entry))) == NULL) goto out;
	new_entry->domainname = saved_domainname;
	new_entry->program = saved_program;
	new_entry->is_not = is_not;
	new_entry->is_last_name = is_last_name;
	list1_add_tail_mb(&new_entry->list, &domain_keeper_list);
	error = 0;
 out:
	mutex_unlock(&lock);
	return error;
}

int AddDomainKeeperPolicy(char *data, const bool is_not, const bool is_delete)
{
	char *cp = strstr(data, " from ");
	if (cp) {
		*cp = '\0';
		return AddDomainKeeperEntry(cp + 6, data, is_not, is_delete);
	} else {
		return AddDomainKeeperEntry(data, NULL, is_not, is_delete);
	}
}

int ReadDomainKeeperPolicy(struct io_buffer *head)
{
	struct list1_head *pos;
	list1_for_each_cookie(pos, head->read_var2, &domain_keeper_list) {
		struct domain_keeper_entry *ptr;
		ptr = list1_entry(pos, struct domain_keeper_entry, list);
		if (ptr->is_deleted) continue;
		if (ptr->program) {
			if (io_printf(head, "%s" KEYWORD_KEEP_DOMAIN "%s from %s\n", ptr->is_not ? "no_" : "", ptr->program->name, ptr->domainname->name)) return -ENOMEM;
		} else {
			if (io_printf(head, "%s" KEYWORD_KEEP_DOMAIN "%s\n", ptr->is_not ? "no_" : "", ptr->domainname->name)) return -ENOMEM;
		}
	}
	return 0;
}

static bool IsDomainKeeper(const struct path_info *domainname, const struct path_info *program, const struct path_info *last_name)
{
	struct domain_keeper_entry *ptr;
	bool flag = false;
	list1_for_each_entry(ptr, &domain_keeper_list, list) {
		if (ptr->is_deleted) continue;
		if (!ptr->is_last_name) {
			if (ptr->domainname != domainname) continue;
		} else {
			if (pathcmp(ptr->domainname, last_name)) continue;
		}
		if (ptr->program && pathcmp(ptr->program, program)) continue;
		if (ptr->is_not) return false;
		flag = true;
	}
	return flag;
}

/*************************  SYMBOLIC LINKED PROGRAM HANDLER  *************************/

static LIST1_HEAD(alias_list);

static int AddAliasEntry(const char *original_name, const char *aliased_name, const bool is_delete)
{
	struct alias_entry *new_entry, *ptr;
	static DEFINE_MUTEX(lock);
	const struct path_info *saved_original_name, *saved_aliased_name;
	int error = -ENOMEM;
	if (!IsCorrectPath(original_name, 1, -1, -1, __FUNCTION__) || !IsCorrectPath(aliased_name, 1, -1, -1, __FUNCTION__)) return -EINVAL; /* No patterns allowed. */
	if ((saved_original_name = SaveName(original_name)) == NULL || (saved_aliased_name = SaveName(aliased_name)) == NULL) return -ENOMEM;
	mutex_lock(&lock);
	list1_for_each_entry(ptr, &alias_list, list) {
		if (ptr->original_name == saved_original_name && ptr->aliased_name == saved_aliased_name) {
			ptr->is_deleted = is_delete;
			error = 0;
			goto out;
		}
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	if ((new_entry = alloc_element(sizeof(*new_entry))) == NULL) goto out;
	new_entry->original_name = saved_original_name;
	new_entry->aliased_name = saved_aliased_name;
	list1_add_tail_mb(&new_entry->list, &alias_list);
	error = 0;
 out:
	mutex_unlock(&lock);
	return error;
}

int ReadAliasPolicy(struct io_buffer *head)
{
	struct list1_head *pos;
	list1_for_each_cookie(pos, head->read_var2, &alias_list) {
		struct alias_entry *ptr;
		ptr = list1_entry(pos, struct alias_entry, list);
		if (ptr->is_deleted) continue;
		if (io_printf(head, KEYWORD_ALIAS "%s %s\n", ptr->original_name->name, ptr->aliased_name->name)) return -ENOMEM;
	}
	return 0;
}

int AddAliasPolicy(char *data, const bool is_delete)
{
	char *cp = strchr(data, ' ');
	if (!cp) return -EINVAL;
	*cp++ = '\0';
	return AddAliasEntry(data, cp, is_delete);
}

/*************************  DOMAIN AGGREGATOR HANDLER  *************************/

static LIST1_HEAD(aggregator_list);

static int AddAggregatorEntry(const char *original_name, const char *aggregated_name, const bool is_delete)
{
	struct aggregator_entry *new_entry, *ptr;
	static DEFINE_MUTEX(lock);
	const struct path_info *saved_original_name, *saved_aggregated_name;
	int error = -ENOMEM;
	if (!IsCorrectPath(original_name, 1, 0, -1, __FUNCTION__) || !IsCorrectPath(aggregated_name, 1, -1, -1, __FUNCTION__)) return -EINVAL;
	if ((saved_original_name = SaveName(original_name)) == NULL || (saved_aggregated_name = SaveName(aggregated_name)) == NULL) return -ENOMEM;
	mutex_lock(&lock);
	list1_for_each_entry(ptr, &aggregator_list, list) {
		if (ptr->original_name == saved_original_name && ptr->aggregated_name == saved_aggregated_name) {
			ptr->is_deleted = is_delete;
			error = 0;
			goto out;
		}
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	if ((new_entry = alloc_element(sizeof(*new_entry))) == NULL) goto out;
	new_entry->original_name = saved_original_name;
	new_entry->aggregated_name = saved_aggregated_name;
	list1_add_tail_mb(&new_entry->list, &aggregator_list);
	error = 0;
 out:
	mutex_unlock(&lock);
	return error;
}

int ReadAggregatorPolicy(struct io_buffer *head)
{
	struct list1_head *pos;
	list1_for_each_cookie(pos, head->read_var2, &aggregator_list) {
		struct aggregator_entry *ptr;
		ptr = list1_entry(pos, struct aggregator_entry, list);
		if (ptr->is_deleted) continue;
		if (io_printf(head, KEYWORD_AGGREGATOR "%s %s\n", ptr->original_name->name, ptr->aggregated_name->name)) return -ENOMEM;
	}
	return 0;
}

int AddAggregatorPolicy(char *data, const bool is_delete)
{
	char *cp = strchr(data, ' ');
	if (!cp) return -EINVAL;
	*cp++ = '\0';
	return AddAggregatorEntry(data, cp, is_delete);
}

/*************************  DOMAIN DELETION HANDLER  *************************/

/* #define DEBUG_DOMAIN_UNDELETE */

int DeleteDomain(char *domainname0)
{
	struct domain_info *domain;
	struct path_info domainname;
	domainname.name = domainname0;
	fill_path_info(&domainname);
	mutex_lock(&new_domain_assign_lock);
#ifdef DEBUG_DOMAIN_UNDELETE
	printk("DeleteDomain %s\n", domainname0);
	list1_for_each_entry(domain, &domain_list, list) {
		if (pathcmp(domain->domainname, &domainname)) continue;
		printk("List: %p %u\n", domain, domain->is_deleted);
	}
#endif
	/* Is there an active domain? */
	list1_for_each_entry(domain, &domain_list, list) {
		struct domain_info *domain2;
		/* Never delete KERNEL_DOMAIN */
		if (domain == &KERNEL_DOMAIN || domain->is_deleted || pathcmp(domain->domainname, &domainname)) continue;
		/* Mark already deleted domains as non undeletable. */
		list1_for_each_entry(domain2, &domain_list, list) {
			if (!domain2->is_deleted || pathcmp(domain2->domainname, &domainname)) continue;
#ifdef DEBUG_DOMAIN_UNDELETE
			if (domain2->is_deleted != 255) printk("Marked %p as non undeletable\n", domain2);
#endif
			domain2->is_deleted = 255;
		}
		/* Delete and mark active domain as undeletable. */
		domain->is_deleted = 1;
#ifdef DEBUG_DOMAIN_UNDELETE
		printk("Marked %p as undeletable\n", domain);
#endif
		break;
	}
	mutex_unlock(&new_domain_assign_lock);
	return 0;
}

struct domain_info *UndeleteDomain(const char *domainname0)
{
	struct domain_info *domain, *candidate_domain = NULL;
	struct path_info domainname;
	domainname.name = domainname0;
	fill_path_info(&domainname);
	mutex_lock(&new_domain_assign_lock);
#ifdef DEBUG_DOMAIN_UNDELETE
	printk("UndeleteDomain %s\n", domainname0);
	list1_for_each_entry(domain, &domain_list, list) {
		if (pathcmp(domain->domainname, &domainname)) continue;
		printk("List: %p %u\n", domain, domain->is_deleted);
	}
#endif
	list1_for_each_entry(domain, &domain_list, list) {
		if (pathcmp(&domainname, domain->domainname)) continue;
		if (!domain->is_deleted) {
			/* This domain is active. I can't undelete. */
			candidate_domain = NULL;
#ifdef DEBUG_DOMAIN_UNDELETE
			printk("%p is active. I can't undelete.\n", domain);
#endif
			break;
		}
		/* Is this domain undeletable? */
		if (domain->is_deleted == 1) candidate_domain = domain;
	}
	if (candidate_domain) {
		candidate_domain->is_deleted = 0;
#ifdef DEBUG_DOMAIN_UNDELETE
		printk("%p was undeleted.\n", candidate_domain);
#endif
	}
	mutex_unlock(&new_domain_assign_lock);
	return candidate_domain;
}

/*************************  DOMAIN TRANSITION HANDLER  *************************/

struct domain_info *FindOrAssignNewDomain(const char *domainname, const u8 profile)
{
	struct domain_info *domain = NULL;
	const struct path_info *saved_domainname;
	mutex_lock(&new_domain_assign_lock);
	if ((domain = FindDomain(domainname)) != NULL) goto out;
	if (!IsCorrectDomain(domainname, __FUNCTION__)) goto out;
	if ((saved_domainname = SaveName(domainname)) == NULL) goto out;
	/* Can I reuse memory of deleted domain? */
	list1_for_each_entry(domain, &domain_list, list) {
		struct task_struct *p;
		struct acl_info *ptr;
		bool flag;
		if (!domain->is_deleted || domain->domainname != saved_domainname) continue;
		flag = false;
		/***** CRITICAL SECTION START *****/
		read_lock(&tasklist_lock);
		for_each_process(p) {
			if (p->domain_info == domain) { flag = true; break; }
		}
		read_unlock(&tasklist_lock);
		/***** CRITICAL SECTION END *****/
		if (flag) continue;
#ifdef DEBUG_DOMAIN_UNDELETE
		printk("Reusing %p %s\n", domain, domain->domainname->name);
#endif
		list1_for_each_entry(ptr, &domain->acl_info_list, list) {
			ptr->type |= ACL_DELETED;
		}
		domain->flags = 0;
		domain->profile = profile;
		domain->quota_warned = false;
		mb(); /* Avoid out-of-order execution. */
		domain->is_deleted = 0;
		goto out;
	}
	/* No memory reusable. Create using new memory. */
	if ((domain = alloc_element(sizeof(*domain))) != NULL) {
		INIT_LIST1_HEAD(&domain->acl_info_list);
		domain->domainname = saved_domainname;
		domain->profile = profile;
		list1_add_tail_mb(&domain->list, &domain_list);
	}
 out: ;
	mutex_unlock(&new_domain_assign_lock);
	return domain;
}

static bool get_argv0(struct linux_binprm *bprm, struct ccs_page_buffer *tmp)
{
	char *arg_ptr = tmp->buffer;
	int arg_len = 0;
	unsigned long pos = bprm->p;
	int i = pos / PAGE_SIZE, offset = pos % PAGE_SIZE;
	bool done = false;
	if (!bprm->argc) goto out;
	while (1) {
		struct page *page;
		const char *kaddr;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) && defined(CONFIG_MMU)
		if (get_user_pages(current, bprm->mm, pos, 1, 0, 1, &page, NULL) <= 0) goto out;
		pos += PAGE_SIZE - offset;
#else
		page = bprm->page[i];
#endif
		/* Map. */
		kaddr = kmap(page);
		if (!kaddr) { /* Mapping failed. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) && defined(CONFIG_MMU)
			put_page(page);
#endif
			goto out;
		}
		/* Read. */
		while (offset < PAGE_SIZE) {
			const unsigned char c = kaddr[offset++];
			if (c && arg_len < CCS_MAX_PATHNAME_LEN - 10) {
				if (c == '\\') {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = '\\';
				} else if (c == '/') {
					arg_len = 0;
				} else if (c > ' ' && c < 127) {
					arg_ptr[arg_len++] = c;
				} else {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = (c >> 6) + '0';
					arg_ptr[arg_len++] = ((c >> 3) & 7) + '0';
					arg_ptr[arg_len++] = (c & 7) + '0';
				}
			} else {
				arg_ptr[arg_len] = '\0';
				done = true;
				break;
			}
		}
		/* Unmap. */
		kunmap(page);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) && defined(CONFIG_MMU)
		put_page(page);
#endif
		i++;
		offset = 0;
		if (done) break;
	}
	return true;
 out:
	return false;
}

static int FindNextDomain(struct linux_binprm *bprm, struct domain_info **next_domain, const struct path_info *path_to_verify, struct ccs_page_buffer *tmp)
{
	/* This function assumes that the size of buffer returned by realpath() = CCS_MAX_PATHNAME_LEN. */
	struct domain_info *old_domain = current->domain_info, *domain = NULL;
	const char *old_domain_name = old_domain->domainname->name;
	const char *original_name = bprm->filename;
	char *new_domain_name = NULL;
	char *real_program_name = NULL, *symlink_program_name = NULL;
	const bool is_enforce = (CheckCCSFlags(CCS_TOMOYO_MAC_FOR_FILE) == 3);
	int retval;
	struct path_info r, s, l;

	{
		/*
		 * Built-in initializers. This is needed because policies are not loaded until starting /sbin/init .
		 */
		static bool first = true;
		if (first) {
			AddDomainInitializerEntry(NULL, "/sbin/hotplug", 0, 0);
			AddDomainInitializerEntry(NULL, "/sbin/modprobe", 0, 0);
			first = false;
		}
	}

	/* Get realpath of program. */
	retval = -ENOENT; /* I hope realpath() won't fail with -ENOMEM. */
	if ((real_program_name = realpath(original_name)) == NULL) goto out;
	/* Get realpath of symbolic link. */
	if ((symlink_program_name = realpath_nofollow(original_name)) == NULL) goto out;

	r.name = real_program_name;
	fill_path_info(&r);
	s.name = symlink_program_name;
	fill_path_info(&s);
	if ((l.name = strrchr(old_domain_name, ' ')) != NULL) l.name++;
	else l.name = old_domain_name;
	fill_path_info(&l);

	if (path_to_verify) {
		if (pathcmp(&r, path_to_verify)) {
			static u8 counter = 20;
			if (counter) {
				counter--;
				printk("Failed to verify: %s\n", path_to_verify->name);
			}
			goto out;
		}
		goto ok;
	}

	/* Check 'alias' directive. */
	if (pathcmp(&r, &s)) {
		struct alias_entry *ptr;
		/* Is this program allowed to be called via symbolic links? */
		list1_for_each_entry(ptr, &alias_list, list) {
			if (ptr->is_deleted || pathcmp(&r, ptr->original_name) || pathcmp(&s, ptr->aliased_name)) continue;
			memset(real_program_name, 0, CCS_MAX_PATHNAME_LEN);
			strncpy(real_program_name, ptr->aliased_name->name, CCS_MAX_PATHNAME_LEN - 1);
			fill_path_info(&r);
			break;
		}
	}
	
	/* Compare basename of real_program_name and argv[0] */
	if (bprm->argc > 0 && CheckCCSFlags(CCS_TOMOYO_MAC_FOR_ARGV0)) {
		char *base_argv0 = tmp->buffer;
		const char *base_filename;
		retval = -ENOMEM;
		if (!get_argv0(bprm, tmp)) goto out;
		if ((base_filename = strrchr(real_program_name, '/')) == NULL) base_filename = real_program_name; else base_filename++;
		if (strcmp(base_argv0, base_filename)) {
			retval = CheckArgv0Perm(&r, base_argv0);
			if (retval) goto out;
		}
	}
	
	/* Check 'aggregator' directive. */
	{
		struct aggregator_entry *ptr;
		/* Is this program allowed to be aggregated? */
		list1_for_each_entry(ptr, &aggregator_list, list) {
			if (ptr->is_deleted || !PathMatchesToPattern(&r, ptr->original_name)) continue;
			memset(real_program_name, 0, CCS_MAX_PATHNAME_LEN);
			strncpy(real_program_name, ptr->aggregated_name->name, CCS_MAX_PATHNAME_LEN - 1);
			fill_path_info(&r);
			break;
		}
	}

	/* Check execute permission. */
	if ((retval = CheckExecPerm(&r, bprm, tmp)) < 0) goto out;

 ok: ;
	new_domain_name = tmp->buffer;
	if (IsDomainInitializer(old_domain->domainname, &r, &l)) {
		/* Transit to the child of KERNEL_DOMAIN domain. */
		snprintf(new_domain_name, CCS_MAX_PATHNAME_LEN + 1, ROOT_NAME " " "%s", real_program_name);
	} else if (old_domain == &KERNEL_DOMAIN && !sbin_init_started) {
		/*
		 * Needn't to transit from kernel domain before starting /sbin/init .
		 * But transit from kernel domain if executing initializers, for they might start before /sbin/init .
		 */
		domain = old_domain;
	} else if (IsDomainKeeper(old_domain->domainname, &r, &l)) {
		/* Keep current domain. */
		domain = old_domain;
	} else {
		/* Normal domain transition. */
		snprintf(new_domain_name, CCS_MAX_PATHNAME_LEN + 1, "%s %s", old_domain_name, real_program_name);
	}
	if (!domain && strlen(new_domain_name) < CCS_MAX_PATHNAME_LEN) {
		if (is_enforce) {
			domain = FindDomain(new_domain_name);
			if (!domain) if (CheckSupervisor("#Need to create domain\n%s\n", new_domain_name) == 0) domain = FindOrAssignNewDomain(new_domain_name, current->domain_info->profile);
		} else {
			domain = FindOrAssignNewDomain(new_domain_name, current->domain_info->profile);
		}
	}
	if (!domain) {
		printk("TOMOYO-ERROR: Domain '%s' not defined.\n", new_domain_name);
		if (is_enforce) retval = -EPERM;
	} else {
		retval = 0;
	}
 out: ;
	ccs_free(real_program_name);
	ccs_free(symlink_program_name);
	*next_domain = domain ? domain : old_domain;
	return retval;
}

static int CheckEnviron(struct linux_binprm *bprm, struct ccs_page_buffer *tmp)
{
	const u8 profile = current->domain_info->profile;
	const u8 mode = CheckCCSFlags(CCS_TOMOYO_MAC_FOR_ENV);
	char *arg_ptr = tmp->buffer;
	int arg_len = 0;
	unsigned long pos = bprm->p;
	int i = pos / PAGE_SIZE, offset = pos % PAGE_SIZE;
	int argv_count = bprm->argc;
	int envp_count = bprm->envc;
	//printk("start %d %d\n", argv_count, envp_count);
	int error = -ENOMEM;
	if (!mode || !envp_count) return 0;
	while (error == -ENOMEM) {
		struct page *page;
		const char *kaddr;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) && defined(CONFIG_MMU)
		if (get_user_pages(current, bprm->mm, pos, 1, 0, 1, &page, NULL) <= 0) goto out;
		pos += PAGE_SIZE - offset;
#else
		page = bprm->page[i];
#endif
		/* Map. */
		kaddr = kmap(page);
		if (!kaddr) { /* Mapping failed. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) && defined(CONFIG_MMU)
			put_page(page);
#endif
			goto out;
		}
		/* Read. */
		while (argv_count && offset < PAGE_SIZE) {
			if (!kaddr[offset++]) argv_count--;
		}
		if (argv_count) goto unmap_page;
		while (offset < PAGE_SIZE) {
			const unsigned char c = kaddr[offset++];
			if (c && arg_len < CCS_MAX_PATHNAME_LEN - 10) {
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
					arg_ptr[arg_len++] = ((c >> 3) & 7) + '0';
					arg_ptr[arg_len++] = (c & 7) + '0';
				}
			} else {
				arg_ptr[arg_len] = '\0';
			}
			if (c) continue;
			if (CheckEnvPerm(arg_ptr, profile, mode)) {
				error = -EPERM;
				break;
			}
			if (!--envp_count) {
				error = 0;
				break;
			}
			arg_len = 0;
		}
	unmap_page:
		/* Unmap. */
		kunmap(page);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) && defined(CONFIG_MMU)
		put_page(page);
#endif
		i++;
		offset = 0;
	}
 out:
	if (error && mode != 3) error = 0;
	return error;
}

static void UnEscape(unsigned char *dest)
{
	unsigned char *src = dest;
	unsigned char c, d, e;
	while ((c = *src++) != '\0') {
		if (c != '\\') {
			*dest++ = c;
			continue;
		}
		c = *src++;
		if (c == '\\') {
			*dest++ = c;
		} else if (c >= '0' && c <= '3' &&
			   (d = *src++) >= '0' && d <= '7' &&
			   (e = *src++) >= '0' && e <= '7') {
			*dest++ = ((c - '0') << 6) | ((d - '0') << 3) | (e - '0');
		} else {
			break;
		}
	}
	*dest = '\0';
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#include <linux/namei.h>
#include <linux/mount.h>
#endif

/*
 * GetRootDepth - return the depth of root directory.
 */
static int GetRootDepth(void)
{
	int depth = 0;
	struct dentry *dentry;
	struct vfsmount *vfsmnt;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	struct path root;
#endif
	read_lock(&current->fs->lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
        root = current->fs->root;
        path_get(&current->fs->root);
	dentry = root.dentry;
	vfsmnt = root.mnt;
#else
	dentry = dget(current->fs->root);
	vfsmnt = mntget(current->fs->rootmnt);
#endif
	read_unlock(&current->fs->lock);
	/***** CRITICAL SECTION START *****/
	spin_lock(&dcache_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	spin_lock(&vfsmount_lock);
#endif
	for (;;) {
		if (dentry == vfsmnt->mnt_root || IS_ROOT(dentry)) {
			/* Global root? */
			if (vfsmnt->mnt_parent == vfsmnt) break;
			dentry = vfsmnt->mnt_mountpoint;
			vfsmnt = vfsmnt->mnt_parent;
			continue;
		}
		dentry = dentry->d_parent;
		depth++;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	spin_unlock(&vfsmount_lock);
#endif
	spin_unlock(&dcache_lock);
	/***** CRITICAL SECTION END *****/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	path_put(&root);
#else
	dput(dentry);
	mntput(vfsmnt);
#endif
	return depth;
}

static int try_alt_exec(struct linux_binprm *bprm, const struct path_info *filename, char **work, struct domain_info **next_domain, struct ccs_page_buffer *tmp)
{
	/*
	 * Contents of modified bprm.
	 * The envp[] in original bprm is moved to argv[] so that
	 * the alternatively executed program won't be affected by
	 * some dangerous environment variables like LD_PRELOAD .
	 *
	 * modified bprm->argc
	 *    = original bprm->argc + original bprm->envc + 7
	 * modified bprm->envc
	 *    = 0
	 *
	 * modified bprm->argv[0]
	 *    = the program's name specified by execute_handler
	 * modified bprm->argv[1]
	 *    = current->domain_info->domainname->name
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
	struct file *filp;
	int retval;
	const int original_argc = bprm->argc;
	const int original_envc = bprm->envc;
	struct task_struct *task = current;
       	char *buffer = tmp->buffer;
	/* Allocate memory for execute handler's pathname. */
	char *execute_handler = ccs_alloc(sizeof(struct ccs_page_buffer));
	*work = execute_handler;
	if (!execute_handler) return -ENOMEM;
	strncpy(execute_handler, filename->name, sizeof(struct ccs_page_buffer) - 1);
	UnEscape(execute_handler);
	
	/* Close the requested program's dentry. */
	allow_write_access(bprm->file);
	fput(bprm->file);
	bprm->file = NULL;

	{ /* Adjust root directory for open_exec(). */
		int depth = GetRootDepth();
		char *cp = execute_handler;
		if (!*cp || *cp != '/') return -ENOENT;
		while (depth) {
			cp = strchr(cp + 1, '/');
			if (!cp) return -ENOENT;
			depth--;
		}
		memmove(execute_handler, cp, strlen(cp) + 1);
	}

	/* Move envp[] to argv[] */
	bprm->argc += bprm->envc;
	bprm->envc = 0;

	/* Set argv[6] */
	{
		snprintf(buffer, sizeof(struct ccs_page_buffer) - 1, "%d", original_envc);
		retval = copy_strings_kernel(1, &buffer, bprm);
		if (retval < 0) goto out;
		bprm->argc++;
	}

	/* Set argv[5] */
	{
		snprintf(buffer, sizeof(struct ccs_page_buffer) - 1, "%d", original_argc);
		retval = copy_strings_kernel(1, &buffer, bprm);
		if (retval < 0) goto out;
		bprm->argc++;
	}

	/* Set argv[4] */
	{
		retval = copy_strings_kernel(1, &bprm->filename, bprm);
		if (retval < 0) goto out;
		bprm->argc++;
	}

	/* Set argv[3] */
	{
		const u32 tomoyo_flags = task->tomoyo_flags;
		snprintf(buffer, sizeof(struct ccs_page_buffer) - 1, "pid=%d uid=%d gid=%d euid=%d egid=%d suid=%d sgid=%d fsuid=%d fsgid=%d state[0]=%u state[1]=%u state[2]=%u", task->pid, task->uid, task->gid, task->euid, task->egid, task->suid, task->sgid, task->fsuid, task->fsgid, (u8) (tomoyo_flags >> 24), (u8) (tomoyo_flags >> 16), (u8) (tomoyo_flags >> 8));
		retval = copy_strings_kernel(1, &buffer, bprm);
		if (retval < 0) goto out;
		bprm->argc++;
	}

	/* Set argv[2] */
	{
		char *exe = (char *) GetEXE();
		if (exe) {
			retval = copy_strings_kernel(1, &exe, bprm);
			ccs_free(exe);
		} else {
			snprintf(buffer, sizeof(struct ccs_page_buffer) - 1, "<unknown>");
			retval = copy_strings_kernel(1, &buffer, bprm);
		}
		if (retval < 0) goto out;
		bprm->argc++;
	}

	/* Set argv[1] */
	{
		strncpy(buffer, task->domain_info->domainname->name, sizeof(struct ccs_page_buffer) - 1);
		retval = copy_strings_kernel(1, &buffer, bprm);
		if (retval < 0) goto out;
		bprm->argc++;
	}

	/* Set argv[0] */
	{
		retval = copy_strings_kernel(1, &execute_handler, bprm);
		if (retval < 0) goto out;
		bprm->argc++;
	}
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,23) || LINUX_VERSION_CODE == KERNEL_VERSION(2,6,24)
	bprm->argv_len = bprm->exec - bprm->p;
#endif

	/* OK, now restart the process with execute handler program's dentry. */
	filp = open_exec(execute_handler);
	if (IS_ERR(filp)) {
		retval = PTR_ERR(filp);
		goto out;
	}
	bprm->file= filp;
	bprm->filename = execute_handler;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	bprm->interp = execute_handler;
#endif
	retval = prepare_binprm(bprm);
	if (retval < 0) goto out;
	task->tomoyo_flags |= CCS_DONT_SLEEP_ON_ENFORCE_ERROR;
	retval = FindNextDomain(bprm, next_domain, filename, tmp);
	task->tomoyo_flags &= ~CCS_DONT_SLEEP_ON_ENFORCE_ERROR;
 out:
	return retval;
}

static const struct path_info *FindExecuteHandler(bool is_preferred_handler)
{
	const struct domain_info *domain = current->domain_info;
	struct acl_info *ptr;
	const u8 type = is_preferred_handler ? TYPE_PREFERRED_EXECUTE_HANDLER : TYPE_DEFAULT_EXECUTE_HANDLER;
	list1_for_each_entry(ptr, &domain->acl_info_list, list) {
		struct execute_handler_record *acl;
		if (ptr->type != type) continue;
		acl = container_of(ptr, struct execute_handler_record, head);
		return acl->handler;
	}
	return NULL;
}

int search_binary_handler_with_transition(struct linux_binprm *bprm, struct pt_regs *regs)
{
	struct task_struct *task = current;
	struct domain_info *next_domain = NULL, *prev_domain = task->domain_info;
	const struct path_info *handler;
 	int retval;
	char *work = NULL; /* Keep valid until search_binary_handler() finishes. */
	struct ccs_page_buffer *buf = ccs_alloc(sizeof(struct ccs_page_buffer));
	CCS_LoadPolicy(bprm->filename);
	if (!buf) return -ENOMEM;
	//printk("rootdepth=%d\n", GetRootDepth());
	handler = FindExecuteHandler(true);
	if (handler) {
		retval = try_alt_exec(bprm, handler, &work, &next_domain, buf);
	} else if ((retval = FindNextDomain(bprm, &next_domain, NULL, buf)) == -EPERM) {
		handler = FindExecuteHandler(false);
		if (handler) retval = try_alt_exec(bprm, handler, &work, &next_domain, buf);
	}
	if (retval) goto out;
	task->domain_info = next_domain;
	retval = CheckEnviron(bprm, buf);
	if (retval) goto out;
	task->tomoyo_flags |= TOMOYO_CHECK_READ_FOR_OPEN_EXEC;
	retval = search_binary_handler(bprm, regs);
	task->tomoyo_flags &= ~TOMOYO_CHECK_READ_FOR_OPEN_EXEC;
 out:
	if (retval < 0) task->domain_info = prev_domain;
	ccs_free(work);
	ccs_free(buf);
	return retval;
}

#else

int search_binary_handler_with_transition(struct linux_binprm *bprm, struct pt_regs *regs)
{
#ifdef CONFIG_SAKURA
	CCS_LoadPolicy(bprm->filename);
#endif
	return search_binary_handler(bprm, regs);
}

#endif

/***** TOMOYO Linux end. *****/
