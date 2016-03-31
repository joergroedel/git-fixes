git-fixes - Find Upstream Fixes for Backported Commits
======================================================

Git-fixes scans a range of commit messages for references to a given list
of commit-ids. By default it only searches for "Fixes:" lines, but there is
also an option to check everything in the commit message that looks like a git
commit-id.

How to Build it
===============

To build git-fixes the libgit2 library is required. If you have a least
version 0.22.0 (with development packages) and the gnu C++ compiler
installed, just type:

	$ make

If that fails because your libgit2 installation is too old, then try:

	$ make BUILD_LIBGIT2=1

This will clone version 0.24.0 of libgit2, build it, and use it to
link git-fixes. Note that you need to have the build requirements of
libgit2 installed. This includes cmake and the development packages for
openssl and zlib.

When the build completed successfully, you can do a 

	$ make install

to install the binary. It will be installed into $HOME/bin by default.
That can be changed by passing the INSTALL\_DIR variable to make.

Getting Started
===============

To make any use of the tool, you first need to create a list of commits
to scan for. The expected format is a set of lines like this:

	<commid-id>,<committer>

where commiter is usually the handle of the person that backported to
commit. The commit-id needs to be a complete git commit-id. No comments are
allowed in the input.

A python script to create such a list from a SUSE kernel-source branch
is part of this repository. Go to the kernel-source directory and call

	$ /path/to/source/of/git-fixes/patches > /tmp/suse-patches.list

Now you created a file with all backported commits in the current
kernel-source branch. Now you can go to a linux git-tree and try

	$ git-fixes -f /tmp/suse-patches.list

This will create a list of commits from HEAD to the root-commit which
reference any of the commits in /tmp/suse-patches.list, grouped by the
committer.

To safe some time it is strongly recommended to limit the commit-range
that is scanned. You can specify a revision range similar to git-log:

	$ git-fixes -f /tmp/suse-patches.list v3.12..

For more convenience you can put the commit-list file to a permanent
place and tell git-fixes where to find it. Run this on your upstream git
repository:

	$ git config fixes.file ~/some/path/suse-patches.list

Now you can run git-fixes without the -f option. Multiple permanent
files are also supported. When you set the config like this:

	$ git config fixes.sle11sp4.file ~/path/to/sle11sp4.list
	$ git config fixes.sle12sp1.file ~/path/to/sle12sp1.list

you can run

	$ git-fixes -d sle11sp4
	$ git-fixes -d sle12sp1

and check against different commit-lists. 

More Options
============

TBD.
