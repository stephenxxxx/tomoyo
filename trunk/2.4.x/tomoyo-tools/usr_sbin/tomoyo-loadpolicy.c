/*
 * tomoyo-loadpolicy.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * Version: 1.8.1   2011/04/01
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */
#include "tomoyotools.h"

static _Bool tomoyo_move_file_to_proc(const char *dest)
{
	FILE *proc_fp = tomoyo_open_write(dest);
	_Bool result = true;
	if (!proc_fp) {
		fprintf(stderr, "Can't open %s for writing.\n", dest);
		return false;
	}
	tomoyo_get();
	while (true) {
		char *line = tomoyo_freadline(stdin);
		if (!line)
			break;
		if (line[0])
			if (fprintf(proc_fp, "%s\n", line) < 0)
				result = false;
	}
	tomoyo_put();
	if (!tomoyo_close_write(proc_fp))
		result = false;
	return result;
}

static _Bool tomoyo_delete_proc_policy(const char *name)
{
	FILE *fp_in;
	FILE *fp_out;
	_Bool result = false;
	if (tomoyo_network_mode) {
		fp_in = tomoyo_open_read(name);
		fp_out = tomoyo_open_write(name);
	} else {
		fp_in = fopen(name, "r");
		fp_out = fopen(name, "w");
	}
	if (!fp_in || !fp_out) {
		fprintf(stderr, "Can't open %s for reading and writing.\n",
			name);
		if (fp_in)
			fclose(fp_in);
		if (fp_out)
			fclose(fp_out);
		return false;
	}
	tomoyo_get();
	while (true) {
		char *line = tomoyo_freadline(fp_in);
		if (!line)
			break;
		if (fprintf(fp_out, "delete %s\n", line) < 0)
			result = false;
	}
	tomoyo_put();
	if (fclose(fp_in))
		result = false;
	if (!tomoyo_close_write(fp_out))
		result = false;
	return result;
}

static _Bool tomoyo_update_domain_policy(struct tomoyo_domain_policy *proc_policy,
				      struct tomoyo_domain_policy *file_policy,
				      const char *src, const char *dest)
{
	int file_index;
	int proc_index;
	FILE *proc_fp;
	_Bool result = true;
	_Bool nm = tomoyo_network_mode;
	/* Load disk policy to file_policy->list. */
	tomoyo_network_mode = false;
	tomoyo_read_domain_policy(file_policy, src);
	tomoyo_network_mode = nm;
	/* Load proc policy to proc_policy->list. */
	tomoyo_read_domain_policy(proc_policy, dest);
	proc_fp = tomoyo_open_write(dest);
	if (!proc_fp) {
		fprintf(stderr, "Can't open %s for writing.\n", dest);
		return false;
	}
	for (file_index = 0; file_index < file_policy->list_len;
	     file_index++) {
		int i;
		int j;
		const struct tomoyo_path_info *domainname
			= file_policy->list[file_index].domainname;
		const struct tomoyo_path_info **file_string_ptr
			= file_policy->list[file_index].string_ptr;
		const int file_string_count
			= file_policy->list[file_index].string_count;
		const struct tomoyo_path_info **proc_string_ptr;
		int proc_string_count;
		proc_index = tomoyo_find_domain_by_ptr(proc_policy, domainname);
		if (fprintf(proc_fp, "%s\n", domainname->name) < 0)
			result = false;
		if (proc_index == EOF)
			goto not_found;

		/* Proc policy for this domain found. */
		proc_string_ptr = proc_policy->list[proc_index].string_ptr;
		proc_string_count = proc_policy->list[proc_index].string_count;
		for (j = 0; j < proc_string_count; j++) {
			for (i = 0; i < file_string_count; i++) {
				if (file_string_ptr[i] == proc_string_ptr[j])
					break;
			}
			/* Delete this entry from proc policy if not found
			   in disk policy. */
			if (i == file_string_count)
				if (fprintf(proc_fp, "delete %s\n",
					    proc_string_ptr[j]->name) < 0)
					result = false;
		}
		tomoyo_delete_domain(proc_policy, proc_index);
not_found:
		/* Append entries defined in disk policy. */
		for (i = 0; i < file_string_count; i++)
			if (fprintf(proc_fp, "%s\n", file_string_ptr[i]->name)
			    < 0)
				result = false;
		if (file_policy->list[file_index].profile_assigned)
			if (fprintf(proc_fp, "use_profile %u\n",
				    file_policy->list[file_index].profile)
			    < 0)
				result = false;
	}
	/* Delete all domains that are not defined in disk policy. */
	for (proc_index = 0; proc_index < proc_policy->list_len;
	     proc_index++)
		if (fprintf(proc_fp, "delete %s\n",
			    proc_policy->list[proc_index].domainname->name)
		    < 0)
			result = false;
	if (!tomoyo_close_write(proc_fp))
		result = false;
	return result;
}

int main(int argc, char *argv[])
{
	struct tomoyo_domain_policy proc_policy = { NULL, 0, NULL };
	struct tomoyo_domain_policy file_policy = { NULL, 0, NULL };
	_Bool refresh_policy = false;
	_Bool result = true;
	char target = 0;
	int i;
	for (i = 1; i < argc; i++) {
		char *ptr = argv[i];
		char *cp = strchr(ptr, ':');
		if (cp) {
			*cp++ = '\0';
			tomoyo_network_ip = inet_addr(ptr);
			tomoyo_network_port = htons(atoi(cp));
			if (tomoyo_network_mode) {
				fprintf(stderr, "You cannot specify multiple "
					"%s at the same time.\n\n",
					"remote agents");
				goto usage;
			}
			tomoyo_network_mode = true;
		} else {
			if (target) {
				fprintf(stderr, "You cannot specify multiple "
					"%s at the same time.\n\n",
					"policies");
				goto usage;
			}
			if (*ptr++ != '-')
				goto usage;
			target = *ptr++;
			if (target != 'e' && target != 'd' && target != 'p' &&
			    target != 'm' && target != 's')
				goto usage;
			if (*ptr) {
				if ((target != 'e' && target != 'd') ||
				    strcmp(ptr, "f"))
					goto usage;
				refresh_policy = true;
			}
		}
	}
	if (!target) {
		fprintf(stderr, "You need to specify %s.\n\n",
			"policy to load");
		goto usage;
	}
	if (tomoyo_network_mode) {
		if (!tomoyo_check_remote_host())
			return 1;
	} else if (access(TOMOYO_PROC_POLICY_DIR, F_OK)) {
		fprintf(stderr,
			"You can't run this program for this kernel.\n");
		return 1;
	}
	switch (target) {
	case 'p':
		result = tomoyo_move_file_to_proc(TOMOYO_PROC_POLICY_PROFILE);
		break;
	case 'm':
		result = tomoyo_move_file_to_proc(TOMOYO_PROC_POLICY_MANAGER);
		break;
	case 's':
		result = tomoyo_move_file_to_proc(TOMOYO_PROC_POLICY_STAT);
		break;
	case 'e':
		if (refresh_policy)
			result = tomoyo_delete_proc_policy
				(TOMOYO_PROC_POLICY_EXCEPTION_POLICY);
		result = tomoyo_move_file_to_proc
			(TOMOYO_PROC_POLICY_EXCEPTION_POLICY);
		break;
	case 'd':
		if (!refresh_policy) {
			result = tomoyo_move_file_to_proc
				(TOMOYO_PROC_POLICY_DOMAIN_POLICY);
			break;
		}
		result = tomoyo_update_domain_policy
			(&proc_policy, &file_policy, NULL,
			 TOMOYO_PROC_POLICY_DOMAIN_POLICY);
		tomoyo_clear_domain_policy(&proc_policy);
		tomoyo_clear_domain_policy(&file_policy);
		break;
	}
	return !result;
usage:
	printf("%s {-e|-ef|-d|-df|-m|-p|-s} [remote_ip:remote_port]\n\n"
	       "-e  : Read from stdin and append to "
	       "/sys/kernel/security/tomoyo/exception_policy .\n"
	       "-ef : Read from stdin and overwrite "
	       "/sys/kernel/security/tomoyo/exception_policy .\n"
	       "-d  : Read from stdin and append to /sys/kernel/security/tomoyo/domain_policy "
	       ".\n"
	       "-df : Read from stdin and overwrite /sys/kernel/security/tomoyo/domain_policy "
	       ".\n"
	       "-m  : Read from stdin and append to /sys/kernel/security/tomoyo/manager .\n"
	       "-p  : Read from stdin and append to /sys/kernel/security/tomoyo/profile .\n"
	       "-s  : Read from stdin and append to /sys/kernel/security/tomoyo/stat .\n"
	       "remote_ip:remote_port : Write to tomoyo-editpolicy-agent "
	       "listening at remote_ip:remote_port rather than /sys/kernel/security/tomoyo/ "
	       "directory.\n", argv[0]);
	return 1;
}