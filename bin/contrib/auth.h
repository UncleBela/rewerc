#ifndef WERC_AUTH_H
#define WERC_AUTH_H
#include <filesystem>
#include <string>
namespace auth {
// true: response completed; false: authenticated request may enter rc.
bool request(const std::filesystem::path &root,const std::string &site,const std::string &path);
int provision(const std::filesystem::path &root,int argc,char **argv);
}
#endif
