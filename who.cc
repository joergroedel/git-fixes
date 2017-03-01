#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include <git2.h>

#include "who.h"

int git_who::load_path_map(std::string filename)
{
	std::ifstream file;
	std::string line;

	file.open(filename.c_str());
	if (!file.is_open()) {
		std::cerr << "Can't open path-map file: " << filename << std::endl;
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
	git_who *w = static_cast<git_who *>(data);

	w->add_path(delta->new_file.path);

	return 0;
}

void git_who::add_path(std::string path)
{
	paths.emplace(std::move(path));
}

bool git_who::get_paths_from_commit(git_commit *commit, size_t idx)
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
	error = git_diff_foreach(diff, diff_file_cb, NULL, NULL, this);
#else
	error = git_diff_foreach(diff, diff_file_cb, NULL, NULL, NULL, this);
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

static int treewalk_cb(const char *root, const git_tree_entry *e, void *data)
{
	git_who *w = static_cast<git_who *>(data);
	std::string path = root;

	path += git_tree_entry_name(e);
	w->add_path(std::move(path));

	return 0;
}

bool git_who::get_paths_from_revision(git_repository *repo, std::string rev)
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

		error = git_tree_walk(tree, GIT_TREEWALK_PRE, treewalk_cb, this);
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

static bool is_prefix(std::string prefix, std::string value)
{
	auto len1 = prefix.length();
	auto len2 = value.length();

	if (len1 > len2)
		return false;

	return (value.substr(0, len1) == prefix);
}

void git_who::match_paths(struct people &results)
{
	std::set<std::string> prefix_paths, new_paths;

	// First build a prefix map for unknown paths
	for (auto path : paths) {
		auto pos = path_map.find(path);

		// We care only about prefixes for unknown paths
		if (pos != path_map.end())
			continue;

		while (path != "" && pos == path_map.end()) {
			auto slash = path.find_last_of("/");
			if (slash == std::string::npos)
				path = "";
			else
				path = path.substr(0, slash);
			pos = path_map.find(path);
		}

		if (path != "" && pos != path_map.end())
			prefix_paths.insert(path);
	}

	// Copy the prefixes to new_paths
	new_paths = prefix_paths;

	// Eliminate paths that have a prefix already stored
	for (auto path : paths) {
		bool store = true;

		for (auto prefix : prefix_paths) {
			if (is_prefix(prefix, path)) {
				store = false;
				break;
			}
		}

		if (!store)
			continue;

		auto pos = path_map.find(path);
		if (pos != path_map.end())
			new_paths.insert(path);
	}

	// Do the matching
	for (auto path : new_paths) {
		auto pos = path_map.find(path);

		if (pos == path_map.end()) {
			std::cerr << "BUG: Unknown path in new_path set" << std::endl;
			continue;
		}

		results = results + pos->second;
	}

	// Sort the results
	std::sort(results.persons.rbegin(), results.persons.rend());
}
