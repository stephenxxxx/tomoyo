/*
 * force-logout.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * Version: 1.7.3   2011/04/01
 *
 */
/*
 * This utility forcibly chases away the user who logged in via network (e.g.
 * SSH).
 * This utility is designed for denied_execute_handler so that an intruder who
 * attempted to execute some programs which are not permitted by policy is
 * automatically chased away.
 * You need to set SUID bit to make vhangup() work.
 */
#include <unistd.h>
#include <sys/socket.h>

int main(int argc, char *argv[])
{
	vhangup();
	shutdown(0, SHUT_RD);
	shutdown(1, SHUT_WR);
	shutdown(2, SHUT_WR);
	return 0;
}
