/*
 * checkpolicy.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2008  NTT DATA CORPORATION
 *
 * Version: 1.6.0-rc   2008/03/26
 *
 */
#include "ccstools.h"

static int strendswith(const char *name, const char *tail) {
	int len;
	if (!name || !tail) return 0;
	len = strlen(name) - strlen(tail);
	return len >= 0 && strcmp(name + len, tail) == 0;
}

static int parse_ulong(unsigned long *result, char **str) {
	const char *cp = *str;
	char *ep;
	int base = 10;
	if (*cp == '0') {
		char c = * (cp + 1);
		if (c == 'x' || c == 'X') {
			base = 16; cp += 2;
		} else if (c >= '0' && c <= '7') {
			base = 8; cp++;
		}
	}
	*result = strtoul(cp, &ep, base);
	if (cp == ep) return 0;
	*str = ep;
	return (base == 16 ? VALUE_TYPE_HEXADECIMAL : (base == 8 ? VALUE_TYPE_OCTAL : VALUE_TYPE_DECIMAL));
}

static char *FindConditionPart(char *data) {
	char *cp = strstr(data, " if "), *cp2;
	if (cp) {
		while ((cp2 = strstr(cp + 3, " if ")) != NULL) cp = cp2;
		*cp++ = '\0';
	} else if ((cp = strstr(data, " ; set ")) != NULL) {
		*cp++ = '\0';
	}
	return cp;
}

static unsigned int line = 0, errors = 0, warnings = 0;

static int CheckCondition(char *condition) {
	enum { TASK_UID, TASK_EUID, TASK_SUID, TASK_FSUID, TASK_GID, TASK_EGID, TASK_SGID, TASK_FSGID,
	       TASK_PID, TASK_PPID, PATH1_UID, PATH1_GID, PATH1_INO, PATH1_PARENT_UID, PATH1_PARENT_GID, PATH1_PARENT_INO,
	       PATH2_PARENT_UID, PATH2_PARENT_GID, PATH2_PARENT_INO, EXEC_ARGC, EXEC_ENVC, EXEC_ARGV, EXEC_ENVP, TASK_STATE_0,
	       TASK_STATE_1, TASK_STATE_2, MAX_KEYWORD };
	static struct {
		const char *keyword;
		const int keyword_len; /* strlen(keyword) */
	} condition_control_keyword[MAX_KEYWORD] = {
		[TASK_UID]         = { "task.uid",           8 },
		[TASK_EUID]        = { "task.euid",          9 },
		[TASK_SUID]        = { "task.suid",          9 },
		[TASK_FSUID]       = { "task.fsuid",        10 },
		[TASK_GID]         = { "task.gid",           8 },
		[TASK_EGID]        = { "task.egid",          9 },
		[TASK_SGID]        = { "task.sgid",          9 },
		[TASK_FSGID]       = { "task.fsgid",        10 },
		[TASK_PID]         = { "task.pid",           8 },
		[TASK_PPID]        = { "task.ppid",          9 },
		[PATH1_UID]        = { "path1.uid",          9 },
		[PATH1_GID]        = { "path1.gid",          9 },
		[PATH1_INO]        = { "path1.ino",          9 },
		[PATH1_PARENT_UID] = { "path1.parent.uid",  16 },
		[PATH1_PARENT_GID] = { "path1.parent.gid",  16 },
		[PATH1_PARENT_INO] = { "path1.parent.ino",  16 },
		[PATH2_PARENT_UID] = { "path2.parent.uid",  16 },
		[PATH2_PARENT_GID] = { "path2.parent.gid",  16 },
		[PATH2_PARENT_INO] = { "path2.parent.ino",  16 },
		[EXEC_ARGC]        = { "exec.argc",          9 },
		[EXEC_ENVC]        = { "exec.envc",          9 },
		[EXEC_ARGV]        = { "exec.argv[",        10 },
		[EXEC_ENVP]        = { "exec.envp[\"",      11 },
		[TASK_STATE_0]     = { "task.state[0]",     13 },
		[TASK_STATE_1]     = { "task.state[1]",     13 },
		[TASK_STATE_2]     = { "task.state[2]",     13 },
	};
	char *start = condition;
	u8 left, right, i;
	unsigned long left_min = 0, left_max = 0, right_min = 0, right_max = 0;
	u8 post_state[4] = { 0, 0, 0, 0 };
	if ((condition = strstr(condition, "; set ")) != NULL) {
		*condition = '\0';
		condition += 6;
		while (1) {
			while (*condition == ' ') condition++;
			if (!*condition) break;
			if (strncmp(condition, "task.state[0]=", 14) == 0) i = 0;
			else if (strncmp(condition, "task.state[1]=", 14) == 0) i = 1;
			else if (strncmp(condition, "task.state[2]=", 14) == 0) i = 2;
			else goto out;
			condition += 14;
			if (post_state[3] & (1 << i)) goto out;
			post_state[3] |= 1 << i;
			if (!parse_ulong(&right_min, &condition) || right_min > 255) goto out;
			post_state[i] = (u8) right_min;
		}
	}
	condition = start;
	if (strncmp(condition, "if ", 3) == 0) condition += 3;
	else if (*condition) goto out;
	start = condition;
	while (1) {
		while (*condition == ' ') condition++;
		if (!*condition) break;
		for (left = 0; left < MAX_KEYWORD; left++) {
			if (strncmp(condition, condition_control_keyword[left].keyword, condition_control_keyword[left].keyword_len) == 0) {
				condition += condition_control_keyword[left].keyword_len;
				break;
			}
		}
		if (left == EXEC_ARGV) {
			if (!parse_ulong(&left_min, &condition)) goto out;
			if (*condition++ != ']') goto out;
		} else if (left == EXEC_ENVP) {
			char *tmp = condition;
			while (1) {
				const char c = *condition;
				/*
				 * Since environment variable names don't contain '=',
				 * I can treat '"]=' and '"]!=' sequences as delimiters.
				 */
				if (strncmp(condition, "\"]=", 3) == 0 || strncmp(condition, "\"]!=", 4) == 0) break;
				if (!c || c == ' ') goto out;
				condition++;
			}
			*condition = '\0';
			if (!SaveName(tmp)) goto out;
			*condition = '"';
			condition += 2;
		} else if (left == MAX_KEYWORD) {
			if (!parse_ulong(&left_min, &condition)) goto out;
			if (*condition == '-') {
				condition++;
				if (!parse_ulong(&left_max, &condition) || left_min > left_max) goto out;
			}
		}
		if (strncmp(condition, "!=", 2) == 0) condition += 2;
		else if (*condition == '=') condition++;
		else goto out;
		if (left == EXEC_ENVP && strncmp(condition, "NULL", 4) == 0) {
			char c;
			condition += 4;
			c = *condition;
			if (!c || c == ' ') continue;
			goto out;
		} else if (left == EXEC_ARGV || left == EXEC_ENVP) {
			char c;
			char *tmp;
			if (*condition++ != '"') goto out;
			tmp = condition;
			while (1) {
				c = *condition++;
				if (!c || c == ' ') goto out;
				if (c != '"') continue;
				c = *condition;
				if (!c || c == ' ') break;
			}
			c = *--condition;
			*condition = '\0';
			if (!SaveName(tmp)) goto out;
			*condition = c;
			condition++;
			continue;
		}
		for (right = 0; right < MAX_KEYWORD; right++) {
			if (strncmp(condition, condition_control_keyword[right].keyword, condition_control_keyword[right].keyword_len) == 0) {
				condition += condition_control_keyword[right].keyword_len;
				break;
			}
		}
		if (right == MAX_KEYWORD) {
			if (!parse_ulong(&right_min, &condition)) goto out;
			if (*condition == '-') {
				condition++;
				if (!parse_ulong(&right_max, &condition) || right_min > right_max) goto out;
			}
		}
	}
	return 1;
 out:
	printf("%u: ERROR: '%s' is a illegal condition.\n", line, start); errors++;
	return 0;
}

static void CheckCapabilityPolicy(char *data) {
	static const char *capability_keywords[] = {
		"inet_tcp_create", "inet_tcp_listen", "inet_tcp_connect", "use_inet_udp", "use_inet_ip", "use_route", "use_packet",
		"SYS_MOUNT", "SYS_UMOUNT", "SYS_REBOOT", "SYS_CHROOT", "SYS_KILL", "SYS_VHANGUP", "SYS_TIME", "SYS_NICE", "SYS_SETHOSTNAME",
		"use_kernel_module", "create_fifo", "create_block_dev", "create_char_dev", "create_unix_socket",
		"SYS_LINK", "SYS_SYMLINK", "SYS_RENAME", "SYS_UNLINK", "SYS_CHMOD", "SYS_CHOWN", "SYS_IOCTL", "SYS_KEXEC_LOAD", "SYS_PIVOT_ROOT",
		"SYS_PTRACE", NULL
	};
	int i;
	for (i = 0; capability_keywords[i]; i++) {
		if (strcmp(data, capability_keywords[i]) == 0) return;
	}
	printf("%u: ERROR: '%s' is a bad capability name.\n", line, data); errors++;
}

static void CheckSignalPolicy(char *data) {
	int sig;
	char *cp;
	cp = strchr(data, ' ');
	if (!cp) {
		printf("%u: ERROR: Too few parameters.\n", line); errors++;
		return;
	}
	*cp++ = '\0';
	if (sscanf(data, "%d", &sig) != 1) {
		printf("%u: ERROR: '%s' is a bad signal number.\n", line, data); errors++;
	}
	if (!IsCorrectDomain(cp)) {
		printf("%u: ERROR: '%s' is a bad domainname.\n", line, cp); errors++;
	}
}

static void CheckArgv0Policy(char *data) {
	char *argv0 = strchr(data, ' ');
	if (!argv0) {
		printf("%u: ERROR: Too few parameters.\n", line); errors++;
		return;
	}
	*argv0++ = '\0';
	if (!IsCorrectPath(data, 1, 0, -1)) {
		printf("%u: ERROR: '%s' is a bad pathname.\n", line, data); errors++;
	}
	if (!IsCorrectPath(argv0, -1, 0, -1) || strchr(argv0, '/')) {
		printf("%u: ERROR: '%s' is a bad argv[0] name.\n", line, data); errors++;
	}
}

static void CheckEnvPolicy(char *data) {
	if (!IsCorrectPath(data, 0, 0, 0)) {
		printf("%u: ERROR: '%s' is a bad variable name.\n", line, data); errors++;
	}
}

static void CheckNetworkPolicy(char *data) {
	int sock_type, operation, is_ipv6;
	u16 min_address[8], max_address[8];
	unsigned int min_port, max_port;
	int count;
	char *cp1 = NULL, *cp2 = NULL;
	if ((cp1 = strchr(data, ' ')) == NULL) goto out; cp1++;
	if (strncmp(data, "TCP ", 4) == 0) sock_type = SOCK_STREAM;
	else if (strncmp(data, "UDP ", 4) == 0) sock_type = SOCK_DGRAM;
	else if (strncmp(data, "RAW ", 4) == 0) sock_type = SOCK_RAW;
	else goto out;
	if ((cp2 = strchr(cp1, ' ')) == NULL) goto out; cp2++;
	if (strncmp(cp1, "bind ", 5) == 0) {
		operation = (sock_type == SOCK_STREAM) ? NETWORK_ACL_TCP_BIND : (sock_type == SOCK_DGRAM) ? NETWORK_ACL_UDP_BIND : NETWORK_ACL_RAW_BIND;
	} else if (strncmp(cp1, "connect ", 8) == 0) {
		operation = (sock_type == SOCK_STREAM) ? NETWORK_ACL_TCP_CONNECT : (sock_type == SOCK_DGRAM) ? NETWORK_ACL_UDP_CONNECT : NETWORK_ACL_RAW_CONNECT;
	} else if (sock_type == SOCK_STREAM && strncmp(cp1, "listen ", 7) == 0) {
		operation = NETWORK_ACL_TCP_LISTEN;
	} else if (sock_type == SOCK_STREAM && strncmp(cp1, "accept ", 7) == 0) {
		operation = NETWORK_ACL_TCP_ACCEPT;
	} else {
		goto out;
	}
	if ((cp1 = strchr(cp2, ' ')) == NULL) goto out; cp1++;
	if ((count = sscanf(cp2, "%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx-%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx",
						&min_address[0], &min_address[1], &min_address[2], &min_address[3],
						&min_address[4], &min_address[5], &min_address[6], &min_address[7],
						&max_address[0], &max_address[1], &max_address[2], &max_address[3],
						&max_address[4], &max_address[5], &max_address[6], &max_address[7])) == 8 || count == 16) {
		int i;
		for (i = 0; i < 8; i++) {
			min_address[i] = htons(min_address[i]);
			max_address[i] = htons(max_address[i]);
		}
		if (count == 8) memmove(max_address, min_address, sizeof(min_address));
		is_ipv6 = 1;
	} else if ((count = sscanf(cp2, "%hu.%hu.%hu.%hu-%hu.%hu.%hu.%hu",
							   &min_address[0], &min_address[1], &min_address[2], &min_address[3],
 							   &max_address[0], &max_address[1], &max_address[2], &max_address[3])) == 4 || count == 8) {
		u32 ip = htonl((((u8) min_address[0]) << 24) + (((u8) min_address[1]) << 16) + (((u8) min_address[2]) << 8) + (u8) min_address[3]);
		* (u32 *) (void *) min_address = ip;
		if (count == 8) ip = htonl((((u8) max_address[0]) << 24) + (((u8) max_address[1]) << 16) + (((u8) max_address[2]) << 8) + (u8) max_address[3]);
		* (u32 *) (void *) max_address = ip;
		is_ipv6 = 0;
	} else if (*cp2 != '@') { // Don't reject address_group.
		goto out;
	}
	if (strchr(cp1, ' ')) goto out;
	if ((count = sscanf(cp1, "%u-%u", &min_port, &max_port)) == 1 || count == 2) {
		if (count == 1) max_port = min_port;
		if (min_port <= max_port && max_port < 65536) return;
	}
 out: ;
	printf("%u: ERROR: Bad network address.\n", line); errors++;
}

static void CheckFilePolicy(char *data) {
	static const struct {
		const char * const keyword;
		const int paths;
	} acl_type_array[] = {
		{ "read/write", 1 },
		{ "execute",    1 },
		{ "read",       1 },
		{ "write",      1 },
		{ "create",     1 },
		{ "unlink",     1 },
		{ "mkdir",      1 },
		{ "rmdir",      1 },
		{ "mkfifo",     1 },
		{ "mksock",     1 },
		{ "mkblock",    1 },
		{ "mkchar",     1 },
		{ "truncate",   1 },
		{ "symlink",    1 },
		{ "link",       2 },
		{ "rename",     2 },
		{ "rewrite",    1 },
		{ NULL, 0 }
	};
	char *filename = strchr(data, ' ');
	char *cp;
	unsigned int perm;
	if (!filename) {
		printf("%u: ERROR: Unknown command '%s'\n", line, data); errors++;
		return;
	}
	*filename++ = '\0';
	if (sscanf(data, "%u", &perm) == 1 && perm > 0 && perm <= 7) {
		if (filename[0] != '@' && strendswith(filename, "/")) { // Don't reject path_group.
			printf("%u: WARNING: Only 'mkdir' and 'rmdir' are valid for directory '%s'.\n", line, filename); warnings++;
		}
		if (!IsCorrectPath(filename, 0, 0, 0)) goto out;
		return;
	}
	if (strncmp(data, "allow_", 6) == 0) {
		int type;
		for (type = 0; acl_type_array[type].keyword; type++) {
			if (strcmp(data + 6, acl_type_array[type].keyword)) continue;
			if (acl_type_array[type].paths == 2) {
				cp = strchr(filename, ' ');
				if (!cp || !IsCorrectPath(cp + 1, 0, 0, 0)) break;
				*cp = '\0';
			}
			if (!IsCorrectPath(filename, 0, 0, 0)) break;
			return;
		}
		if (!acl_type_array[type].keyword) goto out2;
	out:
		printf("%u: ERROR: '%s' is a bad pathname.\n", line, filename); errors++;
		return;
	}
 out2:
	printf("%u: ERROR: Invalid permission '%s %s'\n", line, data, filename); errors++;
}

static void CheckMountPolicy(char *data) {
	char *cp, *cp2;
	const char *dev, *dir;
	unsigned int flags;
	cp2 = data; if ((cp = strchr(cp2, ' ')) == NULL) goto out; *cp = '\0'; dev = cp2;
	cp2 = cp + 1; if ((cp = strchr(cp2, ' ')) == NULL) goto out; *cp = '\0'; dir = cp2;
	cp2 = strchr(cp + 1, ' ');
	if (!cp2) goto out;
	if (!IsCorrectPath(dev, 0, 0, 0)) {
		printf("%u: ERROR: '%s' is a bad device name.\n", line, dir); errors++;
	}
	if (!IsCorrectPath(dir, 0, 0, 0)) {
		printf("%u: ERROR: '%s' is a bad mount point.\n", line, dir); errors++;
	}
	if (sscanf(cp2 + 1, "0x%X", &flags) != 1) {
		printf("%u: ERROR: '%s' is a bad mount option.\n", line, cp2 + 1); errors++;
	}
	return;
 out:
	printf("%u: ERROR: Too few parameters.\n", line); errors++;
}

static void CheckPivotRootPolicy(char *data) {
	char *cp;
	if ((cp = strchr(data, ' ')) == NULL) goto out;
	*cp++ = '\0';
	if (!IsCorrectPath(data, 1, 0, 1)) {
		printf("%u: ERROR: '%s' is a bad directory.\n", line, data); errors++;
	}
	if (!IsCorrectPath(cp, 1, 0, 1)) {
		printf("%u: ERROR: '%s' is a bad directory.\n", line, cp); errors++;
	}
	return;
 out:
	printf("%u: ERROR: Too few parameters.\n", line); errors++;
}

static void CheckReservedPortPolicy(char *data) {
	unsigned int from, to;
	if (strchr(data, ' ')) goto out;
	if (sscanf(data, "%u-%u", &from, &to) == 2) {
		if (from <= to && to < 65536) return;
	} else if (sscanf(data, "%u", &from) == 1) {
		if (from < 65536) return;
	} else {
		printf("%u: ERROR: Too few parameters.\n", line); errors++;
		return;
	}
 out:
	printf("%u: ERROR: '%s' is a bad port number.\n", line, data); errors++;
}

static void CheckDomainInitializerEntry(const char *domainname, const char *program) {
	if (!IsCorrectPath(program, 1, 0, -1)) {
		printf("%u: ERROR: '%s' is a bad pathname.\n", line, program); errors++;
	}
	if (domainname && !IsCorrectPath(domainname, 1, -1, -1) && !IsCorrectDomain(domainname)) {
		printf("%u: ERROR: '%s' is a bad domainname.\n", line, domainname); errors++;
	}
}

static void CheckDomainInitializerPolicy(char *data) {
	char *cp = strstr(data, " from ");
	if (cp) {
		*cp = '\0';
		CheckDomainInitializerEntry(cp + 6, data);
	} else {
		CheckDomainInitializerEntry(NULL, data);
	}
}

static void CheckDomainKeeperEntry(const char *domainname, const char *program) {
	if (!IsCorrectPath(domainname, 1, -1, -1) && !IsCorrectDomain(domainname)) {
		printf("%u: ERROR: '%s' is a bad domainname.\n", line, domainname); errors++;
	}
	if (program && !IsCorrectPath(program, 1, 0, -1)) {
		printf("%u: ERROR: '%s' is a bad pathname.\n", line, program); errors++;
	}
}

static void CheckDomainKeeperPolicy(char *data) {
	char *cp = strstr(data, " from ");
	if (cp) {
		*cp = '\0';
		CheckDomainKeeperEntry(cp + 6, data);
	} else {
		CheckDomainKeeperEntry(data, NULL);
	}
}

static void CheckGroupPolicy(char *data) {
	char *cp = strchr(data, ' ');
	if (!cp) {
		printf("%u: ERROR: Too few parameters.\n", line); errors++;
		return;
	}
	*cp++ = '\0';
	if (!IsCorrectPath(data, 0, 0, 0)) {
		printf("%u: ERROR: '%s' is a bad group name.\n", line, data); errors++;
	}
	if (!IsCorrectPath(cp, 0, 0, 0)) {
		printf("%u: ERROR: '%s' is a bad pathname.\n", line, cp); errors++;
	}
}

static void CheckAddressGroupPolicy(char *data) {
	char *cp = strchr(data, ' ');
	u16 min_address[8], max_address[8];
	int count;
	if (!cp) {
		printf("%u: ERROR: Too few parameters.\n", line); errors++;
		return;
	}
	*cp++ = '\0';
	if ((count = sscanf(cp, "%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx-%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx",
						&min_address[0], &min_address[1], &min_address[2], &min_address[3],
						&min_address[4], &min_address[5], &min_address[6], &min_address[7],
						&max_address[0], &max_address[1], &max_address[2], &max_address[3],
						&max_address[4], &max_address[5], &max_address[6], &max_address[7])) == 8 || count == 16) {
	} else if ((count = sscanf(cp, "%hu.%hu.%hu.%hu-%hu.%hu.%hu.%hu",
							   &min_address[0], &min_address[1], &min_address[2], &min_address[3],
 							   &max_address[0], &max_address[1], &max_address[2], &max_address[3])) == 4 || count == 8) {
	} else {
		printf("%u: ERROR: '%s' is a bad address.\n", line, cp); errors++;
	}
}
		
int checkpolicy_main(int argc, char *argv[]) {
	int policy_type = POLICY_TYPE_UNKNOWN;
	if (argc > 1) {
		switch (argv[1][0]) {
		case 's':
			policy_type = POLICY_TYPE_SYSTEM_POLICY;
			break;
		case 'e':
			policy_type = POLICY_TYPE_EXCEPTION_POLICY;
			break;
		case 'd':
			policy_type = POLICY_TYPE_DOMAIN_POLICY;
			break;
		}
	}
	if (policy_type == POLICY_TYPE_UNKNOWN) {
		fprintf(stderr, "%s s|e|d < policy_to_check\n", argv[0]);
		return 0;
	}
	get();
	while (memset(shared_buffer, 0, shared_buffer_len), fgets(shared_buffer, shared_buffer_len - 1, stdin)) {
		static int domain = EOF;
		int is_select = 0, is_delete = 0, is_undelete = 0;
		char *cp = strchr(shared_buffer, '\n');
		line++;
		if (!cp) {
			printf("%u: ERROR: Line too long.\n", line); errors++;
			break;
		}
		*cp = '\0';
		{
			int c;
			for (c = 1; c < 256; c++) {
				if (c == '\t' || c == '\r' || (c >= ' ' && c < 127)) continue;
				if (strchr(shared_buffer, c)) {
					printf("%u: WARNING: Line contains illegal character (\\%03o).\n", line, c); warnings++;
					break;
				}
			}
		}
		NormalizeLine(shared_buffer);
		if (!shared_buffer[0]) continue;
		switch (policy_type) {
		case POLICY_TYPE_DOMAIN_POLICY:
			if (strncmp(shared_buffer, KEYWORD_DELETE, KEYWORD_DELETE_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_DELETE_LEN);
				is_delete = 1;
			} else if (strncmp(shared_buffer, KEYWORD_SELECT, KEYWORD_SELECT_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_SELECT_LEN);
				is_select = 1;
			} else if (strncmp(shared_buffer, KEYWORD_UNDELETE, KEYWORD_UNDELETE_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_UNDELETE_LEN);
				is_undelete = 1;
			}
			if (IsDomainDef(shared_buffer)) {
				if (!IsCorrectDomain(shared_buffer) || strlen(shared_buffer) >= CCS_MAX_PATHNAME_LEN) {
					printf("%u: ERROR: '%s' is a bad domainname.\n", line, shared_buffer); errors++;
				} else {
					if (is_delete) domain = EOF;
					else domain = 0;
				}
			} else if (is_select) {
				printf("%u: ERROR: Command 'select' is valid for selecting domains only.\n", line); errors++;
			} else if (is_undelete) {
				printf("%u: ERROR: Command 'undelete' is valid for undeleting domains only.\n", line); errors++;
			} else if (domain == EOF) {
				printf("%u: WARNING: '%s' is unprocessed because domain is not selected.\n", line, shared_buffer); warnings++;
			} else if (strncmp(shared_buffer, KEYWORD_USE_PROFILE, KEYWORD_USE_PROFILE_LEN) == 0) {
				unsigned int profile;
				RemoveHeader(shared_buffer, KEYWORD_USE_PROFILE_LEN);
				if (sscanf(shared_buffer, "%u", &profile) != 1 || profile >= 256) {
					printf("%u: ERROR: '%s' is a bad profile.\n", line, shared_buffer); errors++;
				}
			} else if (strcmp(shared_buffer, "ignore_global_allow_read") == 0) {
				/* Nothing to do. */
			} else if (strcmp(shared_buffer, "ignore_global_allow_env") == 0) {
				/* Nothing to do. */
			} else if (strncmp(shared_buffer, "preferred_execute_handler ", 26) == 0) {
				RemoveHeader(shared_buffer, 26);
				if (!IsCorrectPath(shared_buffer, 1, -1, -1)) {
					printf("%u: ERROR: '%s' is a bad pathname.\n", line, shared_buffer); errors++;
				}
			} else if (strncmp(shared_buffer, "default_execute_handler ", 24) == 0) {
				RemoveHeader(shared_buffer, 24);
				if (!IsCorrectPath(shared_buffer, 1, -1, -1)) {
					printf("%u: ERROR: '%s' is a bad pathname.\n", line, shared_buffer); errors++;
				}
			} else if (strcmp(shared_buffer, "quota_exceeded") == 0) {
				/* Nothing to do. */
			} else {
				char *cp;
				if ((cp = FindConditionPart(shared_buffer)) != NULL && !CheckCondition(cp)) break;
				if (strncmp(shared_buffer, KEYWORD_ALLOW_CAPABILITY, KEYWORD_ALLOW_CAPABILITY_LEN) == 0) {
					CheckCapabilityPolicy(shared_buffer + KEYWORD_ALLOW_CAPABILITY_LEN);
				} else if (strncmp(shared_buffer, KEYWORD_ALLOW_NETWORK, KEYWORD_ALLOW_NETWORK_LEN) == 0) {
					CheckNetworkPolicy(shared_buffer + KEYWORD_ALLOW_NETWORK_LEN);
				} else if (strncmp(shared_buffer, KEYWORD_ALLOW_SIGNAL, KEYWORD_ALLOW_SIGNAL_LEN) == 0) {
					CheckSignalPolicy(shared_buffer + KEYWORD_ALLOW_SIGNAL_LEN);
				} else if (strncmp(shared_buffer, KEYWORD_ALLOW_ARGV0, KEYWORD_ALLOW_ARGV0_LEN) == 0) {
					CheckArgv0Policy(shared_buffer + KEYWORD_ALLOW_ARGV0_LEN);
				} else if (strncmp(shared_buffer, KEYWORD_ALLOW_ENV, KEYWORD_ALLOW_ENV_LEN) == 0) {
					CheckEnvPolicy(shared_buffer + KEYWORD_ALLOW_ENV_LEN);
				} else {
					CheckFilePolicy(shared_buffer);
				}
			}
			break;
		case POLICY_TYPE_EXCEPTION_POLICY:
			if (strncmp(shared_buffer, KEYWORD_DELETE, KEYWORD_DELETE_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_DELETE_LEN);
			}
			if (strncmp(shared_buffer, KEYWORD_ALLOW_READ, KEYWORD_ALLOW_READ_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_ALLOW_READ_LEN);
				if (!IsCorrectPath(shared_buffer, 1, -1, -1)) {
					printf("%u: ERROR: '%s' is a bad pathname.\n", line, shared_buffer); errors++;
				}
			} else if (strncmp(shared_buffer, KEYWORD_INITIALIZE_DOMAIN, KEYWORD_INITIALIZE_DOMAIN_LEN) == 0) {
				CheckDomainInitializerPolicy(shared_buffer + KEYWORD_INITIALIZE_DOMAIN_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_NO_INITIALIZE_DOMAIN, KEYWORD_NO_INITIALIZE_DOMAIN_LEN) == 0) {
				CheckDomainInitializerPolicy(shared_buffer + KEYWORD_NO_INITIALIZE_DOMAIN_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_KEEP_DOMAIN, KEYWORD_KEEP_DOMAIN_LEN) == 0) {
				CheckDomainKeeperPolicy(shared_buffer + KEYWORD_KEEP_DOMAIN_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_NO_KEEP_DOMAIN, KEYWORD_NO_KEEP_DOMAIN_LEN) == 0) {
				CheckDomainKeeperPolicy(shared_buffer + KEYWORD_NO_KEEP_DOMAIN_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_PATH_GROUP, KEYWORD_PATH_GROUP_LEN) == 0) {
				CheckGroupPolicy(shared_buffer + KEYWORD_PATH_GROUP_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_ADDRESS_GROUP, KEYWORD_ADDRESS_GROUP_LEN) == 0) {
				CheckAddressGroupPolicy(shared_buffer + KEYWORD_ADDRESS_GROUP_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_ALIAS, KEYWORD_ALIAS_LEN) == 0) {
				char *cp;
				RemoveHeader(shared_buffer, KEYWORD_ALIAS_LEN);
				if ((cp = strchr(shared_buffer, ' ')) == NULL) {
					printf("%u: ERROR: Too few parameters.\n", line); errors++;
				} else {
					*cp++ = '\0';
					if (!IsCorrectPath(shared_buffer, 1, -1, -1)) {
						printf("%u: ERROR: '%s' is a bad pathname.\n", line, shared_buffer); errors++;
					}
					if (!IsCorrectPath(cp, 1, -1, -1)) {
						printf("%u: ERROR: '%s' is a bad pathname.\n", line, cp); errors++;
					}
				}
			} else if (strncmp(shared_buffer, KEYWORD_AGGREGATOR, KEYWORD_AGGREGATOR_LEN) == 0) {
				char *cp;
				RemoveHeader(shared_buffer, KEYWORD_AGGREGATOR_LEN);
				if ((cp = strchr(shared_buffer, ' ')) == NULL) {
					printf("%u: ERROR: Too few parameters.\n", line); errors++;
				} else {
					*cp++ = '\0';
					if (!IsCorrectPath(shared_buffer, 1, 0, -1)) {
						printf("%u: ERROR: '%s' is a bad pattern.\n", line, shared_buffer); errors++;
					}
					if (!IsCorrectPath(cp, 1, -1, -1)) {
						printf("%u: ERROR: '%s' is a bad pathname.\n", line, cp); errors++;
					}
				}
			} else if (strncmp(shared_buffer, KEYWORD_FILE_PATTERN, KEYWORD_FILE_PATTERN_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_FILE_PATTERN_LEN);
				if (!IsCorrectPath(shared_buffer, 0, 1, 0)) {
					printf("%u: ERROR: '%s' is a bad pattern.\n", line, shared_buffer); errors++;
				}
			} else if (strncmp(shared_buffer, KEYWORD_DENY_REWRITE, KEYWORD_DENY_REWRITE_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_DENY_REWRITE_LEN);
				if (!IsCorrectPath(shared_buffer, 0, 0, 0)) {
					printf("%u: ERROR: '%s' is a bad pattern.\n", line, shared_buffer); errors++;
				}
			} else if (strncmp(shared_buffer, KEYWORD_ALLOW_ENV, KEYWORD_ALLOW_ENV_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_ALLOW_ENV_LEN);
				if (!IsCorrectPath(shared_buffer, 0, 0, 0)) {
					printf("%u: ERROR: '%s' is a bad variable name.\n", line, shared_buffer); errors++;
				}
			} else {
				printf("%u: ERROR: Unknown command '%s'.\n", line, shared_buffer); errors++;
			}
			break;
		case POLICY_TYPE_SYSTEM_POLICY:
			if (strncmp(shared_buffer, KEYWORD_DELETE, KEYWORD_DELETE_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_DELETE_LEN);
			}
			if (strncmp(shared_buffer, KEYWORD_ALLOW_MOUNT, KEYWORD_ALLOW_MOUNT_LEN) == 0) {
				CheckMountPolicy(shared_buffer + KEYWORD_ALLOW_MOUNT_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_DENY_UNMOUNT, KEYWORD_DENY_UNMOUNT_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_DENY_UNMOUNT_LEN);
				if (!IsCorrectPath(shared_buffer, 1, 0, 1)) {
					printf("%u: ERROR: '%s' is a bad pattern.\n", line, shared_buffer); errors++;
				}
			} else if (strncmp(shared_buffer, KEYWORD_ALLOW_CHROOT, KEYWORD_ALLOW_CHROOT_LEN) == 0) {
				RemoveHeader(shared_buffer, KEYWORD_ALLOW_CHROOT_LEN);
				if (!IsCorrectPath(shared_buffer, 1, 0, 1)) {
					printf("%u: ERROR: '%s' is a bad pattern.\n", line, shared_buffer); errors++;
				}
			} else if (strncmp(shared_buffer, KEYWORD_ALLOW_PIVOT_ROOT, KEYWORD_ALLOW_PIVOT_ROOT_LEN) == 0) {
				CheckPivotRootPolicy(shared_buffer + KEYWORD_ALLOW_PIVOT_ROOT_LEN);
			} else if (strncmp(shared_buffer, KEYWORD_DENY_AUTOBIND, KEYWORD_DENY_AUTOBIND_LEN) == 0) {
				CheckReservedPortPolicy(shared_buffer + KEYWORD_DENY_AUTOBIND_LEN);
			} else {
				printf("%u: ERROR: Unknown command '%s'.\n", line, shared_buffer); errors++;
			}
			break;
		}
	}
	put();
	printf("Total:   %u Line%s   %u Error%s   %u Warning%s\n", line, line > 1 ? "s" : "", errors, errors > 1 ? "s" : "", warnings, warnings > 1 ? "s" : "");
	return (errors ? 2 : (warnings ? 1 : 0));
}
