#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include<unordered_map>
#include<unordered_set>
#include<string>
#include<regex>//正则表达式
#include<errno.h>
#include<sqlite3.h>
#include"buffer.h"
#include"log.h"
class HttpRequest{
public:
    enum PARSE_STATE{//状态机的状态
            REQUEST_LINE,
            HEADERS,
            BODY,
            FINISH,
    };
    HttpRequest(){Init();}
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);
    
    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

private:
    bool ParseRequestLine_(const std::string& line);//处理请求行
    void ParseHeader_(const std::string& line);//处理请求头
    void ParseBody_(const std::string& line);//处理请求体

    void ParsePath_();//处理请求路径
    void ParsePost_();//处理Post事件
    void ParseFromUrlencoded_();//从url中解析编码

    static bool UserVerify(const std::string& name,const std::string& pwd,bool isLogin);//用户验证

    PARSE_STATE state_;
    std::string method_,path_,version_,body_;
    std::unordered_map<std::string,std::string> header_;
    std::unordered_map<std::string,std::string> post_;

    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string,int> DEFAULT_HTML_TAG;
    static int ConVerHex(char ch);//16进制转换为10进制
    // 判断是否是 API 请求（以 /api/ 开头）
    bool IsApiRequest() const { return path_.rfind("/api/", 0) == 0; }
    // 解析 URL 里面的参数，比如 /api/gpio?action=on 里面的 action 就是 on
    std::string GetQuery(const std::string& key) const;
};
#endif // !HTTP_REQUEST_H
