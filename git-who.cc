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

#include <unistd.h>
#include <getopt.h>
#include <git2.h>

std::string path_map_file;
std::string revision;
std::string file;

enum {
	OPTION_HELP,
	OPTION_PATH_MAP,
	OPTION_FILE,
};

static struct option options[] = {
	{ "help",               no_argument,            0, OPTION_HELP           },
	{ "path-map",           required_argument,      0, OPTION_PATH_MAP       },
	{ "file",               required_argument,      0, OPTION_FILE           },
	{ 0,                    0,                      0, 0                     }
};

static void usage(const char *prg)
{
	std::cout << "Usage " << prg << " [OPTIONS] [REVISION]" << std::endl;
	std::cout << "Options" << std::endl;
	std::cout << "  --help, -h              Print this help message" << std::endl;
	std::cout << "  --path-map, -p <file>   File containing the path-map data" << std::endl;
	std::cout << "  --file <file/path>      Find potential maintainers for given source" << std::endl;
	std::cout << "                          file or path" << std::endl;
}

static bool parse_options(int argc, char **argv)
{
	int c;

	while (true) {
		int opt_idx;

		c = getopt_long(argc, argv, "hp:f:", options, &opt_idx);
		if (c == -1)
			break;

		switch (c) {
		case OPTION_HELP:
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		case OPTION_PATH_MAP:
		case 'p':
			path_map_file = optarg;
			break;
		case OPTION_FILE:
		case 'f':
			file = optarg;
			break;
		default:
			usage(argv[0]);
			return false;
		}
	}

	if (optind < argc)
		revision = argv[optind++];

	return true;
}

int main(int argc, char **argv)
{
	git_repository *repo = NULL;
	int ret, error;

	ret = 1;
	if (!parse_options(argc, argv))
		goto out;

	git_libgit2_init();

	error = git_repository_open(&repo, ".");
	if (error < 0)
		goto error;

	git_repository_free(repo);

	ret = 0;
out:
	git_libgit2_shutdown();

	return ret;

error:
	auto e = giterr_last();
	std::cerr << "Error: " << e->message << std::endl;
	ret = 1;

	goto out;
}
