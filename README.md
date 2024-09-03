### Public domain POSIX make

This is an implementation of [POSIX make](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/make.html).

It should build on most modernish Unix-style systems:

 - It comes with its own makefile, naturally, but if you don't have a `make` binary already the command `cc -o make *.c` should get you started.

 - Command line options may not work properly due to differences in how `getopt(3)` is reset.  Adjust `GETOPT_RESET()` in make.h for your platform, if necessary.

 - Building on MacOS may require a command like: `make CFLAGS="-D_DARWIN_C_SOURCE -Dst_mtim=st_mtimespec"`

Microsoft Windows users will find `pdpmake` included in the
[BusyBox for Windows](https://frippery.org/busybox/index.html) binaries.
Download an appropriate binary for your system and rename it `make.exe` or
`pdpmake.exe`. BusyBox for Windows includes a Unix shell and many utilities.
These can be used in Makefiles without any further setup.

The default configuration enables some non-POSIX extensions. Generally
these are compatible with GNU make:

 - double-colon rules
 - `ifdef`/`ifndef`/`ifeq`/`ifneq`/`else`/`endif` conditionals
 - `lib.a(mem1.o mem2.o...)` syntax for archive members
 - `:=` macro assignments (equivalent to POSIX `::=`)
 - chained inference rules
 - `*`/`?`/`[]` wildcards for filenames in target rules
 - the `$<` and `$*` internal macros are valid for target rules
 - skip duplicate entries in `$?`
 - `-C directory` command line option
 - `#` doesn't start a comment in macro expansions or build commands
 - `#` may be escaped with a backslash
 - macro definitions and targets can be mixed on the command line

When extensions are enabled adding the `.POSIX` target to your makefile
will disable them.  Other versions of make tend to allow extensions even
in POSIX mode.

Setting the environment variable `PDPMAKE_POSIXLY_CORRECT` (its value
doesn't matter) or giving the `--posix` option as the first on the
command line also turn off extensions.

[Additional information](https://frippery.org/make/index.html) and
[release notes](https://frippery.org/make/release-notes/current.html)
are available.
