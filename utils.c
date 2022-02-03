/*
 * Utility functions.
 */
#include "make.h"

/*
 * Error handler.  Print message, with line number, and exit.
 */
void
error(const char *msg, ...)
{
	va_list list;

	if (makefile)
		fprintf(stderr, "%s:%d: ", makefile, dispno);
	else
		fprintf(stderr, "%s: ", myname);
	va_start(list, msg);
	vfprintf(stderr, msg, list);
	va_end(list);
	fputc('\n', stderr);
	exit(2);
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
void
error_unexpected(const char *s)
{
	error("unexpected %s", s);
}
#endif

void
error_in_inference_rule(const char *s)
{
	error("%s in inference rule", s);
}

void
warning(const char *msg, ...)
{
	va_list list;

	fprintf(stderr, "%s: ", myname);
	va_start(list, msg);
	vfprintf(stderr, msg, list);
	va_end(list);
	fputc('\n', stderr);
}

void *
xmalloc(size_t len)
{
	void *ret = malloc(len);
	if (ret == NULL && errno == ENOMEM)
		error("out of memory");
	return ret;
}

void *
xrealloc(void *ptr, size_t len)
{
	void *ret = realloc(ptr, len);
	if (ret == NULL && errno == ENOMEM)
		error("out of memory");
	return ret;
}

char *
xconcat3(const char *s1, const char *s2, const char *s3)
{
	size_t len = strlen(s1) + strlen(s2) + strlen(s3) + 1;
	char *t = xmalloc(len);
	return strcat(strcat(strcpy(t, s1), s2), s3);
}

char *xstrdup(const char *s)
{
	size_t len = strlen(s) + 1;
	char *t = xmalloc(len);
	return memcpy(t, s, len);
}

/*
 * Append a word to a space-separated string of words.  The first
 * call should use a NULL pointer for str, subsequent calls should
 * pass an allocated string which will be freed.
 */
char *
xappendword(const char *str, const char *word)
{
	char *newstr = str ? xconcat3(str, " ", word) : xstrdup(word);
	free((void *)str);
	return newstr;
}
