/*
 * editpolicy.h
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * Version: 2.4.0-pre   2011/06/09
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */
#include <signal.h>
#include <curses.h>

enum tomoyo_screen_type {
	TOMOYO_SCREEN_EXCEPTION_LIST,
	TOMOYO_SCREEN_DOMAIN_LIST,
	TOMOYO_SCREEN_ACL_LIST,
	TOMOYO_SCREEN_PROFILE_LIST,
	TOMOYO_SCREEN_MANAGER_LIST,
	/* TOMOYO_SCREEN_QUERY_LIST, */
	TOMOYO_SCREEN_NS_LIST,
	TOMOYO_SCREEN_STAT_LIST,
	TOMOYO_MAXSCREEN
};

enum tomoyo_transition_type {
	/* Do not change this order, */
	TOMOYO_TRANSITION_CONTROL_NO_RESET,
	TOMOYO_TRANSITION_CONTROL_RESET,
	TOMOYO_TRANSITION_CONTROL_NO_INITIALIZE,
	TOMOYO_TRANSITION_CONTROL_INITIALIZE,
	TOMOYO_TRANSITION_CONTROL_NO_KEEP,
	TOMOYO_TRANSITION_CONTROL_KEEP,
	TOMOYO_MAX_TRANSITION_TYPE
};

enum tomoyo_editpolicy_directives {
	TOMOYO_DIRECTIVE_NONE,
	TOMOYO_DIRECTIVE_ACL_GROUP_000,
	TOMOYO_DIRECTIVE_ACL_GROUP_001,
	TOMOYO_DIRECTIVE_ACL_GROUP_002,
	TOMOYO_DIRECTIVE_ACL_GROUP_003,
	TOMOYO_DIRECTIVE_ACL_GROUP_004,
	TOMOYO_DIRECTIVE_ACL_GROUP_005,
	TOMOYO_DIRECTIVE_ACL_GROUP_006,
	TOMOYO_DIRECTIVE_ACL_GROUP_007,
	TOMOYO_DIRECTIVE_ACL_GROUP_008,
	TOMOYO_DIRECTIVE_ACL_GROUP_009,
	TOMOYO_DIRECTIVE_ACL_GROUP_010,
	TOMOYO_DIRECTIVE_ACL_GROUP_011,
	TOMOYO_DIRECTIVE_ACL_GROUP_012,
	TOMOYO_DIRECTIVE_ACL_GROUP_013,
	TOMOYO_DIRECTIVE_ACL_GROUP_014,
	TOMOYO_DIRECTIVE_ACL_GROUP_015,
	TOMOYO_DIRECTIVE_ACL_GROUP_016,
	TOMOYO_DIRECTIVE_ACL_GROUP_017,
	TOMOYO_DIRECTIVE_ACL_GROUP_018,
	TOMOYO_DIRECTIVE_ACL_GROUP_019,
	TOMOYO_DIRECTIVE_ACL_GROUP_020,
	TOMOYO_DIRECTIVE_ACL_GROUP_021,
	TOMOYO_DIRECTIVE_ACL_GROUP_022,
	TOMOYO_DIRECTIVE_ACL_GROUP_023,
	TOMOYO_DIRECTIVE_ACL_GROUP_024,
	TOMOYO_DIRECTIVE_ACL_GROUP_025,
	TOMOYO_DIRECTIVE_ACL_GROUP_026,
	TOMOYO_DIRECTIVE_ACL_GROUP_027,
	TOMOYO_DIRECTIVE_ACL_GROUP_028,
	TOMOYO_DIRECTIVE_ACL_GROUP_029,
	TOMOYO_DIRECTIVE_ACL_GROUP_030,
	TOMOYO_DIRECTIVE_ACL_GROUP_031,
	TOMOYO_DIRECTIVE_ACL_GROUP_032,
	TOMOYO_DIRECTIVE_ACL_GROUP_033,
	TOMOYO_DIRECTIVE_ACL_GROUP_034,
	TOMOYO_DIRECTIVE_ACL_GROUP_035,
	TOMOYO_DIRECTIVE_ACL_GROUP_036,
	TOMOYO_DIRECTIVE_ACL_GROUP_037,
	TOMOYO_DIRECTIVE_ACL_GROUP_038,
	TOMOYO_DIRECTIVE_ACL_GROUP_039,
	TOMOYO_DIRECTIVE_ACL_GROUP_040,
	TOMOYO_DIRECTIVE_ACL_GROUP_041,
	TOMOYO_DIRECTIVE_ACL_GROUP_042,
	TOMOYO_DIRECTIVE_ACL_GROUP_043,
	TOMOYO_DIRECTIVE_ACL_GROUP_044,
	TOMOYO_DIRECTIVE_ACL_GROUP_045,
	TOMOYO_DIRECTIVE_ACL_GROUP_046,
	TOMOYO_DIRECTIVE_ACL_GROUP_047,
	TOMOYO_DIRECTIVE_ACL_GROUP_048,
	TOMOYO_DIRECTIVE_ACL_GROUP_049,
	TOMOYO_DIRECTIVE_ACL_GROUP_050,
	TOMOYO_DIRECTIVE_ACL_GROUP_051,
	TOMOYO_DIRECTIVE_ACL_GROUP_052,
	TOMOYO_DIRECTIVE_ACL_GROUP_053,
	TOMOYO_DIRECTIVE_ACL_GROUP_054,
	TOMOYO_DIRECTIVE_ACL_GROUP_055,
	TOMOYO_DIRECTIVE_ACL_GROUP_056,
	TOMOYO_DIRECTIVE_ACL_GROUP_057,
	TOMOYO_DIRECTIVE_ACL_GROUP_058,
	TOMOYO_DIRECTIVE_ACL_GROUP_059,
	TOMOYO_DIRECTIVE_ACL_GROUP_060,
	TOMOYO_DIRECTIVE_ACL_GROUP_061,
	TOMOYO_DIRECTIVE_ACL_GROUP_062,
	TOMOYO_DIRECTIVE_ACL_GROUP_063,
	TOMOYO_DIRECTIVE_ACL_GROUP_064,
	TOMOYO_DIRECTIVE_ACL_GROUP_065,
	TOMOYO_DIRECTIVE_ACL_GROUP_066,
	TOMOYO_DIRECTIVE_ACL_GROUP_067,
	TOMOYO_DIRECTIVE_ACL_GROUP_068,
	TOMOYO_DIRECTIVE_ACL_GROUP_069,
	TOMOYO_DIRECTIVE_ACL_GROUP_070,
	TOMOYO_DIRECTIVE_ACL_GROUP_071,
	TOMOYO_DIRECTIVE_ACL_GROUP_072,
	TOMOYO_DIRECTIVE_ACL_GROUP_073,
	TOMOYO_DIRECTIVE_ACL_GROUP_074,
	TOMOYO_DIRECTIVE_ACL_GROUP_075,
	TOMOYO_DIRECTIVE_ACL_GROUP_076,
	TOMOYO_DIRECTIVE_ACL_GROUP_077,
	TOMOYO_DIRECTIVE_ACL_GROUP_078,
	TOMOYO_DIRECTIVE_ACL_GROUP_079,
	TOMOYO_DIRECTIVE_ACL_GROUP_080,
	TOMOYO_DIRECTIVE_ACL_GROUP_081,
	TOMOYO_DIRECTIVE_ACL_GROUP_082,
	TOMOYO_DIRECTIVE_ACL_GROUP_083,
	TOMOYO_DIRECTIVE_ACL_GROUP_084,
	TOMOYO_DIRECTIVE_ACL_GROUP_085,
	TOMOYO_DIRECTIVE_ACL_GROUP_086,
	TOMOYO_DIRECTIVE_ACL_GROUP_087,
	TOMOYO_DIRECTIVE_ACL_GROUP_088,
	TOMOYO_DIRECTIVE_ACL_GROUP_089,
	TOMOYO_DIRECTIVE_ACL_GROUP_090,
	TOMOYO_DIRECTIVE_ACL_GROUP_091,
	TOMOYO_DIRECTIVE_ACL_GROUP_092,
	TOMOYO_DIRECTIVE_ACL_GROUP_093,
	TOMOYO_DIRECTIVE_ACL_GROUP_094,
	TOMOYO_DIRECTIVE_ACL_GROUP_095,
	TOMOYO_DIRECTIVE_ACL_GROUP_096,
	TOMOYO_DIRECTIVE_ACL_GROUP_097,
	TOMOYO_DIRECTIVE_ACL_GROUP_098,
	TOMOYO_DIRECTIVE_ACL_GROUP_099,
	TOMOYO_DIRECTIVE_ACL_GROUP_100,
	TOMOYO_DIRECTIVE_ACL_GROUP_101,
	TOMOYO_DIRECTIVE_ACL_GROUP_102,
	TOMOYO_DIRECTIVE_ACL_GROUP_103,
	TOMOYO_DIRECTIVE_ACL_GROUP_104,
	TOMOYO_DIRECTIVE_ACL_GROUP_105,
	TOMOYO_DIRECTIVE_ACL_GROUP_106,
	TOMOYO_DIRECTIVE_ACL_GROUP_107,
	TOMOYO_DIRECTIVE_ACL_GROUP_108,
	TOMOYO_DIRECTIVE_ACL_GROUP_109,
	TOMOYO_DIRECTIVE_ACL_GROUP_110,
	TOMOYO_DIRECTIVE_ACL_GROUP_111,
	TOMOYO_DIRECTIVE_ACL_GROUP_112,
	TOMOYO_DIRECTIVE_ACL_GROUP_113,
	TOMOYO_DIRECTIVE_ACL_GROUP_114,
	TOMOYO_DIRECTIVE_ACL_GROUP_115,
	TOMOYO_DIRECTIVE_ACL_GROUP_116,
	TOMOYO_DIRECTIVE_ACL_GROUP_117,
	TOMOYO_DIRECTIVE_ACL_GROUP_118,
	TOMOYO_DIRECTIVE_ACL_GROUP_119,
	TOMOYO_DIRECTIVE_ACL_GROUP_120,
	TOMOYO_DIRECTIVE_ACL_GROUP_121,
	TOMOYO_DIRECTIVE_ACL_GROUP_122,
	TOMOYO_DIRECTIVE_ACL_GROUP_123,
	TOMOYO_DIRECTIVE_ACL_GROUP_124,
	TOMOYO_DIRECTIVE_ACL_GROUP_125,
	TOMOYO_DIRECTIVE_ACL_GROUP_126,
	TOMOYO_DIRECTIVE_ACL_GROUP_127,
	TOMOYO_DIRECTIVE_ACL_GROUP_128,
	TOMOYO_DIRECTIVE_ACL_GROUP_129,
	TOMOYO_DIRECTIVE_ACL_GROUP_130,
	TOMOYO_DIRECTIVE_ACL_GROUP_131,
	TOMOYO_DIRECTIVE_ACL_GROUP_132,
	TOMOYO_DIRECTIVE_ACL_GROUP_133,
	TOMOYO_DIRECTIVE_ACL_GROUP_134,
	TOMOYO_DIRECTIVE_ACL_GROUP_135,
	TOMOYO_DIRECTIVE_ACL_GROUP_136,
	TOMOYO_DIRECTIVE_ACL_GROUP_137,
	TOMOYO_DIRECTIVE_ACL_GROUP_138,
	TOMOYO_DIRECTIVE_ACL_GROUP_139,
	TOMOYO_DIRECTIVE_ACL_GROUP_140,
	TOMOYO_DIRECTIVE_ACL_GROUP_141,
	TOMOYO_DIRECTIVE_ACL_GROUP_142,
	TOMOYO_DIRECTIVE_ACL_GROUP_143,
	TOMOYO_DIRECTIVE_ACL_GROUP_144,
	TOMOYO_DIRECTIVE_ACL_GROUP_145,
	TOMOYO_DIRECTIVE_ACL_GROUP_146,
	TOMOYO_DIRECTIVE_ACL_GROUP_147,
	TOMOYO_DIRECTIVE_ACL_GROUP_148,
	TOMOYO_DIRECTIVE_ACL_GROUP_149,
	TOMOYO_DIRECTIVE_ACL_GROUP_150,
	TOMOYO_DIRECTIVE_ACL_GROUP_151,
	TOMOYO_DIRECTIVE_ACL_GROUP_152,
	TOMOYO_DIRECTIVE_ACL_GROUP_153,
	TOMOYO_DIRECTIVE_ACL_GROUP_154,
	TOMOYO_DIRECTIVE_ACL_GROUP_155,
	TOMOYO_DIRECTIVE_ACL_GROUP_156,
	TOMOYO_DIRECTIVE_ACL_GROUP_157,
	TOMOYO_DIRECTIVE_ACL_GROUP_158,
	TOMOYO_DIRECTIVE_ACL_GROUP_159,
	TOMOYO_DIRECTIVE_ACL_GROUP_160,
	TOMOYO_DIRECTIVE_ACL_GROUP_161,
	TOMOYO_DIRECTIVE_ACL_GROUP_162,
	TOMOYO_DIRECTIVE_ACL_GROUP_163,
	TOMOYO_DIRECTIVE_ACL_GROUP_164,
	TOMOYO_DIRECTIVE_ACL_GROUP_165,
	TOMOYO_DIRECTIVE_ACL_GROUP_166,
	TOMOYO_DIRECTIVE_ACL_GROUP_167,
	TOMOYO_DIRECTIVE_ACL_GROUP_168,
	TOMOYO_DIRECTIVE_ACL_GROUP_169,
	TOMOYO_DIRECTIVE_ACL_GROUP_170,
	TOMOYO_DIRECTIVE_ACL_GROUP_171,
	TOMOYO_DIRECTIVE_ACL_GROUP_172,
	TOMOYO_DIRECTIVE_ACL_GROUP_173,
	TOMOYO_DIRECTIVE_ACL_GROUP_174,
	TOMOYO_DIRECTIVE_ACL_GROUP_175,
	TOMOYO_DIRECTIVE_ACL_GROUP_176,
	TOMOYO_DIRECTIVE_ACL_GROUP_177,
	TOMOYO_DIRECTIVE_ACL_GROUP_178,
	TOMOYO_DIRECTIVE_ACL_GROUP_179,
	TOMOYO_DIRECTIVE_ACL_GROUP_180,
	TOMOYO_DIRECTIVE_ACL_GROUP_181,
	TOMOYO_DIRECTIVE_ACL_GROUP_182,
	TOMOYO_DIRECTIVE_ACL_GROUP_183,
	TOMOYO_DIRECTIVE_ACL_GROUP_184,
	TOMOYO_DIRECTIVE_ACL_GROUP_185,
	TOMOYO_DIRECTIVE_ACL_GROUP_186,
	TOMOYO_DIRECTIVE_ACL_GROUP_187,
	TOMOYO_DIRECTIVE_ACL_GROUP_188,
	TOMOYO_DIRECTIVE_ACL_GROUP_189,
	TOMOYO_DIRECTIVE_ACL_GROUP_190,
	TOMOYO_DIRECTIVE_ACL_GROUP_191,
	TOMOYO_DIRECTIVE_ACL_GROUP_192,
	TOMOYO_DIRECTIVE_ACL_GROUP_193,
	TOMOYO_DIRECTIVE_ACL_GROUP_194,
	TOMOYO_DIRECTIVE_ACL_GROUP_195,
	TOMOYO_DIRECTIVE_ACL_GROUP_196,
	TOMOYO_DIRECTIVE_ACL_GROUP_197,
	TOMOYO_DIRECTIVE_ACL_GROUP_198,
	TOMOYO_DIRECTIVE_ACL_GROUP_199,
	TOMOYO_DIRECTIVE_ACL_GROUP_200,
	TOMOYO_DIRECTIVE_ACL_GROUP_201,
	TOMOYO_DIRECTIVE_ACL_GROUP_202,
	TOMOYO_DIRECTIVE_ACL_GROUP_203,
	TOMOYO_DIRECTIVE_ACL_GROUP_204,
	TOMOYO_DIRECTIVE_ACL_GROUP_205,
	TOMOYO_DIRECTIVE_ACL_GROUP_206,
	TOMOYO_DIRECTIVE_ACL_GROUP_207,
	TOMOYO_DIRECTIVE_ACL_GROUP_208,
	TOMOYO_DIRECTIVE_ACL_GROUP_209,
	TOMOYO_DIRECTIVE_ACL_GROUP_210,
	TOMOYO_DIRECTIVE_ACL_GROUP_211,
	TOMOYO_DIRECTIVE_ACL_GROUP_212,
	TOMOYO_DIRECTIVE_ACL_GROUP_213,
	TOMOYO_DIRECTIVE_ACL_GROUP_214,
	TOMOYO_DIRECTIVE_ACL_GROUP_215,
	TOMOYO_DIRECTIVE_ACL_GROUP_216,
	TOMOYO_DIRECTIVE_ACL_GROUP_217,
	TOMOYO_DIRECTIVE_ACL_GROUP_218,
	TOMOYO_DIRECTIVE_ACL_GROUP_219,
	TOMOYO_DIRECTIVE_ACL_GROUP_220,
	TOMOYO_DIRECTIVE_ACL_GROUP_221,
	TOMOYO_DIRECTIVE_ACL_GROUP_222,
	TOMOYO_DIRECTIVE_ACL_GROUP_223,
	TOMOYO_DIRECTIVE_ACL_GROUP_224,
	TOMOYO_DIRECTIVE_ACL_GROUP_225,
	TOMOYO_DIRECTIVE_ACL_GROUP_226,
	TOMOYO_DIRECTIVE_ACL_GROUP_227,
	TOMOYO_DIRECTIVE_ACL_GROUP_228,
	TOMOYO_DIRECTIVE_ACL_GROUP_229,
	TOMOYO_DIRECTIVE_ACL_GROUP_230,
	TOMOYO_DIRECTIVE_ACL_GROUP_231,
	TOMOYO_DIRECTIVE_ACL_GROUP_232,
	TOMOYO_DIRECTIVE_ACL_GROUP_233,
	TOMOYO_DIRECTIVE_ACL_GROUP_234,
	TOMOYO_DIRECTIVE_ACL_GROUP_235,
	TOMOYO_DIRECTIVE_ACL_GROUP_236,
	TOMOYO_DIRECTIVE_ACL_GROUP_237,
	TOMOYO_DIRECTIVE_ACL_GROUP_238,
	TOMOYO_DIRECTIVE_ACL_GROUP_239,
	TOMOYO_DIRECTIVE_ACL_GROUP_240,
	TOMOYO_DIRECTIVE_ACL_GROUP_241,
	TOMOYO_DIRECTIVE_ACL_GROUP_242,
	TOMOYO_DIRECTIVE_ACL_GROUP_243,
	TOMOYO_DIRECTIVE_ACL_GROUP_244,
	TOMOYO_DIRECTIVE_ACL_GROUP_245,
	TOMOYO_DIRECTIVE_ACL_GROUP_246,
	TOMOYO_DIRECTIVE_ACL_GROUP_247,
	TOMOYO_DIRECTIVE_ACL_GROUP_248,
	TOMOYO_DIRECTIVE_ACL_GROUP_249,
	TOMOYO_DIRECTIVE_ACL_GROUP_250,
	TOMOYO_DIRECTIVE_ACL_GROUP_251,
	TOMOYO_DIRECTIVE_ACL_GROUP_252,
	TOMOYO_DIRECTIVE_ACL_GROUP_253,
	TOMOYO_DIRECTIVE_ACL_GROUP_254,
	TOMOYO_DIRECTIVE_ACL_GROUP_255,
	TOMOYO_DIRECTIVE_ADDRESS_GROUP,
	TOMOYO_DIRECTIVE_AGGREGATOR,
	TOMOYO_DIRECTIVE_CAPABILITY,
	TOMOYO_DIRECTIVE_DENY_AUTOBIND,
	TOMOYO_DIRECTIVE_FILE_APPEND,
	TOMOYO_DIRECTIVE_FILE_CHGRP,
	TOMOYO_DIRECTIVE_FILE_CHMOD,
	TOMOYO_DIRECTIVE_FILE_CHOWN,
	TOMOYO_DIRECTIVE_FILE_CHROOT,
	TOMOYO_DIRECTIVE_FILE_CREATE,
	TOMOYO_DIRECTIVE_FILE_EXECUTE,
	TOMOYO_DIRECTIVE_FILE_GETATTR,
	TOMOYO_DIRECTIVE_FILE_IOCTL,
	TOMOYO_DIRECTIVE_FILE_LINK,
	TOMOYO_DIRECTIVE_FILE_MKBLOCK,
	TOMOYO_DIRECTIVE_FILE_MKCHAR,
	TOMOYO_DIRECTIVE_FILE_MKDIR,
	TOMOYO_DIRECTIVE_FILE_MKFIFO,
	TOMOYO_DIRECTIVE_FILE_MKSOCK,
	TOMOYO_DIRECTIVE_FILE_MOUNT,
	TOMOYO_DIRECTIVE_FILE_PIVOT_ROOT,
	TOMOYO_DIRECTIVE_FILE_READ,
	TOMOYO_DIRECTIVE_FILE_RENAME,
	TOMOYO_DIRECTIVE_FILE_RMDIR,
	TOMOYO_DIRECTIVE_FILE_SYMLINK,
	TOMOYO_DIRECTIVE_FILE_TRUNCATE,
	TOMOYO_DIRECTIVE_FILE_UNLINK,
	TOMOYO_DIRECTIVE_FILE_UNMOUNT,
	TOMOYO_DIRECTIVE_FILE_WRITE,
	TOMOYO_DIRECTIVE_INITIALIZE_DOMAIN,
	TOMOYO_DIRECTIVE_IPC_SIGNAL,
	TOMOYO_DIRECTIVE_KEEP_DOMAIN,
	TOMOYO_DIRECTIVE_MISC_ENV,
	TOMOYO_DIRECTIVE_NETWORK_INET,
	TOMOYO_DIRECTIVE_NETWORK_UNIX,
	TOMOYO_DIRECTIVE_NO_INITIALIZE_DOMAIN,
	TOMOYO_DIRECTIVE_NO_KEEP_DOMAIN,
	TOMOYO_DIRECTIVE_NO_RESET_DOMAIN,
	TOMOYO_DIRECTIVE_NUMBER_GROUP,
	TOMOYO_DIRECTIVE_PATH_GROUP,
	TOMOYO_DIRECTIVE_QUOTA_EXCEEDED,
	TOMOYO_DIRECTIVE_RESET_DOMAIN,
	TOMOYO_DIRECTIVE_TASK_AUTO_DOMAIN_TRANSITION,
	TOMOYO_DIRECTIVE_TASK_AUTO_EXECUTE_HANDLER,
	TOMOYO_DIRECTIVE_TASK_DENIED_EXECUTE_HANDLER,
	TOMOYO_DIRECTIVE_TASK_MANUAL_DOMAIN_TRANSITION,
	TOMOYO_DIRECTIVE_TRANSITION_FAILED,
	TOMOYO_DIRECTIVE_USE_GROUP,
	TOMOYO_DIRECTIVE_USE_PROFILE,
	TOMOYO_MAX_DIRECTIVE_INDEX
};

enum tomoyo_color_pair {
	TOMOYO_NORMAL,
	TOMOYO_DOMAIN_HEAD,
	TOMOYO_DOMAIN_CURSOR,
	TOMOYO_EXCEPTION_HEAD,
	TOMOYO_EXCEPTION_CURSOR,
	TOMOYO_ACL_HEAD,
	TOMOYO_ACL_CURSOR,
	TOMOYO_PROFILE_HEAD,
	TOMOYO_PROFILE_CURSOR,
	TOMOYO_MANAGER_HEAD,
	TOMOYO_MANAGER_CURSOR,
	TOMOYO_STAT_HEAD,
	TOMOYO_STAT_CURSOR,
	TOMOYO_DEFAULT_COLOR,
	TOMOYO_DISP_ERR
};

struct tomoyo_transition_control_entry {
	const struct tomoyo_path_info *domainname;    /* This may be NULL */
	const struct tomoyo_path_info *program;       /* This may be NULL */
	u8 type;
	_Bool is_last_name;
};

struct tomoyo_generic_acl {
	enum tomoyo_editpolicy_directives directive;
	u8 selected;
	const char *operand;
};

struct tomoyo_editpolicy_directive {
	const char *original;
	const char *alias;
	int original_len;
	int alias_len;
};

struct tomoyo_misc_policy {
	const struct tomoyo_path_info **list;
	int list_len;
};

struct tomoyo_path_group_entry {
	const struct tomoyo_path_info *group_name;
	const struct tomoyo_path_info **member_name;
	int member_name_len;
};

struct tomoyo_readline_data {
	const char **history;
	int count;
	int max;
	char *search_buffer[TOMOYO_MAXSCREEN];
};

struct tomoyo_screen {
	/* Index of currently selected line on each screen. */
	int current;
	/* Current cursor position on CUI screen. */
	int y;
	/* Columns to shift when displaying. */
	int x;
	/* For tomoyo_editpolicy_line_draw(). */
	int saved_color_current; /* Initialized to -1 */
	int saved_color_y;
};

#define TOMOYO_HEADER_LINES 3

#define TOMOYO_EDITPOLICY_CONF "/etc/tomoyo/tools/editpolicy.conf"

enum tomoyo_color_pair tomoyo_editpolicy_color_head(void);
enum tomoyo_editpolicy_directives tomoyo_find_directive(const _Bool forward,
						  char *line);
int tomoyo_add_address_group_policy(char *data, const _Bool is_delete);
int tomoyo_add_number_group_policy(char *data, const _Bool is_delete);
int tomoyo_editpolicy_get_current(void);
void tomoyo_editpolicy_attr_change(const attr_t attr, const _Bool flg);
void tomoyo_editpolicy_clear_groups(void);
void tomoyo_editpolicy_color_change(const attr_t attr, const _Bool flg);
void tomoyo_editpolicy_color_init(void);
void tomoyo_editpolicy_init_keyword_map(void);
void tomoyo_editpolicy_line_draw(void);
void tomoyo_editpolicy_offline_daemon(const int listener);
void tomoyo_editpolicy_optimize(const int current);
void tomoyo_editpolicy_sttr_restore(void);
void tomoyo_editpolicy_sttr_save(void);

extern enum tomoyo_screen_type tomoyo_current_screen;
extern int tomoyo_list_item_count;
extern int tomoyo_path_group_list_len;
extern struct tomoyo_domain_policy tomoyo_dp;
extern struct tomoyo_editpolicy_directive tomoyo_directives[TOMOYO_MAX_DIRECTIVE_INDEX];
extern struct tomoyo_generic_acl *tomoyo_gacl_list;
extern struct tomoyo_path_group_entry *tomoyo_path_group_list;
extern struct tomoyo_screen tomoyo_screen[TOMOYO_MAXSCREEN];
