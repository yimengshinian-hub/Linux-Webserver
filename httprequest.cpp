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
    if(header_.count("Connection") == 1){
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
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
        header_[subMatch[1]] = subMatch[2];//这里line举个例子：password:123456，subMatch[1] = password,subMatch[2] = 123456，know？
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
    if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}
//处理post请求
void HttpRequest::ParsePost_(){
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded"){//头部内容为表单格式
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
    }
}
//从url中解析编码
void HttpRequest::ParseFromUrlencoded_(){
    if(body_.size() == 0){return;}
    string key,value;
    int num = 0;
    int n = body_.size();
    int i = 0,j=0;//解析下标，j在前，i在后,中间是要操作的
    for(;i<n;i++){
        char ch = body_[i];
        switch (ch)
        {
        case '=': //把等号前面的存入key
            key = body_.substr(j,i-j);
            j = i+1;
            break;
        case '+'://把+替换为空格
            body_[i] = ' ';
            break;
        case '%'://将16进制编码的字符转为10进制
            num = ConVerHex(body_[i+1] )* 16 + ConVerHex(body_[i+2]);
            body_[i+2] = num % 10 + '0';
            body_[i+1] = num / 10 + '0';
            i += 2;
            break;
        case '&'://健值对分隔符
            value = body_.substr(j,i-j);//把&前的复制给value
            j = i + 1;
            post_[key] = value;
            LOG_DEBUG("%s = %s",key.c_str(),value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if(post_.count(key) == 0 && j<i){
        value = body_.substr(j,i-j);
        post_[key] = value;
    }
}
bool HttpRequest::UserVerify(const string& name,const string& pwd,bool isLogin){//验证或注册用户
    if(name == "" || pwd == ""){return false;}
    LOG_INFO("Verify name:%s pwd:%s",name.c_str(),pwd.c_str());
    
    MYSQL* sql;
    SqlConnRAII(&sql,SqlConnPool::Instance());
    // assert(sql); //修改处 2：注释掉断言，改为下方更安全的 if 判断 

    //修改处 3：增加安全判断，防止连不上数据库时静默崩溃 
    if(sql == nullptr) {
        LOG_ERROR("MySQL Connection is NULL! Check your database status.");
        return false;
    }

    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };
    MYSQL_FIELD* fields = nullptr;
    MYSQL_RES* res = nullptr;

    if(!isLogin){flag = true;}
    //查询用户及密码
    snprintf(order,256,"SELECT username,password FROM user WHERE username ='%s' LIMIT 1",name.c_str());//数据库查询
    LOG_DEBUG("%s",order);

    if(mysql_query(sql,order)){
        // mysql_free_result(res); // 修改处 4：这里 res 还是空指针，释放会导致程序崩溃，必须删掉 
        // 修改处 5：增加打印 MySQL 底层报错信息
        LOG_ERROR("MySQL Query Error: %s", mysql_error(sql)); 
        return false;
    }
    
    res = mysql_store_result(sql);
    // === 修改处 6：增加结果集判断，防止空指针 ===
    if (res == nullptr) {
        LOG_ERROR("MySQL Store Result Error: %s", mysql_error(sql));
        return false;
    }

    j = mysql_num_fields(res);
    fields = mysql_fetch_fields(res);

    while(MYSQL_ROW row = mysql_fetch_row(res)){
        LOG_DEBUG("MYSQL ROW: %s %s",row[0],row[1]);
        string password(row[1]);
        //注册行为且用户名未使用
        if(isLogin){
            if(pwd == password){flag = true;}
            else{
                flag = false;
                LOG_INFO("pwd error!");
            }
        }
        else{
            flag = false;
            LOG_INFO("user used!");
        }
    }
    mysql_free_result(res);
    
    //注册行为且用户名未被使用
    if(!isLogin && flag == true){
        LOG_DEBUG("regirster!");
        bzero(order,256);
        snprintf(order,256,"INSERT INTO user(username,password) VALUES('%s','%s')",name.c_str(),pwd.c_str());
        LOG_DEBUG("%s",order);
        if(mysql_query(sql,order)){
            // LOG_DEBUG("Insert error!"); // 修改处 7：改成 ERROR 级别并打印底层原因 
            LOG_ERROR("Insert Error: %s", mysql_error(sql));
            flag = false;
        } else {
            flag = true; // 修改处 8：查询成功才设为 true，原代码写在了 if 外面会覆盖报错 
        }
    }

    // SqlConnPool::Instance()->FreeConn(sql); // 修改处 9：SqlConnRAII 会析构自动释放，手动释放会导致二次释放崩溃，必须删掉 

    LOG_DEBUG("UserVerify success!!");
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