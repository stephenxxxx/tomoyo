/*
 * security/ccsecurity/load_policy.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * Version: 1.8.3   2011/09/29
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/binfmts.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/namei.h>

/*
 * TOMOYO specific part start.
 */

#include <linux/ccsecurity.h>

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

#ifndef CONFIG_CCSECURITY_OMIT_USERSPACE_LOADER

/* Path to the policy loader. (default = CONFIG_CCSECURITY_POLICY_LOADER) */
static const char *ccs_loader;

/**
 * ccs_loader_setup - Set policy loader.
 *
 * @str: Program to use as a policy loader (e.g. /sbin/ccs-init ).
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
 * ccs_policy_loader_exists - Check whether /sbin/ccs-init exists.
 *
 * Returns true if /sbin/ccs-init exists, false otherwise.
 */
static _Bool ccs_policy_loader_exists(void)
{
	struct path path;
	if (!ccs_loader)
		ccs_loader = CONFIG_CCSECURITY_POLICY_LOADER;
	if (kern_path(ccs_loader, LOOKUP_FOLLOW, &path) == 0) {
		path_put(&path);
		return 1;
	}
	printk(KERN_INFO "Not activating Mandatory Access Control "
	       "as %s does not exist.\n", ccs_loader);
	return 0;
}

/* Path to the trigger. (default = CONFIG_CCSECURITY_ACTIVATION_TRIGGER) */
static const char *ccs_trigger;

/**
 * ccs_trigger_setup - Set trigger for activation.
 *
 * @str: Program to use as an activation trigger (e.g. /sbin/init ).
 *
 * Returns 0.
 */
static int __init ccs_trigger_setup(char *str)
{
	ccs_trigger = str;
	return 0;
}

__setup("CCS_trigger=", ccs_trigger_setup);

/**
 * ccs_load_policy - Run external policy loader to load policy.
 *
 * @filename: The program about to start.
 *
 * Returns nothing.
 *
 * This function checks whether @filename is /sbin/init, and if so
 * invoke /sbin/ccs-init and wait for the termination of /sbin/ccs-init
 * and then continues invocation of /sbin/init.
 * /sbin/ccs-init reads policy files in /etc/ccs/ directory and
 * writes to /proc/ccs/ interfaces.
 */
static void ccs_load_policy(const char *filename)
{
	static _Bool done;
	if (ccsecurity_ops.disabled || done)
		return;
	if (!ccs_trigger)
		ccs_trigger = CONFIG_CCSECURITY_ACTIVATION_TRIGGER;
	if (strcmp(filename, ccs_trigger))
		return;
	if (!ccs_policy_loader_exists())
		return;
	done = 1;
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
		call_usermodehelper(argv[0], argv, envp, 1);
	}
	if (ccsecurity_ops.check_profile)
		ccsecurity_ops.check_profile();
	else
		panic("Failed to load policy.");
}

#endif

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
#ifndef CONFIG_CCSECURITY_OMIT_USERSPACE_LOADER
	ccs_load_policy(bprm->filename);
#endif
	/*
	 * ccs_load_policy() executes /sbin/ccs-init if bprm->filename is
	 * /sbin/init. /sbin/ccs-init executes /etc/ccs/ccs-load-module to
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
 * we don't put these into security/ccsecurity/internal.h because we want to
 * split built-in part and loadable kernel module part.
 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)
extern spinlock_t vfsmount_lock;
#endif

/* For exporting variables and functions. */
const struct ccsecurity_exports ccsecurity_exports = {
#ifndef CONFIG_CCSECURITY_OMIT_USERSPACE_LOADER
	.load_policy = ccs_load_policy,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	.__d_path = __d_path,
#else
	.vfsmount_lock = &vfsmount_lock,
#endif
	.find_task_by_vpid = find_task_by_vpid,
	.find_task_by_pid_ns = find_task_by_pid_ns,
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