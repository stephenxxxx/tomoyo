/*
 * fs/tomoyo_bind.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2006  NTT DATA CORPORATION
 *
 * Version: 1.0 2005/11/11
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */
/***** TOMOYO Linux start. *****/

#include <linux/ccs_common.h>
#include <linux/tomoyo.h>
#include <linux/realpath.h>

/***** The structure for bind()able ports. *****/

typedef struct bind_entry {
	struct bind_entry *next;     /* Pointer to next record. NULL if none.                            */
	DOMAIN_INFO *domain;         /* Pointer to domain record that this entry applies to. Never NULL. */
	int is_stream;               /* Nonzero if TCP, zero if UDP.                                     */
	unsigned short int min_port; /* Start of port number range.                                      */
	unsigned short int max_port; /* End of port number range.                                        */
} BIND_ENTRY;

/*************************  AUDIT FUNCTIONS  *************************/

static int AuditPortLog(const int is_stream, const unsigned short int port, const int is_granted);

/*************************  NETWORK BIND ACL HANDLER  *************************/

static BIND_ENTRY localport_list = { NULL, NULL, 0, 0, 0 };

static int AddBindEntry(const int is_stream, const unsigned short int min_port, const unsigned short int max_port, struct domain_info *domain)
{
	BIND_ENTRY *new_entry, *ptr;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	if (!domain) {
		printk("%s: ERROR: domain == NULL\n", __FUNCTION__);
		return -EINVAL;
	}
	/* I don't want to add if it was already added. */
	for (ptr = localport_list.next; ptr; ptr = ptr->next) {
		if (ptr->domain == domain && ptr->is_stream == is_stream && ptr->min_port <= min_port && max_port <= ptr->max_port) return 0;
	}
	if ((new_entry = (BIND_ENTRY *) alloc_element(sizeof(BIND_ENTRY))) == NULL) return -ENOMEM;
	memset(new_entry, 0, sizeof(BIND_ENTRY));
	new_entry->next = NULL;
	new_entry->domain = domain;
	new_entry->is_stream = is_stream;
	new_entry->min_port = min_port;
	new_entry->max_port = max_port;
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	for (ptr = &localport_list; ptr->next; ptr = ptr->next); ptr->next = new_entry;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
	return 0;
}

int CheckBindEntry(const int is_stream, const unsigned short int port)
{
	struct domain_info * const domain = GetCurrentDomain();
	BIND_ENTRY *ptr;
	const int is_enforce = CheckCCSEnforce(CCS_TOMOYO_MAC_FOR_BINDPORT);
	if (!CheckCCSFlags(CCS_TOMOYO_MAC_FOR_BINDPORT)) return 0;
	if (domain->attribute & DOMAIN_ATTRIBUTE_TRUSTED) {
		AuditPortLog(is_stream, port, 1);
		return 0;
	}
	for (ptr = localport_list.next; ptr; ptr = ptr->next) {
		if (ptr->domain == domain && ptr->is_stream == is_stream && ptr->min_port <= port && port <= ptr->max_port) break;
	}
	if (ptr) {
		AuditPortLog(is_stream, port, 1);
		return 0;
	}
	if (TomoyoVerboseMode()) {
		printk("TOMOYO-%s: Bind to %s port %d denied for %s\n", GetMSG(is_enforce), is_stream ? "TCP" : "UDP", port, GetLastName(domain));
	}
	AuditPortLog(is_stream, port, 0);
	if (is_enforce) return -EPERM;
	if (CheckCCSAccept(CCS_TOMOYO_MAC_FOR_BINDPORT)) AddBindEntry(is_stream, port, port, domain);
	return 0;
}

int AddBindPolicy(char *data, void **domain)
{
	unsigned int from, to;
	if (!isRoot()) return -EPERM;
	if (sscanf(data, "TCP/%u-%u", &from, &to) == 2) {
		if (from <= to && to < 65536) return AddBindEntry(1, from, to, (struct domain_info *) *domain);
	} else if (sscanf(data, "TCP/%u", &from) == 1) {
		if (from < 65536) return AddBindEntry(1, from, from, (struct domain_info *) *domain);
	} else if (sscanf(data, "UDP/%u-%u", &from, &to) == 2) {
		if (from <= to && to < 65536) return AddBindEntry(0, from, to, (struct domain_info *) *domain);
	} else if (sscanf(data, "UDP/%u", &from) == 1) {
		if (from < 65536) return AddBindEntry(0, from, from, (struct domain_info *) *domain);
	}
	printk("%s: ERROR: Invalid port range '%s'\n", __FUNCTION__, data);
	return -EINVAL;
}

int ReadBindPolicy(IO_BUFFER *head)
{
	struct domain_info *domain = (struct domain_info *) head->read_var1;
	BIND_ENTRY *ptr = (BIND_ENTRY *) head->read_var2;
	if (!ptr) ptr = localport_list.next;
	while (ptr) {
		head->read_var2 = (void *) ptr;
		if (domain == ptr->domain) {
			if (ptr->min_port != ptr->max_port) {
				if (io_printf(head, KEYWORD_ALLOW_BIND "%s/%d-%d\n", ptr->is_stream ? "TCP" : "UDP", ptr->min_port, ptr->max_port)) break;
			} else {
				if (io_printf(head, KEYWORD_ALLOW_BIND "%s/%d\n", ptr->is_stream ? "TCP" : "UDP", ptr->min_port)) break;
			}
		}
		ptr = ptr->next;
	}
	return ptr ? -ENOMEM : 0;
}

EXPORT_SYMBOL(CheckBindEntry);

/*************************  AUDIT FUNCTIONS  *************************/

static int AuditPortLog(const int is_stream, const unsigned short int port, const int is_granted)
{
	char *buf;
	const struct domain_info *domain = current->domain_info;
	int len;
	struct timeval tv;
	struct task_struct *task = current;
	if (!domain) {
		printk("%s: ERROR: domain == NULL\n", __FUNCTION__);
		return -EINVAL;
	}
	if (CanSaveAuditLog(is_granted) < 0) return -ENOMEM;
	do_gettimeofday(&tv);
	len = strlen(domain->domainname) + 256;
	if ((buf = kmalloc(len, GFP_KERNEL)) == NULL) return -ENOMEM;
	memset(buf, 0, len);
	snprintf(buf, len - 1, "#timestamp=%lu pid=%d uid=%d gid=%d euid=%d egid=%d suid=%d sgid=%d fsuid=%d fsgid=%d\n%s\n" KEYWORD_ALLOW_BIND "%s-%u\n", tv.tv_sec, task->pid, task->uid, task->gid, task->euid, task->egid, task->suid, task->sgid, task->fsuid, task->fsgid, domain->domainname, is_stream ? "TCP" : "UDP", port);
	return WriteAuditLog(buf, is_granted);
}

/***** TOMOYO Linux end. *****/