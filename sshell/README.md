-*- Text -*-

Solution to Assignment 1 (CS 464/564)
by Stefan Bruda


Content

o  source code, included in sshell.cc
o  modules tokenize and tcp-utils as provided previously (note: the
   module tcp-utils is included because of the function `readline')
o  a makefile (the default target builds the executable, there are no
   other targets of interest)
o  a sample configuration file shconfig
o  a small file used for testing of command more (testmore)

Change the makefile and compile with -DDEBUG option for an executable
that prints debugging output to the standard error stream.


User guide and program description

There are no command line arguments for the executable.  Once
launched, the executable behaves as outlined in the assignment
handout, with the following differences/additions:

o  Command more accepts more than one filename as argument and displays
   them all.
o  Command more never goes into background, even if the prefixing `&'
   is given (case in which it is accepted and promptly ignored).

The shell attempts first to run the given external command without any
path prefix.  So commands such as

    & /usr/local/bin/test464

are accepted and run as expected (provided that the executable test464
is in /usr/local/bin).

A new process is fork-ed for a background command.  The new process
runs then the command as a normal, foreground command and reports
completion.

A typical zombie reaper is set up as signal handler for SIGCHLD.  This
reaper is inhibited temporarily when we actually want to wait for
child completion (in function run_it) by setting an empty function as
SIGCHLD handler in the master process.  As soon as the child responds
we restore the normal handler. (Note: we do need the dummy, empty
handler as the behaviour of setting SIG_IGN as handler for SIGCHLD is
undefined according to the POSIX standard.)


Tests

This particular program is simple enough to allow for an exhaustive
test suite, that contains the following test cases:

o  commands from /bin, /usr/bin, with absolute path, and nonexistent
   commands
o  commands that end up in errors (such as `ls nofile')
o  all of the above in background
o  more with various arguments (multiple files, unreadable files,
   nonexistent files)
o  error conditions for the configuration, such as unreadable or
   garbled configuration file; in particular, negative terminal
   dimensions were provided in the configuration file
o  special cases for input such as empty commands and EOFs.

Tests show a correctly working program, except for the problems
below.


Known bugs

Long enough lines in the files that are displayed by the command more
will result in a printout that contains portions of those lines other
than the first sequence of characters.

Commands longer than 128 characters are truncated.

The next page sequence for more is blank followed by enter, not just
blank (it seems pointless to turn buffered input off just for the sake
of more).
