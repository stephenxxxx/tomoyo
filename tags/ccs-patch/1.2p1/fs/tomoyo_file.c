/*
 * fs/tomoyo_file.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2006  NTT DATA CORPORATION
 *
 * Version: 1.2   2006/09/30
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */
/***** TOMOYO Linux start. *****/

#include <linux/ccs_common.h>
#include <linux/tomoyo.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#include <linux/namei.h>
#include <linux/mount.h>
static const int lookup_flags = LOOKUP_FOLLOW;
#else
static const int lookup_flags = LOOKUP_FOLLOW | LOOKUP_POSITIVE;
#endif
#include <linux/realpath.h>

/*************************  VARIABLES  *************************/

extern struct semaphore domain_acl_lock;

/***** The structure for globally readable files. *****/

typedef struct globally_readable_file_entry {
	struct globally_readable_file_entry *next; /* Pointer to next record. NULL if none.                */
	u32 hash_and_flag;                         /* (full_name_hash(filename) << 1). LSB is delete flag. */
	const char *filename;                      /* Absolute pathname. Never NULL.                       */
} GLOBALLY_READABLE_FILE_ENTRY;

/***** The structure for filename patterns. *****/

typedef struct pattern_entry {
	struct pattern_entry *next; /* Pointer to next record. NULL if none.          */
	u32 depth_and_flag;         /* (PathDepth(pattern) << 1). LSB is delete flag. */
	const char *pattern;        /* Patterned filename. Never NULL.                */
} PATTERN_ENTRY;

/***** The structure for detailed write operations. *****/

static struct {
	const char *keyword;
	const int paths;
	int check_type;
} acl_type_array[] = { /* mapping.txt */
	{ "create",   1, 1 }, // TYPE_CREATE_ACL
	{ "unlink",   1, 1 }, // TYPE_UNLINK_ACL
	{ "mkdir",    1, 1 }, // TYPE_MKDIR_ACL
	{ "rmdir",    1, 1 }, // TYPE_RMDIR_ACL
	{ "mkfifo",   1, 1 }, // TYPE_MKFIFO_ACL
	{ "mksock",   1, 1 }, // TYPE_MKSOCK_ACL
	{ "mkblock",  1, 1 }, // TYPE_MKBLOCK_ACL
	{ "mkchar",   1, 1 }, // TYPE_MKCHAR_ACL
	{ "truncate", 1, 1 }, // TYPE_TRUNCATE_ACL
	{ "symlink",  1, 1 }, // TYPE_SYMLINK_ACL
	{ "link",     2, 1 }, // TYPE_LINK_ACL
	{ "rename",   2, 1 }, // TYPE_RENAME_ACL
	{ NULL, 0, 0 }
};

/*************************  UTILITY FUNCTIONS  *************************/

const char *acltype2keyword(const unsigned int acl_type)
{
	return (acl_type < sizeof(acl_type_array) / sizeof(acl_type_array[0]))
		? acl_type_array[acl_type].keyword : NULL;
}

int acltype2paths(const unsigned int acl_type)
{
	return (acl_type < sizeof(acl_type_array) / sizeof(acl_type_array[0]))
		? acl_type_array[acl_type].paths : 0;
}

static unsigned int CheckACLFlags(const unsigned int index)
{
	if (index < (sizeof(acl_type_array) / sizeof(acl_type_array[0])) - 1)
		return acl_type_array[index].check_type;
	printk("%s: Index %u is out of range. Fix the kernel source.\n", __FUNCTION__, index);
	return 0;
}

/*
 *  Check whether the given filename is patterened.
 *  Returns nonzero if patterned, zero otherwise.
 */
static int PathContainsPattern(const char *filename)
{
	if (filename) {
		char c, d, e;
		while ((c = *filename++) != '\0') {
			if (c != '\\') continue;
			switch (c = *filename++) {
			case '\\':  /* "\\" */
				continue;
			case '0':   /* "\ooo" */
			case '1':
			case '2':
			case '3':
				if ((d = *filename++) >= '0' && d <= '7' && (e = *filename++) >= '0' && e <= '7'
					&& (c != '0' || d != '0' || e != '0')) continue; /* pattern is not \000 */
			}
			return 1;
		}
	}
	return 0;
}


/*************************  PROTOTYPES  *************************/

static int AddSingleWriteACL(const unsigned int type_hash, const char *filename, struct domain_info * const domain, const int is_delete, const struct condition_list *condition);
static int AddDoubleWriteACL(const unsigned int type_hash, const char *filename1, const char *filename2, struct domain_info * const domain, const int is_delete, const struct condition_list *condition);

/*************************  AUDIT FUNCTIONS  *************************/

#ifdef CONFIG_TOMOYO_AUDIT
static int AuditFileLog(const char *filename, const u16 perm, const int is_granted)
{
	char *buf;
	int len;
	if (CanSaveAuditLog(is_granted) < 0) return -ENOMEM;
	len = strlen(filename) + 8;
	if ((buf = InitAuditLog(&len)) == NULL) return -ENOMEM;
	snprintf(buf + strlen(buf), len - strlen(buf) - 1, "%d %s\n", perm, filename);
	return WriteAuditLog(buf, is_granted);
}

static int AuditWriteLog(const char *operation, const char *filename1, const char *filename2, const int is_granted)
{
	char *buf;
	int len;
	if (CanSaveAuditLog(is_granted) < 0) return -ENOMEM;
	len = strlen(operation) + strlen(filename1) + strlen(filename2) + 16;
	if ((buf = InitAuditLog(&len)) == NULL) return -ENOMEM;
	snprintf(buf + strlen(buf), len - strlen(buf) - 1, "allow_%s %s %s\n", operation, filename1, filename2);
	return WriteAuditLog(buf, is_granted);
}
#else
static inline void AuditFileLog(const char *filename, const u16 perm, const int is_granted) {}
static inline void AuditWriteLog(const char *operation, const char *filename1, const char *filename2, const int is_granted) {}
#endif

/*************************  GLOBALLY READABLE FILE HANDLER  *************************/

static GLOBALLY_READABLE_FILE_ENTRY globally_readable_list = { NULL, 0, "" };

static int AddGloballyReadableEntry(const char *filename)
{
	GLOBALLY_READABLE_FILE_ENTRY *new_entry, *ptr;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	const char *saved_filename;
	u32 hash_and_flag;
	if (!IsCorrectPath(filename, 0) || strendswith(filename, "/")) {
		printk(KERN_DEBUG "%s: Invalid pathname '%s'\n", __FUNCTION__, filename);
		return -EINVAL; /* No patterns allowed. */
	}
	hash_and_flag = full_name_hash(filename, strlen(filename)) << 1;
	/* I don't want to add if it was already added. */
	for (ptr = globally_readable_list.next; ptr; ptr = ptr->next) if ((ptr->hash_and_flag & ~1) == hash_and_flag && strcmp(ptr->filename, filename) == 0) { ptr->hash_and_flag &= ~1; return 0; }
	if ((saved_filename = SaveName(filename)) == NULL || (new_entry = (GLOBALLY_READABLE_FILE_ENTRY *) alloc_element(sizeof(GLOBALLY_READABLE_FILE_ENTRY))) == NULL) return -ENOMEM;
	new_entry->hash_and_flag = hash_and_flag;
	new_entry->filename = saved_filename;
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	for (ptr = &globally_readable_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

static int IsGloballyReadableFile(const char *filename)
{
	if (filename) {
		GLOBALLY_READABLE_FILE_ENTRY *ptr;
		const u32 hash_and_flag = full_name_hash(filename, strlen(filename)) << 1;
		for (ptr = globally_readable_list.next; ptr; ptr = ptr->next) {
			if (hash_and_flag == ptr->hash_and_flag && strcmp(filename, ptr->filename) == 0) return 1;
		}
	}
	return 0;
}

int AddGloballyReadablePolicy(char *filename, const int is_delete)
{
	GLOBALLY_READABLE_FILE_ENTRY *ptr;
	u32 hash_and_flag;
	if (!is_delete) return AddGloballyReadableEntry(filename);
	hash_and_flag = full_name_hash(filename, strlen(filename)) << 1;
	for (ptr = globally_readable_list.next; ptr; ptr = ptr->next) if (hash_and_flag == ptr->hash_and_flag && strcmp(ptr->filename, filename) == 0) ptr->hash_and_flag |= 1;
	return 0;
}

int ReadGloballyReadablePolicy(IO_BUFFER *head)
{
	GLOBALLY_READABLE_FILE_ENTRY *ptr = (GLOBALLY_READABLE_FILE_ENTRY *) head->read_var2;
	if (!ptr) ptr = globally_readable_list.next;
	while (ptr) {
		head->read_var2 = (void *) ptr;
		if ((ptr->hash_and_flag & 1) == 0 && io_printf(head, KEYWORD_ALLOW_READ "%s\n", ptr->filename)) break;
		ptr = ptr->next;
	}
	return ptr ? -ENOMEM : 0;
}

/*************************  FILE PATTERN HANDLER  *************************/

static PATTERN_ENTRY pattern_list = { NULL, 0, "" };

static int AddPatternEntry(const char *pattern)
{
	PATTERN_ENTRY *new_entry, *ptr;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	const char *saved_pattern;
	u32 depth_and_flag;
	if (!PathContainsPattern(pattern)) {
		printk(KERN_DEBUG "%s: Is not a pattern '%s'\n", __FUNCTION__, pattern);
		return -EINVAL;
	}
	depth_and_flag = PathDepth(pattern) << 1;
	/* I don't want to add if it was already added. */
	for (ptr = pattern_list.next; ptr; ptr = ptr->next) if ((ptr->depth_and_flag & ~1) == depth_and_flag && strcmp(ptr->pattern, pattern) == 0) { ptr->depth_and_flag &= ~1; return 0; }
	if ((saved_pattern = SaveName(pattern)) == NULL || (new_entry = (PATTERN_ENTRY *) alloc_element(sizeof(PATTERN_ENTRY))) == NULL) return -ENOMEM;
	new_entry->depth_and_flag = depth_and_flag;
	new_entry->pattern = saved_pattern;
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	for (ptr = &pattern_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

static const char *GetPattern(const char *filename)
{
	if (filename) {
		PATTERN_ENTRY *ptr;
		const char *pattern = NULL;
		const u32 depth_and_flag = PathDepth(filename) << 1;
		for (ptr = pattern_list.next; ptr; ptr = ptr->next) {
			if (depth_and_flag != ptr->depth_and_flag) continue;
			if (!PathMatchesToPattern(filename, ptr->pattern)) continue;
			pattern = ptr->pattern;
			if (strendswith(pattern, "/\\*")) {
				/* Do nothing. Try to find the better match. */
			} else {
				/* This would be the better match. Use this. */
				break;
			}
		}
		if (pattern) filename = pattern;
	}
	return filename;
}

int AddPatternPolicy(char *pattern, const int is_delete)
{
	PATTERN_ENTRY *ptr;
	if (!is_delete) return AddPatternEntry(pattern);
	for (ptr = pattern_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->pattern, pattern) == 0) ptr->depth_and_flag |= 1;
	return 0;
}

int ReadPatternPolicy(IO_BUFFER *head)
{
	PATTERN_ENTRY *ptr = (PATTERN_ENTRY *) head->read_var2;
	if (!ptr) ptr = pattern_list.next;
	while (ptr) {
		head->read_var2 = (void *) ptr;
		if ((ptr->depth_and_flag & 1) == 0 && io_printf(head, KEYWORD_FILE_PATTERN "%s\n", ptr->pattern)) break;
		ptr = ptr->next;
	}
	return ptr ? -ENOMEM : 0;
}

/*************************  FILE ACL HANDLER  *************************/

static int AddFileACL(const char *filename, u16 perm, struct domain_info * const domain, const int is_add, const struct condition_list *condition)
{
	const char *saved_filename;
	struct acl_info *ptr;
	unsigned int count = 0;
	const int is_dir = strendswith(filename, "/");
	const unsigned int type_hash = MAKE_ACL_TYPE(TYPE_FILE_ACL) + MAKE_ACL_HASH(PathDepth(filename));
	const u16 extra_hash = PathContainsPattern(filename) ? -1 : (full_name_hash(filename, strlen(filename)) << 1);
	int error = -ENOMEM;
	if (!domain) return -EINVAL;
	if (!is_dir) {
		if ((perm & 1) == 1 && (extra_hash & 1) == 1) {
			perm ^= 1;  /* Never allow execute permission with patterns. */
			printk("%s: Dropping execute permission for '%s'\n", __FUNCTION__, filename);
		} else if (perm == 4 && IsGloballyReadableFile(filename) && is_add) {
			return 0;   /* Don't add if the file is globally readable files. */
		}
	} else {
		if ((perm & 2) == 0) return 0; /* Don't add if the directory doesn't have write permission. */
	}
	if (perm > 7 || !perm) {
		printk(KERN_DEBUG "%s: Invalid permission '%d %s'\n", __FUNCTION__, perm, filename);
		return -EINVAL;
	}
	down(&domain_acl_lock);
	if (is_add) {
		if ((ptr = domain->first_acl_ptr) == NULL) goto first_entry;
		while (1) {
			FILE_ACL_RECORD *new_ptr;
			count++;
			if (ptr->type_hash == type_hash && ptr->cond == condition) {
				if (((FILE_ACL_RECORD *) ptr)->extra_hash == extra_hash && strcmp(((FILE_ACL_RECORD *) ptr)->filename, filename) == 0) {
					/* Found. Just 'OR' the permission bits. */
					((FILE_ACL_RECORD *) ptr)->perm |= perm;
					error = 0;
					break;
				}
			}
			if (ptr->next) {
				ptr = ptr->next;
				continue;
			}
	first_entry: ;
			/* If there are so many entries, don't append if accept mode. */
			if (is_add == 1 && count >= GetMaxAutoAppendFiles()) {
				if (GetDomainAttribute(domain) & DOMAIN_ATTRIBUTE_QUOTA_WARNED) break;
				printk("TOMOYO-WARNING: Domain '%s' has so many ACLs to hold. Stopped auto-append mode.\n", domain->domainname);
				SetDomainAttribute(domain, DOMAIN_ATTRIBUTE_QUOTA_WARNED);
				break;
			}
			/* Not found. Append it to the tail. */
			if ((saved_filename = SaveName(filename)) == NULL) break;
			if ((new_ptr = (FILE_ACL_RECORD *) alloc_element(sizeof(FILE_ACL_RECORD))) == NULL) break;
			new_ptr->head.type_hash = type_hash;
			new_ptr->head.cond = condition;
			new_ptr->filename = saved_filename;
			new_ptr->perm = perm;
			new_ptr->extra_hash = extra_hash;
			error = AddDomainACL(ptr, domain, (struct acl_info *) new_ptr);
			break;
		}
	} else {
		struct acl_info *prev = NULL;
		for (ptr = domain->first_acl_ptr; ptr; prev = ptr, ptr = ptr->next) {
			if (type_hash != ptr->type_hash || ptr->cond != condition) continue;
			if (((FILE_ACL_RECORD *) ptr)->perm != perm || strcmp(((FILE_ACL_RECORD *) ptr)->filename, filename)) continue;
			error = DelDomainACL(prev, domain, ptr);
			break;
		}
	}
	up(&domain_acl_lock);
	return error;
}

static int CheckFileACL(const char *filename, u16 perm, struct obj_info *obj)
{
	const struct domain_info *domain = current->domain_info;
	int error = -EPERM;
	unsigned int type_hash;
	struct acl_info *ptr;
	u16 extra_hash;
	const int may_use_pattern = ((perm & 1) == 0);
	if (!CheckCCSFlags(CCS_TOMOYO_MAC_FOR_FILE)) return 0;
	if (GetDomainAttribute(domain) & DOMAIN_ATTRIBUTE_TRUSTED) return 0;
	if (!strendswith(filename, "/")) {
		if (perm == 4 && IsGloballyReadableFile(filename)) return 0;
	}
	type_hash = MAKE_ACL_TYPE(TYPE_FILE_ACL) + MAKE_ACL_HASH(PathDepth(filename));
	extra_hash = full_name_hash(filename, strlen(filename)) << 1;
	for (ptr = domain->first_acl_ptr; ptr; ptr = ptr->next) {
		if (ptr->type_hash == type_hash && (((FILE_ACL_RECORD *) ptr)->perm & perm) == perm && CheckCondition(ptr->cond, obj) == 0 &&
			((((FILE_ACL_RECORD *) ptr)->extra_hash == extra_hash && strcmp(filename, ((FILE_ACL_RECORD *) ptr)->filename) == 0) ||
			 (may_use_pattern && ((FILE_ACL_RECORD *) ptr)->extra_hash == (u16) -1 && PathMatchesToPattern(filename, ((FILE_ACL_RECORD *) ptr)->filename)))) {
			error = 0;
			break;
		}
	}
	return error;
}

static int CheckFilePerm2(const char *filename, const u16 perm, const char *operation, struct obj_info *obj)
{
	int error = 0;
	if (!filename) return 0;
	error = CheckFileACL(filename, perm, obj);
	AuditFileLog(filename, perm, !error);
	if (error) {
		struct domain_info * const domain = current->domain_info;
		const int is_enforce = CheckCCSEnforce(CCS_TOMOYO_MAC_FOR_FILE);
		/* Don't use patterns if execution bit is on. */
		const char *patterned_file = ((perm & 1) == 0) ? GetPattern(filename) : filename;
		if (TomoyoVerboseMode()) {
			printk("TOMOYO-%s: Access %d(%s) to %s denied for %s\n", GetMSG(is_enforce), perm, operation, filename, GetLastName(domain));
		}
		if (is_enforce) error = CheckSupervisor("%s\n%d %s\n", domain->domainname, perm, patterned_file);
		else if (CheckCCSAccept(CCS_TOMOYO_MAC_FOR_FILE)) AddFileACL(patterned_file, perm, domain, 1, NULL);
		if (!is_enforce) error = 0;
	}
	return error;
}

int CheckFilePerm(const char *filename, const u16 perm, const int is_realpath, const char *operation)
{
	int error = -ENOMEM;
	char *buf;
	if (!CheckCCSFlags(CCS_TOMOYO_MAC_FOR_FILE)) return 0;
	if (!filename) return 0;
	if (is_realpath) return CheckFilePerm2(filename, perm, operation, NULL);
	buf = ccs_alloc(PAGE_SIZE);
	if (buf) {
		struct nameidata nd;
		if ((error = path_lookup(filename, lookup_flags, &nd)) == 0) {
			struct obj_info obj;
			if (nd.dentry->d_inode && S_ISDIR(nd.dentry->d_inode->i_mode)) {
				/* I don't check directories here because mkdir() and rmdir() don't call me. */
				path_release(&nd);
				goto ok;
			}
			memset(&obj, 0, sizeof(obj));
			obj.path1_dentry = nd.dentry;
			obj.path1_vfsmnt = nd.mnt;
			error = realpath_from_dentry(nd.dentry, nd.mnt, buf, PAGE_SIZE - 1);
			if (error == 0) error = CheckFilePerm2(buf, perm, operation, &obj);
			path_release(&nd);
		}
	}
 ok:
	ccs_free(buf);
	return error;
}

int AddFilePolicy(char *data, struct domain_info *domain, const int is_delete)
{
	char *filename = strchr(data, ' ');
	char *cp;
	const struct condition_list *condition = NULL;
	unsigned int perm;
	int type;
	if (!filename) return -EINVAL;
	*filename++ = '\0';
	if (sscanf(data, "%u", &perm) == 1) {
		cp = FindConditionPart(filename);
		if (cp && (condition = FindOrAssignNewCondition(cp)) == NULL) goto out;
		return AddFileACL(filename, (u16) perm, domain, is_delete ? 0 : -1, condition);
	}
	if (strncmp(data, "allow_", 6)) goto out;
	data += 6;
	for (type = 0; acl_type_array[type].keyword; type++) {
		if (strcmp(data, acl_type_array[type].keyword)) continue;
		if (acl_type_array[type].paths == 2) {
			char *filename2 = strchr(filename, ' ');
			if (!filename2) break;
			*filename2++ = '\0';
			cp = FindConditionPart(filename2);
			if (cp && (condition = FindOrAssignNewCondition(cp)) == NULL) goto out;
			return AddDoubleWriteACL(MAKE_ACL_TYPE(type) + MAKE_ACL_HASH(PathDepth(filename)), filename, filename2, domain, is_delete, condition);
		} else {
			cp = FindConditionPart(filename);
			if (cp && (condition = FindOrAssignNewCondition(cp)) == NULL) goto out;
			return AddSingleWriteACL(MAKE_ACL_TYPE(type) + MAKE_ACL_HASH(PathDepth(filename)), filename, domain, is_delete, condition);
		}
		break;
	}
 out:
	return -EINVAL;
}

/*************************  DETAILED FILE ACL HANDLER  *************************/

static int AddSingleWriteACL(const unsigned int type_hash, const char *filename, struct domain_info * const domain, const int is_delete, const struct condition_list *condition)
{
	const char *saved_filename;
	struct acl_info *ptr;
	int error = -ENOMEM;
	if (!domain) return -EINVAL;
	down(&domain_acl_lock);
	if (!is_delete) {
		if ((ptr = domain->first_acl_ptr) == NULL) goto first_entry;
		while (1) {
			SINGLE_ACL_RECORD *new_ptr;
			if (ptr->type_hash == type_hash && ptr->cond == condition) {
				if (strcmp(((SINGLE_ACL_RECORD *) ptr)->filename, filename) == 0) {
					/* Found. Nothing to do. */
					error = 0;
					break;
				}
			}
			if (ptr->next) {
				ptr = ptr->next;
				continue;
			}
		first_entry: ;
			/* Not found. Append it to the tail. */
			if ((saved_filename = SaveName(filename)) == NULL) break;
			if ((new_ptr = (SINGLE_ACL_RECORD *) alloc_element(sizeof(SINGLE_ACL_RECORD))) == NULL) break;
			new_ptr->head.type_hash = type_hash;
			new_ptr->head.cond = condition;
			new_ptr->filename = saved_filename;
			error = AddDomainACL(ptr, domain, (struct acl_info *) new_ptr);
			break;
		}
	} else {
		struct acl_info *prev = NULL;
		error = -ENOENT;
		for (ptr = domain->first_acl_ptr; ptr; prev = ptr, ptr = ptr->next) {
			if (type_hash != ptr->type_hash || ptr->cond != condition) continue;
			if (strcmp(((SINGLE_ACL_RECORD *) ptr)->filename, filename)) continue;
			error = DelDomainACL(prev, domain, ptr);
			break;
		}
	}
	up(&domain_acl_lock);
	return error;
}

static int AddDoubleWriteACL(const unsigned int type_hash, const char *filename1, const char *filename2, struct domain_info * const domain, const int is_delete, const struct condition_list *condition)
{
	const char *saved_filename1, *saved_filename2;
	struct acl_info *ptr;
	int error = -ENOMEM;
	if (!domain) return -EINVAL;
	down(&domain_acl_lock);
	if (!is_delete) {
		if ((ptr = domain->first_acl_ptr) == NULL) goto first_entry;
		while (1) {
			DOUBLE_ACL_RECORD *new_ptr;
			if (ptr->type_hash == type_hash && ptr->cond == condition) {
				if (strcmp(((DOUBLE_ACL_RECORD *) ptr)->filename1, filename1) == 0 && strcmp(((DOUBLE_ACL_RECORD *) ptr)->filename2, filename2) == 0) {
					/* Found. Nothing to do. */
					error = 0;
					break;
				}
			}
			if (ptr->next) {
				ptr = ptr->next;
				continue;
			}
		first_entry: ;
			/* Not found. Append it to the tail. */
			if ((saved_filename1 = SaveName(filename1)) == NULL) break;
			if ((saved_filename2 = SaveName(filename2)) == NULL) break;
			if ((new_ptr = (DOUBLE_ACL_RECORD *) alloc_element(sizeof(DOUBLE_ACL_RECORD))) == NULL) break;
			new_ptr->head.type_hash = type_hash;
			new_ptr->head.cond = condition;
			new_ptr->filename1 = saved_filename1;
			new_ptr->filename2 = saved_filename2;
			error = AddDomainACL(ptr, domain, (struct acl_info *) new_ptr);
			break;
		}
	} else {
		struct acl_info *prev = NULL;
		error = -ENOENT;
		for (ptr = domain->first_acl_ptr; ptr; prev = ptr, ptr = ptr->next) {
			if (type_hash != ptr->type_hash || ptr->cond != condition) continue;
			if (strcmp(((DOUBLE_ACL_RECORD *) ptr)->filename1, filename1) ||
				strcmp(((DOUBLE_ACL_RECORD *) ptr)->filename2, filename2)) continue;
			error = DelDomainACL(prev, domain, ptr);
			break;
		}
	}
	up(&domain_acl_lock);
	return error;
}

static int CheckSingleWriteACL(const unsigned int type, const unsigned int hash, const char *filename, struct obj_info *obj)
{
	const struct domain_info *domain = current->domain_info;
	const unsigned int type_hash = MAKE_ACL_TYPE(type) + MAKE_ACL_HASH(hash);
	struct acl_info *ptr;
	if (!CheckCCSFlags(CCS_TOMOYO_MAC_FOR_FILE)) return 0;
	if (GetDomainAttribute(domain) & DOMAIN_ATTRIBUTE_TRUSTED) return 0;
	for (ptr = domain->first_acl_ptr; ptr; ptr = ptr->next) {
		if (ptr->type_hash == type_hash && CheckCondition(ptr->cond, obj) == 0 &&
			PathMatchesToPattern(filename, ((SINGLE_ACL_RECORD *) ptr)->filename)) return 0;
	}
	return -EPERM;
}

static int CheckDoubleWriteACL(const unsigned int type, const unsigned int hash, const char *filename1, const char *filename2, struct obj_info *obj)
{
	const struct domain_info *domain = current->domain_info;
	const unsigned int type_hash = MAKE_ACL_TYPE(type) + MAKE_ACL_HASH(hash);
	struct acl_info *ptr;
	if (!CheckCCSFlags(CCS_TOMOYO_MAC_FOR_FILE)) return 0;
	if (GetDomainAttribute(domain) & DOMAIN_ATTRIBUTE_TRUSTED) return 0;
	for (ptr = domain->first_acl_ptr; ptr; ptr = ptr->next) {
		if (ptr->type_hash == type_hash && CheckCondition(ptr->cond, obj) == 0 &&
			PathMatchesToPattern(filename1, ((DOUBLE_ACL_RECORD *) ptr)->filename1) &&
			PathMatchesToPattern(filename2, ((DOUBLE_ACL_RECORD *) ptr)->filename2)) return 0;
	}
	return -EPERM;
}

int CheckSingleWritePermission(const unsigned int operation, struct dentry *dentry, struct vfsmount *mnt)
{
	int error;
	char *buf;
	struct domain_info * const domain = current->domain_info;
	const int is_enforce = CheckCCSEnforce(CCS_TOMOYO_MAC_FOR_FILE);
	if (!CheckCCSFlags(CCS_TOMOYO_MAC_FOR_FILE)) return 0;
	if (CheckACLFlags(operation) < 0) return 0;	
	buf = ccs_alloc(PAGE_SIZE);
	if (buf == NULL) return -ENOMEM;
	if ((error = realpath_from_dentry(dentry, mnt, buf, PAGE_SIZE - 4)) == 0) {
		struct obj_info obj;
		int len = strlen(buf);
		switch (operation) {
		case TYPE_MKDIR_ACL:
		case TYPE_RMDIR_ACL:
			if (buf[len - 1] != '/') {
				buf[len++] = '/'; buf[len++] = '\0';
			}
		}
		memset(&obj, 0, sizeof(obj));
		obj.path1_dentry = dentry;
		obj.path1_vfsmnt = mnt;
		if (CheckACLFlags(operation) > 0) {
			error = CheckSingleWriteACL(operation, PathDepth(buf), buf, &obj);
			AuditWriteLog(acltype2keyword(operation), buf, "", !error);
			if (error) {
				if (TomoyoVerboseMode()) {
					printk("TOMOYO-%s: Access '%s %s' denied for %s\n", GetMSG(is_enforce), acltype2keyword(operation), buf, GetLastName(domain));
				}
				if (is_enforce) error = CheckSupervisor("%s\nallow_%s %s\n", domain->domainname, acltype2keyword(operation), GetPattern(buf));
				else if (CheckCCSAccept(CCS_TOMOYO_MAC_FOR_FILE)) AddSingleWriteACL(MAKE_ACL_TYPE(operation) + MAKE_ACL_HASH(PathDepth(buf)), GetPattern(buf), domain, 0, NULL);
			}
		} else {
			error = CheckFilePerm2(buf, 2, acltype2keyword(operation), &obj);
		}
	} else {
		printk("DEBUG: realpath_from_dentry = %d\n", error);
	}
	ccs_free(buf);
	if (!is_enforce) error = 0;
	return error;
}

int CheckDoubleWritePermission(const unsigned int operation, struct dentry *dentry1, struct vfsmount *mnt1, struct dentry *dentry2, struct vfsmount *mnt2)
{
	int error;
	char *buf1, *buf2;
	struct domain_info * const domain = current->domain_info;
	const int is_enforce = CheckCCSEnforce(CCS_TOMOYO_MAC_FOR_FILE);
	if (!CheckCCSFlags(CCS_TOMOYO_MAC_FOR_FILE)) return 0;
	if (CheckACLFlags(operation) < 0) return 0;		
	buf1 = ccs_alloc(PAGE_SIZE);
	buf2 = ccs_alloc(PAGE_SIZE);
	if (!buf1 || !buf2) {
		ccs_free(buf1); ccs_free(buf2);
		return -ENOMEM;
	}
	if ((error = realpath_from_dentry(dentry1, mnt1, buf1, PAGE_SIZE - 4)) == 0) {
		if ((error = realpath_from_dentry(dentry2, mnt2, buf2, PAGE_SIZE - 4)) == 0) {
			struct obj_info obj;
			if (operation == TYPE_RENAME_ACL) {
				if (dentry1->d_inode && S_ISDIR(dentry1->d_inode->i_mode)) {
					int len = strlen(buf1);
					if (buf1[len - 1] != '/') {
						buf1[len++] = '/'; buf1[len++] = '\0';
					}
					len = strlen(buf2);
					if (buf2[len - 1] != '/') {
						buf2[len++] = '/'; buf2[len++] = '\0';
					}
				}
			}
			memset(&obj, 0, sizeof(obj));
			obj.path1_dentry = dentry1;
			obj.path1_vfsmnt = mnt1;
			obj.path2_dentry = dentry2;
			obj.path2_vfsmnt = mnt2;
			if (CheckACLFlags(operation) > 0) {
				error = CheckDoubleWriteACL(operation, PathDepth(buf1), buf1, buf2, &obj);
				AuditWriteLog(acltype2keyword(operation), buf1, buf2, !error);
				if (error) {
					if (TomoyoVerboseMode()) {
						printk("TOMOYO-%s: Access '%s %s %s' denied for %s\n", GetMSG(is_enforce), acltype2keyword(operation), buf1, buf2, GetLastName(domain));
					}
					if (is_enforce) error = CheckSupervisor("%s\nallow_%s %s %s\n", domain->domainname, acltype2keyword(operation), GetPattern(buf1), GetPattern(buf2));
					else if (CheckCCSAccept(CCS_TOMOYO_MAC_FOR_FILE)) AddDoubleWriteACL(MAKE_ACL_TYPE(operation) + MAKE_ACL_HASH(PathDepth(buf1)), GetPattern(buf1), GetPattern(buf2), domain, 0, NULL);
				}
			} else {
				error = CheckFilePerm2(buf1, 2, acltype2keyword(operation), &obj);
				if (!error) error = CheckFilePerm2(buf2, 2, acltype2keyword(operation), &obj);
			}
		} else {
			printk("DEBUG: realpath_from_dentry = %d\n", error);
		}
	} else {
		printk("DEBUG: realpath_from_dentry = %d\n", error);
	}
	ccs_free(buf1);
	ccs_free(buf2);
	if (!is_enforce) error = 0;
	return error;
}

int SetPermissionMapping(char *data, void **dummy)
{
	int i;
	char *cp = NULL;
	if (!data || (cp = strchr(data, '=')) == NULL) {
	out: ;
		printk("ERROR: Invalid line '%s=%s'\n", data, cp);
		printk("This line must be one of the following. The first is the default.\n");
		printk("%s=%s if you want to check this permission using this permission.\n", data, data);
		printk("%s=generic-write if you want to check this permission using generic-write permission.\n", data);
		printk("%s=no-check if you don't want to check this permission.\n", data);
		return -EINVAL;
	}
	*cp++ = '\0';
	for (i = 0; acl_type_array[i].keyword; i++) {
		if (strcmp(acl_type_array[i].keyword, data)) continue;
		if (strcmp(cp, acl_type_array[i].keyword) == 0) acl_type_array[i].check_type = 1;
		else if (strcmp(cp, "generic-write") == 0) acl_type_array[i].check_type = 0;
		else if (strcmp(cp, "no-check") == 0) acl_type_array[i].check_type = -1;
		else goto out;
		return 0;
	}
	printk("WARNING: Unprocessed line '%s=%s'\n", data, cp);
	return -EINVAL;
}

int ReadPermissionMapping(IO_BUFFER *head)
{
	if (!head->read_eof) {
		int i;
		for (i = 0; acl_type_array[i].keyword; i++) {
			io_printf(head, "%s=%s\n", acl_type_array[i].keyword, acl_type_array[i].check_type > 0 ? acl_type_array[i].keyword : acl_type_array[i].check_type == 0 ? "generic-write" : "no-check");
		}
		head->read_eof = 1;
	}
	return 0;
}


EXPORT_SYMBOL(CheckFilePerm);
EXPORT_SYMBOL(CheckSingleWritePermission);
EXPORT_SYMBOL(CheckDoubleWritePermission);

/***** TOMOYO Linux end. *****/