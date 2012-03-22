/*
 * patterntest.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * Version: 1.8.3   2011/09/29
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
#include <stdio.h>
#include <string.h>

#define bool _Bool
#define true 1
#define false 0
#define s8 char
#define u8 unsigned char
#define u16 unsigned short int
#define u32 unsigned int

struct ccs_path_info {
	const char *name;
	u32 hash;          /* = full_name_hash(name, strlen(name)) */
	u32 total_len;     /* = strlen(name)                       */
	u32 const_len;     /* = ccs_const_part_length(name)        */
};

/* Copied from kernel source. */
static inline unsigned long partial_name_hash(unsigned long c,
					      unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/* Copied from kernel source. */
static inline unsigned int full_name_hash(const unsigned char *name,
					  unsigned int len)
{
	unsigned long hash = 0;
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return (unsigned int) hash;
}

static bool ccs_pathcmp(const struct ccs_path_info *a,
			const struct ccs_path_info *b)
{
	return a->hash != b->hash || strcmp(a->name, b->name);
}

static bool ccs_byte_range(const char *str)
{
	return *str >= '0' && *str++ <= '3' &&
		*str >= '0' && *str++ <= '7' &&
		*str >= '0' && *str <= '7';
}

static bool ccs_decimal(const char c)
{
	return c >= '0' && c <= '9';
}

static bool ccs_hexadecimal(const char c)
{
	return (c >= '0' && c <= '9') ||
		(c >= 'A' && c <= 'F') ||
		(c >= 'a' && c <= 'f');
}

static bool ccs_alphabet_char(const char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool ccs_correct_word(const char *string)
{
	const char *const start = string;
	u8 in_repetition = 0;
	if (!*string)
		goto out;
	while (*string) {
		unsigned char c = *string++;
		if (in_repetition && c == '/')
			goto out;
		if (c <= ' ' || c >= 127)
			goto out;
		if (c != '\\')
			continue;
		c = *string++;
		if (c >= '0' && c <= '3') {
			unsigned char d;
			unsigned char e;
			d = *string++;
			if (d < '0' || d > '7')
				goto out;
			e = *string++;
			if (e < '0' || e > '7')
				goto out;
			c = ((c - '0') << 6) + ((d - '0') << 3) + (e - '0');
			if (c <= ' ' || c >= 127 || c == '\\')
				continue;
			goto out;
		}
		switch (c) {
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
				goto out;
			in_repetition = 1;
			continue;
		case '}':   /* "\}/" */
			if (in_repetition != 1 || *string++ != '/')
				goto out;
			in_repetition = 0;
			continue;
		case '(':   /* "/\(" */
			if (string - 3 < start || *(string - 3) != '/')
				goto out;
			in_repetition = 2;
			continue;
		case ')':   /* "\)/" */
			if (in_repetition != 2 || *string++ != '/')
				goto out;
			in_repetition = 0;
			continue;
		}
		goto out;
	}
	if (in_repetition)
		goto out;
	return true;
out:
	return false;
}

static int ccs_const_part_length(const char *filename)
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

static bool ccs_file_matches_pattern2(const char *filename,
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
				if (ccs_byte_range(filename + 1))
					filename += 3;
				else
					return false;
			}
			break;
		case '+':
			if (!ccs_decimal(c))
				return false;
			break;
		case 'x':
			if (!ccs_hexadecimal(c))
				return false;
			break;
		case 'a':
			if (!ccs_alphabet_char(c))
				return false;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
			if (c == '\\' && ccs_byte_range(filename + 1)
			    && !strncmp(filename + 1, pattern, 3)) {
				filename += 3;
				pattern += 2;
				break;
			}
			return false; /* Not matched. */
		case '*':
		case '@':
			for (i = 0; i <= filename_end - filename; i++) {
				if (ccs_file_matches_pattern2(filename + i,
							      filename_end,
							      pattern + 1,
							      pattern_end))
					return true;
				c = filename[i];
				if (c == '.' && *pattern == '@')
					break;
				if (c != '\\')
					continue;
				if (ccs_byte_range(filename + i + 1))
					i += 3;
				else
					break; /* Bad pattern. */
			}
			return false; /* Not matched. */
		default:
			j = 0;
			c = *pattern;
			if (c == '$') {
				while (ccs_decimal(filename[j]))
					j++;
			} else if (c == 'X') {
				while (ccs_hexadecimal(filename[j]))
					j++;
			} else if (c == 'A') {
				while (ccs_alphabet_char(filename[j]))
					j++;
			}
			for (i = 1; i <= j; i++) {
				if (ccs_file_matches_pattern2(filename + i,
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
	/* Ignore trailing "\*" and "\@" in @pattern. */
	while (*pattern == '\\' &&
	       (*(pattern + 1) == '*' || *(pattern + 1) == '@'))
		pattern += 2;
	return filename == filename_end && pattern == pattern_end;
}

static bool ccs_file_matches_pattern(const char *filename,
				     const char *filename_end,
				     const char *pattern,
				     const char *pattern_end)
{
	const char *pattern_start = pattern;
	bool first = true;
	bool result;
	while (pattern < pattern_end - 1) {
		/* Split at "\-" pattern. */
		if (*pattern++ != '\\' || *pattern++ != '-')
			continue;
		result = ccs_file_matches_pattern2(filename, filename_end,
						   pattern_start, pattern - 2);
		if (first)
			result = !result;
		if (result)
			return false;
		first = false;
		pattern_start = pattern;
	}
	result = ccs_file_matches_pattern2(filename, filename_end,
					   pattern_start, pattern_end);
	return first ? result : !result;
}

static bool ccs_path_matches_pattern2(const char *f, const char *p)
{
	const char *f_delimiter;
	const char *p_delimiter;
	char recursive_end;
	while (*f && *p) {
		f_delimiter = strchr(f, '/');
		p_delimiter = strchr(p, '/');
		if (!f_delimiter)
			f_delimiter = f + strlen(f);
		if (!p_delimiter)
			p_delimiter = p + strlen(p);
		if (*p == '\\') {
			if (*(p + 1) == '{') {
				recursive_end = '}'; 
				goto recursive;
			}
			if (*(p + 1) == '(') {
				recursive_end = ')';
				goto recursive;
			}
		}
		if (!ccs_file_matches_pattern(f, f_delimiter, p, p_delimiter))
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
	 * The "\{" or "\(" pattern is permitted only after '/' character.
	 * This guarantees that below "*(p - 1)" is safe.
	 * Also, the "\}" or "\)" pattern is permitted only before '/'
	 * character so that "\{" + "\}" or "\(" + "\)" pair will not break
	 * the "\-" operator.
	 */
	if (*(p - 1) != '/' || p_delimiter <= p + 3 || *p_delimiter != '/' ||
	    *(p_delimiter - 1) != recursive_end || *(p_delimiter - 2) != '\\')
		return false; /* Bad pattern. */
	if (recursive_end == ')') {
		/* Check zero repetition. */
		if (ccs_path_matches_pattern2(f, p_delimiter + 1))
			return true;
		/* Fall back to one or more repetition. */
	}
	do {
		/* Compare current component with pattern. */
		if (!ccs_file_matches_pattern(f, f_delimiter, p + 2,
					      p_delimiter - 2))
			break;
		/* Proceed to next component. */
		f = f_delimiter;
		if (!*f)
			break;
		f++;
		/* Continue comparison. */
		if (ccs_path_matches_pattern2(f, p_delimiter + 1))
			return true;
		f_delimiter = strchr(f, '/');
	} while (f_delimiter);
	return false; /* Not matched. */
}

static bool ccs_path_matches_pattern(const struct ccs_path_info *filename,
				     const struct ccs_path_info *pattern)
{
	const char *f = filename->name;
	const char *p = pattern->name;
	const int len = pattern->const_len;
	/* If @pattern doesn't contain pattern, I can use strcmp(). */
	if (len == pattern->total_len)
		return !ccs_pathcmp(filename, pattern);
	/* Compare the initial length without patterns. */
	if (strncmp(f, p, len))
		return false;
	f += len;
	p += len;
	/* Compare the last component first. */
	{
		const char *f2 = strrchr(f, '/');
		const char *p2 = strrchr(p, '/');
		if (!f2++)
			f2 = f;
		if (!p2++)
			p2 = p;
		if (!ccs_file_matches_pattern(f2, filename->name
					      + filename->total_len,
					      p2, pattern->name
					      + pattern->total_len))
			return false;
	}
	return ccs_path_matches_pattern2(f, p);
}

static void ccs_fill_path_info(struct ccs_path_info *ptr)
{
	const char *name = ptr->name;
	const int len = strlen(name);
	ptr->total_len = len;
	ptr->const_len = ccs_const_part_length(name);
	ptr->hash = full_name_hash(name, len);
}

static const struct {
	const char *pathname;
	const char *pattern;
	const bool match;
} testcases[] = {
	{ "/bin/true", "/bin/\\*", 1 },
	{ "/bin/true", "/bin\\@\\*/\\*", 1 },
	{ "/usr/local/", "/usr/\\*/", 1 },
	{ "/usr/local/", "/usr/\\*\\*\\@\\*/", 1 },
	{ "pipe:[12345]", "pipe:[\\$]", 1 },
	{ "socket:[family=1:type=2:protocol=3]",
	  "socket:[family=1:type=2:protocol=\\$]", 1 },
	{ "http://tomoyo.sourceforge.jp/", "\\*/\\*/\\*/", 1 },
	{ "http://tomoyo.sourceforge.jp/index.html", "\\*/\\*/\\*/\\*", 1 },
	{ "http://tomoyo.sourceforge.jp/index.html",
	  "\\*/\\*/\\*/\\*\\*\\@\\*\\@", 1 },
	{ "http://tomoyo.sourceforge.jp/index.html",
	  "\\*/\\@\\*/\\*\\@/\\*\\@\\*\\@\\*", 1 },
	{ "http://tomoyo.sourceforge.jp/1.7/index.html",
	  "http://\\{\\*\\}/\\@.html", 1 },
	{ "http://tomoyo.sourceforge.jp/index.html",
	  "\\*://\\@.sourceforge.jp/\\*", 1 },
	{ "http://tomoyo.sourceforge.jp/index.html",
	  "\\*://\\@.sourceforge.jp/\\*", 1 },
	{ "http://sourceforge.jp/projects/tomoyo/svn/view/trunk/1.7.x/"
	  "ccs-patch/security/ccsecurity/?root=tomoyo",
	  "\\*://\\@sourceforge.jp/\\{\\*\\}/?root=tomoyo", 1 },
	{ "http://sourceforge.jp/projects/tomoyo/svn/view/trunk/1.7.x/"
	  "ccs-patch/security/?root=tomoyo",
	  "\\*://\\@sourceforge.jp/\\{\\*\\}/?root=tomoyo", 1 },
	{ "http://sourceforge.jp/projects/tomoyo/svn/view/trunk/1.7.x/"
	  "ccs-patch/?root=tomoyo",
	  "\\*://\\@sourceforge.jp/\\{\\*\\}/?root=tomoyo", 1 },
	{ "http://sourceforge.jp/projects/tomoyo/svn/view/trunk/1.7.x/"
	  "/ccs-patch///security//ccsecurity///?root=tomoyo",
	  "\\*://\\@sourceforge.jp/\\{\\*\\-.\\-..\\-\\*%\\*\\}/"
	  "?root=tomoyo\\*\\*", 1 },
	{ "/var/www/html/test/test/test/index.html",
	  "/var/www/html/\\{test\\}/\\*.html", 1 },
	{ "/etc/skel/", "/etc/\\{\\*\\}/\\*/", 0 },
	{ "/etc/skel/", "/etc/\\(\\*\\)/\\*/", 1 },
	{ "/etc/passwd", "/etc/\\{\\*\\}/\\*", 0 },
	{ "/etc/passwd", "/etc/\\(\\*\\)/\\*", 1 },
	{ "/bin/true", "/bin/\\*/", 0 },
	{ "/bin/", "/bin/\\*", 1 },
	{ "/bin/", "/bin/\\@", 1 },
	{ "/bin/", "/bin/\\@\\@", 1 },
	{ "http://tomoyo.sourceforge.jp/", "\\*/\\*/\\*/\?", 0 },
	{ "http://tomoyo.sourceforge.jp/index.html", "\\*/\\*/\\*/\\@", 0 },
	{ "http://tomoyo.sourceforge.jp/index.html", "http://\\*/\\@", 0 },
	{ "socket:[family=1:type=2:protocol=3]",
	  "/\\{\\*\\}/socket:[\\*]", 0 },
	{ "/tmp/foo", "/tmp/\\(\\*\\)/", 0 },
	{ "/tmp/foo", "/tmp/\\(\\*\\)/foo", 1 },
	{ "/tmp/foo/", "/tmp/\\(\\*\\)/", 1 },
	{ "/tmp/foo/foo", "/tmp/\\(\\*\\-\\?\\)/", 0 },
	{ "/tmp/foo", "/tmp/\\{\\*\\}/", 0 },
	{ "/tmp/foo", "/tmp/\\{\\*\\}/foo", 0 },
	{ "/tmp/foo/", "/tmp/\\{\\*\\}/", 1 },
	{ "/tmp/foo/foo", "/tmp/\\{\\*\\-\\?\\}/", 0 },
	{ "/tmp/foo/foo/boo/foo", "/tmp/\\{foo\\}/foo", 0 },
	{ "/tmp/foo/boo/foo/foo", "/tmp/\\{foo\\}/foo", 0 },
	{ NULL, NULL, 0 }
};

int main(int argc, char *argv[])
{
	int i;
	struct ccs_path_info f;
	struct ccs_path_info p;
	for (i = 0; testcases[i].pathname; i++) {
		f.name = testcases[i].pathname;
		p.name = testcases[i].pattern;
		if (!ccs_correct_word(f.name)) {
			printf("Bad pathname: %s\n", f.name);
			continue;
		} else if (!ccs_correct_word(p.name)) {
			printf("Bad pattern: %s\n", p.name);
			continue;
		}
		ccs_fill_path_info(&f);
		ccs_fill_path_info(&p);
		if (f.total_len == f.const_len &&
		    ccs_path_matches_pattern(&f, &p) == testcases[i].match)
			continue;
		printf("Bad result: %s %s %u\n", f.name, p.name,
		       testcases[i].match);
		return 1;
	}
	printf("OK\n");
	return 0;
}
