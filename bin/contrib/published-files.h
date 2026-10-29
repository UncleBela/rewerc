#ifndef WERC_PUBLISHED_FILES_H
#define WERC_PUBLISHED_FILES_H
#include <istream>
#include <streambuf>
#include <string>
#include <vector>
namespace published {
enum class Access { Content, Code, Configuration };
// Returned descriptors own a pinned regular inode; callers must close them.
int openFile(const std::string &path, Access access = Access::Content);
int openDirectory(const std::string &path);
std::vector<std::string> listDirectory(const std::string &path);
void createBudget();
std::string descriptorPath(int fd);
bool requestContext();
class InputBuffer : public std::streambuf {
    int fd;
    char buffer[8192];
    int_type underflow() override;
public:
    explicit InputBuffer(int descriptor) : fd(descriptor) {}
    ~InputBuffer() override;
};
class Input : public std::istream {
    InputBuffer buffer;
public:
    explicit Input(const std::string &path) : std::istream(nullptr), buffer(openFile(path)) { rdbuf(&buffer); }
};
}
#endif
