#ifndef __WHO_H
#define __WHO_H

#include <vector>
#include <map>
#include <set>
#include <string>

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
extern std::map<std::string, bool> ignore;
extern std::string path_map_file;

extern int load_path_map(void);
extern void match_paths(void);
extern bool get_paths_from_revision(git_repository*, std::string);
bool ignore_from_file(std::string);

#endif /* __WHO_H */
