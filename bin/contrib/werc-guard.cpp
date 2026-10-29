// Read-only CGI boundary. Symlinks require explicit private publication roots.
#include "published-files.h"
#include "auth.h"
#include "utf8.h"
#include <sys/resource.h>
#include <csignal>
#include <algorithm>
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
// Restrict path syntax without restricting authored names to ASCII.
using text::validUtf8;
static bool pathCharacter(unsigned char c) {
    return c>=128 || asciiAlpha(c) || asciiDigit(c) || string("_+-/.,:; ()").find(c)!=string::npos;
}
static string encodePath(const string &path) {
    static const char hex[]="0123456789ABCDEF";
    string result;
    for(unsigned char c:path) {
        if(asciiAlpha(c)||asciiDigit(c)||string("-._~/").find(c)!=string::npos) result+=c;
        else { result+='%'; result+=hex[c>>4]; result+=hex[c&15]; }
    }
    return result;
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
        if (!pathCharacter(static_cast<unsigned char>(c)))
            throw Rejected{400, "invalid URL path"};
        if (c != '/' || path.empty() || path.back() != '/') path += c;
    }
    if(!validUtf8(path)) throw Rejected{400,"invalid UTF-8 path"};
    if (path.empty() || path[0] != '/' || path.find("..") != string::npos)
        throw Rejected{403, "path traversal is forbidden"};
    if(std::count(path.begin(),path.end(),'/')>33) throw Rejected{413,"directory depth limit exceeded"};
    if(path=="/_users/login"||path=="/_users/logout") return path;
    for(size_t i=1;i<path.size();++i)
        if(path[i-1]=='/'&&(path[i]=='.'||path[i]=='_'))
            throw Rejected{404,"private paths are unavailable"};
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
            key.rfind("logged_", 0) == 0 || key == "HTTP_AUTHORIZATION" ||
            key == "user_name" || key == "user_password" || key == "werc_authenticated" || key == "werc_auth_user") remove.push_back(key);
    }
    for (const string &key : remove)
        if (unsetenv(key.c_str()) != 0) throw Rejected{500, "cannot initialize request environment"};
}
static int reject(const Rejected &error, bool head) {
    const char *title = error.status == 400 ? "400 Bad Request" :
                        error.status == 403 ? "403 Forbidden" :
                        error.status == 404 ? "404 Not Found" :
                        error.status == 405 ? "405 Method Not Allowed" :
                        error.status == 503 ? "503 Service Unavailable" : error.status == 413 ? "413 Content Too Large" : error.status == 414 ? "414 URI Too Long" : "500 Internal Server Error";
    std::cout << "Status: " << title << "\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\n";
    if (error.status == 405) std::cout << "Allow: GET, HEAD\r\n";
    std::cout << "\r\n";
    if (!head) std::cout << "Request rejected.\n";
    std::cerr << "werc-guard: " << error.reason << '\n';
    return 1;
}
static void inherit(const char *name,int fd) {
    if(fcntl(fd,F_SETFD,0)<0) throw Rejected{500,"cannot retain request capabilities"};
    setEnvironment(name,std::to_string(fd));
}
static int fileCommand(int argc,char **argv) {
    if(!published::requestContext()) throw Rejected{403,"file commands require a guarded request"};
    const string mode=argv[1];
    if(mode=="--list" && argc>=3) {
        std::vector<string> lines;
        for(int i=2;i<argc;++i) {
            auto entries=published::listDirectory(argv[i]);
            lines.insert(lines.end(),entries.begin(),entries.end());
        }
        for(auto &line:lines) std::cout<<line<<'\n';
        return 0;
    }
    if(mode=="--mtime" && argc==3) {
        string name=argv[2];
        Descriptor fd(!name.empty()&&name.back()=='/'?published::openDirectory(name):published::openFile(name));struct stat st{};
        if(fstat(fd.get(),&st)!=0) throw Rejected{403,"cannot inspect content"};
        std::cout<<st.st_mtime<<'\n';return 0;
    }
    if(mode=="--encode-path" && argc==3) { std::cout<<encodePath(argv[2]); return 0; }
    if(mode=="--path-lines" && argc==2) {
        string line;
        while(std::getline(std::cin,line)) {
            bool allowed=validUtf8(line);
            for(unsigned char c:line) if(!pathCharacter(c)) allowed=false;
            if(allowed) std::cout<<encodePath(line)<<'\t'<<line<<'\n';
        }
        return 0;
    }
    if(mode=="--template-code") {
        std::vector<int> held;
        std::vector<string> arguments={"awk","-f","bin/template.awk"};
        for(int i=2;i<argc;++i) {
            int fd=published::openFile(argv[i],published::Access::Code);
            if(fcntl(fd,F_SETFD,0)<0) throw Rejected{500,"cannot pin template"};
            held.push_back(fd); arguments.push_back(published::descriptorPath(fd));
        }
        if(held.empty()) arguments.push_back("/dev/null");
        std::vector<char *> args;
        for(auto &argument:arguments) args.push_back(argument.data());
        args.push_back(nullptr); execvp(args[0],args.data());
        throw Rejected{500,"cannot translate template"};
    }
    if(argc!=3 || (mode!="--read"&&mode!="--read-local"&&mode!="--config"))
        throw Rejected{403,"invalid file command"};
    const auto access=mode=="--config"?published::Access::Configuration:
                      mode=="--read-local"?published::Access::Code:published::Access::Content;
    const int fd=published::openFile(argv[2],access);
    if(fd<0) return 0; // Absent local configuration is ordinary.
    Descriptor file(fd);
    char buffer[8192]; ssize_t count;
    while((count=read(fd,buffer,sizeof buffer))>0) std::cout.write(buffer,count);
    if(count<0 || !std::cout) throw Rejected{500,"cannot read pinned file"};
    return 0;
}
static void requestLimits() {
    struct Limit { int resource; rlim_t value; };
    for(const auto &limit:std::vector<Limit>{{RLIMIT_CPU,5},{RLIMIT_AS,512ULL*1024*1024},{RLIMIT_FSIZE,32ULL*1024*1024},{RLIMIT_NOFILE,256},{RLIMIT_CORE,0}}) {
        struct rlimit current{};
        if(getrlimit(limit.resource,&current)!=0) throw Rejected{500,"cannot read process limits"};
        current.rlim_cur=std::min(current.rlim_cur,limit.value);
        if(setrlimit(limit.resource,&current)!=0) throw Rejected{500,"cannot set process limits"};
    }
    alarm(20);
}
int main(int argc,char **argv) {
    const string method=environment("REQUEST_METHOD");
    try {
        if(argc>1) {
            string mode=argv[1];
            if(mode=="--add-user"||mode=="--disable-user")
                return auth::provision(fs::current_path(),argc,argv);
            return fileCommand(argc,argv);
        }
        requestLimits();
        const string host=environment("SERVER_NAME");
        const char *selector=getenv("WERC_SITE");
        const string site=selector?selector:host;
        validateName(host);validateName(site);validatePort(environment("SERVER_PORT"));
        const string uri=environment("REQUEST_URI"),path=requestPath(uri);
        const bool account=path=="/_users/login"||path=="/_users/logout";
        if(method!="GET"&&method!="HEAD"&&!(method=="POST"&&account))
            throw Rejected{405,"only account endpoints accept POST"};
        const fs::path root=fs::current_path().parent_path();
        if(environment("WERC_DEVELOPMENT")!="yes") {
            Descriptor etc=openDirectory(root/"etc");
            int raw=openat(etc.get(),"service.uid",O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
            if(raw<0)throw Rejected{503,"service account has not been prepared"};
            Descriptor config(raw);
            struct stat st{};char data[32];ssize_t count=read(config.get(),data,sizeof data);
            if(fstat(config.get(),&st)!=0||!S_ISREG(st.st_mode)||(st.st_mode&0022)||count<2||count>=ssize_t(sizeof data))
                throw Rejected{503,"service account has not been prepared"};
            string value(data,count);
            if(value.back()=='\n')value.pop_back();
            if(value.empty()||value.find_first_not_of("0123456789")!=string::npos||value.size()>10||std::stoull(value)!=geteuid()||geteuid()==0)
                throw Rejected{503,"CGI must run as the dedicated service account"};
        }
        clearIdentity();
        // Shared assets are deliberately public; every site attachment remains authenticated.
        const bool publicAsset=path=="/pub/style/style.css"||path=="/pub/favicon.ico";
        if(!publicAsset&&auth::request(root,site,path)) return 0;
        unsetenv("HTTP_COOKIE");unsetenv("HTTP_AUTHORIZATION");
        Descriptor project=openDirectory(root),selected=openDirectory(root/"sites"/site);
        inherit("WERC_PROJECT_FD",project.get());inherit("WERC_SITE_FD",selected.get());
        setEnvironment("werc_readonly","yes");setEnvironment("werc_site",site);
        published::createBudget();
        if(publicAsset) {
            Descriptor file(published::openFile(path.substr(1),published::Access::Code));
            std::cout<<"Content-Type: "<<(path=="/pub/style/style.css"?"text/css":"image/x-icon")<<"\r\nX-Content-Type-Options: nosniff\r\n\r\n";
            if(method!="HEAD") { char data[8192];ssize_t n;while((n=read(file.get(),data,sizeof data))>0)std::cout.write(data,n); }
            return 0;
        }
        // Only enumerate navigation ancestors; never scan unrelated subtrees.
        fs::path directory=fs::path("sites")/site;
        published::listDirectory(directory.string());
        fs::path relative=path.substr(1);
        if(path.back()!='/')relative=relative.parent_path();
        for(const auto &part:relative) {
            if(part.empty()||part==".")continue;
            directory/=part;
            published::listDirectory(directory.string());
        }
        setEnvironment("werc_request_path",path);
        const auto question=uri.find('?');
        setEnvironment("REQUEST_URI",path+(question==string::npos?"":uri.substr(question)));
        const string plan9=environment("PLAN9");
        if(plan9.empty())throw Rejected{500,"PLAN9 must identify Plan 9 installation"};
        const string rc=plan9+"/bin/rc";
        execl(rc.c_str(),"rc","-I","./werc.rc",static_cast<char*>(nullptr));
        throw Rejected{500,"cannot execute rc"};
    }catch(const Rejected &error){
        if(argc>1){std::cerr<<"werc-guard: pinned file access failed\n";return 1;}
        return reject(error,method=="HEAD");
    }catch(const std::exception &error){
        if(argc>1){std::cerr<<"werc-guard: pinned file access failed\n";return 1;}
        // Fixed categories only; never log request data or credentials.
        string message=error.what();
        bool limited=message.find("limit")!=string::npos;
        return reject({limited?413:403,limited?"request filesystem limit exceeded":"symlink or special file access rejected"},method=="HEAD");
    }
}
