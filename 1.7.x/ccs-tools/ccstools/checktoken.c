/*
 * checktoken.c
 *
 * An example program for CERBERUS.
 * ( https://osdn.jp/projects/tomoyo/document/winf2005-en.pdf )
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * Version: 1.7.3   2011/04/01
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <time.h>

static const char *get_shell(void)
{
	static char *shell = NULL;
	if (!shell) {
		struct passwd *pw = getpwuid(getuid());
		shell = pw ? pw->pw_shell : "/bin/sh";
	}
	return shell;
}

int main(int argc, char *argv[])
{
	static char buffer[1024];
	static char seed[40];
	int i;
	int trial;
	const char *shell = get_shell();
	srand(time(NULL) / 30);
	memset(seed, 0, sizeof(seed));
	for (i = 0; i < sizeof(seed) - 1; i++)
		seed[i] = (rand() % 64) + 33;
	for (trial = 0; trial < 3; trial++) {
		char *dp;
		memset(buffer, 0, sizeof(buffer));
		printf("Password: ");
		fgets(buffer, sizeof(buffer) - 1, stdin);
		dp = strchr(buffer, '\n');
		if (dp)
			*dp = '\0';
		if (!strcmp(buffer, seed)) {
			if (shell)
				execlp(shell, shell, NULL);
		}
		sleep(3);
	}
	printf("Authentication Failure\n");
	return 0;
}
