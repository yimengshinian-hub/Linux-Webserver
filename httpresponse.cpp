#include"httpresponse.h"
using namespace std;

const unordered_map<string, string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

const unordered_map<int, string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" }, // === 新增 ===
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

const unordered_map<int, string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 401, "/error.html" }, // === 新增：复用现有的 error.html ===
    { 403, "/403.html" },
    { 404, "/404.html" },
};
HttpResponse::HttpResponse(){
    code_ = -1;
    path_ = srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr;
    mmFileStat_ = { 0 };
}
HttpResponse::~HttpResponse(){
    UnmapFile();
}
void HttpResponse::Init(const string& srcDir,string& path,bool isKeepAlive,int code){
    assert(srcDir != "");
    if(mmFile_){UnmapFile();}
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr;
    mmFileStat_ = { 0 };
}
void HttpResponse::MakeResponse(Buffer& buff){
    // 如果是 API 请求，走我们自己的 JSON 逻辑
    if (path_.rfind("/api/", 0) == 0) {
        std::unordered_map<std::string, std::string> query;
        size_t qPos = path_.find('?');
        if (qPos != std::string::npos) {
            std::string qStr = path_.substr(qPos + 1);
            size_t start = 0;
            while (start < qStr.size()) {
                size_t amp = qStr.find('&', start);
                std::string pair = (amp == std::string::npos) ? qStr.substr(start) : qStr.substr(start, amp - start);
                size_t eq = pair.find('=');
                if (eq != std::string::npos) {
                    query[pair.substr(0, eq)] = pair.substr(eq + 1);
                }
                if (amp == std::string::npos) break;
                start = amp + 1;
            }
        }
        path_ = path_.substr(0, qPos);
        MakeApiResponse_(buff, path_, query);
        return;
    }
    //判断请求的资源文件,stat是将资源文件的状态码放入mmFileStat_中,<0代表stat失败
    if(stat((srcDir_ + path_).data(),&mmFileStat_) < 0 ||  S_ISDIR(mmFileStat_.st_mode)){//S_ISDIR判断是否是一个目录
        code_ = 404;
    }
    else if(!(mmFileStat_.st_mode & S_IROTH)){//该文件其他人只有读权限
        code_ = 403;
    }
    else if(code_ == -1){
        code_ = 200;//OK!
    }
    ErrorHtml_();
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}
char* HttpResponse::File(){//返回的是映射文件地址
    return mmFile_;
}
size_t  HttpResponse::FileLen() const{
    return mmFileStat_.st_size;//映射文件大小
}
void HttpResponse::ErrorHtml_(){//根据错误码（如 404、403）设置对应的错误页面路径，并更新文件状态
    if(CODE_PATH.count(code_) == 1){//判断该键是否存在于CODE_PATH中，1代表存在
        path_ = CODE_PATH.find(code_)->second;
        stat((srcDir_ + path_).data(),&mmFileStat_);
    }
}
void HttpResponse::AddStateLine_(Buffer& buff){
    string status;
    if(CODE_STATUS.count(code_) == 1){
        status = CODE_STATUS.find(code_)->second;
    }
    else{
        code_ = 400;
        status = CODE_STATUS.find(400)->second;
    }
    buff.Append("HTTP/1.1" + to_string(code_) + " " + status + "\r\n");//向缓冲区添加状态行
}
void HttpResponse::AddHeader_(Buffer& buff){
    // 新增：告诉浏览器不要缓存，每次都要向服务器请求最新的 
    buff.Append("Cache-Control: no-store\r\n"); 
    buff.Append("Connection: ");
    if(isKeepAlive_){
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6,timeout=120\r\n");
    }
    else{
        buff.Append("close\r\n");
    }
    buff.Append("Content-type: " + GetFileType_() + "\r\n");
}
void HttpResponse::AddContent_(Buffer& buff){
    int srcFd = open((srcDir_ + path_).data(),O_RDONLY);
    if(srcFd < 0){
        ErrorContent(buff,"File NotFound!");
        return;
    }
    //将文件映射到内存提高文件的访问速度，MAP_PRIVATE建立一个写入时拷贝的私有映射
    LOG_DEBUG("file path %s",(srcDir_ + path_).data());
    int* mmRet = (int*)mmap(0,mmFileStat_.st_size,PROT_READ,MAP_PRIVATE,srcFd,0);
    if(*mmRet == -1){
        ErrorContent(buff,"File NotFound!");
        return;
    }
    mmFile_ = (char*)mmRet;
    close(srcFd);
    buff.Append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
}
void HttpResponse::UnmapFile(){//取消文件的内存映射，释放资源。
    if(mmFile_){
        munmap(mmFile_,mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}
string HttpResponse::GetFileType_(){
    //判断文件类型
    //这个函数的作用是根据文件路径 path_ 的后缀名（如 .html、.jpg）返回对应的 MIME 类型（如 text/html、image/jpeg）。如果后缀名未知或文件没有后缀，则返回默认的 "text/plain"。
    string::size_type idx = path_.find_last_of('.');//用于存储 path_ 中 最后一个 . 的位置
    if(idx == string::npos){//没有.
        return "text/plain";
    }
    string suffix = path_.substr(idx);//存储从 idx 开始到字符串末尾的 文件后缀（如 .html或.jpg）
    if(SUFFIX_TYPE.count(suffix) == 1){
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}
void HttpResponse::ErrorContent(Buffer& buff,string message){
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(code_) == 1){
        status = CODE_STATUS.find(code_)->second;
    }
    else{
        status = "Bad Request";
    }
    body += to_string(code_) + " : " + status + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}
//API具体的处理逻辑
void HttpResponse::MakeApiResponse_(Buffer& buff, const std::string& path, const std::unordered_map<std::string, std::string>& query) {
    code_ = 200;
    std::string jsonBody;

    if (path == "/api/sysinfo") {
        // 系统监控数据：调用 sysmonitor 模块获取
        jsonBody = SysMonitor::ToJson();
    } else if (path == "/api/gpio") {
        int pin = 48; // 假设引脚48，板子上再改
        std::string action = "";
        if (query.count("action")) action = query.at("action");

        if (action == "on") {
            GpioCtl::Init(pin);
            bool ok = GpioCtl::SetLed(pin, true);
            jsonBody = ok ? "{\"status\":\"ok\",\"led\":\"on\"}" : "{\"status\":\"error\",\"msg\":\"gpio write failed\"}";
        } else if (action == "off") {
            bool ok = GpioCtl::SetLed(pin, false);
            jsonBody = ok ? "{\"status\":\"ok\",\"led\":\"off\"}" : "{\"status\":\"error\",\"msg\":\"gpio write failed\"}";
        } else {
            jsonBody = "{\"status\":\"error\",\"msg\":\"invalid action\"}";
        }
    } else {
        code_ = 404;
        jsonBody = "{\"status\":\"error\",\"msg\":\"api not found\"}";
    }

    // 拼装 HTTP 响应头与 JSON 实体
    std::string status = (code_ == 200) ? "OK" : "Not Found";
    buff.Append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
    buff.Append("Content-Type: application/json\r\n");
    buff.Append("Content-Length: " + std::to_string(jsonBody.size()) + "\r\n");
    buff.Append("Access-Control-Allow-Origin: *\r\n"); // 允许跨域
    buff.Append("Connection: close\r\n\r\n");
    buff.Append(jsonBody);
}