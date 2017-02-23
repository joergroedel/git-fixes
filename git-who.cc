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
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <git2.h>

std::string path_map_file;
std::string revision;
std::string file;

struct person {
	std::string name;
	int count;

	bool operator<(const struct person &p) const
	{
		return count < p.count;
	}
};

struct people {
	std::vector<struct person> persons;

	void add_one(const struct person &p)
	{
		for (auto &i : persons) {
			if (p.name == i.name) {
				i.count += p.count;
				return;
			}
		}

		persons.push_back(p);
	}

	struct people &operator+(const struct people &p)
	{
		for (auto &i : p.persons)
			add_one(i);

		return *this;
	}
};

std::map<std::string, struct people> path_map;

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

static int load_path_map(void)
{
	std::ifstream file;
	std::string line;

	file.open(path_map_file.c_str());
	if (!file.is_open()) {
		std::cerr << "Can't open path-map file: " << path_map_file << std::endl;
		return 1;
	}

	while (getline(file, line)) {
		struct people people;

		auto pos = line.find_first_of(";");
		if (pos == std::string::npos)
			continue;

		auto path = line.substr(0, pos);
		line = line.substr(pos + 1);

		while (line.length() > 0) {
			std::string token;

			pos = line.find_first_of(";");
			if (pos != std::string::npos) {
				token = line.substr(0, pos);
				line = line.substr(pos + 1);
			} else {
				token = line;
				line = "";
			}

			pos = token.find_first_of(":");
			if (pos == std::string::npos)
				continue;

			auto name  = token.substr(0, pos);
			auto count = token.substr(pos + 1);

			struct person p;

			p.name = name;
			p.count = atoi(count.c_str());

			people.add_one(p);
		}

		path_map[path] = people;
	}

	file.close();

	return 0;
}

int main(int argc, char **argv)
{
	git_repository *repo = NULL;
	int ret, error;

	ret = 1;
	if (!parse_options(argc, argv))
		goto out;

	ret = load_path_map();
	if (ret)
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
