#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H
#include<unordered_map>
#include<fcntl.h> //open
#include<unistd.h> //close
#include<sys/stat.h> //stat,用于检查文件的状态
#include<sys/mman.h> //用于内存映射
#include"buffer.h"
#include"log.h"

class HttpResponse{
public:
    HttpResponse();
    ~HttpResponse();//调用UnmapFile释放映射资源

    void Init(const std::string& srcDir,std::string& path,bool isKeepAlive = false,int code = -1);
    void MakeResponse(Buffer& buff);
    void UnmapFile();
    char* File();
    size_t FileLen() const;
    void ErrorContent(Buffer& buff,std::string message);
    int Code() const {return code_;}
private:
    void AddStateLine_(Buffer& buff);//状态行
    void AddHeader_(Buffer& buff);//头部
    void AddContent_(Buffer& buff);//请求内容

    void ErrorHtml_();
    std::string GetFileType_();
    
    int code_;//状态码
    bool isKeepAlive_;//是否保持连接
    std::string path_;//请求路径
    std::string srcDir_;//资源目录
    char* mmFile_;
    struct stat mmFileStat_;

    static const std::unordered_map<std::string,std::string> SUFFIX_TYPE;//后缀类型集
    static const std::unordered_map<int,std::string> CODE_STATUS;//编码状态集,如404 - NOT FOUNT
    static const std::unordered_map<int,std::string> CODE_PATH;//编码路径集
};
#endif // !HTTP_RESPONSE_H