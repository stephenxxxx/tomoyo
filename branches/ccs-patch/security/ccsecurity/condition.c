/*
 * security/ccsecurity/condition.c
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 * Version: 1.8.0-pre   2010/08/01
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#include <linux/slab.h>
#include "internal.h"

/**
 * ccs_argv - Check argv[] in "struct linux_binbrm".
 *
 * @index:   Index number of @arg_ptr.
 * @arg_ptr: Contents of argv[@index].
 * @argc:    Length of @argv.
 * @argv:    Pointer to "struct ccs_argv".
 * @checked: Set to true if @argv[@index] was found.
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_argv(const unsigned int index, const char *arg_ptr,
		     const int argc, const struct ccs_argv *argv,
		     u8 *checked)
{
	int i;
	struct ccs_path_info arg;
	arg.name = arg_ptr;
	for (i = 0; i < argc; argv++, checked++, i++) {
		bool result;
		if (index != argv->index)
			continue;
		*checked = 1;
		ccs_fill_path_info(&arg);
		result = ccs_path_matches_pattern(&arg, argv->value);
		if (argv->is_not)
			result = !result;
		if (!result)
			return false;
	}
	return true;
}

/**
 * ccs_envp - Check envp[] in "struct linux_binbrm".
 *
 * @env_name:  The name of environment variable.
 * @env_value: The value of environment variable.
 * @envc:      Length of @envp.
 * @envp:      Pointer to "struct ccs_envp".
 * @checked:   Set to true if @envp[@env_name] was found.
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_envp(const char *env_name, const char *env_value,
		     const int envc, const struct ccs_envp *envp,
		     u8 *checked)
{
	int i;
	struct ccs_path_info name;
	struct ccs_path_info value;
	name.name = env_name;
	ccs_fill_path_info(&name);
	value.name = env_value;
	ccs_fill_path_info(&value);
	for (i = 0; i < envc; envp++, checked++, i++) {
		bool result;
		if (!ccs_path_matches_pattern(&name, envp->name))
			continue;
		*checked = 1;
		if (envp->value) {
			result = ccs_path_matches_pattern(&value, envp->value);
			if (envp->is_not)
				result = !result;
		} else {
			result = true;
			if (!envp->is_not)
				result = !result;
		}
		if (!result)
			return false;
	}
	return true;
}

/**
 * ccs_scan_bprm - Scan "struct linux_binprm".
 *
 * @ee:   Pointer to "struct ccs_execve".
 * @argc: Length of @argc.
 * @argv: Pointer to "struct ccs_argv".
 * @envc: Length of @envp.
 * @envp: Poiner to "struct ccs_envp".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_scan_bprm(struct ccs_execve *ee,
			  const u16 argc, const struct ccs_argv *argv,
			  const u16 envc, const struct ccs_envp *envp)
{
	struct linux_binprm *bprm = ee->bprm;
	struct ccs_page_dump *dump = &ee->dump;
	char *arg_ptr = ee->tmp;
	int arg_len = 0;
	unsigned long pos = bprm->p;
	int offset = pos % PAGE_SIZE;
	int argv_count = bprm->argc;
	int envp_count = bprm->envc;
	bool result = true;
	u8 local_checked[32];
	u8 *checked;
	if (argc + envc <= sizeof(local_checked)) {
		checked = local_checked;
		memset(local_checked, 0, sizeof(local_checked));
	} else {
		checked = kzalloc(argc + envc, CCS_GFP_FLAGS);
		if (!checked)
			return false;
	}
	while (argv_count || envp_count) {
		if (!ccs_dump_page(bprm, pos, dump)) {
			result = false;
			goto out;
		}
		pos += PAGE_SIZE - offset;
		while (offset < PAGE_SIZE) {
			/* Read. */
			struct ccs_path_info arg;
			const char *kaddr = dump->data;
			const unsigned char c = kaddr[offset++];
			arg.name = arg_ptr;
			if (c && arg_len < CCS_EXEC_TMPSIZE - 10) {
				if (c == '\\') {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = '\\';
				} else if (c > ' ' && c < 127) {
					arg_ptr[arg_len++] = c;
				} else {
					arg_ptr[arg_len++] = '\\';
					arg_ptr[arg_len++] = (c >> 6) + '0';
					arg_ptr[arg_len++] =
						((c >> 3) & 7) + '0';
					arg_ptr[arg_len++] = (c & 7) + '0';
				}
			} else {
				arg_ptr[arg_len] = '\0';
			}
			if (c)
				continue;
			/* Check. */
			if (argv_count) {
				if (!ccs_argv(bprm->argc - argv_count,
					      arg_ptr, argc, argv,
					      checked)) {
					result = false;
					break;
				}
				argv_count--;
			} else if (envp_count) {
				char *cp = strchr(arg_ptr, '=');
				if (cp) {
					*cp = '\0';
					if (!ccs_envp(arg_ptr, cp + 1,
						      envc, envp,
						      checked + argc)) {
						result = false;
						break;
					}
				}
				envp_count--;
			} else {
				break;
			}
			arg_len = 0;
		}
		offset = 0;
		if (!result)
			break;
	}
 out:
	if (result) {
		int i;
		/* Check not-yet-checked entries. */
		for (i = 0; i < argc; i++) {
			if (checked[i])
				continue;
			/*
			 * Return true only if all unchecked indexes in
			 * bprm->argv[] are not matched.
			 */
			if (argv[i].is_not)
				continue;
			result = false;
			break;
		}
		for (i = 0; i < envc; envp++, i++) {
			if (checked[argc + i])
				continue;
			/*
			 * Return true only if all unchecked environ variables
			 * in bprm->envp[] are either undefined or not matched.
			 */
			if ((!envp->value && !envp->is_not) ||
			    (envp->value && envp->is_not))
				continue;
			result = false;
			break;
		}
	}
	if (checked != local_checked)
		kfree(checked);
	return result;
}

static bool ccs_scan_exec_realpath(struct file *file,
				   const struct ccs_name_union *ptr,
				   const bool match)
{
	bool result;
	struct ccs_path_info exe;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
	struct path path;
#endif
	if (!file)
		return false;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
	exe.name = ccs_realpath_from_path(&file->f_path);
#else
	path.mnt = file->f_vfsmnt;
	path.dentry = file->f_dentry;
	exe.name = ccs_realpath_from_path(&path);
#endif
	if (!exe.name)
		return false;
	ccs_fill_path_info(&exe);
	result = ccs_compare_name_union(&exe, ptr);
	kfree(exe.name);
	return result == match;
}

static bool ccs_parse_name_union_quoted(char *filename,
					struct ccs_name_union *ptr)
{
	bool result;
	char *cp = NULL;
	if (*filename == '"') {
		cp = filename + strlen(filename) - 1;
		if (*cp != '"')
			return false;
		*cp = '\0';
		filename++;
	}
	result = ccs_parse_name_union(filename, ptr);
	if (cp)
		*cp = '"';
	return result;
}

/**
 * ccs_get_dqword - ccs_get_name() for a quoted string.
 *
 * @start: String to save.
 *
 * Returns pointer to "struct ccs_path_info" on success, NULL otherwise.
 */
static const struct ccs_path_info *ccs_get_dqword(char *start)
{
	char *cp;
	if (*start++ != '"')
		return NULL;
	cp = start;
	while (1) {
		const char c = *cp++;
		if (!c)
			return NULL;
		if (c != '"' || *cp)
			continue;
		*(cp - 1) = '\0';
		break;
	}
	if (*start && !ccs_correct_word(start))
		return NULL;
	return ccs_get_name(start);
}

/**
 * ccs_parse_argv - Parse an argv[] condition part.
 *
 * @start: String to parse.
 * @argv:  Pointer to "struct ccs_argv".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_parse_argv(char *start, struct ccs_argv *argv)
{
	unsigned long index;
	const struct ccs_path_info *value;
	bool is_not;
	char c;
	if (ccs_parse_ulong(&index, &start) != CCS_VALUE_TYPE_DECIMAL)
		goto out;
	if (*start++ != ']')
		goto out;
	c = *start++;
	if (c == '=')
		is_not = false;
	else if (c == '!' && *start++ == '=')
		is_not = true;
	else
		goto out;
	value = ccs_get_dqword(start);
	if (!value)
		goto out;
	argv->index = index;
	argv->is_not = is_not;
	argv->value = value;
	return true;
 out:
	return false;
}

/**
 * ccs_parse_envp - Parse an envp[] condition part.
 *
 * @start: String to parse.
 * @envp:  Pointer to "struct ccs_envp".
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_parse_envp(char *start, struct ccs_envp *envp)
{
	const struct ccs_path_info *name;
	const struct ccs_path_info *value;
	bool is_not;
	char *cp = start;
	/*
	 * Since environment variable names don't
	 * contain '=', I can treat '"]=' and '"]!='
	 * sequences as delimiters.
	 */
	while (1) {
		if (!strncmp(start, "\"]=", 3)) {
			is_not = false;
			*start = '\0';
			start += 3;
			break;
		} else if (!strncmp(start, "\"]!=", 4)) {
			is_not = true;
			*start = '\0';
			start += 4;
			break;
		} else if (!*start++) {
			goto out;
		}
	}
	if (!ccs_correct_word(cp))
		goto out;
	name = ccs_get_name(cp);
	if (!name)
		goto out;
	if (!strcmp(start, "NULL")) {
		value = NULL;
	} else {
		value = ccs_get_dqword(start);
		if (!value) {
			ccs_put_name(name);
			goto out;
		}
	}
	envp->name = name;
	envp->is_not = is_not;
	envp->value = value;
	return true;
 out:
	return false;
}

const char *ccs_condition_keyword[CCS_MAX_CONDITION_KEYWORD] = {
	[CCS_TASK_UID]             = "task.uid",
	[CCS_TASK_EUID]            = "task.euid",
	[CCS_TASK_SUID]            = "task.suid",
	[CCS_TASK_FSUID]           = "task.fsuid",
	[CCS_TASK_GID]             = "task.gid",
	[CCS_TASK_EGID]            = "task.egid",
	[CCS_TASK_SGID]            = "task.sgid",
	[CCS_TASK_FSGID]           = "task.fsgid",
	[CCS_TASK_PID]             = "task.pid",
	[CCS_TASK_PPID]            = "task.ppid",
	[CCS_EXEC_ARGC]            = "exec.argc",
	[CCS_EXEC_ENVC]            = "exec.envc",
	[CCS_TASK_STATE_0]         = "task.state[0]",
	[CCS_TASK_STATE_1]         = "task.state[1]",
	[CCS_TASK_STATE_2]         = "task.state[2]",
	[CCS_TYPE_IS_SOCKET]       = "socket",
	[CCS_TYPE_IS_SYMLINK]      = "symlink",
	[CCS_TYPE_IS_FILE]         = "file",
	[CCS_TYPE_IS_BLOCK_DEV]    = "block",
	[CCS_TYPE_IS_DIRECTORY]    = "directory",
	[CCS_TYPE_IS_CHAR_DEV]     = "char",
	[CCS_TYPE_IS_FIFO]         = "fifo",
	[CCS_MODE_SETUID]          = "setuid",
	[CCS_MODE_SETGID]          = "setgid",
	[CCS_MODE_STICKY]          = "sticky",
	[CCS_MODE_OWNER_READ]      = "owner_read",
	[CCS_MODE_OWNER_WRITE]     = "owner_write",
	[CCS_MODE_OWNER_EXECUTE]   = "owner_execute",
	[CCS_MODE_GROUP_READ]      = "group_read",
	[CCS_MODE_GROUP_WRITE]     = "group_write",
	[CCS_MODE_GROUP_EXECUTE]   = "group_execute",
	[CCS_MODE_OTHERS_READ]     = "others_read",
	[CCS_MODE_OTHERS_WRITE]    = "others_write",
	[CCS_MODE_OTHERS_EXECUTE]  = "others_execute",
	[CCS_TASK_TYPE]            = "task.type",
	[CCS_TASK_EXECUTE_HANDLER] = "execute_handler",
	[CCS_EXEC_REALPATH]        = "exec.realpath",
	[CCS_SYMLINK_TARGET]       = "symlink.target",
	[CCS_PATH1_UID]            = "path1.uid",
	[CCS_PATH1_GID]            = "path1.gid",
	[CCS_PATH1_INO]            = "path1.ino",
	[CCS_PATH1_MAJOR]          = "path1.major",
	[CCS_PATH1_MINOR]          = "path1.minor",
	[CCS_PATH1_PERM]           = "path1.perm",
	[CCS_PATH1_TYPE]           = "path1.type",
	[CCS_PATH1_DEV_MAJOR]      = "path1.dev_major",
	[CCS_PATH1_DEV_MINOR]      = "path1.dev_minor",
	[CCS_PATH2_UID]            = "path2.uid",
	[CCS_PATH2_GID]            = "path2.gid",
	[CCS_PATH2_INO]            = "path2.ino",
	[CCS_PATH2_MAJOR]          = "path2.major",
	[CCS_PATH2_MINOR]          = "path2.minor",
	[CCS_PATH2_PERM]           = "path2.perm",
	[CCS_PATH2_TYPE]           = "path2.type",
	[CCS_PATH2_DEV_MAJOR]      = "path2.dev_major",
	[CCS_PATH2_DEV_MINOR]      = "path2.dev_minor",
	[CCS_PATH1_PARENT_UID]     = "path1.parent.uid",
	[CCS_PATH1_PARENT_GID]     = "path1.parent.gid",
	[CCS_PATH1_PARENT_INO]     = "path1.parent.ino",
	[CCS_PATH1_PARENT_PERM]    = "path1.parent.perm",
	[CCS_PATH2_PARENT_UID]     = "path2.parent.uid",
	[CCS_PATH2_PARENT_GID]     = "path2.parent.gid",
	[CCS_PATH2_PARENT_INO]     = "path2.parent.ino",
	[CCS_PATH2_PARENT_PERM]    = "path2.parent.perm",
};

/**
 * ccs_parse_post_condition - Parse post-condition part.
 *
 * @condition:  String to parse.
 * @post_state: Buffer to store post-condition part.
 *
 * Returns true on success, false otherwise.
 */
static bool ccs_parse_post_condition(char * const condition, u8 post_state[5])
{
	char *start = strstr(condition, "; set ");
	if (!start)
		return true;
	*start = '\0';
	start += 6;
	while (1) {
		static const char *ccs_state[4] = {
			"task.state[0]=",
			"task.state[1]=",
			"task.state[2]=",
			"audit="
		};
		unsigned int i;
		while (*start == ' ')
			start++;
		if (!*start)
			break;
		for (i = 0; i < 4; i++)
			if (ccs_str_starts(&start, ccs_state[i]))
				break;
		if (i == 4)
			goto out;
		if (post_state[3] & (1 << i))
			goto out;
		post_state[3] |= 1 << i;
		if (i < 3) {
			unsigned long value;
			if (!ccs_parse_ulong(&value, &start) || value > 255)
				goto out;
			post_state[i] = (u8) value;
		} else {
			if (ccs_str_starts(&start, "yes"))
				post_state[4] = 1;
			else if (ccs_str_starts(&start, "no"))
				post_state[4] = 0;
			else
				goto out;
		}
	}
	return true;
 out:
	return false;
}

static inline bool ccs_same_condition(const struct ccs_condition *p1,
				      const struct ccs_condition *p2)
{
	return p1->size == p2->size && p1->condc == p2->condc &&
		p1->numbers_count == p2->numbers_count &&
		p1->names_count == p2->names_count &&
		p1->argc == p2->argc && p1->envc == p2->envc &&
		p1->post_state[0] == p2->post_state[0] &&
		p1->post_state[1] == p2->post_state[1] &&
		p1->post_state[2] == p2->post_state[2] &&
		p1->post_state[3] == p2->post_state[3] &&
		p1->post_state[4] == p2->post_state[4] &&
		!memcmp(p1 + 1, p2 + 1, p1->size - sizeof(*p1));
}

static u8 ccs_condition_type(const char *word)
{
	u8 i;
	for (i = 0; i < CCS_MAX_CONDITION_KEYWORD; i++) {
		if (!strcmp(word, ccs_condition_keyword[i]))
			break;
	}
	return i;
}

/* #define DEBUG_CONDITION */

#ifdef DEBUG_CONDITION
#define dprintk printk
#else
#define dprintk(...) do { } while (0)
#endif

static struct ccs_condition *ccs_commit_condition(struct ccs_condition *entry)
{
	struct ccs_condition *ptr;
	bool found = false;
	if (mutex_lock_interruptible(&ccs_policy_lock)) {
		dprintk(KERN_WARNING "%u: %s failed\n", __LINE__, __func__);
		ptr = NULL;
		found = true;
		goto out;
	}
	list_for_each_entry_rcu(ptr, &ccs_shared_list[CCS_CONDITION_LIST],
				head.list) {
		if (!ccs_same_condition(ptr, entry))
			continue;
		/* Same entry found. Share this entry. */
		atomic_inc(&ptr->head.users);
		found = true;
		break;
	}
	if (!found) {
		if (ccs_memory_ok(entry, entry->size)) {
			atomic_set(&entry->head.users, 1);
			list_add_rcu(&entry->head.list,
				     &ccs_shared_list[CCS_CONDITION_LIST]);
		} else {
			found = true;
			ptr = NULL;
		}
	}
	mutex_unlock(&ccs_policy_lock);
 out:
	if (found) {
		ccs_del_condition(&entry->head.list);
		kfree(entry);
		entry = ptr;
	}
	return entry;
}

/**
 * ccs_get_condition - Parse condition part.
 *
 * @condition: Pointer to string to parse.
 *
 * Returns pointer to "struct ccs_condition" on success, NULL otherwise.
 */
struct ccs_condition *ccs_get_condition(char *condition)
{
	char *start;
	struct ccs_condition *entry = NULL;
	struct ccs_condition_element *condp = NULL;
	struct ccs_number_union *numbers_p = NULL;
	struct ccs_name_union *names_p = NULL;
	struct ccs_argv *argv = NULL;
	struct ccs_envp *envp = NULL;
	struct ccs_condition e = { };
	bool dry_run = true;
	char *end_of_string;
	if (!ccs_parse_post_condition(condition, e.post_state) ||
	    (*condition && !ccs_str_starts(&condition, "if ")))
		return NULL;
	end_of_string = condition + strlen(condition);
 rerun:
	start = condition;
	while (1) {
		u8 left = -1;
		u8 right = -1;
		char *word = start;
		char *cp;
		char *eq;
		bool is_not = false;
		if (!*word)
			break;
		cp = strchr(start, ' ');
		if (cp) {
			*cp = '\0';
			start = cp + 1;
		} else {
			start = "";
		}
		dprintk(KERN_WARNING "%u: <%s>\n", __LINE__, word);
		if (!strncmp(word, "exec.argv[", 10)) {
			if (dry_run) {
				e.argc++;
				e.condc++;
			} else {
				e.argc--;
				e.condc--;
				left = CCS_ARGV_ENTRY;
				if (!ccs_parse_argv(word + 10, argv++))
					goto out;
			}
			goto store_value;
		} else if (!strncmp(word, "exec.envp[\"", 11)) {
			if (dry_run) {
				e.envc++;
				e.condc++;
			} else {
				e.envc--;
				e.condc--;
				left = CCS_ENVP_ENTRY;
				if (!ccs_parse_envp(word + 11, envp++))
					goto out;
			}
			goto store_value;
		}
		eq = strchr(word, '=');
		if (!eq)
			goto out;
		if (eq > word && *(eq - 1) == '!') {
			is_not = true;
			eq--;
		}
		*eq = '\0';
		left = ccs_condition_type(word);
		dprintk(KERN_WARNING "%u: <%s> left=%u\n", __LINE__, word,
			left);
		if (left == CCS_MAX_CONDITION_KEYWORD) {
			if (dry_run) {
				e.numbers_count++;
			} else {
				e.numbers_count--;
				left = CCS_NUMBER_UNION;
				if (!ccs_parse_number_union(word, numbers_p))
					goto out;
				if (numbers_p->is_group)
					goto out;
				numbers_p++;
			}
		}
		*eq = is_not ? '!' : '=';
		word = eq + 1;
		if (is_not)
			word++;
		if (dry_run)
			e.condc++;
		else
			e.condc--;
		if (left == CCS_EXEC_REALPATH || left == CCS_SYMLINK_TARGET) {
			if (dry_run) {
				e.names_count++;
			} else {
				e.names_count--;
				right = CCS_NAME_UNION;
				if (!ccs_parse_name_union_quoted(word,
								 names_p++))
					goto out;
			}
			goto store_value;
		}
		right = ccs_condition_type(word);
		if (right == CCS_MAX_CONDITION_KEYWORD) {
			if (dry_run) {
				e.numbers_count++;
			} else {
				e.numbers_count--;
				right = CCS_NUMBER_UNION;
				if (!ccs_parse_number_union(word, numbers_p++))
					goto out;
			}
		}
 store_value:
		if (dry_run)
			continue;
		condp->left = left;
		condp->right = right;
		condp->equals = !is_not;
		dprintk(KERN_WARNING "%u: left=%u right=%u match=%u\n",
			__LINE__, condp->left, condp->right,
			condp->equals);
		condp++;
	}
	dprintk(KERN_INFO "%u: cond=%u numbers=%u names=%u ac=%u ec=%u\n",
		__LINE__, e.condc, e.numbers_count, e.names_count, e.argc,
		e.envc);
	if (!dry_run) {
		BUG_ON(e.names_count | e.numbers_count | e.argc | e.envc |
		       e.condc);
		return ccs_commit_condition(entry);
	}
	e.size = sizeof(*entry)
		+ e.condc * sizeof(struct ccs_condition_element)
		+ e.numbers_count * sizeof(struct ccs_number_union)
		+ e.names_count * sizeof(struct ccs_name_union)
		+ e.argc * sizeof(struct ccs_argv)
		+ e.envc * sizeof(struct ccs_envp);
	entry = kzalloc(e.size, CCS_GFP_FLAGS);
	if (!entry)
		return NULL;
	*entry = e;
	condp = (struct ccs_condition_element *) (entry + 1);
	numbers_p = (struct ccs_number_union *) (condp + e.condc);
	names_p = (struct ccs_name_union *) (numbers_p + e.numbers_count);
	argv = (struct ccs_argv *) (names_p + e.names_count);
	envp = (struct ccs_envp *) (argv + e.argc);
	for (start = condition; start < end_of_string; start++)
		if (!*start)
			*start = ' ';
	dry_run = false;
	goto rerun;
 out:
	dprintk(KERN_WARNING "%u: %s failed\n", __LINE__, __func__);
	if (entry) {
		ccs_del_condition(&entry->head.list);
		kfree(entry);
	}
	return NULL;
}

/**
 * ccs_get_attributes - Revalidate "struct inode".
 *
 * @obj: Pointer to "struct ccs_obj_info".
 *
 * Returns nothing.
 */
void ccs_get_attributes(struct ccs_obj_info *obj)
{
	u8 i;
	struct vfsmount *mnt = NULL;
	struct dentry *dentry = NULL;

	for (i = 0; i < CCS_MAX_STAT; i++) {
		struct inode *inode;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
		struct kstat kstat;
#endif
		switch (i) {
		case CCS_PATH1:
			mnt = obj->path1.mnt;
			dentry = obj->path1.dentry;
			break;
		case CCS_PATH2:
			mnt = obj->path2.mnt;
			dentry = obj->path2.dentry;
			break;
		default:
			if (!dentry)
				continue;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
			spin_lock(&dcache_lock);
			dentry = dget(dentry->d_parent);
			spin_unlock(&dcache_lock);
#else
			dentry = dget_parent(dentry);
#endif
			break;
		}
		if (!mnt)
			goto out;
		inode = dentry->d_inode;
		if (!inode)
			goto out;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
		if (inode->i_op && inode->i_op->revalidate &&
		    inode->i_op->revalidate(dentry)) {
			/* Nothing to do. */
		} else {
			struct ccs_mini_stat *stat = &obj->stat[i];
			stat->uid = inode->i_uid;
			stat->gid = inode->i_gid;
			stat->ino = inode->i_ino;
			stat->mode = inode->i_mode;
			stat->dev = inode->i_dev;
			stat->rdev = inode->i_rdev;
			obj->stat_valid[i] = true;
		}
#else
		if (!inode->i_op || vfs_getattr(mnt, dentry, &kstat)) {
			/* Nothing to do. */
		} else {
			struct ccs_mini_stat *stat = &obj->stat[i];
			stat->uid = kstat.uid;
			stat->gid = kstat.gid;
			stat->ino = kstat.ino;
			stat->mode = kstat.mode;
			stat->dev = kstat.dev;
			stat->rdev = kstat.rdev;
			obj->stat_valid[i] = true;
		}
#endif
	out:
		if (i & 1) /* i == CCS_PATH1_PARENT || i == CCS_PATH2_PARENT */
			dput(dentry);
	}
}

/**
 * ccs_condition - Check condition part.
 *
 * @r:    Pointer to "struct ccs_request_info".
 * @cond: Pointer to "struct ccs_condition". Maybe NULL.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds ccs_read_lock().
 */
bool ccs_condition(struct ccs_request_info *r,
		   const struct ccs_condition *cond)
{
	const struct task_struct *task = current;
	const u32 ccs_flags = task->ccs_flags;
	u32 i;
	unsigned long min_v[2] = { 0, 0 };
	unsigned long max_v[2] = { 0, 0 };
	const struct ccs_condition_element *condp;
	const struct ccs_number_union *numbers_p;
	const struct ccs_name_union *names_p;
	const struct ccs_argv *argv;
	const struct ccs_envp *envp;
	struct ccs_obj_info *obj;
	u16 condc;
	u16 argc;
	u16 envc;
	struct linux_binprm *bprm = NULL;
	if (!cond)
		return true;
	condc = cond->condc;
	argc = cond->argc;
	envc = cond->envc;
	obj = r->obj;
	if (r->ee)
		bprm = r->ee->bprm;
	if (!bprm && (argc || envc))
		return false;
	condp = (struct ccs_condition_element *) (cond + 1);
	numbers_p = (const struct ccs_number_union *) (condp + condc);
	names_p = (const struct ccs_name_union *)
		(numbers_p + cond->numbers_count);
	argv = (const struct ccs_argv *) (names_p + cond->names_count);
	envp = (const struct ccs_envp *) (argv + argc);
	for (i = 0; i < condc; i++) {
		const bool match = condp->equals;
		const u8 left = condp->left;
		const u8 right = condp->right;
		bool is_bitop[2] = { false, false };
		u8 j;
		condp++;
		/* Check argv[] and envp[] later. */
		if (left == CCS_ARGV_ENTRY || left == CCS_ENVP_ENTRY)
			continue;
		/* Check string expressions. */
		if (right == CCS_NAME_UNION) {
			const struct ccs_name_union *ptr = names_p++;
			switch (left) {
				struct ccs_path_info *symlink;
				struct ccs_execve *ee;
				struct file *file;
			case CCS_SYMLINK_TARGET:
				symlink = obj ? obj->symlink_target : NULL;
				if (!symlink ||
				    !ccs_compare_name_union(symlink, ptr)
				    == match)
					goto out;
				break;
			case CCS_EXEC_REALPATH:
				ee = r->ee;
				file = ee ? ee->bprm->file : NULL;
				if (!ccs_scan_exec_realpath(file, ptr, match))
					goto out;
				break;
			}
			continue;
		}
		/* Check numeric or bit-op expressions. */
		for (j = 0; j < 2; j++) {
			const u8 index = j ? right : left;
			unsigned long value = 0;
			switch (index) {
			case CCS_TASK_UID:
				value = current_uid();
				break;
			case CCS_TASK_EUID:
				value = current_euid();
				break;
			case CCS_TASK_SUID:
				value = current_suid();
				break;
			case CCS_TASK_FSUID:
				value = current_fsuid();
				break;
			case CCS_TASK_GID:
				value = current_gid();
				break;
			case CCS_TASK_EGID:
				value = current_egid();
				break;
			case CCS_TASK_SGID:
				value = current_sgid();
				break;
			case CCS_TASK_FSGID:
				value = current_fsgid();
				break;
			case CCS_TASK_PID:
				value = ccsecurity_exports.sys_getpid();
				break;
			case CCS_TASK_PPID:
				value = ccsecurity_exports.sys_getppid();
				break;
			case CCS_TYPE_IS_SOCKET:
				value = S_IFSOCK;
				break;
			case CCS_TYPE_IS_SYMLINK:
				value = S_IFLNK;
				break;
			case CCS_TYPE_IS_FILE:
				value = S_IFREG;
				break;
			case CCS_TYPE_IS_BLOCK_DEV:
				value = S_IFBLK;
				break;
			case CCS_TYPE_IS_DIRECTORY:
				value = S_IFDIR;
				break;
			case CCS_TYPE_IS_CHAR_DEV:
				value = S_IFCHR;
				break;
			case CCS_TYPE_IS_FIFO:
				value = S_IFIFO;
				break;
			case CCS_MODE_SETUID:
				value = S_ISUID;
				break;
			case CCS_MODE_SETGID:
				value = S_ISGID;
				break;
			case CCS_MODE_STICKY:
				value = S_ISVTX;
				break;
			case CCS_MODE_OWNER_READ:
				value = S_IRUSR;
				break;
			case CCS_MODE_OWNER_WRITE:
				value = S_IWUSR;
				break;
			case CCS_MODE_OWNER_EXECUTE:
				value = S_IXUSR;
				break;
			case CCS_MODE_GROUP_READ:
				value = S_IRGRP;
				break;
			case CCS_MODE_GROUP_WRITE:
				value = S_IWGRP;
				break;
			case CCS_MODE_GROUP_EXECUTE:
				value = S_IXGRP;
				break;
			case CCS_MODE_OTHERS_READ:
				value = S_IROTH;
				break;
			case CCS_MODE_OTHERS_WRITE:
				value = S_IWOTH;
				break;
			case CCS_MODE_OTHERS_EXECUTE:
				value = S_IXOTH;
				break;
			case CCS_EXEC_ARGC:
				if (!bprm)
					goto out;
				value = bprm->argc;
				break;
			case CCS_EXEC_ENVC:
				if (!bprm)
					goto out;
				value = bprm->envc;
				break;
			case CCS_TASK_STATE_0:
				value = (u8) (ccs_flags >> 24);
				break;
			case CCS_TASK_STATE_1:
				value = (u8) (ccs_flags >> 16);
				break;
			case CCS_TASK_STATE_2:
				value = (u8) (ccs_flags >> 8);
				break;
			case CCS_TASK_TYPE:
				value = ((u8) ccs_flags)
					& CCS_TASK_IS_EXECUTE_HANDLER;
				break;
			case CCS_TASK_EXECUTE_HANDLER:
				value = CCS_TASK_IS_EXECUTE_HANDLER;
				break;
			case CCS_NUMBER_UNION:
				/* Fetch values later. */
				break;
			default:
				if (!obj)
					goto out;
				if (!obj->validate_done) {
					ccs_get_attributes(obj);
					obj->validate_done = true;
				}
				{
					u8 stat_index;
					struct ccs_mini_stat *stat;
					switch (index) {
					case CCS_PATH1_UID:
					case CCS_PATH1_GID:
					case CCS_PATH1_INO:
					case CCS_PATH1_MAJOR:
					case CCS_PATH1_MINOR:
					case CCS_PATH1_TYPE:
					case CCS_PATH1_DEV_MAJOR:
					case CCS_PATH1_DEV_MINOR:
					case CCS_PATH1_PERM:
						stat_index = CCS_PATH1;
						break;
					case CCS_PATH2_UID:
					case CCS_PATH2_GID:
					case CCS_PATH2_INO:
					case CCS_PATH2_MAJOR:
					case CCS_PATH2_MINOR:
					case CCS_PATH2_TYPE:
					case CCS_PATH2_DEV_MAJOR:
					case CCS_PATH2_DEV_MINOR:
					case CCS_PATH2_PERM:
						stat_index = CCS_PATH2;
						break;
					case CCS_PATH1_PARENT_UID:
					case CCS_PATH1_PARENT_GID:
					case CCS_PATH1_PARENT_INO:
					case CCS_PATH1_PARENT_PERM:
						stat_index = CCS_PATH1_PARENT;
						break;
					case CCS_PATH2_PARENT_UID:
					case CCS_PATH2_PARENT_GID:
					case CCS_PATH2_PARENT_INO:
					case CCS_PATH2_PARENT_PERM:
						stat_index = CCS_PATH2_PARENT;
						break;
					default:
						goto out;
					}
					if (!obj->stat_valid[stat_index])
						goto out;
					stat = &obj->stat[stat_index];
					switch (index) {
					case CCS_PATH1_UID:
					case CCS_PATH2_UID:
					case CCS_PATH1_PARENT_UID:
					case CCS_PATH2_PARENT_UID:
						value = stat->uid;
						break;
					case CCS_PATH1_GID:
					case CCS_PATH2_GID:
					case CCS_PATH1_PARENT_GID:
					case CCS_PATH2_PARENT_GID:
						value = stat->gid;
						break;
					case CCS_PATH1_INO:
					case CCS_PATH2_INO:
					case CCS_PATH1_PARENT_INO:
					case CCS_PATH2_PARENT_INO:
						value = stat->ino;
						break;
					case CCS_PATH1_MAJOR:
					case CCS_PATH2_MAJOR:
						value = MAJOR(stat->dev);
						break;
					case CCS_PATH1_MINOR:
					case CCS_PATH2_MINOR:
						value = MINOR(stat->dev);
						break;
					case CCS_PATH1_TYPE:
					case CCS_PATH2_TYPE:
						value = stat->mode & S_IFMT;
						break;
					case CCS_PATH1_DEV_MAJOR:
					case CCS_PATH2_DEV_MAJOR:
						value = MAJOR(stat->rdev);
						break;
					case CCS_PATH1_DEV_MINOR:
					case CCS_PATH2_DEV_MINOR:
						value = MINOR(stat->rdev);
						break;
					case CCS_PATH1_PERM:
					case CCS_PATH2_PERM:
					case CCS_PATH1_PARENT_PERM:
					case CCS_PATH2_PARENT_PERM:
						value = stat->mode & S_IALLUGO;
						break;
					}
				}
				break;
			}
			max_v[j] = value;
			min_v[j] = value;
			switch (index) {
			case CCS_MODE_SETUID:
			case CCS_MODE_SETGID:
			case CCS_MODE_STICKY:
			case CCS_MODE_OWNER_READ:
			case CCS_MODE_OWNER_WRITE:
			case CCS_MODE_OWNER_EXECUTE:
			case CCS_MODE_GROUP_READ:
			case CCS_MODE_GROUP_WRITE:
			case CCS_MODE_GROUP_EXECUTE:
			case CCS_MODE_OTHERS_READ:
			case CCS_MODE_OTHERS_WRITE:
			case CCS_MODE_OTHERS_EXECUTE:
				is_bitop[j] = true;
			}
		}
		if (left == CCS_NUMBER_UNION) {
			/* Fetch values now. */
			const struct ccs_number_union *ptr = numbers_p++;
			min_v[0] = ptr->values[0];
			max_v[0] = ptr->values[1];
		}
		if (right == CCS_NUMBER_UNION) {
			/* Fetch values now. */
			const struct ccs_number_union *ptr = numbers_p++;
			if (ptr->is_group) {
				if (ccs_number_matches_group(min_v[0],
							     max_v[0],
							     ptr->group)
				    == match)
					continue;
			} else {
				if ((min_v[0] <= ptr->values[1] &&
				     max_v[0] >= ptr->values[0]) == match)
					continue;
			}
			goto out;
		}
		/*
		 * Bit operation is valid only when counterpart value
		 * represents permission.
		 */
		if (is_bitop[0] && is_bitop[1]) {
			goto out;
		} else if (is_bitop[0]) {
			switch (right) {
			case CCS_PATH1_PERM:
			case CCS_PATH1_PARENT_PERM:
			case CCS_PATH2_PERM:
			case CCS_PATH2_PARENT_PERM:
				if (!(max_v[0] & max_v[1]) == !match)
					continue;
			}
			goto out;
		} else if (is_bitop[1]) {
			switch (left) {
			case CCS_PATH1_PERM:
			case CCS_PATH1_PARENT_PERM:
			case CCS_PATH2_PERM:
			case CCS_PATH2_PARENT_PERM:
				if (!(max_v[0] & max_v[1]) == !match)
					continue;
			}
			goto out;
		}
		/* Normal value range comparison. */
		if ((min_v[0] <= max_v[1] && max_v[0] >= min_v[1]) == match)
			continue;
out:
		return false;
	}
	/* Check argv[] and envp[] now. */
	if (r->ee && (argc || envc))
		return ccs_scan_bprm(r->ee, argc, argv, envc, envp);
	return true;
}
