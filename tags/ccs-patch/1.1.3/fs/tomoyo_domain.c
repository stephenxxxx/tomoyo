/*
 * fs/tomoyo_domain.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2006  NTT DATA CORPORATION
 *
 * Version: 1.1.3   2006/07/13
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */
/***** TOMOYO Linux start. *****/

#include <linux/ccs_common.h>
#include <linux/tomoyo.h>
#include <linux/realpath.h>

/*************************  VARIABLES  *************************/

#define ROOT_NAME "<kernel>"             /* A domain definition starts with <kernel> . */

/* The initial domain. */
struct domain_info KERNEL_DOMAIN = { NULL, NULL, ROOT_NAME, 0 };

/* /sbin/init started? */
extern int sbin_init_started;

#ifdef CONFIG_TOMOYO

/* Lock for appending domain's ACL. */
DECLARE_MUTEX(domain_acl_lock);

/*************************  UTILITY FUNCTIONS  *************************/

/***** The structure for program files to force domain reconstruction. *****/

typedef struct initializer_entry {
	struct initializer_entry *next; /* Pointer to next record. NULL if none. */
	int is_deleted;                 /* Delete flag.                          */
	const char *initializer;        /* Absolute pathname. Never NULL.        */
} INITIALIZER_ENTRY;

/***** The structure for program files that should be aggregated. *****/

typedef struct aggregator_entry {
	struct aggregator_entry *next; /* Pointer to next record. NULL if none. */
	int is_deleted;                /* Delete flag.                          */
	const char *original_name;     /* Absolute pathname. Never NULL.        */
	const char *aggregated_name;   /* Absolute pathname. Never NULL.        */
} AGGREGATOR_ENTRY;

/***** The structure for program files that should be aliased. *****/

typedef struct alias_entry {
	struct alias_entry *next;  /* Pointer to next record. NULL if none. */
	int is_deleted;            /* Delete flag.                          */
	const char *original_name; /* Absolute pathname. Never NULL.        */
	const char *aliased_name;  /* Absolute pathname. Never NULL.        */
} ALIAS_ENTRY;

/***** The structure for domain definitions to treat as trusted. *****/

typedef struct trusted_pattern_entry {
	struct trusted_pattern_entry *next; /* Pointer to next record. NULL if none. */
	int is_deleted;                     /* Delete flag.                          */
	const char *trusted_pattern;        /* Pattern of domain name. Never NULL.   */
} TRUSTED_PATTERN_ENTRY;

/*************************  PROTOTYPES  *************************/

static int IsInitializer(const char *filename);
static int IsTrustedDomain(const char *domainname);
static int IsCorrectDomain(const unsigned char *domainname);
static int AddInitializerEntry(const char *initializer);
static void ReadPIDStatus(IO_BUFFER *head, const unsigned int attribute);

/*************************  VARIABLES  *************************/

/* Domain creation lock. */
static DECLARE_MUTEX(new_domain_assign_lock);

/*************************  UTILITY FUNCTIONS  *************************/

int IsDomainDef(const unsigned char *buffer)
{
	/* while (*buffer && (*buffer <= ' ' || *buffer >= 127)) buffer++; */
	return strncmp(buffer, ROOT_NAME, strlen(ROOT_NAME)) == 0;
}

const char *GetLastName(const struct domain_info *domain)
{
	const char *cp0 = domain->domainname, *cp1;
	if ((cp1 = strrchr(cp0, ' ')) != NULL) return cp1 + 1;
	return cp0;
}

/*
 *  Check whether the given domainname follows the naming rules.
 *  Returns nonzero if follows, zero otherwise.
 */
static int IsCorrectDomain(const unsigned char *domainname)
{
	unsigned char c, d, e;
	if (!domainname || strncmp(domainname, ROOT_NAME, strlen(ROOT_NAME))) goto out;
	domainname += strlen(ROOT_NAME);
	if (!*domainname) return 1;
	do {
		if (*domainname++ != ' ') goto out;
		if (*domainname++ != '/') goto out;
		while ((c = *domainname) != '\0' && c != ' ') {
			domainname++;
			if (c == '\\') {
				switch ((c = *domainname++)) {
				case '\\':  /* "\\" */
					continue;
				case '0':   /* "\ooo" */
				case '1':
				case '2':
				case '3':
					if ((d = *domainname++) >= '0' && d <= '7' && (e = *domainname++) >= '0' && e <= '7') {
						const unsigned char f =
							(((unsigned char) (c - '0')) << 6) +
							(((unsigned char) (d - '0')) << 3) +
							(((unsigned char) (e - '0')));
						if (f && (f <= ' ' || f >= 127)) continue; /* pattern is not \000 */
					}
				}
				goto out;
			} else if (c < ' ' || c >= 127) {
				goto out;
			}
		}
	} while (*domainname);
	return 1;
 out:
	return 0;
}

/* Update domain attribute. */
void SetDomainAttribute(struct domain_info *domain, const unsigned int attribute)
{
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	spin_lock(&lock);
	if (attribute == DOMAIN_ATTRIBUTE_UNTRUSTED) domain->attribute &= ~DOMAIN_ATTRIBUTE_TRUSTED;
	else domain->attribute |= attribute;
	spin_unlock(&lock);
}

int ReadSelfDomain(IO_BUFFER *head)
{
	if (!head->read_eof) {
		io_printf(head, "%s", current->domain_info->domainname);
		head->read_eof = 1;
	}
	return 0;
}

int AddDomainACL(struct acl_info *ptr, struct domain_info *domain, struct acl_info *new_ptr) {
	mb(); /* Instead of using spinlock. */
	if (!ptr) domain->first_acl_ptr = (struct acl_info *) new_ptr;
	else ptr->next = (struct acl_info *) new_ptr;
	return 0;
}

int DelDomainACL(struct acl_info *prev, struct domain_info *domain, struct acl_info *ptr) {
	if (prev) prev->next = ptr->next;
	else domain->first_acl_ptr = ptr->next;
	return 0;
}

/*************************  DOMAIN INITIALIZER HANDLER  *************************/

static INITIALIZER_ENTRY initializer_list = { NULL, 0, "" };

static int AddInitializerEntry(const char *initializer)
{
	INITIALIZER_ENTRY *new_entry, *ptr;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	const char *saved_initializer;
	if (!IsCorrectPath(initializer, 0) || strendswith(initializer, "/")) {
		printk(KERN_DEBUG "%s: Invalid pathname '%s'\n", __FUNCTION__, initializer);
		return -EINVAL; /* No patterns allowed. */
	}
	/* I don't want to add if it was already added. */
	for (ptr = initializer_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->initializer, initializer) == 0) { ptr->is_deleted = 0; return 0; }
	if ((saved_initializer = SaveName(initializer)) == NULL || (new_entry = (INITIALIZER_ENTRY *) alloc_element(sizeof(INITIALIZER_ENTRY))) == NULL) return -ENOMEM;
	new_entry->initializer = saved_initializer;
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	for (ptr = &initializer_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

int ReadInitializerPolicy(IO_BUFFER *head)
{
	INITIALIZER_ENTRY *ptr = (INITIALIZER_ENTRY *) head->read_var2;
	if (!ptr) ptr = initializer_list.next;
	while (ptr) {
		head->read_var2 = (void *) ptr;
		if (ptr->is_deleted == 0 && io_printf(head, KEYWORD_INITIALIZER "%s\n", ptr->initializer)) break;
		ptr = ptr->next;
	}
	return ptr ? -ENOMEM : 0;
}

int AddInitializerPolicy(char *initializer, const int is_delete)
{
	INITIALIZER_ENTRY *ptr;
	if (!is_delete) return AddInitializerEntry(initializer);
	for (ptr = initializer_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->initializer, initializer) == 0) ptr->is_deleted = 1;
	return 0;
}

static int IsInitializer(const char *filename)
{
	if (filename) {
		INITIALIZER_ENTRY *ptr;
		for (ptr = initializer_list.next; ptr; ptr = ptr->next) {
			if (ptr->is_deleted) continue;
			if (strcmp(filename, ptr->initializer) == 0) return 1;
		}
	}
	return 0;
}

/*************************  SYMBOLIC LINKED PROGRAM HANDLER  *************************/

static ALIAS_ENTRY alias_list = { NULL, 0, "", "" };

static int AddAliasEntry(const char *original_name, const char *aliased_name)
{
	ALIAS_ENTRY *new_entry, *ptr;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	const char *saved_original_name, *saved_aliased_name;
	if (!IsCorrectPath(original_name, 0)) {
		printk(KERN_DEBUG "%s: Invalid pathname '%s'\n", __FUNCTION__, original_name);
		return -EINVAL;
	} else if (!IsCorrectPath(aliased_name, 0)) {
		printk(KERN_DEBUG "%s: Invalid pathname '%s'\n", __FUNCTION__, aliased_name);
		return -EINVAL; /* No patterns allowed. */
	}
	/* I don't want to add if it was already added. */
	for (ptr = alias_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->original_name, original_name) == 0 && strcmp(ptr->aliased_name, aliased_name) == 0) { ptr->is_deleted = 0; return 0; }
	if ((saved_original_name = SaveName(original_name)) == NULL || (saved_aliased_name = SaveName(aliased_name)) == NULL || (new_entry = (ALIAS_ENTRY *) alloc_element(sizeof(ALIAS_ENTRY))) == NULL) return -ENOMEM;
	new_entry->original_name = saved_original_name;
	new_entry->aliased_name = saved_aliased_name;
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	for (ptr = &alias_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

int ReadAliasPolicy(IO_BUFFER *head)
{
	ALIAS_ENTRY *ptr = (ALIAS_ENTRY *) head->read_var2;
	if (!ptr) ptr = alias_list.next;
	while (ptr) {
		head->read_var2 = (void *) ptr;
		if (ptr->is_deleted == 0 && io_printf(head, KEYWORD_ALIAS "%s %s\n", ptr->original_name, ptr->aliased_name)) break;
		ptr = ptr->next;
	}
	return ptr ? -ENOMEM : 0;
}

int AddAliasPolicy(char *data, const int is_delete)
{
	ALIAS_ENTRY *ptr;
	char *cp = strchr(data, ' ');
	if (!cp) return -EINVAL;
	*cp = '\0';
	if (!is_delete) return AddAliasEntry(data, cp + 1);
	for (ptr = alias_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->original_name, data) == 0 && strcmp(ptr->aliased_name, cp + 1) == 0) ptr->is_deleted = 1;
	return 0;
}

/*************************  DOMAIN AGGREGATOR HANDLER  *************************/

static AGGREGATOR_ENTRY aggregator_list = { NULL, 0, "", "" };

static int AddAggregatorEntry(const char *original_name, const char *aggregated_name)
{
	AGGREGATOR_ENTRY *new_entry, *ptr;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	const char *saved_original_name, *saved_aggregated_name;
	if (!IsCorrectPath(original_name, 1)) {
		printk(KERN_DEBUG "%s: Invalid pathname '%s'\n", __FUNCTION__, original_name);
		return -EINVAL;
	} else if (!IsCorrectPath(aggregated_name, 0)) {
		printk(KERN_DEBUG "%s: Invalid pathname '%s'\n", __FUNCTION__, aggregated_name);
		return -EINVAL; /* No patterns allowed. */
	}
	/* I don't want to add if it was already added. */
	for (ptr = aggregator_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->original_name, original_name) == 0 && strcmp(ptr->aggregated_name, aggregated_name) == 0) { ptr->is_deleted = 0; return 0; }
	if ((saved_original_name = SaveName(original_name)) == NULL || (saved_aggregated_name = SaveName(aggregated_name)) == NULL || (new_entry = (AGGREGATOR_ENTRY *) alloc_element(sizeof(AGGREGATOR_ENTRY))) == NULL) return -ENOMEM;
	new_entry->original_name = saved_original_name;
	new_entry->aggregated_name = saved_aggregated_name;
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	for (ptr = &aggregator_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

int ReadAggregatorPolicy(IO_BUFFER *head)
{
	AGGREGATOR_ENTRY *ptr = (AGGREGATOR_ENTRY *) head->read_var2;
	if (!ptr) ptr = aggregator_list.next;
	while (ptr) {
		head->read_var2 = (void *) ptr;
		if (ptr->is_deleted == 0 && io_printf(head, KEYWORD_AGGREGATOR "%s %s\n", ptr->original_name, ptr->aggregated_name)) break;
		ptr = ptr->next;
	}
	return ptr ? -ENOMEM : 0;
}

int AddAggregatorPolicy(char *data, const int is_delete)
{
	AGGREGATOR_ENTRY *ptr;
	char *cp = strchr(data, ' ');
	if (!cp) return -EINVAL;
	*cp = '\0';
	if (!is_delete) return AddAggregatorEntry(data, cp + 1);
	for (ptr = aggregator_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->original_name, data) == 0 && strcmp(ptr->aggregated_name, cp + 1) == 0) ptr->is_deleted = 1;
	return 0;
}

/*************************  TRUSTED DOMAIN HANDLER  *************************/

static TRUSTED_PATTERN_ENTRY trusted_pattern_list = { NULL, 0, "" };

static int AddTrustedPatternEntry(const char *trusted_pattern)
{
	TRUSTED_PATTERN_ENTRY *new_entry, *ptr;
	if (!IsCorrectDomain(trusted_pattern)) {
		printk(KERN_DEBUG "%s: Invalid domainname pattern '%s'\n", __FUNCTION__, trusted_pattern);
		return -EINVAL;
	}
	/* I don't want to add if it was already added. */
	for (ptr = trusted_pattern_list.next; ptr; ptr = ptr->next) {
		if (strcmp(ptr->trusted_pattern, trusted_pattern) == 0) {
			ptr->is_deleted = 0; break;
		}
	}
	if (!ptr) {
		static spinlock_t lock = SPIN_LOCK_UNLOCKED;
		const char *saved_trusted_pattern;
		if ((saved_trusted_pattern = SaveName(trusted_pattern)) == NULL || (new_entry = (TRUSTED_PATTERN_ENTRY *) alloc_element(sizeof(TRUSTED_PATTERN_ENTRY))) == NULL) return -ENOMEM;
		new_entry->trusted_pattern = saved_trusted_pattern;
		/***** CRITICAL SECTION START *****/
		spin_lock(&lock);
		for (ptr = &trusted_pattern_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
		spin_unlock(&lock);
		/***** CRITICAL SECTION END *****/
	}
	/***** CRITICAL SECTION START *****/
	{   /* Update trusted flag for existing domains. */
		struct domain_info *domain;
		const int len = strlen(trusted_pattern);
		down(&new_domain_assign_lock);
		for (domain = &KERNEL_DOMAIN; domain; domain = domain->next) {
			if (domain->attribute & DOMAIN_ATTRIBUTE_TRUSTED) continue;
			if (strncmp(domain->domainname, trusted_pattern, len)) continue;
			if (domain->domainname[len] == '\0' || domain->domainname[len] == ' ') {
				SetDomainAttribute(domain, DOMAIN_ATTRIBUTE_TRUSTED);
				printk("TOMOYO: Domain '%s' is now trusted.\n", domain->domainname);
			}
		}
		up(&new_domain_assign_lock);
	}
	/***** CRITICAL SECTION END *****/
	return 0;
}

int AddTrustedPatternPolicy(char *trusted_pattern, const int is_delete)
{
	TRUSTED_PATTERN_ENTRY *ptr;
	if (!is_delete) return AddTrustedPatternEntry(trusted_pattern);
	for (ptr = trusted_pattern_list.next; ptr; ptr = ptr->next) {
		if (strcmp(ptr->trusted_pattern, trusted_pattern)) continue;
		ptr->is_deleted = 1;
		/***** CRITICAL SECTION START *****/
		{   /* Update trusted flag for existing domains. */
			struct domain_info *domain;
			down(&new_domain_assign_lock);
			for (domain = &KERNEL_DOMAIN; domain; domain = domain->next) {
				if ((domain->attribute & DOMAIN_ATTRIBUTE_TRUSTED) == 0) continue;
				if (IsTrustedDomain(domain->domainname) == 0) {
					SetDomainAttribute(domain, DOMAIN_ATTRIBUTE_UNTRUSTED);
					printk("TOMOYO: Domain '%s' is now untrusted.\n", domain->domainname);
				}
			}
			up(&new_domain_assign_lock);
		}
		/***** CRITICAL SECTION END *****/
	}
	return 0;
}

int ReadTrustedPatternPolicy(IO_BUFFER *head)
{
	TRUSTED_PATTERN_ENTRY *ptr = (TRUSTED_PATTERN_ENTRY *) head->read_var2;
	if (!ptr) ptr = trusted_pattern_list.next;
	while (ptr) {
		head->read_var2 = (void *) ptr;
		if (ptr->is_deleted == 0 && io_printf(head, KEYWORD_TRUST_DOMAIN "%s\n", ptr->trusted_pattern)) break;
		ptr = ptr->next;
	}
	return ptr ? -ENOMEM : 0;
}

static int IsTrustedDomain(const char *domainname)
{
	if (domainname) {
		TRUSTED_PATTERN_ENTRY *ptr;
		for (ptr = trusted_pattern_list.next; ptr; ptr = ptr->next) {
			const int len = strlen(ptr->trusted_pattern);
			if (ptr->is_deleted) continue;
			if (strncmp(domainname, ptr->trusted_pattern, len)) continue;
			if (domainname[len] == ' ' || domainname[len] == '\0') return 1;
		}
	}
	return 0;
}

int ReadTrustedPIDs(IO_BUFFER *head)
{
	if (!head->read_eof) {
		ReadPIDStatus(head, DOMAIN_ATTRIBUTE_TRUSTED);
		head->read_eof = 1;
	}
	return 0;
}

/*************************  DOMAIN DELETION HANDLER  *************************/

static int SetDeleteMark(const char *domainname)
{
	struct domain_info *domain;
	for (domain = &KERNEL_DOMAIN; domain; domain = domain->next) {
		if (domain->attribute & DOMAIN_ATTRIBUTE_DELETED) continue;
		if (strcmp(domain->domainname, domainname)) continue;
		SetDomainAttribute(domain, DOMAIN_ATTRIBUTE_DELETED);
	}
	return 0;
}

int DeleteDomain(char *data)
{
	return SetDeleteMark(data);
}

int ReadDeletedPIDs(IO_BUFFER *head)
{
	if (!head->read_eof) {
		ReadPIDStatus(head, DOMAIN_ATTRIBUTE_DELETED);
		head->read_eof = 1;
	}
	return 0;
}

static void ReadPIDStatus(IO_BUFFER *head, const unsigned int attribute)
{
	struct task_struct *p;
	/***** CRITICAL SECTION START *****/
	read_lock(&tasklist_lock);
	for_each_process(p) {
		struct domain_info *domain = p->domain_info;
		if (domain->attribute & attribute) {
			/* If there are too many process to list up, I can't sleep to reallocate, so I ignore them. */
			if (io_printf(head, " %d", p->pid)) break;
		}
	}
	read_unlock(&tasklist_lock);
	/***** CRITICAL SECTION END *****/
}

/*************************  DOMAIN TRANSITION HANDLER  *************************/

struct domain_info *FindDomain(const char *domainname)
{
	struct domain_info *domain;
	for (domain = &KERNEL_DOMAIN; domain; domain = domain->next) {
		if ((domain->attribute & DOMAIN_ATTRIBUTE_DELETED) == 0 && strcmp(domainname, domain->domainname) == 0) return domain;
	}
	return NULL;
}

struct domain_info *FindOrAssignNewDomain(const char *domainname)
{
	struct domain_info *domain = NULL;
	const char *saved_domainname;
	down(&new_domain_assign_lock);
	if ((domain = FindDomain(domainname)) == NULL) {
		if (IsCorrectDomain(domainname)) {
			if ((saved_domainname = SaveName(domainname)) != NULL && (domain = (struct domain_info *) alloc_element(sizeof(struct domain_info))) != NULL) {
				struct domain_info *ptr = &KERNEL_DOMAIN;
				domain->domainname = saved_domainname;
				/* Check is_trusted flag for this domain. */
				if (IsTrustedDomain(saved_domainname)) {
					SetDomainAttribute(domain, DOMAIN_ATTRIBUTE_TRUSTED);
					/* printk("TOMOYO: Created a trusted domain '%s'.\n", saved_domainname); */
				}
				mb(); /* Instead of using spinlock. */
				while (ptr->next) ptr = ptr->next; ptr->next = domain;
			}
		} else {
			printk(KERN_DEBUG "%s: Invalid domainname '%s'\n", __FUNCTION__, domainname);
		}
	}
	up(&new_domain_assign_lock);
	return domain;
}

struct domain_info *FindNextDomain(const char *original_name, int *retval)
{
	/* This function assumes that the size of buffer returned by realpath() = PAGE_SIZE. */
	struct domain_info *old_domain = current->domain_info, *domain = NULL;
	char *new_domain_name = NULL, *real_program_name = NULL;
	const int is_enforce = CheckCCSEnforce(CCS_TOMOYO_MAC_FOR_FILE);

	{
		/*
		 * Built-in initializers. This is needed because policies are not loaded until starting /sbin/init .
		 */
		static int first = 1;
		if (first) {
			AddInitializerEntry("/sbin/hotplug");
			AddInitializerEntry("/sbin/modprobe");
			first = 0;
		}
	}

	/* Get realpath of program. */
	*retval = -ENOENT; /* I hope realpath() won't fail with -ENOMEM. */
	if ((real_program_name = (char *) realpath(original_name)) == NULL) goto out;

	/* Check 'alias' directive. */
	{
		ALIAS_ENTRY *ptr;
		/* Is this program allowed to be called via symbolic links? */
		for (ptr = alias_list.next; ptr; ptr = ptr->next) {
			if (ptr->is_deleted || strcmp(real_program_name, ptr->original_name)) continue;
			break;
		}
		if (ptr) {
			/* Get realpath of symbolic link. */
			char *symlink_program_name = (char *) realpath_nofollow(original_name);
			if (symlink_program_name && strcmp(real_program_name, symlink_program_name)) {
				/* Find a symbolic link to use. */
				for (ptr = alias_list.next; ptr; ptr = ptr->next) {
					if (ptr->is_deleted || strcmp(real_program_name, ptr->original_name) || strcmp(symlink_program_name, ptr->aliased_name)) continue;
					memset(real_program_name, 0, PAGE_SIZE);
					strncpy(real_program_name, ptr->aliased_name, PAGE_SIZE - 1);
					break;
				}
			}
			ccs_free(symlink_program_name);
		}
	}

	/* Check 'aggregator' directive. */
	{
		AGGREGATOR_ENTRY *ptr;
		/* Is this program allowed to be aggregated? */
		for (ptr = aggregator_list.next; ptr; ptr = ptr->next) {
			if (ptr->is_deleted || !PathMatchesToPattern(real_program_name, ptr->original_name)) continue;
			memset(real_program_name, 0, PAGE_SIZE);
			strncpy(real_program_name, ptr->aggregated_name, PAGE_SIZE - 1);
			break;
		}
	}

	/* Check execute permission. */
	if ((*retval = CheckFilePerm(real_program_name, 1, 1, "do_execve")) < 0) goto out;

	/* Allocate memory for calcurating domain name. */
	*retval = -ENOMEM;
	if ((new_domain_name = ccs_alloc(PAGE_SIZE)) == NULL) goto out;

	/* Check 'initializer' directtive. */
	if (IsInitializer(real_program_name)) {
		snprintf(new_domain_name, PAGE_SIZE - 1, ROOT_NAME " " "%s", real_program_name);
		if (strlen(new_domain_name) < PAGE_SIZE - 10) {
			if (is_enforce) domain = FindDomain(new_domain_name);
			else domain = FindOrAssignNewDomain(new_domain_name);
		}
	} else if (old_domain == &KERNEL_DOMAIN && !sbin_init_started) {
		/*
		 * Needn't to transit from kernel domain before starting /sbin/init .
		 * But transit from kernel domain if executing initializers, for they might start before /sbin/init .
		 */
		domain = old_domain;
	} else if ((old_domain->attribute & DOMAIN_ATTRIBUTE_TRUSTED) == 0) {
		/* Normal domain transition. */
		snprintf(new_domain_name, PAGE_SIZE - 1, "%s %s", old_domain->domainname, real_program_name);
		if (strlen(new_domain_name) < PAGE_SIZE - 10) {
			if (is_enforce) domain = FindDomain(new_domain_name);
			else domain = FindOrAssignNewDomain(new_domain_name);
		}
	} else {
		domain = old_domain;
	}
	if (!domain && is_enforce && strlen(new_domain_name) < PAGE_SIZE - 10) {
		if (CheckSupervisor("#Need to create domain\n%s\n", new_domain_name) == 0) domain = FindOrAssignNewDomain(new_domain_name);
	}
	if (!domain) {
		if (is_enforce) *retval = -EPERM;
	} else {
		*retval = 0;
	}
 out: ;
	ccs_free(new_domain_name);
	ccs_free(real_program_name);
	if (!domain) domain = old_domain;
	return domain;
}

#endif

/***** TOMOYO Linux end. *****/