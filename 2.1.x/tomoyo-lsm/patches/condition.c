/*
 * fs/tomoyo_cond.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 */

#include "tomoyo.h"
#include "realpath.h"

/*****************************************************************************/

/* TODO: Move these definitions and prototypes to tomoyo.h . */
struct mini_stat {
	uid_t uid;
	gid_t gid;
	ino_t ino;
};
struct dentry;
struct vfsmount;
struct obj_info {
	u8 validate_done;
	u8 path1_valid;
	u8 path1_parent_valid;
	u8 path2_parent_valid;
	struct dentry *path1_dentry;
	struct vfsmount *path1_vfsmnt;
	struct dentry *path2_dentry;
	struct vfsmount *path2_vfsmnt;
	struct mini_stat path1_stat;
	/* I don't handle path2_stat for rename operation. */
	struct mini_stat path1_parent_stat;
	struct mini_stat path2_parent_stat;
};
struct condition_list;
char *tmy_find_condition_part(char *data);
const struct condition_list *tmy_assign_condition(const char *condition);
int tmy_check_condition(const struct condition_list *ptr,
	struct obj_info *obj);
int tmy_dump_condition(struct io_buffer *head,
	const struct condition_list *ptr);

/* TODO: Add "const struct condition_list *cond;" at "struct acl_info". */

/* TODO: Add tmy_dump_condition() at print_\*_acl(). */

/* TODO: Add

   const struct condition_list *condition = NULL;
   char *cp = tmy_find_condition_part(...);
   if (cp && (condition = tmy_assign_condition(cp)) == NULL) ...;

   at tmy_add_\*_policy().
*/

/* TODO: Add "const struct condition_list *cond" at add_\*_entry(). */

/* TODO: Add "tmy_check_condition(ptr->cond, ...)" at check_\*_acl(). */

/*****************************************************************************/



/**
 * tmy_find_condition_part - check whether a line contains condition part.
 * @data: a line to check.
 *
 * Returns pointer to condition part if found.
 * Returns NULL if not found.
 *
 * Since the trailing spaces are removed by tmy_normalize_line(),
 * the last "\040if\040" sequence corresponds to condition part.
 */
char *tmy_find_condition_part(char *data)
{
	char *cp = strstr(data, " if ");
	if (!cp) {
		char *cp2;
		while ((cp2 = strstr(cp + 4, " if ")) != NULL)
			cp = cp2;
		*cp++ = '\0';
	}
	return cp;
}

#define VALUE_TYPE_DECIMAL     1 /* 01 */
#define VALUE_TYPE_OCTAL       2 /* 10 */
#define VALUE_TYPE_HEXADECIMAL 3 /* 11 */

static int tmy_parse_ulong(unsigned long *result, const char **str)
{
	const char *cp = *str;
	char *ep;
	int base = 10;
	if (*cp == '0') {
		char c = * (cp + 1);
		if (c == 'x' || c == 'X') {
			base = 16; cp += 2;
		} else if (c >= '0' && c <= '7') {
			base = 8; cp++;
		}
	}
	*result = simple_strtoul(cp, &ep, base);
	if (cp == ep) return 0;
	*str = ep;
	return (base == 16 ? VALUE_TYPE_HEXADECIMAL :
		(base == 8 ? VALUE_TYPE_OCTAL : VALUE_TYPE_DECIMAL));
}

static void tmy_print_ulong(char *buffer, const int buffer_len,
			const unsigned long value, const int type)
{
	if (type == VALUE_TYPE_DECIMAL)
		snprintf(buffer, buffer_len, "%lu", value);
	else if (type == VALUE_TYPE_OCTAL)
		snprintf(buffer, buffer_len, "0%lo", value);
	else
		snprintf(buffer, buffer_len, "0x%lX", value);
}
			
static struct condition_list {
	struct condition_list *next;
	int length;
	/* "unsigned long condition[length]" comes here.*/
} head = { NULL, 0 };

#define TASK_UID          0
#define TASK_EUID         1
#define TASK_SUID         2
#define TASK_FSUID        3
#define TASK_GID          4
#define TASK_EGID         5
#define TASK_SGID         6
#define TASK_FSGID        7
#define TASK_PID          8
#define TASK_PPID         9
#define PATH1_UID        10
#define PATH1_GID        11
#define PATH1_INO        12
#define PATH1_PARENT_UID 13
#define PATH1_PARENT_GID 14
#define PATH1_PARENT_INO 15
#define PATH2_PARENT_UID 16
#define PATH2_PARENT_GID 17
#define PATH2_PARENT_INO 18
#define MAX_KEYWORD      19

static struct {
	const char *keyword;
	const int keyword_len; /* strlen(keyword) */
} cc_keyword[MAX_KEYWORD] = {
	[TASK_UID]         = { "task.uid",           8 },
	[TASK_EUID]        = { "task.euid",          9 },
	[TASK_SUID]        = { "task.suid",          9 },
	[TASK_FSUID]       = { "task.fsuid",        10 },
	[TASK_GID]         = { "task.gid",           8 },
	[TASK_EGID]        = { "task.egid",          9 },
	[TASK_SGID]        = { "task.sgid",          9 },
	[TASK_FSGID]       = { "task.fsgid",        10 },
	[TASK_PID]         = { "task.pid",           8 },
	[TASK_PPID]        = { "task.ppid",          9 },
	[PATH1_UID]        = { "path1.uid",          9 },
	[PATH1_GID]        = { "path1.gid",          9 },
	[PATH1_INO]        = { "path1.ino",          9 },
	[PATH1_PARENT_UID] = { "path1.parent.uid",  16 },
	[PATH1_PARENT_GID] = { "path1.parent.gid",  16 },
	[PATH1_PARENT_INO] = { "path1.parent.ino",  16 },
	[PATH2_PARENT_UID] = { "path2.parent.uid",  16 },
	[PATH2_PARENT_GID] = { "path2.parent.gid",  16 },
	[PATH2_PARENT_INO] = { "path2.parent.ino",  16 }
};

/**
 * tmy_assign_condition - create condition part.
 * @data: a condition part to create.
 *
 * Returns pointer to "struct condition_list" on success.
 * Returns NULL on failure.
 */
const struct condition_list *tmy_assign_condition(const char *condition)
{
	const char *start;
	struct condition_list *ptr;
	struct condition_list *new_ptr;
	unsigned long *ptr2;
	int counter = 0;
	int size;
	int left;
	int right;
	unsigned long left_min = 0;
	unsigned long left_max = 0;
	unsigned long right_min = 0;
	unsigned long right_max = 0;
	if (strncmp(condition, "if ", 3))
		return NULL;
	condition += 3;
	start = condition;
	while (*condition) {
		if (*condition == ' ')
			condition++;
		for (left = 0; left < MAX_KEYWORD; left++) {
			if (strncmp(condition, cc_keyword[left].keyword,
				    cc_keyword[left].keyword_len))
				continue;
			condition += cc_keyword[left].keyword_len;
			break;
		}
		if (left == MAX_KEYWORD) {
			if (!tmy_parse_ulong(&left_min, &condition))
				goto out;
			counter++; /* body */
			if (*condition != '-')
				goto not_range1;
			condition++;
			if (!tmy_parse_ulong(&left_max, &condition)
			    || left_min > left_max)
				goto out;
			counter++; /* body */
not_range1: ;
		}
		if (strncmp(condition, "!=", 2) == 0)
			condition += 2;
		else if (*condition == '=')
			condition++;
		else
			goto out;
		counter++; /* header */
		for (right = 0; right < MAX_KEYWORD; right++) {
			if (strncmp(condition, cc_keyword[right].keyword,
				    cc_keyword[right].keyword_len))
				continue;
			condition += cc_keyword[right].keyword_len;
			break;
		}
		if (right == MAX_KEYWORD) {
			if (!tmy_parse_ulong(&right_min, &condition))
				goto out;
			counter++; /* body */
			if (*condition != '-')
				goto not_range2;
			condition++;
			if (!tmy_parse_ulong(&right_max, &condition)
			    || right_min > right_max)
				goto out;
			counter++; /* body */
not_range2: ;
		}
	}
	size = sizeof(*new_ptr) + counter * sizeof(unsigned long);
	new_ptr = tmy_alloc(size);
	if (!new_ptr)
		return NULL;
	new_ptr->length = counter;
	ptr2 = (unsigned long *) (((u8 *) new_ptr) + sizeof(*new_ptr));
	condition = start;
	while (*condition) {
		unsigned int match = 0;
		if (*condition == ' ')
			condition++;
		for (left = 0; left < MAX_KEYWORD; left++) {
			if (strncmp(condition, cc_keyword[left].keyword,
			    cc_keyword[left].keyword_len))
				continue;
			condition += cc_keyword[left].keyword_len;
			break;
		}
		if (left == MAX_KEYWORD) {
			match |= tmy_parse_ulong(&left_min, &condition) << 2;
			counter--; /* body */
			if (*condition != '-')
				goto not_range3;
			condition++;
			match |= tmy_parse_ulong(&left_max, &condition) << 4;
			counter--; /* body */
			left++;
not_range3: ;
		}
		if (strncmp(condition, "!=", 2) == 0)
			condition += 2;
		else if (*condition == '=') {
			match |= 1; condition++;
		} else
			goto out2;
		counter--; /* header */
		for (right = 0; right < MAX_KEYWORD; right++) {
			if (strncmp(condition, cc_keyword[right].keyword,
			    cc_keyword[right].keyword_len))
				continue;
			condition += cc_keyword[right].keyword_len;
			break;
		}
		if (right == MAX_KEYWORD) {
			match |= tmy_parse_ulong(&right_min, &condition) << 6;
			counter--; /* body */
			if (*condition != '-')
				goto not_range4;
			condition++;
			match |= tmy_parse_ulong(&right_max, &condition) << 8;
			counter--; /* body */
			right++;
not_range4: ;
		}
		if (counter < 0)
			goto out2;
		*ptr2++ = (match << 16) | (left << 8) | right;
		if (left >= MAX_KEYWORD)
			*ptr2++ = left_min;
		if (left == MAX_KEYWORD + 1)
			*ptr2++ = left_max;
		if (right >= MAX_KEYWORD)
			*ptr2++ = right_min;
		if (right == MAX_KEYWORD + 1)
			*ptr2++ = right_max;
	}
	{
		static DECLARE_MUTEX(lock);
		struct condition_list *prev = NULL;
		down(&lock);
		for (ptr = &head; ptr; prev = ptr, ptr = ptr->next) {
			/* Don't compare if size differs. */
			if (ptr->length != new_ptr->length)
				continue;
			/*
			 * Compare ptr and new_ptr 
			 * except ptr->next and new_ptr->next .
			 */
			if (memcmp(((u8 *) ptr) + sizeof(ptr->next),
				   ((u8 *) new_ptr) + sizeof(new_ptr->next),
				   size - sizeof(ptr->next)))
				continue;
			/* Same entry found. Share this entry. */
			tmy_free(new_ptr);
			new_ptr = ptr;
			goto ok;
		}
		/* Same entry not found. Save this entry. */
		ptr = tmy_alloc_element(size);
		if (ptr)
			memmove(ptr, new_ptr, size);
		tmy_free(new_ptr);
		new_ptr = ptr;
		/* Append to chain. */
		prev->next = new_ptr;
ok: ;
		up(&lock);
	}
	return new_ptr;
out2: ;
	tmy_free(new_ptr);
out: ;
	return NULL;
}

/* Get inode's attribute information. */
static void tmy_get_attributes(struct obj_info *obj)
{
	struct vfsmount *mnt;
	struct dentry *dentry;
	struct inode *inode;
	struct kstat stat;
	
	mnt = obj->path1_vfsmnt;
	dentry = obj->path1_dentry;
	inode = dentry->d_inode;
	if (inode) {
		if (!inode->i_op || vfs_getattr(mnt, dentry, &stat)) {
			/* Nothing to do. */
		} else {
			obj->path1_stat.uid = stat.uid;
			obj->path1_stat.gid = stat.gid;
			obj->path1_stat.ino = stat.ino;
			obj->path1_valid = 1;
		}
	}

	dentry = dget_parent(obj->path1_dentry);
	inode = dentry->d_inode;
	if (inode) {
		if (!inode->i_op || vfs_getattr(mnt, dentry, &stat)) {
			/* Nothing to do. */
		} else {
			obj->path1_parent_stat.uid = stat.uid;
			obj->path1_parent_stat.gid = stat.gid;
			obj->path1_parent_stat.ino = stat.ino;
			obj->path1_parent_valid = 1;
		}
	}
	dput(dentry);

	if ((mnt = obj->path2_vfsmnt) != NULL) {
		dentry = dget_parent(obj->path2_dentry);
		inode = dentry->d_inode;
		if (inode) {
			if (!inode->i_op || vfs_getattr(mnt, dentry, &stat)) {
				/* Nothing to do. */
			} else {
				obj->path2_parent_stat.uid = stat.uid;
				obj->path2_parent_stat.gid = stat.gid;
				obj->path2_parent_stat.ino = stat.ino;
				obj->path2_parent_valid = 1;
			}
		}
		dput(dentry);
	}
}

/**
 * tmy_check_condition - check condition part.
 * @ptr: pointer to "struct condition_list".
 * @obj: pointer to "struct obj_info". May be NULL.
 *
 * Returns zero on success.
 * Returns nonzero on failure.
 */
int tmy_check_condition(const struct condition_list *ptr, struct obj_info *obj)
{
	extern asmlinkage long sys_getppid(void);
	struct task_struct *task = current;
	int i;
	unsigned long left_min = 0;
	unsigned long left_max = 0;
	unsigned long right_min = 0;
	unsigned long right_max = 0;
	const unsigned long *ptr2;
	if (!ptr)
		return 0;
	ptr2 = (unsigned long *) (((u8 *) ptr) + sizeof(*ptr));
	for (i = 0; i < ptr->length; i++) {
		const u8 match = ((*ptr2) >> 16) & 1;
		const u8 left = (*ptr2) >> 8;
		const u8 right = *ptr2;
		ptr2++;
		if ((left >= PATH1_UID && left < MAX_KEYWORD)
		    || (right >= PATH1_UID && right < MAX_KEYWORD)) {
			if (!obj)
				goto out;
			if (!obj->validate_done) {
				tmy_get_attributes(obj);
				obj->validate_done = 1;
			}
		}
		switch (left) {
		case TASK_UID:
			left_min = task->uid; 
			left_max = left_min;
			break;
		case TASK_EUID:
			left_min = task->euid;
			left_max = left_min;
			break;
		case TASK_SUID:
			left_min = task->suid;
			left_max = left_min;
			break;
		case TASK_FSUID:
			left_min = task->fsuid;
			left_max = left_min;
			break;
		case TASK_GID:
			left_min = task->gid;
			left_max = left_min;
			break;
		case TASK_EGID:
			left_min = task->egid;
			left_max = left_min;
			break;
		case TASK_SGID:
			left_min = task->sgid;
			left_max = left_min;
			break;
		case TASK_FSGID:
			left_min = task->fsgid;
			left_max = left_min;
			break;
		case TASK_PID:
			left_min = task->pid;
			left_max = left_min;
			break;
		case TASK_PPID:
			left_min = sys_getppid();
			left_max = left_min;
			break;
		case PATH1_UID:
			if (!obj->path1_valid)
				goto out;
			left_min = obj->path1_stat.uid;
			left_max = left_min;
			break;
		case PATH1_GID:
			if (!obj->path1_valid)
				goto out;
			left_min = obj->path1_stat.gid;
			left_max = left_min;
			break;
		case PATH1_INO:
			if (!obj->path1_valid)
				goto out;
			left_min = obj->path1_stat.ino;
			left_max = left_min;
			break;
		case PATH1_PARENT_UID:
			if (!obj->path1_parent_valid)
				goto out;
			left_min = obj->path1_parent_stat.uid;
			left_max = left_min;
			break;
		case PATH1_PARENT_GID:
			if (!obj->path1_parent_valid)
				goto out;
			left_min = obj->path1_parent_stat.gid;
			left_max = left_min;
			break;
		case PATH1_PARENT_INO:
			if (!obj->path1_parent_valid)
				goto out;
			left_min = obj->path1_parent_stat.ino;
			left_max = left_min;
			break;
		case PATH2_PARENT_UID:
			if (!obj->path2_parent_valid)
				goto out;
			left_min = obj->path2_parent_stat.uid;
			left_max = left_min;
			break;
		case PATH2_PARENT_GID:
			if (!obj->path2_parent_valid)
				goto out;
			left_min = obj->path2_parent_stat.gid;
			left_max = left_min;
			break;
		case PATH2_PARENT_INO:
			if (!obj->path2_parent_valid)
				goto out;
			left_min = obj->path2_parent_stat.ino;
			left_max = left_min;
			break;
		case MAX_KEYWORD:
			left_min = *ptr2++;
			left_max = left_min;
			i++;
			break;
		case MAX_KEYWORD + 1:
			left_min = *ptr2++;
			left_max = *ptr2++;
			i += 2;
			break;
		}
		switch (right) {
		case TASK_UID:
			right_min = task->uid;
			right_max = right_min;
			break;
		case TASK_EUID:
			right_min = task->euid;
			right_max = right_min;
			break;
		case TASK_SUID:
			right_min = task->suid;
			right_max = right_min;
			break;
		case TASK_FSUID:
			right_min = task->fsuid;
			right_max = right_min;
			break;
		case TASK_GID:
			right_min = task->gid;
			right_max = right_min;
			break;
		case TASK_EGID:
			right_min = task->egid;
			right_max = right_min;
			break;
		case TASK_SGID:
			right_min = task->sgid;
			right_max = right_min;
			break;
		case TASK_FSGID:
			right_min = task->fsgid;
			right_max = right_min;
			break;
		case TASK_PID:
			right_min = task->pid;
			right_max = right_min;
			break;
		case TASK_PPID:
			right_min = sys_getppid();
			right_max = right_min;
			break;
		case PATH1_UID:
			if (!obj->path1_valid)
				goto out;
			right_min = obj->path1_stat.uid;
			right_max = right_min;
			break;
		case PATH1_GID:
			if (!obj->path1_valid)
				goto out;
			right_min = obj->path1_stat.gid;
			right_max = right_min;
			break;
		case PATH1_INO:
			if (!obj->path1_valid)
				goto out;
			right_min = obj->path1_stat.ino;
			right_max = right_min;
			break;
		case PATH1_PARENT_UID:
			if (!obj->path1_parent_valid)
				goto out;
			right_min = obj->path1_parent_stat.uid;
			right_max = right_min;
			break;
		case PATH1_PARENT_GID:
			if (!obj->path1_parent_valid)
				goto out;
			right_min = obj->path1_parent_stat.gid;
			right_max = right_min;
			break;
		case PATH1_PARENT_INO:
			if (!obj->path1_parent_valid)
				goto out;
			right_min = obj->path1_parent_stat.ino;
			right_max = right_min;
			break;
		case PATH2_PARENT_UID:
			if (!obj->path2_parent_valid)
				goto out;
			right_min = obj->path2_parent_stat.uid;
			right_max = right_min;
			break;
		case PATH2_PARENT_GID:
			if (!obj->path2_parent_valid)
				goto out;
			right_min = obj->path2_parent_stat.gid;
			right_max = right_min;
			break;
		case PATH2_PARENT_INO:
			if (!obj->path2_parent_valid)
				goto out;
			right_min = obj->path2_parent_stat.ino;
			right_max = right_min;
			break;
		case MAX_KEYWORD:
			right_min = *ptr2++;
			right_max = right_min;
			i++;
			break;
		case MAX_KEYWORD + 1:
			right_min = *ptr2++;
			right_max = *ptr2++;
			i += 2;
			break;
		}
		if (match) {
			if (left_min <= right_max && left_max >= right_min)
				continue;
		} else {
			if (left_min > right_max || left_max < right_min)
				continue;
		}
out: ;
		return -EPERM;
	}
	return 0;
}

/**
 * tmy_dump_condition - dump condition part.
 * @head: pointer to "struct io_buffer".
 * @ptr: pointer to "struct condition_list".
 *
 * Returns nonzero if reading incomplete.
 * Returns zero otherwise.
 */
int tmy_dump_condition(struct io_buffer *head, const struct condition_list *ptr)
{
	int i;
	const unsigned long *ptr2;
	char buffer[32];
	if (!ptr)
		goto last;
	ptr2 = (unsigned long *) (((u8 *) ptr) + sizeof(*ptr));
	memset(buffer, 0, sizeof(buffer));
	for (i = 0; i < ptr->length; i++) {
		const u16 match = (*ptr2) >> 16;
		const u8 left = (*ptr2) >> 8;
		const u8 right = *ptr2;
		ptr2++;
		if (tmy_io_printf(head, "%s", i ? " " : " if "))
			break;
		if (left < MAX_KEYWORD) {
			if (tmy_io_printf(head, "%s", cc_keyword[left].keyword))
				break;
		} else {
			tmy_print_ulong(buffer, sizeof(buffer) - 1, *ptr2++,
					(match >> 2) & 3);
			if (tmy_io_printf(head, "%s", buffer))
				break;
			i++;
			if (left == MAX_KEYWORD + 1) {
				tmy_print_ulong(buffer, sizeof(buffer) - 1,
						*ptr2++, (match >> 4) & 3);
				if (tmy_io_printf(head, "-%s", buffer))
					break;
				i++;
			}
		}
		if (tmy_io_printf(head, "%s", (match & 1) ? "=" : "!="))
			break;
		if (right < MAX_KEYWORD) {
			if (tmy_io_printf(head, "%s", cc_keyword[right].keyword))
				break;
		} else {
			tmy_print_ulong(buffer, sizeof(buffer) - 1, *ptr2++,
					(match >> 6) & 3);
			if (tmy_io_printf(head, "%s", buffer))
				break;
			i++;
			if (right == MAX_KEYWORD + 1) {
				tmy_print_ulong(buffer, sizeof(buffer) - 1,
						*ptr2++, (match >> 8) & 3);
				if (tmy_io_printf(head, "-%s", buffer))
					break;
				i++;
			}
		}
	}
	if (i < ptr->length)
		return -ENOMEM;
last: ;
	return tmy_io_printf(head, "\n") ? -ENOMEM : 0;
}
