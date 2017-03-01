#ifndef __WHO_H
#define __WHO_H

#include <vector>
#include <string>
#include <map>
#include <set>

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

extern std::set<std::string> paths;

extern int load_path_map(std::string);
extern void match_paths(struct people &results);
extern bool get_paths_from_revision(git_repository*, std::string);

#endif /* __WHO_H */
