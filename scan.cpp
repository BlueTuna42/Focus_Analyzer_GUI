#include "scan.h"
#include <glob.h>

std::vector<std::string> Scanner::scanFiles(const std::string& path) {
    glob_t g;
    // List of all supported image extensions
    std::vector<std::string> exts = {
        "*.bmp", "*.jpg", "*.png", "*.jpeg", 
        "*.ARW", "*.DNG", "*.CR2", "*.NEF", "*.RW2", "*.RAF"
    };
    std::vector<std::string> res;
    
    // Find files matching the extensions
    for (size_t i = 0; i < exts.size(); ++i) {
        std::string pattern = path + "/" + exts[i];
        glob(pattern.c_str(), (i == 0 ? 0 : GLOB_APPEND), nullptr, &g);
    }
    
    for (size_t i = 0; i < g.gl_pathc; ++i) {
        res.push_back(g.gl_pathv[i]);
    }
    
    globfree(&g);
    return res;
}