/* convert-audit-log.c */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char buffer[65536];
static char *cond = NULL;
static int cond_len = 0;

static void realloc_buffer(const int len)
{
	cond = realloc(cond, cond_len + len);
	if (!cond)
		exit(1);
}

static void handle_task_condition(void)
{
	while (fscanf(stdin, "%65534s", buffer) == 1 && strcmp(buffer, "}")) {
		realloc_buffer(strlen(buffer) + 7);
		cond_len += sprintf(cond + cond_len, " task.%s", buffer);
	}
}

static void handle_path_condition(const char *path)
{
	const int len0 = strlen(path) + 2;
	while (fscanf(stdin, "%65534s", buffer) == 1 && strcmp(buffer, "}")) {
		realloc_buffer(len0 + strlen(path));
		cond_len += sprintf(cond + cond_len, " %s%s", path, buffer);
	}
}

static void handle_argv_condition(void)
{
	int i = 0;
	while (fscanf(stdin, "%65534s", buffer) == 1 && strcmp(buffer, "}")
	       && strcmp(buffer, "...")) {
		realloc_buffer(strlen(buffer) + 34);
		cond_len += sprintf(cond + cond_len, " exec.argv[%u]=%s", i++,
				    buffer);
	}
}

static void handle_envp_condition(void)
{
	while (fscanf(stdin, "%65534s", buffer) == 1 && strcmp(buffer, "}")
	       && strcmp(buffer, "...")) {
		char *cp = strchr(buffer, '=');
		if (!cp)
			break;
		realloc_buffer(strlen(buffer) + 16);
		*cp++ = '\0';
		cond_len += sprintf(cond + cond_len, " exec.envp[%s\"]=\"%s",
				    buffer, cp);
	}
}

static void handle_exec_condition(void)
{
	while (fscanf(stdin, "%65534s", buffer) == 1 && strcmp(buffer, "}")) {
		if (!strcmp(buffer, "argv[]={"))
			handle_argv_condition();
		else if (!strcmp(buffer, "envp[]={"))
			handle_envp_condition();
		else {
			realloc_buffer(strlen(buffer) + 7);
			cond_len += sprintf(cond + cond_len, " exec.%s",
					    buffer);
		}
	}
}

int main(int argc, char *argv[])
{
	memset(buffer, 0, sizeof(buffer));
	if (argc > 1) {
		fprintf(stderr, "Usage: %s < grant_log or reject_log\n",
			argv[0]);
		return 0;
	}
	while (fscanf(stdin, "%65534s", buffer) == 1) {
		if (!strcmp(buffer, "task={"))
			handle_task_condition();
		else if (!strcmp(buffer, "path1={"))
			handle_path_condition("path1.");
		else if (!strcmp(buffer, "path1.parent={"))
			handle_path_condition("path1.parent.");
		else if (!strcmp(buffer, "path2={"))
			handle_path_condition("path2.");
		else if (!strcmp(buffer, "path2.parent={"))
			handle_path_condition("path2.parent.");
		else if (!strcmp(buffer, "path2.parent={"))
			handle_path_condition("path2.parent.");
		else if (!strcmp(buffer, "exec={"))
			handle_exec_condition();
		else if (!strncmp(buffer, "symlink.target=", 15)) {
			realloc_buffer(strlen(buffer) + 2);
			cond_len += sprintf(cond + cond_len, " %s", buffer);
		} else if (!strcmp(buffer, "<kernel>")) {
			char *cp;
			int c;
			printf("<kernel>");
			while (1) {
				c = getchar();
				if (c == '\n' || c == EOF)
					break;
				putchar(c);
			}
			if (c == EOF)
				break;
			putchar('\n');
			if (!fgets(buffer, sizeof(buffer) - 1, stdin))
				break;
			cp = strstr(buffer, " if ");
			if (!cp)
				cp = strchr(buffer, '\n');
			if (!cp)
				break;
			*cp = '\0';
			printf("%s", buffer);
			if (cond_len) {
				printf(" if%s", cond);
				cond_len = 0;
			}
			putchar('\n');
		}
	}
	free(cond);
	return 0;
}
