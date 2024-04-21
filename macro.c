/*
 * Macro control for make
 */
#include "make.h"

struct macro *macrohead[HTABSIZE];

struct macro *
getmp(const char *name)
{
	struct macro *mp;

	for (mp = macrohead[getbucket(name)]; mp; mp = mp->m_next)
		if (strcmp(name, mp->m_name) == 0)
			return mp;
	return NULL;
}

static int
is_valid_macro(const char *name)
{
	const char *s;
	for (s = name; *s; ++s) {
		// In POSIX mode only a limited set of characters are guaranteed
		// to be allowed in macro names.
		if (IF_FEATURE_MAKE_EXTENSIONS(posix &&)
				((
					IF_FEATURE_MAKE_EXTENSIONS((pragma & P_MACRO_NAME) ||)
					(ENABLE_FEATURE_MAKE_POSIX_202X && !POSIX_2017)
				) ? !isfname(*s) : !ispname(*s)))
			return FALSE;
		// As an extension allow anything that can get through the
		// input parser, apart from the following.
		if (*s == '=')
			return FALSE;
#if ENABLE_FEATURE_MAKE_POSIX_202X
		if (isblank(*s) || iscntrl(*s))
			return FALSE;
#endif
	}
	return TRUE;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
static int
potentially_valid_macro(const char *name)
{
	int ret = FALSE;

	if (!(pragma & P_MACRO_NAME)) {
		pragma |= P_MACRO_NAME;
		ret = is_valid_macro(name);
		pragma &= ~P_MACRO_NAME;
	}
	return ret;
}
#endif

void
setmacro(const char *name, const char *val, int level)
{
	struct macro *mp;
	bool valid = level & M_VALID;
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_202X
	bool immediate = level & M_IMMEDIATE;
#endif

	level &= ~(M_IMMEDIATE | M_VALID);
	mp = getmp(name);
	if (mp) {
		// Don't replace existing macro from a lower level
		if (level > mp->m_level)
			return;

		// Replace existing macro
		free(mp->m_val);
	} else {
		// If not defined, allocate space for new
		unsigned int bucket;

		if (!valid && !is_valid_macro(name)) {
			// Silently drop invalid names from the environment
			if (level == 3)
				return;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			error("invalid macro name '%s'%s", name,
					potentially_valid_macro(name) ?
					": allow with pragma macro_name" : "");
#else
			error("invalid macro name '%s'", name);
#endif
		}

		bucket = getbucket(name);
		mp = xmalloc(sizeof(struct macro));
		mp->m_next = macrohead[bucket];
		macrohead[bucket] = mp;
		mp->m_flag = FALSE;
		mp->m_name = xstrdup(name);
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_202X
	mp->m_immediate = immediate;
#endif
	mp->m_level = level;
	mp->m_val = xstrdup(val ? val : "");
}

#if ENABLE_FEATURE_CLEAN_UP
void
freemacros(void)
{
	int i;
	struct macro *mp, *nextmp;

	for (i = 0; i < HTABSIZE; i++) {
		for (mp = macrohead[i]; mp; mp = nextmp) {
			nextmp = mp->m_next;
			free(mp->m_name);
			free(mp->m_val);
			free(mp);
		}
	}
}
#endif
