/*
 * tomoyotools.c
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
#include "tomoyotools.h"

struct tomoyo_savename_entry {
	struct tomoyo_savename_entry *next;
	struct tomoyo_path_info entry;
};

#define TOMOYO_SAVENAME_MAX_HASH            256

/* Use tomoyo-editpolicy-agent process? */
_Bool tomoyo_network_mode = false;
/* The IPv4 address of the remote host running the tomoyo-editpolicy-agent . */
u32 tomoyo_network_ip = INADDR_NONE;
/* The port number of the remote host running the tomoyo-editpolicy-agent . */
u16 tomoyo_network_port = 0;
/* The list of processes currently running. */
struct tomoyo_task_entry *tomoyo_task_list = NULL;
/* The length of tomoyo_task_list . */
int tomoyo_task_list_len = 0;
/* Read files without calling tomoyo_normalize_line() ? */
_Bool tomoyo_freadline_raw = false;

/* Prototypes */

static _Bool tomoyo_byte_range(const char *str);
static _Bool tomoyo_decimal(const char c);
static _Bool tomoyo_hexadecimal(const char c);
static _Bool tomoyo_alphabet_char(const char c);
static u8 tomoyo_make_byte(const u8 c1, const u8 c2, const u8 c3);
static int tomoyo_const_part_length(const char *filename);
static int tomoyo_domainname_compare(const void *a, const void *b);
static int tomoyo_path_info_compare(const void *a, const void *b);
static void tomoyo_sort_domain_policy(struct tomoyo_domain_policy *dp);

/* Utility functions */

/**
 * tomoyo_out_of_memory - Print error message and abort.
 *
 * This function does not return.
 */
static void tomoyo_out_of_memory(void)
{
	fprintf(stderr, "Out of memory. Aborted.\n");
	exit(1);
}

/**
 * tomoyo_strdup - strdup() with abort on error.
 *
 * @string: String to duplicate.
 *
 * Returns copy of @string on success, abort otherwise.
 */
char *tomoyo_strdup(const char *string)
{
	char *cp = strdup(string);
	if (!cp)
		tomoyo_out_of_memory();
	return cp;
}

/**
 * tomoyo_realloc - realloc() with abort on error.
 *
 * @ptr:  Pointer to void.
 * @size: New size.
 *
 * Returns return value of realloc() on success, abort otherwise.
 */
void *tomoyo_realloc(void *ptr, const size_t size)
{
	void *vp = realloc(ptr, size);
	if (!vp)
		tomoyo_out_of_memory();
	return vp;
}

/**
 * tomoyo_realloc2 - realloc() with abort on error.
 *
 * @ptr:  Pointer to void.
 * @size: New size.
 *
 * Returns return value of realloc() on success, abort otherwise.
 *
 * Allocated memory is cleared with 0.
 */
void *tomoyo_realloc2(void *ptr, const size_t size)
{
	void *vp = tomoyo_realloc(ptr, size);
	memset(vp, 0, size);
	return vp;
}

/**
 * tomoyo_malloc - malloc() with abort on error.
 *
 * @size: Size to allocate.
 *
 * Returns return value of malloc() on success, abort otherwise.
 *
 * Allocated memory is cleared with 0.
 */
void *tomoyo_malloc(const size_t size)
{
	void *vp = malloc(size);
	if (!vp)
		tomoyo_out_of_memory();
	memset(vp, 0, size);
	return vp;
}

/**
 * tomoyo_str_starts - Check whether the given string starts with the given keyword.
 *
 * @str:   Pointer to "char *".
 * @begin: Pointer to "const char *".
 *
 * Returns true if @str starts with @begin, false otherwise.
 *
 * Note that @begin will be removed from @str before returning true. Therefore,
 * @str must not be "const char *".
 *
 * Note that this function in kernel source has different arguments and behaves
 * differently.
 */
_Bool tomoyo_str_starts(char *str, const char *begin)
{
	const int len = strlen(begin);
	if (strncmp(str, begin, len))
		return false;
	memmove(str, str + len, strlen(str + len) + 1);
	return true;
}

/**
 * tomoyo_byte_range - Check whether the string is a \ooo style octal value.
 *
 * @str: Pointer to the string.
 *
 * Returns true if @str is a \ooo style octal value, false otherwise.
 */
static _Bool tomoyo_byte_range(const char *str)
{
	return *str >= '0' && *str++ <= '3' &&
		*str >= '0' && *str++ <= '7' &&
		*str >= '0' && *str <= '7';
}

/**
 * tomoyo_decimal - Check whether the character is a decimal character.
 *
 * @c: The character to check.
 *
 * Returns true if @c is a decimal character, false otherwise.
 */
static _Bool tomoyo_decimal(const char c)
{
	return c >= '0' && c <= '9';
}

/**
 * tomoyo_hexadecimal - Check whether the character is a hexadecimal character.
 *
 * @c: The character to check.
 *
 * Returns true if @c is a hexadecimal character, false otherwise.
 */
static _Bool tomoyo_hexadecimal(const char c)
{
	return (c >= '0' && c <= '9') ||
		(c >= 'A' && c <= 'F') ||
		(c >= 'a' && c <= 'f');
}

/**
 * tomoyo_alphabet_char - Check whether the character is an alphabet.
 *
 * @c: The character to check.
 *
 * Returns true if @c is an alphabet character, false otherwise.
 */
static _Bool tomoyo_alphabet_char(const char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * tomoyo_make_byte - Make byte value from three octal characters.
 *
 * @c1: The first character.
 * @c2: The second character.
 * @c3: The third character.
 *
 * Returns byte value.
 */
static u8 tomoyo_make_byte(const u8 c1, const u8 c2, const u8 c3)
{
	return ((c1 - '0') << 6) + ((c2 - '0') << 3) + (c3 - '0');
}

/**
 * tomoyo_normalize_line - Format string.
 *
 * @buffer: The line to normalize.
 *
 * Returns nothing.
 *
 * Leading and trailing whitespaces are removed.
 * Multiple whitespaces are packed into single space.
 */
void tomoyo_normalize_line(char *buffer)
{
	unsigned char *sp = (unsigned char *) buffer;
	unsigned char *dp = (unsigned char *) buffer;
	_Bool first = true;
	while (*sp && (*sp <= ' ' || 127 <= *sp))
		sp++;
	while (*sp) {
		if (!first)
			*dp++ = ' ';
		first = false;
		while (' ' < *sp && *sp < 127)
			*dp++ = *sp++;
		while (*sp && (*sp <= ' ' || 127 <= *sp))
			sp++;
	}
	*dp = '\0';
}

/**
 * tomoyo_partial_name_hash - Hash name.
 *
 * @c:        A unsigned long value.
 * @prevhash: A previous hash value.
 *
 * Returns new hash value.
 *
 * This function is copied from partial_name_hash() in the kernel source.
 */
static inline unsigned long tomoyo_partial_name_hash(unsigned long c,
						  unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/**
 * tomoyo_full_name_hash - Hash full name.
 *
 * @name: Pointer to "const unsigned char".
 * @len:  Length of @name in byte.
 *
 * Returns hash value.
 *
 * This function is copied from full_name_hash() in the kernel source.
 */
static inline unsigned int tomoyo_full_name_hash(const unsigned char *name,
					      unsigned int len)
{
	unsigned long hash = 0;
	while (len--)
		hash = tomoyo_partial_name_hash(*name++, hash);
	return (unsigned int) hash;
}

/**
 * tomoyo_const_part_length - Evaluate the initial length without a pattern in a token.
 *
 * @filename: The string to evaluate.
 *
 * Returns the initial length without a pattern in @filename.
 */
static int tomoyo_const_part_length(const char *filename)
{
	int len = 0;
	if (filename) {
		while (true) {
			char c = *filename++;
			if (!c)
				break;
			if (c != '\\') {
				len++;
				continue;
			}
			c = *filename++;
			switch (c) {
			case '\\':  /* "\\" */
				len += 2;
				continue;
			case '0':   /* "\ooo" */
			case '1':
			case '2':
			case '3':
				c = *filename++;
				if (c < '0' || c > '7')
					break;
				c = *filename++;
				if (c < '0' || c > '7')
					break;
				len += 4;
				continue;
			}
			break;
		}
	}
	return len;
}

/**
 * tomoyo_fprintf_encoded - fprintf() using TOMOYO's escape rules.
 *
 * @fp:       Pointer to "FILE".
 * @pathname: String to print.
 *
 * Returns nothing.
 */
void tomoyo_fprintf_encoded(FILE *fp, const char *pathname)
{
	while (true) {
		unsigned char c = *(const unsigned char *) pathname++;
		if (!c)
			break;
		if (c == '\\') {
			fputc('\\', fp);
			fputc('\\', fp);
		} else if (c > ' ' && c < 127) {
			fputc(c, fp);
		} else {
			fprintf(fp, "\\%c%c%c", (c >> 6) + '0',
				((c >> 3) & 7) + '0', (c & 7) + '0');
		}
	}
}

/**
 * tomoyo_decode - Decode a string in TOMOYO's rule to a string in C.
 *
 * @ascii: Pointer to "const char".
 * @bin:   Pointer to "char". Must not contain wildcards nor '\000'.
 *
 * Returns true if @ascii was successfully decoded, false otherwise.
 *
 * Note that it is legal to pass @ascii == @bin if the caller want to decode
 * a string in a temporary buffer.
 */
_Bool tomoyo_decode(const char *ascii, char *bin)
{
	while (true) {
		char c = *ascii++;
		*bin++ = c;
		if (!c)
			break;
		if (c == '\\') {
			char d;
			char e;
			u8 f;
			c = *ascii++;
			switch (c) {
			case '\\':      /* "\\" */
				continue;
			case '0':       /* "\ooo" */
			case '1':
			case '2':
			case '3':
				d = *ascii++;
				if (d < '0' || d > '7')
					break;
				e = *ascii++;
				if (e < '0' || e > '7')
					break;
				f = (u8) ((c - '0') << 6) +
					(((u8) (d - '0')) << 3) +
					(((u8) (e - '0')));
				if (f <= ' ' || f >= 127) {
					*(bin - 1) = f;
					continue;
				}
			}
			return false;
		} else if (c <= ' ' || c >= 127) {
			return false;
		}
	}
	return true;
}

/**
 * tomoyo_correct_word2 - Check whether the given string follows the naming rules.
 *
 * @string: The byte sequence to check. Not '\0'-terminated.
 * @len:    Length of @string.
 *
 * Returns true if @string follows the naming rules, false otherwise.
 */
static _Bool tomoyo_correct_word2(const char *string, size_t len)
{
	const char *const start = string;
	_Bool in_repetition = false;
	unsigned char c;
	unsigned char d;
	unsigned char e;
	if (!len)
		goto out;
	while (len--) {
		c = *string++;
		if (c == '\\') {
			if (!len--)
				goto out;
			c = *string++;
			switch (c) {
			case '\\':  /* "\\" */
				continue;
			case '$':   /* "\$" */
			case '+':   /* "\+" */
			case '?':   /* "\?" */
			case '*':   /* "\*" */
			case '@':   /* "\@" */
			case 'x':   /* "\x" */
			case 'X':   /* "\X" */
			case 'a':   /* "\a" */
			case 'A':   /* "\A" */
			case '-':   /* "\-" */
				continue;
			case '{':   /* "/\{" */
				if (string - 3 < start || *(string - 3) != '/')
					break;
				in_repetition = true;
				continue;
			case '}':   /* "\}/" */
				if (*string != '/')
					break;
				if (!in_repetition)
					break;
				in_repetition = false;
				continue;
			case '0':   /* "\ooo" */
			case '1':
			case '2':
			case '3':
				if (!len-- || !len--)
					break;
				d = *string++;
				e = *string++;
				if (d < '0' || d > '7' || e < '0' || e > '7')
					break;
				c = tomoyo_make_byte(c, d, e);
				if (c <= ' ' || c >= 127)
					continue;
			}
			goto out;
		} else if (in_repetition && c == '/') {
			goto out;
		} else if (c <= ' ' || c >= 127) {
			goto out;
		}
	}
	if (in_repetition)
		goto out;
	return true;
out:
	return false;
}

/**
 * tomoyo_correct_word - Check whether the given string follows the naming rules.
 *
 * @string: The string to check.
 *
 * Returns true if @string follows the naming rules, false otherwise.
 */
_Bool tomoyo_correct_word(const char *string)
{
	return tomoyo_correct_word2(string, strlen(string));
}

/**
 * tomoyo_correct_path - Check whether the given pathname follows the naming rules.
 *
 * @filename: The pathname to check.
 *
 * Returns true if @filename follows the naming rules, false otherwise.
 */
_Bool tomoyo_correct_path(const char *filename)
{
	return *filename == '/' && tomoyo_correct_word(filename);
}

/**
 * tomoyo_domain_def - Check whether the given token can be a domainname.
 *
 * @buffer: The token to check.
 *
 * Returns true if @buffer possibly be a domainname, false otherwise.
 */
_Bool tomoyo_domain_def(const char *buffer)
{
	const char *cp;
	int len;
	if (*buffer != '<')
		return false;
	cp = strchr(buffer, ' ');
	if (!cp)
		len = strlen(buffer);
	else
		len = cp - buffer;
	return buffer[len - 1] == '>' &&
		tomoyo_correct_word2(buffer + 1, len - 2);
}

/**
 * tomoyo_correct_domain - Check whether the given domainname follows the naming rules.
 *
 * @domainname: The domainname to check.
 *
 * Returns true if @domainname follows the naming rules, false otherwise.
 */
_Bool tomoyo_correct_domain(const char *domainname)
{
	if (!domainname || !tomoyo_domain_def(domainname))
		return false;
	domainname = strchr(domainname, ' ');
	if (!domainname++)
		return true;
	while (1) {
		const char *cp = strchr(domainname, ' ');
		if (!cp)
			break;
		if (*domainname != '/' ||
		    !tomoyo_correct_word2(domainname, cp - domainname))
			goto out;
		domainname = cp + 1;
	}
	return tomoyo_correct_path(domainname);
out:
	return false;
}

/**
 * tomoyo_file_matches_pattern2 - Pattern matching without '/' character and "\-" pattern.
 *
 * @filename:     The start of string to check.
 * @filename_end: The end of string to check.
 * @pattern:      The start of pattern to compare.
 * @pattern_end:  The end of pattern to compare.
 *
 * Returns true if @filename matches @pattern, false otherwise.
 */
static _Bool tomoyo_file_matches_pattern2(const char *filename,
				       const char *filename_end,
				       const char *pattern,
				       const char *pattern_end)
{
	while (filename < filename_end && pattern < pattern_end) {
		char c;
		if (*pattern != '\\') {
			if (*filename++ != *pattern++)
				return false;
			continue;
		}
		c = *filename;
		pattern++;
		switch (*pattern) {
			int i;
			int j;
		case '?':
			if (c == '/') {
				return false;
			} else if (c == '\\') {
				if (filename[1] == '\\')
					filename++;
				else if (tomoyo_byte_range(filename + 1))
					filename += 3;
				else
					return false;
			}
			break;
		case '\\':
			if (c != '\\')
				return false;
			if (*++filename != '\\')
				return false;
			break;
		case '+':
			if (!tomoyo_decimal(c))
				return false;
			break;
		case 'x':
			if (!tomoyo_hexadecimal(c))
				return false;
			break;
		case 'a':
			if (!tomoyo_alphabet_char(c))
				return false;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
			if (c == '\\' && tomoyo_byte_range(filename + 1)
			    && !strncmp(filename + 1, pattern, 3)) {
				filename += 3;
				pattern += 2;
				break;
			}
			return false; /* Not matched. */
		case '*':
		case '@':
			for (i = 0; i <= filename_end - filename; i++) {
				if (tomoyo_file_matches_pattern2(filename + i,
							      filename_end,
							      pattern + 1,
							      pattern_end))
					return true;
				c = filename[i];
				if (c == '.' && *pattern == '@')
					break;
				if (c != '\\')
					continue;
				if (filename[i + 1] == '\\')
					i++;
				else if (tomoyo_byte_range(filename + i + 1))
					i += 3;
				else
					break; /* Bad pattern. */
			}
			return false; /* Not matched. */
		default:
			j = 0;
			c = *pattern;
			if (c == '$') {
				while (tomoyo_decimal(filename[j]))
					j++;
			} else if (c == 'X') {
				while (tomoyo_hexadecimal(filename[j]))
					j++;
			} else if (c == 'A') {
				while (tomoyo_alphabet_char(filename[j]))
					j++;
			}
			for (i = 1; i <= j; i++) {
				if (tomoyo_file_matches_pattern2(filename + i,
							      filename_end,
							      pattern + 1,
							      pattern_end))
					return true;
			}
			return false; /* Not matched or bad pattern. */
		}
		filename++;
		pattern++;
	}
	while (*pattern == '\\' &&
	       (*(pattern + 1) == '*' || *(pattern + 1) == '@'))
		pattern += 2;
	return filename == filename_end && pattern == pattern_end;
}

/**
 * tomoyo_file_matches_pattern - Pattern matching without '/' character.
 *
 * @filename:     The start of string to check.
 * @filename_end: The end of string to check.
 * @pattern:      The start of pattern to compare.
 * @pattern_end:  The end of pattern to compare.
 *
 * Returns true if @filename matches @pattern, false otherwise.
 */
static _Bool tomoyo_file_matches_pattern(const char *filename,
				      const char *filename_end,
				      const char *pattern,
				      const char *pattern_end)
{
	const char *pattern_start = pattern;
	_Bool first = true;
	_Bool result;
	while (pattern < pattern_end - 1) {
		/* Split at "\-" pattern. */
		if (*pattern++ != '\\' || *pattern++ != '-')
			continue;
		result = tomoyo_file_matches_pattern2(filename, filename_end,
						   pattern_start, pattern - 2);
		if (first)
			result = !result;
		if (result)
			return false;
		first = false;
		pattern_start = pattern;
	}
	result = tomoyo_file_matches_pattern2(filename, filename_end,
					   pattern_start, pattern_end);
	return first ? result : !result;
}

/**
 * tomoyo_path_matches_pattern2 - Do pathname pattern matching.
 *
 * @f: The start of string to check.
 * @p: The start of pattern to compare.
 *
 * Returns true if @f matches @p, false otherwise.
 */
static _Bool tomoyo_path_matches_pattern2(const char *f, const char *p)
{
	const char *f_delimiter;
	const char *p_delimiter;
	while (*f && *p) {
		f_delimiter = strchr(f, '/');
		if (!f_delimiter)
			f_delimiter = f + strlen(f);
		p_delimiter = strchr(p, '/');
		if (!p_delimiter)
			p_delimiter = p + strlen(p);
		if (*p == '\\' && *(p + 1) == '{')
			goto recursive;
		if (!tomoyo_file_matches_pattern(f, f_delimiter, p, p_delimiter))
			return false;
		f = f_delimiter;
		if (*f)
			f++;
		p = p_delimiter;
		if (*p)
			p++;
	}
	/* Ignore trailing "\*" and "\@" in @pattern. */
	while (*p == '\\' &&
	       (*(p + 1) == '*' || *(p + 1) == '@'))
		p += 2;
	return !*f && !*p;
recursive:
	/*
	 * The "\{" pattern is permitted only after '/' character.
	 * This guarantees that below "*(p - 1)" is safe.
	 * Also, the "\}" pattern is permitted only before '/' character
	 * so that "\{" + "\}" pair will not break the "\-" operator.
	 */
	if (*(p - 1) != '/' || p_delimiter <= p + 3 || *p_delimiter != '/' ||
	    *(p_delimiter - 1) != '}' || *(p_delimiter - 2) != '\\')
		return false; /* Bad pattern. */
	do {
		/* Compare current component with pattern. */
		if (!tomoyo_file_matches_pattern(f, f_delimiter, p + 2,
					      p_delimiter - 2))
			break;
		/* Proceed to next component. */
		f = f_delimiter;
		if (!*f)
			break;
		f++;
		/* Continue comparison. */
		if (tomoyo_path_matches_pattern2(f, p_delimiter + 1))
			return true;
		f_delimiter = strchr(f, '/');
	} while (f_delimiter);
	return false; /* Not matched. */
}

/**
 * tomoyo_path_matches_pattern - Check whether the given filename matches the given pattern.
 *
 * @filename: The filename to check.
 * @pattern:  The pattern to compare.
 *
 * Returns true if matches, false otherwise.
 *
 * The following patterns are available.
 *   \\     \ itself.
 *   \ooo   Octal representation of a byte.
 *   \*     Zero or more repetitions of characters other than '/'.
 *   \@     Zero or more repetitions of characters other than '/' or '.'.
 *   \?     1 byte character other than '/'.
 *   \$     One or more repetitions of decimal digits.
 *   \+     1 decimal digit.
 *   \X     One or more repetitions of hexadecimal digits.
 *   \x     1 hexadecimal digit.
 *   \A     One or more repetitions of alphabet characters.
 *   \a     1 alphabet character.
 *
 *   \-     Subtraction operator.
 *
 *   /\{dir\}/   '/' + 'One or more repetitions of dir/' (e.g. /dir/ /dir/dir/
 *               /dir/dir/dir/ ).
 */
_Bool tomoyo_path_matches_pattern(const struct tomoyo_path_info *filename,
			       const struct tomoyo_path_info *pattern)
{
	/*
	if (!filename || !pattern)
		return false;
	*/
	const char *f = filename->name;
	const char *p = pattern->name;
	const int len = pattern->const_len;
	/* If @pattern doesn't contain pattern, I can use strcmp(). */
	if (!pattern->is_patterned)
		return !tomoyo_pathcmp(filename, pattern);
	/* Don't compare directory and non-directory. */
	if (filename->is_dir != pattern->is_dir)
		return false;
	/* Compare the initial length without patterns. */
	if (strncmp(f, p, len))
		return false;
	f += len;
	p += len;
	return tomoyo_path_matches_pattern2(f, p);
}

/**
 * tomoyo_string_compare - strcmp() for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns return value of strcmp().
 */
int tomoyo_string_compare(const void *a, const void *b)
{
	return strcmp(*(char **) a, *(char **) b);
}

/**
 * tomoyo_pathcmp - strcmp() for "struct tomoyo_path_info".
 *
 * @a: Pointer to "const struct tomoyo_path_info".
 * @b: Pointer to "const struct tomoyo_path_info".
 *
 * Returns true if @a != @b, false otherwise.
 */
_Bool tomoyo_pathcmp(const struct tomoyo_path_info *a, const struct tomoyo_path_info *b)
{
	return a->hash != b->hash || strcmp(a->name, b->name);
}

/**
 * tomoyo_fill_path_info - Fill in "struct tomoyo_path_info" members.
 *
 * @ptr: Pointer to "struct tomoyo_path_info" to fill in.
 *
 * The caller sets "struct tomoyo_path_info"->name.
 */
void tomoyo_fill_path_info(struct tomoyo_path_info *ptr)
{
	const char *name = ptr->name;
	const int len = strlen(name);
	ptr->total_len = len;
	ptr->const_len = tomoyo_const_part_length(name);
	ptr->is_dir = len && (name[len - 1] == '/');
	ptr->is_patterned = (ptr->const_len < len);
	ptr->hash = tomoyo_full_name_hash((const unsigned char *) name, len);
}

/**
 * tomoyo_savename - Remember string data.
 *
 * @name: Pointer to "const char".
 *
 * Returns pointer to "const struct tomoyo_path_info" on success, abort otherwise.
 *
 * The returned pointer refers shared string. Thus, the caller must not free().
 */
const struct tomoyo_path_info *tomoyo_savename(const char *name)
{
	/* The list of names. */
	static struct tomoyo_savename_entry name_list[TOMOYO_SAVENAME_MAX_HASH];
	struct tomoyo_savename_entry *ptr;
	struct tomoyo_savename_entry *prev = NULL;
	unsigned int hash;
	int len;
	static _Bool first_call = true;
	if (!name)
		tomoyo_out_of_memory();
	len = strlen(name) + 1;
	hash = tomoyo_full_name_hash((const unsigned char *) name, len - 1);
	if (first_call) {
		int i;
		first_call = false;
		memset(&name_list, 0, sizeof(name_list));
		for (i = 0; i < TOMOYO_SAVENAME_MAX_HASH; i++) {
			name_list[i].entry.name = "/";
			tomoyo_fill_path_info(&name_list[i].entry);
		}
	}
	for (ptr = &name_list[hash % TOMOYO_SAVENAME_MAX_HASH]; ptr;
	     ptr = ptr->next) {
		if (hash == ptr->entry.hash && !strcmp(name, ptr->entry.name))
			return &ptr->entry;
		prev = ptr;
	}
	ptr = tomoyo_malloc(sizeof(*ptr) + len);
	ptr->next = NULL;
	ptr->entry.name = ((char *) ptr) + sizeof(*ptr);
	memmove((void *) ptr->entry.name, name, len);
	tomoyo_fill_path_info(&ptr->entry);
	prev->next = ptr; /* prev != NULL because name_list is not empty. */
	return &ptr->entry;
}

/**
 * tomoyo_parse_number - Parse a tomoyo_number_entry.
 *
 * @number: Number or number range.
 * @entry:  Pointer to "struct tomoyo_number_entry".
 *
 * Returns 0 on success, -EINVAL otherwise.
 */
int tomoyo_parse_number(const char *number, struct tomoyo_number_entry *entry)
{
	unsigned long min;
	unsigned long max;
	char *cp;
	memset(entry, 0, sizeof(*entry));
	if (number[0] != '0') {
		if (sscanf(number, "%lu", &min) != 1)
			return -EINVAL;
	} else if (number[1] == 'x' || number[1] == 'X') {
		if (sscanf(number + 2, "%lX", &min) != 1)
			return -EINVAL;
	} else if (sscanf(number, "%lo", &min) != 1)
		return -EINVAL;
	cp = strchr(number, '-');
	if (cp)
		number = cp + 1;
	if (number[0] != '0') {
		if (sscanf(number, "%lu", &max) != 1)
			return -EINVAL;
	} else if (number[1] == 'x' || number[1] == 'X') {
		if (sscanf(number + 2, "%lX", &max) != 1)
			return -EINVAL;
	} else if (sscanf(number, "%lo", &max) != 1)
		return -EINVAL;
	entry->min = min;
	entry->max = max;
	return 0;
}

/**
 * tomoyo_parse_ip - Parse a tomoyo_ip_address_entry.
 *
 * @address: IP address or IP range.
 * @entry:   Pointer to "struct tomoyo_address_entry".
 *
 * Returns 0 on success, -EINVAL otherwise.
 */
int tomoyo_parse_ip(const char *address, struct tomoyo_ip_address_entry *entry)
{
	unsigned int min[8];
	unsigned int max[8];
	int i;
	int j;
	memset(entry, 0, sizeof(*entry));
	i = sscanf(address, "%u.%u.%u.%u-%u.%u.%u.%u",
		   &min[0], &min[1], &min[2], &min[3],
		   &max[0], &max[1], &max[2], &max[3]);
	if (i == 4)
		for (j = 0; j < 4; j++)
			max[j] = min[j];
	if (i == 4 || i == 8) {
		for (j = 0; j < 4; j++) {
			entry->min[j] = (u8) min[j];
			entry->max[j] = (u8) max[j];
		}
		return 0;
	}
	i = sscanf(address, "%X:%X:%X:%X:%X:%X:%X:%X-%X:%X:%X:%X:%X:%X:%X:%X",
		   &min[0], &min[1], &min[2], &min[3],
		   &min[4], &min[5], &min[6], &min[7],
		   &max[0], &max[1], &max[2], &max[3],
		   &max[4], &max[5], &max[6], &max[7]);
	if (i == 8)
		for (j = 0; j < 8; j++)
			max[j] = min[j];
	if (i == 8 || i == 16) {
		for (j = 0; j < 8; j++) {
			entry->min[j * 2] = (u8) (min[j] >> 8);
			entry->min[j * 2 + 1] = (u8) min[j];
			entry->max[j * 2] = (u8) (max[j] >> 8);
			entry->max[j * 2 + 1] = (u8) max[j];
		}
		entry->is_ipv6 = true;
		return 0;
	}
	return -EINVAL;
}

/**
 * tomoyo_open_stream - Establish IP connection.
 *
 * @filename: String to send to remote tomoyo-editpolicy-agent program.
 *
 * Retruns file descriptor on success, EOF otherwise.
 */
int tomoyo_open_stream(const char *filename)
{
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	char c;
	int len = strlen(filename) + 1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = tomoyo_network_ip;
	addr.sin_port = tomoyo_network_port;
	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) ||
	    write(fd, filename, len) != len || read(fd, &c, 1) != 1 || c) {
		close(fd);
		return EOF;
	}
	return fd;
}

/**
 * tomoyo_find_domain - Find a domain by name and other attributes.
 *
 * @dp:          Pointer to "const struct tomoyo_domain_policy".
 * @domainname0: Name of domain to find.
 * @is_dis:      True if the domain acts as domain initialize source, false
 *               otherwise.
 * @is_dd:       True if the domain is marked as deleted, false otherwise.
 *
 * Returns index number (>= 0) in the @dp array if found, EOF otherwise.
 */
int tomoyo_find_domain(const struct tomoyo_domain_policy *dp,
		    const char *domainname0, const _Bool is_dis,
		    const _Bool is_dd)
{
	int i;
	struct tomoyo_path_info domainname;
	domainname.name = domainname0;
	tomoyo_fill_path_info(&domainname);
	for (i = 0; i < dp->list_len; i++) {
		if (dp->list[i].is_dis == is_dis &&
		    dp->list[i].is_dd == is_dd &&
		    !tomoyo_pathcmp(&domainname, dp->list[i].domainname))
			return i;
	}
	return EOF;
}

/**
 * tomoyo_assign_domain - Create a domain by name and other attributes.
 *
 * @dp:         Pointer to "struct tomoyo_domain_policy".
 * @domainname: Name of domain to find.
 * @is_dis:     True if the domain acts as domain initialize source, false
 *              otherwise.
 * @is_dd:      True if the domain is marked as deleted, false otherwise.
 *
 * Returns index number (>= 0) in the @dp array if created or already exists,
 * abort otherwise.
 */
int tomoyo_assign_domain(struct tomoyo_domain_policy *dp, const char *domainname,
		      const _Bool is_dis, const _Bool is_dd)
{
	int index = tomoyo_find_domain(dp, domainname, is_dis, is_dd);
	if (index >= 0)
		return index;
	if (!is_dis && !tomoyo_correct_domain(domainname)) {
		fprintf(stderr, "Invalid domainname '%s'\n", domainname);
		tomoyo_out_of_memory();
	}
	index = dp->list_len++;
	dp->list = tomoyo_realloc(dp->list, dp->list_len *
			       sizeof(struct tomoyo_domain_info));
	memset(&dp->list[index], 0, sizeof(struct tomoyo_domain_info));
	dp->list[index].domainname = tomoyo_savename(domainname);
	dp->list[index].is_dis = is_dis;
	dp->list[index].is_dd = is_dd;
	return index;
}

/**
 * tomoyo_get_ppid - Get PPID of the given PID.
 *
 * @pid: A pid_t value.
 *
 * Returns PPID value.
 */
static pid_t tomoyo_get_ppid(const pid_t pid)
{
	char buffer[1024];
	FILE *fp;
	pid_t ppid = 1;
	memset(buffer, 0, sizeof(buffer));
	snprintf(buffer, sizeof(buffer) - 1, "/proc/%u/status", pid);
	fp = fopen(buffer, "r");
	if (fp) {
		while (memset(buffer, 0, sizeof(buffer)) &&
		       fgets(buffer, sizeof(buffer) - 1, fp)) {
			if (sscanf(buffer, "PPid: %u", &ppid) == 1)
				break;
		}
		fclose(fp);
	}
	return ppid;
}

/**
 * tomoyo_get_name - Get comm name of the given PID.
 *
 * @pid: A pid_t value.
 *
 * Returns comm name using on success, NULL otherwise.
 *
 * The caller must free() the returned pointer.
 */
static char *tomoyo_get_name(const pid_t pid)
{
	char buffer[1024];
	FILE *fp;
	memset(buffer, 0, sizeof(buffer));
	snprintf(buffer, sizeof(buffer) - 1, "/proc/%u/status", pid);
	fp = fopen(buffer, "r");
	if (fp) {
		static const int offset = sizeof(buffer) / 6;
		while (memset(buffer, 0, sizeof(buffer)) &&
		       fgets(buffer, sizeof(buffer) - 1, fp)) {
			if (!strncmp(buffer, "Name:\t", 6)) {
				char *cp = buffer + 6;
				memmove(buffer, cp, strlen(cp) + 1);
				cp = strchr(buffer, '\n');
				if (cp)
					*cp = '\0';
				break;
			}
		}
		fclose(fp);
		if (buffer[0] && strlen(buffer) < offset - 1) {
			const char *src = buffer;
			char *dest = buffer + offset;
			while (1) {
				unsigned char c = *src++;
				if (!c) {
					*dest = '\0';
					break;
				}
				if (c == '\\') {
					c = *src++;
					if (c == '\\') {
						memmove(dest, "\\\\", 2);
						dest += 2;
					} else if (c == 'n') {
						memmove(dest, "\\012", 4);
						dest += 4;
					} else {
						break;
					}
				} else if (c > ' ' && c <= 126) {
					*dest++ = c;
				} else {
					*dest++ = '\\';
					*dest++ = (c >> 6) + '0';
					*dest++ = ((c >> 3) & 7) + '0';
					*dest++ = (c & 7) + '0';
				}
			}
			return strdup(buffer + offset);
		}
	}
	return NULL;
}

/* Serial number for sorting tomoyo_task_list . */
static int tomoyo_dump_index = 0;

/**
 * tomoyo_sort_process_entry - Sort tomoyo_tasklist list.
 *
 * @pid:   Pid to search.
 * @depth: Depth of the process for printing like pstree command.
 *
 * Returns nothing.
 */
static void tomoyo_sort_process_entry(const pid_t pid, const int depth)
{
	int i;
	for (i = 0; i < tomoyo_task_list_len; i++) {
		if (pid != tomoyo_task_list[i].pid)
			continue;
		tomoyo_task_list[i].index = tomoyo_dump_index++;
		tomoyo_task_list[i].depth = depth;
		tomoyo_task_list[i].selected = true;
	}
	for (i = 0; i < tomoyo_task_list_len; i++) {
		if (pid != tomoyo_task_list[i].ppid)
			continue;
		tomoyo_sort_process_entry(tomoyo_task_list[i].pid, depth + 1);
	}
}

/**
 * tomoyo_task_entry_compare - Compare routine for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns index diff value.
 */
static int tomoyo_task_entry_compare(const void *a, const void *b)
{
	const struct tomoyo_task_entry *a0 = (struct tomoyo_task_entry *) a;
	const struct tomoyo_task_entry *b0 = (struct tomoyo_task_entry *) b;
	return a0->index - b0->index;
}

/**
 * tomoyo_add_process_entry - Add entry for running processes.
 *
 * @line:    A line containing PID and profile and domainname.
 * @ppid:    Parent PID.
 * @name:    Comm name (allocated by strdup()).
 *
 * Returns nothing.
 *
 * @name is free()d on failure.
 */
static void tomoyo_add_process_entry(const char *line, const pid_t ppid,
				  char *name)
{
	int index;
	unsigned int pid = 0;
	int profile = -1;
	char *domain;
	if (!line || sscanf(line, "%u %u", &pid, &profile) != 2) {
		free(name);
		return;
	}
	domain = strchr(line, '<');
	if (domain)
		domain = tomoyo_strdup(domain);
	else
		domain = tomoyo_strdup("<UNKNOWN>");
	index = tomoyo_task_list_len++;
	tomoyo_task_list = tomoyo_realloc(tomoyo_task_list, tomoyo_task_list_len *
				    sizeof(struct tomoyo_task_entry));
	memset(&tomoyo_task_list[index], 0, sizeof(tomoyo_task_list[0]));
	tomoyo_task_list[index].pid = pid;
	tomoyo_task_list[index].ppid = ppid;
	tomoyo_task_list[index].profile = profile;
	tomoyo_task_list[index].name = name;
	tomoyo_task_list[index].domain = domain;
}

/**
 * tomoyo_read_process_list - Read all process's information.
 *
 * @show_all: Ture if kernel threads should be included, false otherwise.
 *
 * Returns nothing.
 */
void tomoyo_read_process_list(_Bool show_all)
{
	int i;
	while (tomoyo_task_list_len) {
		tomoyo_task_list_len--;
		free((void *) tomoyo_task_list[tomoyo_task_list_len].name);
		free((void *) tomoyo_task_list[tomoyo_task_list_len].domain);
	}
	tomoyo_dump_index = 0;
	if (tomoyo_network_mode) {
		FILE *fp = tomoyo_open_write(show_all ?
					  "proc:all_process_status" :
					  "proc:process_status");
		if (!fp)
			return;
		tomoyo_get();
		while (true) {
			char *line = tomoyo_freadline(fp);
			unsigned int pid = 0;
			unsigned int ppid = 0;
			char *name;
			if (!line)
				break;
			sscanf(line, "PID=%u PPID=%u", &pid, &ppid);
			name = strstr(line, "NAME=");
			if (name)
				name = tomoyo_strdup(name + 5);
			else
				name = tomoyo_strdup("<UNKNOWN>");
			line = tomoyo_freadline(fp);
			tomoyo_add_process_entry(line, ppid, name);
		}
		tomoyo_put();
		fclose(fp);
	} else {
		static const int line_len = 8192;
		char *line;
		int status_fd = open(TOMOYO_PROC_POLICY_PROCESS_STATUS, O_RDWR);
		DIR *dir = opendir("/proc/");
		if (status_fd == EOF || !dir) {
			if (status_fd != EOF)
				close(status_fd);
			if (dir)
				closedir(dir);
			return;
		}
		line = tomoyo_malloc(line_len);
		while (1) {
			char *name;
			int ret_ignored;
			unsigned int pid = 0;
			char buffer[128];
			char test[16];
			struct dirent *dent = readdir(dir);
			if (!dent)
				break;
			if (dent->d_type != DT_DIR ||
			    sscanf(dent->d_name, "%u", &pid) != 1 || !pid)
				continue;
			memset(buffer, 0, sizeof(buffer));
			if (!show_all) {
				snprintf(buffer, sizeof(buffer) - 1,
					 "/proc/%u/exe", pid);
				if (readlink(buffer, test, sizeof(test)) <= 0)
					continue;
			}
			name = tomoyo_get_name(pid);
			if (!name)
				name = tomoyo_strdup("<UNKNOWN>");
			snprintf(buffer, sizeof(buffer) - 1, "%u\n", pid);
			ret_ignored = write(status_fd, buffer, strlen(buffer));
			memset(line, 0, line_len);
			ret_ignored = read(status_fd, line, line_len - 1);
			tomoyo_add_process_entry(line, tomoyo_get_ppid(pid), name);
		}
		free(line);
		closedir(dir);
		close(status_fd);
	}
	tomoyo_sort_process_entry(1, 0);
	for (i = 0; i < tomoyo_task_list_len; i++) {
		if (tomoyo_task_list[i].selected) {
			tomoyo_task_list[i].selected = false;
			continue;
		}
		tomoyo_task_list[i].index = tomoyo_dump_index++;
		tomoyo_task_list[i].depth = 0;
	}
	qsort(tomoyo_task_list, tomoyo_task_list_len, sizeof(struct tomoyo_task_entry),
	      tomoyo_task_entry_compare);
}

/**
 * tomoyo_open_write - Open a file for writing.
 *
 * @filename: String to send to remote tomoyo-editpolicy-agent program if using
 *            network mode, file to open for writing otherwise.
 *
 * Returns pointer to "FILE" on success, NULL otherwise.
 */
FILE *tomoyo_open_write(const char *filename)
{
	if (tomoyo_network_mode) {
		const int fd = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in addr;
		FILE *fp;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = tomoyo_network_ip;
		addr.sin_port = tomoyo_network_port;
		if (connect(fd, (struct sockaddr *) &addr, sizeof(addr))) {
			close(fd);
			return NULL;
		}
		fp = fdopen(fd, "r+");
		/* setbuf(fp, NULL); */
		fprintf(fp, "%s", filename);
		fputc(0, fp);
		fflush(fp);
		if (fgetc(fp) != 0) {
			fclose(fp);
			return NULL;
		}
		return fp;
	} else {
		return fdopen(open(filename, O_WRONLY), "w");
	}
}

/**
 * tomoyo_close_write - Close stream opened by tomoyo_open_write().
 *
 * @fp: Pointer to "FILE".
 *
 * Returns true on success, false otherwise.
 */
_Bool tomoyo_close_write(FILE *fp)
{
	_Bool result = true;
	if (tomoyo_network_mode) {
		if (fputc(0, fp) == EOF)
			result = false;
		if (fflush(fp) == EOF)
			result = false;
		if (fgetc(fp) == EOF)
			result = false;
	}
	if (fclose(fp) == EOF)
		result = false;
	return result;
}


/**
 * tomoyo_open_read - Open a file for reading.
 *
 * @filename: String to send to remote tomoyo-editpolicy-agent program if using
 *            network mode, file to open for reading otherwise.
 *
 * Returns pointer to "FILE" on success, NULL otherwise.
 */
FILE *tomoyo_open_read(const char *filename)
{
	if (tomoyo_network_mode) {
		FILE *fp = tomoyo_open_write(filename);
		if (fp) {
			fputc(0, fp);
			fflush(fp);
		}
		return fp;
	} else {
		return fopen(filename, "r");
	}
}

/**
 * tomoyo_move_proc_to_file - Save /sys/kernel/security/tomoyo/ to /etc/tomoyo/ .
 *
 * @src:  Filename to save from.
 * @dest: Filename to save to.
 *
 * Returns true on success, false otherwise.
 */
_Bool tomoyo_move_proc_to_file(const char *src, const char *dest)
{
	FILE *proc_fp = tomoyo_open_read(src);
	FILE *file_fp;
	_Bool result = true;
	if (!proc_fp) {
		fprintf(stderr, "Can't open %s for reading.\n", src);
		return false;
	}
	file_fp = dest ? fopen(dest, "w") : stdout;
	if (!file_fp) {
		fprintf(stderr, "Can't open %s for writing.\n", dest);
		fclose(proc_fp);
		return false;
	}
	while (true) {
		const int c = fgetc(proc_fp);
		if (tomoyo_network_mode && !c)
			break;
		if (c == EOF)
			break;
		if (fputc(c, file_fp) == EOF)
			result = false;
	}
	fclose(proc_fp);
	if (file_fp != stdout)
		if (fclose(file_fp) == EOF)
			result = false;
	return result;
}

/**
 * tomoyo_clear_domain_policy - Clean up domain policy.
 *
 * @dp: Pointer to "struct tomoyo_domain_policy".
 *
 * Returns nothing.
 */
void tomoyo_clear_domain_policy(struct tomoyo_domain_policy *dp)
{
	int index;
	for (index = 0; index < dp->list_len; index++) {
		free(dp->list[index].string_ptr);
		dp->list[index].string_ptr = NULL;
		dp->list[index].string_count = 0;
	}
	free(dp->list);
	dp->list = NULL;
	dp->list_len = 0;
}

/**
 * tomoyo_find_domain_by_ptr - Find a domain by memory address.
 *
 * @dp:         Pointer to "struct tomoyo_domain_policy".
 * @domainname: Pointer to "const struct tomoyo_path_info".
 *
 * Returns index number (>= 0) in the @dp array if found, EOF otherwise.
 *
 * This function is faster than faster than tomoyo_find_domain() because
 * this function searches for a domain by address (i.e. avoid strcmp()).
 */
int tomoyo_find_domain_by_ptr(struct tomoyo_domain_policy *dp,
			   const struct tomoyo_path_info *domainname)
{
	int i;
	for (i = 0; i < dp->list_len; i++) {
		if (dp->list[i].domainname == domainname)
			return i;
	}
	return EOF;
}

/**
 * tomoyo_domain_name - Return domainname.
 *
 * @dp:    Pointer to "const struct tomoyo_domain_policy".
 * @index: Index in the @dp array.
 *
 * Returns domainname.
 *
 * Note that this function does not validate @index value.
 */
const char *tomoyo_domain_name(const struct tomoyo_domain_policy *dp,
			    const int index)
{
	return dp->list[index].domainname->name;
}

/**
 * tomoyo_domainname_compare - strcmp() for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns return value of strcmp().
 */
static int tomoyo_domainname_compare(const void *a, const void *b)
{
	return strcmp(((struct tomoyo_domain_info *) a)->domainname->name,
		      ((struct tomoyo_domain_info *) b)->domainname->name);
}

/**
 * tomoyo_path_info_compare - strcmp() for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns return value of strcmp().
 */
static int tomoyo_path_info_compare(const void *a, const void *b)
{
	const char *a0 = (*(struct tomoyo_path_info **) a)->name;
	const char *b0 = (*(struct tomoyo_path_info **) b)->name;
	return strcmp(a0, b0);
}

/**
 * tomoyo_sort_domain_policy - Sort domain policy.
 *
 * @dp: Pointer to "struct tomoyo_domain_policy".
 *
 * Returns nothing.
 */
static void tomoyo_sort_domain_policy(struct tomoyo_domain_policy *dp)
{
	int i;
	qsort(dp->list, dp->list_len, sizeof(struct tomoyo_domain_info),
	      tomoyo_domainname_compare);
	for (i = 0; i < dp->list_len; i++)
		qsort(dp->list[i].string_ptr, dp->list[i].string_count,
		      sizeof(struct tomoyo_path_info *), tomoyo_path_info_compare);
}

/**
 * tomoyo_read_domain_policy - Read domain policy from file or network or stdin.
 *
 * @dp:       Pointer to "struct tomoyo_domain_policy".
 * @filename: Domain policy's pathname.
 *
 * Returns nothing.
 */
void tomoyo_read_domain_policy(struct tomoyo_domain_policy *dp, const char *filename)
{
	FILE *fp = stdin;
	if (filename) {
		fp = tomoyo_open_read(filename);
		if (!fp) {
			fprintf(stderr, "Can't open %s\n", filename);
			return;
		}
	}
	tomoyo_get();
	tomoyo_handle_domain_policy(dp, fp, true);
	tomoyo_put();
	if (fp != stdin)
		fclose(fp);
	tomoyo_sort_domain_policy(dp);
}

/**
 * tomoyo_write_domain_policy - Write domain policy to file descriptor.
 *
 * @dp: Pointer to "struct tomoyo_domain_policy".
 * @fd: File descriptor.
 *
 * Returns 0.
 */
int tomoyo_write_domain_policy(struct tomoyo_domain_policy *dp, const int fd)
{
	int i;
	int j;
	for (i = 0; i < dp->list_len; i++) {
		const struct tomoyo_path_info **string_ptr
			= dp->list[i].string_ptr;
		const int string_count = dp->list[i].string_count;
		int ret_ignored;
		ret_ignored = write(fd, dp->list[i].domainname->name,
				    dp->list[i].domainname->total_len);
		ret_ignored = write(fd, "\n", 1);
		if (dp->list[i].profile_assigned) {
			char buf[128];
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf) - 1, "use_profile %u\n\n",
				 dp->list[i].profile);
			ret_ignored = write(fd, buf, strlen(buf));
		} else
			ret_ignored = write(fd, "\n", 1);
		for (j = 0; j < string_count; j++) {
			ret_ignored = write(fd, string_ptr[j]->name,
					    string_ptr[j]->total_len);
			ret_ignored = write(fd, "\n", 1);
		}
		ret_ignored = write(fd, "\n", 1);
	}
	return 0;
}

/**
 * tomoyo_delete_domain - Delete a domain from domain policy.
 *
 * @dp:    Pointer to "struct tomoyo_domain_policy".
 * @index: Index in the @dp array.
 *
 * Returns nothing.
 */
void tomoyo_delete_domain(struct tomoyo_domain_policy *dp, const int index)
{
	if (index >= 0 && index < dp->list_len) {
		int i;
		free(dp->list[index].string_ptr);
		for (i = index; i < dp->list_len - 1; i++)
			dp->list[i] = dp->list[i + 1];
		dp->list_len--;
	}
}

/**
 * tomoyo_add_string_entry - Add string entry to a domain.
 *
 * @dp:    Pointer to "struct tomoyo_domain_policy".
 * @entry: String to add.
 * @index: Index in the @dp array.
 *
 * Returns 0 if successfully added or already exists, -EINVAL otherwise.
 */
int tomoyo_add_string_entry(struct tomoyo_domain_policy *dp, const char *entry,
			 const int index)
{
	const struct tomoyo_path_info **acl_ptr;
	int acl_count;
	const struct tomoyo_path_info *cp;
	int i;
	if (index < 0 || index >= dp->list_len) {
		fprintf(stderr, "ERROR: domain is out of range.\n");
		return -EINVAL;
	}
	if (!entry || !*entry)
		return -EINVAL;
	cp = tomoyo_savename(entry);

	acl_ptr = dp->list[index].string_ptr;
	acl_count = dp->list[index].string_count;

	/* Check for the same entry. */
	for (i = 0; i < acl_count; i++)
		/* Faster comparison, for they are tomoyo_savename'd. */
		if (cp == acl_ptr[i])
			return 0;

	acl_ptr = tomoyo_realloc(acl_ptr, (acl_count + 1) *
			      sizeof(const struct tomoyo_path_info *));
	acl_ptr[acl_count++] = cp;
	dp->list[index].string_ptr = acl_ptr;
	dp->list[index].string_count = acl_count;
	return 0;
}

/**
 * tomoyo_del_string_entry - Delete string entry from a domain.
 *
 * @dp:    Pointer to "struct tomoyo_domain_policy".
 * @entry: String to remove.
 * @index: Index in the @dp array.
 *
 * Returns 0 if successfully removed, -ENOENT if not found,
 * -EINVAL otherwise.
 */
int tomoyo_del_string_entry(struct tomoyo_domain_policy *dp, const char *entry,
			 const int index)
{
	const struct tomoyo_path_info **acl_ptr;
	int acl_count;
	const struct tomoyo_path_info *cp;
	int i;
	if (index < 0 || index >= dp->list_len) {
		fprintf(stderr, "ERROR: domain is out of range.\n");
		return -EINVAL;
	}
	if (!entry || !*entry)
		return -EINVAL;
	cp = tomoyo_savename(entry);

	acl_ptr = dp->list[index].string_ptr;
	acl_count = dp->list[index].string_count;

	for (i = 0; i < acl_count; i++) {
		/* Faster comparison, for they are tomoyo_savename'd. */
		if (cp != acl_ptr[i])
			continue;
		dp->list[index].string_count--;
		for (; i < acl_count - 1; i++)
			acl_ptr[i] = acl_ptr[i + 1];
		return 0;
	}
	return -ENOENT;
}

/**
 * tomoyo_handle_domain_policy - Parse domain policy.
 *
 * @dp:       Pointer to "struct tomoyo_domain_policy".
 * @fp:       Pointer to "FILE".
 * @is_write: True if input, false if output.
 *
 * Returns nothing.
 */
void tomoyo_handle_domain_policy(struct tomoyo_domain_policy *dp, FILE *fp,
			      _Bool is_write)
{
	int i;
	int index = EOF;
	if (!is_write)
		goto read_policy;
	while (true) {
		char *line = tomoyo_freadline_unpack(fp);
		_Bool is_delete = false;
		_Bool is_select = false;
		unsigned int profile;
		if (!line)
			break;
		if (tomoyo_str_starts(line, "delete "))
			is_delete = true;
		else if (tomoyo_str_starts(line, "select "))
			is_select = true;
		tomoyo_str_starts(line, "domain=");
		if (tomoyo_domain_def(line)) {
			if (is_delete) {
				index = tomoyo_find_domain(dp, line, false,
							false);
				if (index >= 0)
					tomoyo_delete_domain(dp, index);
				index = EOF;
				continue;
			}
			if (is_select) {
				index = tomoyo_find_domain(dp, line, false,
							false);
				continue;
			}
			index = tomoyo_assign_domain(dp, line, false, false);
			continue;
		}
		if (index == EOF || !line[0])
			continue;
		if (sscanf(line, "use_profile %u", &profile) == 1) {
			dp->list[index].profile = (u8) profile;
			dp->list[index].profile_assigned = 1;
		} else if (is_delete)
			tomoyo_del_string_entry(dp, line, index);
		else
			tomoyo_add_string_entry(dp, line, index);
	}
	return;
read_policy:
	for (i = 0; i < dp->list_len; i++) {
		int j;
		const struct tomoyo_path_info **string_ptr
			= dp->list[i].string_ptr;
		const int string_count = dp->list[i].string_count;
		fprintf(fp, "%s\n", tomoyo_domain_name(dp, i));
		if (dp->list[i].profile_assigned)
			fprintf(fp, "use_profile %u\n", dp->list[i].profile);
		fprintf(fp, "\n");
		for (j = 0; j < string_count; j++)
			fprintf(fp, "%s\n", string_ptr[j]->name);
		fprintf(fp, "\n");
	}
}

/* Is the shared buffer for tomoyo_freadline() and tomoyo_shprintf() owned? */
static _Bool tomoyo_buffer_locked = false;

/**
 * tomoyo_get - Mark the shared buffer for tomoyo_freadline() and tomoyo_shprintf() owned.
 *
 * Returns nothing.
 *
 * This is for avoiding accidental overwriting.
 * tomoyo_freadline() and tomoyo_shprintf() have their own memory buffer.
 */
void tomoyo_get(void)
{
	if (tomoyo_buffer_locked)
		tomoyo_out_of_memory();
	tomoyo_buffer_locked = true;
}

/**
 * tomoyo_put - Mark the shared buffer for tomoyo_freadline() and tomoyo_shprintf() no longer owned.
 *
 * Returns nothing.
 *
 * This is for avoiding accidental overwriting.
 * tomoyo_freadline() and tomoyo_shprintf() have their own memory buffer.
 */
void tomoyo_put(void)
{
	if (!tomoyo_buffer_locked)
		tomoyo_out_of_memory();
	tomoyo_buffer_locked = false;
}

/**
 * tomoyo_shprintf - sprintf() to dynamically allocated buffer.
 *
 * @fmt: The printf()'s format string, followed by parameters.
 *
 * Returns pointer to dynamically allocated buffer.
 *
 * The caller must not free() the returned pointer.
 */
char *tomoyo_shprintf(const char *fmt, ...)
{
	while (true) {
		static char *policy = NULL;
		static int max_policy_len = 0;
		va_list args;
		int len;
		va_start(args, fmt);
		len = vsnprintf(policy, max_policy_len, fmt, args);
		va_end(args);
		if (len < 0)
			tomoyo_out_of_memory();
		if (len >= max_policy_len) {
			max_policy_len = len + 1;
			policy = tomoyo_realloc(policy, max_policy_len);
		} else
			return policy;
	}
}

/**
 * tomoyo_freadline - Read a line from file to dynamically allocated buffer.
 *
 * @fp: Pointer to "FILE".
 *
 * Returns pointer to dynamically allocated buffer on success, NULL otherwise.
 *
 * The caller must not free() the returned pointer.
 */
char *tomoyo_freadline(FILE *fp)
{
	static char *policy = NULL;
	int pos = 0;
	while (true) {
		static int max_policy_len = 0;
		const int c = fgetc(fp);
		if (c == EOF)
			return NULL;
		if (tomoyo_network_mode && !c)
			return NULL;
		if (pos == max_policy_len) {
			max_policy_len += 4096;
			policy = tomoyo_realloc(policy, max_policy_len);
		}
		policy[pos++] = (char) c;
		if (c == '\n') {
			policy[--pos] = '\0';
			break;
		}
	}
	if (!tomoyo_freadline_raw)
		tomoyo_normalize_line(policy);
	return policy;
}

/**
 * tomoyo_freadline_unpack - Read a line from file to dynamically allocated buffer.
 *
 * @fp: Pointer to "FILE". Maybe NULL.
 *
 * Returns pointer to dynamically allocated buffer on success, NULL otherwise.
 *
 * The caller must not free() the returned pointer.
 *
 * The caller must repeat calling this function without changing @fp (or with
 * changing @fp to NULL) until this function returns NULL, for this function
 * caches a line if the line is packed. Otherwise, some garbage lines might be
 * returned to the caller.
 */
char *tomoyo_freadline_unpack(FILE *fp)
{
	static char *previous_line = NULL;
	static char *cached_line = NULL;
	static int pack_start = 0;
	static int pack_len = 0;
	if (cached_line)
		goto unpack;
	if (!fp)
		return NULL;
	{
		char *pos;
		unsigned int offset;
		unsigned int len;
		char *line = tomoyo_freadline(fp);
		if (!line)
			return NULL;
		if (sscanf(line, "acl_group %u", &offset) == 1 && offset < 256)
			pos = strchr(line + 11, ' ');
		else
			pos = NULL;
		if (pos++)
			offset = pos - line;
		else
			offset = 0;
		if (!strncmp(line + offset, "file ", 5)) {
			char *cp = line + offset + 5;
			char *cp2 = strchr(cp + 1, ' ');
			len = cp2 - cp;
			if (cp2 && memchr(cp, '/', len)) {
				pack_start = cp - line;
				goto prepare;
			}
		} else if (!strncmp(line + offset, "network ", 8)) {
			char *cp = strchr(line + offset + 8, ' ');
			char *cp2 = NULL;
			if (cp)
				cp = strchr(cp + 1, ' ');
			if (cp)
				cp2 = strchr(cp + 1, ' ');
			cp++;
			len = cp2 - cp;
			if (cp2 && memchr(cp, '/', len)) {
				pack_start = cp - line;
				goto prepare;
			}
		}
		return line;
prepare:
		pack_len = len;
		cached_line = tomoyo_strdup(line);
	}
unpack:
	{
		char *line = NULL;
		char *pos = cached_line + pack_start;
		char *cp = memchr(pos, '/', pack_len);
		unsigned int len = cp - pos;
		free(previous_line);
		previous_line = NULL;
		if (!cp) {
			previous_line = cached_line;
			cached_line = NULL;
			line = previous_line;
		} else if (pack_len == 1) {
			/* Ignore trailing empty word. */
			free(cached_line);
			cached_line = NULL;
		} else {
			/* Current string is "abc d/e/f ghi". */
			line = tomoyo_strdup(cached_line);
			previous_line = line;
			/* Overwrite "abc d/e/f ghi" with "abc d ghi". */
			memmove(line + pack_start + len, pos + pack_len,
				strlen(pos + pack_len) + 1);
			/* Overwrite "abc d/e/f ghi" with "abc e/f ghi". */
			cp++;
			memmove(pos, cp, strlen(cp) + 1);
			/* Forget "d/" component. */
			pack_len -= len + 1;
			/* Ignore leading and middle empty word. */
			if (!len)
				goto unpack;
		}
		return line;
	}
}

/**
 * tomoyo_check_remote_host - Check whether the remote host is running with the TOMOYO 2.4 kernel or not.
 *
 * Returns true if running with TOMOYO 2.4 kernel, false otherwise.
 */
_Bool tomoyo_check_remote_host(void)
{
	int major = 0;
	int minor = 0;
	int rev = 0;
	FILE *fp = tomoyo_open_read("version");
	if (!fp ||
	    fscanf(fp, "%u.%u.%u", &major, &minor, &rev) < 2 ||
	    major != 2 || minor != 4) {
		const u32 ip = ntohl(tomoyo_network_ip);
		fprintf(stderr, "Can't connect to %u.%u.%u.%u:%u\n",
			(u8) (ip >> 24), (u8) (ip >> 16),
			(u8) (ip >> 8), (u8) ip, ntohs(tomoyo_network_port));
		if (fp)
			fclose(fp);
		return false;
	}
	fclose(fp);
	return true;
}

void tomoyo_mount_securityfs(void)
{
	if (access("/sys/kernel/security/tomoyo/", X_OK)) {
		if (unshare(CLONE_NEWNS) ||
		    mount("none", "/sys/kernel/security/", "securityfs", 0,
			  NULL)) {
			fprintf(stderr, "Please mount securityfs on "
				"/sys/kernel/security/ .\n");
		}
	}
}
