/*
 * pstree.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 1.7.0   2009/09/03
 *
 */
#include "ccstools.h"

static void ccs_dump(const pid_t pid, const int depth)
{
	int i;
	for (i = 0; i < ccs_task_list_len; i++) {
		int j;
		if (pid != ccs_task_list[i].pid)
			continue;
		printf("%3d", ccs_task_list[i].profile);
		for (j = 0; j < depth - 1; j++)
			printf("    ");
		for (; j < depth; j++)
			printf("  +-");
		printf(" %s (%u) %s\n", ccs_task_list[i].name,
		       ccs_task_list[i].pid, ccs_task_list[i].domain);
		ccs_task_list[i].selected = true;
	}
	for (i = 0; i < ccs_task_list_len; i++) {
		if (pid != ccs_task_list[i].ppid)
			continue;
		ccs_dump(ccs_task_list[i].pid, depth + 1);
	}
}

int ccs_pstree_main(int argc, char *argv[])
{
	static _Bool show_all = false;
	int i;
	for (i = 1; i < argc; i++) {
		char *ptr = argv[i];
		char *cp = strchr(ptr, ':');
		if (cp) {
			*cp++ = '\0';
			if (ccs_network_mode)
				goto usage;
			ccs_network_ip = inet_addr(ptr);
			ccs_network_port = htons(atoi(cp));
			ccs_network_mode = true;
			if (!ccs_check_remote_host())
				return 1;
		} else if (!strcmp(ptr, "-a")) {
			show_all = true;
		} else {
usage:
			fprintf(stderr, "Usage: %s "
				"[-a] [remote_ip:remote_port]\n", argv[0]);
			return 0;
		}
	}
	ccs_read_process_list(show_all);
	if (!ccs_task_list_len) {
		if (ccs_network_mode) {
			fprintf(stderr, "Can't connect.\n");
			return 1;
		} else {
			fprintf(stderr, "You can't use this command "
				"for this kernel.\n");
			return 1;
		}
	}
	ccs_dump(1, 0);
	for (i = 0; i < ccs_task_list_len; i++) {
		if (ccs_task_list[i].selected)
			continue;
		printf("%3d %s (%u) %s\n",
		       ccs_task_list[i].profile, ccs_task_list[i].name,
		       ccs_task_list[i].pid, ccs_task_list[i].domain);
		ccs_task_list[i].selected = true;
	}
	while (ccs_task_list_len) {
		ccs_task_list_len--;
		free((void *) ccs_task_list[ccs_task_list_len].name);
		free((void *) ccs_task_list[ccs_task_list_len].domain);
	}
	free(ccs_task_list);
	ccs_task_list = NULL;
	return 0;
}
