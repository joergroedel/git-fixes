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

std::string path_map_file;

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
std::vector<std::string> ignore_params;
std::map<std::string, bool> ignore;
std::vector<std::string> params;
std::set<std::string> paths;
std::string repo_path = ".";

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
static int diff_file_cb(const git_diff_delta *delta, float progess, void *data)
{
	paths.emplace(delta->new_file.path);

	return 0;
}

static bool get_paths_from_commit(git_commit *commit, size_t idx)
{
	git_commit *parent;
	git_tree *a, *b;
	git_diff *diff;
	int error;
	bool ret;

	ret = false;
	error = git_commit_parent(&parent, commit, 0);
	if (error)
		goto out;

	error = git_commit_tree(&a, parent);
	if (error)
		goto out_parent;

	error = git_commit_tree(&b, commit);
	if (error)
		goto out_tree_a;

	error = git_diff_tree_to_tree(&diff, git_commit_owner(commit), a, b, NULL);
	if (error)
		goto out_tree_b;

#if LIBGIT2_VER_MAJOR == 0 && LIBGIT2_VER_MINOR < 23
	error = git_diff_foreach(diff, diff_file_cb, NULL, NULL, NULL);
#else
	error = git_diff_foreach(diff, diff_file_cb, NULL, NULL, NULL, NULL);
#endif
	if (error)
		goto out_diff;

	ret = true;
out_diff:
	git_diff_free(diff);

out_tree_b:
	git_tree_free(b);

out_tree_a:
	git_tree_free(a);

out_parent:
	git_commit_free(parent);

out:
	return ret;
}

static int treewalk_cb(const char *root, const git_tree_entry *e, void *d)
{
	std::string path = root;

	paths.emplace(path + git_tree_entry_name(e));

	return 0;
}

static bool get_paths_from_revision(git_repository *repo, std::string rev)
{
	unsigned int parents;
	git_commit *commit;
	git_object *obj;
	int error;
	bool ret;

	error = git_revparse_single(&obj, repo, rev.c_str());
	if (error)
		return false;

	ret = false;
	error = git_commit_lookup(&commit, repo, git_object_id(obj));
	if (error)
		goto out_obj_free;

	parents = git_commit_parentcount(commit);

	if (parents == 0) {
		git_tree *tree;

		error = git_commit_tree(&tree, commit);
		if (error)
			goto out_commit_free;

		error = git_tree_walk(tree, GIT_TREEWALK_PRE, treewalk_cb, NULL);
		git_tree_free(tree);
		if (error)
			goto out_commit_free;
	} else {
		for (unsigned int i = 0; i < parents; ++i) {
			ret = get_paths_from_commit(commit, i);
			if (ret)
				goto out_commit_free;
		}
	}

	ret = true;

out_commit_free:
	git_commit_free(commit);

out_obj_free:
	git_object_free(obj);

	return ret;
}

static void match_paths(void)
{
	bool do_ignore = false;
	struct people results;

	for (auto path : paths) {
		auto pos = path_map.find(path);

		while (path != "" && pos == path_map.end()) {
			auto slash = path.find_last_of("/");
			if (slash == std::string::npos)
				path = "";
			else
				path = path.substr(0, slash);
			pos = path_map.find(path);
		}

		if (pos == path_map.end())
			continue;

		results = results + path_map[path];
	}

	std::sort(results.persons.rbegin(), results.persons.rend());

	for (auto &p : results.persons) {
		auto pos = ignore.find(p.name);

		if (pos == ignore.end()) {
			do_ignore = true;
			break;
		}
	}

	for (auto &p : results.persons) {
		if (do_ignore) {
			auto pos = ignore.find(p.name);
			if (pos != ignore.end())
				continue;
		}

		std::cout << p.name << " (" << p.count << ")" << std::endl;
	}
}

enum {
	OPTION_HELP,
	OPTION_PATH_MAP,
	OPTION_REPO,
	OPTION_IGNORE,
};

static struct option options[] = {
	{ "help",               no_argument,            0, OPTION_HELP           },
	{ "path-map",           required_argument,      0, OPTION_PATH_MAP       },
	{ "repo",               required_argument,      0, OPTION_REPO           },
	{ "ignore",             required_argument,      0, OPTION_IGNORE         },
	{ 0,                    0,                      0, 0                     }
};

static void usage(const char *prg)
{
	std::cout << "Usage " << prg << " [OPTIONS] [REVISION/PATH ...]" << std::endl;
	std::cout << "Options" << std::endl;
	std::cout << "  --help, -h              Print this help message" << std::endl;
	std::cout << "  --path-map, -p <file>   File containing the path-map data" << std::endl;
	std::cout << "  --repo, -r <path>       Path to git repository" << std::endl;
	std::cout << "  --ignore, -i <user>     Email address to ignore (if possible)" << std::endl;
}

static bool parse_options(int argc, char **argv)
{
	int c;

	while (true) {
		int opt_idx;

		c = getopt_long(argc, argv, "hp:r:i:", options, &opt_idx);
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
		default:
			usage(argv[0]);
			return false;
		}
	}

	while (optind < argc)
		params.emplace_back(argv[optind++]);

	return true;
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

	error = git_repository_open(&repo, repo_path.c_str());
	if (error < 0)
		goto error;

	for (auto &p : params) {
		if (!get_paths_from_revision(repo, p))
			// param is not a revision, treat as path
			paths.emplace(p);
	}

	for (auto &i : ignore_params)
		ignore[i] = true;

	match_paths();

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
