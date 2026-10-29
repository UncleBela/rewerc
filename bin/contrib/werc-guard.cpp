// Read-only CGI boundary. Symlinks require explicit private publication roots.
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;
namespace fs = std::filesystem;
using std::string;
static const string approvalFile = "APPROVED_DIRECTORIES";

struct Rejected {
    int status;
    const char *reason;
};

class Descriptor {
    int fd;
public:
    explicit Descriptor(int value) : fd(value) {
        if (fd < 0) throw Rejected{403, "directory is missing, inaccessible or linked"};
    }
    ~Descriptor() { if (fd >= 0) close(fd); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    Descriptor(Descriptor &&other) noexcept : fd(other.fd) { other.fd = -1; }
    Descriptor &operator=(Descriptor &&other) noexcept {
        if (fd >= 0) close(fd);
        fd = other.fd;
        other.fd = -1;
        return *this;
    }
    int get() const { return fd; }
    int release() { int result = fd; fd = -1; return result; }
};

static string environment(const char *name) {
    const char *value = std::getenv(name);
    return value ? value : "";
}
static bool asciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool asciiDigit(char c) { return c >= '0' && c <= '9'; }
static void validateName(const string &name) {
    if (name.empty() || name.size() > 253 ||
        (!asciiAlpha(name[0]) && !asciiDigit(name[0])) || name.find("..") != string::npos)
        throw Rejected{400, "invalid hostname or site selector"};
    for (char c : name)
        if (!asciiAlpha(c) && !asciiDigit(c) && c != '_' && c != '-' && c != '.')
            throw Rejected{400, "invalid hostname or site selector"};
}
static void validatePort(const string &port) {
    if (port.empty() || port.size() > 5) throw Rejected{400, "invalid server port"};
    unsigned value = 0;
    for (char c : port) {
        if (!asciiDigit(c)) throw Rejected{400, "invalid server port"};
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > 65535) throw Rejected{400, "invalid server port"};
}
static int hexDigit(char c) {
    if (asciiDigit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static string requestPath(const string &uri) {
    if (uri.size() > 8192) throw Rejected{414, "request URI is too long"};
    string path;
    const string raw = uri.substr(0, uri.find('?'));
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '%') {
            if (i + 2 >= raw.size() || hexDigit(raw[i+1]) < 0 || hexDigit(raw[i+2]) < 0)
                throw Rejected{400, "invalid URL encoding"};
            c = static_cast<char>(hexDigit(raw[i+1]) * 16 + hexDigit(raw[i+2]));
            i += 2;
        }
        if (!asciiAlpha(c) && !asciiDigit(c) && string("_+-/.,:").find(c) == string::npos)
            throw Rejected{400, "invalid URL path"};
        if (c != '/' || path.empty() || path.back() != '/') path += c;
    }
    if (path.empty() || path[0] != '/' || path.find("..") != string::npos)
        throw Rejected{403, "path traversal is forbidden"};
    for (size_t i = 1; i < path.size(); ++i)
        if (path.compare(i, approvalFile.size(), approvalFile) == 0 &&
            path[i-1] == '/' && (i+approvalFile.size() == path.size() || path[i+approvalFile.size()] == '/'))
            throw Rejected{404, "publication policy is private"};
        else if (path[i-1] == '/' && (path[i] == '.' || path[i] == '_'))
            throw Rejected{404, "private paths and account endpoints are unavailable"};
    const size_t dot = path.find_last_of('.');
    if (dot != string::npos) {
        string extension = path.substr(dot);
        for (char &c : extension) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        for (const char *blocked : {".md", ".tpl", ".rc", ".cgi", ".conf", ".bak", ".swp",
                                    ".pl", ".awk", ".py", ".cpp", ".h", ".sqlite", ".sqlite3", ".db"})
            if (extension == blocked) throw Rejected{404, "source and state paths are unavailable"};
    }
    return path;
}
static Descriptor openDirectory(const fs::path &path) {
    Descriptor fd(open(path.is_absolute() ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    for (const auto &component : path.relative_path()) {
        const string part = component.string();
        if (part == ".") continue;
        if (part == "..") throw Rejected{403, "directory traversal is forbidden"};
        fd = Descriptor(openat(fd.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    }
    return fd;
}
// This list is plain data; physical paths never enter rc or HTML output.
static std::vector<fs::path> approvedDirectories(const fs::path &root) {
    Descriptor etc = openDirectory(root / "etc");
    const int raw = openat(etc.get(), approvalFile.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (raw < 0 && errno == ENOENT) return {};
    if (raw < 0) throw Rejected{403, "cannot open private APPROVED_DIRECTORIES"};
    Descriptor file(raw);
    struct stat info{};
    if (fstat(file.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 0077) || info.st_nlink != 1)
        throw Rejected{403, "APPROVED_DIRECTORIES must be a private owner-only regular file"};
    string data;
    char buffer[4096];
    ssize_t count;
    while ((count = read(file.get(), buffer, sizeof buffer)) > 0) {
        if (data.size() + static_cast<size_t>(count) > 65536)
            throw Rejected{403, "APPROVED_DIRECTORIES exceeds its size limit"};
        data.append(buffer, static_cast<size_t>(count));
    }
    if (count < 0) throw Rejected{403, "cannot read APPROVED_DIRECTORIES"};
    std::vector<fs::path> result;
    std::istringstream input(data);
    string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == string::npos || line[first] == '#') continue;
        line = line.substr(first, line.find_last_not_of(" \t") - first + 1);
        for (unsigned char c : line)
            if (c < 32 || c == 127) throw Rejected{403, "invalid approved directory entry"};
        fs::path entry(line);
        if (!entry.is_absolute() || entry == "/")
            throw Rejected{403, "approved directories must be absolute, specific directories"};
        // No variable expansion, relative traversal or linked root components.
        Descriptor checked = openDirectory(entry);
        std::error_code error;
        const fs::path canonical = fs::canonical(entry, error);
        if (error) throw Rejected{403, "approved directory is unavailable"};
        if (result.size() == 256) throw Rejected{403, "too many approved directories"};
        result.push_back(canonical);
    }
    return result;
}
static bool beneath(const fs::path &path, const fs::path &root) {
    auto part = path.begin();
    for (const auto &base : root) {
        if (part == path.end() || *part != base) return false;
        ++part;
    }
    return true;
}
static string lowerExtension(const fs::path &path) {
    string value = path.extension().string();
    for (char &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
struct Publication {
    fs::path project;
    std::vector<fs::path> approved;
    std::vector<std::pair<dev_t, ino_t>> ancestors;
    std::vector<string> linkedDirectories;

    void protectApplication(const fs::path &path) const {
        for (const auto &part : path)
            if (part == "_werc" || part == ".git")
                throw Rejected{403, "private configuration cannot be published through links"};
        if (path.filename() == approvalFile)
            throw Rejected{403, "publication policy cannot be linked into a site"};
        for (const char *privatePart : {"etc", "bin", "lib", "tpl", "apps", "var", ".git"})
            if (beneath(path, project / privatePart))
                throw Rejected{403, "application-private data cannot be published through links"};
    }
    fs::path linkedTarget(const fs::path &link) const {
        std::error_code error;
        fs::path target = fs::canonical(link, error);
        if (error) throw Rejected{403, "linked target is missing or cyclic"};
        bool allowed = false;
        for (const auto &directory : approved) {
            if (!beneath(target, directory)) continue;
            const auto relative = target.lexically_relative(directory);
            bool hidden = false;
            for (const auto &part : relative) {
                const string name = part.string();
                if (name != "." && !name.empty() && (name[0] == '.' || name[0] == '_')) hidden = true;
            }
            if (!hidden) { allowed = true; break; }
        }
        if (!allowed) throw Rejected{403, "symlink or special file target is not in an approved directory"};
        protectApplication(target);
        const string extension = lowerExtension(target);
        for (const char *code : {".tpl", ".rc", ".cgi", ".conf", ".py", ".pl", ".awk", ".cpp", ".h", ".db", ".sqlite", ".sqlite3"})
            if (extension == code) throw Rejected{403, "linked source or state files are not publishable notes"};
        return target;
    }
    void inspect(int fd, const fs::path &physical, const fs::path &relative,
                 bool external = false, bool configuration = false) {
        struct stat current{};
        if (fstat(fd, &current) != 0) throw Rejected{403, "cannot inspect selected site"};
        const auto identity = std::make_pair(current.st_dev, current.st_ino);
        for (const auto &ancestor : ancestors)
            if (ancestor == identity) throw Rejected{403, "linked directory cycle is forbidden"};
        ancestors.push_back(identity);
        Descriptor copy(dup(fd));
        DIR *raw = fdopendir(copy.get());
        if (!raw) throw Rejected{403, "cannot inspect selected site"};
        copy.release();
        struct CloseDirectory { void operator()(DIR *dir) const { closedir(dir); } };
        std::unique_ptr<DIR, CloseDirectory> directory(raw);
        while (true) {
            errno = 0;
            dirent *entry = readdir(directory.get());
            if (!entry) {
                if (errno) throw Rejected{403, "cannot inspect selected site"};
                break;
            }
            const string name = entry->d_name;
            if (name == "." || name == "..") continue;
            const auto item = physical / name;
            const auto visible = relative / name;
            const bool code = configuration || name == "_werc" || lowerExtension(item) == ".tpl";
            if (external) {
                protectApplication(item);
                if (name == "_werc" || lowerExtension(item) == ".tpl")
                    throw Rejected{403, "linked directories cannot supply werc configuration or templates"};
            }
            struct stat info{};
            if (fstatat(fd, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0)
                throw Rejected{403, "cannot inspect selected site"};
            if (S_ISLNK(info.st_mode)) {
                if (code) throw Rejected{403, "configuration and template symlinks are forbidden"};
                const auto target = linkedTarget(item);
                Descriptor parent = openDirectory(target.parent_path());
                Descriptor opened(openat(parent.get(), target.filename().c_str(),
                    O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
                if (fstat(opened.get(), &info) != 0) throw Rejected{403, "cannot inspect linked target"};
                if (S_ISDIR(info.st_mode)) {
                    inspect(opened.get(), target, visible, true, false);
                    const string url = "/" + visible.generic_string() + "/";
                    try { requestPath(url); linkedDirectories.push_back(url); }
                    catch (const Rejected &) { /* Hidden/unroutable aliases stay out of navigation. */ }
                } else if (!S_ISREG(info.st_mode)) {
                    throw Rejected{403, "linked target is a special file"};
                }
            } else if (S_ISDIR(info.st_mode)) {
                Descriptor child(openat(fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                inspect(child.get(), item, visible, external, code);
            } else if (!S_ISREG(info.st_mode)) {
                throw Rejected{403, "site contains a symlink or special file"};
            }
        }
        ancestors.pop_back();
    }
};
static void setEnvironment(const char *name, const string &value) {
    if (setenv(name, value.c_str(), 1) != 0) throw Rejected{500, "cannot initialize request environment"};
}
static void clearIdentity() {
    std::vector<string> remove;
    for (char **item = environ; *item; ++item) {
        const char *equal = std::strchr(*item, '=');
        if (!equal) continue;
        string key(*item, equal - *item);
        if (key.rfind("security_", 0) == 0 || key.rfind("post_arg_", 0) == 0 ||
            key.rfind("logged_", 0) == 0 || key == "HTTP_COOKIE" || key == "HTTP_AUTHORIZATION" ||
            key == "user_name" || key == "user_password") remove.push_back(key);
    }
    for (const string &key : remove)
        if (unsetenv(key.c_str()) != 0) throw Rejected{500, "cannot initialize request environment"};
}
static int reject(const Rejected &error, bool head) {
    const char *title = error.status == 400 ? "400 Bad Request" :
                        error.status == 403 ? "403 Forbidden" :
                        error.status == 404 ? "404 Not Found" :
                        error.status == 405 ? "405 Method Not Allowed" :
                        error.status == 414 ? "414 URI Too Long" : "500 Internal Server Error";
    std::cout << "Status: " << title << "\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\n";
    if (error.status == 405) std::cout << "Allow: GET, HEAD\r\n";
    std::cout << "\r\n";
    if (!head) std::cout << "Request rejected.\n";
    std::cerr << "werc-guard: " << error.reason << '\n';
    return 1;
}
int main() {
    const string method = environment("REQUEST_METHOD");
    try {
        // Reject before reading stdin, inspecting content or starting rc.
        if (method != "GET" && method != "HEAD") throw Rejected{405, "read-only publishing accepts GET and HEAD only"};
        const string host = environment("SERVER_NAME");
        const char *selector = std::getenv("WERC_SITE");
        const string site = selector ? selector : host;
        validateName(host);
        validateName(site);
        validatePort(environment("SERVER_PORT"));
        const string uri = environment("REQUEST_URI");
        const string path = requestPath(uri);
        // The launcher enters bin/ before invoking this executable.
        const fs::path root = fs::current_path().parent_path();
        Publication publication{root, approvedDirectories(root), {}, {}};
        {
            Descriptor selected = openDirectory(root / "sites" / site);
            publication.inspect(selected.get(), root / "sites" / site, {});
        }
        clearIdentity();
        string linkedDirectories;
        for (const auto &alias : publication.linkedDirectories) {
            const string parent = fs::path(alias.substr(0, alias.size()-1)).parent_path().generic_string() + "/";
            if (parent == "//" || path.rfind(parent, 0) == 0) linkedDirectories += alias + "\n";
        }
        setEnvironment("werc_linked_dirs", linkedDirectories);
        setEnvironment("werc_readonly", "yes");
        setEnvironment("werc_site", site);
        const size_t question = uri.find('?');
        setEnvironment("REQUEST_URI", path + (question == string::npos ? "" : uri.substr(question)));
        const string plan9 = environment("PLAN9");
        if (plan9.empty()) throw Rejected{500, "PLAN9 must identify the Plan 9 installation"};
        const string rc = plan9 + "/bin/rc";
        execl(rc.c_str(), "rc", "-I", "./werc.rc", static_cast<char *>(nullptr));
        throw Rejected{500, "cannot execute the rc interpreter"};
    } catch (const Rejected &error) {
        return reject(error, method == "HEAD");
    } catch (const std::exception &) {
        return reject({500, "cannot initialize read-only CGI"}, method == "HEAD");
    }
}
