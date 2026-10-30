#include"httprequest.h"
using namespace std;
const unordered_set<string> HttpRequest::DEFAULT_HTML{
                        "/index","/register","/login",
                        "/welcome","/video","/picture",
                        "/dashboard",
};
const unordered_map<string,int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html",0},{"/login.html",1},
};
void HttpRequest::Init(){
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}
bool HttpRequest::IsKeepAlive() const{
    /* HTTP 头名大小写不敏感（RFC 7230），存的时候统一转小写 */
    std::unordered_map<std::string,std::string>::const_iterator it = header_.find("connection");
    if(it == header_.end()){
        return false;
    }
    std::string conn = it->second;
    for(size_t i = 0; i < conn.size(); ++i){
        if(conn[i] >= 'A' && conn[i] <= 'Z'){ conn[i] = conn[i] - 'A' + 'a'; }
    }
    return conn == "keep-alive" && version_ == "1.1";
}
bool HttpRequest::parse(Buffer& buff){//解析处理
    const char CRLF[] = "\r\n";//行结束符标志（回车换行）
    if(buff.ReadableBytes()<=0){
        return false;
    }
    //读取缓冲区里面的数据
    while(buff.ReadableBytes() && state_ != FINISH){
        //从buff读指针开始到读指针结束，并且去除最后的“\r\n"，返回指向一行的指针
        const char* lineEnd = search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF + 2);
        //转化为string类型
        std::string line(buff.Peek(),lineEnd);
        switch (state_)//有限状态机，按照解析行-》解析头部-》解析body来进行(状态之间会自行转入下一个状态)
        {
        case REQUEST_LINE:
            if(!ParseRequestLine_(line)){
                return false;
            }
            ParsePath_();//解析路径
            break;
        case HEADERS:
            ParseHeader_(line);
            if(buff.ReadableBytes() <= 2){//只有\r\n了，说明后面没有内容了
                state_ = FINISH;
            };
            break;
        case BODY:
            ParseBody_(line);
            break;
        default:
            break;
        }
        if(lineEnd == buff.BeginWrite()){break;}//读完了
        buff.RetrieveUntil(lineEnd + 2);//跳过回车换行
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}
//解析路径
void HttpRequest::ParsePath_(){
     // 如果是 /api/ 开头的，直接保留原样，不往后拼 .html
    if (path_.rfind("/api/", 0) == 0) {
        return; 
    }
    /* 页面/静态请求：先剥掉 ?query 再做路由匹配。
     * 否则 "/login.html?t=1750000000" 匹配不上 DEFAULT_HTML_TAG，
     * 登录/注册的 POST 根本不会进入 UserVerify，最后 stat 失败返回 404 */
    size_t qPos = path_.find('?');
    if (qPos != std::string::npos) {
        path_ = path_.substr(0, qPos);
    }
    if(path_ == "/"){
        path_ = "/index.html";
    }
    else{
        for(auto &item:DEFAULT_HTML){//依次匹配预设好的网址
            if(item == path_){
                path_ += ".html";
                break;
            }
        }
    }
}
bool HttpRequest::ParseRequestLine_(const string& line){//解析请求行
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");//孩子们，是正则表达式
    smatch subMatch;//存储的是正则表达式的结果，实际上是一个数组，里面存的是一个个捕获组的值
    if(regex_match(line,subMatch,patten)){
        method_ = subMatch[1];//请求的方法GET或POST等
        path_ = subMatch[2];//url路径
        version_ = subMatch[3];//http版本
        state_ = HEADERS;//状态转换
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}
void HttpRequest::ParseHeader_(const string& line){
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line,subMatch,patten)){
        /* 头名统一转小写存储，避免 "Content-Type" / "content-type" 两种写法
         * 命中不了同一份数据（Chrome fetch 发的是小写） */
        std::string key = subMatch[1];
        for(size_t i = 0; i < key.size(); ++i){
            if(key[i] >= 'A' && key[i] <= 'Z'){ key[i] = key[i] - 'A' + 'a'; }
        }
        header_[key] = subMatch[2];//这里line举个例子：password:123456，subMatch[1] = password,subMatch[2] = 123456，know？
    }
    else{
        state_ = BODY;
    }
}
void HttpRequest::ParseBody_(const string& line){
    body_ = line;
    ParsePost_();//处理post请求
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}
//16进制转化为10进制
int HttpRequest::ConVerHex(char ch){
    if(ch >= '0' && ch <= '9') return ch - '0';
    if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return -1;      // 非法字符，调用方据此放弃解码
}
//处理post请求
void HttpRequest::ParsePost_(){
    /* 头名已统一小写；Content-Type 可能带 "; charset=UTF-8" 后缀，按前缀判断 */
    std::unordered_map<std::string,std::string>::const_iterator ct = header_.find("content-type");
    bool isForm = (ct != header_.end())
               && ct->second.rfind("application/x-www-form-urlencoded", 0) == 0;

    if(method_ == "POST" && isForm){//头部内容为表单格式
        ParseFromUrlencoded_();//POST请求体示例 （从url中解析编码
        if(DEFAULT_HTML_TAG.count(path_)){//如果是登陆注册的path
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d",tag);
            if(tag == 0 || tag == 1){
                bool isLogin = (tag == 1);//为1是登陆
                if(UserVerify(post_["username"],post_["password"],isLogin)){
                    path_ = "/welcome.html";
                }
                else{
                    path_ = "/error.html";
                }
            }
        }
        return;
    }

    /* POST 到登录/注册页却不是表单格式：明确判失败。
     * 否则 path_ 保持原样，会以 200 返回登录页本身，
     * 前端看到 200 就当成登录成功——等于任何密码都能进 */
    if(method_ == "POST" && DEFAULT_HTML_TAG.count(path_)){
        LOG_WARN("login/register POST without form content-type, rejected");
        path_ = "/error.html";
    }
}
//从url中解析编码
void HttpRequest::ParseFromUrlencoded_(){
    if(body_.size() == 0){return;}

    /* 第一步：先把 %XX 和 + 解码到一个独立字符串。
     * 原实现在 body_ 上原地改下标，既会破坏后续 key/value 的偏移，
     * 解码算法本身也是错的（%40 会被算成 "64" 而不是 "@"）。
     * 拆成「先解码、再切分」两步，逻辑才站得住。 */
    std::string decoded;
    decoded.reserve(body_.size());
    for(size_t i = 0; i < body_.size(); ++i){
        char ch = body_[i];
        if(ch == '+'){
            decoded += ' ';
        }
        else if(ch == '%' && i + 2 < body_.size()){
            int hi = ConVerHex(body_[i+1]);
            int lo = ConVerHex(body_[i+2]);
            if(hi >= 0 && lo >= 0){
                decoded += static_cast<char>(hi * 16 + lo);
                i += 2;
            }
            else{
                decoded += ch;      // 非法的 %XX，原样保留
            }
        }
        else{
            decoded += ch;
        }
    }

    /* 第二步：按 & 切键值对，再按第一个 = 切 key/value */
    size_t start = 0;
    while(start <= decoded.size()){
        size_t ampPos = decoded.find('&', start);
        std::string pair = (ampPos == std::string::npos)
                         ? decoded.substr(start)
                         : decoded.substr(start, ampPos - start);
        size_t eqPos = pair.find('=');
        if(eqPos != std::string::npos && eqPos > 0){
            std::string key = pair.substr(0, eqPos);
            std::string value = pair.substr(eqPos + 1);
            post_[key] = value;
            LOG_DEBUG("%s = %s",key.c_str(),value.c_str());
        }
        if(ampPos == std::string::npos) break;
        start = ampPos + 1;
    }
}
bool HttpRequest::UserVerify(const string& name,const string& pwd,bool isLogin){
    if(name.empty() || pwd.empty()){ return false; }

    sqlite3* db = nullptr;
    if(sqlite3_open("webserver.db", &db) != SQLITE_OK){
        LOG_ERROR("SQLite open failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }
    /* 线程池多线程同时 INSERT 时会撞 SQLITE_BUSY，让 SQLite 自己等一会儿 */
    sqlite3_busy_timeout(db, 3000);

    /* 首次运行、或从别的目录启动时，webserver.db 可能不存在/是空库。
     * 不建表的话后面 SELECT 会报 "no such table: user"，
     * 登录和注册会一起失败——这就是"换目录跑就登不上"的根因。 */
    const char* initSql =
        "CREATE TABLE IF NOT EXISTS user ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT NOT NULL UNIQUE, "
        "password TEXT NOT NULL)";
    char* initErr = nullptr;
    if(sqlite3_exec(db, initSql, nullptr, nullptr, &initErr) != SQLITE_OK){
        LOG_ERROR("init table failed: %s", initErr ? initErr : "unknown");
        sqlite3_free(initErr);
        sqlite3_close(db);
        return false;
    }

    bool flag = false;
    sqlite3_stmt* stmt = nullptr;

    /* ---------- 1. 查这个用户 ---------- */
    const char* selSql = "SELECT password FROM user WHERE username = ? LIMIT 1";
    if(sqlite3_prepare_v2(db, selSql, -1, &stmt, nullptr) != SQLITE_OK){
        LOG_ERROR("prepare SELECT failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }
    /* 参数绑定，杜绝 SQL 注入；SQLITE_TRANSIENT 让 SQLite 自己拷贝一份 */
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);

    if(rc == SQLITE_ROW){
        /* 用户已存在 */
        const unsigned char* dbPwd = sqlite3_column_text(stmt, 0);
        string password = dbPwd ? (const char*)dbPwd : "";
        if(isLogin){
            flag = (pwd == password);
            if(!flag){ LOG_INFO("password error!"); }
        }else{
            flag = false;
            LOG_INFO("user already exists!");
        }
        sqlite3_finalize(stmt);
    }
    else if(rc == SQLITE_DONE){
        /* 用户不存在 */
        sqlite3_finalize(stmt);
        stmt = nullptr;

        if(isLogin){
            flag = false;
            LOG_INFO("user not found!");
        }else{
            const char* insSql = "INSERT INTO user(username,password) VALUES(?,?)";
            if(sqlite3_prepare_v2(db, insSql, -1, &stmt, nullptr) != SQLITE_OK){
                LOG_ERROR("prepare INSERT failed: %s", sqlite3_errmsg(db));
                sqlite3_close(db);
                return false;
            }
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pwd.c_str(), -1, SQLITE_TRANSIENT);

            if(sqlite3_step(stmt) == SQLITE_DONE){
                flag = true;
                LOG_INFO("register success!");
            }else{
                LOG_ERROR("INSERT failed: %s", sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt);
        }
    }
    else{
        LOG_ERROR("step error: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return flag;
}

std::string HttpRequest::path() const{
    return path_;
}
std:: string& HttpRequest::path(){
    return path_;
}
std::string HttpRequest::method() const{
    return method_;
}
std::string HttpRequest::version() const{
    return version_;
}
std::string HttpRequest::GetPost(const std::string& key) const{
    assert(key != "");
    if(post_.count(key) == 1){
        return post_.find(key)->second;
    }
    return "";
}
std::string HttpRequest::GetPost(const char* key) const{
    assert(key != nullptr);
    if(post_.count(key) == 1){
        return post_.find(key)->second;
    }
    return "";
}
//从浏览器的请求网址（URL）里，把参数给“抠”出来。
std::string HttpRequest::GetQuery(const std::string& key) const {
    size_t pos = path_.find('?');
    if (pos == std::string::npos) return "";
    std::string queryStr = path_.substr(pos + 1);
    size_t start = 0;
    while (start < queryStr.size()) {
        size_t ampPos = queryStr.find('&', start);
        std::string pair = (ampPos == std::string::npos) ? queryStr.substr(start) : queryStr.substr(start, ampPos - start);
        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string k = pair.substr(0, eqPos);
            std::string v = pair.substr(eqPos + 1);
            if (k == key) return v;
        }
        if (ampPos == std::string::npos) break;
        start = ampPos + 1;
    }
    return "";
}