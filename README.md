### Public domain POSIX make

This is an implementation of [POSIX make](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/make.html).

It comes with its own makefile, naturally, and should build on most
modernish Unix-style systems.  (Command line options may not work
properly due to differences in how `getopt(3)` is reset.  Adjust
`GETOPT_RESET()` in make.h for your platform, if necessary.)

The default configuration enables extensions:  some from a future POSIX
standard and some that are non-POSIX.  Generally these extensions are
compatible with GNU make:

 - double-colon rules
 - `-include` to ignore missing include files
 - include files can be created if required
 - `ifdef`/`ifndef`/`else`/`endif` conditionals
 - `lib.a(mem1.o mem2.o...)` syntax for archive members
 - `:=`/`::=`/`:::=`/`+=`/`?=`/`!=` macro assignments
 - macro expansions can be nested
 - chained inference rules
 - `*`/`?`/`[]` wildcards for filenames in target rules
 - `$(SRC:%.c=%.o)` pattern macro expansions
 - special handling of `MAKE` macro
 - `$^` and `$+` internal macros
 - skip duplicate entries in `$^` and `$?`
 - `.PHONY` special target
 - `-C directory` and `-j maxjobs` command line options

When extensions are enabled adding the `.POSIX` target to your makefile
will disable them.  Other versions of make tend to allow extensions even
in POSIX mode.

Setting the environment variable `PDPMAKE_POSIXLY_CORRECT` (its value
doesn't matter) or giving the `--posix` option as the first on the
command line also turn off extensions.
