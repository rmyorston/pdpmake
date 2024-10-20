/*
 * Utility functions.
 */
#include "make.h"

/*
 * Print message, with makefile and line number if possible.
 */
static void
vwarning(FILE *stream, const char *msg, va_list list)
{
	if (makefile)
		fprintf(stream, "%s: (%s:%d): ", myname, makefile, dispno);
	else
		fprintf(stream, "%s: ", myname);
	vfprintf(stream, msg, list);
	fputc('\n', stream);
}

/*
 * Diagnostic handler.  Print message to standard error.
 */
void
diagnostic(const char *msg, ...)
{
	va_list list;

	va_start(list, msg);
	vwarning(stderr, msg, list);
	va_end(list);
}

/*
 * Error handler.  Print message and exit.
 */
void
error(const char *msg, ...)
{
	va_list list;

	va_start(list, msg);
	vwarning(stderr, msg, list);
	va_end(list);
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
error_not_allowed(const char *s, const char *t)
{
	error("%s not allowed for %s", s, t);
}

void
warning(const char *msg, ...)
{
	va_list list;

	va_start(list, msg);
	vwarning(stdout, msg, list);
	va_end(list);
}

void *
xmalloc(size_t len)
{
	void *ret = malloc(len);
	if (ret == NULL)
		error("out of memory");
	return ret;
}

void *
xrealloc(void *ptr, size_t len)
{
	void *ret = realloc(ptr, len);
	if (ret == NULL)
		error("out of memory");
	return ret;
}

char *
xconcat3(const char *s1, const char *s2, const char *s3)
{
	const size_t len1 = strlen(s1);
	const size_t len2 = strlen(s2);
	const size_t len3 = strlen(s3);

	char *t = xmalloc(len1 + len2 + len3 + 1);
	char *s = t;

	s = (char *)memcpy(s, s1, len1) + len1;
	s = (char *)memcpy(s, s2, len2) + len2;
	s = (char *)memcpy(s, s3, len3) + len3;
	*s = '\0';

	return t;
}

char *
xstrdup(const char *s)
{
	size_t len = strlen(s) + 1;
	char *t = xmalloc(len);
	return memcpy(t, s, len);
}

char *
xstrndup(const char *s, size_t n)
{
	char *t = strndup(s, n);
	if (t == NULL)
		error("out of memory");
	return t;
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

unsigned int
getbucket(const char *name)
{
	unsigned int hashval = 0;
	const unsigned char *p = (unsigned char *)name;

	while (*p)
		hashval ^= (hashval << 5) + (hashval >> 2) + *p++;
	return hashval % HTABSIZE;
}

/*
 * Add a file to the end of the supplied list of files.
 * Return the new head pointer for that list.
 */
struct file *
newfile(char *str, struct file *fphead)
{
	struct file *fpnew;
	struct file *fp;

	fpnew = xmalloc(sizeof(struct file));
	fpnew->f_next = NULL;
	fpnew->f_name = xstrdup(str);

	if (fphead == NULL)
		return fpnew;

	for (fp = fphead; fp->f_next; fp = fp->f_next)
		;

	fp->f_next = fpnew;

	return fphead;
}

#if ENABLE_FEATURE_CLEAN_UP
void
freefiles(struct file *fp)
{
	struct file *nextfp;

	for (; fp; fp = nextfp) {
		nextfp = fp->f_next;
		free(fp->f_name);
		free(fp);
	}
}
#endif
