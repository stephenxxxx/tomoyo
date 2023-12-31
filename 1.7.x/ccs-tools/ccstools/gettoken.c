/*
 * gettoken.c
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
#include <time.h>

int main(int argc, char *argv[])
{
	static char seed[40];
	int i;
	srand(time(NULL) / 30);
	memset(seed, 0, sizeof(seed));
	for (i = 0; i < sizeof(seed) - 1; i++)
		seed[i] = (rand() % 64) + 33;
	printf("%s\n", seed);
	return 0;
}
