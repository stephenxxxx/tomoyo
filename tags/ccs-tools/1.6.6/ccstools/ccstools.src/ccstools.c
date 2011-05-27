/*
 * ccstools.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 1.6.6   2009/02/02
 *
 */
#include "ccstools.h"

/***** UTILITY FUNCTIONS START *****/

void out_of_memory(void)
{
	fprintf(stderr, "Out of memory. Aborted.\n");
	exit(1);
}

bool str_starts(char *str, const char *begin)
{
	const int len = strlen(begin);
	if (strncmp(str, begin, len))
		return false;
	memmove(str, str + len, strlen(str + len) + 1);
	return true;
}

static bool is_byte_range(const char *str)
{
	return *str >= '0' && *str++ <= '3' &&
		*str >= '0' && *str++ <= '7' &&
		*str >= '0' && *str <= '7';
}

static bool is_decimal(const char c)
{
	return c >= '0' && c <= '9';
}

static bool is_hexadecimal(const char c)
{
	return (c >= '0' && c <= '9') ||
		(c >= 'A' && c <= 'F') ||
		(c >= 'a' && c <= 'f');
}

static bool is_alphabet_char(const char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static u8 make_byte(const u8 c1, const u8 c2, const u8 c3)
{
	return ((c1 - '0') << 6) + ((c2 - '0') << 3) + (c3 - '0');
}

void normalize_line(unsigned char *line)
{
	unsigned char *sp = line;
	unsigned char *dp = line;
	bool first = true;
	while (*sp && (*sp <= ' ' || 127 <= *sp))
		sp++;
	while (*sp) {
		if (!first)
			*dp++ = ' ';
		first = false;
		while (' ' < *sp && *sp < 127)
			*dp++ = *sp++;
		while (*sp && (*sp <= ' ' || 127 <= *sp))
			sp++;
	}
	*dp = '\0';
}

char *make_filename(const char *prefix, const time_t time)
{
	struct tm *tm = localtime(&time);
	static char filename[1024];
	memset(filename, 0, sizeof(filename));
	snprintf(filename, sizeof(filename) - 1,
		 "%s.%02d-%02d-%02d.%02d:%02d:%02d.conf",
		 prefix, tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);
	return filename;
}

/* Copied from kernel source. */
static inline unsigned long partial_name_hash(unsigned long c,
					      unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/* Copied from kernel source. */
static inline unsigned int full_name_hash(const unsigned char *name,
					  unsigned int len)
{
	unsigned long hash = 0;
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return (unsigned int) hash;
}

static void *alloc_element(const unsigned int size)
{
	static char *buf = NULL;
	static unsigned int buf_used_len = PAGE_SIZE;
	char *ptr = NULL;
	if (size > PAGE_SIZE)
		return NULL;
	if (buf_used_len + size > PAGE_SIZE) {
		ptr = malloc(PAGE_SIZE);
		if (!ptr)
			out_of_memory();
		buf = ptr;
		memset(buf, 0, PAGE_SIZE);
		buf_used_len = size;
		ptr = buf;
	} else if (size) {
		int i;
		ptr = buf + buf_used_len;
		buf_used_len += size;
		for (i = 0; i < size; i++)
			if (ptr[i])
				out_of_memory();
	}
	return ptr;
}

static int path_depth(const char *pathname)
{
	int i = 0;
	if (pathname) {
		char *ep = strchr(pathname, '\0');
		if (pathname < ep--) {
			if (*ep != '/')
				i++;
			while (pathname <= ep)
				if (*ep-- == '/')
					i += 2;
		}
	}
	return i;
}

static int const_part_length(const char *filename)
{
	int len = 0;
	if (filename) {
		while (true) {
			char c = *filename++;
			if (!c)
				break;
			if (c != '\\') {
				len++;
				continue;
			}
			c = *filename++;
			switch (c) {
			case '\\':  /* "\\" */
				len += 2;
				continue;
			case '0':   /* "\ooo" */
			case '1':
			case '2':
			case '3':
				c = *filename++;
				if (c < '0' || c > '7')
					break;
				c = *filename++;
				if (c < '0' || c > '7')
					break;
				len += 4;
				continue;
			}
			break;
		}
	}
	return len;
}

bool is_domain_def(const unsigned char *domainname)
{
	return !strncmp(domainname, ROOT_NAME, ROOT_NAME_LEN) &&
		(domainname[ROOT_NAME_LEN] == '\0'
		 || domainname[ROOT_NAME_LEN] == ' ');
}

bool is_correct_domain(const unsigned char *domainname)
{
	if (!domainname || strncmp(domainname, ROOT_NAME, ROOT_NAME_LEN))
		goto out;
	domainname += ROOT_NAME_LEN;
	if (!*domainname)
		return true;
	do {
		if (*domainname++ != ' ')
			goto out;
		if (*domainname++ != '/')
			goto out;
		while (true) {
			unsigned char c = *domainname;
			if (!c || c == ' ')
				break;
			domainname++;
			if (c == '\\') {
				unsigned char d;
				unsigned char e;
				u8 f;
				c = *domainname++;
				switch (c) {
				case '\\':  /* "\\" */
					continue;
				case '0':   /* "\ooo" */
				case '1':
				case '2':
				case '3':
					d = *domainname++;
					if (d < '0' || d > '7')
						break;
					e = *domainname++;
					if (e < '0' || e > '7')
						break;
					f = (((u8) (c - '0')) << 6) +
						(((u8) (d - '0')) << 3) +
						(((u8) (e - '0')));
					/* pattern is not \000 */
					if (f && (f <= ' ' || f >= 127))
						continue;
				}
				goto out;
			} else if (c < ' ' || c >= 127) {
				goto out;
			}
		}
	} while (*domainname);
	return true;
out:
	return false;
}

void fprintf_encoded(FILE *fp, const char *pathname)
{
	while (true) {
		unsigned char c = *(const unsigned char *) pathname++;
		if (!c)
			break;
		if (c == '\\') {
			fputc('\\', fp);
			fputc('\\', fp);
		} else if (c > ' ' && c < 127) {
			fputc(c, fp);
		} else {
			fprintf(fp, "\\%c%c%c", (c >> 6) + '0',
				((c >> 3) & 7) + '0', (c & 7) + '0');
		}
	}
}

bool decode(const char *ascii, char *bin)
{
	while (true) {
		char c = *ascii++;
		*bin++ = c;
		if (!c)
			break;
		if (c == '\\') {
			char d;
			char e;
			u8 f;
			c = *ascii++;
			switch (c) {
			case '\\':      /* "\\" */
				continue;
			case '0':       /* "\ooo" */
			case '1':
			case '2':
			case '3':
				d = *ascii++;
				if (d < '0' || d > '7')
					break;
				e = *ascii++;
				if (e < '0' || e > '7')
					break;
				f = (u8) ((c - '0') << 6) +
					(((u8) (d - '0')) << 3) +
					(((u8) (e - '0')));
				if (f && (f <= ' ' || f >= 127)) {
					*(bin - 1) = f;
					continue; /* pattern is not \000 */
				}
			}
			return false;
		} else if (c <= ' ' || c >= 127) {
			return false;
		}
	}
	return true;
}

bool is_correct_path(const char *filename, const s8 start_type,
		     const s8 pattern_type, const s8 end_type)
{
	bool contains_pattern = false;
	unsigned char c;
	if (!filename)
		goto out;
	c = *filename;
	if (start_type == 1) { /* Must start with '/' */
		if (c != '/')
			goto out;
	} else if (start_type == -1) { /* Must not start with '/' */
		if (c == '/')
			goto out;
	}
	if (c)
		c = *(strchr(filename, '\0') - 1);
	if (end_type == 1) { /* Must end with '/' */
		if (c != '/')
			goto out;
	} else if (end_type == -1) { /* Must not end with '/' */
		if (c == '/')
			goto out;
	}
	while (true) {
		c = *filename++;
		if (!c)
			break;
		if (c == '\\') {
			unsigned char d;
			unsigned char e;
			c = *filename++;
			switch (c) {
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
			case '-':   /* "\-" */
				if (pattern_type == -1)
					break; /* Must not contain pattern */
				contains_pattern = true;
				continue;
			case '0':   /* "\ooo" */
			case '1':
			case '2':
			case '3':
				d = *filename++;
				if (d < '0' || d > '7')
					break;
				e = *filename++;
				if (e < '0' || e > '7')
					break;
				c = make_byte(c, d, e);
				if (c && (c <= ' ' || c >= 127))
					continue; /* pattern is not \000 */
			}
			goto out;
		} else if (c <= ' ' || c >= 127) {
			goto out;
		}
	}
	if (pattern_type == 1) { /* Must contain pattern */
		if (!contains_pattern)
			goto out;
	}
	return true;
out:
	return false;
}

static bool file_matches_pattern2(const char *filename,
				  const char *filename_end,
				  const char *pattern, const char *pattern_end)
{
	while (filename < filename_end && pattern < pattern_end) {
		char c;
		if (*pattern != '\\') {
			if (*filename++ != *pattern++)
				return false;
			continue;
		}
		c = *filename;
		pattern++;
		switch (*pattern) {
			int i;
			int j;
		case '?':
			if (c == '/') {
				return false;
			} else if (c == '\\') {
				if (filename[1] == '\\')
					filename++;
				else if (is_byte_range(filename + 1))
					filename += 3;
				else
					return false;
			}
			break;
		case '\\':
			if (c != '\\')
				return false;
			if (*++filename != '\\')
				return false;
			break;
		case '+':
			if (!is_decimal(c))
				return false;
			break;
		case 'x':
			if (!is_hexadecimal(c))
				return false;
			break;
		case 'a':
			if (!is_alphabet_char(c))
				return false;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
			if (c == '\\' && is_byte_range(filename + 1)
			    && !strncmp(filename + 1, pattern, 3)) {
				filename += 3;
				pattern += 2;
				break;
			}
			return false; /* Not matched. */
		case '*':
		case '@':
			for (i = 0; i <= filename_end - filename; i++) {
				if (file_matches_pattern2(filename + i,
							  filename_end,
							  pattern + 1,
							  pattern_end))
					return true;
				c = filename[i];
				if (c == '.' && *pattern == '@')
					break;
				if (c != '\\')
					continue;
				if (filename[i + 1] == '\\')
					i++;
				else if (is_byte_range(filename + i + 1))
					i += 3;
				else
					break; /* Bad pattern. */
			}
			return false; /* Not matched. */
		default:
			j = 0;
			c = *pattern;
			if (c == '$') {
				while (is_decimal(filename[j]))
					j++;
			} else if (c == 'X') {
				while (is_hexadecimal(filename[j]))
					j++;
			} else if (c == 'A') {
				while (is_alphabet_char(filename[j]))
					j++;
			}
			for (i = 1; i <= j; i++) {
				if (file_matches_pattern2(filename + i,
							  filename_end,
							  pattern + 1,
							  pattern_end))
					return true;
			}
			return false; /* Not matched or bad pattern. */
		}
		filename++;
		pattern++;
	}
	while (*pattern == '\\' &&
	       (*(pattern + 1) == '*' || *(pattern + 1) == '@'))
		pattern += 2;
	return filename == filename_end && pattern == pattern_end;
}

bool file_matches_pattern(const char *filename, const char *filename_end,
			  const char *pattern, const char *pattern_end)
{
	const char *pattern_start = pattern;
	bool first = true;
	bool result;
	while (pattern < pattern_end - 1) {
		/* Split at "\-" pattern. */
		if (*pattern++ != '\\' || *pattern++ != '-')
			continue;
		result = file_matches_pattern2(filename, filename_end,
					       pattern_start, pattern - 2);
		if (first)
			result = !result;
		if (result)
			return false;
		first = false;
		pattern_start = pattern;
	}
	result = file_matches_pattern2(filename, filename_end,
				       pattern_start, pattern_end);
	return first ? result : !result;
}

bool path_matches_pattern(const struct path_info *filename,
			  const struct path_info *pattern)
{
	/*
	if (!filename || !pattern)
		return false;
	*/
	const char *f = filename->name;
	const char *p = pattern->name;
	const int len = pattern->const_len;
	/* If @pattern doesn't contain pattern, I can use strcmp(). */
	if (!pattern->is_patterned)
		return !pathcmp(filename, pattern);
	/* Don't compare if the number of '/' differs. */
	if (filename->depth != pattern->depth)
		return false;
	/* Compare the initial length without patterns. */
	if (strncmp(f, p, len))
		return false;
	f += len;
	p += len;
	/* Main loop. Compare each directory component. */
	while (*f && *p) {
		const char *f_delimiter = strchr(f, '/');
		const char *p_delimiter = strchr(p, '/');
		if (!f_delimiter)
			f_delimiter = strchr(f, '\0');
		if (!p_delimiter)
			p_delimiter = strchr(p, '\0');
		if (!file_matches_pattern(f, f_delimiter, p, p_delimiter))
			return false;
		f = f_delimiter;
		if (*f)
			f++;
		p = p_delimiter;
		if (*p)
			p++;
	}
	/* Ignore trailing "\*" and "\@" in @pattern. */
	while (*p == '\\' &&
	       (*(p + 1) == '*' || *(p + 1) == '@'))
		p += 2;
	return !*f && !*p;
}

int string_compare(const void *a, const void *b)
{
	return strcmp(*(char **) a, *(char **) b);
}

bool pathcmp(const struct path_info *a, const struct path_info *b)
{
	return a->hash != b->hash || strcmp(a->name, b->name);
}

void fill_path_info(struct path_info *ptr)
{
	const char *name = ptr->name;
	const int len = strlen(name);
	ptr->total_len = len;
	ptr->const_len = const_part_length(name);
	ptr->is_dir = len && (name[len - 1] == '/');
	ptr->is_patterned = (ptr->const_len < len);
	ptr->hash = full_name_hash(name, len);
	ptr->depth = path_depth(name);
}

const struct path_info *savename(const char *name)
{
	static struct free_memory_block_list fmb_list = { NULL, NULL, 0 };
	/* The list of names. */
	static struct savename_entry name_list[SAVENAME_MAX_HASH];
	struct savename_entry *ptr;
	struct savename_entry *prev = NULL;
	unsigned int hash;
	struct free_memory_block_list *fmb = &fmb_list;
	int len;
	static bool first_call = true;
	if (!name)
		return NULL;
	len = strlen(name) + 1;
	if (len > CCS_MAX_PATHNAME_LEN) {
		fprintf(stderr, "ERROR: Name too long for savename().\n");
		return NULL;
	}
	hash = full_name_hash((const unsigned char *) name, len - 1);
	if (first_call) {
		int i;
		first_call = false;
		memset(&name_list, 0, sizeof(name_list));
		for (i = 0; i < SAVENAME_MAX_HASH; i++) {
			name_list[i].entry.name = "/";
			fill_path_info(&name_list[i].entry);
		}
		if (CCS_MAX_PATHNAME_LEN > PAGE_SIZE)
			abort();
	}
	ptr = &name_list[hash % SAVENAME_MAX_HASH];
	while (ptr) {
		if (hash == ptr->entry.hash && !strcmp(name, ptr->entry.name))
			goto out;
		prev = ptr;
		ptr = ptr->next;
	}
	while (len > fmb->len) {
		char *cp;
		if (fmb->next) {
			fmb = fmb->next;
			continue;
		}
		cp = malloc(PAGE_SIZE);
		if (!cp)
			out_of_memory();
		fmb->next = alloc_element(sizeof(*fmb->next));
		if (!fmb->next)
			out_of_memory();
		memset(cp, 0, PAGE_SIZE);
		fmb = fmb->next;
		fmb->ptr = cp;
		fmb->len = PAGE_SIZE;
	}
	ptr = alloc_element(sizeof(*ptr));
	if (!ptr)
		out_of_memory();
	memset(ptr, 0, sizeof(struct savename_entry));
	ptr->entry.name = fmb->ptr;
	memmove(fmb->ptr, name, len);
	fill_path_info(&ptr->entry);
	fmb->ptr += len;
	fmb->len -= len;
	prev->next = ptr; /* prev != NULL because name_list is not empty. */
	if (!fmb->len) {
		struct free_memory_block_list *ptr = &fmb_list;
		while (ptr->next != fmb)
			ptr = ptr->next;
		ptr->next = fmb->next;
	}
out:
	return ptr ? &ptr->entry : NULL;
}

char *shared_buffer = NULL;
static int buffer_lock = 0;

void get(void)
{
	if (buffer_lock)
		out_of_memory();
	if (!shared_buffer) {
		shared_buffer = malloc(shared_buffer_len);
		if (!shared_buffer)
			out_of_memory();
	}
	buffer_lock++;
}

void put(void)
{
	if (buffer_lock != 1)
		out_of_memory();
	buffer_lock--;
}

bool freadline(FILE *fp)
{
	char *cp;
	memset(shared_buffer, 0, shared_buffer_len);
	if (!fgets(shared_buffer, shared_buffer_len - 1, fp))
		return false;
	cp = strchr(shared_buffer, '\n');
	if (!cp)
		return false;
	*cp = '\0';
	normalize_line(shared_buffer);
	return true;
}

/***** UTILITY FUNCTIONS END *****/

const char *proc_policy_dir           = "/proc/ccs/",
	*disk_policy_dir              = "/etc/ccs/",
	*proc_policy_domain_policy    = "/proc/ccs/domain_policy",
	*disk_policy_domain_policy    = "/etc/ccs/domain_policy.conf",
	*base_policy_domain_policy    = "/etc/ccs/domain_policy.base",
	*proc_policy_exception_policy = "/proc/ccs/exception_policy",
	*disk_policy_exception_policy = "/etc/ccs/exception_policy.conf",
	*base_policy_exception_policy = "/etc/ccs/exception_policy.base",
	*proc_policy_system_policy    = "/proc/ccs/system_policy",
	*disk_policy_system_policy    = "/etc/ccs/system_policy.conf",
	*base_policy_system_policy    = "/etc/ccs/system_policy.base",
	*proc_policy_profile          = "/proc/ccs/profile",
	*disk_policy_profile          = "/etc/ccs/profile.conf",
	*base_policy_profile          = "/etc/ccs/profile.base",
	*proc_policy_manager          = "/proc/ccs/manager",
	*disk_policy_manager          = "/etc/ccs/manager.conf",
	*base_policy_manager          = "/etc/ccs/manager.base",
	*proc_policy_query            = "/proc/ccs/query",
	*proc_policy_grant_log        = "/proc/ccs/grant_log",
	*proc_policy_reject_log       = "/proc/ccs/reject_log",
	*proc_policy_domain_status    = "/proc/ccs/.domain_status",
	*proc_policy_process_status   = "/proc/ccs/.process_status";

int main(int argc, char *argv[])
{
	const char *argv0 = argv[0];
	if (!argv0) {
		fprintf(stderr, "Function not specified.\n");
		return 1;
	}
	if (!access("/sys/kernel/security/tomoyo/", F_OK)) {
		proc_policy_dir
			= "/sys/kernel/security/tomoyo/";
		disk_policy_dir
			= "/etc/tomoyo/";
		proc_policy_domain_policy
			= "/sys/kernel/security/tomoyo/domain_policy";
		disk_policy_domain_policy
			= "/etc/tomoyo/domain_policy.conf";
		base_policy_domain_policy
			= "/etc/tomoyo/domain_policy.base";
		proc_policy_exception_policy
			= "/sys/kernel/security/tomoyo/exception_policy";
		disk_policy_exception_policy
			= "/etc/tomoyo/exception_policy.conf";
		base_policy_exception_policy
			= "/etc/tomoyo/exception_policy.base";
		proc_policy_system_policy
			= "/sys/kernel/security/tomoyo/system_policy";
		disk_policy_system_policy
			= "/etc/tomoyo/system_policy.conf";
		base_policy_system_policy
			= "/etc/tomoyo/system_policy.base";
		proc_policy_profile
			= "/sys/kernel/security/tomoyo/profile";
		disk_policy_profile
			= "/etc/tomoyo/profile.conf";
		base_policy_profile
			= "/etc/tomoyo/profile.base";
		proc_policy_manager
			= "/sys/kernel/security/tomoyo/manager";
		disk_policy_manager
			= "/etc/tomoyo/manager.conf";
		base_policy_manager
			= "/etc/tomoyo/manager.base";
		proc_policy_query
			= "/sys/kernel/security/tomoyo/query";
		proc_policy_grant_log
			= "/sys/kernel/security/tomoyo/grant_log";
		proc_policy_reject_log
			= "/sys/kernel/security/tomoyo/reject_log";
		proc_policy_domain_status
			= "/sys/kernel/security/tomoyo/.domain_status";
		proc_policy_process_status
			= "/sys/kernel/security/tomoyo/.process_status";
	} else if (!access("/proc/tomoyo/", F_OK)) {
		proc_policy_dir
			= "/proc/tomoyo/";
		disk_policy_dir
			= "/etc/tomoyo/";
		proc_policy_domain_policy
			= "/proc/tomoyo/domain_policy";
		disk_policy_domain_policy
			= "/etc/tomoyo/domain_policy.conf";
		base_policy_domain_policy
			= "/etc/tomoyo/domain_policy.base";
		proc_policy_exception_policy
			= "/proc/tomoyo/exception_policy";
		disk_policy_exception_policy
			= "/etc/tomoyo/exception_policy.conf";
		base_policy_exception_policy
			= "/etc/tomoyo/exception_policy.base";
		proc_policy_system_policy
			= "/proc/tomoyo/system_policy";
		disk_policy_system_policy
			= "/etc/tomoyo/system_policy.conf";
		base_policy_system_policy
			= "/etc/tomoyo/system_policy.base";
		proc_policy_profile
			= "/proc/tomoyo/profile";
		disk_policy_profile
			= "/etc/tomoyo/profile.conf";
		base_policy_profile
			= "/etc/tomoyo/profile.base";
		proc_policy_manager
			= "/proc/tomoyo/manager";
		disk_policy_manager
			= "/etc/tomoyo/manager.conf";
		base_policy_manager
			= "/etc/tomoyo/manager.base";
		proc_policy_query
			= "/proc/tomoyo/query";
		proc_policy_grant_log
			= "/proc/tomoyo/grant_log";
		proc_policy_reject_log
			= "/proc/tomoyo/reject_log";
		proc_policy_domain_status
			= "/proc/tomoyo/.domain_status";
		proc_policy_process_status
			= "/proc/tomoyo/.process_status";
	}
	if (strrchr(argv0, '/'))
		argv0 = strrchr(argv0, '/') + 1;
retry:
	if (!strcmp(argv0, "sortpolicy"))
		return sortpolicy_main(argc, argv);
	if (!strcmp(argv0, "setprofile"))
		return setprofile_main(argc, argv);
	if (!strcmp(argv0, "setlevel"))
		return setlevel_main(argc, argv);
	if (!strcmp(argv0, "diffpolicy"))
		return diffpolicy_main(argc, argv);
	if (!strcmp(argv0, "savepolicy"))
		return savepolicy_main(argc, argv);
	if (!strcmp(argv0, "pathmatch"))
		return pathmatch_main(argc, argv);
	if (!strcmp(argv0, "loadpolicy"))
		return loadpolicy_main(argc, argv);
	if (!strcmp(argv0, "ld-watch"))
		return ldwatch_main(argc, argv);
	if (!strcmp(argv0, "findtemp"))
		return findtemp_main(argc, argv);
	if (!strcmp(argv0, "editpolicy") ||
	    !strcmp(argv0, "editpolicy_offline"))
		return editpolicy_main(argc, argv);
	if (!strcmp(argv0, "checkpolicy"))
		return checkpolicy_main(argc, argv);
	if (!strcmp(argv0, "ccstree"))
		return ccstree_main(argc, argv);
	if (!strcmp(argv0, "ccs-queryd"))
		return ccsqueryd_main(argc, argv);
	if (!strcmp(argv0, "ccs-auditd"))
		return ccsauditd_main(argc, argv);
	if (!strcmp(argv0, "patternize"))
		return patternize_main(argc, argv);
	if (!strncmp(argv0, "ccs-", 4)) {
		argv0 += 4;
		goto retry;
	}
	/*
	 * Unlike busybox, I don't use argv[1] if argv[0] is the name of this
	 * program because it is dangerous to allow updating policies via
	 * unchecked argv[1].
	 * You should use either "symbolic links with 'alias' directive" or
	 * "hard links".
	 */
	printf("ccstools version 1.6.6 build 2009/02/02\n");
	fprintf(stderr, "Function %s not implemented.\n", argv0);
	return 1;
}