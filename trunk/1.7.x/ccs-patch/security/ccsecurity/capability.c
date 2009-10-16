/*
 * security/ccsecurity/capability.c
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 1.7.1-pre   2009/10/16
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include "internal.h"

/**
 * ccs_audit_capability_log - Audit capability log.
 *
 * @r:          Pointer to "struct ccs_request_info".
 * @operation:  Type of operation.
 * @is_granted: True if this is a granted log.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int ccs_audit_capability_log(struct ccs_request_info *r,
				    const u8 operation, const bool is_granted)
{
	if (!is_granted)
		ccs_warn_log(r, "capability %s", ccs_cap2keyword(operation));
	return ccs_write_audit_log(is_granted, r, CCS_KEYWORD_ALLOW_CAPABILITY
				   "%s\n", ccs_cap2keyword(operation));
}

/**
 * ccs_capable - Check permission for capability.
 *
 * @operation: Type of operation.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds ccs_read_lock().
 */
static bool ccs_capable2(const u8 operation)
{
	struct ccs_request_info r;
	struct ccs_acl_info *ptr;
	int error;
	if (ccs_init_request_info(&r, NULL, CCS_MAX_MAC_INDEX + operation)
	    == CCS_CONFIG_DISABLED)
		return true;
	do {
		error = -EPERM;
		list_for_each_entry_rcu(ptr, &r.domain->acl_info_list, list) {
			struct ccs_capability_acl *acl;
			if (ptr->is_deleted ||
			    ptr->type != CCS_TYPE_CAPABILITY_ACL)
				continue;
			acl = container_of(ptr, struct ccs_capability_acl,
					   head);
			if (acl->operation != operation ||
			    !ccs_condition(&r, ptr))
				continue;
			r.cond = ptr->cond;
			error = 0;
			break;
		}
		ccs_audit_capability_log(&r, operation, !error);
		if (!error)
			break;
		error = ccs_supervisor(&r, CCS_KEYWORD_ALLOW_CAPABILITY "%s\n",
				       ccs_cap2keyword(operation));
	} while (error == 1);
	return !error;
}

/**
 * ccs_capable - Check permission for capability.
 *
 * @operation: Type of operation.
 *
 * Returns true on success, false otherwise.
 */
bool ccs_capable(const u8 operation)
{
	const int idx = ccs_read_lock();
	const int error = ccs_capable2(operation);
	ccs_read_unlock(idx);
	return error;
}

/**
 * ccs_write_capability_policy - Write "struct ccs_capability_acl" list.
 *
 * @data:      String to parse.
 * @domain:    Pointer to "struct ccs_domain_info".
 * @condition: Pointer to "struct ccs_condition". May be NULL.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_write_capability_policy(char *data, struct ccs_domain_info *domain,
				struct ccs_condition *condition,
				const bool is_delete)
{
	struct ccs_capability_acl e = {
		.head.type = CCS_TYPE_CAPABILITY_ACL,
		.head.cond = condition,
	};
	struct ccs_capability_acl *entry = NULL;
	struct ccs_acl_info *ptr;
	int error = is_delete ? -ENOENT : -ENOMEM;
	u8 capability;
	for (capability = 0; capability < CCS_MAX_CAPABILITY_INDEX;
	     capability++) {
		if (strcmp(data, ccs_cap2keyword(capability)))
			continue;
		break;
	}
	if (capability == CCS_MAX_CAPABILITY_INDEX)
		return -EINVAL;
	e.operation = capability;
	if (!is_delete)
		entry = kmalloc(sizeof(e), GFP_KERNEL);
	mutex_lock(&ccs_policy_lock);
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct ccs_capability_acl *acl =
			container_of(ptr, struct ccs_capability_acl,
				     head);
		if (ptr->type != CCS_TYPE_CAPABILITY_ACL ||
		    ptr->cond != condition || acl->operation != capability)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (!is_delete && error && ccs_commit_ok(entry, &e, sizeof(e))) {
		ccs_add_domain_acl(domain, &entry->head);
		entry = NULL;
		error = 0;
	}
	mutex_unlock(&ccs_policy_lock);
	kfree(entry);
	return error;
}
