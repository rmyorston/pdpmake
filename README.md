### Public domain POSIX make

This is an implementation of [POSIX make](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/make.html).

It comes with its own makefile, naturally, and should build on most
modernish Unix-style systems.

In its default configuration only POSIX features are supported.  Some
extensions can be enabled by setting `ENABLE_FEATURE_MAKE_EXTENSIONS`
to 1.  These are largely compatible with GNU make:

 - double-colon rules
 - `-include` to ignore missing include files
 - `ifdef`/`ifndef`/`else`/`endif` conditionals
 - `lib.a(mem1.o mem2.o...)` syntax for archive members
 - `:=`/`+=`/`?=` macro assignments
 - macro expansions can be nested
 - chained inference rules

When extensions are enabled adding the `.POSIX` target to your makefile
will disable them.  Other versions of make tend to allow extensions even
in POSIX mode.
