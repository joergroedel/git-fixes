git-fixes - Find Upstream Fixes for Backported Commits
======================================================

Git-fixes scans a range of commit messages for references to a given list
of commit-ids. By default it only searches for "Fixes:" lines, but there is
also an option to check everything in the commit message that looks like a git
commit-id.

For SUSE kernel developers this repository also contains the git-suse helper.
It takes a base revision from the suse kernel repository and fetches a list of
backported commit-ids from it and writes them to a file or standard output. The
output format is the same as expected by git-fixes.

How to Build it
===============

To build the tools the libgit2 library is required. If you have a least
version 0.22.0 (with development packages) and the gnu C++ compiler
installed, just type:

	$ make

If that fails because your libgit2 installation is too old, then try:

	$ make BUILD_LIBGIT2=1

This will clone version 0.24.0 of libgit2, build it, and use it to
link git-fixes and git-suse. Note that you need to have the build
requirements of libgit2 installed. This includes cmake and the development
packages for openssl and zlib.

When the build completed successfully, you can do a 

	$ make install

to install the binarys. It will be installed into $HOME/bin by default.
That can be changed by passing the INSTALL\_DIR variable to make.

Getting Started
===============

To make any use of the git-fixes tool, you first need to create a list of
commits to scan for. The expected format is a set of lines like this:

	<commid-id>,<committer>

where commiter is usually the handle of the person that backported the
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

Creating Commit Lists
=====================

The git-suse tool creates commit lists from the SUSE kernel-source
repository. It takes a revision of the tree and extracts all commit-ids of
backported patches in that revision. It works a lot like the 'patches' tool
mentioned before, except that it processes git-objects instead of files on
disk. A simple usage could look like this:

	$ git suse --repo /path/to/kernel-source -f /tmp/SLE12-SP1.list origin/SLE12-SP1

This extracts the commits and writes them to /tmp/SLE12-SP1.list. The file can
then be used as input for git-fixes:

	$ git fixes -d /tmp/SLE12-SP1.list v3.12..linus/master

The git-suse tool can also be used to only extract newly backported commits.
When you backported a couple of upstream commits to SLE12-SP1 and want to
create a list of these patches, you can do:

	$ git suse -c --base origin/SLE12-SP1 users/your/backport/branch

This prints the list of commits to standard output (using -c option). The
full power of this shows when it gets combined with git-fixes to show if there
are upstream fixes for the stuff you just backported:

	$ git suse --repo /path/to/kernel-source -f /tmp/my.list --base origin/SLE12-SP1 users/your/backport/branch
	$ git fixes --repo /path/to/linus.git/ -f /tmp/my.list v3.12..origin/master

You can also redirect the output of git-suse (when called with -c and without
-f) to git-fixes (use -f - there).

More Options
============

TBD.
