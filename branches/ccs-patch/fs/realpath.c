/*
 * fs/realpath.c
 *
 * Get the canonicalized absolute pathnames. The basis for SAKURA and TOMOYO.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 1.7.0-pre   2009/05/28
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/utime.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
#include <linux/namei.h>
#include <linux/mount.h>
static const int ccs_lookup_flags = LOOKUP_FOLLOW;
#else
static const int ccs_lookup_flags = LOOKUP_FOLLOW | LOOKUP_POSITIVE;
#endif
#include <linux/proc_fs.h>
#include <linux/ccs_common.h>
#include <linux/realpath.h>
#include <net/sock.h>

/**
 * ccs_get_absolute_path - Get the path of a dentry but ignores chroot'ed root.
 *
 * @dentry: Pointer to "struct dentry".
 * @vfsmnt: Pointer to "struct vfsmount".
 * @buffer: Pointer to buffer to return value in.
 * @buflen: Sizeof @buffer.
 *
 * Returns 0 on success, -ENOMEM otherwise.
 *
 * Caller holds the dcache_lock and vfsmount_lock.
 * Based on __d_path() in fs/dcache.c
 *
 * If dentry is a directory, trailing '/' is appended.
 * Characters out of 0x20 < c < 0x7F range are converted to
 * \ooo style octal string.
 * Character \ is converted to \\ string.
 */
static int ccs_get_absolute_path(struct dentry *dentry, struct vfsmount *vfsmnt,
				 char *buffer, int buflen)
{
	/***** CRITICAL SECTION START *****/
	char *start = buffer;
	char *end = buffer + buflen;
	bool is_dir = (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode));

	if (buflen < 256)
		goto out;

	*--end = '\0';
	buflen--;

	for (;;) {
		struct dentry *parent;

		if (dentry == vfsmnt->mnt_root || IS_ROOT(dentry)) {
			/* Global root? */
			if (vfsmnt->mnt_parent == vfsmnt)
				break;
			dentry = vfsmnt->mnt_mountpoint;
			vfsmnt = vfsmnt->mnt_parent;
			continue;
		}
		if (is_dir) {
			is_dir = false;
			*--end = '/';
			buflen--;
		}
		parent = dentry->d_parent;
		{
			const char *sp = dentry->d_name.name;
			const char *cp = sp + dentry->d_name.len - 1;
			unsigned char c;

			/*
			 * Exception: Use /proc/self/ rather than
			 * /proc/\$/ for current process.
			 */
			if (IS_ROOT(parent) && *sp > '0' && *sp <= '9' &&
			    parent->d_sb &&
			    parent->d_sb->s_magic == PROC_SUPER_MAGIC) {
				char *ep;
				const pid_t pid
					= (pid_t) simple_strtoul(sp, &ep, 10);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
				const pid_t tgid
					= task_tgid_nr_ns(current,
							  dentry->d_sb->
							  s_fs_info);
				if (!*ep && pid == tgid && tgid) {
					sp = "self";
					cp = sp + 3;
				}
#else
				if (!*ep && pid == sys_getpid()) {
					sp = "self";
					cp = sp + 3;
				}
#endif
			}

			while (sp <= cp) {
				c = *(unsigned char *) cp;
				if (c == '\\') {
					buflen -= 2;
					if (buflen < 0)
						goto out;
					*--end = '\\';
					*--end = '\\';
				} else if (c > ' ' && c < 127) {
					if (--buflen < 0)
						goto out;
					*--end = (char) c;
				} else {
					buflen -= 4;
					if (buflen < 0)
						goto out;
					*--end = (c & 7) + '0';
					*--end = ((c >> 3) & 7) + '0';
					*--end = (c >> 6) + '0';
					*--end = '\\';
				}
				cp--;
			}
			if (--buflen < 0)
				goto out;
			*--end = '/';
		}
		dentry = parent;
	}
	if (*end == '/') {
		buflen++;
		end++;
	}
	{
		const char *sp = dentry->d_name.name;
		const char *cp = sp + dentry->d_name.len - 1;
		unsigned char c;
		while (sp <= cp) {
			c = *(unsigned char *) cp;
			if (c == '\\') {
				buflen -= 2;
				if (buflen < 0)
					goto out;
				*--end = '\\';
				*--end = '\\';
			} else if (c > ' ' && c < 127) {
				if (--buflen < 0)
					goto out;
				*--end = (char) c;
			} else {
				buflen -= 4;
				if (buflen < 0)
					goto out;
				*--end = (c & 7) + '0';
				*--end = ((c >> 3) & 7) + '0';
				*--end = (c >> 6) + '0';
				*--end = '\\';
			}
			cp--;
		}
	}
	/* Move the pathname to the top of the buffer. */
	memmove(start, end, strlen(end) + 1);
	return 0;
 out:
	return -ENOMEM;
	/***** CRITICAL SECTION END *****/
}

#define SOCKFS_MAGIC 0x534F434B

/**
 * ccs_realpath_from_dentry2 - Returns realpath(3) of the given dentry but ignores chroot'ed root.
 *
 * @dentry:      Pointer to "struct dentry".
 * @mnt:         Pointer to "struct vfsmount".
 * @newname:     Pointer to buffer to return value in.
 * @newname_len: Size of @newname.
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_realpath_from_dentry2(struct dentry *dentry, struct vfsmount *mnt,
			      char *newname, int newname_len)
{
	int error = -EINVAL;
	struct dentry *d_dentry;
	struct vfsmount *d_mnt;
	if (!dentry || !newname || newname_len <= 2048)
		goto out;
	/* Get better name for socket. */
	if (dentry->d_sb && dentry->d_sb->s_magic == SOCKFS_MAGIC) {
		struct inode *inode = dentry->d_inode;
		struct socket *sock = inode ? SOCKET_I(inode) : NULL;
		struct sock *sk = sock ? sock->sk : NULL;
		if (sk) {
			snprintf(newname, newname_len - 1,
				 "socket:[family=%u:type=%u:protocol=%u]",
				 sk->sk_family, sk->sk_type, sk->sk_protocol);
		} else {
			snprintf(newname, newname_len - 1, "socket:[unknown]");
		}
		return 0;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
	if (dentry->d_op && dentry->d_op->d_dname) {
		/* For "socket:[\$]" and "pipe:[\$]". */
		static const int offset = 1536;
		char *dp = newname;
		char *sp = dentry->d_op->d_dname(dentry, newname + offset,
						 newname_len - offset);
		if (IS_ERR(sp)) {
			error = PTR_ERR(sp);
			goto out;
		}
		error = -ENOMEM;
		newname += offset;
		while (1) {
			const unsigned char c = *(unsigned char *) sp++;
			if (c == '\\') {
				if (dp + 2 >= newname)
					break;
				*dp++ = '\\';
				*dp++ = '\\';
			} else if (c > ' ' && c < 127) {
				if (dp + 1 >= newname)
					break;
				*dp++ = (char) c;
			} else if (c) {
				if (dp + 4 >= newname)
					break;
				*dp++ = '\\';
				*dp++ = (c >> 6) + '0';
				*dp++ = ((c >> 3) & 7) + '0';
				*dp++ = (c & 7) + '0';
			} else {
				*dp = '\0';
				return 0;
			}
		}
		goto out;
	}
#endif
	if (!mnt)
		goto out;
	d_dentry = dget(dentry);
	d_mnt = mntget(mnt);
	/***** CRITICAL SECTION START *****/
	ccs_realpath_lock();
	error = ccs_get_absolute_path(d_dentry, d_mnt, newname, newname_len);
	ccs_realpath_unlock();
	/***** CRITICAL SECTION END *****/
	dput(d_dentry);
	mntput(d_mnt);
 out:
	if (error)
		printk(KERN_WARNING "ccs_realpath: Pathname too long. (%d)\n",
		       error);
	return error;
}

/**
 * ccs_realpath_from_dentry - Returns realpath(3) of the given pathname but ignores chroot'ed root.
 *
 * @dentry: Pointer to "struct dentry".
 * @mnt:    Pointer to "struct vfsmount".
 *
 * Returns the realpath of the given @dentry and @mnt on success,
 * NULL otherwise.
 *
 * These functions use ccs_alloc(), so caller must ccs_free()
 * if these functions didn't return NULL.
 */
char *ccs_realpath_from_dentry(struct dentry *dentry, struct vfsmount *mnt)
{
	char *buf = ccs_alloc(CCS_MAX_PATHNAME_LEN, false);
	if (buf && ccs_realpath_from_dentry2(dentry, mnt, buf,
					     CCS_MAX_PATHNAME_LEN - 1) == 0)
		return buf;
	ccs_free(buf);
	return NULL;
}

/**
 * ccs_realpath - Get realpath of a pathname.
 *
 * @pathname: The pathname to solve.
 *
 * Returns the realpath of @pathname on success, NULL otherwise.
 */
char *ccs_realpath(const char *pathname)
{
	struct nameidata nd;
	if (pathname && path_lookup(pathname, ccs_lookup_flags, &nd) == 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
		char *buf = ccs_realpath_from_dentry(nd.path.dentry,
						     nd.path.mnt);
		path_put(&nd.path);
#else
		char *buf = ccs_realpath_from_dentry(nd.dentry, nd.mnt);
		path_release(&nd);
#endif
		return buf;
	}
	return NULL;
}

/**
 * ccs_realpath_both - Get realpath of a pathname and symlink.
 *
 * @pathname: The pathname to solve.
 * @ee:       Pointer to "struct ccs_execve_entry".
 *
 * Returns 0 on success, negative value otherwise.
 */
int ccs_realpath_both(const char *pathname, struct ccs_execve_entry *ee)
{
	struct nameidata nd;
	int ret;
	bool is_symlink;
	if (!pathname ||
	    path_lookup(pathname, ccs_lookup_flags ^ LOOKUP_FOLLOW, &nd))
		return -ENOENT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	is_symlink = nd.path.dentry->d_inode &&
		S_ISLNK(nd.path.dentry->d_inode->i_mode);
	ret = ccs_realpath_from_dentry2(nd.path.dentry, nd.path.mnt,
					ee->tmp, CCS_EXEC_TMPSIZE - 1);
	path_put(&nd.path);
#else
	is_symlink = nd.dentry->d_inode && S_ISLNK(nd.dentry->d_inode->i_mode);
	ret = ccs_realpath_from_dentry2(nd.dentry, nd.mnt, ee->tmp,
					CCS_EXEC_TMPSIZE - 1);
	path_release(&nd);
#endif
	if (ret)
		return -ENOMEM;
	if (strlen(ee->tmp) > CCS_MAX_PATHNAME_LEN - 1)
		return -ENOMEM;
	ee->program_path[CCS_MAX_PATHNAME_LEN - 1] = '\0';
	if (!is_symlink) {
		strncpy(ee->program_path, ee->tmp,
			CCS_MAX_PATHNAME_LEN - 1);
		return 0;
	}
	if (path_lookup(pathname, ccs_lookup_flags, &nd))
		return -ENOENT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	ret = ccs_realpath_from_dentry2(nd.path.dentry, nd.path.mnt,
					ee->program_path,
					CCS_MAX_PATHNAME_LEN - 1);
	path_put(&nd.path);
#else
	ret = ccs_realpath_from_dentry2(nd.dentry, nd.mnt, ee->program_path,
					CCS_MAX_PATHNAME_LEN - 1);
	path_release(&nd);
#endif
	return ret ? -ENOMEM : 0;
}

/**
 * ccs_encode: Encode binary string to ascii string.
 *
 * @str: String in binary format.
 *
 * Returns pointer to @str in ascii format on success, NULL otherwise.
 *
 * This function uses ccs_alloc(), so caller must ccs_free() if this function
 * didn't return NULL.
 */
char *ccs_encode(const char *str)
{
	int len = 0;
	const char *p = str;
	char *cp;
	char *cp0;
	if (!p)
		return NULL;
	while (*p) {
		const unsigned char c = *p++;
		if (c == '\\')
			len += 2;
		else if (c > ' ' && c < 127)
			len++;
		else
			len += 4;
	}
	len++;
	cp = ccs_alloc(len, false);
	if (!cp)
		return NULL;
	cp0 = cp;
	p = str;
	while (*p) {
		const unsigned char c = *p++;
		if (c == '\\') {
			*cp++ = '\\';
			*cp++ = '\\';
		} else if (c > ' ' && c < 127) {
			*cp++ = c;
		} else {
			*cp++ = '\\';
			*cp++ = (c >> 6) + '0';
			*cp++ = ((c >> 3) & 7) + '0';
			*cp++ = (c & 7) + '0';
		}
	}
	return cp0;
}

/**
 * ccs_round_up - Round up an integer so that the returned pointers are appropriately aligned.
 *
 * @size: Size in bytes.
 *
 * Returns rounded value of @size.
 *
 * FIXME: Are there more requirements that is needed for assigning value
 * atomically?
 */
static inline unsigned int ccs_round_up(const unsigned int size)
{
	if (sizeof(void *) >= sizeof(long))
		return ((size + sizeof(void *) - 1)
			/ sizeof(void *)) * sizeof(void *);
	else
		return ((size + sizeof(long) - 1)
			/ sizeof(long)) * sizeof(long);
}

static atomic_t ccs_allocated_memory_for_elements;
static unsigned int ccs_quota_for_elements;

/**
 * ccs_memory_ok - Check memory quota.
 *
 * @ptr: Pointer to allocated memory.
 *
 * Returns true if @ptr is not NULL and quota not exceeded, false otehrwise.
 */
bool ccs_memory_ok(const void *ptr)
{
	const unsigned int len = ptr ? ksize(ptr) : 0;
	if (len && (!ccs_quota_for_elements ||
		    atomic_read(&ccs_allocated_memory_for_elements) + len
		    <= ccs_quota_for_elements)) {
		atomic_add(len, &ccs_allocated_memory_for_elements);
		return true;
	}
	printk(KERN_WARNING "ERROR: Out of memory. (%s)\n", __func__);
	if (!ccs_policy_loaded)
		panic("MAC Initialization failed.\n");
	return false;
}

/**
 * ccs_memory_free - Free memory for elements.
 *
 * @ptr: Pointer to allocated memory.
 */
static void ccs_memory_free(const void *ptr)
{
	atomic_sub(ksize(ptr), &ccs_allocated_memory_for_elements);
	kfree(ptr);
}

/**
 * ccs_free_element - Delete memory for structures.
 *
 * @ptr: Memory to release.
 */
static void ccs_free_element(void *ptr)
{
	if (!ptr)
		return;
	atomic_sub(ksize(ptr), &ccs_allocated_memory_for_elements);
	kfree(ptr);
}

/**
 * ccs_put_path_group - Delete memory for "struct ccs_path_group_entry".
 *
 * @group: Pointer to "struct ccs_path_group_entry".
 */
void ccs_put_path_group(struct ccs_path_group_entry *group)
{
	struct ccs_path_group_member *member;
	struct ccs_path_group_member *next_member;
	LIST_HEAD(q);
	bool can_delete_group = false;
	if (!group)
		return;
	mutex_lock(&ccs_policy_lock);
	if (atomic_dec_and_test(&group->users)) {
		list_for_each_entry_safe(member, next_member,
					 &group->path_group_member_list,
					 list) {
			if (!member->is_deleted)
				break;
			list_del(&member->list);
			list_add(&member->list, &q);
		}
		if (list_empty(&group->path_group_member_list)) {
			list_del(&group->list);
			can_delete_group = true;
		}
	}
	mutex_unlock(&ccs_policy_lock);
	list_for_each_entry_safe(member, next_member, &q, list) {
		list_del(&member->list);
		ccs_put_name(member->member_name);
		ccs_free_element(member);
	}
	if (can_delete_group) {
		ccs_put_name(group->group_name);
		ccs_free_element(group);
	}
}

/**
 * ccs_put_address_group - Delete memory for "struct ccs_address_group_entry".
 *
 * @group: Pointer to "struct ccs_address_group_entry".
 */
void ccs_put_address_group(struct ccs_address_group_entry *group)
{
	struct ccs_address_group_member *member;
	struct ccs_address_group_member *next_member;
	LIST_HEAD(q);
	bool can_delete_group = false;
	if (!group)
		return;
	mutex_lock(&ccs_policy_lock);
	if (atomic_dec_and_test(&group->users)) {
		list_for_each_entry_safe(member, next_member,
					 &group->address_group_member_list,
					 list) {
			if (!member->is_deleted)
				break;
			list_del(&member->list);
			list_add(&member->list, &q);
		}
		if (list_empty(&group->address_group_member_list)) {
			list_del(&group->list);
			can_delete_group = true;
		}
	}
	mutex_unlock(&ccs_policy_lock);
	list_for_each_entry_safe(member, next_member, &q, list) {
		list_del(&member->list);
		if (member->is_ipv6) {
			ccs_put_ipv6_address(member->min.ipv6);
			ccs_put_ipv6_address(member->max.ipv6);
		}
		ccs_free_element(member);
	}
	if (can_delete_group) {
		ccs_put_name(group->group_name);
		ccs_free_element(group);
	}
}

static LIST_HEAD(ccs_address_list);

/**
 * ccs_get_ipv6_address - Keep the given IPv6 address on the RAM.
 *
 * @addr: Pointer to "struct in6_addr".
 *
 * Returns pointer to "struct in6_addr" on success, NULL otherwise.
 *
 * The RAM is shared, so NEVER try to modify or kfree() the returned address.
 */
const struct in6_addr *ccs_get_ipv6_address(const struct in6_addr *addr)
{
	struct ccs_ipv6addr_entry *entry;
	struct ccs_ipv6addr_entry *ptr;
	int error = -ENOMEM;
	if (!addr)
		return NULL;
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	mutex_lock(&ccs_policy_lock);
	list_for_each_entry(ptr, &ccs_address_list, list) {
		if (memcmp(&ptr->addr, addr, sizeof(*addr)))
			continue;
		atomic_inc(&ptr->users);
		error = 0;
		break;
	}
	if (error && ccs_memory_ok(entry)) {
		ptr = entry;
		ptr->addr = *addr;
		atomic_set(&ptr->users, 1);
		list_add_tail(&ptr->list, &ccs_address_list);
		entry = NULL;
	}
	mutex_unlock(&ccs_policy_lock);
	kfree(entry);
	return ptr ? &ptr->addr : NULL;
}

/**
 * ccs_put_ipv6_address - Delete the given IPv6 address on the RAM.
 *
 * @addr: Pointer to "struct in6_addr".
 */
void ccs_put_ipv6_address(const struct in6_addr *addr)
{
	struct ccs_ipv6addr_entry *ptr;
	bool can_delete = false;
	if (!addr)
		return;
	ptr = container_of(addr, struct ccs_ipv6addr_entry, addr);
	mutex_lock(&ccs_policy_lock);
	if (atomic_dec_and_test(&ptr->users)) {
		list_del(&ptr->list);
		can_delete = true;
	}
	mutex_unlock(&ccs_policy_lock);
	if (can_delete)
		ccs_free_element(ptr);
}

/**
 * ccs_put_condition - Delete memory for "struct ccs_condition".
 *
 * @cond: Pointer to "struct ccs_condition".
 */
void ccs_put_condition(struct ccs_condition *cond)
{
	const unsigned long *ptr;
	const struct ccs_argv_entry *argv;
	const struct ccs_envp_entry *envp;
	const struct ccs_symlinkp_entry *symlinkp;
	u16 condc;
	u16 argc;
	u16 envc;
	u16 symlinkc;
	u16 i;
	bool can_delete = false;
	if (!cond)
		return;
	mutex_lock(&ccs_policy_lock);
	if (atomic_dec_and_test(&cond->users)) {
		list_del(&cond->list);
		can_delete = true;
	}
	mutex_unlock(&ccs_policy_lock);
	if (!can_delete)
		return;
	condc = cond->condc;
	argc = cond->argc;
	envc = cond->envc;
	symlinkc = cond->symlinkc;
	ptr = (const unsigned long *) (cond + 1);
	argv = (const struct ccs_argv_entry *) (ptr + condc);
	envp = (const struct ccs_envp_entry *) (argv + argc);
	symlinkp = (const struct ccs_symlinkp_entry *) (envp + envc);
	for (i = 0; i < argc; argv++, i++)
		ccs_put_name(argv->value);
	for (i = 0; i < envc; envp++, i++) {
		ccs_put_name(envp->name);
		ccs_put_name(envp->value);
	}
	for (i = 0; i < symlinkc; symlinkp++, i++)
		ccs_put_name(symlinkp->value);
	ccs_free_element(cond);
}

static atomic_t ccs_allocated_memory_for_savename;
static unsigned int ccs_quota_for_savename;

#define MAX_HASH 256

/* Structure for string data. */
struct ccs_name_entry {
	struct list_head list;
	atomic_t users;
	struct ccs_path_info entry;
};

/* The list for "struct ccs_name_entry". */
static struct list_head ccs_name_list[MAX_HASH];
static DEFINE_MUTEX(ccs_name_list_lock);

/**
 * ccs_get_name - Allocate memory for string data.
 *
 * @name: The string to store into the permernent memory.
 *
 * Returns pointer to "struct ccs_path_info" on success, NULL otherwise.
 */
const struct ccs_path_info *ccs_get_name(const char *name)
{
	struct ccs_name_entry *ptr;
	unsigned int hash;
	int len;
	int allocated_len;

	if (!name)
		return NULL;
	len = strlen(name) + 1;
	if (len > CCS_MAX_PATHNAME_LEN) {
		printk(KERN_WARNING "ERROR: Name too long. (%s)\n", __func__);
		return NULL;
	}
	hash = full_name_hash((const unsigned char *) name, len - 1);
	/***** EXCLUSIVE SECTION START *****/
	mutex_lock(&ccs_name_list_lock);
	list_for_each_entry(ptr, &ccs_name_list[hash % MAX_HASH], list) {
		if (hash != ptr->entry.hash || strcmp(name, ptr->entry.name))
			continue;
		atomic_inc(&ptr->users);
		goto out;
	}
	ptr = kmalloc(sizeof(*ptr) + len, GFP_KERNEL);
	allocated_len = ptr ? ksize(ptr) : 0;
	if (!allocated_len ||
	    (ccs_quota_for_savename &&
	     atomic_read(&ccs_allocated_memory_for_savename) + allocated_len
	     > ccs_quota_for_savename)) {
		kfree(ptr);
		ptr = NULL;
		printk(KERN_WARNING "ERROR: Out of memory. (%s)\n", __func__);
		if (!ccs_policy_loaded)
			panic("MAC Initialization failed.\n");
		goto out;
	}
	atomic_add(allocated_len, &ccs_allocated_memory_for_savename);
	ptr->entry.name = ((char *) ptr) + sizeof(*ptr);
	memmove((char *) ptr->entry.name, name, len);
	atomic_set(&ptr->users, 1);
	ccs_fill_path_info(&ptr->entry);
	list_add_tail(&ptr->list, &ccs_name_list[hash % MAX_HASH]);
 out:
	mutex_unlock(&ccs_name_list_lock);
	/***** EXCLUSIVE SECTION END *****/
	return ptr ? &ptr->entry : NULL;
}

/**
 * ccs_put_name - Delete shared memory for string data.
 *
 * @name: Pointer to "struct ccs_path_info".
 */
void ccs_put_name(const struct ccs_path_info *name)
{
	struct ccs_name_entry *ptr;
	bool can_delete = false;
	if (!name)
		return;
	ptr = container_of(name, struct ccs_name_entry, entry);
	/***** EXCLUSIVE SECTION START *****/
	mutex_lock(&ccs_name_list_lock);
	if (atomic_dec_and_test(&ptr->users)) {
		list_del(&ptr->list);
		can_delete = true;
	}
	mutex_unlock(&ccs_name_list_lock);
	/***** EXCLUSIVE SECTION END *****/
	if (can_delete) {
		atomic_sub(ksize(ptr), &ccs_allocated_memory_for_savename);
		kfree(ptr);
	}
}

/* Structure for temporarily allocated memory. */
struct ccs_cache_entry {
	struct list_head list;
	void *ptr;
	int size;
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
static struct kmem_cache *ccs_cachep;
#else
static kmem_cache_t *ccs_cachep;
#endif

struct srcu_struct ccs_ss;

/**
 * ccs_realpath_init - Initialize realpath related code.
 *
 * Returns 0.
 */
static int __init ccs_realpath_init(void)
{
	int i;
	/* Constraint for ccs_get_name(). */
	if (CCS_MAX_PATHNAME_LEN > PAGE_SIZE)
		panic("Bad size.");
	/* Constraint for "struct ccs_execve_entry"->tmp users. */
	if (CCS_MAX_PATHNAME_LEN > CCS_EXEC_TMPSIZE)
		panic("Bad size.");
	if (init_srcu_struct(&ccs_ss))
		panic("Out of memory.");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
	ccs_cachep = kmem_cache_create("ccs_cache",
				       sizeof(struct ccs_cache_entry),
				       0, 0, NULL);
#else
	ccs_cachep = kmem_cache_create("ccs_cache",
				       sizeof(struct ccs_cache_entry),
				       0, 0, NULL, NULL);
#endif
	if (!ccs_cachep)
		panic("Can't create cache.\n");
	for (i = 0; i < MAX_HASH; i++)
		INIT_LIST_HEAD(&ccs_name_list[i]);
	INIT_LIST_HEAD(&ccs_kernel_domain.acl_info_list);
	ccs_kernel_domain.domainname = ccs_get_name(ROOT_NAME);
	list_add_tail_rcu(&ccs_kernel_domain.list, &ccs_domain_list);
	if (ccs_find_domain(ROOT_NAME) != &ccs_kernel_domain)
		panic("Can't register ccs_kernel_domain");
#ifdef CONFIG_CCSECURITY_BUILTIN_INITIALIZERS
	{
		/* Load built-in policy. */
		static char ccs_builtin_initializers[] __initdata
			= CONFIG_CCSECURITY_BUILTIN_INITIALIZERS;
		char *cp = ccs_builtin_initializers;
		ccs_normalize_line(cp);
		while (cp && *cp) {
			char *cp2 = strchr(cp, ' ');
			if (cp2)
				*cp2++ = '\0';
			ccs_write_domain_initializer_policy(cp, false, false);
			cp = cp2;
		}
	}
#endif
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
__initcall(ccs_realpath_init);
#else
core_initcall(ccs_realpath_init);
#endif

/* The list for "struct ccs_cache_entry". */
static LIST_HEAD(ccs_audit_cache_list);
static LIST_HEAD(ccs_acl_cache_list);
static DEFINE_SPINLOCK(ccs_cache_list_lock);

static unsigned int ccs_dynamic_memory_size;
static unsigned int ccs_quota_for_dynamic;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
/**
 * ccs_round2 - Rounded up to power-of-two value.
 *
 * @size: Size in bytes.
 *
 * Returns power-of-two of @size.
 */
static int ccs_round2(size_t size)
{
#if PAGE_SIZE == 4096
	size_t bsize = 32;
#else
	size_t bsize = 64;
#endif
	while (size > bsize)
		bsize <<= 1;
	return bsize;
}
#endif

/**
 * ccs_alloc - Allocate memory for temporary purpose.
 *
 * @size: Size in bytes.
 *
 * Returns pointer to allocated memory on success, NULL otherwise.
 */
void *ccs_alloc(const size_t size, const _Bool check_quota)
{
	struct ccs_cache_entry *new_entry;
	void *ret = kzalloc(size, GFP_KERNEL);
	if (!ret)
		goto out;
	new_entry = kmem_cache_alloc(ccs_cachep, GFP_KERNEL);
	if (!new_entry) {
		kfree(ret);
		ret = NULL;
		goto out;
	}
	INIT_LIST_HEAD(&new_entry->list);
	new_entry->ptr = ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
	new_entry->size = ksize(ret);
#else
	new_entry->size = ccs_round2(size);
#endif
	if (check_quota) {
		bool quota_exceeded = false;
		/***** CRITICAL SECTION START *****/
		spin_lock(&ccs_cache_list_lock);
		if (!ccs_quota_for_dynamic ||
		    ccs_dynamic_memory_size + new_entry->size
		    <= ccs_quota_for_dynamic) {
			list_add_tail(&new_entry->list, &ccs_audit_cache_list);
			ccs_dynamic_memory_size += new_entry->size;
		} else {
			quota_exceeded = true;
		}
		spin_unlock(&ccs_cache_list_lock);
		/***** CRITICAL SECTION END *****/
		if (quota_exceeded) {
			kfree(ret);
			kmem_cache_free(ccs_cachep, new_entry);
			ret = NULL;
		}
	} else {
		/***** CRITICAL SECTION START *****/
		spin_lock(&ccs_cache_list_lock);
		list_add(&new_entry->list, &ccs_acl_cache_list);
		ccs_dynamic_memory_size += new_entry->size;
		spin_unlock(&ccs_cache_list_lock);
		/***** CRITICAL SECTION END *****/
	}
 out:
	return ret;
}

/**
 * ccs_free - Release memory allocated by ccs_alloc().
 *
 * @p: Pointer returned by ccs_alloc(). May be NULL.
 *
 * Returns nothing.
 */
void ccs_free(const void *p)
{
	struct list_head *v;
	struct ccs_cache_entry *entry = NULL;
	if (!p)
		return;
	/***** CRITICAL SECTION START *****/
	spin_lock(&ccs_cache_list_lock);
	list_for_each(v, &ccs_acl_cache_list) {
		entry = list_entry(v, struct ccs_cache_entry, list);
		if (entry->ptr == p)
			break;
		entry = NULL;
	}
	if (!entry) {
		list_for_each(v, &ccs_audit_cache_list) {
			entry = list_entry(v, struct ccs_cache_entry, list);
			if (entry->ptr == p)
				break;
			entry = NULL;
		}
	}
	if (entry) {
		list_del(&entry->list);
		ccs_dynamic_memory_size -= entry->size;
	}
	spin_unlock(&ccs_cache_list_lock);
	/***** CRITICAL SECTION END *****/
	if (entry) {
		kfree(p);
		kmem_cache_free(ccs_cachep, entry);
	} else {
		printk(KERN_WARNING "BUG: ccs_free() with invalid pointer.\n");
	}
}

/**
 * ccs_read_memory_counter - Check for memory usage.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns memory usage.
 */
int ccs_read_memory_counter(struct ccs_io_buffer *head)
{
	if (!head->read_eof) {
		const unsigned int shared
			= atomic_read(&ccs_allocated_memory_for_savename);
		const unsigned int private
			= atomic_read(&ccs_allocated_memory_for_elements);
		const unsigned int dynamic = ccs_dynamic_memory_size;
		char buffer[64];
		memset(buffer, 0, sizeof(buffer));
		if (ccs_quota_for_savename)
			snprintf(buffer, sizeof(buffer) - 1,
				 "   (Quota: %10u)", ccs_quota_for_savename);
		else
			buffer[0] = '\0';
		ccs_io_printf(head, "Shared:  %10u%s\n", shared, buffer);
		if (ccs_quota_for_elements)
			snprintf(buffer, sizeof(buffer) - 1,
				 "   (Quota: %10u)", ccs_quota_for_elements);
		else
			buffer[0] = '\0';
		ccs_io_printf(head, "Private: %10u%s\n", private, buffer);
		if (ccs_quota_for_dynamic)
			snprintf(buffer, sizeof(buffer) - 1,
				 "   (Quota: %10u)", ccs_quota_for_dynamic);
		else
			buffer[0] = '\0';
		ccs_io_printf(head, "Dynamic: %10u%s\n", dynamic, buffer);
		ccs_io_printf(head, "Total:   %10u\n",
			      shared + private + dynamic);
		head->read_eof = true;
	}
	return 0;
}

/**
 * ccs_write_memory_quota - Set memory quota.
 *
 * @head: Pointer to "struct ccs_io_buffer".
 *
 * Returns 0.
 */
int ccs_write_memory_quota(struct ccs_io_buffer *head)
{
	char *data = head->write_buf;
	unsigned int size;
	if (sscanf(data, "Shared: %u", &size) == 1)
		ccs_quota_for_savename = size;
	else if (sscanf(data, "Private: %u", &size) == 1)
		ccs_quota_for_elements = size;
	else if (sscanf(data, "Dynamic: %u", &size) == 1)
		ccs_quota_for_dynamic = size;
	return 0;
}

/* Garbage collector functions */

enum ccs_gc_id {
	CCS_ID_CONDITION,
	CCS_ID_RESERVEDPORT,
	CCS_ID_ADDRESS_GROUP,
	CCS_ID_ADDRESS_GROUP_MEMBER,
	CCS_ID_PATH_GROUP,
	CCS_ID_PATH_GROUP_MEMBER,
	CCS_ID_GLOBAL_ENV,
	CCS_ID_AGGREGATOR,
	CCS_ID_DOMAIN_INITIALIZER,
	CCS_ID_DOMAIN_KEEPER,
	CCS_ID_ALIAS,
	CCS_ID_GLOBALLY_READABLE,
	CCS_ID_PATTERN,
	CCS_ID_NO_REWRITE,
	CCS_ID_MANAGER,
	CCS_ID_ACL,
	CCS_ID_DOMAIN
};

struct ccs_gc_entry {
	struct list_head list;
	int type;
	void *element;
};

/* Caller holds ccs_policy_lock mutex. */
static bool ccs_add_to_gc(const int type, void *element, struct list_head *head)
{
	struct ccs_gc_entry *entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return false;
	entry->type = type;
	entry->element = element;
	list_add(&entry->list, head);
	return true;
}

static inline void ccs_gc_del_domain_initializer
(struct ccs_domain_initializer_entry *ptr)
{
	ccs_put_name(ptr->domainname);
	ccs_put_name(ptr->program);
}

static inline void ccs_gc_del_domain_keeper
(struct ccs_domain_keeper_entry *ptr)
{
	ccs_put_name(ptr->domainname);
	ccs_put_name(ptr->program);
}

static void ccs_del_allow_read(struct ccs_globally_readable_file_entry *ptr)
{
	ccs_put_name(ptr->filename);
	ccs_memory_free(ptr);
}

static void ccs_del_allow_env(struct ccs_globally_usable_env_entry *ptr)
{
	ccs_put_name(ptr->env);
	ccs_memory_free(ptr);
}

static void ccs_del_file_pattern(struct ccs_pattern_entry *ptr)
{
	ccs_put_name(ptr->pattern);
	ccs_memory_free(ptr);
}

static void ccs_del_no_rewrite(struct ccs_no_rewrite_entry *ptr)
{
	ccs_put_name(ptr->pattern);
	ccs_memory_free(ptr);
}

static void ccs_del_domain_initializer(struct ccs_domain_initializer_entry *ptr)
{
	ccs_put_name(ptr->domainname);
	ccs_put_name(ptr->program);
	ccs_memory_free(ptr);
}

static void ccs_del_domain_keeper(struct ccs_domain_keeper_entry *ptr)
{
	ccs_put_name(ptr->domainname);
	ccs_put_name(ptr->program);
	ccs_memory_free(ptr);
}

static void ccs_del_alias(struct ccs_alias_entry *ptr)
{
	ccs_put_name(ptr->original_name);
	ccs_put_name(ptr->aliased_name);
	ccs_memory_free(ptr);
}

static void ccs_del_aggregator(struct ccs_aggregator_entry *ptr)
{
	ccs_put_name(ptr->original_name);
	ccs_put_name(ptr->aggregated_name);
	ccs_memory_free(ptr);
}

static void ccs_del_manager(struct ccs_policy_manager_entry *ptr)
{
	ccs_put_name(ptr->manager);
	ccs_memory_free(ptr);
}

/* For compatibility with older kernels. */
#ifndef for_each_process
#define for_each_process for_each_task
#endif

/**
 * ccs_used_by_task - Check whether the given pointer is referenced by a task.
 *
 * @domain: Pointer to "struct ccs_domain_info".
 *
 * Returns true if @ptr is in use, false otherwise.
 */
static bool ccs_used_by_task(struct ccs_domain_info *domain)
{
	bool in_use = false;
	struct task_struct *p;
	/***** CRITICAL SECTION START *****/
	read_lock(&tasklist_lock);
	for_each_process(p) {
		if (p->ccs_domain_info != domain)
			continue;
		in_use = true;
		break;
	}
	read_unlock(&tasklist_lock);
	/***** CRITICAL SECTION END *****/
	return in_use;
}

static void ccs_del_acl(struct ccs_acl_info *acl)
{
	struct ccs_single_path_acl_record *acl1;
	struct ccs_double_path_acl_record *acl2;
	struct ccs_ip_network_acl_record *acl3;
	struct ccs_ioctl_acl_record *acl4;
	struct ccs_argv0_acl_record *acl5;
	struct ccs_env_acl_record *acl6;
	struct ccs_capability_acl_record *acl7;
	struct ccs_signal_acl_record *acl8;
	struct ccs_execute_handler_record *acl9;
	struct ccs_mount_acl_record *acl10;
	struct ccs_umount_acl_record *acl11;
	struct ccs_chroot_acl_record *acl12;
	struct ccs_pivot_root_acl_record *acl13;
	ccs_put_condition(acl->cond);
	switch (ccs_acl_type1(acl)) {
	case TYPE_SINGLE_PATH_ACL:
		acl1 = container_of(acl, struct ccs_single_path_acl_record,
				    head);
		if (acl1->u_is_group)
			ccs_put_path_group(acl1->u.group);
		else
			ccs_put_name(acl1->u.filename);
		break;
	case TYPE_DOUBLE_PATH_ACL:
		acl2 = container_of(acl, struct ccs_double_path_acl_record,
				    head);
		if (acl2->u1_is_group)
			ccs_put_path_group(acl2->u1.group1);
		else
			ccs_put_name(acl2->u1.filename1);
		if (acl2->u2_is_group)
			ccs_put_path_group(acl2->u2.group2);
		else
			ccs_put_name(acl2->u2.filename2);
		break;
	case TYPE_IP_NETWORK_ACL:
		acl3 = container_of(acl, struct ccs_ip_network_acl_record,
				    head);
		if (acl3->record_type == IP_RECORD_TYPE_ADDRESS_GROUP)
			ccs_put_address_group(acl3->u.group);
		else if (acl3->record_type == IP_RECORD_TYPE_IPv6) {
			ccs_put_ipv6_address(acl3->u.ipv6.min);
			ccs_put_ipv6_address(acl3->u.ipv6.max);
		}
		break;
	case TYPE_IOCTL_ACL:
		acl4 = container_of(acl, struct ccs_ioctl_acl_record, head);
		if (acl4->u_is_group)
			ccs_put_path_group(acl4->u.group);
		else
			ccs_put_name(acl4->u.filename);
		break;
	case TYPE_ARGV0_ACL:
		acl5 = container_of(acl, struct ccs_argv0_acl_record, head);
		ccs_put_name(acl5->argv0);
		break;
	case TYPE_ENV_ACL:
		acl6 = container_of(acl, struct ccs_env_acl_record, head);
		ccs_put_name(acl6->env);
		break;
	case TYPE_CAPABILITY_ACL:
		acl7 = container_of(acl, struct ccs_capability_acl_record,
				    head);
		break;
	case TYPE_SIGNAL_ACL:
		acl8 = container_of(acl, struct ccs_signal_acl_record,
				    head);
		ccs_put_name(acl8->domainname);
		break;
		case TYPE_EXECUTE_HANDLER:
	case TYPE_DENIED_EXECUTE_HANDLER:
		acl9 = container_of(acl, struct ccs_execute_handler_record,
				    head);
		ccs_put_name(acl9->handler);
		break;
	case TYPE_MOUNT_ACL:
		acl10 = container_of(acl, struct ccs_mount_acl_record, head);
		ccs_put_name(acl10->dev_name);
		ccs_put_name(acl10->dir_name);
		ccs_put_name(acl10->fs_type);
		break;
	case TYPE_UMOUNT_ACL:
		acl11 = container_of(acl, struct ccs_umount_acl_record, head);
		ccs_put_name(acl11->dir);
		break;
	case TYPE_CHROOT_ACL:
		acl12 = container_of(acl, struct ccs_chroot_acl_record, head);
		ccs_put_name(acl12->dir);
		break;
	case TYPE_PIVOT_ROOT_ACL:
		acl13 = container_of(acl, struct ccs_pivot_root_acl_record,
				     head);
		ccs_put_name(acl13->old_root);
		ccs_put_name(acl13->new_root);
		break;

	}
	ccs_memory_free(acl);
}

static bool ccs_del_domain(struct ccs_domain_info *domain)
{
	if (ccs_used_by_task(domain))
		return false;
	ccs_put_name(domain->domainname);
	ccs_memory_free(domain);
	return true;
}

static void ccs_del_path_group_member(struct ccs_path_group_member *member)
{
	ccs_put_name(member->member_name);
	ccs_free_element(member);
}

static void ccs_del_path_group(struct ccs_path_group_entry *group)
{
	ccs_put_name(group->group_name);
	ccs_free_element(group);
}

static void ccs_del_address_group_member
(struct ccs_address_group_member *member)
{
	if (member->is_ipv6) {
		ccs_put_ipv6_address(member->min.ipv6);
		ccs_put_ipv6_address(member->max.ipv6);
	}
	ccs_free_element(member);
}

static void ccs_del_address_group(struct ccs_address_group_entry *group)
{
	ccs_put_name(group->group_name);
	ccs_free_element(group);
}

static void ccs_del_reservedport(struct ccs_reserved_entry *ptr)
{
	ccs_memory_free(ptr);
}

static void ccs_del_condition(struct ccs_condition *ptr)
{
	int i;
	u16 condc = ptr->condc;
	u16 argc = ptr->argc;
	u16 envc = ptr->envc;
	u16 symlinkc = ptr->symlinkc;
	unsigned long *ptr2 = (unsigned long *) (ptr + 1);
	struct ccs_argv_entry *argv = (struct ccs_argv_entry *) (ptr2 + condc);
	struct ccs_envp_entry *envp = (struct ccs_envp_entry *) (argv + argc);
	struct ccs_symlinkp_entry *symlinkp
		= (struct ccs_symlinkp_entry *) (envp + envc);
	for (i = 0; i < argc; i++)
		ccs_put_name(argv[i].value);
	for (i = 0; i < envc; i++) {
		ccs_put_name(envp[i].name);
		ccs_put_name(envp[i].value);
	}
	for (i = 0; i < symlinkc; i++)
		ccs_put_name(symlinkp[i].value);
	ccs_memory_free(ptr);
}

static int ccs_gc_thread(void *unused)
{
	static DEFINE_MUTEX(ccs_gc_mutex);
	static LIST_HEAD(ccs_gc_queue);
	if (!mutex_trylock(&ccs_gc_mutex))
		return 0;
	mutex_lock(&ccs_policy_lock);
	{
		struct ccs_globally_readable_file_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_globally_readable_list,
					list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_GLOBALLY_READABLE, ptr,
					  &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_globally_usable_env_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_globally_usable_env_list,
					list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_GLOBAL_ENV, ptr,
					  &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_pattern_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_pattern_list, list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_PATTERN, ptr,
					  &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_no_rewrite_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_no_rewrite_list, list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_NO_REWRITE, ptr,
					  &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_domain_initializer_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_domain_initializer_list,
					list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_DOMAIN_INITIALIZER,
					  ptr, &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_domain_keeper_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_domain_keeper_list, list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_DOMAIN_KEEPER, ptr,
					  &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_alias_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_alias_list, list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_ALIAS, ptr, &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_policy_manager_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_policy_manager_list, list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_MANAGER, ptr, &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_aggregator_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_aggregator_list, list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_AGGREGATOR, ptr,
					  &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	mutex_unlock(&ccs_policy_lock);
	{
		struct ccs_domain_info *domain;
		list_for_each_entry_rcu(domain, &ccs_domain_list, list) {
			struct ccs_acl_info *acl;
			list_for_each_entry_rcu(acl, &domain->acl_info_list,
						list) {
				if (!(acl->type & ACL_DELETED))
					continue;
				if (ccs_add_to_gc(CCS_ID_ACL, acl,
						  &ccs_gc_queue))
					list_del_rcu(&acl->list);
				else
					break;
			}
			if (!domain->is_deleted ||
			    ccs_used_by_task(domain))
				continue;
			if (ccs_add_to_gc(CCS_ID_DOMAIN, domain, &ccs_gc_queue))
				list_del_rcu(&domain->list);
			else
				break;
		}
	}
	{
		struct ccs_path_group_entry *group;
		list_for_each_entry_rcu(group, &ccs_path_group_list, list) {
			struct ccs_path_group_member *member;
			list_for_each_entry_rcu(member,
						&group->path_group_member_list,
						list) {
				if (!member->is_deleted)
					continue;
				if (ccs_add_to_gc(CCS_ID_PATH_GROUP_MEMBER,
						  member, &ccs_gc_queue))
					list_del_rcu(&member->list);
				else
					break;
			}
			if (!list_empty(&group->path_group_member_list) ||
			    atomic_read(&group->users))
				continue;
			if (ccs_add_to_gc(CCS_ID_PATH_GROUP, group,
					  &ccs_gc_queue))
				list_del_rcu(&group->list);
			else
				break;
		}
	}
	{
		struct ccs_address_group_entry *group;
		list_for_each_entry_rcu(group, &ccs_address_group_list, list) {
			struct ccs_address_group_member *member;
			list_for_each_entry_rcu(member,
					&group->address_group_member_list,
						list) {
				if (!member->is_deleted)
					break;
				if (ccs_add_to_gc(CCS_ID_ADDRESS_GROUP_MEMBER,
						  member, &ccs_gc_queue))
					list_del_rcu(&member->list);
				else
					break;
			}
			if (!list_empty(&group->address_group_member_list) ||
			    atomic_read(&group->users))
				continue;
			if (ccs_add_to_gc(CCS_ID_ADDRESS_GROUP, group,
					  &ccs_gc_queue))
				list_del_rcu(&group->list);
			else
				break;
		}
	}
	{
		struct ccs_reserved_entry *ptr;
		list_for_each_entry_rcu(ptr, &ccs_reservedport_list, list) {
			if (!ptr->is_deleted)
				continue;
			if (ccs_add_to_gc(CCS_ID_RESERVEDPORT, ptr,
					  &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	{
		struct ccs_condition *ptr;
		list_for_each_entry_rcu(ptr, &ccs_condition_list, list) {
			if (atomic_read(&ptr->users))
				continue;
			if (ccs_add_to_gc(CCS_ID_CONDITION, ptr, &ccs_gc_queue))
				list_del_rcu(&ptr->list);
			else
				break;
		}
	}
	mutex_unlock(&ccs_policy_lock);
	if (list_empty(&ccs_gc_queue))
		goto done;
	synchronize_srcu(&ccs_ss);
	{
		struct ccs_gc_entry *p;
		struct ccs_gc_entry *tmp;
		list_for_each_entry_safe(p, tmp, &ccs_gc_queue, list) {
			switch (p->type) {
			case CCS_ID_DOMAIN_INITIALIZER:
				ccs_del_domain_initializer(p->element);
				break;
			case CCS_ID_DOMAIN_KEEPER:
				ccs_del_domain_keeper(p->element);
				break;
			case CCS_ID_ALIAS:
				ccs_del_alias(p->element);
				break;
			case CCS_ID_GLOBALLY_READABLE:
				ccs_del_allow_read(p->element);
				break;
			case CCS_ID_PATTERN:
				ccs_del_file_pattern(p->element);
				break;
			case CCS_ID_NO_REWRITE:
				ccs_del_no_rewrite(p->element);
				break;
			case CCS_ID_MANAGER:
				ccs_del_manager(p->element);
				break;
			case CCS_ID_GLOBAL_ENV:
				ccs_del_allow_env(p->element);
				break;
			case CCS_ID_AGGREGATOR:
				ccs_del_aggregator(p->element);
				break;
			case CCS_ID_PATH_GROUP_MEMBER:
				ccs_del_path_group_member(p->element);
				break;
			case CCS_ID_PATH_GROUP:
				ccs_del_path_group(p->element);
				break;
			case CCS_ID_ADDRESS_GROUP_MEMBER:
				ccs_del_address_group_member(p->element);
				break;
			case CCS_ID_ADDRESS_GROUP:
				ccs_del_address_group(p->element);
				break;
			case CCS_ID_RESERVEDPORT:
				ccs_del_reservedport(p->element);
				break;
			case CCS_ID_CONDITION:
				ccs_del_condition(p->element);
				break;
			case CCS_ID_ACL:
				ccs_del_acl(p->element);
				break;
			case CCS_ID_DOMAIN:
				if (!ccs_del_domain(p->element))
					continue;
				break;
			}
			ccs_free_element(p->element);
			list_del(&p->list);
			kfree(p);
		}
	}
 done:
	mutex_unlock(&ccs_gc_mutex);
	return 0;
}

#if 0 && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)

static struct srcu_struct {
	int counter_idx;
	int counter[2];
} ccs_ss;
static DEFINE_SPINLOCK(ccs_counter_lock);

int ccs_read_lock(struct srcu_struct *sp)
{
	int idx;
	spin_lock(&ccs_counter_lock);
	idx = sp->counter_idx;
	sp->counter[idx]++;
	spin_unlock(&ccs_counter_lock);
	return idx;
}

void ccs_read_unlock(struct srcu_struct *sp, const int idx)
{
	spin_lock(&ccs_counter_lock);
	sp->counter[idx]--;
	spin_unlock(&ccs_counter_lock);
}

void ccs_wait_readers(struct srcu_struct *sp)
{
	int idx;
	int v;
	spin_lock(&ccs_counter_lock);
	idx = sp->counter_idx;
	sp->counter_idx ^= 1;
	v = sp->counter[idx];
	spin_unlock(&ccs_counter_lock);
	while (v) {
		msleep(1000);
		spin_lock(&ccs_counter_lock);
		v = sp->counter[idx];
		spin_unlock(&ccs_counter_lock);
	}
}

#endif
