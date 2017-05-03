/*
 * Copyright (c) 2016 SUSE Linux GmbH
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
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>

#include <getopt.h>
#include <git2.h>

using namespace std;

/* Options */
string path_blacklist_file;
string revision = "HEAD";
string repo_path = ".";
bool diff_mode = false;
string blacklist_file;
string path_map_file;
bool std_out = false;
bool append = false;
string file_name;
string base_rev;
string base_file;

map<string, bool> blob_id_cache;
map<string, map<string, int> > path_map;

struct patch_info {
	string context;
	string path;
};

using results_type = map<string, patch_info>;

static bool is_hex(const string &s)
{
	for (string::const_iterator c = s.begin(); c != s.end(); ++c) {
		if (!isxdigit(*c))
			return false;
	}

	return true;
}

static string to_lower(string s)
{
	transform(s.begin(), s.end(), s.begin(), ::tolower);

	return s;
}

string trim(const string &line)
{
	static const char *spaces = " \n\t\r";
	size_t pos1, pos2;

	pos1 = line.find_first_not_of(spaces);
	pos2 = line.find_last_not_of(spaces);

	if (pos1 == string::npos)
		return string("");

	return line.substr(pos1, pos2-pos1+1);
}

static int split_trim(vector<string> &items, const char *delim,
		      string buffer, unsigned splits)
{
	unsigned num;
	string item;
	size_t pos;

	buffer = trim(buffer);

	num = 0;
	pos = 0;

	while (pos != std::string::npos) {
		pos = buffer.find_first_of(delim, 0);
		if (pos == string::npos) {
			item = buffer;
		} else {
			item   = buffer.substr(0, pos);
			buffer = buffer.substr(pos + 1);
		}

		num += 1;
		items.push_back(trim(item));

		if (splits && num == splits)
			break;
	}

	return num;
}

static const char *domains[] = {
	"suse.de",
	"suse.cz",
	"suse.com",
	"novell.com",
	0,
};

static bool is_suse_email(const string &email)
{
	size_t pos = email.find_first_of("@");

	if (pos == string::npos)
		return false;

	string domain = email.substr(pos + 1);

	for (const char **c = domains; *c; ++c) {
		if (to_lower(domain) == *c)
			return true;
	}

	return false;
}

static int blob_content(string& content, const char *path,
		 git_repository *repo, git_tree *tree)
{
	git_tree_entry *entry;
	git_blob *blob;
	string oid;
	int error;

	content.clear();

	error = git_tree_entry_bypath(&entry, tree, path);
	if (error)
		goto out;

	if (git_tree_entry_type(entry) != GIT_OBJ_BLOB)
		goto out_free_entry;

	oid = git_oid_tostr_s(git_tree_entry_id(entry));
	if (blob_id_cache.find(oid) != blob_id_cache.end())
		goto out_free_entry;

	blob_id_cache[oid] = true;

	error = git_blob_lookup(&blob, repo, git_tree_entry_id(entry));
	if (error)
		goto out_free_entry;

	content = (const char *)git_blob_rawcontent(blob);

	git_blob_free(blob);

out_free_entry:
	git_tree_entry_free(entry);

out:
	return error;
}

static void parse_patch(const string &path,
			git_repository *repo, git_tree *tree,
			results_type &results,
			set<string> &blacklist)
{
	string committer = "Unknown";
	vector<string> commit_ids;
	bool in_patch = false;
	string content;
	int error;

	error = blob_content(content, path.c_str(), repo, tree);
	if (error)
		return;

	istringstream is(content);
	string line;

	while (getline(is, line)) {
		size_t len, pos;
		string token;

		if (line == "---") {
			in_patch = true;
			if (path_map_file == "")
				break;
		}

		if (in_patch) {
			if (line.length() < 5)
				continue;

			string prefix = line.substr(0, 4);

			if (prefix != "+++ ")
				continue;

			string path = line.substr(4);
			auto pos = path.find_first_of("/");

			if (pos != string::npos)
				path = path.substr(pos + 1);

			pos = path.find_first_of(" ");
			if (pos != string::npos)
				path = path.substr(0, pos);

			while (true) {
				path_map[path][committer] += 1;

				pos = path.find_last_of("/");
				if (pos == string::npos)
					break;

				path = path.substr(0, pos);
			}

			continue;
		}

		len = line.length();
		pos = line.find_first_of(":");
		if (pos == string::npos || pos + 1 >= len)
			continue;

		token = to_lower(line.substr(0, pos));

		if (token == "git-commit" || token == "no-fix") {
			string id;

			pos = line.find_first_not_of(" ", pos + 1);
			if (pos == string::npos)
				continue;

			line = to_lower(line);
			auto pos2 = line.find_first_not_of("0123456789abcdef",
							   pos);
			if (pos2 == string::npos)
				pos2 = line.length();

			if (pos2 - pos != 40)
				continue;

			id = line.substr(pos, pos2 - pos);
			if (token == "git-commit")
				commit_ids.emplace_back(id);
			else
				blacklist.emplace(id);

		} else if (token == "signed-off-by" || token == "acked-by" ||
			   token == "reviewed-by") {
			vector<string> items;

			split_trim(items, " \t", line, 0);

			for (vector<string>::iterator it = items.begin();
			     it != items.end();
			     ++it) {
				string email;

				pos = it->find_first_of("@", 0);
				len = it->length();
				if (!len || pos == string::npos)
					continue;

				email = *it;

				if (email[0] == '<') {
					email = email.substr(1);
					len -= 1;
				}

				if (!len)
					continue;

				if (email[len - 1] == '>')
					email = email.substr(0, len - 1);

				if (is_suse_email(email))
					committer = email;
			}
		}
	}

	for (auto &it : commit_ids) {
		results[it].context = committer;
		results[it].path = path;
	}
}

static void parse_blacklist(string &content,
			    set<string> &blacklist,
			    vector<string> &path_blacklist)
{
	istringstream is(content);
	string line;

	while (getline(is, line)) {

		line = trim(line);

		auto pos = line.find_first_of("# \n\t\r");

		if (pos != string::npos)
			line = line.substr(0, pos);

		if (line.length() == 40 && is_hex(line))
			blacklist.emplace(to_lower(line));
		else if (line.length() > 0)
			path_blacklist.emplace_back(line);
	}
}

static void parse_series(const string& series,
			 git_repository *repo, git_tree *tree,
			 results_type &results,
			 set<string> &blacklist)
{
	istringstream is(series);
	string line;

	while (getline(is, line)) {
		vector<string> items;
		string path;
		size_t pos;

		pos = line.find_first_of("#", 0);
		if (pos != string::npos)
			line = line.substr(0, pos);

		split_trim(items, " \t", line, 0);

		for (vector<string>::iterator it = items.begin();
		     it != items.end();
		     ++it) {
			if (it->find_first_of("/", 0) != string::npos) {
				path = *it;
				break;
			}
		}

		if (!path.length())
			continue;

		parse_patch(path, repo, tree, results, blacklist);
	}
}

static int handle_revision(git_repository *repo, const char *revision,
			   const string& outfile,
			   results_type &results,
			   set<string> &blacklist,
			   vector<string> &path_blacklist)
{
	git_commit *commit;
	git_object *obj;
	git_tree *tree;
	string series;
	string blist;
	int error;

	blob_id_cache.clear();

	error = git_revparse_single(&obj, repo, revision);
	if (error < 0)
		goto out;

	error = git_commit_lookup(&commit, repo, git_object_id(obj));
	if (error)
		goto out_obj_free;

	error = git_commit_tree(&tree, commit);
	if (error)
		goto out_commit_free;

	error = blob_content(blist, "blacklist.conf", repo, tree);
	if (!error)
		parse_blacklist(blist, blacklist, path_blacklist);

	error = blob_content(series, "series.conf", repo, tree);
	if (error)
		goto out_free_tree;

	parse_series(series, repo, tree, results, blacklist);

out_free_tree:
	git_tree_free(tree);

out_commit_free:
	git_commit_free(commit);

out_obj_free:
	git_object_free(obj);

out:
	return error;
}

static string base_name(const string &s)
{
	size_t pos = s.find_last_of("/");

	if (pos == string::npos)
		return s;

	return s.substr(pos + 1);
}

enum {
	OPTION_HELP,
	OPTION_REPO,
	OPTION_FILE,
	OPTION_BASE,
	OPTION_APPEND,
	OPTION_STDOUT,
	OPTION_BLACKLIST,
	OPTION_PATH_BLACKLIST,
	OPTION_PATH_MAP,
	OPTION_BASE_FILE,
};

static struct option options[] = {
	{ "help",		no_argument,		0, OPTION_HELP           },
	{ "repo",		required_argument,	0, OPTION_REPO           },
	{ "file",		required_argument,	0, OPTION_FILE           },
	{ "base",		required_argument,	0, OPTION_BASE           },
	{ "append",		no_argument,		0, OPTION_APPEND         },
	{ "stdout",		no_argument,		0, OPTION_STDOUT         },
	{ "blacklist",		required_argument,	0, OPTION_BLACKLIST      },
	{ "path-blacklist",	required_argument,	0, OPTION_PATH_BLACKLIST },
	{ "path-map",		required_argument,	0, OPTION_PATH_MAP       },
	{ "base-file",		required_argument,	0, OPTION_BASE_FILE      },
	{ 0,			0,			0, 0                     }
};

static void usage(const char *prg)
{
	printf("Usage: %s [Options] Revision\n", base_name(prg).c_str());
	printf("Options:\n");
	printf("  --help, -h       Print this message end exit\n");
	printf("  --repo, -r       Path to git repository\n");
	printf("  --file, -f       Write output to specified file\n");
	printf("  --blacklist      Write blacklist to specified file\n");
	printf("  --path-blacklist Write path-blacklist to specified file\n");
	printf("  --path-map       Write path-map to specified file\n");
	printf("  --base, -b       Show only commits not in given base version\n");
	printf("  --base-file      File to store commit-list from the base-kernel\n");
	printf("                   (Only used when --base is specified)\n");
	printf("  --append         Open output file in append mode\n");
	printf("  --stdout, -c     Write output to stdout\n");
}

static void parse_options(int argc, char **argv)
{
	int c;

	if (argc < 2) {
		usage(argv[0]);
		exit(1);
	}

	while (true) {
		int opt_idx;

		c = getopt_long(argc, argv, "hr:f:b:c", options, &opt_idx);
		if (c == -1)
			break;

		switch (c) {
		case OPTION_HELP:
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		case OPTION_REPO:
		case 'r':
			repo_path = optarg;
			break;
		case OPTION_FILE:
		case 'f':
			file_name = optarg;
			break;
		case OPTION_BASE:
		case 'b':
			base_rev = optarg;
			diff_mode = true;
			break;
		case OPTION_BASE_FILE:
			base_file = optarg;
			break;
		case OPTION_APPEND:
			append = true;
			break;
		case OPTION_STDOUT:
		case 'c':
			std_out = true;
			break;
		case OPTION_BLACKLIST:
			blacklist_file = optarg;
			break;
		case OPTION_PATH_BLACKLIST:
			path_blacklist_file = optarg;
			break;
		case OPTION_PATH_MAP:
			path_map_file = optarg;
			break;
		default:
			usage(argv[0]);
			exit(1);
		}
	}

	if (optind < argc)
		revision = argv[optind++];
}

static void write_blacklist(set<string> &blacklist)
{
	ofstream file;

	if (blacklist_file == "")
		return;

	file.open(blacklist_file.c_str());

	if (!file.is_open()) {
		fprintf(stderr, "Can't open blacklist file for writing\n");
		return;
	}

	for (auto &c : blacklist)
		file << c << std::endl;

	cout << "Wrote " << blacklist.size() << " blacklisted commits to " << blacklist_file << endl;

	file.close();
}

static void write_path_blacklist(vector<string> &path_blacklist)
{
	ofstream file;

	if (path_blacklist_file == "")
		return;

	file.open(path_blacklist_file.c_str());

	if (!file.is_open()) {
		fprintf(stderr, "Can't open path-blacklist file for writing\n");
		return;
	}

	for (auto &path : path_blacklist)
		file << path << std::endl;

	cout << "Wrote " << path_blacklist.size() << " blacklisted paths to "
	     << path_blacklist_file << endl;

	file.close();
}

static void write_results(ostream &os, results_type &results)
{
	for (auto &it : results) {
		os << it.first << ',' << it.second.context;
		if (it.second.path.length() > 0)
			os << ',' << it.second.path;
		os << endl;
	}
}

static void write_path_map(string filename)
{
	ofstream file;

	if (filename == "")
		return;

	file.open(filename.c_str());

	if (!file.is_open()) {
		fprintf(stderr, "Can't open path-map file for writing\n");
		return;
	}

	for (auto &path : path_map) {
		file << path.first;
		for (auto &m : path.second)
			file << ';' << m.first << ":" << m.second;
		file << endl;
	}

	file.close();
}

static void do_diff(results_type &result,
		    const results_type &base,
		    const results_type &branch)
{
	for (const auto &it : branch) {
		if (base.find(it.first) == base.end())
			result[it.first] = it.second;
	}
}

int main(int argc, char **argv)
{
	ios_base::openmode file_mode = ios_base::out;
	results_type results, base;
	vector<string> path_blacklist;
	git_repository *repo = NULL;
	set<string> blacklist;
	const git_error *e;
	ofstream of;
	ostream *os;
	int error;

	parse_options(argc, argv);

	if (file_name == "")
		file_name = base_name(revision) + ".list";

	if (append)
		file_mode |= ofstream::app;

	if (!std_out) {
		of.open(file_name.c_str(), file_mode);
		if (!of.is_open()) {
			cerr << "Can't open output file " << file_name << endl;
			error = 1;
			goto out;
		}

		os = &of;
	} else {
		os = &cout;
	}

	git_libgit2_init();

	error = git_repository_open(&repo, repo_path.c_str());
	if (error < 0)
		goto error;

	if (diff_mode) {
		vector<string> ignored2;
		set<string> ignored;

		error = handle_revision(repo, base_rev.c_str(), file_name, base,
					ignored, ignored2);
		if (error)
			goto error;
	}

	error = handle_revision(repo, revision.c_str(), file_name,
				results, blacklist, path_blacklist);
	if (error)
		goto error;

	if (diff_mode) {
		results_type r;

		do_diff(r, base, results);

		results = r;

		if (base_file != "") {
			ofstream bof(base_file);

			if (bof.is_open()) {
				write_results(bof, base);
				cout << "Wrote " << base.size() << " commits to " << base_file << endl;
			} else {
				cerr << "Can't open " << base_file << " for writing" << endl;
			}
		}
	}

	write_results(*os, results);

	if (!std_out)
		cout << "Wrote " << results.size() << " commits to " << file_name << endl;

	write_blacklist(blacklist);
	write_path_blacklist(path_blacklist);
	write_path_map(path_map_file);

out:
	if (of.is_open())
		of.close();

	git_repository_free(repo);

	git_libgit2_shutdown();

	return error;

error:
	e = giterr_last();
	cout << "Error: " << e->message << endl;

	goto out;
}
