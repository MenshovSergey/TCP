#ifndef OS_EXTRA_PARSERS_H
#define OS_EXTRA_PARSERS_H

#include <string.h>

#include <vector>

#include "helper.h"

using namespace std;

bool isAlpha(char c) {
    return c != ' ' && c != '\'' && c != '\"' && c != '|';
}

int parse_command(string _line, vector<execargs_t> &progs) {
    if (_line.empty())
        return -1;
    string line = _line ;//+ "|";
    line[line.size()-1] = '|'; //change '\n' to '|'
    vector<string> prog;
    string temp;
    int i = 0;
    while (i < line.size()) {
        while (line[i] == ' ')
            i ++;
        while (i < line.size() && isAlpha(line[i])) {
            temp = temp + line[i];
            i ++;
        }
        if (!temp.empty()) {
            prog.push_back(temp);
            temp.clear();
            continue ;
        }

        if (line[i] == '\'') {
            i++;
            while (!(line[i] == '\'' && line[i-1] != '\\')) {
                temp = temp + line[i];
                i ++;
                if (i == line.size()-1)
                    return -1;
            }
            prog.push_back(temp);
            temp.clear();
            i ++ ;
            continue;
        }
        if (line[i] == '\"') {
            i++;
            while (!(line[i] == '\"' && line[i-1] != '\\')) {
                temp = temp + line[i];
                i ++;
                if (i == line.size()-1)
                    return -1;
            }
            prog.push_back(temp);
            temp.clear();
            i ++ ;
            continue;
        }
        if (line[i] == '|') {
            if (prog.empty())
                return -1;
            progs.push_back(execargs_from_vector(prog));
            prog.clear();
            i++;
        }
    }
    return 0;
}

#endif //OS_EXTRA_PARSERS_H
