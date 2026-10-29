#include "published-files.h"
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <memory>
namespace published {
namespace fs=std::filesystem;
namespace {
struct Fd {
    int value;
    explicit Fd(int fd):value(fd) {}
    ~Fd(){ if(value>=0) close(value); }
    Fd(const Fd&)=delete;
    int release(){int n=value;value=-1;return n;}
};
[[noreturn]] void denied(){throw std::runtime_error("symlink, special file, or inaccessible publication path");}
std::string env(const char *key){const char *v=getenv(key);return v?v:"";}
int capability(const char *key,bool directory=true){
    auto text=env(key);
    if(text.empty()||text.size()>7) denied();
    unsigned n=0;
    for(char c:text){if(c<'0'||c>'9') denied();n=n*10+unsigned(c-'0');}
    struct stat st{};
    if(n<3||n>1000000||fstat(n,&st)!=0|| (directory?!S_ISDIR(st.st_mode):!S_ISREG(st.st_mode))) denied();
    return n;
}
void charge(unsigned amount){
    if(!requestContext()) return;
    int fd=capability("WERC_BUDGET_FD",false);
    struct flock lock{};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fcntl(fd,F_SETLKW,&lock)<0) denied();
    unsigned used=0;
    bool ok=pread(fd,&used,sizeof used,0)==sizeof used && used<=16384 && amount<=16384-used;
    if(ok){used+=amount;ok=pwrite(fd,&used,sizeof used,0)==sizeof used;}
    lock.l_type=F_UNLCK;fcntl(fd,F_SETLK,&lock);
    if(!ok) throw std::runtime_error("request filesystem work limit exceeded");
}
fs::path directoryName(int fd){
    char data[16384];auto n=readlink(descriptorPath(fd).c_str(),data,sizeof data);
    if(n<=0||n==sizeof data) denied();
    return std::string(data,n);
}
bool beneath(const fs::path &path,const fs::path &base){
    auto i=path.begin();for(auto &part:base){if(i==path.end()||*i!=part)return false;++i;}return true;
}
void publicPath(const fs::path &path){
    for(auto &part:path) if(part=="_werc"||part==".git") denied();
    auto ext=path.extension().string();
    for(char &c:ext)if(c>='A'&&c<='Z')c+='a'-'A';
    for(auto blocked:{".tpl",".rc",".conf",".cgi",".py",".pl",".awk",".cpp",".h",".sqlite",".sqlite3",".db"})
        if(ext==blocked)denied();
}
int resolve(const std::string &name,Access access,bool directory){
    if(!requestContext()){
        int fd=open(name.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|(directory?O_DIRECTORY:0));
        struct stat st{};Fd held(fd);
        if(fd<0||fstat(fd,&st)!=0||(directory?!S_ISDIR(st.st_mode):!S_ISREG(st.st_mode)))denied();
        return held.release();
    }
    int project=capability("WERC_PROJECT_FD"),site=capability("WERC_SITE_FD");
    fs::path path(name),projectName=directoryName(project);
    for(auto &part:path)if(part=="..")denied();
    if(path.is_absolute()){
        if(!beneath(path,projectName))denied();
        path=path.lexically_relative(projectName);
    }
    path=path.lexically_normal();
    fs::path prefix=fs::path("sites")/env("werc_site"),relative=path;
    int root=project;
    if(access==Access::Content){
        if(!beneath(path,prefix))denied();
        relative=path.lexically_relative(prefix);publicPath(relative);root=site;
    }else{
        if(path.empty()||path.is_absolute()||*path.begin()=="var"||*path.begin()==".git")denied();
        if(access==Access::Configuration&&(!beneath(path,prefix)||path.filename()!="config"||path.parent_path().filename()!="_werc"))denied();
    }
    int current=dup(root);if(current<0)denied();
    Fd start(current);
    std::vector<std::string> components;
    for(auto &part:relative)if(part!="."&&!part.empty())components.push_back(part.string());
    if(components.size()>32)throw std::runtime_error("directory depth limit exceeded");
    for(size_t i=0;i<components.size();++i){
        charge(1);
        struct open_how how{};
        how.flags=O_PATH|O_NOFOLLOW|O_CLOEXEC;
        how.resolve=RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS;
        int next=syscall(SYS_openat2,current,components[i].c_str(),&how,sizeof how);
        if(next<0){if(access==Access::Configuration&&errno==ENOENT)return -1;denied();}
        struct stat st{};
        if(fstat(next,&st)!=0||S_ISLNK(st.st_mode)||(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))){close(next);denied();}
        if(current!=start.value)close(current);
        current=next;
        if(i+1<components.size()&&!S_ISDIR(st.st_mode)){close(current);denied();}
    }
    Fd final(current==start.value?dup(current):current);
    struct stat before{},after{};
    if(final.value<0||fstat(final.value,&before)!=0||(directory?!S_ISDIR(before.st_mode):!S_ISREG(before.st_mode)))denied();
    Fd result(open(descriptorPath(final.value).c_str(),O_RDONLY|O_CLOEXEC|(directory?O_DIRECTORY:0)));
    if(result.value<0||fstat(result.value,&after)!=0||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino)denied();
    return result.release();
}
}
bool requestContext(){return env("werc_readonly")=="yes";}
std::string descriptorPath(int fd){return "/proc/self/fd/"+std::to_string(fd);}
void createBudget(){
    int fd=syscall(SYS_memfd_create,"werc-work",0);
    unsigned count=0;
    if(fd<0||write(fd,&count,sizeof count)!=sizeof count)denied();
    if(setenv("WERC_BUDGET_FD",std::to_string(fd).c_str(),1)!=0)denied();
}
int openFile(const std::string &name,Access access){return resolve(name,access,false);}
int openDirectory(const std::string &name){return resolve(name,Access::Content,true);}
std::vector<std::string> listDirectory(const std::string &name){
    Fd fd(openDirectory(name));
    DIR *raw=fdopendir(dup(fd.value));if(!raw)denied();
    std::unique_ptr<DIR,int(*)(DIR*)> dir(raw,closedir);
    std::vector<std::string> result;
    unsigned entries=0;
    while(true){
        errno=0;dirent *entry=readdir(dir.get());
        if(!entry){if(errno)denied();break;}
        std::string item=entry->d_name;
        if(item=="."||item=="..")continue;
        charge(1);if(++entries>2048)throw std::runtime_error("directory entry limit exceeded");
        // Private entries never enter navigation, including their metadata.
        if(item[0]=='.'||item[0]=='_')continue;
        struct open_how how{};how.flags=O_PATH|O_NOFOLLOW|O_CLOEXEC;how.resolve=RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS;
        Fd child(syscall(SYS_openat2,fd.value,item.c_str(),&how,sizeof how));
        struct stat st{};
        if(child.value<0||fstat(child.value,&st)!=0||(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode)))denied();
        if(S_ISDIR(st.st_mode))item+='/';
        result.push_back((fs::path(name)/item).generic_string());
    }
    std::sort(result.begin(),result.end());return result;
}
InputBuffer::int_type InputBuffer::underflow(){
    if(gptr()&&gptr()<egptr())return traits_type::to_int_type(*gptr());
    ssize_t n;do{n=read(fd,buffer,sizeof buffer);}while(n<0&&errno==EINTR);
    if(n<0)throw std::runtime_error("cannot read publication file");
    if(!n)return traits_type::eof();
    setg(buffer,buffer,buffer+n);return traits_type::to_int_type(*gptr());
}
InputBuffer::~InputBuffer(){close(fd);}
}
