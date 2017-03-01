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

#ifndef __WHO_H
#define __WHO_H

#include <vector>
#include <string>
#include <map>
#include <set>

#include <git2.h>

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

	inline void add_one(const struct person &p)
	{
		for (auto &i : persons) {
			if (p.name == i.name) {
				i.count += p.count;
				return;
			}
		}

		persons.push_back(p);
	}

	inline struct people &operator+(const struct people &p)
	{
		for (auto &i : p.persons)
			add_one(i);

		return *this;
	}
};

class git_who {
private:
	std::map<std::string, struct people> path_map;
	std::set<std::string> paths;

	bool get_paths_from_commit(git_commit*, size_t);

public:
	void add_path(std::string);
	int  load_path_map(std::string);
	void match_paths(struct people &results);
	bool get_paths_from_revision(git_repository*, std::string);
};

#endif /* __WHO_H */
