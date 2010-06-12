/*
 * security/ccsecurity/policy_io.c
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 * Version: 1.7.2+   2010/06/04
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include "internal.h"

static struct ccs_profile ccs_default_profile = {
	.learning = &ccs_default_profile.preference,
	.permissive = &ccs_default_profile.preference,
	.enforcing = &ccs_default_profile.preference,
	.audit = &ccs_default_profile.preference,
#ifdef CONFIG_CCSECURITY_AUDIT
	.preference.audit_max_grant_log = CONFIG_CCSECURITY_MAX_GRANT_LOG,
	.preference.audit_max_reject_log = CONFIG_CCSECURITY_MAX_REJECT_LOG,
#endif
	.preference.audit_task_info = true,
	.preference.audit_path_info = true,
	.preference.enforcing_penalty = 0,
	.preference.enforcing_verbose = true,
	.preference.learning_max_entry = CONFIG_CCSECURITY_MAX_ACCEPT_ENTRY,
	.preference.learning_verbose = false,
	.preference.learning_exec_realpath = true,
	.preference.learning_exec_argv0 = true,
	.preference.learning_symlink_target = true,
	.preference.permissive_verbose = true
};

/* Profile version. Currently only 20090903 is defined. */
static unsigned int ccs_profile_version;

/* Profile table. Memory is allocated as needed. */
static struct ccs_profile *ccs_profile_ptr[CCS_MAX_PROFILES];

/* String table for functionality that takes 4 modes. */
static const char *ccs_mode[CCS_CONFIG_MAX_MODE] = {
	[CCS_CONFIG_DISABLED] = "disabled",
	[CCS_CONFIG_LEARNING] = "learning",
	[CCS_CONFIG_PERMISSIVE] = "permissive",
	[CCS_CONFIG_ENFORCING] = "enforcing"
};

/* String table for /proc/ccs/profile */
static const char *ccs_mac_keywords[CCS_MAX_MAC_INDEX +
				    CCS_MAX_CAPABILITY_INDEX +
				    CCS_MAX_MAC_CATEGORY_INDEX] = {
	[CCS_MAC_FILE_EXECUTE]
	= "file::execute",
	[CCS_MAC_FILE_OPEN]
	= "file::open",
	[CCS_MAC_FILE_CREATE]
	= "file::create",
	[CCS_MAC_FILE_UNLINK]
	= "file::unlink",
	[CCS_MAC_FILE_MKDIR]
	= "file::mkdir",
	[CCS_MAC_FILE_RMDIR]
	= "file::rmdir",
	[CCS_MAC_FILE_MKFIFO]
	= "file::mkfifo",
	[CCS_MAC_FILE_MKSOCK]
	= "file::mksock",
	[CCS_MAC_FILE_TRUNCATE]
	= "file::truncate",
	[CCS_MAC_FILE_SYMLINK]
	= "file::symlink",
	[CCS_MAC_FILE_REWRITE]
	= "file::rewrite",
	[CCS_MAC_FILE_MKBLOCK]
	= "file::mkblock",
	[CCS_MAC_FILE_MKCHAR]
	= "file::mkchar",
	[CCS_MAC_FILE_LINK]
	= "file::link",
	[CCS_MAC_FILE_RENAME]
	= "file::rename",
	[CCS_MAC_FILE_CHMOD]
	= "file::chmod",
	[CCS_MAC_FILE_CHOWN]
	= "file::chown",
	[CCS_MAC_FILE_CHGRP]
	= "file::chgrp",
	[CCS_MAC_FILE_IOCTL]
	= "file::ioctl",
	[CCS_MAC_FILE_CHROOT]
	= "file::chroot",
	[CCS_MAC_FILE_MOUNT]
	= "file::mount",
	[CCS_MAC_FILE_UMOUNT]
	= "file::umount",
	[CCS_MAC_FILE_PIVOT_ROOT]
	= "file::pivot_root",
	[CCS_MAC_FILE_TRANSIT]
	= "file::transit",
	[CCS_MAC_ENVIRON]
	= "misc::env",
	[CCS_MAC_NETWORK_UDP_BIND]
	= "network::inet_udp_bind",
	[CCS_MAC_NETWORK_UDP_CONNECT]
	= "network::inet_udp_connect",
	[CCS_MAC_NETWORK_TCP_BIND]
	= "network::inet_tcp_bind",
	[CCS_MAC_NETWORK_TCP_LISTEN]
	= "network::inet_tcp_listen",
	[CCS_MAC_NETWORK_TCP_CONNECT]
	= "network::inet_tcp_connect",
	[CCS_MAC_NETWORK_TCP_ACCEPT]
	= "network::inet_tcp_accept",
	[CCS_MAC_NETWORK_RAW_BIND]
	= "network::inet_raw_bind",
	[CCS_MAC_NETWORK_RAW_CONNECT]
	= "network::inet_raw_connect",
	[CCS_MAC_SIGNAL]
	= "ipc::signal",
	[CCS_MAX_MAC_INDEX + CCS_INET_STREAM_SOCKET_CREATE]
	= "capability::inet_tcp_create",
	[CCS_MAX_MAC_INDEX + CCS_INET_STREAM_SOCKET_LISTEN]
	= "capability::inet_tcp_listen",
	[CCS_MAX_MAC_INDEX + CCS_INET_STREAM_SOCKET_CONNECT]
	= "capability::inet_tcp_connect",
	[CCS_MAX_MAC_INDEX + CCS_USE_INET_DGRAM_SOCKET]
	= "capability::use_inet_udp",
	[CCS_MAX_MAC_INDEX + CCS_USE_INET_RAW_SOCKET]
	= "capability::use_inet_ip",
	[CCS_MAX_MAC_INDEX + CCS_USE_ROUTE_SOCKET]
	= "capability::use_route",
	[CCS_MAX_MAC_INDEX + CCS_USE_PACKET_SOCKET]
	= "capability::use_packet",
	[CCS_MAX_MAC_INDEX + CCS_SYS_MOUNT]
	= "capability::SYS_MOUNT",
	[CCS_MAX_MAC_INDEX + CCS_SYS_UMOUNT]
	= "capability::SYS_UMOUNT",
	[CCS_MAX_MAC_INDEX + CCS_SYS_REBOOT]
	= "capability::SYS_REBOOT",
	[CCS_MAX_MAC_INDEX + CCS_SYS_CHROOT]
	= "capability::SYS_CHROOT",
	[CCS_MAX_MAC_INDEX + CCS_SYS_KILL]
	= "capability::SYS_KILL",
	[CCS_MAX_MAC_INDEX + CCS_SYS_VHANGUP]
	= "capability::SYS_VHANGUP",
	[CCS_MAX_MAC_INDEX + CCS_SYS_SETTIME]
	= "capability::SYS_TIME",
	[CCS_MAX_MAC_INDEX + CCS_SYS_NICE]
	= "capability::SYS_NICE",
	[CCS_MAX_MAC_INDEX + CCS_SYS_SETHOSTNAME]
	= "capability::SYS_SETHOSTNAME",
	[CCS_MAX_MAC_INDEX + CCS_USE_KERNEL_MODULE]
	= "capability::use_kernel_module",
	[CCS_MAX_MAC_INDEX + CCS_CREATE_FIFO]
	= "capability::create_fifo",
	[CCS_MAX_MAC_INDEX + CCS_CREATE_BLOCK_DEV]
	= "capability::create_block_dev",
	[CCS_MAX_MAC_INDEX + CCS_CREATE_CHAR_DEV]
	= "capability::create_char_dev",
	[CCS_MAX_MAC_INDEX + CCS_CREATE_UNIX_SOCKET]
	= "capability::create_unix_socket",
	[CCS_MAX_MAC_INDEX + CCS_SYS_LINK]
	= "capability::SYS_LINK",
	[CCS_MAX_MAC_INDEX + CCS_SYS_SYMLINK]
	= "capability::SYS_SYMLINK",
	[CCS_MAX_MAC_INDEX + CCS_SYS_RENAME]
	= "capability::SYS_RENAME",
	[CCS_MAX_MAC_INDEX + CCS_SYS_UNLINK]
	= "capability::SYS_UNLINK",
	[CCS_MAX_MAC_INDEX + CCS_SYS_CHMOD]
	= "capability::SYS_CHMOD",
	[CCS_MAX_MAC_INDEX + CCS_SYS_CHOWN]
	= "capability::SYS_CHOWN",
	[CCS_MAX_MAC_INDEX + CCS_SYS_IOCTL]
	= "capability::SYS_IOCTL",
	[CCS_MAX_MAC_INDEX + CCS_SYS_KEXEC_LOAD]
	= "capability::SYS_KEXEC_LOAD",
	[CCS_MAX_MAC_INDEX + CCS_SYS_PIVOT_ROOT]
	= "capability::SYS_PIVOT_ROOT",
	[CCS_MAX_MAC_INDEX + CCS_SYS_PTRACE]
	= "capability::SYS_PTRACE",
	[CCS_MAX_MAC_INDEX + CCS_CONCEAL_MOUNT]
	= "capability::conceal_mount",
	[CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
	 + CCS_MAC_CATEGORY_FILE] = "file",
	[CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
	 + CCS_MAC_CATEGORY_NETWORK] = "network",
	[CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
	 + CCS_MAC_CATEGORY_MISC] = "misc",
	[CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
	 + CCS_MAC_CATEGORY_IPC] = "ipc",
	[CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
	 + CCS_MAC_CATEGORY_CAPABILITY] = "capability",
};

/* Permit policy management by non-root user? */
static bool ccs_manage_by_non_root;

/**
 * ccs_cap2keyword - Convert capability operation to capability name.
 *
 * @operation: The capability index.
 *
 * Returns the name of the specified capability's name.
 */
const char *ccs_cap2keyword(const u8 operation)
{
	return operation < CCS_MAX_CAPABILITY_INDEX
		? ccs_mac_keywords[CCS_MAX_MAC_INDEX + operation] + 12 : NULL;
}

/**
 * ccs_yesno - Return "yes" or "no".
 *
 * @value: Bool value.
 */
static const char *ccs_yesno(const unsigned int value)
{
	return value ? "yes" : "no";
}

/**
 * ccs_io_printf - Transactional printf() to "struct ccs_io_buffer" structure.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @fmt:  The printf()'s format string, followed by parameters.
 *
 * Returns true on success, false otherwise.
 *
 * The snprintf() will truncate, but ccs_io_printf() won't.
 */
bool ccs_io_printf(struct ccs_io_buffer *head, const char *fmt, ...)
{
	va_list args;
	int len;
	int pos = head->read_avail;
	int size = head->readbuf_size - pos;
	if (size <= 0)
		return false;
	va_start(args, fmt);
	len = vsnprintf(head->read_buf + pos, size, fmt, args);
	va_end(args);
	if (pos + len >= head->readbuf_size)
		return false;
	head->read_avail += len;
	return true;
}

/**
 * ccs_assign_profile - Create a new profile.
 *
 * @profile: Profile number to create.
 *
 * Returns pointer to "struct ccs_profile" on success, NULL otherwise.
 */
static struct ccs_profile *ccs_assign_profile(const unsigned int profile)
{
	struct ccs_profile *ptr;
	struct ccs_profile *entry;
	if (profile >= CCS_MAX_PROFILES)
		return NULL;
	ptr = ccs_profile_ptr[profile];
	if (ptr)
		return ptr;
	entry = kzalloc(sizeof(*entry), CCS_GFP_FLAGS);
	if (mutex_lock_interruptible(&ccs_policy_lock))
		goto out;
	ptr = ccs_profile_ptr[profile];
	if (!ptr && ccs_memory_ok(entry, sizeof(*entry))) {
		ptr = entry;
		ptr->audit = &ccs_default_profile.preference;
		ptr->learning = &ccs_default_profile.preference;
		ptr->permissive = &ccs_default_profile.preference;
		ptr->enforcing = &ccs_default_profile.preference;
		ptr->default_config = CCS_CONFIG_DISABLED |
			CCS_CONFIG_WANT_GRANT_LOG | CCS_CONFIG_WANT_REJECT_LOG;
		memset(ptr->config, CCS_CONFIG_USE_DEFAULT,
		       sizeof(ptr->config));
		mb(); /* Avoid out-of-order execution. */
		ccs_profile_ptr[profile] = ptr;
		entry = NULL;
	}
	mutex_unlock(&ccs_policy_lock);
 out:
	kfree(entry);
	return ptr;
}

/**
 * ccs_check_profile - Check all profiles currently assigned to domains are defined.
 */
static void ccs_check_profile(void)
{
	struct ccs_domain_info *domain;
	const int idx = ccs_read_lock();
	ccs_policy_loaded = true;
	list_for_each_entry_rcu(domain, &ccs_domain_list, list) {
		const u8 profile = domain->profile;
		if (ccs_profile_ptr[profile])
			continue;
		panic("Profile %u (used by '%s') not defined.\n",
		      profile, domain->domainname->name);
	}
	ccs_read_unlock(idx);
	if (ccs_profile_version != 20090903)
		panic("Profile version %u is not supported.\n",
		      ccs_profile_version);
	printk(KERN_INFO "CCSecurity: 1.7.2+   2010/06/04\n");
	printk(KERN_INFO "Mandatory Access Control activated.\n");
}

/**
 * ccs_profile - Find a profile.
 *
 * @profile: Profile number to find.
 *
 * Returns pointer to "struct ccs_profile".
 */
struct ccs_profile *ccs_profile(const u8 profile)
{
	struct ccs_profile *ptr = ccs_profile_ptr[profile];
	if (!ccs_policy_loaded)
		return &ccs_default_profile;
	BUG_ON(!ptr);
	return ptr;
}

static s8 ccs_find_yesno(const char *string, const char *find)
{
	const char *cp = strstr(string, find);
	if (cp) {
		cp += strlen(find);
		if (strncmp(cp, "=yes", 4))
			return 1;
		else if (strncmp(cp, "=no", 3))
			return 0;
	}
	return -1;
}

static void ccs_set_bool(bool *b, const char *string, const char *find)
{
	switch (ccs_find_yesno(string, find)) {
	case 1:
		*b = true;
		break;
	case 0:
		*b = false;
		break;
	}
}

static void ccs_set_uint(unsigned int *i, const char *string, const char *find)
{
	const char *cp = strstr(string, find);
	if (cp)
		sscanf(cp + strlen(find), "=%u", i);
}

static void ccs_set_pref(const char *cp, const char *data,
			 struct ccs_profile *profile, const bool use_default)
{
	struct ccs_preference **pref;
	bool *verbose;
	if (!strcmp(data, "audit")) {
		if (use_default) {
			pref = &profile->audit;
			goto set_default;
		}
		profile->audit = &profile->preference;
#ifdef CONFIG_CCSECURITY_AUDIT
		ccs_set_uint(&profile->preference.audit_max_grant_log, cp,
			     "max_grant_log");
		ccs_set_uint(&profile->preference.audit_max_reject_log, cp,
			     "max_reject_log");
#endif
		ccs_set_bool(&profile->preference.audit_task_info, cp,
			     "task_info");
		ccs_set_bool(&profile->preference.audit_path_info, cp, 
			     "path_info");
		return;
	}
	if (!strcmp(data, "enforcing")) {
		if (use_default) {
			pref = &profile->enforcing;
			goto set_default;
		}
		profile->enforcing = &profile->preference;
		ccs_set_uint(&profile->preference.enforcing_penalty, cp,
			     "penalty");
		verbose = &profile->preference.enforcing_verbose;
		goto set_verbose;
	}
	if (!strcmp(data, "permissive")) {
		if (use_default) {
			pref = &profile->permissive;
			goto set_default;
		}
		profile->permissive = &profile->preference;
		verbose = &profile->preference.permissive_verbose;
		goto set_verbose;
	}
	if (!strcmp(data, "learning")) {
		if (use_default) {
			pref = &profile->learning;
			goto set_default;
		}
		profile->learning = &profile->preference;
		ccs_set_uint(&profile->preference.learning_max_entry, cp,
			     "max_entry");
		ccs_set_bool(&profile->preference.learning_exec_realpath, cp,
			     "exec.realpath");
		ccs_set_bool(&profile->preference.learning_exec_argv0, cp,
			     "exec.argv0");
		ccs_set_bool(&profile->preference.learning_symlink_target, cp,
			     "symlink.target");
		verbose = &profile->preference.learning_verbose;
		goto set_verbose;
	}
	return;
 set_default:
	*pref = &ccs_default_profile.preference;
	return;
 set_verbose:
	ccs_set_bool(verbose, cp, "verbose");
}

static int ccs_set_mode(const char *cp, char *data,
			struct ccs_profile *profile, const bool use_default)
{
	u8 i;
	u8 config;
	if (!strcmp(data, "CONFIG")) {
		i = CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
			+ CCS_MAX_MAC_CATEGORY_INDEX;
		config = profile->default_config;
	} else if (ccs_str_starts(&data, "CONFIG::")) {
		config = 0;
		for (i = 0; i < CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
			     + CCS_MAX_MAC_CATEGORY_INDEX; i++) {
			if (strcmp(data, ccs_mac_keywords[i]))
				continue;
			config = profile->config[i];
			break;
		}
		if (i == CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
		    + CCS_MAX_MAC_CATEGORY_INDEX)
			return -EINVAL;
	} else {
		return -EINVAL;
	}
	if (use_default) {
		config = CCS_CONFIG_USE_DEFAULT;
	} else {
		u8 mode;
		for (mode = 0; mode < CCS_CONFIG_MAX_MODE; mode--)
			if (strstr(cp, ccs_mode[mode]))
				/*
				 * Update lower 3 bits in order to distinguish
				 * 'config' from 'CCS_CONFIG_USE_DEAFULT'.
				 */
				config = (config & ~7) | mode;
#ifdef CONFIG_CCSECURITY_AUDIT
		if (config != CCS_CONFIG_USE_DEFAULT) {
			switch (ccs_find_yesno(cp, "grant_log")) {
			case 1:
				config |= CCS_CONFIG_WANT_GRANT_LOG;
				break;
			case 0:
				config &= ~CCS_CONFIG_WANT_GRANT_LOG;
				break;
			}
			switch (ccs_find_yesno(cp, "reject_log")) {
			case 1:
				config |= CCS_CONFIG_WANT_REJECT_LOG;
				break;
			case 0:
				config &= ~CCS_CONFIG_WANT_REJECT_LOG;
				break;
			}
		}
#endif
	}
	if (i < CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
	    + CCS_MAX_MAC_CATEGORY_INDEX)
		profile->config[i] = config;
	else if (config != CCS_CONFIG_USE_DEFAULT)
		profile->default_config = config;
	return 0;
}

/**
 * ccs_write_profile - Write profile table.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_write_profile(struct ccs_io_buffer *head)
{
	char *data = head->write_buf;
	bool use_default = false;
	char *cp;
	int i;
	struct ccs_profile *profile;
	if (sscanf(data, "PROFILE_VERSION=%u", &ccs_profile_version) == 1)
		return 0;
	i = simple_strtoul(data, &cp, 10);
	if (data == cp) {
		profile = &ccs_default_profile;
	} else {
		if (*cp != '-')
			return -EINVAL;
		data = cp + 1;
		profile = ccs_assign_profile(i);
		if (!profile)
			return -EINVAL;
	}
	cp = strchr(data, '=');
	if (!cp)
		return -EINVAL;
	*cp++ = '\0';
	if (profile != &ccs_default_profile)
		use_default = strstr(cp, "use_default") != NULL;
	if (ccs_str_starts(&data, "PREFERENCE::")) {
		ccs_set_pref(cp, data, profile, use_default);
		return 0;
	}
	if (profile == &ccs_default_profile)
		return -EINVAL;
	if (!strcmp(data, "COMMENT")) {
		const struct ccs_path_info *old_comment = profile->comment;
		profile->comment = ccs_get_name(cp);
		ccs_put_name(old_comment);
		return 0;
	}
	return ccs_set_mode(cp, data, profile, use_default);
}

static bool ccs_print_preference(struct ccs_io_buffer *head, const int idx)
{
	struct ccs_preference *pref = &ccs_default_profile.preference;
	const struct ccs_profile *profile = idx >= 0 ?
		ccs_profile_ptr[idx] : NULL;
	char buffer[16] = "";
	if (profile) {
		buffer[sizeof(buffer) - 1] = '\0';
		snprintf(buffer, sizeof(buffer) - 1, "%u-", idx);
	}
	if (profile) {
		pref = profile->audit;
		if (pref == &ccs_default_profile.preference)
			pref = NULL;
	}
	if (pref && !ccs_io_printf(head, "%sPREFERENCE::%s={ "
#ifdef CONFIG_CCSECURITY_AUDIT
				   "max_grant_log=%u max_reject_log=%u "
#endif
				   "task_info=%s path_info=%s }\n", buffer,
				   "audit",
#ifdef CONFIG_CCSECURITY_AUDIT
				   pref->audit_max_grant_log,
				   pref->audit_max_reject_log,
#endif
				   ccs_yesno(pref->audit_task_info),
				   ccs_yesno(pref->audit_path_info)))
		return false;
	if (profile) {
		pref = profile->learning;
		if (pref == &ccs_default_profile.preference)
			pref = NULL;
	}
	if (pref && !ccs_io_printf(head, "%sPREFERENCE::%s={ "
				   "verbose=%s max_entry=%u exec.realpath=%s "
				   "exec.argv0=%s symlink.target=%s }\n",
				   buffer, "learning",
				   ccs_yesno(pref->learning_verbose),
				   pref->learning_max_entry,
				   ccs_yesno(pref->learning_exec_realpath),
				   ccs_yesno(pref->learning_exec_argv0),
				   ccs_yesno(pref->learning_symlink_target)))
		return false;
	if (profile) {
		pref = profile->permissive;
		if (pref == &ccs_default_profile.preference)
			pref = NULL;
	}
	if (pref && !ccs_io_printf(head, "%sPREFERENCE::%s={ verbose=%s }\n",
				   buffer, "permissive",
				   ccs_yesno(pref->permissive_verbose)))
		return false;
	if (profile) {
		pref = profile->enforcing;
		if (pref == &ccs_default_profile.preference)
			pref = NULL;
	}
	return !pref || ccs_io_printf(head, "%sPREFERENCE::%s={ verbose=%s "
				      "penalty=%u }\n", buffer, "enforcing",
				      ccs_yesno(pref->enforcing_verbose),
				      pref->enforcing_penalty);
}

/**
 * ccs_read_profile - Read profile table.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 */
static void ccs_read_profile(struct ccs_io_buffer *head)
{
	int index;
	if (head->read_eof)
		return;
	if (head->read_bit)
		goto body;
	ccs_io_printf(head, "PROFILE_VERSION=%s\n", "20090903");
	ccs_print_preference(head, -1);
	head->read_bit = 1;
 body:
	for (index = head->read_step; index < CCS_MAX_PROFILES; index++) {
		bool done;
		u8 config;
		int i;
		int pos;
		const struct ccs_profile *profile = ccs_profile_ptr[index];
		const struct ccs_path_info *comment;
		head->read_step = index;
		if (!profile)
			continue;
		pos = head->read_avail;
		comment = profile->comment;
		done = ccs_io_printf(head, "%u-COMMENT=%s\n", index,
				     comment ? comment->name : "");
		if (!done)
			goto out;
		config = profile->default_config;
#ifdef CONFIG_CCSECURITY_AUDIT
		if (!ccs_io_printf(head, "%u-%s%s={ mode=%s "
				   "grant_log=%s reject_log=%s }\n", index,
				   "CONFIG", "", ccs_mode[config & 3],
				   ccs_yesno(config &
					     CCS_CONFIG_WANT_GRANT_LOG),
				   ccs_yesno(config &
					     CCS_CONFIG_WANT_REJECT_LOG)))
			goto out;
#else
		if (!ccs_io_printf(head, "%u-%s%s={ mode=%s }\n", index,
				   "CONFIG", "", ccs_mode[config & 3]))
			goto out;
#endif
		for (i = 0; i < CCS_MAX_MAC_INDEX + CCS_MAX_CAPABILITY_INDEX
			     + CCS_MAX_MAC_CATEGORY_INDEX; i++) {
#ifdef CONFIG_CCSECURITY_AUDIT
			const char *g;
			const char *r;
#endif
			config = profile->config[i];
			if (config == CCS_CONFIG_USE_DEFAULT)
				continue;
#ifdef CONFIG_CCSECURITY_AUDIT
			g = ccs_yesno(config & CCS_CONFIG_WANT_GRANT_LOG);
			r = ccs_yesno(config & CCS_CONFIG_WANT_REJECT_LOG);
			if (!ccs_io_printf(head, "%u-%s%s={ mode=%s "
					   "grant_log=%s reject_log=%s }\n",
					   index, "CONFIG::",
					   ccs_mac_keywords[i],
					   ccs_mode[config & 3], g, r))
				goto out;
#else
			if (!ccs_io_printf(head, "%u-%s%s={ mode=%s }\n",
					   index, "CONFIG::",
					   ccs_mac_keywords[i],
					   ccs_mode[config & 3]))
				goto out;
#endif
		}
		if (!ccs_print_preference(head, index))
			goto out;
		continue;
 out:
		head->read_avail = pos;
		break;
	}
	if (index == CCS_MAX_PROFILES)
		head->read_eof = true;
}

static bool ccs_same_manager_entry(const struct ccs_acl_head *a,
				   const struct ccs_acl_head *b)
{
	return container_of(a, struct ccs_manager, head)->manager
		== container_of(b, struct ccs_manager, head)->manager;
}

/**
 * ccs_update_manager_entry - Add a manager entry.
 *
 * @manager:   The path to manager or the domainnamme.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_update_manager_entry(const char *manager, const bool is_delete)
{
	struct ccs_manager e = { };
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (ccs_domain_def(manager)) {
		if (!ccs_correct_domain(manager))
			return -EINVAL;
		e.is_domain = true;
	} else {
		if (!ccs_correct_path(manager))
			return -EINVAL;
	}
	e.manager = ccs_get_name(manager);
	if (!e.manager)
		return error;
	error = ccs_update_policy(&e.head, sizeof(e), is_delete,
				  &ccs_policy_list[CCS_ID_MANAGER],
				  ccs_same_manager_entry);
	ccs_put_name(e.manager);
	return error;
}

/**
 * ccs_write_manager - Write manager policy.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_write_manager(struct ccs_io_buffer *head)
{
	char *data = head->write_buf;
	bool is_delete = ccs_str_starts(&data, CCS_KEYWORD_DELETE);
	if (!strcmp(data, "manage_by_non_root")) {
		ccs_manage_by_non_root = !is_delete;
		return 0;
	}
	return ccs_update_manager_entry(data, is_delete);
}

/**
 * ccs_read_manager - Read manager policy.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Caller holds ccs_read_lock().
 */
static void ccs_read_manager(struct ccs_io_buffer *head)
{
	struct list_head *pos;
	if (head->read_eof)
		return;
	list_for_each_cookie(pos, head->read_var2,
			     &ccs_policy_list[CCS_ID_MANAGER]) {
		struct ccs_manager *ptr
			= list_entry(pos, typeof(*ptr), head.list);
		if (ptr->head.is_deleted)
			continue;
		if (!ccs_io_printf(head, "%s\n", ptr->manager->name))
			return;
	}
	head->read_eof = true;
}

/**
 * ccs_manager - Check whether the current process is a policy manager.
 *
 * Returns true if the current process is permitted to modify policy
 * via /proc/ccs/ interface.
 *
 * Caller holds ccs_read_lock().
 */
static bool ccs_manager(void)
{
	struct ccs_manager *ptr;
	const char *exe;
	struct task_struct *task = current;
	const struct ccs_path_info *domainname
		= ccs_current_domain()->domainname;
	bool found = false;
	if (!ccs_policy_loaded)
		return true;
	if (task->ccs_flags & CCS_TASK_IS_MANAGER)
		return true;
	if (!ccs_manage_by_non_root && (current_uid() || current_euid()))
		return false;
	exe = ccs_get_exe();
	list_for_each_entry_rcu(ptr, &ccs_policy_list[CCS_ID_MANAGER],
				head.list) {
		if (ptr->head.is_deleted)
			continue;
		if (ptr->is_domain) {
			if (ccs_pathcmp(domainname, ptr->manager))
				continue;
		} else {
			if (!exe || strcmp(exe, ptr->manager->name))
				continue;
		}
		/* Set manager flag. */
		task->ccs_flags |= CCS_TASK_IS_MANAGER;
		found = true;
		break;
	}
	if (!found) { /* Reduce error messages. */
		static pid_t ccs_last_pid;
		const pid_t pid = current->pid;
		if (ccs_last_pid != pid) {
			printk(KERN_WARNING "%s ( %s ) is not permitted to "
			       "update policies.\n", domainname->name, exe);
			ccs_last_pid = pid;
		}
	}
	kfree(exe);
	return found;
}

/**
 * ccs_find_condition_part - Find condition part from the statement.
 *
 * @data: String to parse.
 *
 * Returns pointer to the condition part if it was found in the statement,
 * NULL otherwise.
 */
static char *ccs_find_condition_part(char *data)
{
	char *cp = strstr(data, " if ");
	if (!cp)
		cp = strstr(data, " ; set ");
	if (cp)
		*cp++ = '\0';
	return cp;
}

/**
 * ccs_select_one - Parse select command.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @data: String to parse.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static bool ccs_select_one(struct ccs_io_buffer *head, const char *data)
{
	unsigned int pid;
	struct ccs_domain_info *domain = NULL;
	bool global_pid = false;
	if (!strcmp(data, "allow_execute")) {
		head->read_execute_only = true;
		return true;
	}
	if (sscanf(data, "pid=%u", &pid) == 1 ||
	    (global_pid = true, sscanf(data, "global-pid=%u", &pid) == 1)) {
		struct task_struct *p;
		ccs_tasklist_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
		if (global_pid)
			p = ccsecurity_exports.find_task_by_pid_ns(pid,
							       &init_pid_ns);
		else
			p = ccsecurity_exports.find_task_by_vpid(pid);
#else
		p = find_task_by_pid(pid);
#endif
		if (p)
			domain = ccs_task_domain(p);
		ccs_tasklist_unlock();
	} else if (!strncmp(data, "domain=", 7)) {
		if (ccs_domain_def(data + 7))
			domain = ccs_find_domain(data + 7);
	} else
		return false;
	head->write_var1 = domain;
	/* Accessing read_buf is safe because head->io_sem is held. */
	if (!head->read_buf)
		return true; /* Do nothing if open(O_WRONLY). */
	head->read_avail = 0;
	ccs_io_printf(head, "# select %s\n", data);
	head->read_single_domain = true;
	head->read_eof = !domain;
	if (domain) {
		struct ccs_domain_info *d;
		head->read_var1 = NULL;
		list_for_each_entry_rcu(d, &ccs_domain_list, list) {
			if (d == domain)
				break;
			head->read_var1 = &d->list;
		}
		head->read_var2 = NULL;
		head->read_bit = 0;
		head->read_step = 0;
		if (domain->is_deleted)
			ccs_io_printf(head, "# This is a deleted domain.\n");
	}
	return true;
}

static int ccs_write_domain2(char *data, struct ccs_domain_info *domain,
			     const bool is_delete)
{
	static const struct {
		const char *keyword;
		int (*write) (char *, struct ccs_domain_info *,
			      struct ccs_condition *, const bool);
	} ccs_callback[5] = {
		{ CCS_KEYWORD_ALLOW_NETWORK, ccs_write_network },
		{ CCS_KEYWORD_ALLOW_ENV, ccs_write_env },
		{ CCS_KEYWORD_ALLOW_CAPABILITY, ccs_write_capability },
		{ CCS_KEYWORD_ALLOW_SIGNAL, ccs_write_signal },
		{ CCS_KEYWORD_ALLOW_MOUNT, ccs_write_mount }
	};
	int (*write) (char *, struct ccs_domain_info *, struct ccs_condition *,
		      const bool) = ccs_write_file;
	int error;
	u8 i;
	struct ccs_condition *cond = NULL;
	char *cp = ccs_find_condition_part(data);
	if (cp) {
		cond = ccs_get_condition(cp);
		if (!cond)
			return -EINVAL;
	}
	for (i = 0; i < 5; i++) {
		if (!ccs_str_starts(&data, ccs_callback[i].keyword))
			continue;
		write = ccs_callback[i].write;
		break;
	}
	error = write(data, domain, cond, is_delete);
	if (cond)
		ccs_put_condition(cond);
	return error;
}

static const char *ccs_dif[CCS_MAX_DOMAIN_INFO_FLAGS] = {
	[CCS_DIF_QUOTA_WARNED] = CCS_KEYWORD_QUOTA_EXCEEDED "\n",
	[CCS_DIF_IGNORE_GLOBAL] = CCS_KEYWORD_IGNORE_GLOBAL "\n",
	[CCS_DIF_IGNORE_GLOBAL_ALLOW_READ]
	= CCS_KEYWORD_IGNORE_GLOBAL_ALLOW_READ "\n",
	[CCS_DIF_IGNORE_GLOBAL_ALLOW_ENV]
	= CCS_KEYWORD_IGNORE_GLOBAL_ALLOW_ENV "\n",
	[CCS_DIF_TRANSITION_FAILED] = CCS_KEYWORD_TRANSITION_FAILED "\n"
};
	
/**
 * ccs_write_domain - Write domain policy.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_write_domain(struct ccs_io_buffer *head)
{
	char *data = head->write_buf;
	struct ccs_domain_info *domain = head->write_var1;
	bool is_delete = false;
	bool is_select = false;
	unsigned int profile;
	if (ccs_str_starts(&data, CCS_KEYWORD_DELETE))
		is_delete = true;
	else if (ccs_str_starts(&data, CCS_KEYWORD_SELECT))
		is_select = true;
	if (is_select && ccs_select_one(head, data))
		return 0;
	/* Don't allow updating policies by non manager programs. */
	if (!ccs_manager())
		return -EPERM;
	if (ccs_domain_def(data)) {
		domain = NULL;
		if (is_delete)
			ccs_delete_domain(data);
		else if (is_select)
			domain = ccs_find_domain(data);
		else
			domain = ccs_assign_domain(data, 0);
		head->write_var1 = domain;
		return 0;
	}
	if (!domain)
		return -EINVAL;

	if (sscanf(data, CCS_KEYWORD_USE_PROFILE "%u", &profile) == 1
	    && profile < CCS_MAX_PROFILES) {
		if (!ccs_policy_loaded || ccs_profile_ptr[(u8) profile])
			domain->profile = (u8) profile;
		return 0;
	}
	for (profile = 0; profile < CCS_MAX_DOMAIN_INFO_FLAGS; profile++) {
		const char *cp = ccs_dif[profile];
		if (strncmp(data, cp, strlen(cp) - 1))
			continue;
		domain->flags[profile] = !is_delete;
		return 0;
	}
	return ccs_write_domain2(data, domain, is_delete);
}

/**
 * ccs_print_name_union - Print a ccs_name_union.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_name_union".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_name_union(struct ccs_io_buffer *head,
				 const struct ccs_name_union *ptr)
{
	int pos = head->read_avail;
	if (pos && head->read_buf[pos - 1] == ' ')
		head->read_avail--;
	if (ptr->is_group)
		return ccs_io_printf(head, " @%s",
				     ptr->group->group_name->name);
	return ccs_io_printf(head, " %s", ptr->filename->name);
}

/**
 * ccs_print_name_union_quoted - Print a ccs_name_union with double quotes.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_name_union".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_name_union_quoted(struct ccs_io_buffer *head,
					const struct ccs_name_union *ptr)
{
	if (ptr->is_group)
		return ccs_io_printf(head, "@%s",
				     ptr->group->group_name->name);
	return ccs_io_printf(head, "\"%s\"", ptr->filename->name);
}

static void ccs_print_number(char *buffer, int buffer_len,
			     const struct ccs_number_union *ptr)
{
	int i;
	unsigned long min = ptr->values[0];
	const unsigned long max = ptr->values[1];
	u8 min_type = ptr->value_type[0];
	const u8 max_type = ptr->value_type[1];
	memset(buffer, 0, buffer_len);
	buffer_len -= 2;
	for (i = 0; i < 2; i++) {
		int len;
		switch (min_type) {
		case CCS_VALUE_TYPE_HEXADECIMAL:
			snprintf(buffer, buffer_len, "0x%lX", min);
			break;
		case CCS_VALUE_TYPE_OCTAL:
			snprintf(buffer, buffer_len, "0%lo", min);
			break;
		default:
			snprintf(buffer, buffer_len, "%lu", min);
			break;
		}
		if (min == max && min_type == max_type)
			break;
		len = strlen(buffer);
		buffer[len++] = '-';
		buffer += len;
		buffer_len -= len;
		min_type = max_type;
		min = max;
	}
}

/**
 * ccs_print_number_union_common - Print a ccs_number_union.
 *
 * @head:       Pointer to "struct ccs_io_buffer".
 * @ptr:        Pointer to "struct ccs_number_union".
 * @need_space: True if a space character is needed.
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_number_union_common(struct ccs_io_buffer *head,
					  const struct ccs_number_union *ptr,
					  const bool need_space)
{
	char buffer[128];
	if (need_space && !ccs_io_printf(head, " "))
		return false;
	if (ptr->is_group)
		return ccs_io_printf(head, "@%s",
				     ptr->group->group_name->name);
	ccs_print_number(buffer, sizeof(buffer), ptr);
	return ccs_io_printf(head, "%s", buffer);
}

/**
 * ccs_print_number_union - Print a ccs_number_union.
 *
 * @head:       Pointer to "struct ccs_io_buffer".
 * @ptr:        Pointer to "struct ccs_number_union".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_number_union(struct ccs_io_buffer *head,
				   const struct ccs_number_union *ptr)
{
	return ccs_print_number_union_common(head, ptr, true);
}

/**
 * ccs_print_number_union_nospace - Print a ccs_number_union without a space character.
 *
 * @head:       Pointer to "struct ccs_io_buffer".
 * @ptr:        Pointer to "struct ccs_number_union".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_number_union_nospace(struct ccs_io_buffer *head,
					   const struct ccs_number_union *ptr)
{
	return ccs_print_number_union_common(head, ptr, false);
}

/**
 * ccs_print_condition - Print condition part.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @cond: Pointer to "struct ccs_condition". Maybe NULL.
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_condition(struct ccs_io_buffer *head,
				const struct ccs_condition *cond)
{
	const struct ccs_condition_element *condp;
	const struct ccs_number_union *numbers_p;
	const struct ccs_name_union *names_p;
	const struct ccs_argv *argv;
	const struct ccs_envp *envp;
	u16 condc;
	u16 i;
	u16 j;
	char buffer[32];
	if (!cond)
		goto no_condition;
	condc = cond->condc;
	condp = (const struct ccs_condition_element *) (cond + 1);
	numbers_p = (const struct ccs_number_union *) (condp + condc);
	names_p = (const struct ccs_name_union *)
		(numbers_p + cond->numbers_count);
	argv = (const struct ccs_argv *) (names_p + cond->names_count);
	envp = (const struct ccs_envp *) (argv + cond->argc);
	memset(buffer, 0, sizeof(buffer));
	if (condc && !ccs_io_printf(head, "%s", " if"))
		goto out;
	for (i = 0; i < condc; i++) {
		const u8 match = condp->equals;
		const u8 left = condp->left;
		const u8 right = condp->right;
		condp++;
		switch (left) {
		case CCS_ARGV_ENTRY:
			if (!ccs_io_printf(head, " exec.argv[%u]%s\"%s\"",
					   argv->index, argv->is_not ?
					   "!=" : "=", argv->value->name))
				goto out;
			argv++;
			continue;
		case CCS_ENVP_ENTRY:
			if (!ccs_io_printf(head, " exec.envp[\"%s\"]%s",
					   envp->name->name, envp->is_not ?
					   "!=" : "="))
				goto out;
			if (envp->value) {
				if (!ccs_io_printf(head, "\"%s\"",
						   envp->value->name))
					goto out;
			} else {
				if (!ccs_io_printf(head, "NULL"))
					goto out;
			}
			envp++;
			continue;
		case CCS_NUMBER_UNION:
			if (!ccs_print_number_union(head, numbers_p++))
				goto out;
			break;
		default:
			if (left >= CCS_MAX_CONDITION_KEYWORD)
				goto out;
			if (!ccs_io_printf(head, " %s",
					   ccs_condition_keyword[left]))
				goto out;
			break;
		}
		if (!ccs_io_printf(head, "%s", match ? "=" : "!="))
			goto out;
		switch (right) {
		case CCS_NAME_UNION:
			if (!ccs_print_name_union_quoted(head, names_p++))
				goto out;
			break;
		case CCS_NUMBER_UNION:
			if (!ccs_print_number_union_nospace(head, numbers_p++))
				goto out;
			break;
		default:
			if (right >= CCS_MAX_CONDITION_KEYWORD)
				goto out;
			if (!ccs_io_printf(head, "%s",
					   ccs_condition_keyword[right]))
				goto out;
			break;
		}
	}
	i = cond->post_state[3];
	if (!i)
		goto no_condition;
	if (!ccs_io_printf(head, " ; set"))
		goto out;
	for (j = 0; j < 3; j++) {
		if (!(i & (1 << j)))
			continue;
		if (!ccs_io_printf(head, " task.state[%u]=%u", j,
				   cond->post_state[j]))
			goto out;
	}
	if (i & (1 << 4)) {
		if (!ccs_io_printf(head, " audit=%s",
				   ccs_yesno(cond->post_state[4])))
			goto out;
	}
 no_condition:
	if (ccs_io_printf(head, "\n"))
		return true;
 out:
	return false;
}

/**
 * ccs_print_path_acl - Print a path ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_path_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_path_acl(struct ccs_io_buffer *head,
			       struct ccs_path_acl *ptr)
{
	int pos;
	u8 bit;
	const u16 perm = ptr->perm;
	for (bit = head->read_bit; bit < CCS_MAX_PATH_OPERATION; bit++) {
		if (!(perm & (1 << bit)))
			continue;
		if (head->read_execute_only && bit != CCS_TYPE_EXECUTE
		    && bit != CCS_TYPE_TRANSIT)
			continue;
		/* Print "read/write" instead of "read" and "write". */
		if ((bit == CCS_TYPE_READ || bit == CCS_TYPE_WRITE)
		    && (perm & (1 << CCS_TYPE_READ_WRITE)))
			continue;
		pos = head->read_avail;
		if (!ccs_io_printf(head, "allow_%s", ccs_path_keyword[bit]) ||
		    !ccs_print_name_union(head, &ptr->name) ||
		    !ccs_print_condition(head, ptr->head.cond)) {
			head->read_bit = bit;
			head->read_avail = pos;
			return false;
		}
	}
	head->read_bit = 0;
	return true;
}

/**
 * ccs_print_mkdev_acl - Print a mkdev ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_mkdev_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_mkdev_acl(struct ccs_io_buffer *head,
				struct ccs_mkdev_acl *ptr)
{
	int pos;
	u8 bit;
	const u16 perm = ptr->perm;
	for (bit = head->read_bit; bit < CCS_MAX_MKDEV_OPERATION;
	     bit++) {
		if (!(perm & (1 << bit)))
			continue;
		pos = head->read_avail;
		if (!ccs_io_printf(head, "allow_%s", ccs_mkdev_keyword[bit]) ||
		    !ccs_print_name_union(head, &ptr->name) ||
		    !ccs_print_number_union(head, &ptr->mode) ||
		    !ccs_print_number_union(head, &ptr->major) ||
		    !ccs_print_number_union(head, &ptr->minor) ||
		    !ccs_print_condition(head, ptr->head.cond)) {
			head->read_bit = bit;
			head->read_avail = pos;
			return false;
		}
	}
	head->read_bit = 0;
	return true;
}

/**
 * ccs_print_path2_acl - Print a path2 ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_path2_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_path2_acl(struct ccs_io_buffer *head,
				struct ccs_path2_acl *ptr)
{
	int pos;
	u8 bit;
	const u8 perm = ptr->perm;
	for (bit = head->read_bit; bit < CCS_MAX_PATH2_OPERATION; bit++) {
		if (!(perm & (1 << bit)))
			continue;
		pos = head->read_avail;
		if (!ccs_io_printf(head, "allow_%s", ccs_path2_keyword[bit]) ||
		    !ccs_print_name_union(head, &ptr->name1) ||
		    !ccs_print_name_union(head, &ptr->name2) ||
		    !ccs_print_condition(head, ptr->head.cond)) {
			head->read_bit = bit;
			head->read_avail = pos;
			return false;
		}
	}
	head->read_bit = 0;
	return true;
}

/**
 * ccs_print_path_number_acl - Print a path_number ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_path_number_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_path_number_acl(struct ccs_io_buffer *head,
				      struct ccs_path_number_acl *ptr)
{
	int pos;
	u8 bit;
	const u8 perm = ptr->perm;
	for (bit = head->read_bit; bit < CCS_MAX_PATH_NUMBER_OPERATION;
	     bit++) {
		if (!(perm & (1 << bit)))
			continue;
		pos = head->read_avail;
		if (!ccs_io_printf(head, "allow_%s",
				   ccs_path_number_keyword[bit]) ||
		    !ccs_print_name_union(head, &ptr->name) ||
		    !ccs_print_number_union(head, &ptr->number) ||
		    !ccs_print_condition(head, ptr->head.cond)) {
			head->read_bit = bit;
			head->read_avail = pos;
			return false;
		}
	}
	head->read_bit = 0;
	return true;
}

/**
 * ccs_print_env_acl - Print an evironment variable name's ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_env_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_env_acl(struct ccs_io_buffer *head,
			      struct ccs_env_acl *ptr)
{
	const int pos = head->read_avail;
	if (!ccs_io_printf(head, CCS_KEYWORD_ALLOW_ENV "%s", ptr->env->name) ||
	    !ccs_print_condition(head, ptr->head.cond)) {
		head->read_avail = pos;
		return false;
	}
	return true;
}

/**
 * ccs_print_capability_acl - Print a capability ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_capability_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_capability_acl(struct ccs_io_buffer *head,
				     struct ccs_capability_acl *ptr)
{
	const int pos = head->read_avail;
	if (!ccs_io_printf(head, CCS_KEYWORD_ALLOW_CAPABILITY "%s",
			   ccs_cap2keyword(ptr->operation)) ||
	    !ccs_print_condition(head, ptr->head.cond)) {
		head->read_avail = pos;
		return false;
	}
	return true;
}

/**
 * ccs_print_network_acl - Print a network ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_ip_network_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_network_acl(struct ccs_io_buffer *head,
				  struct ccs_ip_network_acl *ptr)
{
	int pos;
	u8 bit;
	const u8 perm = ptr->perm;
	char buf[128];
	for (bit = head->read_bit; bit < CCS_MAX_NETWORK_OPERATION; bit++) {
		const char *w[2] = { "", "" };
		if (!(perm & (1 << bit)))
			continue;
		pos = head->read_avail;
		switch (ptr->address_type) {
		case CCS_IP_ADDRESS_TYPE_ADDRESS_GROUP:
			w[0] = "@";
			w[1] = ptr->address.group->group_name->name;
			break;
		case CCS_IP_ADDRESS_TYPE_IPv4:
			ccs_print_ipv4(buf, sizeof(buf), ptr->address.ipv4.min,
				       ptr->address.ipv4.max);
			w[0] = buf;
			break;
		case CCS_IP_ADDRESS_TYPE_IPv6:
			ccs_print_ipv6(buf, sizeof(buf), ptr->address.ipv6.min,
				       ptr->address.ipv6.max);
			w[0] = buf;
			break;
		}
		if (!ccs_io_printf(head, CCS_KEYWORD_ALLOW_NETWORK "%s %s%s",
				   ccs_net_keyword[bit], w[0], w[1]) ||
		    !ccs_print_number_union(head, &ptr->port) ||
		    !ccs_print_condition(head, ptr->head.cond))
			goto out;
	}
	head->read_bit = 0;
	return true;
 out:
	head->read_bit = bit;
	head->read_avail = pos;
	return false;
}

/**
 * ccs_print_signal_acl - Print a signal ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct signale_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_signal_acl(struct ccs_io_buffer *head,
				 struct ccs_signal_acl *ptr)
{
	const int pos = head->read_avail;
	if (!ccs_io_printf(head, CCS_KEYWORD_ALLOW_SIGNAL "%u %s",
			   ptr->sig, ptr->domainname->name) ||
	    !ccs_print_condition(head, ptr->head.cond)) {
		head->read_avail = pos;
		return false;
	}
	return true;
}

/**
 * ccs_print_execute_handler - Print an execute handler ACL entry.
 *
 * @head:    Pointer to "struct ccs_io_buffer".
 * @keyword: Name of the keyword.
 * @ptr:     Pointer to "struct ccs_execute_handler".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_execute_handler(struct ccs_io_buffer *head,
				      const char *keyword,
				      struct ccs_execute_handler *ptr)
{
	const int pos = head->read_avail;
	if (!ccs_io_printf(head, "%s %s", keyword, ptr->handler->name) ||
	    !ccs_print_condition(head, ptr->head.cond)) {
		head->read_avail = pos;
		return false;
	}
	return true;
}

/**
 * ccs_print_mount_acl - Print a mount ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to "struct ccs_mount_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_mount_acl(struct ccs_io_buffer *head,
				struct ccs_mount_acl *ptr)
{
	const int pos = head->read_avail;
	if (!ccs_io_printf(head, CCS_KEYWORD_ALLOW_MOUNT) ||
	    !ccs_print_name_union(head, &ptr->dev_name) ||
	    !ccs_print_name_union(head, &ptr->dir_name) ||
	    !ccs_print_name_union(head, &ptr->fs_type) ||
	    !ccs_print_number_union(head, &ptr->flags) ||
	    !ccs_print_condition(head, ptr->head.cond)) {
		head->read_avail = pos;
		return false;
	}
	return true;
}

/**
 * ccs_print_entry - Print an ACL entry.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @ptr:  Pointer to an ACL entry.
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_print_entry(struct ccs_io_buffer *head,
			    struct ccs_acl_info *ptr)
{
	const u8 acl_type = ptr->type;
	if (ptr->is_deleted)
		return true;
	if (acl_type == CCS_TYPE_PATH_ACL) {
		struct ccs_path_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_path_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_EXECUTE_HANDLER ||
	    acl_type == CCS_TYPE_DENIED_EXECUTE_HANDLER) {
		struct ccs_execute_handler *acl
			= container_of(ptr, typeof(*acl), head);
		const char *keyword = acl_type == CCS_TYPE_EXECUTE_HANDLER ?
			CCS_KEYWORD_EXECUTE_HANDLER :
			CCS_KEYWORD_DENIED_EXECUTE_HANDLER;
		return ccs_print_execute_handler(head, keyword, acl);
	}
	if (head->read_execute_only)
		return true;
	if (acl_type == CCS_TYPE_MKDEV_ACL) {
		struct ccs_mkdev_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_mkdev_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_PATH2_ACL) {
		struct ccs_path2_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_path2_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_PATH_NUMBER_ACL) {
		struct ccs_path_number_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_path_number_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_ENV_ACL) {
		struct ccs_env_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_env_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_CAPABILITY_ACL) {
		struct ccs_capability_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_capability_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_IP_NETWORK_ACL) {
		struct ccs_ip_network_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_network_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_SIGNAL_ACL) {
		struct ccs_signal_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_signal_acl(head, acl);
	}
	if (acl_type == CCS_TYPE_MOUNT_ACL) {
		struct ccs_mount_acl *acl
			= container_of(ptr, typeof(*acl), head);
		return ccs_print_mount_acl(head, acl);
	}
	BUG(); /* This must not happen. */
	return false;
}

/**
 * ccs_read_domain2 - Read domain policy.
 *
 * @head:   Pointer to "struct ccs_io_buffer".
 * @domain: Pointer to "struct ccs_domain_info".
 *
 * Caller holds ccs_read_lock().
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_read_domain2(struct ccs_io_buffer *head,
			     struct ccs_domain_info *domain)
{
	struct list_head *pos;
	/* Print ACL entries in the domain. */
	list_for_each_cookie(pos, head->read_var2, &domain->acl_info_list) {
		struct ccs_acl_info *ptr
			= list_entry(pos, struct ccs_acl_info, list);
		if (!ccs_print_entry(head, ptr))
			return false;
	}
	return true;
}

/**
 * ccs_read_domain - Read domain policy.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Caller holds ccs_read_lock().
 */
static void ccs_read_domain(struct ccs_io_buffer *head)
{
	struct list_head *pos;
	if (head->read_eof)
		return;
	list_for_each_cookie(pos, head->read_var1, &ccs_domain_list) {
		struct ccs_domain_info *domain =
			list_entry(pos, struct ccs_domain_info, list);
		switch (head->read_step) {
			u8 i;
			const char *w[CCS_MAX_DOMAIN_INFO_FLAGS];
		case 0:
			if (domain->is_deleted && !head->read_single_domain)
				continue;
			/* Print domainname and flags. */
			for (i = 0; i < CCS_MAX_DOMAIN_INFO_FLAGS; i++)
				w[i] = domain->flags[i] ? ccs_dif[i] : "";
			if (!ccs_io_printf(head, "%s\n" CCS_KEYWORD_USE_PROFILE
					   "%u\n%s%s%s%s%s\n",
					   domain->domainname->name,
					   domain->profile,
					   w[CCS_DIF_QUOTA_WARNED],
					   w[CCS_DIF_IGNORE_GLOBAL],
					   w[CCS_DIF_IGNORE_GLOBAL_ALLOW_READ],
					   w[CCS_DIF_IGNORE_GLOBAL_ALLOW_ENV],
					   w[CCS_DIF_TRANSITION_FAILED]))
				return;
			head->read_step++;
			/* fall through */
		case 1:
			if (!ccs_read_domain2(head, domain))
				return;
			head->read_step++;
			/* fall through */
		case 2:
			if (!ccs_io_printf(head, "\n"))
				return;
			head->read_step = 0;
			if (head->read_single_domain)
				goto out;
		}
	}
 out:
	head->read_eof = true;
}

/**
 * ccs_write_domain_profile - Assign profile for specified domain.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0 on success, -EINVAL otherwise.
 *
 * This is equivalent to doing
 *
 *     ( echo "select " $domainname; echo "use_profile " $profile ) |
 *     /usr/sbin/ccs-loadpolicy -d
 *
 * Caller holds ccs_read_lock().
 */
static int ccs_write_domain_profile(struct ccs_io_buffer *head)
{
	char *data = head->write_buf;
	char *cp = strchr(data, ' ');
	struct ccs_domain_info *domain;
	unsigned int profile;
	if (!cp)
		return -EINVAL;
	*cp = '\0';
	profile = simple_strtoul(data, NULL, 10);
	if (profile >= CCS_MAX_PROFILES)
		return -EINVAL;
	domain = ccs_find_domain(cp + 1);
	if (domain && (!ccs_policy_loaded || ccs_profile_ptr[(u8) profile]))
		domain->profile = (u8) profile;
	return 0;
}

/**
 * ccs_read_domain_profile - Read only domainname and profile.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * This is equivalent to doing
 *
 *     grep -A 1 '^<kernel>' /proc/ccs/domain_policy |
 *     awk ' { if ( domainname == "" ) { if ( $1 == "<kernel>" )
 *     domainname = $0; } else if ( $1 == "use_profile" ) {
 *     print $2 " " domainname; domainname = ""; } } ; '
 *
 * Caller holds ccs_read_lock().
 */
static void ccs_read_domain_profile(struct ccs_io_buffer *head)
{
	struct list_head *pos;
	if (head->read_eof)
		return;
	list_for_each_cookie(pos, head->read_var1, &ccs_domain_list) {
		struct ccs_domain_info *domain;
		domain = list_entry(pos, struct ccs_domain_info, list);
		if (domain->is_deleted)
			continue;
		if (!ccs_io_printf(head, "%u %s\n", domain->profile,
				   domain->domainname->name))
			return;
	}
	head->read_eof = true;
}

/**
 * ccs_write_pid: Specify PID to obtain domainname.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0.
 */
static int ccs_write_pid(struct ccs_io_buffer *head)
{
	head->read_eof = false;
	return 0;
}

/**
 * ccs_read_pid - Read information of a process.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns the domainname which the specified PID is in or
 * process information of the specified PID on success,
 * empty string otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static void ccs_read_pid(struct ccs_io_buffer *head)
{
	char *buf = head->write_buf;
	bool task_info = false;
	bool global_pid = false;
	unsigned int pid;
	struct task_struct *p;
	struct ccs_domain_info *domain = NULL;
	u32 ccs_flags = 0;
	/* Accessing write_buf is safe because head->io_sem is held. */
	if (!buf) {
		head->read_eof = true;
		return; /* Do nothing if open(O_RDONLY). */
	}
	if (head->read_avail || head->read_eof)
		return;
	head->read_eof = true;
	if (ccs_str_starts(&buf, "info "))
		task_info = true;
	if (ccs_str_starts(&buf, "global-pid "))
		global_pid = true;
	pid = (unsigned int) simple_strtoul(buf, NULL, 10);
	ccs_tasklist_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	if (global_pid)
		p = ccsecurity_exports.find_task_by_pid_ns(pid, &init_pid_ns);
	else
		p = ccsecurity_exports.find_task_by_vpid(pid);
#else
	p = find_task_by_pid(pid);
#endif
	if (p) {
		domain = ccs_task_domain(p);
		ccs_flags = p->ccs_flags;
	}
	ccs_tasklist_unlock();
	if (!domain)
		return;
	if (!task_info)
		ccs_io_printf(head, "%u %u %s", pid, domain->profile,
			      domain->domainname->name);
	else
		ccs_io_printf(head, "%u manager=%s execute_handler=%s "
			      "state[0]=%u state[1]=%u state[2]=%u", pid,
			      ccs_yesno(ccs_flags &
					CCS_TASK_IS_MANAGER),
			      ccs_yesno(ccs_flags &
					CCS_TASK_IS_EXECUTE_HANDLER),
			      (u8) (ccs_flags >> 24),
			      (u8) (ccs_flags >> 16),
			      (u8) (ccs_flags >> 8));
}

static const char *ccs_transition_type[CCS_MAX_TRANSITION_TYPE] = {
	[CCS_TRANSITION_CONTROL_NO_INITIALIZE]
	= CCS_KEYWORD_NO_INITIALIZE_DOMAIN,
	[CCS_TRANSITION_CONTROL_INITIALIZE] = CCS_KEYWORD_INITIALIZE_DOMAIN,
	[CCS_TRANSITION_CONTROL_NO_KEEP] = CCS_KEYWORD_NO_KEEP_DOMAIN,
	[CCS_TRANSITION_CONTROL_KEEP] = CCS_KEYWORD_KEEP_DOMAIN
};

static const char *ccs_group_name[CCS_MAX_GROUP] = {
	[CCS_PATH_GROUP] = CCS_KEYWORD_PATH_GROUP,
	[CCS_NUMBER_GROUP] = CCS_KEYWORD_NUMBER_GROUP,
	[CCS_ADDRESS_GROUP] = CCS_KEYWORD_ADDRESS_GROUP
};

/**
 * ccs_write_exception - Write exception policy.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_write_exception(struct ccs_io_buffer *head)
{
	char *data = head->write_buf;
	const bool is_delete = ccs_str_starts(&data, CCS_KEYWORD_DELETE);
	u8 i;
	static const struct {
		const char *keyword;
		int (*write) (char *, const bool);
	} ccs_callback[4] = {
		{ CCS_KEYWORD_AGGREGATOR, ccs_write_aggregator },
		{ CCS_KEYWORD_FILE_PATTERN, ccs_write_pattern },
		{ CCS_KEYWORD_DENY_REWRITE, ccs_write_no_rewrite },
		{ CCS_KEYWORD_DENY_AUTOBIND, ccs_write_reserved_port }
	};
	for (i = 0; i < 4; i++) {
		if (ccs_str_starts(&data, ccs_callback[i].keyword))
			return ccs_callback[i].write(data, is_delete);
	}
	for (i = 0; i < CCS_MAX_TRANSITION_TYPE; i++) {
		if (ccs_str_starts(&data, ccs_transition_type[i]))
			return ccs_write_transition_control(data, is_delete,
							    i);
	}
	for (i = 0; i < CCS_MAX_GROUP; i++) {
		if (ccs_str_starts(&data, ccs_group_name[i]))
			return ccs_write_group(data, is_delete, i);
	}
	return ccs_write_domain2(data, &ccs_global_domain, is_delete);
}

/**
 * ccs_read_group - Read "struct ccs_path_group"/"struct ccs_number_group"/"struct ccs_address_group" list.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @idx:  Index number.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static bool ccs_read_group(struct ccs_io_buffer *head, const int idx)
{
	struct list_head *gpos;
	struct list_head *mpos;
	const char *w[3] = { "", "", "" };
	w[0] = ccs_group_name[idx];
	list_for_each_cookie(gpos, head->read_var1, &ccs_group_list[idx]) {
		struct ccs_group *group =
			list_entry(gpos, struct ccs_group, head.list);
		w[1] = group->group_name->name;
		list_for_each_cookie(mpos, head->read_var2,
				     &group->member_list) {
			char buffer[128];
			struct ccs_acl_head *ptr =
				list_entry(mpos, struct ccs_acl_head, list);
			if (ptr->is_deleted)
				continue;
			if (idx == CCS_PATH_GROUP) {
				w[2] = container_of(ptr, struct ccs_path_group,
						    head)->member_name->name;
			} else if (idx == CCS_NUMBER_GROUP) {
				w[2] = buffer;
				ccs_print_number(buffer, sizeof(buffer),
						 &container_of
						 (ptr, struct ccs_number_group,
						  head)->number);
			} else if (idx == CCS_ADDRESS_GROUP) {
				struct ccs_address_group *member =
					container_of(ptr, typeof(*member),
						     head);
				w[2] = buffer;
				if (member->is_ipv6)
					ccs_print_ipv6(buffer, sizeof(buffer),
						       member->min.ipv6,
						       member->max.ipv6);
				else
					ccs_print_ipv4(buffer, sizeof(buffer),
						       member->min.ipv4,
						       member->max.ipv4);
			}
			if (!ccs_io_printf(head, "%s%s %s\n", w[0], w[1],
					   w[2]))
				return false;
		}
	}
	return true;
}

/**
 * ccs_read_policy - Read "struct ccs_..._entry" list.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 * @idx:  Index number.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static bool ccs_read_policy(struct ccs_io_buffer *head, const int idx)
{
	struct list_head *pos;
	list_for_each_cookie(pos, head->read_var2, &ccs_policy_list[idx]) {
		const char *w[4] = { "", "", "", "" };
		char buffer[16];
		struct ccs_acl_head *acl = container_of(pos, typeof(*acl),
							list);
		if (acl->is_deleted)
			continue;
		switch (idx) {
		case CCS_ID_TRANSITION_CONTROL:
			{
				struct ccs_transition_control *ptr =
					container_of(acl, typeof(*ptr), head);
				w[0] = ccs_transition_type[ptr->type];
				w[1] = ptr->program ? ptr->program->name
					: "any";
				w[2] = " from ";
				w[3] = ptr->domainname ? ptr->domainname->name
					: "any";
			}
			break;
		case CCS_ID_AGGREGATOR:
			{
				struct ccs_aggregator *ptr =
					container_of(acl, typeof(*ptr), head);
				w[0] = CCS_KEYWORD_AGGREGATOR;
				w[1] = ptr->original_name->name;
				w[2] = " ";
				w[3] = ptr->aggregated_name->name;
			}
			break;
		case CCS_ID_PATTERN:
			{
				struct ccs_pattern *ptr =
					container_of(acl, typeof(*ptr), head);
				w[0] = CCS_KEYWORD_FILE_PATTERN;
				w[1] = ptr->pattern->name;
			}
			break;
		case CCS_ID_NO_REWRITE:
			{
				struct ccs_no_rewrite *ptr =
					container_of(acl, typeof(*ptr), head);
				w[0] = CCS_KEYWORD_DENY_REWRITE;
				w[1] = ptr->pattern->name;
			}
			break;
		case CCS_ID_RESERVEDPORT:
			{
				struct ccs_reserved *ptr =
					container_of(acl, typeof(*ptr), head);
				const u16 min_port = ptr->min_port;
				const u16 max_port = ptr->max_port;
				w[0] = CCS_KEYWORD_DENY_AUTOBIND;
				snprintf(buffer, sizeof(buffer) - 1, "%u%c%u",
					 min_port, min_port != max_port ?
					 '-' : '\0', max_port);
				buffer[sizeof(buffer) - 1] = '\0';
				w[1] = buffer;
			}
			break;
		default:
			continue;
		}
		if (!ccs_io_printf(head, "%s%s%s%s\n", w[0], w[1], w[2], w[3]))
			return false;
	}
	return true;
}

static void ccs_read_global_domain(struct ccs_io_buffer *head)
{
	if (!head->read_eof)
		head->read_eof = ccs_read_domain2(head, &ccs_global_domain);
}

/**
 * ccs_read_exception - Read exception policy.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Caller holds ccs_read_lock().
 */
static void ccs_read_exception(struct ccs_io_buffer *head)
{
	if (head->read_eof)
		return;
	while (head->read_step < CCS_MAX_POLICY &&
	       ccs_read_policy(head, head->read_step))
		head->read_step++;
	if (head->read_step < CCS_MAX_POLICY)
		return;
	while (head->read_step < CCS_MAX_POLICY + CCS_MAX_GROUP &&
	       ccs_read_group(head, head->read_step - CCS_MAX_POLICY))
		head->read_step++;
	if (head->read_step < CCS_MAX_POLICY + CCS_MAX_GROUP)
		return;
	head->read = ccs_read_global_domain;
}

/**
 * ccs_get_argv0 - Get argv[0].
 *
 * @ee: Pointer to "struct ccs_execve".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_get_argv0(struct ccs_execve *ee)
{
	struct linux_binprm *bprm = ee->bprm;
	char *arg_ptr = ee->tmp;
	int arg_len = 0;
	unsigned long pos = bprm->p;
	int offset = pos % PAGE_SIZE;
	bool done = false;
	if (!bprm->argc)
		goto out;
	while (1) {
		if (!ccs_dump_page(bprm, pos, &ee->dump))
			goto out;
		pos += PAGE_SIZE - offset;
		/* Read. */
		while (offset < PAGE_SIZE) {
			const char *kaddr = ee->dump.data;
			const unsigned char c = kaddr[offset++];
			if (c && arg_len < CCS_EXEC_TMPSIZE - 10) {
				if (c == '\\') {
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
				done = true;
				break;
			}
		}
		offset = 0;
		if (done)
			break;
	}
	return true;
 out:
	return false;
}

/* Wait queue for ccs_query_list. */
static DECLARE_WAIT_QUEUE_HEAD(ccs_query_wait);

/* Lock for manipulating ccs_query_list. */
static DEFINE_SPINLOCK(ccs_query_list_lock);

/* Structure for query. */
struct ccs_query_entry {
	struct list_head list;
	char *query;
	int query_len;
	unsigned int serial;
	int timer;
	int answer;
};

/* The list for "struct ccs_query_entry". */
static LIST_HEAD(ccs_query_list);

/* Number of "struct file" referring /proc/ccs/query interface. */
static atomic_t ccs_query_observers = ATOMIC_INIT(0);

static void ccs_addprintf(char *buffer, int len, const char *fmt, ...)
{
	va_list args;
	const int pos = strlen(buffer);
	va_start(args, fmt);
	vsnprintf(buffer + pos, len - pos - 1, fmt, args);
	va_end(args);
}
			
/**
 * ccs_supervisor - Ask for the supervisor's decision.
 *
 * @r:   Pointer to "struct ccs_request_info".
 * @fmt: The printf()'s format string, followed by parameters.
 *
 * Returns 0 if the supervisor decided to permit the access request which
 * violated the policy in enforcing mode, CCS_RETRY_REQUEST if the supervisor
 * decided to retry the access request which violated the policy in enforcing
 * mode, 0 if it is not in enforcing mode, -EPERM otherwise.
 */
int ccs_supervisor(struct ccs_request_info *r, const char *fmt, ...)
{
	va_list args;
	int error = -EPERM;
	int pos;
	int len;
	static unsigned int ccs_serial;
	struct ccs_query_entry *ccs_query_entry = NULL;
	bool quota_exceeded = false;
	char *header;
	struct ccs_domain_info * const domain = ccs_current_domain();
	va_start(args, fmt);
	len = vsnprintf((char *) &pos, sizeof(pos) - 1, fmt, args) + 80;
	va_end(args);
	if (r->mode == CCS_CONFIG_LEARNING) {
		char *buffer;
		char *realpath = NULL;
		char *argv0 = NULL;
		const char *symlink = NULL;
		const struct ccs_preference *pref;
		if (!ccs_domain_quota_ok(r))
			return 0;
		pref = ccs_profile(r->profile)->learning;
		if (r->param_type == CCS_TYPE_PATH_ACL &&
		    r->param.path.operation == CCS_TYPE_EXECUTE) {
			if (pref->learning_exec_realpath) {
				struct file *file = r->ee->bprm->file;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
				struct path path = { file->f_vfsmnt,
						     file->f_dentry };
				realpath = ccs_realpath_from_path(&path);
#else
				realpath = ccs_realpath_from_path(&file->
								  f_path);
#endif
				if (realpath)
					len += strlen(realpath);
			}
			if (pref->learning_exec_argv0) {
				if (ccs_get_argv0(r->ee)) {
					argv0 = r->ee->tmp;
					len += strlen(argv0);
				}
			}
		}
		if (r->param_type == CCS_TYPE_PATH_ACL &&
		    r->param.path.operation == CCS_TYPE_SYMLINK &&
		    pref->learning_symlink_target) {
			symlink = r->obj->symlink_target->name;
			len += strlen(symlink);
		}
		buffer = kmalloc(len, CCS_GFP_FLAGS);
		if (buffer) {
			const bool handler = (current->ccs_flags &
					      CCS_TASK_IS_EXECUTE_HANDLER)
				!= 0;
			va_start(args, fmt);
			vsnprintf(buffer, len - 1, fmt, args);
			va_end(args);
			if (handler || realpath || argv0 || symlink) {
				ccs_addprintf(buffer, len, " if");
				if (handler)
					ccs_addprintf(buffer, len, " task.type"
						      "=execute_handler");
				if (realpath)
					ccs_addprintf(buffer, len,
						      " exec.realpath=\"%s\"",
						      realpath);
				if (argv0)
					ccs_addprintf(buffer, len,
						      " exec.argv[0]=\"%s\"",
						      argv0);
				if (symlink)
					ccs_addprintf(buffer, len,
						      " symlink.target=\"%s\"",
						      symlink);
			}
			ccs_normalize_line(buffer);
			ccs_write_domain2(buffer, domain, false);
			kfree(buffer);
		}
		kfree(realpath);
		return 0;
	}
	if (r->mode != CCS_CONFIG_ENFORCING)
		return 0;
	if (!atomic_read(&ccs_query_observers)) {
		int i;
		if (current->ccs_flags & CCS_DONT_SLEEP_ON_ENFORCE_ERROR)
			return -EPERM;
		for (i = 0; i < ccs_profile(domain->profile)->enforcing->
			     enforcing_penalty; i++) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ / 10);
		}
		return -EPERM;
	}
	header = ccs_init_log(&len, r);
	if (!header)
		goto out;
	ccs_query_entry = kzalloc(sizeof(*ccs_query_entry), CCS_GFP_FLAGS);
	if (!ccs_query_entry)
		goto out;
	len = ccs_round2(len);
	ccs_query_entry->query = kzalloc(len, CCS_GFP_FLAGS);
	if (!ccs_query_entry->query)
		goto out;
	INIT_LIST_HEAD(&ccs_query_entry->list);
	spin_lock(&ccs_query_list_lock);
	if (ccs_quota_for_query && ccs_query_memory_size + len +
	    sizeof(*ccs_query_entry) >= ccs_quota_for_query) {
		quota_exceeded = true;
	} else {
		ccs_query_memory_size += len + sizeof(*ccs_query_entry);
		ccs_query_entry->serial = ccs_serial++;
	}
	spin_unlock(&ccs_query_list_lock);
	if (quota_exceeded)
		goto out;
	snprintf(ccs_query_entry->query, len - 1, "Q%u-%hu\n%s",
		 ccs_query_entry->serial, r->retry, header);
	kfree(header);
	header = NULL;
	ccs_addprintf(ccs_query_entry->query, len, fmt, args);
	ccs_query_entry->query_len = strlen(ccs_query_entry->query) + 1;
	spin_lock(&ccs_query_list_lock);
	list_add_tail(&ccs_query_entry->list, &ccs_query_list);
	spin_unlock(&ccs_query_list_lock);
	/* Give 10 seconds for supervisor's opinion. */
	for (ccs_query_entry->timer = 0;
	     atomic_read(&ccs_query_observers) && ccs_query_entry->timer < 100;
	     ccs_query_entry->timer++) {
		wake_up(&ccs_query_wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 10);
		if (ccs_query_entry->answer)
			break;
	}
	spin_lock(&ccs_query_list_lock);
	list_del(&ccs_query_entry->list);
	ccs_query_memory_size -= len + sizeof(*ccs_query_entry);
	spin_unlock(&ccs_query_list_lock);
	switch (ccs_query_entry->answer) {
	case 3: /* Asked to retry by administrator. */
		error = CCS_RETRY_REQUEST;
		r->retry++;
		break;
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
 out:
	if (ccs_query_entry)
		kfree(ccs_query_entry->query);
	kfree(ccs_query_entry);
	kfree(header);
	return error;
}

/**
 * ccs_poll_query - poll() for /proc/ccs/query.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table".
 *
 * Returns POLLIN | POLLRDNORM when ready to read, 0 otherwise.
 *
 * Waits for access requests which violated policy in enforcing mode.
 */
static int ccs_poll_query(struct file *file, poll_table *wait)
{
	struct list_head *tmp;
	bool found = false;
	u8 i;
	for (i = 0; i < 2; i++) {
		spin_lock(&ccs_query_list_lock);
		list_for_each(tmp, &ccs_query_list) {
			struct ccs_query_entry *ptr =
				list_entry(tmp, struct ccs_query_entry, list);
			if (ptr->answer)
				continue;
			found = true;
			break;
		}
		spin_unlock(&ccs_query_list_lock);
		if (found)
			return POLLIN | POLLRDNORM;
		if (i)
			break;
		poll_wait(file, &ccs_query_wait, wait);
	}
	return 0;
}

/**
 * ccs_read_query - Read access requests which violated policy in enforcing mode.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 */
static void ccs_read_query(struct ccs_io_buffer *head)
{
	struct list_head *tmp;
	int pos = 0;
	int len = 0;
	char *buf;
	if (head->read_avail)
		return;
	if (head->read_buf) {
		kfree(head->read_buf);
		head->read_buf = NULL;
		head->readbuf_size = 0;
	}
	spin_lock(&ccs_query_list_lock);
	list_for_each(tmp, &ccs_query_list) {
		struct ccs_query_entry *ptr
			= list_entry(tmp, struct ccs_query_entry, list);
		if (ptr->answer)
			continue;
		if (pos++ != head->read_step)
			continue;
		len = ptr->query_len;
		break;
	}
	spin_unlock(&ccs_query_list_lock);
	if (!len) {
		head->read_step = 0;
		return;
	}
	buf = kzalloc(len, CCS_GFP_FLAGS);
	if (!buf)
		return;
	pos = 0;
	spin_lock(&ccs_query_list_lock);
	list_for_each(tmp, &ccs_query_list) {
		struct ccs_query_entry *ptr
			= list_entry(tmp, struct ccs_query_entry, list);
		if (ptr->answer)
			continue;
		if (pos++ != head->read_step)
			continue;
		/*
		 * Some query can be skipped because ccs_query_list
		 * can change, but I don't care.
		 */
		if (len == ptr->query_len)
			memmove(buf, ptr->query, len);
		break;
	}
	spin_unlock(&ccs_query_list_lock);
	if (buf[0]) {
		head->read_avail = len;
		head->readbuf_size = head->read_avail;
		head->read_buf = buf;
		head->read_step++;
	} else {
		kfree(buf);
	}
}

/**
 * ccs_write_answer - Write the supervisor's decision.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0 on success, -EINVAL otherwise.
 */
static int ccs_write_answer(struct ccs_io_buffer *head)
{
	char *data = head->write_buf;
	struct list_head *tmp;
	unsigned int serial;
	unsigned int answer;
	spin_lock(&ccs_query_list_lock);
	list_for_each(tmp, &ccs_query_list) {
		struct ccs_query_entry *ptr
			= list_entry(tmp, struct ccs_query_entry, list);
		ptr->timer = 0;
	}
	spin_unlock(&ccs_query_list_lock);
	if (sscanf(data, "A%u=%u", &serial, &answer) != 2)
		return -EINVAL;
	spin_lock(&ccs_query_list_lock);
	list_for_each(tmp, &ccs_query_list) {
		struct ccs_query_entry *ptr
			= list_entry(tmp, struct ccs_query_entry, list);
		if (ptr->serial != serial)
			continue;
		if (!ptr->answer)
			ptr->answer = answer;
		break;
	}
	spin_unlock(&ccs_query_list_lock);
	return 0;
}

/**
 * ccs_read_version: Get version.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 */
static void ccs_read_version(struct ccs_io_buffer *head)
{
	if (head->read_eof)
		return;
	ccs_io_printf(head, "1.7.2");
	head->read_eof = true;
}

/**
 * ccs_read_self_domain - Get the current process's domainname.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 */
static void ccs_read_self_domain(struct ccs_io_buffer *head)
{
	if (head->read_eof)
		return;
	/*
	 * ccs_current_domain()->domainname != NULL because every process
	 * belongs to a domain and the domain's name cannot be NULL.
	 */
	ccs_io_printf(head, "%s", ccs_current_domain()->domainname->name);
	head->read_eof = true;
}

/**
 * ccs_open_control - open() for /proc/ccs/ interface.
 *
 * @type: Type of interface.
 * @file: Pointer to "struct file".
 *
 * Associates policy handler and returns 0 on success, -ENOMEM otherwise.
 */
int ccs_open_control(const u8 type, struct file *file)
{
	struct ccs_io_buffer *head = kzalloc(sizeof(*head), CCS_GFP_FLAGS);
	if (!head)
		return -ENOMEM;
	mutex_init(&head->io_sem);
	head->type = type;
	switch (type) {
	case CCS_DOMAINPOLICY: /* /proc/ccs/domain_policy */
		head->write = ccs_write_domain;
		head->read = ccs_read_domain;
		break;
	case CCS_EXCEPTIONPOLICY: /* /proc/ccs/exception_policy */
		head->write = ccs_write_exception;
		head->read = ccs_read_exception;
		break;
#ifdef CONFIG_CCSECURITY_AUDIT
	case CCS_GRANTLOG: /* /proc/ccs/grant_log */
	case CCS_REJECTLOG: /* /proc/ccs/reject_log */
		head->poll = ccs_poll_log;
		head->read = ccs_read_log;
		break;
#endif
	case CCS_SELFDOMAIN: /* /proc/ccs/self_domain */
		head->read = ccs_read_self_domain;
		break;
	case CCS_DOMAIN_STATUS: /* /proc/ccs/.domain_status */
		head->write = ccs_write_domain_profile;
		head->read = ccs_read_domain_profile;
		break;
	case CCS_EXECUTE_HANDLER: /* /proc/ccs/.execute_handler */
		/* Allow execute_handler to read process's status. */
		if (!(current->ccs_flags & CCS_TASK_IS_EXECUTE_HANDLER)) {
			kfree(head);
			return -EPERM;
		}
		/* fall through */
	case CCS_PROCESS_STATUS: /* /proc/ccs/.process_status */
		head->write = ccs_write_pid;
		head->read = ccs_read_pid;
		break;
	case CCS_VERSION: /* /proc/ccs/version */
		head->read = ccs_read_version;
		head->readbuf_size = 128;
		break;
	case CCS_MEMINFO: /* /proc/ccs/meminfo */
		head->write = ccs_write_memory_quota;
		head->read = ccs_read_memory_counter;
		head->readbuf_size = 512;
		break;
	case CCS_PROFILE: /* /proc/ccs/profile */
		head->write = ccs_write_profile;
		head->read = ccs_read_profile;
		break;
	case CCS_QUERY: /* /proc/ccs/query */
		head->poll = ccs_poll_query;
		head->write = ccs_write_answer;
		head->read = ccs_read_query;
		break;
	case CCS_MANAGER: /* /proc/ccs/manager */
		head->write = ccs_write_manager;
		head->read = ccs_read_manager;
		break;
	}
	if (!(file->f_mode & FMODE_READ)) {
		/*
		 * No need to allocate read_buf since it is not opened
		 * for reading.
		 */
		head->read = NULL;
		head->poll = NULL;
	} else if (!head->poll) {
		/* Don't allocate read_buf for poll() access. */
		if (!head->readbuf_size)
			head->readbuf_size = 4096;
		head->read_buf = kzalloc(head->readbuf_size, CCS_GFP_FLAGS);
		if (!head->read_buf) {
			kfree(head);
			return -ENOMEM;
		}
	}
	if (!(file->f_mode & FMODE_WRITE)) {
		/*
		 * No need to allocate write_buf since it is not opened
		 * for writing.
		 */
		head->write = NULL;
	} else if (head->write) {
		head->writebuf_size = 4096;
		head->write_buf = kzalloc(head->writebuf_size, CCS_GFP_FLAGS);
		if (!head->write_buf) {
			kfree(head->read_buf);
			kfree(head);
			return -ENOMEM;
		}
	}
	if (type != CCS_QUERY &&
	    type != CCS_GRANTLOG && type != CCS_REJECTLOG)
		head->reader_idx = ccs_lock();
	file->private_data = head;
	/*
	 * Call the handler now if the file is /proc/ccs/self_domain
	 * so that the user can use "cat < /proc/ccs/self_domain" to
	 * know the current process's domainname.
	 */
	if (type == CCS_SELFDOMAIN)
		ccs_read_control(file, NULL, 0);
	/*
	 * If the file is /proc/ccs/query , increment the observer counter.
	 * The obserber counter is used by ccs_supervisor() to see if
	 * there is some process monitoring /proc/ccs/query.
	 */
	else if (type == CCS_QUERY)
		atomic_inc(&ccs_query_observers);
	return 0;
}

/**
 * ccs_poll_control - poll() for /proc/ccs/ interface.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table".
 *
 * Waits for read readiness.
 * /proc/ccs/query is handled by /usr/sbin/ccs-queryd and
 * /proc/ccs/grant_log and /proc/ccs/reject_log are handled by
 * /usr/sbin/ccs-auditd .
 */
int ccs_poll_control(struct file *file, poll_table *wait)
{
	struct ccs_io_buffer *head = file->private_data;
	if (!head->poll)
		return -ENOSYS;
	return head->poll(file, wait);
}

/**
 * ccs_read_control - read() for /proc/ccs/ interface.
 *
 * @file:       Pointer to "struct file".
 * @buffer:     Poiner to buffer to write to.
 * @buffer_len: Size of @buffer.
 *
 * Returns bytes read on success, negative value otherwise.
 */
int ccs_read_control(struct file *file, char __user *buffer,
		     const int buffer_len)
{
	int len = 0;
	struct ccs_io_buffer *head = file->private_data;
	char *cp;
	int idx;
	if (!head->read)
		return -ENOSYS;
	if (!access_ok(VERIFY_WRITE, buffer, buffer_len))
		return -EFAULT;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	idx = ccs_read_lock();
	while (1) {
		/* Call the policy handler. */
		head->read(head);
		/* Write to buffer. */
		len = head->read_avail;
		if (len || head->poll || head->read_eof)
			break;
		len = head->readbuf_size * 2;
		cp = kzalloc(len, CCS_GFP_FLAGS);
		if (!cp) {
			len = -ENOMEM;
			goto out;
		}
		kfree(head->read_buf);
		head->read_buf = cp;
		head->readbuf_size = len;
	}
	if (len > buffer_len)
		len = buffer_len;
	if (!len)
		goto out;
	/* head->read_buf changes by some functions. */
	cp = head->read_buf;
	if (copy_to_user(buffer, cp, len)) {
		len = -EFAULT;
		goto out;
	}
	head->read_avail -= len;
	memmove(cp, cp + len, head->read_avail);
 out:
	ccs_read_unlock(idx);
	mutex_unlock(&head->io_sem);
	return len;
}

/**
 * ccs_write_control - write() for /proc/ccs/ interface.
 *
 * @file:       Pointer to "struct file".
 * @buffer:     Pointer to buffer to read from.
 * @buffer_len: Size of @buffer.
 *
 * Returns @buffer_len on success, negative value otherwise.
 */
int ccs_write_control(struct file *file, const char __user *buffer,
		      const int buffer_len)
{
	struct ccs_io_buffer *head = file->private_data;
	int error = buffer_len;
	int avail_len = buffer_len;
	char *cp0 = head->write_buf;
	int idx;
	if (!head->write)
		return -ENOSYS;
	if (!access_ok(VERIFY_READ, buffer, buffer_len))
		return -EFAULT;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	idx = ccs_read_lock();
	/* Don't allow updating policies by non manager programs. */
	if (head->write != ccs_write_pid && head->write != ccs_write_domain &&
	    !ccs_manager()) {
		ccs_read_unlock(idx);
		mutex_unlock(&head->io_sem);
		return -EPERM;
	}
	/* Read a line and dispatch it to the policy handler. */
	while (avail_len > 0) {
		char c;
		if (head->write_avail >= head->writebuf_size - 1) {
			const int len = head->writebuf_size * 2;
			char *cp = kzalloc(len, CCS_GFP_FLAGS);
			if (!cp) {
				error = -ENOMEM;
				break;
			}
			memmove(cp, cp0, head->write_avail);
			kfree(cp0);
			head->write_buf = cp;
			cp0 = cp;
			head->writebuf_size = len;
		}
		if (get_user(c, buffer)) {
			error = -EFAULT;
			break;
		}
		buffer++;
		avail_len--;
		cp0[head->write_avail++] = c;
		if (c != '\n')
			continue;
		cp0[head->write_avail - 1] = '\0';
		head->write_avail = 0;
		ccs_normalize_line(cp0);
		head->write(head);
	}
	ccs_read_unlock(idx);
	mutex_unlock(&head->io_sem);
	return error;
}

/**
 * ccs_close_control - close() for /proc/ccs/ interface.
 *
 * @file: Pointer to "struct file".
 *
 * Releases memory and returns 0.
 */
int ccs_close_control(struct file *file)
{
	struct ccs_io_buffer *head = file->private_data;
	const bool is_write = head->write_buf != NULL;
	const u8 type = head->type;
	/*
	 * If the file is /proc/ccs/query , decrement the observer counter.
	 */
	if (type == CCS_QUERY)
		atomic_dec(&ccs_query_observers);
	if (type != CCS_QUERY &&
	    type != CCS_GRANTLOG && type != CCS_REJECTLOG)
		ccs_unlock(head->reader_idx);
	/* Release memory used for policy I/O. */
	kfree(head->read_buf);
	head->read_buf = NULL;
	kfree(head->write_buf);
	head->write_buf = NULL;
	kfree(head);
	head = NULL;
	file->private_data = NULL;
	if (is_write)
		ccs_run_gc();
	return 0;
}

void __init ccs_policy_io_init(void)
{
	ccsecurity_ops.check_profile = ccs_check_profile;
}
