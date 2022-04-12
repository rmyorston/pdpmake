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

void
setmacro(const char *name, const char *val, int level)
{
	struct macro *mp;
	const char *s;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	bool simple = FALSE;

	if ((level & M_SIMPLE)) {
		simple = TRUE;
		level &= ~M_SIMPLE;
	}
#endif
	for (s = name; *s; ++s) {
		if (*s == '=' || isspace(*s)) {
			error("invalid macro name");
		}
	}

	mp = getmp(name);
	if (mp) {
		// Don't replace existing macro from a lower level
		if (level > mp->m_level)
			return;

		// Replace existing macro
		free(mp->m_val);
	} else {
		// If not defined, allocate space for new
		unsigned int bucket = getbucket(name);
		mp = xmalloc(sizeof(struct macro));
		mp->m_next = macrohead[bucket];
		macrohead[bucket] = mp;
		mp->m_flag = FALSE;
		mp->m_name = xstrdup(name);
		mp->m_level = level;
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	mp->m_simple = simple;
#endif
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
