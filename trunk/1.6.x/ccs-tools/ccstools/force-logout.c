/*
 * force-logout.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2008  NTT DATA CORPORATION
 *
 * Version: 1.6.0-pre   2008/01/28
 *
 */
/*
 * This utility forcibly chases away the user who logged in via network (e.g. SSH).
 * This utility is designed for ALT_EXEC feature so that an intruder who attempted to
 * execute some programs which are not permitted by policy is automatically chased away.
 * You need to set SUID bit to make vhangup() work.
 */
#include <unistd.h>
#include <sys/socket.h>

int main(int argc, char *argv[]) {
	vhangup();
	shutdown(0, SHUT_RD);
	shutdown(1, SHUT_WR);
	shutdown(2, SHUT_WR);
	return 0;
}
