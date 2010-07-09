/*
 * security/ccsecurity/signal.c
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 * Version: 1.7.2+   2010/06/04
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include "internal.h"

/* To support PID namespace. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
#define find_task_by_pid ccsecurity_exports.find_task_by_vpid
#endif

/**
 * ccs_audit_signal_log - Audit signal log.
 *
 * @r: Pointer to "struct ccs_request_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_signal_log(struct ccs_request_info *r)
{
	const int sig = r->param.signal.sig;
	const char *dest_domain = r->param.signal.dest_pattern;
	ccs_write_log(r, "ipc signal %d %s\n", sig, dest_domain);
	if (r->granted)
		return 0;
	ccs_warn_log(r, "signal %d to %s", sig, ccs_last_word(dest_domain));
	return ccs_supervisor(r, "ipc signal %d %s\n", sig, dest_domain);
}

static bool ccs_check_signal_acl(const struct ccs_request_info *r,
				 const struct ccs_acl_info *ptr)
{
	const struct ccs_signal_acl *acl =
		container_of(ptr, typeof(*acl), head);
	if (acl->sig == r->param.signal.sig) {
		const int len = acl->domainname->total_len;
		if (!strncmp(acl->domainname->name,
			     r->param.signal.dest_pattern, len)) {
			switch (r->param.signal.dest_pattern[len]) {
			case ' ':
			case '\0':
				return true;
			}
		}
	}
	return false;
}

/**
 * ccs_signal_acl2 - Check permission for signal.
 *
 * @sig: Signal number.
 * @pid: Target's PID.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static int ccs_signal_acl2(const int sig, const int pid)
{
	struct ccs_request_info r;
	struct ccs_domain_info *dest = NULL;
	int error;
	const struct ccs_domain_info * const domain = ccs_current_domain();
	if (ccs_init_request_info(&r, CCS_MAC_SIGNAL) == CCS_CONFIG_DISABLED)
		return 0;
	if (!sig)
		return 0;                /* No check for NULL signal. */
	r.param_type = CCS_TYPE_SIGNAL_ACL;
	r.param.signal.sig = sig;
	r.param.signal.dest_pattern = domain->domainname->name;
	r.granted = true;
	if (ccsecurity_exports.sys_getpid() == pid) {
		ccs_audit_signal_log(&r);
		return 0;                /* No check for self process. */
	}
	{ /* Simplified checking. */
		struct task_struct *p = NULL;
		ccs_tasklist_lock();
		if (pid > 0)
			p = find_task_by_pid((pid_t) pid);
		else if (pid == 0)
			p = current;
		else if (pid == -1)
			dest = &ccs_kernel_domain;
		else
			p = find_task_by_pid((pid_t) -pid);
		if (p)
			dest = ccs_task_domain(p);
		ccs_tasklist_unlock();
	}
	if (!dest)
		return 0; /* I can't find destinatioin. */
	if (domain == dest) {
		ccs_audit_signal_log(&r);
		return 0;                /* No check for self domain. */
	}
	r.param.signal.dest_pattern = dest->domainname->name;
	do {
		ccs_check_acl(&r, ccs_check_signal_acl);
		error = ccs_audit_signal_log(&r);
	} while (error == CCS_RETRY_REQUEST);
	return error;
}

/**
 * ccs_signal_acl - Check permission for signal.
 *
 * @pid: Target's PID.
 * @sig: Signal number.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_signal_acl(const int pid, const int sig)
{
	int error;
	if (!sig)
		error = 0;
	else if (!ccs_capable(CCS_SYS_KILL))
		error = -EPERM;
	else {
		const int idx = ccs_read_lock();
		error = ccs_signal_acl2(sig, pid);
		ccs_read_unlock(idx);
	}
	return error;
}

/**
 * ccs_signal_acl0 - Permission check for signal().
 *
 * @tgid: Unused.
 * @pid:  PID
 * @sig:  Signal number.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_signal_acl0(pid_t tgid, pid_t pid, int sig)
{
	return ccs_signal_acl(pid, sig);
}

static bool ccs_same_signal_entry(const struct ccs_acl_info *a,
				  const struct ccs_acl_info *b)
{
	const struct ccs_signal_acl *p1 = container_of(a, typeof(*p1), head);
	const struct ccs_signal_acl *p2 = container_of(b, typeof(*p2), head);
	return p1->head.type == p2->head.type && p1->head.cond == p2->head.cond
		&& p1->head.type == CCS_TYPE_SIGNAL_ACL && p1->sig == p2->sig
		&& p1->domainname == p2->domainname;
}

/**
 * ccs_write_signal - Write "struct ccs_signal_acl" list.
 *
 * @data:      String to parse.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". Maybe NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_write_signal(char *data, struct ccs_domain_info *domain,
			    struct ccs_condition *condition,
			    const bool is_delete)
{
	struct ccs_signal_acl e = { .head.type = CCS_TYPE_SIGNAL_ACL,
				    .head.cond = condition };
	int error;
	int sig;
	char *domainname = strchr(data, ' ');
	if (sscanf(data, "%d", &sig) != 1 || !domainname ||
	    !ccs_correct_domain(domainname + 1))
		return -EINVAL;
	e.sig = sig;
	e.domainname = ccs_get_name(domainname + 1);
	if (!e.domainname)
		return -ENOMEM;
	error = ccs_update_domain(&e.head, sizeof(e), is_delete, domain,
				  ccs_same_signal_entry, NULL);
	ccs_put_name(e.domainname);
	return error;
}

int ccs_write_ipc(char *data, struct ccs_domain_info *domain,
		  struct ccs_condition *condition, const bool is_delete)
{
	if (ccs_str_starts(&data, "signal "))
		return ccs_write_signal(data, domain, condition, is_delete);
	return -EINVAL;
}

void __init ccs_signal_init(void)
{
	ccsecurity_ops.kill_permission = ccs_signal_acl;
	ccsecurity_ops.tgkill_permission = ccs_signal_acl0;
	ccsecurity_ops.tkill_permission = ccs_signal_acl;
	ccsecurity_ops.sigqueue_permission = ccs_signal_acl;
	ccsecurity_ops.tgsigqueue_permission = ccs_signal_acl0;
}
