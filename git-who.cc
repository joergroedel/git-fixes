/*
 * Copyright (c) 2017 SUSE Linux GmbH
 *
 * Licensed under the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 *
 * See http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * for details.
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#include <iostream>
#include <string>

#include <git2.h>

int main(int argc, char **argv)
{
	git_repository *repo = NULL;
	int ret = 0, error;

	git_libgit2_init();

	error = git_repository_open(&repo, ".");
	if (error < 0)
		goto error;

	git_repository_free(repo);

out:
	git_libgit2_shutdown();

	return ret;

error:
	auto e = giterr_last();
	std::cerr << "Error: " << e->message << std::endl;
	ret = 1;

	goto out;
}
