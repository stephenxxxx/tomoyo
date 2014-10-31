/*
 * include/linux/ccs_common.h
 *
 * Common functions for SAKURA and TOMOYO.
 *
 * Copyright (C) 2005-2006  NTT DATA CORPORATION
 *
 * Version: 1.1.1   2006/05/15
 *
 * This file is applicable to both 2.4.30 and 2.6.11 and later.
 * See README.ccs for ChangeLog.
 *
 */

#ifndef _LINUX_CCS_COMMON_H
#define _LINUX_CCS_COMMON_H

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/utime.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/dnotify.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <stdarg.h>
#include <linux/delay.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <linux/kmod.h>
#define for_each_process for_each_task
#endif

#ifdef CONFIG_TOMOYO_MAX_ACCEPT_FILES
#define MAX_ACCEPT_FILES (CONFIG_TOMOYO_MAX_ACCEPT_FILES)
#else
#define MAX_ACCEPT_FILES 256
#endif
#ifdef CONFIG_TOMOYO_MAX_GRANT_LOG
#define MAX_GRANT_LOG (CONFIG_TOMOYO_MAX_GRANT_LOG)
#else
#define MAX_GRANT_LOG 1024
#endif
#ifdef CONFIG_TOMOYO_MAX_REJECT_LOG
#define MAX_REJECT_LOG (CONFIG_TOMOYO_MAX_REJECT_LOG)
#else
#define MAX_REJECT_LOG 1024
#endif

/*
 *  TOMOYO uses the following structures.
 *  These structures are used append only (like CD-R) and memory allocated for them are never kfree()ed.
 *  Since no locks are used for reading, assignment must be performed atomically.
 */

/*************************  The structure for domains.  *************************/

struct acl_info {
	struct acl_info *next;
	unsigned int type_hash;
	/* ACL specific variables come here. */
};

typedef struct domain_info {
	struct domain_info *next;       /* Pointer to next record. NULL if none.       */
	struct acl_info *first_acl_ptr; /* Pointer to first acl. NULL if not assigned. */
	const char *domainname;         /* Name of this domain. Never NULL.            */
	unsigned int attribute;         /* Domain attributes.                          */
} DOMAIN_INFO;

#define DOMAIN_ATTRIBUTE_TRUSTED       1 /* This domain is trusted. (No domain mandatory access controlls are applied.) */
#define DOMAIN_ATTRIBUTE_DELETED       2 /* This domain is deleted.                                                     */
#define DOMAIN_ATTRIBUTE_UPDATED       4 /* This domain is updated.                                                     */
#define DOMAIN_ATTRIBUTE_QUOTA_WARNED  8 /* This domain has too many FILE_ACL_RECORDs to keep.                          */
#define DOMAIN_ATTRIBUTE_BUG_WARNED   16 /* This domain is marked as updated, but no updated domain found.              */
#define DOMAIN_ATTRIBUTE_UNTRUSTED    32 /* This domain is no longer trusted.                                           */

typedef struct {
	struct acl_info *next;   /* Pointer to next record. NULL if none.            */
	unsigned int type_hash;  /* = TYPE_FILE_ACL | number of "/" in the filename. */
	const char *filename;    /* Absolute pathname. Never NULL.                   */
	unsigned short int perm; /* Permissions. Between 0(---) and 7(rwx).          */
} FILE_ACL_RECORD;

typedef struct {
	struct acl_info *next;  /* Pointer to next record. NULL if none.           */
	unsigned int type_hash; /* = TYPE_DIR_ACL | number of "/" in the filename. */
	const char *filename;   /* Absolute pathname. Never NULL.                  */
} DIR_ACL_RECORD;

typedef struct {
	struct acl_info *next;  /* Pointer to next record. NULL if none.     */
	unsigned int type_hash; /* = TYPE_CAPABILITY_ACL | capability index. */
} CAPABILITY_ACL_RECORD;

typedef struct {
	struct acl_info *next;  /* Pointer to next record. NULL if none.       */
	unsigned int type_hash; /* = TYPE_SIGNAL_ACL | signal_number.          */
	const char *domainname; /* Pointer to destination pattern. Never NULL. */
} SIGNAL_ACL_RECORD;

typedef struct {
	struct acl_info *next;       /* Pointer to next record. NULL if none.              */
	unsigned int type_hash;      /* = TYPE_CONNECT_ACL or TYPE_BIND_ACL | boolean TCP. */
	unsigned short int min_port; /* Start of port number range.                        */
	unsigned short int max_port; /* End of port number range.                          */
} NETWORK_ACL_RECORD;

typedef struct {
	struct acl_info *next;  /* Pointer to next record. NULL if none. */
	unsigned int type_hash;
	const char *filename;   /* Absolute pathname. Never NULL.        */
} SINGLE_ACL_RECORD;

typedef struct {
	struct acl_info *next;  /* Pointer to next record. NULL if none. */
	unsigned int type_hash;
	const char *filename1;  /* Absolute pathname. Never NULL.        */
	const char *filename2;  /* Absolute pathname. Never NULL.        */
} DOUBLE_ACL_RECORD;

/*************************  Keywords for ACLs.  *************************/

#define KEYWORD_DELETE                   "delete "
#define KEYWORD_ALLOW_MOUNT              "allow_mount "
#define KEYWORD_DENY_UNMOUNT             "deny_unmount "
#define KEYWORD_ALLOW_CHROOT             "allow_chroot "
#define KEYWORD_DENY_AUTOBIND            "deny_autobind "
#define KEYWORD_ALLOW_CAPABILITY         "allow_capability "
#define KEYWORD_ALLOW_BIND               "allow_bind "
#define KEYWORD_ALLOW_CONNECT            "allow_connect "
#define KEYWORD_ALLOW_SIGNAL             "allow_signal "
#define KEYWORD_TRUST_DOMAIN             "trust_domain "
#define KEYWORD_ALLOW_READ               "allow_read "
#define KEYWORD_INITIALIZER              "initializer "
#define KEYWORD_FILE_PATTERN             "file_pattern "
#define KEYWORD_DELETE_LEN             7 /* strlen(KEYWORD_dELETE)           */
#define KEYWORD_ALLOW_MOUNT_LEN       12 /* strlen(KEYWORD_ALLOW_MOUNT)      */
#define KEYWORD_DENY_UNMOUNT_LEN      13 /* strlen(KEYWORD_DENY_UNMOUNT)     */
#define KEYWORD_ALLOW_CHROOT_LEN      13 /* strlen(KEYWORD_ALLOW_CHROOT)     */
#define KEYWORD_DENY_AUTOBIND_LEN     14 /* strlen(KEYWORD_DENY_AUTOBIND)    */
#define KEYWORD_ALLOW_CAPABILITY_LEN  17 /* strlen(KEYWORD_ALLOW_CAPABILITY) */
#define KEYWORD_ALLOW_BIND_LEN        11 /* strlen(KEYWORD_ALLOW_BIND)       */
#define KEYWORD_ALLOW_CONNECT_LEN     14 /* strlen(KEYWORD_ALLOW_CONNECT)    */
#define KEYWORD_ALLOW_SIGNAL_LEN      13 /* strlen(KEYWORD_ALLOW_SIGNAL)     */
#define KEYWORD_TRUST_DOMAIN_LEN      13 /* strlen(KEYWORD_TRUST_DOMAIN)     */
#define KEYWORD_ALLOW_READ_LEN        11 /* strlen(KEYWORD_ALLOW_READ)       */
#define KEYWORD_INITIALIZER_LEN       12 /* strlen(KEYWORD_INITIALIZER)      */
#define KEYWORD_FILE_PATTERN_LEN      13 /* strlen(KEYWORD_FILE_PATTERN)     */

#define KEYWORD_MAC_FOR_CAPABILITY      "MAC_FOR_CAPABILITY::"
#define KEYWORD_MAC_FOR_CAPABILITY_LEN  20 /* strlen(KEYWORD_MAC_FOR_CAPABILITY) */

/*************************  Index numbers for Access Controls.  *************************/

#define CCS_TOMOYO_MAC_FOR_FILE                  0  /* domain_policy.txt      */
#define CCS_TOMOYO_MAC_FOR_BINDPORT              1  /* domain_policy.txt      */
#define CCS_TOMOYO_MAC_FOR_CONNECTPORT           2  /* domain_policy.txt      */
#define CCS_TOMOYO_MAC_FOR_SIGNAL                3  /* domain_policy.txt      */
#define CCS_SAKURA_DENY_CONCEAL_MOUNT            4
#define CCS_SAKURA_RESTRICT_CHROOT               5  /* system_policy.txt      */
#define CCS_SAKURA_RESTRICT_MOUNT                6  /* system_policy.txt      */
#define CCS_SAKURA_RESTRICT_UNMOUNT              7  /* system_policy.txt      */
#define CCS_SAKURA_DENY_PIVOT_ROOT               8
#define CCS_SAKURA_TRACE_READONLY                9
#define CCS_SAKURA_RESTRICT_AUTOBIND            10  /* system_policy.txt      */
#define CCS_TOMOYO_MAX_ACCEPT_FILES             11
#define CCS_TOMOYO_MAX_GRANT_LOG                12
#define CCS_TOMOYO_MAX_REJECT_LOG               13
#define CCS_TOMOYO_VERBOSE                      14
#define CCS_MAX_ENFORCE_GRACE                   15

/*************************  The structure for /proc interfaces.  *************************/

typedef struct io_buffer {
	int (*read) (struct io_buffer *);
	struct semaphore read_sem;
	int (*write) (char *, void **);
	struct semaphore write_sem;
	int (*poll) (struct file *file, poll_table *wait);
	void *read_var1;                  /* The position currently reading from. */
	void *read_var2;                  /* Extra variables for reading.         */
	void *write_var1;                 /* The position currently writing to.   */
	int read_step;                    /* The step for reading.                */
	char *read_buf;                   /* Buffer for reading.                  */
	int read_eof;                     /* EOF flag for reading.                */
	int read_avail;                   /* Bytes available for reading.         */
	int readbuf_size;                 /* Size of read buffer.                 */
	int write_step;                   /* The step for writing.                */
	char *write_buf;                  /* Buffer for writing.                  */
	int write_avail;                  /* Bytes available for writing.         */
	int writebuf_size;                /* Size of write buffer.                */
} IO_BUFFER;

/*************************  PROTOTYPES  *************************/

const char *GetEXE(void);
const char *GetLastName(const struct domain_info *domain);
const char *GetMSG(const int is_enforce);
const char *acltype2keyword(const unsigned int acl_type);
const char *capability2keyword(const unsigned int capability);
int AddBindPolicy(char *data, void **domain);
int AddCapabilityPolicy(char *data, void **domain);
int AddChrootPolicy(char *data);
int AddConnectPolicy(char *data, void **domain);
int AddFilePolicy(char *data, void **domain);
int AddGloballyReadablePolicy(char *data);
int AddInitializerPolicy(char *data);
int AddMountPolicy(char *data);
int AddNoUmountPolicy(char *data);
int AddPatternPolicy(char *data);
int AddReservedPortPolicy(char *data);
int AddSignalPolicy(char *data, void **domain);
int AddTrustedPatternPolicy(char *data);
int CCS_CloseControl(struct file *file);
int CCS_OpenControl(const int type, struct file *file);
int CCS_PollControl(struct file *file, poll_table *wait);
int CCS_ReadControl(struct file *file, char __user *buffer, const int buffer_len);
int CCS_WriteControl(struct file *file, const char __user *buffer, const int buffer_len);
int CanSaveAuditLog(const int is_granted);
int CheckSupervisor(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
int DelChrootPolicy(const char *data);
int DelGloballyReadablePolicy(const char *data);
int DelInitializerPolicy(const char *data);
int DelMountPolicy(char *data);
int DelNoUmountPolicy(const char *data);
int DelPatternPolicy(const char *data);
int DelReservedPortPolicy(char *data);
int DelTrustedPatternPolicy(const char *data);
int DeleteDomain(char *data, void **dummy);
int IsCorrectPath(const char *filename, const int may_contain_pattern);
int IsDomainDef(const unsigned char *buffer);
int PathMatchesToPattern(const char *pathname, const char *pattern);
int PollGrantLog(struct file *file, poll_table *wait);
int PollRejectLog(struct file *file, poll_table *wait);
int ReadCapabilityStatus(IO_BUFFER *head);
int ReadChrootPolicy(IO_BUFFER *head);
int ReadDeletedPIDs(IO_BUFFER *head);
int ReadFileAndProcess(struct file *file, int (*func) (char *, void **));
int ReadGloballyReadablePolicy(IO_BUFFER *head);
int ReadGrantLog(IO_BUFFER *head);
int ReadInitializerPolicy(IO_BUFFER *head);
int ReadMountPolicy(IO_BUFFER *head);
int ReadNoUmountPolicy(IO_BUFFER *head);
int ReadPatternPolicy(IO_BUFFER *head);
int ReadPermissionMapping(IO_BUFFER *head);
int ReadRejectLog(IO_BUFFER *head);
int ReadReservedPortPolicy(IO_BUFFER *head);
int ReadSelfDomain(IO_BUFFER *head);
int ReadTrustedPIDs(IO_BUFFER *head);
int ReadTrustedPatternPolicy(IO_BUFFER *head);
int SetCapabilityStatus(const char *data, unsigned int value);
int SetPermissionMapping(char *data, void **dummy);
int UpdateDomain(char *data, void **dummy);
int WriteAuditLog(char *log, const int is_granted);
int acltype2paths(const unsigned int acl_type);
int io_printf(IO_BUFFER *head, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
int isRoot(void);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
void *kzalloc(const size_t size, const int flags);
#endif
int strendswith(const char *name, const char *tail);
struct domain_info *FindOrAssignNewDomain(const char *domainname);
struct domain_info *GetCurrentDomain(void);
unsigned int CheckCCSAccept(const unsigned int index);
unsigned int CheckCCSEnforce(const unsigned int index);
unsigned int CheckCCSFlags(const unsigned int index);
unsigned int GetMaxAutoAppendFiles(void);
unsigned int GetMaxGrantLog(void);
unsigned int GetMaxRejectLog(void);
unsigned int TomoyoVerboseMode(void);
void NormalizeLine(unsigned char *buffer);
void SetDomainAttribute(struct domain_info *domain, const unsigned int attribute);

#endif