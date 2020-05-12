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

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <git2.h>

#include "who.h"

static std::string path_map_file;
static std::string db;
static std::vector<std::string> ignore_params;
static std::vector<std::string> params;
static std::map<std::string, bool> ignore;

std::string repo_path = ".";

static std::string trim(const std::string &line)
{
	static const char *spaces = " \n\t\r";
	size_t pos1, pos2;

	pos1 = line.find_first_not_of(spaces);
	pos2 = line.find_last_not_of(spaces);

	if (pos1 == std::string::npos)
		return std::string("");

	return line.substr(pos1, pos2-pos1+1);
}

static bool ignore_from_file(std::string filename)
{
	std::ifstream file;
	std::string line;

	file.open(filename.c_str());
	if (!file.is_open())
		return false;

	while (getline(file, line)) {
		line = trim(line);

		auto pos = line.find_first_of("#");
		if (pos != std::string::npos)
			line = trim(line.substr(0, pos));

		if (line == "")
			continue;

		ignore[line] = true;
	}

	file.close();

	return true;
}

enum {
	OPTION_HELP,
	OPTION_PATH_MAP,
	OPTION_REPO,
	OPTION_IGNORE,
	OPTION_DB,
};

static struct option options[] = {
	{ "help",               no_argument,            0, OPTION_HELP           },
	{ "path-map",           required_argument,      0, OPTION_PATH_MAP       },
	{ "repo",               required_argument,      0, OPTION_REPO           },
	{ "ignore",             required_argument,      0, OPTION_IGNORE         },
	{ "database",		required_argument,	0, OPTION_DB		 },
	{ 0,                    0,                      0, 0                     }
};

static void usage(const char *prg)
{
	std::cout << "Usage " << prg << " [OPTIONS] [REVISION/PATH ...]" << std::endl;
	std::cout << "Options" << std::endl;
	std::cout << "  --help, -h                Print this help message" << std::endl;
	std::cout << "  --path-map, -p <file>     File containing the path-map data" << std::endl;
	std::cout << "  --repo, -r <path>         Path to git repository" << std::endl;
	std::cout << "  --ignore, -i <user/file>  Email address to ignore (if possible)" << std::endl;
	std::cout << "                            If the parameter is a file, the email addresses" << std::endl;
	std::cout << "                            are read from there" << std::endl;
	std::cout << "  --database, -d <name>     Select database (set fixes.<name>.pathmap and " << std::endl;
	std::cout << "                            fixes.<name>.ignore)" << std::endl;
}

static bool parse_options(int argc, char **argv)
{
	int c;

	while (true) {
		int opt_idx;

		c = getopt_long(argc, argv, "hp:r:i:d:", options, &opt_idx);
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
		case OPTION_REPO:
		case 'r':
			repo_path = optarg;
			break;
		case OPTION_IGNORE:
		case 'i':
			ignore_params.emplace_back(optarg);
			break;
		case OPTION_DB:
		case 'd':
			db = optarg;
			break;
		default:
			usage(argv[0]);
			return false;
		}
	}

	while (optind < argc)
		params.emplace_back(argv[optind++]);

	return true;
}

static std::string config_get_string_nofail(git_config *cfg, const char *name)
{
#if LIBGIT2_VER_MAJOR == 0 && LIBGIT2_VER_MINOR < 23
	const git_config_entry *entry;
#else
	git_config_entry *entry;
#endif
	std::string ret;

	if (git_config_get_entry(&entry, cfg, name))
		return ret;

	ret = entry->value;

#if LIBGIT2_VER_MAJOR > 0 || LIBGIT2_VER_MINOR >= 23
	git_config_entry_free(entry);
#endif

	return ret;
}

static std::string config_get_path_nofail(git_config *cfg, const char *name)
{
        std::string path;

        path = config_get_string_nofail(cfg, name);
        auto len  = path.length();

        if (!len || path[0] != '~')
                return path;

        if (len > 1 && path[1] != '/')
                return path;

        return std::string(getenv("HOME")) + path.substr(1);
}

void load_git_config(git_repository *repo)
{
	git_config *repo_cfg;
	std::string key, val;
	int error;

	error = git_repository_config(&repo_cfg, repo);
	if (error)
		return;

	key = "fixes." + db + ".pathmap";
	val = config_get_path_nofail(repo_cfg, key.c_str());
	if (path_map_file == "" && val != "")
		path_map_file = val;

	key = "fixes." + db + ".ignore";
	val = config_get_path_nofail(repo_cfg, key.c_str());
	if (val != "")
		ignore_params.emplace_back(val);

	git_config_free(repo_cfg);
}

static void print_results(struct people &results)
{
	bool do_ignore = false;

	// Check if there are unignored people in the list
	for (auto &p : results.persons) {
		auto pos = ignore.find(p.name);

		if (pos == ignore.end()) {
			do_ignore = true;
			break;
		}
	}

	// Print results
	for (auto &p : results.persons) {
		if (do_ignore) {
			auto pos = ignore.find(p.name);
			if (pos != ignore.end())
				continue;
		}

		std::cout << p.name << " (" << p.count << ")" << std::endl;
	}
}

int main(int argc, char **argv)
{
	git_repository *repo = NULL;
	struct people results;
	int ret, error;
	git_who who;

	ret = 1;
	if (!parse_options(argc, argv))
		goto out;

	git_libgit2_init();

	error = git_repository_open(&repo, repo_path.c_str());
	if (error < 0)
		goto error;

	if (db != "")
		load_git_config(repo);

	ret = who.load_path_map(path_map_file);
	if (ret)
		goto out_repo;

	for (auto &p : params) {
		if (!who.get_paths_from_revision(repo, p))
			// param is not a revision, treat as path
			who.add_path(p);
	}

	for (auto &i : ignore_params) {
		if (!ignore_from_file(i))
			ignore[i] = true;
	}

	who.match_paths(results);

	print_results(results);

	ret = 0;

out_repo:
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
