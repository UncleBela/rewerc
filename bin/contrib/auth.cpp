#include "auth.h"
#include "utf8.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
#include <memory>
#include <algorithm>
namespace auth {
using std::string;
namespace fs=std::filesystem;
namespace {
struct Failure:std::runtime_error{int code;Failure(int c):std::runtime_error("authentication request rejected"),code(c){}};
string env(const char *name){const char *v=getenv(name);return v?v:"";}
string hex(const unsigned char *bytes,size_t n){static const char chars[]="0123456789abcdef";string out;for(size_t i=0;i<n;++i){out+=chars[bytes[i]>>4];out+=chars[bytes[i]&15];}return out;}
string randomToken(){unsigned char data[32];if(RAND_bytes(data,sizeof data)!=1)throw Failure(503);return hex(data,sizeof data);}
string digest(const string &value){unsigned char out[EVP_MAX_MD_SIZE];unsigned n=0;if(EVP_Digest(value.data(),value.size(),out,&n,EVP_sha256(),nullptr)!=1)throw Failure(503);return hex(out,n);}
bool equal(const string &a,const string &b){return a.size()==b.size()&&CRYPTO_memcmp(a.data(),b.data(),a.size())==0;}
bool token(const string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==string::npos;}
bool username(const string &s){return !s.empty()&&s.size()<=64&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==string::npos;}
string passwordHash(const string &password,const string &salt){
    unsigned char out[32];
    // OpenSSL implements scrypt; 128 MiB work factor, no project cryptography.
    if(EVP_PBE_scrypt(password.data(),password.size(),reinterpret_cast<const unsigned char*>(salt.data()),salt.size(),131072,8,1,160ULL*1024*1024,out,sizeof out)!=1)throw Failure(503);
    return hex(out,sizeof out);
}
int secureDirectory(const fs::path &path,bool create){
    int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)throw Failure(503);
    for(const auto &part:path.relative_path()){
        auto name=part.string();if(name==".."||name.empty()){close(fd);throw Failure(503);}if(name==".")continue;
        if(create&&mkdirat(fd,name.c_str(),0700)!=0&&errno!=EEXIST){close(fd);throw Failure(503);}
        int next=openat(fd,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(fd);fd=next;
        if(fd<0)throw Failure(503);
    }
    struct stat st{};
    if(fstat(fd,&st)!=0||st.st_uid!=geteuid()||(st.st_mode&0077)){close(fd);throw Failure(503);}
    return fd;
}
struct Query {
    sqlite3_stmt *s=nullptr;
    explicit Query(sqlite3 *db,const char *sql){if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK)throw Failure(503);}
    ~Query(){sqlite3_finalize(s);}
    void bind(int i,const string &value){if(sqlite3_bind_text(s,i,value.c_str(),value.size(),SQLITE_TRANSIENT)!=SQLITE_OK)throw Failure(503);}
    void bind(int i,long long value){if(sqlite3_bind_int64(s,i,value)!=SQLITE_OK)throw Failure(503);}
    bool row(){int r=sqlite3_step(s);if(r!=SQLITE_ROW&&r!=SQLITE_DONE)throw Failure(503);return r==SQLITE_ROW;}
    string text(int col){auto p=sqlite3_column_text(s,col);return p?reinterpret_cast<const char*>(p):"";}
    long long integer(int col){return sqlite3_column_int64(s,col);}
};
struct Database {
    sqlite3 *db=nullptr;int directory=-1;
    explicit Database(const fs::path &root){
        fs::path dir=env("WERC_AUTH_DIR").empty()?root/"var/auth":fs::path(env("WERC_AUTH_DIR"));
        if(!dir.is_absolute())throw Failure(503);
        directory=secureDirectory(dir,true);
        int file=openat(directory,"auth.sqlite",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
        struct stat st{};
        if(file<0||fstat(file,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077)||st.st_nlink!=1){if(file>=0)close(file);throw Failure(503);}
        close(file);
        auto path=(dir/"auth.sqlite").string();
        if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOFOLLOW,nullptr)!=SQLITE_OK)throw Failure(503);
        sqlite3_busy_timeout(db,1000);
        exec("PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON;");
        exec("CREATE TABLE IF NOT EXISTS users(name TEXT PRIMARY KEY,salt TEXT NOT NULL,hash TEXT NOT NULL,enabled INTEGER NOT NULL DEFAULT 1);"
             "CREATE TABLE IF NOT EXISTS sessions(id TEXT PRIMARY KEY,scope TEXT NOT NULL,user TEXT NOT NULL,csrf TEXT NOT NULL,expires INTEGER NOT NULL,touched INTEGER NOT NULL);"
             "CREATE INDEX IF NOT EXISTS session_expiry ON sessions(expires);"
             "CREATE TABLE IF NOT EXISTS limits(key TEXT PRIMARY KEY,started INTEGER NOT NULL,count INTEGER NOT NULL);");
    }
    ~Database(){if(db)sqlite3_close(db);if(directory>=0)close(directory);}
    void exec(const char *sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw Failure(503);}
    long long scalar(const char *sql){Query q(db,sql);return q.row()?q.integer(0):0;}
    void cleanup(long long now){
        Query q(db,"DELETE FROM sessions WHERE expires<? OR touched<?");q.bind(1,now);q.bind(2,now-1800);q.row();
        Query l(db,"DELETE FROM limits WHERE started<?");l.bind(1,now-900);l.row();
    }
    void limit(const string &key,int maximum,int seconds,long long now){
        Query old(db,"SELECT started,count FROM limits WHERE key=?");old.bind(1,key);
        long long start=now,count=0;
        if(old.row()&&old.integer(0)>now-seconds){start=old.integer(0);count=old.integer(1);}
        if(count>=maximum)throw Failure(429);
        if(scalar("SELECT count(*) FROM limits")>=4096)throw Failure(429);
        Query q(db,"INSERT INTO limits VALUES(?,?,?) ON CONFLICT(key) DO UPDATE SET started=excluded.started,count=excluded.count");
        q.bind(1,key);q.bind(2,start);q.bind(3,count+1);q.row();
    }
};
string cookie(){
    auto value=env("HTTP_COOKIE");if(value.size()>8192)throw Failure(400);
    string answer;bool found=false;size_t pos=0;
    while(pos<value.size()){
        auto end=value.find(';',pos);auto part=value.substr(pos,end==string::npos?end:end-pos);
        auto first=part.find_first_not_of(" \t");if(first!=string::npos)part.erase(0,first);
        if(part.rfind("werc_session=",0)==0){if(found)throw Failure(400);found=true;answer=part.substr(13);}
        if(end==string::npos)break;
        pos=end+1;
    }
    return token(answer)?answer:"";
}
void headers(const string &status,const string &extra="",const string &type="text/html; charset=utf-8"){
    std::cout<<"Status: "<<status<<"\r\nContent-Type: "<<type<<"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"<<extra<<"\r\n";
}
string setCookie(const string &id,bool clear=false){
    return "Set-Cookie: werc_session="+id+"; Path=/; HttpOnly; SameSite=Strict; Max-Age="+(clear?"0":"28800")+(env("HTTPS")=="on"||env("HTTPS")=="yes"?"; Secure":"")+"\r\n";
}
void redirect(const string &where,const string &extra=""){headers("303 See Other","Location: "+where+"\r\n"+extra);}
struct Session {string id,user,csrf;bool valid=false;};
Session lookup(Database &db,const string &scope,long long now){
    Session result;auto id=cookie();if(id.empty())return result;
    Query q(db.db,"SELECT s.id,s.user,s.csrf FROM sessions s LEFT JOIN users u ON u.name=s.user WHERE s.id=? AND s.scope=? AND s.expires>? AND s.touched>? AND (s.user='' OR u.enabled=1)");
    q.bind(1,digest(id));q.bind(2,scope);q.bind(3,now);q.bind(4,now-1800);
    if(q.row()){result={q.text(0),q.text(1),q.text(2),true};}
    return result;
}
Session create(Database &db,const string &scope,const string &user,long long now,string &raw){
    if(db.scalar("SELECT count(*) FROM sessions")>=4096)throw Failure(429);
    raw=randomToken();Session s{digest(raw),user,randomToken(),true};
    Query q(db.db,"INSERT INTO sessions VALUES(?,?,?,?,?,?)");q.bind(1,s.id);q.bind(2,scope);q.bind(3,user);q.bind(4,s.csrf);q.bind(5,now+(user.empty()?600:28800));q.bind(6,now);q.row();return s;
}
void erase(Database &db,const string &id){Query q(db.db,"DELETE FROM sessions WHERE id=?");q.bind(1,id);q.row();}
int fromHex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw Failure(400);}
string decode(const string &s){string out;for(size_t i=0;i<s.size();++i){char c=s[i];if(c=='%'){if(i+2>=s.size())throw Failure(400);c=char(fromHex(s[i+1])*16+fromHex(s[i+2]));i+=2;}else if(c=='+')c=' ';if(c=='\0'||c=='\r'||c=='\n')throw Failure(400);out+=c;}if(!text::validUtf8(out))throw Failure(400);return out;}
std::map<string,string> form(){
    if(env("CONTENT_TYPE")!="application/x-www-form-urlencoded")throw Failure(415);
    auto length=env("CONTENT_LENGTH");if(length.empty()||length.size()>6||length.find_first_not_of("0123456789")!=string::npos)throw Failure(400);
    auto size=std::stoul(length);if(size>65536)throw Failure(413);
    string body(size,'\0');std::cin.read(body.data(),size);if(size_t(std::cin.gcount())!=size)throw Failure(400);
    std::map<string,string> result;size_t pos=0;
    while(pos<body.size()){
        auto end=body.find('&',pos),eq=body.find('=',pos);if(eq==string::npos||(end!=string::npos&&eq>end))throw Failure(400);
        auto key=decode(body.substr(pos,eq-pos));auto value=decode(body.substr(eq+1,end==string::npos?end:end-eq-1));
        if(key.empty()||result.size()>=32||!result.emplace(key,value).second)throw Failure(400);
        if(end==string::npos)break;
        pos=end+1;
    }
    OPENSSL_cleanse(body.data(),body.size());return result;
}
void page(const Session &session,bool logout,const string &extra){
    headers("200 OK",extra);
    if(env("REQUEST_METHOD")=="HEAD")return;
    std::cout<<"<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>rewerc account</title><link rel=\"stylesheet\" href=\"/pub/style/style.css\"><main><h1>"<<(logout?"Sign out":"Sign in")<<"</h1><form method=\"post\" action=\"/_users/"<<(logout?"logout":"login")<<"\"><input type=\"hidden\" name=\"csrf\" value=\""<<session.csrf<<"\">";
    if(!logout)std::cout<<"<label>Username <input name=\"username\" autocomplete=\"username\" required maxlength=\"64\"></label><label>Password <input type=\"password\" name=\"password\" autocomplete=\"current-password\" required maxlength=\"1024\"></label>";
    std::cout<<"<button>"<<(logout?"Sign out":"Sign in")<<"</button></form></main></html>\n";
}
}
bool request(const fs::path &root,const string &site,const string &path){
    try{
        Database db(root);auto now=static_cast<long long>(time(nullptr));db.exec("BEGIN IMMEDIATE");db.cleanup(now);
        auto scope=env("SERVER_NAME")+":"+env("SERVER_PORT")+"/"+site+"/"+env("HTTPS");
        Session session=lookup(db,scope,now);
        bool login=path=="/_users/login",logout=path=="/_users/logout";
        auto method=env("REQUEST_METHOD");
        if(!login&&!logout){
            if(!session.valid||session.user.empty()){db.exec("COMMIT");redirect("/_users/login");return true;}
            Query q(db.db,"UPDATE sessions SET touched=? WHERE id=?");q.bind(1,now);q.bind(2,session.id);q.row();db.exec("COMMIT");
            setenv("werc_authenticated","yes",1);setenv("werc_auth_user",session.user.c_str(),1);return false;
        }
        if(db.scalar("SELECT count(*) FROM users WHERE enabled=1")==0)throw Failure(503);
        if(method=="GET"||method=="HEAD"){
            if(logout&&(!session.valid||session.user.empty())){db.exec("COMMIT");redirect("/_users/login");return true;}
            if(login&&!session.user.empty()){db.exec("COMMIT");redirect("/");return true;}
            string raw,extra;
            if(!session.valid){
                db.limit("pre:"+digest(env("REMOTE_ADDR")),40,300,now);db.limit("pre:global",400,300,now);
                session=create(db,scope,"",now,raw);extra=setCookie(raw);
            }
            db.exec("COMMIT");page(session,logout,extra);return true;
        }
        string origin=(env("HTTPS")=="on"||env("HTTPS")=="yes"?"https://":"http://")+env("SERVER_NAME");
        string port=env("SERVER_PORT");if(!((origin.rfind("https",0)==0&&port=="443")||(origin.rfind("http://",0)==0&&port=="80")))origin+=":"+port;
        bool sameOrigin=env("HTTP_ORIGIN")==origin;
        if(env("SERVER_NAME")=="localhost") {
            string alias=(env("HTTPS")=="on"||env("HTTPS")=="yes"?"https://":"http://");
            alias+="127.0.0.1";
            if(!((alias.rfind("https",0)==0&&port=="443")||(alias.rfind("http://",0)==0&&port=="80")))alias+=":"+port;
            sameOrigin=sameOrigin||env("HTTP_ORIGIN")==alias;
        }
        if(!session.valid||!sameOrigin)throw Failure(403);
        auto values=form();
        if(!values.count("csrf")||!equal(values["csrf"],session.csrf))throw Failure(403);
        if(logout){if(values.size()!=1||session.user.empty())throw Failure(403);erase(db,session.id);db.exec("COMMIT");redirect("/_users/login",setCookie("",true));return true;}
        if(values.size()!=3||!values.count("username")||!values.count("password")||!username(values["username"])||values["password"].size()>1024)throw Failure(400);
        // Count attempts before expensive password work, with global and per-account/IP caps.
        db.limit("login:global",30,300,now);db.limit("login:ip:"+digest(env("REMOTE_ADDR")),10,300,now);db.limit("login:user:"+digest(values["username"]),10,300,now);
        db.exec("COMMIT");
        string salt(64,'0'),expected(64,'0');bool enabled=false;
        {Query q(db.db,"SELECT salt,hash,enabled FROM users WHERE name=?");q.bind(1,values["username"]);if(q.row()){salt=q.text(0);expected=q.text(1);enabled=q.integer(2)==1;}}
        string actual=passwordHash(values["password"],salt);OPENSSL_cleanse(values["password"].data(),values["password"].size());
        if(!equal(actual,expected)||!enabled)throw Failure(403);
        db.exec("BEGIN IMMEDIATE");
        // Recheck after hashing so revocation/password changes cannot race a new session.
        {Query q(db.db,"SELECT hash FROM users WHERE name=? AND enabled=1");q.bind(1,values["username"]);if(!q.row()||!equal(q.text(0),expected))throw Failure(403);}
        erase(db,session.id);string raw;create(db,scope,values["username"],now,raw);db.exec("COMMIT");redirect("/",setCookie(raw));return true;
    }catch(const Failure &e){
        string status=e.code==400?"400 Bad Request":e.code==403?"403 Forbidden":e.code==413?"413 Content Too Large":e.code==415?"415 Unsupported Media Type":e.code==429?"429 Too Many Requests":"503 Service Unavailable";
        headers(status,e.code==429?"Retry-After: 300\r\n":"","text/plain; charset=utf-8");
        if(env("REQUEST_METHOD")!="HEAD")std::cout<<(e.code==503?"Account service unavailable. Provision an account offline or check private state permissions.\n":"Account request rejected.\n");
        return true;
    }
}
int provision(const fs::path &root,int argc,char **argv){
    try{
        if(!env("REQUEST_METHOD").empty()||argc<3||!username(argv[2]))throw Failure(400);
        string mode=argv[1];if(mode!="--add-user"&&mode!="--disable-user")throw Failure(400);
        Database db(root);
        if(mode=="--disable-user"){
            Query q(db.db,"UPDATE users SET enabled=0 WHERE name=?");q.bind(1,argv[2]);q.row();
            Query s(db.db,"DELETE FROM sessions WHERE user=?");s.bind(1,argv[2]);s.row();return 0;
        }
        string password;
        if(argc==4&&string(argv[3])=="--password-stdin"){
            char c;while(std::cin.get(c)&&c!='\n'){if(password.size()>=1024)throw Failure(400);password+=c;}
        }else if(argc==3){
            char buffer[1025]{};
            if(EVP_read_pw_string(buffer,sizeof buffer,"Password",1)!=0){OPENSSL_cleanse(buffer,sizeof buffer);throw Failure(400);}
            password=buffer;OPENSSL_cleanse(buffer,sizeof buffer);
        }else throw Failure(400);
        if(password.size()<12||password.size()>1024||password.find_first_of(string("\0\r",2))!=string::npos||!text::validUtf8(password))throw Failure(400);
        auto salt=randomToken(),hash=passwordHash(password,salt);OPENSSL_cleanse(password.data(),password.size());
        db.exec("BEGIN IMMEDIATE");
        Query q(db.db,"INSERT INTO users VALUES(?,?,?,1) ON CONFLICT(name) DO UPDATE SET salt=excluded.salt,hash=excluded.hash,enabled=1");q.bind(1,argv[2]);q.bind(2,salt);q.bind(3,hash);q.row();
        Query s(db.db,"DELETE FROM sessions WHERE user=?");s.bind(1,argv[2]);s.row();db.exec("COMMIT");
        std::cout<<"Account provisioned; existing sessions revoked.\n";return 0;
    }catch(...){std::cerr<<"Account provisioning failed. Use a valid username and a matching 12–1024 byte password; check private state permissions.\n";return 1;}
}
}
