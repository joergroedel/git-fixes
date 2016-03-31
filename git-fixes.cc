#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <git2.h>

using namespace std;

struct options {
	string revision;
	string committer;
	string fixes_file;
	string db;
	bool all;
	bool match_all;
	bool no_group;
	bool stats;
	bool reverse;
	vector<string> path;
};

struct commit {
	string subject;
	string id;
	vector<struct reference> refs;
};

struct reference {
	string id;
	bool fixes;

	reference()
		: id(), fixes(false)
	{ }
};

struct match_info {
	string commit_id;
	string committer;

	bool operator<(const struct match_info &i) const
	{
		return commit_id < i.commit_id;
	}
};

vector<struct match_info> match_list;
map<string, vector<commit> > results;

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
	string item, delimiters;
	unsigned num;

	buffer = trim(buffer);

	delimiters  = delim;
	num         = 0;

	for (size_t pos = 0; pos != std::string::npos;) {
		pos = buffer.find_first_of(delimiters.c_str(), pos);
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

string fix_revision(string rev)
{
	if (rev.length() < 2)
		return rev;

	if (rev.substr(0,2) == "..")
		rev = "HEAD" + rev;

	if (rev.substr(rev.length() - 2, 2) == "..")
		rev = rev + "HEAD";

	return rev;
}

static int match_parent_tree(git_commit *commit, size_t p,
			     git_diff_options *diffopts)
{
	git_commit *parent;
	git_tree *a, *b;
	git_diff *diff;
	int err;

	err = git_commit_parent(&parent, commit, p);
	if (err)
		return err;

	err = git_commit_tree(&a, parent);
	if (err)
		goto out_free_parent;

	err = git_commit_tree(&b, commit);
	if (err)
		goto out_free_a;

	err = git_diff_tree_to_tree(&diff, git_commit_owner(commit), a, b, diffopts);
	if (err < 0)
		goto out_free_b;

	err = git_diff_num_deltas(diff) > 0 ? 1 : 0;

	git_diff_free(diff);
out_free_b:
	git_tree_free(b);
out_free_a:
	git_tree_free(a);
out_free_parent:
	git_commit_free(parent);

	return err;
}

static bool match_tree(git_commit *commit, git_diff_options *diffopts)
{
	git_pathspec *ps = NULL;
	unsigned int parents;
	bool ret = false;
	int err;

	if (diffopts->pathspec.count <= 0)
		return true;

	parents = git_commit_parentcount(commit);

	if (parents == 0) {
		git_tree *tree;

		err = git_pathspec_new(&ps, &diffopts->pathspec);
		if (err < 0)
			return false;
		err = git_commit_tree(&tree, commit);
		if (err < 0)
			return false;

		err = git_pathspec_match_tree(NULL, tree, GIT_PATHSPEC_NO_MATCH_ERROR, ps);
		if (!err)
			ret = true;

		git_tree_free(tree);
		git_pathspec_free(ps);
	} else if (parents == 1) {
		ret = match_parent_tree(commit, 0, diffopts) > 0;
	} else {
		for (unsigned i = 0; i < parents; ++i) {
			if (match_parent_tree(commit, i, diffopts) > 0) {
				ret = true;
				break;
			}
		}
	}

	return ret;
}

static bool match_commit(const struct commit &c, const string &id,
			 git_commit *commit, git_diff_options *diffopts,
			 struct options *opts)
{
	vector<struct match_info>::const_iterator it;
	struct match_info info;
	bool ret;

	/* First check if the commit is already in the tree */
	info.commit_id = to_lower(c.id);
	it = lower_bound(match_list.begin(), match_list.end(), info);
	if (it != match_list.end() && it->commit_id == info.commit_id)
		return false;

	info.commit_id = to_lower(id);

	it = lower_bound(match_list.begin(), match_list.end(), info);

	if (it == match_list.end())
		return false;

	if (!opts->all && opts->committer.length() > 0) {
	    if (it->committer.find(opts->committer) == string::npos)
		return false;
	}

	ret = (it->commit_id == info.commit_id) &&
	       match_tree(commit, diffopts);

	if (ret) {
		string key = opts->no_group ? "default" : it->committer;

		results[key].push_back(c);
	}

	return ret;
}

static void parse_line(const string &line, vector<struct reference> &commits)
{
	bool found_commit = false;
	string::const_iterator c;
	struct reference commit;
	int last_c = -1;

	if (line.length() >= 6 && to_lower(line.substr(0,6)) == "fixes:")
		commit.fixes = true;

	for (c = line.begin(); c != line.end(); last_c = *c, ++c) {
		bool hex = isxdigit(*c);

		if (isblank(last_c) && hex) {
			found_commit = true;
		}

		if (found_commit && hex)
			commit.id += *c;

		if (found_commit && (isblank(*c) || (c + 1) == line.end())) {
			int len = commit.id.length();

			if (len >= 8 && len <= 40)
				commits.push_back(commit);
		}

		if (found_commit && !hex) {
			found_commit = false;
			commit.id.clear();
		}
	}
}

static void parse_commit_msg(struct commit &commit, const char *msg)
{
	vector<string>::iterator it;
	vector<string> lines;
	string buffer(msg);

	split_trim(lines, "\n", buffer, 0);

	if (!lines.size())
		return;

	commit.subject = lines[0];

	for (it = lines.begin(); it != lines.end(); ++it)
		parse_line(*it, commit.refs);
}

static int handle_commit(git_commit *commit, git_repository *repo,
			 git_diff_options *diffopts, struct options *opts)
{
	const git_oid *oid;
	char commit_id[41];
	struct commit c;
	const char *msg;
	int error;

	oid = git_commit_id(commit);

	git_oid_fmt(commit_id, oid);
	commit_id[40] = 0;
	msg = git_commit_message(commit);

	c.id = commit_id;
	parse_commit_msg(c, msg);

	error = 0;
	if (c.refs.size() > 0) {
		vector<struct reference>::iterator it;

		for (it = c.refs.begin(); it != c.refs.end(); ++it) {
			git_object *obj;

			if (git_revparse_single(&obj, repo, it->id.c_str()) < 0)
				continue;

			string id(git_oid_tostr_s(git_object_id(obj)));

			git_object_free(obj);

			if (!opts->match_all && !it->fixes)
				continue;

			if (match_commit(c, id, commit, diffopts, opts)) {
				error = 1;
				break;
			}
		}
	}

	return error;
}

static void print_results(struct options *opts)
{
	map<string, vector<commit> >::iterator r;
	vector<commit>::iterator i;
	const char *prefix;
	bool found = false;

	prefix = opts->no_group ? "" : "\t";

	for (r = results.begin(); r != results.end(); ++r) {
		if (!r->second.size())
			continue;

		found = true;

		if (!opts->no_group)
			printf("%s (%lu):\n", r->first.c_str(), r->second.size());

		for (i = r->second.begin(); i != r->second.end(); ++i) {
			printf("%s%s %s\n", prefix,
			       i->id.substr(0,12).c_str(),
			       i->subject.c_str());
		}
		printf("\n");
	}

	if (!found)
		printf("Nothing found\n");
}

static bool load_commit_file(const char *filename, vector<struct match_info> &commits)
{
	ifstream file;
	istream *in;
	string line;

	if (strcmp(filename, "-") == 0) {
		in = &cin;
	} else {
		file.open(filename);
		if (!file.is_open()) {
			printf("Can't open %s\n", filename);
			return false;
		}

		in = &file;
	}

	while (getline(*in, line)) {
		struct match_info info;
		vector<string> tokens;
		int num;

		num = split_trim(tokens, ",", line, 2);

		if (!num)
			continue;

		info.commit_id = to_lower(tokens[0]);
		if (num > 1)
			info.committer = tokens[1];

		commits.push_back(info);
	}

	sort(commits.begin(), commits.end());

	if (file.is_open())
		file.close();

	return true;
}

static int revwalk_init(git_revwalk **walker, git_repository *repo,
		 const char *revision)
{
	git_revspec spec;
	int err;

	err = git_revwalk_new(walker, repo);
	if (err)
		return err;

	err = git_revparse(&spec, repo, revision);
	if (err)
		goto out_free;

	if (spec.flags & GIT_REVPARSE_SINGLE) {
		git_revwalk_push(*walker, git_object_id(spec.from));
		git_object_free(spec.from);
	} else if (spec.flags & GIT_REVPARSE_RANGE) {
		git_revwalk_push(*walker, git_object_id(spec.to));

		if (spec.flags & GIT_REVPARSE_MERGE_BASE) {
			git_oid base;
			err = git_merge_base(&base, repo,
					     git_object_id(spec.from),
					     git_object_id(spec.to));
			if (err) {
				git_object_free(spec.to);
				git_object_free(spec.from);
				goto out_free;
			}

			git_revwalk_push(*walker, &base);
		}

		git_revwalk_hide(*walker, git_object_id(spec.from));
		git_object_free(spec.to);
		git_object_free(spec.from);
	}

	return 0;

out_free:
	git_revwalk_free(*walker);

	return err;
}

static void destroy_diffopts(git_diff_options *diffopts)
{
	if (!diffopts->pathspec.strings)
		return;

	for (unsigned i = 0; i < diffopts->pathspec.count; ++i)
		free(diffopts->pathspec.strings[i]);

	free(diffopts->pathspec.strings);

	diffopts->pathspec.count = 0;
}

static bool init_diffopts(git_diff_options *diffopts, struct options *opts)
{
	unsigned count = opts->path.size();

	if (count) {
		diffopts->pathspec.strings = (char **)malloc(count * sizeof(char*));
		if (!diffopts->pathspec.strings)
			return false;

		for (unsigned i = 0; i < count; ++i) {
			diffopts->pathspec.strings[i] = strdup(opts->path[i].c_str());
			if (!diffopts->pathspec.strings[i]) {
				destroy_diffopts(diffopts);
				return false;
			}
		}
	}

	diffopts->pathspec.count = count;

	return true;
}

static int fixes(git_repository *repo, struct options *opts)
{
	git_diff_options diffopts = GIT_DIFF_OPTIONS_INIT;
	int sorting = GIT_SORT_TIME;
	int match = 0, count = 0;
	git_revwalk *walker;
	git_commit *commit;
	string revision;
	git_oid oid;
	int err;

	revision = fix_revision(opts->revision);

	err = revwalk_init(&walker, repo, revision.c_str());
	if (err < 0)
		return err;

	if (opts->reverse)
		sorting |= GIT_SORT_REVERSE;

	git_revwalk_sorting(walker, sorting);

	err = -1;
	if (!init_diffopts(&diffopts, opts))
		goto error;

	while (!git_revwalk_next(&oid, walker)) {
		count += 1;

		err = git_commit_lookup(&commit, repo, &oid);
		if (err < 0)
			goto error;

		err = handle_commit(commit, repo, &diffopts, opts);
		if (err < 0) {
			git_commit_free(commit);
			goto error;
		}

		git_commit_free(commit);
		match += err;
	}

	destroy_diffopts(&diffopts);
	git_revwalk_free(walker);

	print_results(opts);

	if (opts->stats)
		printf("Found %d objects (%d matches)\n", count, match);

	return 0;

error:
	destroy_diffopts(&diffopts);
	git_revwalk_free(walker);

	return err;
}

static int load_defaults(git_repository *repo, struct options *opts)
{
	git_buf buffer = GIT_BUF_INIT_CONST(NULL, 0);
	git_config *repo_cfg = NULL;
	int val, error;

	opts->revision  = "HEAD";
	opts->reverse   = true;
	opts->match_all = false;
	opts->all       = true;
	opts->no_group  = false;
	opts->stats	= false;

	error = git_repository_config(&repo_cfg, repo);
	if (error < 0)
		goto out;

	error = git_config_get_string_buf(&buffer, repo_cfg, "user.email");

	if (!error)
		opts->committer = buffer.ptr;

	error = git_config_get_path(&buffer, repo_cfg, "fixes.file");
	if (!error)
		opts->fixes_file = buffer.ptr;

	error = git_config_get_bool(&val, repo_cfg, "fixes.all");
	if (!error)
		opts->all = val ? true : false;

	git_buf_free(&buffer);

	error = 0;
out:
	git_config_free(repo_cfg);

	return error;
}

enum {
	OPTION_HELP,
	OPTION_ALL,
	OPTION_ME,
	OPTION_REVERSE,
	OPTION_COMMITTER,
	OPTION_GROUPING,
	OPTION_NO_GROUPING,
	OPTION_MATCH_ALL,
	OPTION_FILE,
	OPTION_DATA_BASE,
	OPTION_STATS,
};

static struct option options[] = {
	{ "help",		no_argument,		0, OPTION_HELP        },
	{ "all",		no_argument,		0, OPTION_ALL         },
	{ "me",			no_argument,		0, OPTION_ME          },
	{ "reverse",		no_argument,		0, OPTION_REVERSE     },
	{ "committer",		required_argument,	0, OPTION_COMMITTER   },
	{ "grouping",		no_argument,		0, OPTION_GROUPING    },
	{ "no-grouping",	no_argument,		0, OPTION_NO_GROUPING },
	{ "match-all",		no_argument,		0, OPTION_MATCH_ALL   },
	{ "data-base",		required_argument,	0, OPTION_DATA_BASE   },
	{ "file",		required_argument,	0, OPTION_FILE        },
	{ "stats",		no_argument,		0, OPTION_STATS       },
	{ 0,			0,			0, 0                  }
};

static void usage(const char *prg)
{
	printf("Usage: %s [Options] [Revspec [Path...]]\n", prg);
	printf("Options:\n");
	printf("  --help, -h       Print this message end exit\n");
	printf("  --all, -a        Show all potential fixes\n");
	printf("  --me             Show only fixes for patches I committed\n");
	printf("  --reverse        Sort fixes in reverse order\n");
	printf("  --committer, -c  Show only fixes for a given committer\n");
	printf("  --grouping       Group fixes by committer (default)\n");
	printf("  --no-grouping    Don't group fixes by committer\n");
	printf("  --match-all, -m  Match against everything that looks like git commit-id\n");
	printf("  --data-base, -d  Select specific data-base (set file with fixes.<db>.file)\n");
	printf("  --file, -f       Read commit-list from file\n");
	printf("  --stats, -s      Print some statistics at the end\n");
}

static bool parse_options(struct options *opts, int argc, char **argv)
{
	int c;

	while (true) {
		int opt_idx;

		c = getopt_long(argc, argv, "ac:f:d:ms", options, &opt_idx);
		if (c == -1)
			break;

		switch (c) {
		case OPTION_HELP:
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		case OPTION_ALL:
		case 'a':
			opts->all = true;
			break;
		case OPTION_ME:
			opts->all = false;
			break;
		case OPTION_REVERSE:
			opts->reverse = true;
			break;
		case OPTION_COMMITTER:
		case 'c':
			opts->committer = optarg;
			opts->all = false;
			break;
		case OPTION_GROUPING:
			opts->no_group = false;
			break;
		case OPTION_NO_GROUPING:
			opts->no_group = true;
			break;
		case OPTION_MATCH_ALL:
		case 'm':
			opts->match_all = true;
			break;
		case OPTION_FILE:
		case 'f':
			opts->fixes_file = optarg;
			break;
		case OPTION_DATA_BASE:
		case 'd':
			opts->db = optarg;
			break;
		case OPTION_STATS:
		case 's':
			opts->stats = true;
			break;
		default:
			usage(argv[0]);
			return false;
		}
	}

	if (optind < argc)
		opts->revision = argv[optind++];

	for (;optind < argc; optind++)
		opts->path.push_back(argv[optind]);

	return true;
}

static int db_file(string &filename, git_repository *repo, struct options *opts)
{
	if (opts->db.length() > 0) {
		git_buf buffer = GIT_BUF_INIT_CONST(NULL, 0);
		git_config *repo_cfg = NULL;
		string key;
		int error;

		key = "fixes." + opts->db + ".file";

		error = git_repository_config(&repo_cfg, repo);
		if (error < 0)
			return error;

		error = git_config_get_path(&buffer, repo_cfg, key.c_str());

		if (!error)
			filename = buffer.ptr;

		git_buf_free(&buffer);
		git_config_free(repo_cfg);

		return error;
	} else {
		filename = opts->fixes_file;
	}

	return 0;
}

int main(int argc, char **argv)
{
	git_repository *repo = NULL;
	struct options opts;
	const git_error *e;
	string filename;
	int error;

	git_libgit2_init();

	error = git_repository_open(&repo, ".");
	if (error < 0)
		goto error;

	error = load_defaults(repo, &opts);
	if (error < 0)
		goto error;

	error = 1;
	if (!parse_options(&opts, argc, argv))
		goto out;

	error = db_file(filename, repo, &opts);
	if (error)
		goto error;

	if (!load_commit_file(filename.c_str(), match_list))
		goto out;

	error = fixes(repo, &opts);
	if (error < 0)
		goto error;

out:
	git_repository_free(repo);

	git_libgit2_shutdown();

	return error;

error:
	e = giterr_last();
	printf("Error: %s\n", e->message);

	goto out;
}
