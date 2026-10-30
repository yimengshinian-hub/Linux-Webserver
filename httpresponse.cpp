/* ============================================================
 * httpresponse.cpp  ——  修复版
 *
 * 相对原版的改动（共 6 处，均已在下方用 [FIX-n] 标注）：
 *  [FIX-1] AddContent_ 里 mmap 返回值判断错误：原写法 int* mmRet 且 *mmRet==-1
 *          会解引用 MAP_FAILED 导致段错误；改为 void* + MAP_FAILED 比较，
 *          并补上失败时 close(srcFd)。
 *  [FIX-2] 空文件保护：st_size == 0 时 mmap(0,0,...) 必定失败，
 *          直接返回 Content-length: 0，避免整台服务器崩掉。
 *  [FIX-3] AddStateLine_ 少了空格，原输出 "HTTP/1.1200 OK"，
 *          改为 "HTTP/1.1 200 OK"。
 *  [FIX-4] /api/gpio 业务失败时返回 500 而不是 200，客户端不用再猜响应体。
 *  [FIX-5] JSON 响应补 charset=utf-8 和 Cache-Control: no-store。
 *  [FIX-6] action 参数先校验再开文件；写文件失败会真正检测出来。
 * ============================================================ */

#include "httpresponse.h"
#include "sysmonitor.h"     // SysMonitor::ToJson()
#include <fstream>
#include <string>
#include <unordered_map>

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
    { ".css",   "text/css" },
    { ".js",    "text/javascript" },
};

const unordered_map<int, string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 500, "Internal Server Error" },
};

const unordered_map<int, string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 401, "/error.html" },
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
    /* ---------- API 请求：走自己的 JSON 逻辑 ---------- */
    if (path_.rfind("/api/", 0) == 0) {
        unordered_map<string, string> query;

        size_t qPos = path_.find('?');
        if (qPos != string::npos) {
            string qStr = path_.substr(qPos + 1);
            size_t start = 0;
            while (start < qStr.size()) {
                size_t amp = qStr.find('&', start);
                string pair = (amp == string::npos) ? qStr.substr(start)
                                                    : qStr.substr(start, amp - start);
                size_t eq = pair.find('=');
                if (eq != string::npos && eq > 0) {
                    query[pair.substr(0, eq)] = pair.substr(eq + 1);
                }
                if (amp == string::npos) break;
                start = amp + 1;
            }
            path_ = path_.substr(0, qPos);   // 剥掉查询串，只留路径
        }

        MakeApiResponse_(buff, path_, query);
        return;
    }

    /* ---------- 静态资源 ---------- */
    if(stat((srcDir_ + path_).data(),&mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)){
        code_ = 404;
    }
    else if(!(mmFileStat_.st_mode & S_IROTH)){
        code_ = 403;
    }
    else if(code_ == -1){
        code_ = 200;
    }

    ErrorHtml_();
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

char* HttpResponse::File(){
    return mmFile_;
}

size_t HttpResponse::FileLen() const{
    return mmFileStat_.st_size;
}

void HttpResponse::ErrorHtml_(){
    if(CODE_PATH.count(code_) == 1){
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
    /* [FIX-3] "HTTP/1.1" 后面必须有一个空格 */
    buff.Append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::AddHeader_(Buffer& buff){
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

    /* [FIX-2] 空文件不能 mmap(0,0,...)，会返回 MAP_FAILED */
    if(mmFileStat_.st_size == 0){
        close(srcFd);
        buff.Append("Content-length: 0\r\n\r\n");
        return;
    }

    LOG_DEBUG("file path %s",(srcDir_ + path_).data());

    /* [FIX-1] 返回 void*，用 MAP_FAILED 判断，绝不 dereference */
    void* mmRet = mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    close(srcFd);                       // 映射建立后 fd 可以立刻关闭

    if(mmRet == MAP_FAILED){
        LOG_ERROR("mmap failed: %s", (srcDir_ + path_).data());
        ErrorContent(buff,"File NotFound!");
        return;
    }

    mmFile_ = (char*)mmRet;
    buff.Append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

void HttpResponse::UnmapFile(){
    if(mmFile_){
        munmap(mmFile_,mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

string HttpResponse::GetFileType_(){
    string::size_type idx = path_.find_last_of('.');
    if(idx == string::npos){
        return "text/plain";
    }
    string suffix = path_.substr(idx);
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

/* ============================================================
 *  API 具体处理
 * ============================================================ */
void HttpResponse::MakeApiResponse_(Buffer& buff, const std::string& path,
                                   const std::unordered_map<std::string, std::string>& query) {
    code_ = 200;
    std::string jsonBody;

    if (path == "/api/sysinfo") {
        /* 系统监控：CPU 主频 / 内存 / 运行时长，读不到的字段为 null */
        jsonBody = SysMonitor::ToJson();
    }
    else if (path == "/api/gpio") {
        /* [FIX-6] 先校验参数，非法直接 400，不必去碰硬件 */
        std::string action;
        std::unordered_map<std::string, std::string>::const_iterator it = query.find("action");
        if (it != query.end()) {
            action = it->second;
        }

        if (action != "on" && action != "off") {
            code_ = 400;
            jsonBody = "{\"status\":\"error\",\"msg\":\"invalid action, use ?action=on or ?action=off\"}";
        }
        else {
            bool writeOk = false;

            /* 必须 root 权限，否则 is_open() 直接失败 */
            std::ofstream ledFile("/sys/class/leds/red/brightness");
            if (ledFile.is_open()) {
                ledFile << (action == "on" ? "1" : "0");
                ledFile.close();
                writeOk = !ledFile.fail();      // close 会触发 flush，失败在这里体现
            }

            if (writeOk) {
                jsonBody = (action == "on")
                    ? "{\"status\":\"ok\",\"led\":\"on\"}"
                    : "{\"status\":\"ok\",\"led\":\"off\"}";
            }
            else {
                /* [FIX-4] 权限不足返回 500，而不是假装成功 */
                code_ = 500;
                jsonBody = "{\"status\":\"error\",\"msg\":\"permission denied, run with sudo\"}";
            }
        }
    }
    else {
        code_ = 404;
        jsonBody = "{\"status\":\"error\",\"msg\":\"api not found\"}";
    }

    /* 状态行按真实 code_ 取描述，不再写死 OK / Not Found */
    std::string statusText = "OK";
    if (CODE_STATUS.count(code_) == 1) {
        statusText = CODE_STATUS.find(code_)->second;
    }

    buff.Append("HTTP/1.1 " + std::to_string(code_) + " " + statusText + "\r\n");   // [FIX-3]
    buff.Append("Content-Type: application/json; charset=utf-8\r\n");               // [FIX-5]
    buff.Append("Content-Length: " + std::to_string(jsonBody.size()) + "\r\n");
    buff.Append("Cache-Control: no-store\r\n");                                     // [FIX-5]
    buff.Append("Access-Control-Allow-Origin: *\r\n");
    buff.Append("Connection: close\r\n\r\n");
    buff.Append(jsonBody);
}
