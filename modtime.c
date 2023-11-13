/*
 * Get modification time of file or archive member
 */
#include "make.h"

#ifdef _WIN32
# define ARMAG	"!<arch>\n"
# define ARFMAG	"`\n"
# define SARMAG	8
struct ar_hdr {
	char ar_name[16];
	char ar_date[12];
	char ar_uid[6], ar_gid[6];
	char ar_mode[8];
	char ar_size[10];
	char ar_fmag[2];
};
#else
# include <ar.h>
#endif

/*
 * Read a number from an archive header.
 */
static size_t
argetnum(const char *str, int len)
{
	const char *s;
	size_t val = 0;

	for (s = str; s < str + len && isdigit(*s); s++) {
		// Half-hearted attempt to avoid overflow
		if (val > (INT_MAX - 1)/10)
			break;
		val = val * 10 + *s - '0';
	}
	if (s != str + len && *s != ' ')
		error("invalid archive");
	return val;
}

/*
 * Search an archive for the given member and return its timestamp or 0.
 * This code assumes System V/GNU archive format.
 */
static time_t
arsearch(FILE *fd, const char *member)
{
	struct ar_hdr hdr;
	char *s, *t, *names = NULL;
	size_t len, offset, max_offset = 0;
	time_t mtime = 0;

	do {
 top:
		len = fread(&hdr, 1, sizeof(hdr), fd);
		if (len < sizeof(hdr) ||
				memcmp(hdr.ar_fmag, ARFMAG, sizeof(hdr.ar_fmag)) != 0) {
			if (feof(fd))
				break;
			error("invalid archive");
		}

		// Get length of this member.  Length in the file is padded
		// to an even number of bytes.
		len = argetnum(hdr.ar_size, sizeof(hdr.ar_size));
		if (len % 2 == 1)
			len++;

		t = hdr.ar_name;
		if (hdr.ar_name[0] == '/') {
			if (hdr.ar_name[1] == ' ') {
				// Skip symbol table
				continue;
			} else if (hdr.ar_name[1] == '/' && names == NULL) {
				// Save list of extended filenames for later use
				names = xmalloc(len);
				if (fread(names, 1, len, fd) != len)
					error("invalid archive");
				// Replace newline separators with NUL
				for (s = names; s < names + len; s++) {
					if (*s == '\n')
						*s = '\0';
				}
				max_offset = len;
				goto top;
			} else if (isdigit(hdr.ar_name[1]) && names) {
				// An extended filename, get its offset in the names list
				offset = argetnum(hdr.ar_name + 1, sizeof(hdr.ar_name) - 1);
				if (offset > max_offset)
					error("invalid archive");
				t = names + offset;
			} else {
				error("invalid archive");
			}
		}

		s = strchr(t, '/');
		if (s == NULL)
			error("invalid archive");
		*s = '\0';

		if (strcmp(t, member) == 0) {
			mtime = argetnum(hdr.ar_date, sizeof(hdr.ar_date));
			break;
		}
	} while (fseek(fd, len, SEEK_CUR) == 0);
	free(names);
	return mtime;
}

static time_t
artime(const char *archive, const char *member)
{
	FILE *fd;
	char magic[SARMAG];
	size_t len;
	time_t mtime;

	fd = fopen(archive, "r");
	if (fd == NULL)
		return 0;

	len = fread(magic, 1, sizeof(magic), fd);
	if (len < sizeof(magic) || memcmp(magic, ARMAG, SARMAG) != 0)
		error("%s: not an archive", archive);

	mtime = arsearch(fd, member);
	fclose(fd);
	return mtime;
}

/*
 * If the name is of the form 'libname(member.o)' split it into its
 * name and member parts and set the member pointer to point to the
 * latter.  Otherwise just take a copy of the name and don't alter
 * the member pointer.
 *
 * In either case the return value is an allocated string which must
 * be freed by the caller.
 */
char *
splitlib(const char *name, char **member)
{
	char *s, *t;
	size_t len;

	t = xstrdup(name);
	s = strchr(t, '(');
	if (s) {
		// We have 'libname(member.o)'
		*s++ = '\0';
		len = strlen(s);
		if (len <= 1 || s[len - 1] != ')' || *t == '\0')
			error("invalid name '%s'", name);
		s[len - 1] = '\0';
		*member = s;
	}
	return t;
}

/*
 * Get the modification time of a file.  Set it to 0 if the file
 * doesn't exist.
 */
void
modtime(struct name *np)
{
	char *name, *member = NULL;
	struct stat info;

	name = splitlib(np->n_name, &member);
	if (member) {
		// Looks like library(member)
		np->n_tim.tv_sec = artime(name, member);
		np->n_tim.tv_nsec = 0;
	} else if (stat(name, &info) < 0) {
		if (errno != ENOENT)
			error("can't open %s: %s", name, strerror(errno));
		np->n_tim.tv_sec = 0;
		np->n_tim.tv_nsec = 0;
	} else {
#ifdef _WIN32
		np->n_tim.tv_sec = info.st_mtime;
		np->n_tim.tv_nsec = 0;
#else
		np->n_tim.tv_sec = info.st_mtim.tv_sec;
		np->n_tim.tv_nsec = info.st_mtim.tv_nsec;
#endif
	}
	free(name);
}
