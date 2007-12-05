/*
 * chaplet.c
 *
 * An example program for CERBERUS.
 * ( http://sourceforge.jp/projects/tomoyo/document/winf2005-en.pdf )
 *
 * Copyright (C) 2005-2006  NTT DATA CORPORATION
 *
 * Version: 1.0 2005/11/11
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <time.h>

static const char *get_shell(void) {
	static char *shell = NULL;
	if (!shell) {
		struct passwd *pw = getpwuid(getuid());
		shell = pw ? pw->pw_shell : "/bin/sh";
	}
	return shell;
}

int main(int argc, char *argv[]) {
	static char buffer[1024];
	static char seed[40];
	int i, trial;
	const char *shell = get_shell();
	srand(time(NULL));
	for (trial = 0; trial < 3; trial++) {
		char *sp, *dp;
		memset(seed, 0, sizeof(seed));
		for (i = 0; i < sizeof(seed) - 1; i++) seed[i] = (rand() % 64) + 33;
		printf("Challenge: %s\n", seed);
		sp = dp = seed;
		while ((*dp = *sp) != '\0') {
			if (*sp < '0' || *sp > '9') { sp++; continue; }
			sp++; dp++;
		}
		//printf("Answer: %s\n", seed);
		memset(buffer, 0, sizeof(buffer));
		printf("Response: ");
		fgets(buffer, sizeof(buffer) - 1, stdin);
		if ((dp = strchr(buffer, '\n')) != NULL) *dp = '\0';
		if (strcmp(buffer, seed) == 0) {
			if (shell) execlp(shell, shell, NULL);
		}
		sleep(3);
	}
	printf("Authentication Failure\n");
	return 0;
}
