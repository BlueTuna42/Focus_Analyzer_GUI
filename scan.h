#ifndef SCAN_H
#define SCAN_H

#include <vector>
#include <string>

class Scanner {
public:
    static std::vector<std::string> scanFiles(const std::string& path);
};

#endif