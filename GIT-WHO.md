git-who - Find the Right Person to Backport a Patch
===================================================

With the git-who tool you can easily find the downstream maintainers of
a file or of certain areas of code. I wrote it for the kernel-source.git
repository used within SUSE to maintain the kernels for the enterprise
and community distributions. Here is how to make use of it.

The tool works on a path-map file that maps source code paths and files
to developers. Each developer also has a score for any given entry.

Creating a path-map File
========================

The best way to create a path-map file for kernel-source.git is to use
the git-suse tool:

	kernel-source.git $ git-suse -f commits --path-map ~/path/to/path-map HEAD

Now you have a path-map file to use with git-who.

Using git-who
=============

That path-map file can be used with git-who now:

	linux.git $ git-who -p ~/path/to/path-map drivers/iommu/

This will tell you who touched the drivers/iommu/ path the most with
existing backports and who are thus the best persons to contact for this
area of code.

Instead of paths you can also pass commit-ids to git-who:

	linux.git $ git-who -p ~/path/to/path-map c37a01779b39

Here git-who will lookup the commit and match the path-map against all
files that the given commit touches.

You can also pass multiple commits and multiple paths to git-who. The
matching will then be done against a combined path-list extracted from
the commits and the paths passed on the command line.

Ignore-Lists
============

Some people are reported quite too often by git-who, like the people
doing the stable backports to kernel-source.git. To avoid that these
people are reported, git-who can use ignore-lists.

Note that the ignore-list is only applied when there is at least one
non-ignored developer in the results-list. To remove a given developer
from the results, use the -i option:

	linux.git $ git-who -p ~/path/to/path-map -i stable-backporter@example.com c37a01779b39

You can also pass a file-name with one ignored developer per line to -i,
or just use -i multiple times.

Store Path-Map and Ignore-List in Git Config
============================================

Instead of always passing the path-map or the ignore list to git-who
manually, you can also store their locations in git-config among the
configuration for git-fixes:

	linux.git $ git config fixes.sle12sp2.pathmap ~/path/to/path-map
	linux.git $ git config fixes.sle12sp2.ignore ~/path/to/ignore.list

Now you can save some typing:

	linux.git $ git-who -d sle12sp2 c37a01779b39

Have fun with the tool and report any bugs, wishes and feature requests
to jroedel <at> suse.de.
