/*
 * security/ccsecurity/load_policy.c
 *
 * Copyright (C) 2005-2012  NTT DATA CORPORATION
 *
 * Version: 1.7.3+   2015/01/01
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/binfmts.h>
#include <linux/sched.h>
#include <linux/ccsecurity.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
#include <linux/kmod.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#define ccs_lookup_flags  (LOOKUP_FOLLOW | LOOKUP_POSITIVE)
#else
#include <linux/fs.h>
#include <linux/namei.h>
#define ccs_lookup_flags LOOKUP_FOLLOW
#endif

/* Path to the policy loader. (default = CONFIG_CCSECURITY_DEFAULT_LOADER) */
static const char *ccs_loader;

/**
 * ccs_loader_setup - Specify the policy loader to use.
 *
 * @str: Path to the policy loader.
 *
 * Returns 0.
 */
static int __init ccs_loader_setup(char *str)
{
	ccs_loader = str;
	return 0;
}

__setup("CCS_loader=", ccs_loader_setup);

/**
 * ccs_setup - Set enable/disable upon boot.
 *
 * @str: "off" to disable, "on" to enable.
 *
 * Returns 0.
 */
static int __init ccs_setup(char *str)
{
	if (!strcmp(str, "off"))
		ccsecurity_ops.disabled = 1;
	else if (!strcmp(str, "on"))
		ccsecurity_ops.disabled = 0;
	return 0;
}

__setup("ccsecurity=", ccs_setup);

/**
 * ccs_policy_loader_exists - Check whether /sbin/ccs-init exists.
 *
 * Returns true if /sbin/ccs-init exists, false otherwise.
 */
static _Bool ccs_policy_loader_exists(void)
{
	/*
	 * Don't activate MAC if the path given by 'CCS_loader=' option doesn't
	 * exist. If the initrd includes /sbin/init but real-root-dev has not
	 * mounted on / yet, activating MAC will block the system since
	 * policies are not loaded yet.
	 * Thus, let do_execve() call this function every time.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
	struct path path;
	if (!ccs_loader)
		ccs_loader = CONFIG_CCSECURITY_DEFAULT_LOADER;
	if (kern_path(ccs_loader, ccs_lookup_flags, &path) == 0) {
		path_put(&path);
		return 1;
	}
#else
	struct nameidata nd;
	if (!ccs_loader)
		ccs_loader = CONFIG_CCSECURITY_DEFAULT_LOADER;
	if (path_lookup(ccs_loader, ccs_lookup_flags, &nd) == 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
		path_put(&nd.path);
#else
		path_release(&nd);
#endif
		return 1;
	}
#endif
	printk(KERN_INFO "Not activating Mandatory Access Control now "
	       "since %s doesn't exist.\n", ccs_loader);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
/**
 * ccs_run_loader - Start /sbin/ccs-init .
 *
 * @unused: Not used.
 *
 * Returns PID of /sbin/ccs-init on success, negative value otherwise.
 */
static int ccs_run_loader(void *unused)
{
	char *argv[2];
	char *envp[3];
	printk(KERN_INFO "Calling %s to load policy. Please wait.\n",
	       ccs_loader);
	argv[0] = (char *) ccs_loader;
	argv[1] = NULL;
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[2] = NULL;
	return exec_usermodehelper(argv[0], argv, envp);
}
#endif

/**
 * ccs_load_policy - Run external policy loader to load policy.
 *
 * @filename: The program about to start.
 *
 * This function checks whether @filename is /sbin/init , and if so
 * invoke /sbin/ccs-init and wait for the termination of /sbin/ccs-init
 * and then continues invocation of /sbin/init.
 * /sbin/ccs-init reads policy files in /etc/ccs/ directory and
 * writes to /proc/ccs/ interfaces.
 *
 * Returns nothing.
 */
static void ccs_load_policy(const char *filename)
{
	if (ccsecurity_ops.disabled)
		return;
	if (strcmp(filename, "/sbin/init") &&
	    strcmp(filename, CONFIG_CCSECURITY_ALTERNATIVE_TRIGGER))
		return;
	if (!ccs_policy_loader_exists())
		return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
	{
		char *argv[2];
		char *envp[3];
		printk(KERN_INFO "Calling %s to load policy. Please wait.\n",
		       ccs_loader);
		argv[0] = (char *) ccs_loader;
		argv[1] = NULL;
		envp[0] = "HOME=/";
		envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
		envp[2] = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23) || defined(UMH_WAIT_PROC)
		call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
#else
		call_usermodehelper(argv[0], argv, envp, 1);
#endif
	}
#elif defined(TASK_DEAD)
	{
		/* Copied from kernel/kmod.c */
		struct task_struct *task = current;
		pid_t pid = kernel_thread(ccs_run_loader, NULL, 0);
		sigset_t tmpsig;
		spin_lock_irq(&task->sighand->siglock);
		tmpsig = task->blocked;
		siginitsetinv(&task->blocked,
			      sigmask(SIGKILL) | sigmask(SIGSTOP));
		recalc_sigpending();
		spin_unlock_irq(&task->sighand->siglock);
		if (pid >= 0)
			waitpid(pid, NULL, __WCLONE);
		spin_lock_irq(&task->sighand->siglock);
		task->blocked = tmpsig;
		recalc_sigpending();
		spin_unlock_irq(&task->sighand->siglock);
	}
#else
	{
		/* Copied from kernel/kmod.c */
		struct task_struct *task = current;
		pid_t pid = kernel_thread(ccs_run_loader, NULL, 0);
		sigset_t tmpsig;
		spin_lock_irq(&task->sigmask_lock);
		tmpsig = task->blocked;
		siginitsetinv(&task->blocked,
			      sigmask(SIGKILL) | sigmask(SIGSTOP));
		recalc_sigpending(task);
		spin_unlock_irq(&task->sigmask_lock);
		if (pid >= 0)
			waitpid(pid, NULL, __WCLONE);
		spin_lock_irq(&task->sigmask_lock);
		task->blocked = tmpsig;
		recalc_sigpending(task);
		spin_unlock_irq(&task->sigmask_lock);
	}
#endif
	if (ccsecurity_ops.check_profile)
		ccsecurity_ops.check_profile();
	else
		panic("Failed to load policy.");
}

/**
 * __ccs_search_binary_handler - Load policy before calling search_binary_handler().
 *
 * @bprm: Pointer to "struct linux_binprm".
 * @regs: Pointer to "struct pt_regs".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int __ccs_search_binary_handler(struct linux_binprm *bprm,
				       struct pt_regs *regs)
{
	ccs_load_policy(bprm->filename);
	/*
	 * ccs_load_policy() executes /sbin/ccs-init if bprm->filename is
	 * /sbin/init . /sbin/ccs-init executes /etc/ccs/ccs-load-module to
	 * load loadable kernel module. The loadable kernel module modifies
	 * "struct ccsecurity_ops". Thus, we need to transfer control to
	 * __ccs_search_binary_handler() in security/ccsecurity/domain.c
	 * if "struct ccsecurity_ops" was modified.
	 */
	if (ccsecurity_ops.search_binary_handler
	    != __ccs_search_binary_handler)
		return ccsecurity_ops.search_binary_handler(bprm, regs);
	return search_binary_handler(bprm, regs);
}

/*
 * Some exports for loadable kernel module part.
 *
 * Although scripts/checkpatch.pl complains about use of "extern" in C file,
 * we don't put these into security/ccsecurity/compat.h because we want to
 * split built-in part and loadable kernel module part.
 */
extern asmlinkage long sys_getpid(void);
extern asmlinkage long sys_getppid(void);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)
extern spinlock_t vfsmount_lock;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
static inline void module_put(struct module *module)
{
	if (module)
		__MOD_DEC_USE_COUNT(module);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
static void put_filesystem(struct file_system_type *fs)
{
	module_put(fs->owner);
}
#endif

/* Jump table for loadable kernel module. */
const struct ccsecurity_exports ccsecurity_exports = {
	.load_policy = ccs_load_policy,
	.put_filesystem = put_filesystem,
	.sys_getppid = sys_getppid,
	.sys_getpid = sys_getpid,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)
	.vfsmount_lock = &vfsmount_lock,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	.find_task_by_vpid = find_task_by_vpid,
	.find_task_by_pid_ns = find_task_by_pid_ns,
#endif
};
#ifdef CONFIG_CCSECURITY_LKM
/* Only ccsecurity module need to access this struct. */
EXPORT_SYMBOL_GPL(ccsecurity_exports);
#endif

/* Members are updated by loadable kernel module. */
struct ccsecurity_operations ccsecurity_ops = {
	.search_binary_handler = __ccs_search_binary_handler,
#ifdef CONFIG_CCSECURITY_DISABLE_BY_DEFAULT
	.disabled = 1,
#endif
};
/*
 * Non-GPL modules might need to access this struct via inlined functions
 * embedded into include/linux/security.h and include/net/ip.h
 */
EXPORT_SYMBOL(ccsecurity_ops);
