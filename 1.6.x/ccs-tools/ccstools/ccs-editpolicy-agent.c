#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

static _Bool verbose = 0;

static void do_child(const int client)
{
	int i;
	int fd = EOF;
	char buffer[1024];
	while (1) {
		/* Read filename. */
		for (i = 0; i < sizeof(buffer) - 1; i++) {
			if (read(client, buffer + i, 1) != 1)
				goto out;
			if (!buffer[i]) {
				char *cp = strrchr(buffer, '/');
				if (!cp)
					cp = buffer;
				else
					cp++;
				/* Open for read/write. */
				fd = open(cp, O_RDWR);
				break;
			}
		}
		if (fd == EOF) 
			goto out;
		/* Return \0 to indicate success. */
		if (write(client, "", 1) != 1)
			goto out;
		if (verbose) {
			write(2, "opened ", 7);
			write(2, buffer, strlen(buffer));
			write(2, "\n", 1);
		}
		while (1) {
			char c;
			/* Read a byte. */
			if (read(client, &c, 1) != 1)
				goto out;
			if (c) {
				/* Write that byte. */
				if (write(fd, &c, 1) != 1)
					goto out;
				if (verbose)
					write(1, &c, 1);
				continue;
			}
			/* Read until EOF. */
			while (1) {
				int len = read(fd, buffer, sizeof(buffer));
				if (len == 0)
					break;
				/* Don't send \0 because it is EOF marker. */
				if (len < 0 || memchr(buffer, '\0', len) ||
				    write(client, buffer, len) != len)
					goto out;
			}
			/* Return \0 to indicate EOF. */
			if (write(client, "", 1) != 1)
				goto out;
		}
	}
 out:
	if (verbose)
		write(2, "disconnected\n", 13);
	close(fd);
	close(client);
}

int main(int argc, char *argv[])
{
	const int listener = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	socklen_t size = sizeof(addr);
	char *port;
	if (chdir("/proc/ccs/") && chdir("/sys/kernel/security/tomoyo/"))
		return 1;
	{
		int i;
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--verbose"))
				continue;
			verbose = 1;
			argc--;
			for (; i < argc; i++)
				argv[i] = argv[i + 1];
			break;
		}
	}
	if (argc != 2) {
usage:
		fprintf(stderr, "%s listen_address:listen_port\n", argv[0]);
		return 1;
	}
	port = strchr(argv[1], ':');
	if (!port)
		goto usage;
	*port++ = '\0';
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	addr.sin_port = htons(atoi(port));
	if (bind(listener, (struct sockaddr *) &addr, sizeof(addr)) ||
	    listen(listener, 5) ||
	    getsockname(listener, (struct sockaddr *) &addr, &size)) {
		close(listener);
		return 1;
	}
	{
		const unsigned int ip = ntohl(addr.sin_addr.s_addr);
		printf("Listening at %u.%u.%u.%u:%u\n",
		       (unsigned char) (ip >> 24), (unsigned char) (ip >> 16),
		       (unsigned char) (ip >> 8), (unsigned char) ip,
		       ntohs(addr.sin_port));
		fflush(stdout);
	}
	close(0);
	if (!verbose) {
		close(1);
		close(2);
	}
	signal(SIGCHLD, SIG_IGN);
	while (1) {
		socklen_t size = sizeof(addr);
		const int client = accept(listener, (struct sockaddr *) &addr,
					  &size);
		if (client == EOF) {
			if (verbose)
				fprintf(stderr, "accept() failed\n");
			continue;
		}
		switch (fork()) {
		case 0:
			close(listener);
			do_child(client);
			_exit(0);
		case -1:
			if (verbose)
				fprintf(stderr, "fork() failed\n");
			close(client);
			break;
		default:
			close(client);
		}
	}
	close(listener);
	return 1;
}
