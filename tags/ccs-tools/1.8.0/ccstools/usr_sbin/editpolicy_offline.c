/*
 * editpolicy_offline.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 * Version: 1.8.0   2010/11/11
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
#include "ccstools.h"
#include "editpolicy.h"

static void ccs_handle_misc_policy(struct ccs_misc_policy *mp, FILE *fp,
				   _Bool is_write)
{
	int i;
	if (!is_write)
		goto read_policy;
	while (true) {
		char *line = ccs_freadline(fp);
		const struct ccs_path_info *cp;
		_Bool is_delete;
		if (!line)
			break;
		if (!line[0])
			continue;
		is_delete = ccs_str_starts(line, "delete ");
		cp = ccs_savename(line);
		if (!cp)
			ccs_out_of_memory();
		if (!is_delete)
			goto append_policy;
		for (i = 0; i < mp->list_len; i++)
			/* Faster comparison, for they are ccs_savename'd. */
			if (mp->list[i] == cp)
				break;
		if (i < mp->list_len)
			for (mp->list_len--; i < mp->list_len; i++)
				mp->list[i] = mp->list[i + 1];
		continue;
append_policy:
		for (i = 0; i < mp->list_len; i++)
			/* Faster comparison, for they are ccs_savename'd. */
			if (mp->list[i] == cp)
				break;
		if (i < mp->list_len)
			continue;
		mp->list = realloc(mp->list, (mp->list_len + 1)
				   * sizeof(const struct ccs_path_info *));
		if (!mp->list)
			ccs_out_of_memory();
		mp->list[mp->list_len++] = cp;
	}
	return;
read_policy:
	for (i = 0; i < mp->list_len; i++)
		fprintf(fp, "%s\n", mp->list[i]->name);
}

/* Variables */

int ccs_persistent_fd = EOF;

/* Main functions */

void ccs_send_fd(char *data, int *fd)
{
	struct msghdr msg;
	struct iovec iov = { data, strlen(data) };
	char cmsg_buf[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *cmsg = (struct cmsghdr *) cmsg_buf;
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	msg.msg_controllen = cmsg->cmsg_len;
	memmove(CMSG_DATA(cmsg), fd, sizeof(int));
	sendmsg(ccs_persistent_fd, &msg, 0);
	close(*fd);
}

void ccs_editpolicy_offline_daemon(void)
{
	struct ccs_misc_policy mp[3];
	struct ccs_domain_policy dp;
	static const int buffer_len = 8192;
	char *buffer = malloc(buffer_len);
	if (!buffer)
		ccs_out_of_memory();
	memset(&dp, 0, sizeof(dp));
	memset(&mp, 0, sizeof(mp));
	ccs_get();
	ccs_assign_domain(&dp, CCS_ROOT_NAME, false, false);
	while (true) {
		FILE *fp;
		struct msghdr msg;
		struct iovec iov = { buffer, buffer_len - 1 };
		char cmsg_buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr *cmsg = (struct cmsghdr *) cmsg_buf;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = cmsg_buf;
		msg.msg_controllen = sizeof(cmsg_buf);
		memset(buffer, 0, buffer_len);
		errno = 0;
		if (recvmsg(ccs_persistent_fd, &msg, 0) <= 0)
			break;
		cmsg = CMSG_FIRSTHDR(&msg);
		if (!cmsg)
			break;
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_RIGHTS &&
		    cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
			const int *fdp = (int *) CMSG_DATA(cmsg);
			const int fd = *fdp;
			fp = fdopen(fd, "w+");
			if (!fp) {
				close(fd);
				continue;
			}
		} else {
			break;
		}
		if (ccs_str_starts(buffer, "POST ")) {
			if (!strcmp(buffer, CCS_PROC_POLICY_DOMAIN_POLICY))
				ccs_handle_domain_policy(&dp, fp, true);
			else if (!strcmp(buffer, CCS_PROC_POLICY_EXCEPTION_POLICY))
				ccs_handle_misc_policy(&mp[0], fp, true);
			else if (!strcmp(buffer, CCS_PROC_POLICY_PROFILE))
				ccs_handle_misc_policy(&mp[1], fp, true);
			else if (!strcmp(buffer, CCS_PROC_POLICY_MANAGER))
				ccs_handle_misc_policy(&mp[2], fp, true);
		} else if (ccs_str_starts(buffer, "GET ")) {
			if (!strcmp(buffer, CCS_PROC_POLICY_DOMAIN_POLICY))
				ccs_handle_domain_policy(&dp, fp, false);
			else if (!strcmp(buffer, CCS_PROC_POLICY_EXCEPTION_POLICY))
				ccs_handle_misc_policy(&mp[0], fp, false);
			else if (!strcmp(buffer, CCS_PROC_POLICY_PROFILE))
				ccs_handle_misc_policy(&mp[1], fp, false);
			else if (!strcmp(buffer, CCS_PROC_POLICY_MANAGER))
				ccs_handle_misc_policy(&mp[2], fp, false);
		}
		fclose(fp);
	}
	ccs_put();
	ccs_clear_domain_policy(&dp);
	{
		int i;
		for (i = 0; i < 3; i++) {
			free(mp[i].list);
			mp[i].list = NULL;
			mp[i].list_len = 0;
		}
	}
	free(buffer);
	_exit(0);
}