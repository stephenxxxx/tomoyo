/*
 * include.h
 *
 * Common functions for testing TOMOYO Linux's kernel.
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * Version: 1.6.9   2011/04/01
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/kdev_t.h>
struct module;
#include <linux/reboot.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <arpa/inet.h>
#include <asm/byteorder.h>
#include <linux/ip.h>
#include <sched.h>
#include <sys/ptrace.h>
#include <sys/ioctl.h>
#include <net/if.h>

#ifndef __NR_sys_kexec_load
#ifdef __NR_kexec_load
#define __NR_sys_kexec_load  __NR_kexec_load
#endif
#endif

static inline pid_t gettid(void)
{
	return syscall(__NR_gettid);
}
static inline int uselib(const char *library)
{
	return syscall(__NR_uselib, library);
}
static inline caddr_t create_module(const char *name, size_t size)
{
	return (caddr_t) syscall(__NR_create_module, name, size);
}
static inline int pivot_root(const char *new_root, const char *put_old)
{
	return syscall(__NR_pivot_root, new_root, put_old);
}
static inline int tkill(int tid, int sig)
{
	return syscall(__NR_tkill, tid, sig);
}
#ifdef __NR_tgkill
static inline int tgkill(int tgid, int tid, int sig)
{
	return syscall(__NR_tgkill, tgid, tid, sig);
}
#endif
#ifdef __NR_sys_kexec_load
struct kexec_segment;
static inline long sys_kexec_load(unsigned long entry,
				  unsigned long nr_segments,
				  struct kexec_segment *segments,
				  unsigned long flags)
{
	return (long) syscall(__NR_sys_kexec_load, entry, nr_segments,
			      segments, flags);
}
#endif
/* reboot() in glibc takes just one argument. */
int reboot(int cmd);
int init_module(const char *name, struct module *image);
int delete_module(const char *name);

static const char *proc_policy_dir    = "/proc/ccs/",
	*proc_policy_domain_policy    = "/proc/ccs/domain_policy",
	*proc_policy_exception_policy = "/proc/ccs/exception_policy",
	*proc_policy_system_policy    = "/proc/ccs/system_policy",
	*proc_policy_profile          = "/proc/ccs/profile",
	*proc_policy_manager          = "/proc/ccs/manager",
	*proc_policy_query            = "/proc/ccs/query",
	*proc_policy_grant_log        = "/proc/ccs/grant_log",
	*proc_policy_reject_log       = "/proc/ccs/reject_log",
	*proc_policy_domain_status    = "/proc/ccs/.domain_status",
	*proc_policy_process_status   = "/proc/ccs/.process_status",
	*proc_policy_self_domain      = "/proc/ccs/self_domain";

static void ccs_test_pre_init(void)
{
	if (access("/sys/kernel/security/tomoyo/", F_OK) == 0) {
		proc_policy_dir              = "/sys/kernel/security/tomoyo/";
		proc_policy_domain_policy    =
			"/sys/kernel/security/tomoyo/domain_policy";
		proc_policy_exception_policy =
			"/sys/kernel/security/tomoyo/exception_policy";
		proc_policy_system_policy    =
			"/sys/kernel/security/tomoyo/system_policy";
		proc_policy_profile          =
			"/sys/kernel/security/tomoyo/profile";
		proc_policy_manager          =
			"/sys/kernel/security/tomoyo/manager";
		proc_policy_query            =
			"/sys/kernel/security/tomoyo/query";
		proc_policy_grant_log        =
			"/sys/kernel/security/tomoyo/grant_log";
		proc_policy_reject_log       =
			"/sys/kernel/security/tomoyo/reject_log";
		proc_policy_domain_status    =
			"/sys/kernel/security/tomoyo/.domain_status";
		proc_policy_process_status   =
			"/sys/kernel/security/tomoyo/.process_status";
		proc_policy_self_domain      =
			"/sys/kernel/security/tomoyo/self_domain";
	}
}

static int profile_fd = EOF;
static int is_kernel26 = 0;
static pid_t pid = 0;

static void write_status(const char *cp)
{
	write(profile_fd, "255-", 4);
	write(profile_fd, cp, strlen(cp));
}

static void clear_status(void)
{
	FILE *fp = fopen(proc_policy_profile, "r");
	static char buffer[4096];
	if (!fp) {
		fprintf(stderr, "Can't open %s\n", proc_policy_profile);
		exit(1);
	}
	while (memset(buffer, 0, sizeof(buffer)),
	       fgets(buffer, sizeof(buffer) - 10, fp)) {
		const char *mode;
		int v;
		char *cp = strchr(buffer, '=');
		if (!cp)
			continue;
		*cp = '\0';
		mode = cp + 1;
		cp = strchr(buffer, '-');
		if (!cp)
			continue;
		*cp++ = '\0';
		if (strcmp(buffer, "0"))
			continue;
		/*
		  if (strcmp(cp, "TOMOYO_VERBOSE") == 0)
		  continue;
		*/
		write(profile_fd, "255-", 4);
		write(profile_fd, cp, strlen(cp));
		if (!strcmp(cp, "COMMENT"))
			mode = "=Profile for kernel test\n";
		else if (sscanf(mode, "%u", &v) == 1)
			mode = "=0\n";
		else
			mode = "=disabled\n";
		write(profile_fd, mode, strlen(mode));
	}
	/* write(profile_fd, "255-SLEEP_PERIOD=1\n", 19); */
	/* write(profile_fd, "255-TOMOYO_VERBOSE=1\n", 21); */
	fclose(fp);
}

static void ccs_test_init(void)
{
	ccs_test_pre_init();
	pid = getpid();
	if (access(proc_policy_dir, F_OK)) {
		fprintf(stderr, "You can't use this program for this kernel."
			"\n");
		exit(1);
	}
	profile_fd = open(proc_policy_profile, O_WRONLY);
	if (profile_fd == EOF) {
		fprintf(stderr, "Can't open %s .\n", proc_policy_profile);
		exit(1);
	}
	if (write(profile_fd, "", 0) != 0) {
		fprintf(stderr, "You need to register this program to %s to "
			"run this program.\n", proc_policy_manager);
		exit(1);
	}
	clear_status();
	{
		FILE *fp = fopen("/proc/sys/kernel/osrelease", "r");
		int version = 0;
		if (!fp || fscanf(fp, "2.%d.", &version) != 1) {
			fprintf(stderr, "Can't read /proc/sys/kernel/osrelease"
				"\n");
			exit(1);
		}
		fclose(fp);
		if (version == 6)
			is_kernel26 = 1;
	}
	{
		char buffer[4096];
		FILE *fp = fopen(proc_policy_self_domain, "r");
		memset(buffer, 0, sizeof(buffer));
		if (fp) {
			fgets(buffer, sizeof(buffer) - 1, fp);
			fclose(fp);
		} else
			exit(1);
		fp = fopen(proc_policy_domain_status, "w");
		if (fp) {
			fprintf(fp, "255 %s\n", buffer);
			fclose(fp);
		} else
			exit(1);
	}
}
