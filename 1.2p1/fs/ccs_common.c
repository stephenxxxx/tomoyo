/*
 * fs/ccs_common.c
 *
 * Common functions for SAKURA and TOMOYO.
 *
 * Copyright (C) 2005-2006  NTT DATA CORPORATION
 *
 * Version: 1.2   2006/09/30
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#define POLICY_FILE_LOCATION "/root/security"

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/utime.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <stdarg.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#include <linux/namei.h>
#include <linux/mount.h>
static const int lookup_flags = LOOKUP_FOLLOW;
#else
static const int lookup_flags = LOOKUP_FOLLOW | LOOKUP_POSITIVE;
#endif
#include <linux/realpath.h>
#include <linux/ccs_common.h>
#include <linux/ccs_proc.h>
#include <linux/tomoyo.h>

/*************************  VARIABLES  *************************/

/* /sbin/init started? */
int sbin_init_started = 0;

const char *ccs_log_level = KERN_DEBUG;

static struct {
	const char *keyword;
	unsigned int current_value;
	const unsigned int max_value;
} ccs_control_array[CCS_MAX_CONTROL_INDEX] = {
	[CCS_TOMOYO_MAC_FOR_FILE]        = { "MAC_FOR_FILE",        0, 3 },
	[CCS_TOMOYO_MAC_FOR_ARGV0]       = { "MAC_FOR_ARGV0",       0, 3 },
	[CCS_TOMOYO_MAC_FOR_NETWORK]     = { "MAC_FOR_NETWORK",     0, 3 },
	[CCS_TOMOYO_MAC_FOR_BINDPORT]    = { "MAC_FOR_BINDPORT",    0, 3 },
	[CCS_TOMOYO_MAC_FOR_CONNECTPORT] = { "MAC_FOR_CONNECTPORT", 0, 3 },
	[CCS_TOMOYO_MAC_FOR_SIGNAL]      = { "MAC_FOR_SIGNAL",      0, 3 },
	[CCS_SAKURA_DENY_CONCEAL_MOUNT]  = { "DENY_CONCEAL_MOUNT",  0, 3 },
	[CCS_SAKURA_RESTRICT_CHROOT]     = { "RESTRICT_CHROOT",     0, 3 },
	[CCS_SAKURA_RESTRICT_MOUNT]      = { "RESTRICT_MOUNT",      0, 3 },
	[CCS_SAKURA_RESTRICT_UNMOUNT]    = { "RESTRICT_UNMOUNT",    0, 3 },
	[CCS_SAKURA_DENY_PIVOT_ROOT]     = { "DENY_PIVOT_ROOT",     0, 3 },
	[CCS_SAKURA_TRACE_READONLY]      = { "TRACE_READONLY",      0, 1 },
	[CCS_SAKURA_RESTRICT_AUTOBIND]   = { "RESTRICT_AUTOBIND",   0, 1 },
	[CCS_TOMOYO_MAX_ACCEPT_FILES]    = { "MAX_ACCEPT_FILES",    MAX_ACCEPT_FILES, INT_MAX },
	[CCS_TOMOYO_MAX_GRANT_LOG]       = { "MAX_GRANT_LOG",       MAX_GRANT_LOG, INT_MAX },
	[CCS_TOMOYO_MAX_REJECT_LOG]      = { "MAX_REJECT_LOG",      MAX_REJECT_LOG, INT_MAX },
	[CCS_TOMOYO_VERBOSE]             = { "TOMOYO_VERBOSE",      1, 1 },
	[CCS_MAX_ENFORCE_GRACE]          = { "MAX_ENFORCE_GRACE",   0, INT_MAX },
};

/*************************  UTILITY FUNCTIONS  *************************/

#ifdef CONFIG_TOMOYO
static int __init TOMOYO_Quiet_Setup(char *str)
{
	ccs_control_array[CCS_TOMOYO_VERBOSE].current_value = 0;
	return 0;
}

__setup("TOMOYO_QUIET", TOMOYO_Quiet_Setup);
#endif

/* Am I root? */
static int isRoot(void)
{
	return !current->uid && !current->euid;
}

int strendswith(const char *name, const char *tail)
{
	int len;
	if (!name || !tail) return 0;
	len = strlen(name) - strlen(tail);
	return len >= 0 && strcmp(name + len, tail) == 0;
}

/*
 * Format string.
 * Leading and trailing whitespaces are removed.
 * Multiple whitespaces are packed into single space.
 */
void NormalizeLine(unsigned char *buffer)
{
	unsigned char *sp = buffer, *dp = buffer;
	int first = 1;
	while (*sp && (*sp <= ' ' || *sp >= 127)) sp++;
	while (*sp) {
		if (!first) *dp++ = ' ';
		first = 0;
		while (*sp > ' ' && *sp < 127) *dp++ = *sp++;
		while (*sp && (*sp <= ' ' || *sp >= 127)) sp++;
	}
	*dp = '\0';
}

/*
 *  Check whether the given filename follows the naming rules.
 *  Returns nonzero if follows, zero otherwise.
 */
int IsCorrectPath(const char *filename, const int may_contain_pattern)
{
	if (filename && *filename == '/') {
		char c, d, e;
		while ((c = *filename++) != '\0') {
			if (c == '\\') {
				switch ((c = *filename++)) {
				case '\\':  /* "\\" */
					continue;
				case '$':   /* "\$" */
				case '+':   /* "\+" */
				case '?':   /* "\?" */
				case '*':   /* "\*" */
				case '@':   /* "\@" */
				case 'x':   /* "\x" */
				case 'X':   /* "\X" */
				case 'a':   /* "\a" */
				case 'A':   /* "\A" */
					if (may_contain_pattern) continue;
					break;
				case '0':   /* "\ooo" */
				case '1':
				case '2':
				case '3':
					if ((d = *filename++) >= '0' && d <= '7' && (e = *filename++) >= '0' && e <= '7') {
						const unsigned char f =
							(((unsigned char) (c - '0')) << 6) +
							(((unsigned char) (d - '0')) << 3) +
							(((unsigned char) (e - '0')));
						if (f && (f <= ' ' || f >= 127)) continue; /* pattern is not \000 */
					}
				}
				return 0;
			} else if (c <= ' ' || c >= 127) {
				return 0;
			}
		}
		return 1;
	}
	return 0;
}

int PathDepth(const char *pathname)
{
	int i = 0;
	if (pathname) {
		char *ep = strchr(pathname, '\0');
		if (pathname < ep--) {
			if (*ep != '/') i++;
			while (pathname <= ep) if (*ep-- == '/') i += 2;
		}
	}
	return i;
}

static int FileMatchesToPattern(const char *filename, const char *filename_end, const char *pattern, const char *pattern_end)
{
	while (filename < filename_end && pattern < pattern_end) {
		if (*pattern != '\\') {
			if (*filename++ != *pattern++) return 0;
		} else {
			char c = *filename;
			pattern++;
			switch (*pattern) {
			case '?':
				if (c == '/') {
					return 0;
				} else if (c == '\\') {
					if ((c = filename[1]) == '\\') {
						filename++; /* safe because filename is \\ */
					} else if (c >= '0' && c <= '3' && (c = filename[2]) >= '0' && c <= '7' && (c = filename[3]) >= '0' && c <= '7') {
						filename += 3; /* safe because filename is \ooo */
					} else {
						return 0;
					}
				}
				break;
			case '\\':
				if (c != '\\') return 0;
				if (*++filename != '\\') return 0; /* safe because *filename != '\0' */
				break;
			case '+':
				if (c < '0' || c > '9') return 0;
				break;
			case 'x':
				if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return 0;
				break;
			case 'a':
				if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
				if (c == '\\' && (c = filename[1]) >= '0' && c <= '3' && c == *pattern
					&& (c = filename[2]) >= '0' && c <= '7' && c == pattern[1]
					&& (c = filename[3]) >= '0' && c <= '7' && c == pattern[2]) {
					filename += 3; /* safe because filename is \ooo */
					pattern += 2; /* safe because pattern is \ooo  */
					break;
				}
				return 0; /* Not matched. */
			case '*':
			case '@':
				{
					int i;
					for (i = 0; i <= filename_end - filename; i++) {
						if (FileMatchesToPattern(filename + i, filename_end, pattern + 1, pattern_end)) return 1;
						if ((c = filename[i]) == '.' && *pattern == '@') break;
						if (c == '\\') {
							if ((c = filename[i + 1]) == '\\') {
								i++; /* safe because filename is \\ */
							} else if (c >= '0' && c <= '3' && (c = filename[i + 2]) >= '0' && c <= '7' && (c = filename[i + 3]) >= '0' && c <= '7') {
								i += 3; /* safe because filename is \ooo */
							} else {
								break; /* Bad pattern. */
							}
						}
					}
					return 0; /* Not matched. */
				}
			default:
				{
					int i, j = 0;
					if ((c = *pattern) == '$') {
						while ((c = filename[j]) >= '0' && c <= '9') j++;
					} else if (c == 'X') {
						while (((c = filename[j]) >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) j++;
					} else if (c == 'A') {
						while (((c = filename[j]) >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) j++;
					}
					for (i = 1; i <= j; i++) {
						if (FileMatchesToPattern(filename + i, filename_end, pattern + 1, pattern_end)) return 1;
					}
				}
				return 0; /* Not matched or bad pattern. */
			}
			filename++; /* safe because *filename != '\0' */
			pattern++; /* safe because *pattern != '\0' */
		}
	}
	while (*pattern == '\\' && (*(pattern + 1) == '*' || *(pattern + 1) == '@')) pattern += 2;
	return (filename == filename_end && pattern == pattern_end);
}

/*
 *  Check whether the given pathname matches to the given pattern.
 *  Returns nonzero if matches, zero otherwise.
 *
 *  The following patterns are available.
 *    \\     \ itself.
 *    \ooo   Octal representation of a byte.
 *    \*     More than or equals to 0 character other than '/'.
 *    \@     More than or equals to 0 character other than '/' or '.'.
 *    \?     1 byte character other than '/'.
 *    \$     More than or equals to 1 decimal digit.
 *    \+     1 decimal digit.
 *    \X     More than or equals to 1 hexadecimal digit.
 *    \x     1 hexadecimal digit.
 *    \A     More than or equals to 1 alphabet character.
 *    \a     1 alphabet character.
 */

int PathMatchesToPattern(const char *pathname, const char *pattern)
{
	if (!pathname || !pattern) return 0;
	/* if pattern doesn't contain '\', I can use strcmp(). */
	if (!strchr(pattern, '\\')) return !strcmp(pathname, pattern);
	if (PathDepth(pathname) != PathDepth(pattern)) return 0;
	while (*pathname && *pattern) {
		const char *pathname_delimiter = strchr(pathname, '/'), *pattern_delimiter = strchr(pattern, '/');
		if (!pathname_delimiter) pathname_delimiter = strchr(pathname, '\0');
		if (!pattern_delimiter) pattern_delimiter = strchr(pattern, '\0');
		if (!FileMatchesToPattern(pathname, pathname_delimiter, pattern, pattern_delimiter)) return 0;
		pathname = *pathname_delimiter ? pathname_delimiter + 1 : pathname_delimiter;
		pattern = *pattern_delimiter ? pattern_delimiter + 1 : pattern_delimiter;
	}
	while (*pattern == '\\' && (*(pattern + 1) == '*' || *(pattern + 1) == '@')) pattern += 2;
	return (!*pathname && !*pattern);
}

/*
 *  Read policy file and dispatch.
 */
int ReadFileAndProcess(struct file *file, int (*func) (char *, void **))
{
	char *buffer, *cp;
	void *ptr = NULL;
	int len;
	unsigned long offset = 0;
	if (!file || !func) return -EINVAL;
	if ((buffer = ccs_alloc(PAGE_SIZE * 2)) == NULL) return -ENOMEM;
	/* Reads one line and process it. Ends when no more '\n' found. */
	while ((len = kernel_read(file, offset, buffer, PAGE_SIZE * 2)) > 0 && (cp = memchr(buffer, '\n', len)) != NULL) {
		*cp = '\0';
		offset += cp - buffer + 1;
		NormalizeLine(buffer);
		func(buffer, &ptr);
	}
	ccs_free(buffer);
	return 0;
}

/*
 *  Transactional printf() to IO_BUFFER structure.
 *  snprintf() will truncate, but io_printf() won't.
 *  Returns zero on success, nonzero otherwise.
 */
int io_printf(IO_BUFFER *head, const char *fmt, ...)
{
	va_list args;
	int len, pos = head->read_avail, size = head->readbuf_size - pos;
	if (size <= 0) return -ENOMEM;
	va_start(args, fmt);
	len = vsnprintf(head->read_buf + pos, size, fmt, args);
	va_end(args);
	if (pos + len >= head->readbuf_size) return -ENOMEM;
	head->read_avail += len;
	return 0;
}

/*
 * Get realpath() of current process.
 * This function uses ccs_alloc(), so caller must ccs_free() if this function didn't return NULL.
 */
const char *GetEXE(void)
{
	if (current->mm) {
		struct vm_area_struct *vma = current->mm->mmap;
		while (vma) {
			if ((vma->vm_flags & VM_EXECUTABLE) && vma->vm_file) {
				char *buf = ccs_alloc(PAGE_SIZE);
				if (buf == NULL) return NULL;
				if (realpath_from_dentry(vma->vm_file->f_dentry, vma->vm_file->f_vfsmnt, buf, PAGE_SIZE - 1) == 0) return (const char *) buf;
				ccs_free(buf); return NULL;
			}
			vma = vma->vm_next;
		}
	}
	return NULL;
}

const char *GetMSG(const int is_enforce)
{
	if (is_enforce) return "ERROR"; else return "WARNING";
}

unsigned int TomoyoVerboseMode(void)
{
	return ccs_control_array[CCS_TOMOYO_VERBOSE].current_value;
}

/*************************  DOMAIN POLICY HANDLER  *************************/

/* Default mode is "disable all mandatory access control functions". */
/* You must create profiles and choose one using CCS= options to the kernel command line. */
static unsigned int ccs_profile_index = 0;
static int panic_if_load_profile_fails = 0;

static int __init CCS_Setup(char *str)
{
	panic_if_load_profile_fails = 1;
	if (sscanf(str, "%u", &ccs_profile_index) != 1) printk("Invalid value for CCS= .");
	return 0;
}

__setup("CCS=", CCS_Setup);

#ifdef CONFIG_TOMOYO

static int skip_domain_policy_loading = 0;

static int __init TOMOYO_NOLOAD_Setup(char *str)
{
	skip_domain_policy_loading = 1;
	ccs_control_array[CCS_TOMOYO_VERBOSE].current_value = 0;
	return 0;
}

__setup("TOMOYO_NOLOAD", TOMOYO_NOLOAD_Setup);
#endif

/* Check whether the given access control is enabled. */
unsigned int CheckCCSFlags(const unsigned int index)
{
	return index < CCS_MAX_CONTROL_INDEX ? ccs_control_array[index].current_value : 0;
}

/* Check whether the given access control is enforce mode. */
unsigned int CheckCCSEnforce(const unsigned int index)
{
	return CheckCCSFlags(index) == 3;
}

/* Check whether the given access control is accept mode. */
unsigned int CheckCCSAccept(const unsigned int index)
{
	return CheckCCSFlags(index) == 1;
}

unsigned int GetMaxAutoAppendFiles(void)
{
	return ccs_control_array[CCS_TOMOYO_MAX_ACCEPT_FILES].current_value;
}

unsigned int GetMaxGrantLog(void)
{
	return ccs_control_array[CCS_TOMOYO_MAX_GRANT_LOG].current_value;
}

unsigned int GetMaxRejectLog(void)
{
	return ccs_control_array[CCS_TOMOYO_MAX_REJECT_LOG].current_value;
}

static int SetStatus(char *data, void **dummy)
{
	unsigned int value;
	int i;
	char *cp = strrchr(data, '=');
	if (!isRoot()) return -EPERM;
	if (!cp) return -EINVAL;
	*cp = '\0';
	if (sscanf(cp + 1, "%u", &value) != 1) return -EINVAL;
#ifdef CONFIG_TOMOYO_MAC_FOR_CAPABILITY
	if (strncmp(data, KEYWORD_MAC_FOR_CAPABILITY, KEYWORD_MAC_FOR_CAPABILITY_LEN) == 0) {
		return SetCapabilityStatus(data + KEYWORD_MAC_FOR_CAPABILITY_LEN, value);
	}
#endif
	for (i = 0; i < CCS_MAX_CONTROL_INDEX; i++) {
		if (strcmp(data, ccs_control_array[i].keyword)) continue;
		if (value > ccs_control_array[i].max_value) value = ccs_control_array[i].max_value;
		ccs_control_array[i].current_value = value;
		return 0;
	}
	return -EINVAL;
}

static int LoadProfile(void)
{
	struct file *f;
	char buffer[64];
	memset(buffer, 0, sizeof(buffer));
	snprintf(buffer, sizeof(buffer) - 1, POLICY_FILE_LOCATION "/profile%u.txt", ccs_profile_index);
	f = filp_open(buffer, O_RDONLY, 0600);
	if (!IS_ERR(f)) {
		if (f->f_dentry->d_inode->i_size > 0 && ReadFileAndProcess(f, SetStatus) < 0) panic("%s: FATAL: Failed to load.\n", __FUNCTION__);
		filp_close(f, NULL);
		printk("%s: Loading profiles done.\n", __FUNCTION__);
	} else {
		printk("%s: Can't load %s\n", __FUNCTION__, buffer);
		if (panic_if_load_profile_fails) panic("%s: Failed to load profile.\n", __FUNCTION__);
	}
	return 0;
}

static int ReadStatus(IO_BUFFER *head)
{
	if (!head->read_eof) {
		int i;
		if (!isRoot()) return -EPERM;
		if (!head->read_var2) {
			for (i = head->read_step; i < CCS_MAX_CONTROL_INDEX; i++) {
				head->read_step = i;
				switch (i) {
				case -1: // Dummy
#ifndef CONFIG_TOMOYO_MAC_FOR_FILE
				case CCS_TOMOYO_MAC_FOR_FILE:
				case CCS_TOMOYO_MAX_ACCEPT_FILES:
#endif
#ifndef CONFIG_TOMOYO_MAC_FOR_ARGV0
				case CCS_TOMOYO_MAC_FOR_ARGV0:
#endif
#ifndef CONFIG_TOMOYO_MAC_FOR_NETWORKPORT
				case CCS_TOMOYO_MAC_FOR_BINDPORT:
				case CCS_TOMOYO_MAC_FOR_CONNECTPORT:
#endif
#ifndef CONFIG_TOMOYO_MAC_FOR_NETWORK
				case CCS_TOMOYO_MAC_FOR_NETWORK:
#endif
#ifndef CONFIG_TOMOYO_MAC_FOR_SIGNAL
				case CCS_TOMOYO_MAC_FOR_SIGNAL:
#endif
#ifndef CONFIG_SAKURA_DENY_CONCEAL_MOUNT
				case CCS_SAKURA_DENY_CONCEAL_MOUNT:
#endif
#ifndef CONFIG_SAKURA_RESTRICT_CHROOT
				case CCS_SAKURA_RESTRICT_CHROOT:
#endif
#ifndef CONFIG_SAKURA_RESTRICT_MOUNT
				case CCS_SAKURA_RESTRICT_MOUNT:
#endif
#ifndef CONFIG_SAKURA_RESTRICT_UNMOUNT
				case CCS_SAKURA_RESTRICT_UNMOUNT:
#endif
#ifndef CONFIG_SAKURA_DENY_PIVOT_ROOT
				case CCS_SAKURA_DENY_PIVOT_ROOT:
#endif
#ifndef CONFIG_SAKURA_TRACE_READONLY
				case CCS_SAKURA_TRACE_READONLY:
#endif
#ifndef CONFIG_SAKURA_RESTRICT_AUTOBIND
				case CCS_SAKURA_RESTRICT_AUTOBIND:
#endif
#ifndef CONFIG_TOMOYO
				case CCS_TOMOYO_MAX_GRANT_LOG:
				case CCS_TOMOYO_MAX_REJECT_LOG:
				case CCS_TOMOYO_VERBOSE:
#endif
					continue;
				}
				if (io_printf(head, "%s=%u\n", ccs_control_array[i].keyword, ccs_control_array[i].current_value)) break;
			}
			if (i == CCS_MAX_CONTROL_INDEX) {
				head->read_var2 = (void *) 1;
				head->read_step = 0;
			}
		}
		if (head->read_var2) {
#ifdef CONFIG_TOMOYO_MAC_FOR_CAPABILITY
			if (ReadCapabilityStatus(head) == 0) head->read_eof = 1;
#else
			head->read_eof = 1;
#endif
		}
	}
	return 0;
}

/*************************  POLICY MANAGER HANDLER  *************************/

/* Definition file for programs that can update policies via /proc interface . */
#define POLICY_MANAGER_FILE           POLICY_FILE_LOCATION "/manager.txt"

typedef struct policy_manager_entry {
	struct policy_manager_entry *next; /* Pointer to next record. NULL if none. */
	const char *exe;                   /* Filename. Never NULL.                 */
} POLICY_MANAGER_ENTRY;

static POLICY_MANAGER_ENTRY policy_manager_list = { NULL, "" };

static int AddPolicyManagerPolicy(char *exe, void **dummy)
{
	POLICY_MANAGER_ENTRY *new_entry, *ptr;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	const char *saved_exe;
	if (!isRoot()) return -EPERM;
	if (!IsCorrectPath(exe, 0) || strendswith(exe, "/")) {
		printk(KERN_DEBUG "%s: Invalid filename '%s'\n", __FUNCTION__, exe);
		return -EINVAL;
	}
	/* I don't want to add if it was already added. */
	for (ptr = policy_manager_list.next; ptr; ptr = ptr->next) if (strcmp(ptr->exe, exe) == 0) return 0;
	if ((saved_exe = SaveName(exe)) == NULL || (new_entry = (POLICY_MANAGER_ENTRY *) alloc_element(sizeof(POLICY_MANAGER_ENTRY))) == NULL) return -ENOMEM;
	new_entry->exe = saved_exe;
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	for (ptr = &policy_manager_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

/* Check whether the current process is a policy manager. */
static int IsPolicyManager(void)
{
	POLICY_MANAGER_ENTRY *ptr;
	const char *exe;
	if ((exe = GetEXE()) == NULL) return 0;
	for (ptr = policy_manager_list.next; ptr; ptr = ptr->next) {
		if (strcmp(exe, ptr->exe) == 0) break;
	}
	if (!ptr) { /* Reduce error messages. */
		static pid_t last_pid = 0;
		const pid_t pid = current->pid;
		if (last_pid != pid) {
			printk("%s is not permitted to update policies.\n", exe);
			last_pid = pid;
		}
	}
	ccs_free(exe);
	return ptr ? 1 : 0;
}

static int LoadPolicyManager(void)
{
	struct file *f = filp_open(POLICY_MANAGER_FILE, O_RDONLY, 0600);
	if (!IS_ERR(f)) {
		if (f->f_dentry->d_inode->i_size > 0 && ReadFileAndProcess(f, AddPolicyManagerPolicy) < 0) panic("%s: Failed to load.\n", __FUNCTION__);
		filp_close(f, NULL);
		printk("%s: Loading policy managers done.\n", __FUNCTION__);
	}
	if (!policy_manager_list.next) printk("%s: No program can update policy via /proc interface.\n", __FUNCTION__);
	return 0;
}

#ifdef CONFIG_TOMOYO

#ifdef CONFIG_TOMOYO_MAC_FOR_FILE

/*************************  PERMISSION MAPPING HANDLER  *************************/

/* Definition file for permission mapping. */
#define POLICY_MAPPING_FILE           POLICY_FILE_LOCATION "/mapping.txt"

static int LoadPermissionMapping(void)
{
	struct file *f = filp_open(POLICY_MAPPING_FILE, O_RDONLY, 0600);
	if (!IS_ERR(f)) {
		if (f->f_dentry->d_inode->i_size > 0 && ReadFileAndProcess(f, SetPermissionMapping) < 0) panic("%s: Failed to load.\n", __FUNCTION__);
		filp_close(f, NULL);
		printk("%s: Loading permission mappings done.\n", __FUNCTION__);
	}
	return 0;
}
#endif

/*************************  DOMAIN POLICY HANDLER  *************************/

/* Definition file for domains policy. */
#define DOMAIN_POLICY_FILE           POLICY_FILE_LOCATION "/domain_policy.txt"

static int AddDomainPolicy(char *data, void **domain)
{
	int is_delete = 0, is_select = 0;
	if (!isRoot()) return -EPERM;
	if (strncmp(data, KEYWORD_DELETE, KEYWORD_DELETE_LEN) == 0) {
		data += KEYWORD_DELETE_LEN;
		is_delete = 1;
	} else if (strncmp(data, KEYWORD_SELECT, KEYWORD_SELECT_LEN) == 0) {
		data += KEYWORD_SELECT_LEN;
		is_select = 1;
	}
	if (IsDomainDef(data)) {
		if (is_delete) {
			DeleteDomain(data);
			*domain = NULL;
		} else if (is_select) {
			*domain = FindDomain(data);
		} else {
			*domain = FindOrAssignNewDomain(data);
		}
		return 0;
#ifdef CONFIG_TOMOYO_MAC_FOR_CAPABILITY
	} else if (strncmp(data, KEYWORD_ALLOW_CAPABILITY, KEYWORD_ALLOW_CAPABILITY_LEN) == 0) {
		return AddCapabilityPolicy(data + KEYWORD_ALLOW_CAPABILITY_LEN, (struct domain_info *) *domain, is_delete);
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_NETWORKPORT
	} else if (strncmp(data, KEYWORD_ALLOW_BIND, KEYWORD_ALLOW_BIND_LEN) == 0 ||
			   strncmp(data, KEYWORD_ALLOW_CONNECT, KEYWORD_ALLOW_CONNECT_LEN) == 0) {
		return AddPortPolicy(data, (struct domain_info *) *domain, is_delete);
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_NETWORK
	} else if (strncmp(data, KEYWORD_ALLOW_NETWORK, KEYWORD_ALLOW_NETWORK_LEN) == 0) {
		return AddNetworkPolicy(data + KEYWORD_ALLOW_NETWORK_LEN, (struct domain_info *) *domain, is_delete);
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_SIGNAL
	} else if (strncmp(data, KEYWORD_ALLOW_SIGNAL, KEYWORD_ALLOW_SIGNAL_LEN) == 0) {
		return AddSignalPolicy(data + KEYWORD_ALLOW_SIGNAL_LEN, (struct domain_info *) *domain, is_delete);
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_ARGV0
	} else if (strncmp(data, KEYWORD_ALLOW_ARGV0, KEYWORD_ALLOW_ARGV0_LEN) == 0) {
		return AddArgv0Policy(data + KEYWORD_ALLOW_ARGV0_LEN, (struct domain_info *) *domain, is_delete);
#endif
	} else {
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
		return AddFilePolicy(data, (struct domain_info *) *domain, is_delete);
#endif
	}
	return -EINVAL;
}

static int LoadDomainPolicy(void)
{
	struct file *f = filp_open(DOMAIN_POLICY_FILE, O_RDONLY, 0600);
	if (!IS_ERR(f)) {
		if (f->f_dentry->d_inode->i_size > 0 && ReadFileAndProcess(f, AddDomainPolicy) < 0) panic("%s: FATAL: Failed to load.\n", __FUNCTION__);
		filp_close(f, NULL);
		printk("%s: Loading domain policy done.\n", __FUNCTION__);
	} else {
		ccs_control_array[CCS_TOMOYO_VERBOSE].current_value = 0;
		printk("%s: Can't load domain policy.\n", __FUNCTION__);
	}
	return 0;
}

static int ReadDomainPolicy(IO_BUFFER *head)
{
	if (!head->read_eof) {
		struct domain_info *domain = (struct domain_info *) head->read_var1;
		switch (head->read_step) {
		case 0: break;
		case 1: goto step1;
		case 2: goto step2;
		case 3: goto step3;
		default: return -EINVAL;
		}
		if (!isRoot()) return -EPERM;
		for (domain = &KERNEL_DOMAIN; domain; domain = domain->next) {
			struct acl_info *ptr;
			if (GetDomainAttribute(domain) & DOMAIN_ATTRIBUTE_DELETED) continue;
			head->read_var1 = (void *) domain;
			head->read_var2 = NULL; head->read_step = 1;
		step1:
			if (io_printf(head, "%s\n\n", domain->domainname)) break;
			head->read_var2 = (void *) domain->first_acl_ptr; head->read_step = 2;
		step2:
			for (ptr = (struct acl_info *) head->read_var2; ptr; ptr = ptr->next) {
				const unsigned int acl_type = GET_ACL_TYPE(ptr->type_hash);
				const int pos = head->read_avail;
				head->read_var2 = (void *) ptr;
				if (0) {
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
				} else if (acl_type == TYPE_FILE_ACL) {
					if (io_printf(head, "%d %s", ((FILE_ACL_RECORD *) ptr)->perm, ((FILE_ACL_RECORD *) ptr)->filename)
						|| DumpCondition(head, ptr->cond)) {
						head->read_avail = pos; break;
					}
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_ARGV0
				} else if (acl_type == TYPE_ARGV0_ACL) {
					if (io_printf(head, KEYWORD_ALLOW_ARGV0 "%s %s", ((ARGV0_ACL_RECORD *) ptr)->filename, ((ARGV0_ACL_RECORD *) ptr)->argv0) ||
						DumpCondition(head, ptr->cond)) {
						head->read_avail = pos; break;
					}
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_CAPABILITY
				} else if (acl_type == TYPE_CAPABILITY_ACL) {
					if (io_printf(head, KEYWORD_ALLOW_CAPABILITY "%s", capability2keyword(GET_ACL_HASH(ptr->type_hash))) ||
						DumpCondition(head, ptr->cond)) {
						head->read_avail = pos; break;
					}
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_NETWORKPORT
				} else if (acl_type == TYPE_BIND_ACL || acl_type == TYPE_CONNECT_ACL) {
					const int is_stream = GET_ACL_HASH(ptr->type_hash);
					const u16 min_port = ((NETWORK_ACL_RECORD *) ptr)->min_port, max_port = ((NETWORK_ACL_RECORD *) ptr)->max_port;
					if (min_port != max_port) {
						if (io_printf(head, "%s%s/%u-%u", acl_type == TYPE_CONNECT_ACL ? KEYWORD_ALLOW_CONNECT : KEYWORD_ALLOW_BIND, is_stream ? "TCP" : "UDP", min_port, max_port) ||
							DumpCondition(head, ptr->cond)) {
							head->read_avail = pos; break;
						}
					} else {
						if (io_printf(head, "%s%s/%u", acl_type == TYPE_CONNECT_ACL ? KEYWORD_ALLOW_CONNECT : KEYWORD_ALLOW_BIND, is_stream ? "TCP" : "UDP", min_port) ||
							DumpCondition(head, ptr->cond)) {
							head->read_avail = pos; break;
						}
					}
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_NETWORK
				} else if (acl_type == TYPE_IPv4_NETWORK_ACL) {
					const char *keyword = network2keyword(GET_ACL_HASH(ptr->type_hash));
					const u32 min_address = ((IPv4_NETWORK_ACL_RECORD *) ptr)->min_address, max_address = ((IPv4_NETWORK_ACL_RECORD *) ptr)->max_address;
					const u16 min_port = ((IPv4_NETWORK_ACL_RECORD *) ptr)->min_port, max_port = ((IPv4_NETWORK_ACL_RECORD *) ptr)->max_port;
					if (min_address != max_address) {
						if (min_port != max_port) {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %d.%d.%d.%d-%d.%d.%d.%d %u-%u", keyword, HIPQUAD(min_address), HIPQUAD(max_address), min_port, max_port) || DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						} else {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %d.%d.%d.%d-%d.%d.%d.%d %u", keyword, HIPQUAD(min_address), HIPQUAD(max_address), min_port) || DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						}
					} else {
						if (min_port != max_port) {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %d.%d.%d.%d %u-%u", keyword, HIPQUAD(min_address), min_port, max_port) || DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						} else {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %d.%d.%d.%d %u", keyword, HIPQUAD(min_address), min_port) || DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						}
					}
				} else if (acl_type == TYPE_IPv6_NETWORK_ACL) {
					const char *keyword = network2keyword(GET_ACL_HASH(ptr->type_hash));
					const u8 *min_address = ((IPv6_NETWORK_ACL_RECORD *) ptr)->min_address, *max_address = ((IPv6_NETWORK_ACL_RECORD *) ptr)->max_address;
					const u16 min_port = ((IPv6_NETWORK_ACL_RECORD *) ptr)->min_port, max_port = ((IPv6_NETWORK_ACL_RECORD *) ptr)->max_port;
					char buf1[64], buf2[64];
					print_ipv6(buf1, sizeof(buf1), (const u16 *) min_address);
					print_ipv6(buf2, sizeof(buf2), (const u16 *) max_address);
					if (memcmp(min_address, max_address, 16)) {
						if (min_port != max_port) {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %s-%s %u-%u", keyword, buf1, buf2, min_port, max_port)|| DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						} else {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %s-%s %u", keyword, buf1, buf2, min_port) || DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						}
					} else {
						if (min_port != max_port) {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %s %u-%u", keyword, buf1, min_port, max_port) || DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						} else {
							if (io_printf(head, KEYWORD_ALLOW_NETWORK "%s %s %u", keyword, buf1, min_port) || DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						}
					}
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_SIGNAL
				} else if (acl_type == TYPE_SIGNAL_ACL) {
					if (io_printf(head, KEYWORD_ALLOW_SIGNAL "%u %s", GET_ACL_HASH(ptr->type_hash), ((SIGNAL_ACL_RECORD *) ptr)->domainname) ||
						DumpCondition(head, ptr->cond)) {
						head->read_avail = pos; break;
					}
#endif
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
				} else {
					const char *keyword = acltype2keyword(acl_type);
					if (keyword) {
						if (acltype2paths(acl_type) == 2) {
							if (io_printf(head, "allow_%s %s %s", keyword, ((DOUBLE_ACL_RECORD *) ptr)->filename1, ((DOUBLE_ACL_RECORD *) ptr)->filename2)
								|| DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						} else {
							if (io_printf(head, "allow_%s %s", keyword, ((SINGLE_ACL_RECORD *) ptr)->filename)
								|| DumpCondition(head, ptr->cond)) {
								head->read_avail = pos; break;
							}
						}
					}
#endif
				}
			}
			if (ptr) break;
			head->read_var2 = NULL; head->read_step = 3;
		step3:
			if (io_printf(head, "\n")) break;
		}
		if (!domain) head->read_eof = 1;
	}
	return 0;
}

#endif

/*************************  EXCEPTION POLICY HANDLER  *************************/

#ifdef CONFIG_TOMOYO

/* Definition file for exception policy. */
#define EXCEPTION_POLICY_FILE           POLICY_FILE_LOCATION "/exception_policy.txt"

static int AddExceptionPolicy(char *data, void **domain)
{
	int is_delete = 0;
	if (!isRoot()) return -EPERM;
	if (strncmp(data, KEYWORD_DELETE, KEYWORD_DELETE_LEN) == 0) {
		data += KEYWORD_DELETE_LEN;
		is_delete = 1;
	}
	if (strncmp(data, KEYWORD_TRUST_DOMAIN, KEYWORD_TRUST_DOMAIN_LEN) == 0) {
		return AddTrustedPatternPolicy(data + KEYWORD_TRUST_DOMAIN_LEN, is_delete);
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
	} else if (strncmp(data, KEYWORD_ALLOW_READ, KEYWORD_ALLOW_READ_LEN) == 0) {
		return AddGloballyReadablePolicy(data + KEYWORD_ALLOW_READ_LEN, is_delete);
#endif
	} else if (strncmp(data, KEYWORD_INITIALIZER, KEYWORD_INITIALIZER_LEN) == 0) {
		return AddInitializerPolicy(data + KEYWORD_INITIALIZER_LEN, is_delete);
	} else if (strncmp(data, KEYWORD_ALIAS, KEYWORD_ALIAS_LEN) == 0) {
		return AddAliasPolicy(data + KEYWORD_ALIAS_LEN, is_delete);
	} else if (strncmp(data, KEYWORD_AGGREGATOR, KEYWORD_AGGREGATOR_LEN) == 0) {
		return AddAggregatorPolicy(data + KEYWORD_AGGREGATOR_LEN, is_delete);
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
	} else if (strncmp(data, KEYWORD_FILE_PATTERN, KEYWORD_FILE_PATTERN_LEN) == 0) {
		return AddPatternPolicy(data + KEYWORD_FILE_PATTERN_LEN, is_delete);
#endif
	}
	return -EINVAL;
}

static int LoadExceptionPolicy(void)
{
	struct file *f = filp_open(EXCEPTION_POLICY_FILE, O_RDONLY, 0600);
	if (!IS_ERR(f)) {
		if (f->f_dentry->d_inode->i_size > 0 && ReadFileAndProcess(f, AddExceptionPolicy) < 0) panic("%s: FATAL: Failed to load.\n", __FUNCTION__);
		filp_close(f, NULL);
		printk("%s: Loading exception policy done.\n", __FUNCTION__);
	} else {
		printk("%s: Can't load exception policy.\n", __FUNCTION__);
	}
	return 0;
}

static int ReadExceptionPolicy(IO_BUFFER *head)
{
	if (!head->read_eof) {
		switch (head->read_step) {
		case 0:
			if (!isRoot()) return -EPERM;
			head->read_var2 = NULL; head->read_step = 1;
		case 1:
			if (ReadTrustedPatternPolicy(head)) break;
			head->read_var2 = NULL; head->read_step = 2;
		case 2:
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
			if (ReadGloballyReadablePolicy(head)) break;
#endif
			head->read_var2 = NULL; head->read_step = 3;
		case 3:
			if (ReadInitializerPolicy(head)) break;
			head->read_var2 = NULL; head->read_step = 4;
		case 4:
			if (ReadAliasPolicy(head)) break;
			head->read_var2 = NULL; head->read_step = 5;
		case 5:
			if (ReadAggregatorPolicy(head)) break;
			head->read_var2 = NULL; head->read_step = 6;
		case 6:
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
			if (ReadPatternPolicy(head)) break;
#endif
			head->read_eof = 1;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

#endif

/*************************  SYSTEM POLICY HANDLER  *************************/

#ifdef CONFIG_SAKURA

/* Definition file for system policy. */
#define SYSTEM_POLICY_FILE           POLICY_FILE_LOCATION "/system_policy.txt"

static int AddSystemPolicy(char *data, void **domain)
{
	int is_delete = 0;
	if (!isRoot()) return -EPERM;
	if (strncmp(data, KEYWORD_DELETE, KEYWORD_DELETE_LEN) == 0) {
		data += KEYWORD_DELETE_LEN;
		is_delete = 1;
	}
#ifdef CONFIG_SAKURA_RESTRICT_MOUNT
	if (strncmp(data, KEYWORD_ALLOW_MOUNT, KEYWORD_ALLOW_MOUNT_LEN) == 0)
		return AddMountPolicy(data + KEYWORD_ALLOW_MOUNT_LEN, is_delete);
#endif
#ifdef CONFIG_SAKURA_RESTRICT_UNMOUNT
	if (strncmp(data, KEYWORD_DENY_UNMOUNT, KEYWORD_DENY_UNMOUNT_LEN) == 0)
		return AddNoUmountPolicy(data + KEYWORD_DENY_UNMOUNT_LEN, is_delete);
#endif
#ifdef CONFIG_SAKURA_RESTRICT_CHROOT
	if (strncmp(data, KEYWORD_ALLOW_CHROOT, KEYWORD_ALLOW_CHROOT_LEN) == 0)
		return AddChrootPolicy(data + KEYWORD_ALLOW_CHROOT_LEN, is_delete);
#endif
#ifdef CONFIG_SAKURA_RESTRICT_AUTOBIND
	if (strncmp(data, KEYWORD_DENY_AUTOBIND, KEYWORD_DENY_AUTOBIND_LEN) == 0)
		return AddReservedPortPolicy(data + KEYWORD_DENY_AUTOBIND_LEN, is_delete);
#endif
	return -EINVAL;
}

static int LoadSystemPolicy(void)
{
	struct file *f = filp_open(SYSTEM_POLICY_FILE, O_RDONLY, 0600);
	if (!IS_ERR(f)) {
		if (f->f_dentry->d_inode->i_size > 0 && ReadFileAndProcess(f, AddSystemPolicy) < 0) panic("%s: FATAL: Failed to load.\n", __FUNCTION__);
		filp_close(f, NULL);
		printk("%s: Loading system policy done.\n", __FUNCTION__);
	} else {
		printk("%s: Can't load system policy.\n", __FUNCTION__);
	}
	return 0;
}

static int ReadSystemPolicy(IO_BUFFER *head)
{
	if (!head->read_eof) {
		switch (head->read_step) {
		case 0:
			if (!isRoot()) return -EPERM;
			head->read_var2 = NULL; head->read_step = 1;
		case 1:
#ifdef CONFIG_SAKURA_RESTRICT_MOUNT
			if (ReadMountPolicy(head)) break;
#endif
			head->read_var2 = NULL; head->read_step = 2;
		case 2:
#ifdef CONFIG_SAKURA_RESTRICT_UNMOUNT
			if (ReadNoUmountPolicy(head)) break;
#endif
			head->read_var2 = NULL; head->read_step = 3;
		case 3:
#ifdef CONFIG_SAKURA_RESTRICT_CHROOT
			if (ReadChrootPolicy(head)) break;
#endif
			head->read_var2 = NULL; head->read_step = 4;
		case 4:
#ifdef CONFIG_SAKURA_RESTRICT_AUTOBIND
			if (ReadReservedPortPolicy(head)) break;
#endif
			head->read_eof = 1;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

#endif

/*************************  POLICY LOADER  *************************/

void CCS_LoadPolicy(const char *filename)
{
	static int init_started = 0;
	if (init_started) return;
	/*
	 * Check filename is /sbin/init or /sbin/ccs-start .
	 * /sbin/ccs-start is a dummy filename in case where /sbin/init can't be passed.
	 * You can create /sbin/ccs-start by "ln -s /bin/true /sbin/ccs-start", for
	 * only the pathname is needed to load policies.
	 */
	if (strcmp(filename, "/sbin/init") != 0 && strcmp(filename, "/sbin/ccs-start") != 0) return;
	/*
	 * Don't try to load policy if POLICY_FILE_LOCATION doesn't exist.
	 * If initrd.img includes /sbin/init but real-root-dev has not mounted on / yet,
	 * an attempt to load policy fails because POLICY_FILE_LOCATION doesn't exist.
	 * So let do_execve() call this function everytime.
	 */
	{
		struct nameidata nd;
		int error = path_lookup(POLICY_FILE_LOCATION, lookup_flags, &nd);
		if (error) return;
		path_release(&nd);
	}
	/* Now try to load policies. */
	init_started = 1;
	LoadPolicyManager();
#ifdef CONFIG_SAKURA
	printk("SAKURA: 1.2   2006/09/03\n");
	printk("SAKURA is loading policy. Please wait.\n");
	LoadSystemPolicy();
#endif
#ifdef CONFIG_TOMOYO
	printk("TOMOYO: 1.2   2006/09/03 (hotfix 2006/09/30)\n");
	printk("TOMOYO is loading policy. Please wait.\n");
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
	LoadPermissionMapping();
#endif
	LoadExceptionPolicy();
	if (skip_domain_policy_loading == 0) LoadDomainPolicy();
	{
		struct domain_info *domain;
		int domain_count = 0;
		int acl_count = 0;
		for (domain = &KERNEL_DOMAIN; domain; domain = domain->next) {
			struct acl_info *ptr = domain->first_acl_ptr;
			while (ptr) { ptr = ptr->next; acl_count++; };
			domain_count++;
		}
		printk("TOMOYO: %d domains. %d ACL entries. %d KB shared. %d KB private.\n", domain_count, acl_count, GetMemoryUsedForSaveName() / 1024, GetMemoryUsedForElements() / 1024);
	}
#endif
	LoadProfile();
	printk("Mandatory Access Control activated.\n");
	sbin_init_started = 1;
	ccs_log_level = KERN_WARNING;
}


/*************************  MAC Decision Delayer  *************************/

static DECLARE_WAIT_QUEUE_HEAD(query_wait);

static spinlock_t query_lock = SPIN_LOCK_UNLOCKED;

typedef struct query_entry {
	struct list_head list;
	unsigned int serial;
	char *query;
	int answer;
} QUERY_ENTRY;

static LIST_HEAD(query_list);

int CheckSupervisor(const char *fmt, ...)
{
	va_list args;
	int error = -EPERM;
	int i, len;
	static unsigned int serial = 0;
	QUERY_ENTRY *query_entry;
	if (!CheckCCSFlags(CCS_MAX_ENFORCE_GRACE)) return -EPERM;
	va_start(args, fmt);
	len = vsnprintf((char *) &i, sizeof(i) - 1, fmt, args) + 16;
	va_end(args);
	if ((query_entry = (QUERY_ENTRY *) ccs_alloc(sizeof(QUERY_ENTRY))) == NULL ||
		(query_entry->query = ccs_alloc(len)) == NULL) goto out;
	INIT_LIST_HEAD(&query_entry->list);
	/***** CRITICAL SECTION START *****/
	spin_lock(&query_lock);
	query_entry->serial = serial++;
	i = snprintf(query_entry->query, len - 1, "Q%u\n", query_entry->serial);
	va_start(args, fmt);
	vsnprintf(query_entry->query + i, len - 1 - i, fmt, args);
	va_end(args);
	list_add_tail(&query_entry->list, &query_list);
	spin_unlock(&query_lock);
	/***** CRITICAL SECTION END *****/
	for (i = 0; i < CheckCCSFlags(CCS_MAX_ENFORCE_GRACE); i++) {
		wake_up(&query_wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
		if (query_entry->answer) break;
	}
	/***** CRITICAL SECTION START *****/
	spin_lock(&query_lock);
	list_del(&query_entry->list);
	spin_unlock(&query_lock);
	/***** CRITICAL SECTION END *****/
	switch (query_entry->answer) {
	case 1:
		/* Granted by administrator. */
		error = 0;
		break;
	case 0:
		/* Timed out. */
		break;
	default:
		/* Rejected by administrator. */
		break;
	}
 out: ;
	/* query_entry->query is set to NULL by ReadQuery(). */
	if (query_entry) ccs_free(query_entry->query);
	ccs_free(query_entry);
	return error;
}

static int PollQuery(struct file *file, poll_table *wait)
{
	int found = 0;
	struct list_head *tmp;
	/***** CRITICAL SECTION START *****/
	spin_lock(&query_lock);
	list_for_each(tmp, &query_list) {
		QUERY_ENTRY *ptr = list_entry(tmp, QUERY_ENTRY, list);
		if (!ptr->query) continue;
		found = 1;
		break;
	}
	spin_unlock(&query_lock);
	/***** CRITICAL SECTION END *****/
	if (found) return POLLIN | POLLRDNORM;
	poll_wait(file, &query_wait, wait);
	/***** CRITICAL SECTION START *****/
	spin_lock(&query_lock);
	list_for_each(tmp, &query_list) {
		QUERY_ENTRY *ptr = list_entry(tmp, QUERY_ENTRY, list);
		if (!ptr->query) continue;
		found = 1;
		break;
	}
	spin_unlock(&query_lock);
	/***** CRITICAL SECTION END *****/
	if (found) return POLLIN | POLLRDNORM;
	return 0;
}

static int ReadQuery(IO_BUFFER *head)
{
	struct list_head *tmp;
	if (head->read_avail) return 0;
	if (head->read_buf) {
		ccs_free(head->read_buf); head->read_buf = NULL;
		head->readbuf_size = 0;
	}
	/***** CRITICAL SECTION START *****/
	spin_lock(&query_lock);
	list_for_each(tmp, &query_list) {
		QUERY_ENTRY *ptr = list_entry(tmp, QUERY_ENTRY, list);
		if (!ptr->query) continue;
		head->readbuf_size = head->read_avail = strlen(ptr->query) + 1;
		head->read_buf = ptr->query; ptr->query = NULL;
		break;
	}
	spin_unlock(&query_lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

static int WriteAnswer(char *data, void **dummy)
{
	struct list_head *tmp;
	unsigned int serial, answer;
	if (sscanf(data, "A%u=%u", &serial, &answer) != 2) return -EINVAL;
	/***** CRITICAL SECTION START *****/
	spin_lock(&query_lock);
	list_for_each(tmp, &query_list) {
		QUERY_ENTRY *ptr = list_entry(tmp, QUERY_ENTRY, list);
		if (ptr->serial != serial) continue;
		if (!ptr->answer) ptr->answer = answer;
		break;
	}
	spin_unlock(&query_lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

/*************************  /proc INTERFACE HANDLER  *************************/

static int ReadMemoryCounter(IO_BUFFER *head)
{
	if (!head->read_eof) {
		const int shared = GetMemoryUsedForSaveName(), private = GetMemoryUsedForElements(), dynamic = GetMemoryUsedForDynamic();
		if (io_printf(head, "Shared:  %10u\nPrivate: %10u\nDynamic: %10u\nTotal:   %10u\n", shared, private, dynamic, shared + private + dynamic) == 0) head->read_eof = 1;
	}
	return 0;
}

int CCS_OpenControl(const int type, struct file *file)
{
	IO_BUFFER *head = (IO_BUFFER *) ccs_alloc(sizeof(IO_BUFFER));
	if (!head) return -ENOMEM;
	init_MUTEX(&head->read_sem);
	init_MUTEX(&head->write_sem);
	switch (type) {
#ifdef CONFIG_TOMOYO
	case CCS_POLICY_DOMAINPOLICY:
		head->write = AddDomainPolicy;
		head->read = ReadDomainPolicy;
		break;
	case CCS_POLICY_EXCEPTIONPOLICY:
		head->write = AddExceptionPolicy;
		head->read = ReadExceptionPolicy;
		break;
	case CCS_INFO_TRUSTEDPIDS:
		head->read = ReadTrustedPIDs;
		break;
	case CCS_INFO_DELETEDPIDS:
		head->read = ReadDeletedPIDs;
		break;
#ifdef CONFIG_TOMOYO_AUDIT
	case CCS_INFO_GRANTLOG:
		head->poll = PollGrantLog;
		head->read = ReadGrantLog;
		break;
	case CCS_INFO_REJECTLOG:
		head->poll = PollRejectLog;
		head->read = ReadRejectLog;
		break;
#endif
	case CCS_INFO_SELFDOMAIN:
		head->read = ReadSelfDomain;
		break;
#ifdef CONFIG_TOMOYO_MAC_FOR_FILE
	case CCS_INFO_MAPPING:
		head->read = ReadPermissionMapping;
		break;
#endif
#endif
#ifdef CONFIG_SAKURA
	case CCS_POLICY_SYSTEMPOLICY:
		head->write = AddSystemPolicy;
		head->read = ReadSystemPolicy;
		break;
#endif
	case CCS_INFO_MEMINFO:
		head->read = ReadMemoryCounter;
		head->readbuf_size = 128;
		break;
	case CCS_STATUS:
		head->write = SetStatus;
		head->read = ReadStatus;
		break;
	case CCS_POLICY_QUERY:
		head->poll = PollQuery;
		head->write = WriteAnswer;
		head->read = ReadQuery;
		break;
	}
	if (type != CCS_INFO_GRANTLOG && type != CCS_INFO_REJECTLOG && type != CCS_POLICY_QUERY) {
		if (!head->readbuf_size) head->readbuf_size = PAGE_SIZE * 2;
		if ((head->read_buf = ccs_alloc(head->readbuf_size)) == NULL) {
			ccs_free(head);
			return -ENOMEM;
		}
	}
	if (head->write) {
		head->writebuf_size = PAGE_SIZE * 2;
		if ((head->write_buf = ccs_alloc(head->writebuf_size)) == NULL) {
			ccs_free(head->read_buf);
			ccs_free(head);
			return -ENOMEM;
		}
	}
	file->private_data = head;
	return 0;
}

static int CopyToUser(IO_BUFFER *head, char __user * buffer, int buffer_len)
{
	int len = head->read_avail;
	char *cp = head->read_buf;
	if (len > buffer_len) len = buffer_len;
	if (len) {
		if (copy_to_user(buffer, cp, len)) return -EFAULT;
		head->read_avail -= len;
		memmove(cp, cp + len, head->read_avail);
	}
	return len;
}

int CCS_PollControl(struct file *file, poll_table *wait)
{
	IO_BUFFER *head = (IO_BUFFER *) file->private_data;
	if (!head->poll) return -ENOSYS;
	return head->poll(file, wait);
}

int CCS_ReadControl(struct file *file, char __user *buffer, const int buffer_len)
{
	int len = 0;
	IO_BUFFER *head = (IO_BUFFER *) file->private_data;
	if (!head->read) return -ENOSYS;
	if (!access_ok(VERIFY_WRITE, buffer, buffer_len)) return -EFAULT;
	if (down_interruptible(&head->read_sem)) return -EINTR;
	len = head->read(head);
	if (len >= 0) len = CopyToUser(head, buffer, buffer_len);
	up(&head->read_sem);
	return len;
}

int CCS_WriteControl(struct file *file, const char __user *buffer, const int buffer_len)
{
	IO_BUFFER *head = (IO_BUFFER *) file->private_data;
	void *ptr;
	int error = buffer_len;
	int avail_len = buffer_len;
	char *cp0 = head->write_buf;
	if (!head->write) return -ENOSYS;
	if (!access_ok(VERIFY_READ, buffer, buffer_len)) return -EFAULT;
	if (!isRoot()) return -EPERM;
	if (!IsPolicyManager()) {
		return -EPERM; /* Forbid updating policies for non manager programs. */
	}
	if (down_interruptible(&head->write_sem)) return -EINTR;
	ptr = head->write_var1;
	while (avail_len > 0) {
		char c;
		if (head->write_avail >= head->writebuf_size - 1) {
			error = -ENOMEM;
			break;
		} else if (get_user(c, buffer)) {
			error = -EFAULT;
			break;
		}
		buffer++; avail_len--;
		cp0[head->write_avail++] = c;
		if (c != '\n') continue;
		cp0[head->write_avail - 1] = '\0';
		head->write_avail = 0;
		NormalizeLine(cp0);
		head->write(cp0, &ptr);
	}
	head->write_var1 = ptr;
	up(&head->write_sem);
	return error;
}


int CCS_CloseControl(struct file *file)
{
	IO_BUFFER *head = file->private_data;
	ccs_free(head->read_buf); head->read_buf = NULL;
	ccs_free(head->write_buf); head->write_buf = NULL;
	ccs_free(head); head = NULL;
	file->private_data = NULL;
	return 0;
}

EXPORT_SYMBOL(CheckCCSFlags);
EXPORT_SYMBOL(CheckCCSEnforce);
EXPORT_SYMBOL(CheckCCSAccept);
