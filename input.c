/*
 * Parse a makefile
 */
#include "make.h"
#include <glob.h>

int ilevel;	// Level of nesting of included files
int lineno;	// Physical line number in file
int dispno;	// Line number for display purposes

/*
 * Return a pointer to the next whitespace-delimited word or NULL if
 * there are none left.
 */
static char *
gettok(char **ptr)
{
	char *p;

	while (isspace(**ptr))	// Skip spaces
		(*ptr)++;

	if (**ptr == '\0')	// Nothing after spaces
		return NULL;

	p = *ptr;		// Word starts here

	while (**ptr != '\0' && !isspace(**ptr))
		(*ptr)++;	// Find end of word

	// Terminate token and move on unless already at end of string
	if (**ptr != '\0')
		*(*ptr)++ = '\0';

	return(p);
}

/*
 * Skip over (possibly adjacent or nested) macro expansions.
 */
static char *
skip_macro(const char *s)
{
	while (*s && s[0] == '$') {
		if (s[1] == '(' || s[1] == '{') {
			char end = *++s == '(' ? ')' : '}';
			while (*s && *s != end)
				s = skip_macro(s + 1);
			if (*s == end)
				++s;
		} else if (s[1] != '\0') {
			s += 2;
		} else {
			break;
		}
	}
	return (char *)s;
}

#if !ENABLE_FEATURE_MAKE_POSIX_202X
# define modify_words(v, m, l, fp, rp, fs, rs) modify_words(v, m, l, fs, rs)
#endif
/*
 * Process each whitespace-separated word in the input string:
 *
 * - replace paths with their directory or filename part
 * - replace prefixes and suffixes
 *
 * Returns an allocated string or NULL if the input is unmodified.
 */
static char *
modify_words(const char *val, int modifier, size_t lenf,
				const char *find_pref, const char *repl_pref,
				const char *find_suff, const char *repl_suff)
{
	char *s, *copy, *word, *sep, *newword, *buf = NULL;
#if ENABLE_FEATURE_MAKE_POSIX_202X
	size_t find_pref_len = 0, find_suff_len = 0;
#endif

	if (!modifier && !lenf)
		return buf;

#if ENABLE_FEATURE_MAKE_POSIX_202X
	if (find_pref) {
		// get length of find prefix, e.g: src/
		find_pref_len = strlen(find_pref);
		// get length of find suffix, e.g: .c
		find_suff_len = lenf - find_pref_len - 1;
	}
#endif

	s = copy = xstrdup(val);
	while ((word = gettok(&s)) != NULL) {
		newword = NULL;
		if (modifier) {
			sep = strrchr(word, '/');
			if (modifier == 'D') {
				if (!sep) {
					word[0] = '.';	// no '/', return "."
					sep = word + 1;
				} else if (sep == word) {
					// '/' at start of word, return "/"
					sep = word + 1;
				}
				// else terminate at separator
				*sep = '\0';
			} else if (/* modifier == 'F' && */ sep) {
				word = sep + 1;
			}
		}
		if (lenf) {
			size_t lenw = strlen(word);
#if ENABLE_FEATURE_MAKE_POSIX_202X
			// This code implements pattern macro expansions:
			//    https://austingroupbugs.net/view.php?id=519
			//
			// find: <prefix>%<suffix>
			// example: src/%.c
			if (lenw >= lenf - 1 && find_pref) {
				// If prefix and suffix of word match find_pref and
				// find_suff, then do substitution.
				if (strncmp(word, find_pref, find_pref_len) == 0 &&
						strcmp(word + lenw - find_suff_len, find_suff) == 0) {
					// replace: <prefix>[%<suffix>]
					// example: build/%.o or build/all.o (notice no %)
					// If repl_suff is NULL, replace whole word with repl_pref.
					if (!repl_suff) {
						word = newword = xstrdup(repl_pref);
					} else {
						word[lenw - find_suff_len] = '\0';
						word = newword = xconcat3(repl_pref,
									word + find_pref_len, repl_suff);
					}
				}
			} else
#endif
			if (lenw >= lenf && strcmp(word + lenw - lenf, find_suff) == 0) {
				word[lenw - lenf] = '\0';
				word = newword = xconcat3(word, repl_suff, "");
			}
		}
		buf = xappendword(buf, word);
		free(newword);
	}
	free(copy);
	return buf;
}

/*
 * Return a pointer to the next instance of a given character.  Macro
 * expansions are skipped so the ':' and '=' in $(VAR:.s1=.s2) aren't
 * detected as separators for rules or macro definitions.
 */
static char *
find_char(const char *str, int c)
{
	const char *s;

	for (s = skip_macro(str); *s; s = skip_macro(s + 1)) {
		if (*s == c)
			return (char *)s;
	}
	return NULL;
}

/*
 * Recursively expand any macros in str to an allocated string.
 */
char *
expand_macros(const char *str, int except_dollar)
{
	char *exp, *newexp, *s, *t, *p, *q, *name;
	char *find, *replace, *modified;
	char *expval, *expfind, *find_suff, *repl_suff;
#if ENABLE_FEATURE_MAKE_POSIX_202X
	char *find_pref = NULL, *repl_pref = NULL;
#endif
	size_t lenf;
	char modifier;
	struct macro *mp;

	exp = xstrdup(str);
	for (t = exp; *t; t++) {
		if (*t == '$') {
			if (t[1] == '\0') {
				break;
			}
#if ENABLE_FEATURE_MAKE_POSIX_202X
			if (t[1] == '$' && except_dollar) {
				t++;
				continue;
			}
#endif
			// Need to expand a macro.  Find its extent (s to t inclusive)
			// and take a copy of its content.
			s = t;
			t++;
			if (*t == '{' || *t == '(') {
				t = find_char(t, *t == '{' ? '}' : ')');
				if (t == NULL)
					error("unterminated variable '%s'", s);
				name = xstrndup(s + 2, t - s - 2);
			} else {
				name = xmalloc(2);
				name[0] = *t;
				name[1] = '\0';
			}

			// Only do suffix replacement or pattern macro expansion
			// if both ':' and '=' are found.  This is indicated by
			// lenf != 0.
			expfind = NULL;
			find_suff = repl_suff = NULL;
			lenf = 0;
			if ((find = find_char(name, ':'))) {
				*find++ = '\0';
				expfind = expand_macros(find, FALSE);
				if ((replace = find_char(expfind, '='))) {
					*replace++ = '\0';
					lenf = strlen(expfind);
#if ENABLE_FEATURE_MAKE_POSIX_202X
					if (!POSIX_2017 && (find_suff = strchr(expfind, '%'))) {
						find_pref = expfind;
						repl_pref = replace;
						*find_suff++ = '\0';
						if ((repl_suff = strchr(replace, '%')))
							*repl_suff++ = '\0';
					} else
#endif
					{
						find_suff = expfind;
						repl_suff = replace;
					}
				}
			}

			p = q = name;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			// If not in POSIX mode expand macros in the name.
			if (!posix) {
				char *expname = expand_macros(name, FALSE);
				free(name);
				name = expname;
			} else
#endif
			// Skip over nested expansions in name
			do {
				*q++ = *p;
			} while ((p = skip_macro(p + 1)) && *p);

			// The internal macros support 'D' and 'F' modifiers
			modifier = '\0';
			switch (name[0]) {
#if ENABLE_FEATURE_MAKE_POSIX_202X
			case '^':
				if (POSIX_2017)
					break;
				// fall through
#endif
			case '@': case '%': case '?': case '<': case '*':
				if ((name[1] == 'D' || name[1] == 'F') && name[2] == '\0') {
					modifier = name[1];
					name[1] = '\0';
				}
				break;
			}

			modified = NULL;
			if ((mp = getmp(name)))  {
				// Recursive expansion
				if (mp->m_flag)
					error("recursive macro %s", name);

				mp->m_flag = TRUE;
				expval = expand_macros(mp->m_val, FALSE);
				mp->m_flag = FALSE;
				modified = modify_words(expval, modifier, lenf,
								find_pref, repl_pref, find_suff, repl_suff);
				if (modified)
					free(expval);
				else
					modified = expval;
			}
			free(name);
			free(expfind);

			if (modified && *modified) {
				// The text to be replaced by the macro expansion is
				// from s to t inclusive.
				*s = '\0';
				newexp = xconcat3(exp, modified, t + 1);
				t = newexp + (s - exp) + strlen(modified) - 1;
				free(exp);
				exp = newexp;
			} else {
				// Macro wasn't expanded or expanded to nothing.
				// Close the space occupied by the macro reference.
				q = t + 1;
				t = s - 1;
				while ((*s++ = *q++))
					continue;
			}
			free(modified);
		}
	}
	return exp;
}

/*
 * Process a non-command line
 */
static char *
process_line(char *s)
{
	char *r, *t;

	// Skip leading spaces
	while (isspace(*s))
		s++;
	r = s;

	// Strip comment
	t = strchr(s, '#');
	if (t)
		*t = '\0';

	// Replace escaped newline and any following blanks with a single space
	for (t = s; *s; ) {
		if (s[0] == '\\' && s[1] == '\n') {
			s += 2;
			while (isblank(*s))
				++s;
			*t++ = ' ';
		} else {
			*t++ = *s++;
		}
	}
	*t = '\0';

	return r;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
enum {
	INITIAL = 0,
	SKIP_LINE = 1 << 0,
	EXPECT_ELSE = 1 << 1,
	GOT_MATCH = 1 << 2
};

#define IF_MAX 10

static uint8_t clevel = 0;
static uint8_t cstate[IF_MAX + 1] = {INITIAL};

/*
 * Process conditional directives and return TRUE if the current line
 * should be skipped.
 */
static int
skip_line(const char *str1)
{
	char *copy, *q, *token, *next_token;
	bool new_level = TRUE;
	// Default is to return skip flag for current level
	int ret = cstate[clevel] & SKIP_LINE;

	if (*str1 == '\t')
		return ret;

	copy = xstrdup(str1);
	q = process_line(copy);
	if ((token = gettok(&q)) != NULL) {
		next_token = gettok(&q);

		if (strcmp(token, "endif") == 0) {
			if (next_token != NULL)
				error_unexpected("text");
			if (clevel == 0)
				error_unexpected(token);
			--clevel;
			ret = TRUE;
			goto end;
		} else if (strcmp(token, "else") == 0) {
			if (!(cstate[clevel] & EXPECT_ELSE))
				error_unexpected(token);

			// If an earlier condition matched we'll now skip lines.
			// If not we don't, though an 'else if' may override this.
			if ((cstate[clevel] & GOT_MATCH))
				cstate[clevel] |= SKIP_LINE;
			else
				cstate[clevel] &= ~SKIP_LINE;

			if (next_token == NULL) {
				// Simple else with no conditional directive
				cstate[clevel] &= ~EXPECT_ELSE;
				ret = TRUE;
				goto end;
			} else {
				// A conditional directive is now required ('else if').
				token = next_token;
				next_token = gettok(&q);
				new_level = FALSE;
			}
		}

		if (strcmp(token, "ifdef") == 0 || strcmp(token, "ifndef") == 0) {
			if (next_token != NULL && gettok(&q) == NULL) {
				if (new_level) {
					// Start a new level.
					if (clevel == IF_MAX)
						error("nesting too deep");
					++clevel;
					cstate[clevel] = EXPECT_ELSE | SKIP_LINE;
					// If we were skipping lines at the previous level
					// we need to continue doing that unconditionally
					// at the new level.
					if ((cstate[clevel - 1] & SKIP_LINE))
						cstate[clevel] |= GOT_MATCH;
				}

				if (!(cstate[clevel] & GOT_MATCH)) {
					char *t = expand_macros(next_token, FALSE);
					struct macro *mp = getmp(t);
					int match = mp != NULL && mp->m_val[0] != '\0';

					if (token[2] == 'n')
						match = !match;
					if (match) {
						cstate[clevel] &= ~SKIP_LINE;
						cstate[clevel] |= GOT_MATCH;
					}
					free(t);
				}
			} else {
				error("invalid condition");
			}
			ret = TRUE;
		} else if (!new_level) {
			error("missing conditional");
		}
	}
 end:
	free(copy);
	return ret;
}
#endif

/*
 * If fd is NULL read the built-in rules.  Otherwise read from the
 * specified file descriptor.
 */
static char *
make_fgets(char *s, int size, FILE *fd)
{
	return fd ? fgets(s, size, fd) : getrules(s, size);
}

/*
 * Read a newline-terminated line into an allocated string.
 * Backslash-escaped newlines don't terminate the line.
 * Ignore comment lines.  Return NULL on EOF.
 */
static char *
readline(FILE *fd)
{
	char *p, *str;
	int pos = 0;
	int len = 256;

	str = xmalloc(len);

	for (;;) {
		if (make_fgets(str + pos, len - pos, fd) == NULL) {
			if (pos)
				return str;
			free(str);
			return NULL;	// EOF
		}

		if ((p = strchr(str + pos, '\n')) == NULL) {
			// Need more room
			pos = len - 1;
			len += 256;
			str = xrealloc(str, len);
			continue;
		}
		lineno++;

		// Remove CR before LF
		if (p != str && p[-1] == '\r') {
			p[-1] = '\n';
			*p-- = '\0';
		}

		// Keep going if newline has been escaped
		if (p != str && p[-1] == '\\') {
			pos = p - str + 1;
			continue;
		}
		dispno = lineno;

		p = str;
		while (isspace(*p))	// Check for blank line
			p++;

		if (*p != '\0' && *str != '#'
				IF_FEATURE_MAKE_EXTENSIONS(&& (posix || !skip_line(str)))
		) {
			return str;
		}

		pos = 0;
	}
}

/*
 * Return TRUE if the argument is a known suffix.
 */
static int
is_suffix(const char *s)
{
	struct name *np;
	struct rule *rp;
	struct depend *dp;

	np = newname(".SUFFIXES");
	for (rp = np->n_rule; rp; rp = rp->r_next) {
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			if (strcmp(s, dp->d_name->n_name) == 0) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

#define T_NORMAL 0
#define T_SPECIAL 1
#define T_INFERENCE 2

/*
 * Determine if the argument is a special target and return a set
 * of flags indicating its properties.
 */
static int
target_type(char *s)
{
	char *sfx;
	int ret;
	static const char *s_name[] = {
		".DEFAULT",
		".POSIX",
		".IGNORE",
		".PRECIOUS",
		".SILENT",
		".SUFFIXES",
#if ENABLE_FEATURE_MAKE_POSIX_202X
		".PHONY",
#endif
	};

	if (*s != '.')
		return T_NORMAL;

	// Check for one of the known special targets
	for (ret = 0; ret < sizeof(s_name)/sizeof(s_name[0]); ret++)
		if (strcmp(s_name[ret], s) == 0)
			return T_SPECIAL;

	// Check for an inference rule
	ret = T_NORMAL;
	sfx = suffix(s);
	if (is_suffix(sfx)) {
		if (s == sfx) {	// Single suffix rule
			ret = T_INFERENCE;
		} else {
			// Suffix is valid, check that prefix is too
			*sfx = '\0';
			if (is_suffix(s))
				ret = T_INFERENCE;
			*sfx = '.';
		}
	}
	return ret;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
static int
ends_with_bracket(const char *s)
{
	const char *t = strrchr(s, ')');
	return t && t[1] == '\0';
}
#endif

/*
 * Process a command line
 */
static char *
process_command(char *s)
{
	char *t, *u;

	// Remove tab following escaped newline
	for (t = u = s; *u; u++) {
		if (u[0] == '\\' && u[1] == '\n' && u[2] == '\t') {
			*t++ = *u++;
			*t++ = *u++;
		} else {
			*t++ = *u;
		}
	}
	*t = '\0';
	return s;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
static char *
run_command(const char *cmd)
{
	FILE *fd;
	char *s, *val = NULL;
	char buf[256];
	size_t len = 0, nread;

	if ((fd = popen(cmd, "r")) == NULL)
		return val;

	for (;;) {
		nread = fread(buf, 1, sizeof(buf), fd);
		if (nread == 0)
			break;

		val = xrealloc(val, len + nread + 1);
		memcpy(val + len, buf, nread);
		len += nread;
		val[len] = '\0';
	}
	pclose(fd);

	if (val) {
		// Remove one newline from the end (BSD compatibility)
		if (val[len - 1] == '\n')
			val[len - 1] = '\0';
		// Other newlines are changed to spaces
		for (s = val; *s; ++s) {
			if (*s == '\n')
				*s = ' ';
		}
	}
	return val;
}

/*
 * Check for an unescaped wildcard character
 */
static int wildchar(const char *p)
{
	while (*p) {
		switch (*p) {
		case '?':
		case '*':
		case '[':
			return 1;
		case '\\':
			if (p[1] != '\0')
				++p;
			break;
		}
		++p;
	}
	return 0;
}

/*
 * Expand any wildcards in a pattern.  Return TRUE if a match is
 * found, in which case the caller should call globfree() on the
 * glob_t structure.
 */
static int
wildcard(char *p, glob_t *gd)
{
	int ret;
	char *s;

	// Don't call glob() if there are no wildcards.
	if (!wildchar(p)) {
 nomatch:
		// Remove backslashes from the name.
		for (s = p; *p; ++p) {
			if (*p == '\\' && p[1] != '\0')
				continue;
			*s++ = *p;
		}
		*s = '\0';
		return 0;
	}

	memset(gd, 0, sizeof(*gd));
	ret = glob(p, GLOB_NOSORT, NULL, gd);
	if (ret == GLOB_NOMATCH) {
		globfree(gd);
		goto nomatch;
	} else if (ret != 0) {
		error("glob error for '%s'", p);
	}
	return 1;
}
#endif

/*
 * Parse input from the makefile and construct a tree structure of it.
 */
void
input(FILE *fd)
{
	char *p, *q, *s, *a, *str, *expanded, *copy;
	char *str1, *str2;
	struct name *np;
	struct depend *dp;
	struct cmd *cp;
	int startno, count;
	bool semicolon_cmd, seen_inference;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	uint8_t old_clevel = clevel;
	bool dbl;
	char *lib = NULL;
	glob_t gd;
	int nfile, i;
	char **files;
#else
	const bool dbl = FALSE;
#endif
#if ENABLE_FEATURE_MAKE_POSIX_202X
	bool minus;
#else
	const bool minus = FALSE;
#endif

	lineno = 0;
	str1 = readline(fd);
	while (str1) {
		str2 = NULL;
		if (*str1 == '\t')	// Command without target
			error("command not allowed here");

		// Newlines and comments are handled differently in command lines
		// and other types of line.  Take a copy of the current line before
		// processing it as a non-command line in case it contains a
		// rule with a command line.  That is, a line of the form:
		//
		//   target: prereq; command
		//
		copy = xstrdup(str1);
		str = process_line(str1);

		// Check for an include line
#if ENABLE_FEATURE_MAKE_POSIX_202X
		minus = !POSIX_2017 && *str == '-';
#endif
		p = str + minus;
		if (strncmp(p, "include", 7) == 0 && isblank(p[7])) {
			const char *old_makefile = makefile;
			int old_lineno = lineno;

			if (ilevel > 16)
				error("too many includes");

			q = expanded = expand_macros(p + 7, FALSE);
			while ((p = gettok(&q)) != NULL) {
				FILE *ifd;

#if ENABLE_FEATURE_MAKE_POSIX_202X
				if (!POSIX_2017) {
					// Try to create include file or bring it up-to-date
					opts |= OPT_include;
					make(newname(p), 0);
					opts &= ~OPT_include;
				}
#endif
				if ((ifd = fopen(p, "r")) == NULL) {
					if (!minus)
						error("can't open include file '%s'", p);
				} else {
					makefile = p;
					ilevel++;
					input(ifd);
					fclose(ifd);
					ilevel--;
				}
			}

			makefile = old_makefile;
			lineno = old_lineno;
			goto end_loop;
		}

		// Check for a macro definition
		q = find_char(str, '=');
		if (q != NULL) {
			int level = (useenv || fd == NULL) ? 4 : 3;
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_202X
			char *newq = NULL;
			char eq = '\0';

			if (q - 1 > str) {
				switch (q[-1]) {
				case ':':
# if ENABLE_FEATURE_MAKE_POSIX_202X
					// '::=' and ':::=' are from POSIX 202X.
					if (!POSIX_2017 && q - 2 > str && q[-2] == ':') {
						if (q - 3 > str && q[-3] == ':') {
							eq = 'B';	// BSD-style ':='
							q[-3] = '\0';
						} else {
							eq = ':';	// GNU-style ':='
							q[-2] = '\0';
						}
						break;
					}
# endif
# if ENABLE_FEATURE_MAKE_EXTENSIONS
				case '!':
					// ':=' and '!=' are non-POSIX extensions.
					if (posix)
						break;
					IF_FEATURE_MAKE_POSIX_202X(goto set_eq;)
# else
					break;
# endif
# if ENABLE_FEATURE_MAKE_POSIX_202X
				case '+':
				case '?':
					// '+=' and '?=' are from POSIX 202X.
					if (POSIX_2017)
						break;
 IF_FEATURE_MAKE_EXTENSIONS(set_eq:)
# endif
					eq = q[-1];
					q[-1] = '\0';
					break;
				}
			}
#endif
			*q++ = '\0';	// Separate name and value
			while (isspace(*q))
				q++;
			if ((p = strrchr(q, '\n')) != NULL)
				*p = '\0';

			// Expand left-hand side of assignment
			p = expanded = expand_macros(str, FALSE);
			if ((a = gettok(&p)) == NULL || gettok(&p))
				error("invalid macro assignment");

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_202X
			if (eq == ':') {
				// GNU-style ':='.  Expand right-hand side of assignment.
				// Macro is of type immediate-expansion.
				q = newq = expand_macros(q, FALSE);
				level |= M_IMMEDIATE;
			}
# if ENABLE_FEATURE_MAKE_POSIX_202X
			else if (eq == 'B') {
				// BSD-style ':='.  Expand right-hand side of assignment,
				// though not '$$'.  Macro is of type delayed-expansion.
				q = newq = expand_macros(q, TRUE);
			} else if (eq == '?' && getmp(a) != NULL) {
				// Skip assignment if macro is already set
				goto end_loop;
			} else if (eq == '+') {
				// Append to current value
				struct macro *mp = getmp(a);
				char *rhs;
				newq = mp && mp->m_val[0] ? xstrdup(mp->m_val) : NULL;
				if (mp && mp->m_immediate) {
					// Expand right-hand side of assignment (GNU make
					// compatibility)
					rhs = expand_macros(q, FALSE);
					level |= M_IMMEDIATE;
				} else {
					rhs = q;
				}
				newq = xappendword(newq, rhs);
				if (rhs != q)
					free(rhs);
				q = newq;
			}
# endif
# if ENABLE_FEATURE_MAKE_EXTENSIONS
			else if (eq == '!') {
				char *cmd = expand_macros(q, FALSE);
				q = newq = run_command(cmd);
				free(cmd);
			}
# endif
#endif
			setmacro(a, q, level);
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_202X
			free(newq);
#endif
			goto end_loop;
		}

		// If we get here it must be a target rule
		p = expanded = expand_macros(str, FALSE);

		// Look for colon separator
		q = find_char(p, ':');
		if (q == NULL)
			error("expected separator");

		*q++ = '\0';	// Separate targets and prerequisites

#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// Double colon
		dbl = !posix && *q == ':';
		if (dbl)
			q++;
#endif

		// Look for semicolon separator
		cp = NULL;
		s = strchr(q, ';');
		if (s) {
			*s = '\0';
			// Retrieve command from copy of line
			if ((p = find_char(copy, ':')) && (p = strchr(p, ';')))
				cp = newcmd(process_command(p + 1), cp);
		}
		semicolon_cmd = cp != NULL;

		// Create list of prerequisites
		dp = NULL;
		while (((p = gettok(&q)) != NULL)) {
#if !ENABLE_FEATURE_MAKE_EXTENSIONS
			np = newname(p);
			dp = newdep(np, dp);
#else
			char *newp = NULL;

			if (!posix) {
				// Allow prerequisites of form library(member1 member2).
				// Leading and trailing spaces in the brackets are skipped.
				if (!lib) {
					s = strchr(p, '(');
					if (s && !ends_with_bracket(s) && strchr(q, ')')) {
						// Looks like an unterminated archive member
						// with a terminator later on the line.
						lib = p;
						if (s[1] != '\0') {
							p = newp = xconcat3(lib, ")", "");
							s[1] = '\0';
						} else {
							continue;
						}
					}
				} else if (ends_with_bracket(p)) {
					if (*p != ')')
						p = newp = xconcat3(lib, p, "");
					lib = NULL;
					if (newp == NULL)
						continue;
				} else {
					p = newp = xconcat3(lib, p, ")");
				}
			}

			// If not in POSIX mode expand wildcards in the name.
			nfile = 1;
			files = &p;
			if (!posix && wildcard(p, &gd)) {
				nfile = gd.gl_pathc;
				files = gd.gl_pathv;
			}
			for (i = 0; i < nfile; ++i) {
				np = newname(files[i]);
				dp = newdep(np, dp);
			}
			if (files != &p)
				globfree(&gd);
			free(newp);
#endif /* ENABLE_FEATURE_MAKE_EXTENSIONS */
		}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		lib = NULL;
#endif

		// Create list of commands
		startno = dispno;
		while ((str2 = readline(fd)) && *str2 == '\t') {
			cp = newcmd(process_command(str2), cp);
			free(str2);
		}
		dispno = startno;

		// Create target names and attach rule to them
		q = expanded;
		count = 0;
		seen_inference = FALSE;
		while ((p = gettok(&q)) != NULL) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			// If not in POSIX mode expand wildcards in the name.
			nfile = 1;
			files = &p;
			if (!posix && wildcard(p, &gd)) {
				nfile = gd.gl_pathc;
				files = gd.gl_pathv;
			}
			for (i = 0; i < nfile; ++i)
# define p files[i]
#endif
			{
				int ttype = target_type(p);

				np = newname(p);
				if (ttype != T_NORMAL) {
					if (ttype == T_INFERENCE
							IF_FEATURE_MAKE_EXTENSIONS(&& posix)) {
						if (semicolon_cmd)
							error_in_inference_rule("'; command'");
						seen_inference = TRUE;
					}
					np->n_flag |= N_SPECIAL;
				} else if (!firstname) {
					firstname = np;
				}
				addrule(np, dp, cp, dbl);
				count++;
			}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
# undef p
			if (files != &p)
				globfree(&gd);
#endif
		}
		if (seen_inference && count != 1)
			error_in_inference_rule("multiple targets");

		// Prerequisites and commands will be unused if there were
		// no targets.  Avoid leaking memory.
		if (count == 0) {
			freedeps(dp);
			freecmds(cp);
		}
 end_loop:
		free(str1);
		dispno = lineno;
		str1 = str2 ? str2 : readline(fd);
		free(copy);
		free(expanded);
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if (first_line && findname(".POSIX")) {
			setenv("PDPMAKE_POSIXLY_CORRECT", "", 1);
			posix = TRUE;
		}
		first_line = FALSE;
#endif
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	// Conditionals aren't allowed to span files
	if (clevel != old_clevel)
		error("invalid conditional");
#endif
}
