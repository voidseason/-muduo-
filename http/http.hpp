#include "../server.hpp"
#include <vector>
#include <sstream>
#include <functional>
#include <cstdio>
#include <cctype>
#include <sys/types.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <memory>
#include <cassert>
#include <fstream>
#include <iostream>
#include <regex>

#define DEFALT_TIMEOUT 10


std::unordered_map<int, std::string> _statu_msg = {
    {100,  "Continue"},
    {101,  "Switching Protocol"},
    {102,  "Processing"},
    {103,  "Early Hints"},
    {200,  "OK"},
    {201,  "Created"},
    {202,  "Accepted"},
    {203,  "Non-Authoritative Information"},
    {204,  "No Content"},
    {205,  "Reset Content"},
    {206,  "Partial Content"},
    {207,  "Multi-Status"},
    {208,  "Already Reported"},
    {226,  "IM Used"},
    {300,  "Multiple Choice"},
    {301,  "Moved Permanently"},
    {302,  "Found"},
    {303,  "See Other"},
    {304,  "Not Modified"},
    {305,  "Use Proxy"},
    {306,  "unused"},
    {307,  "Temporary Redirect"},
    {308,  "Permanent Redirect"},
    {400,  "Bad Request"},
    {401,  "Unauthorized"},
    {402,  "Payment Required"},
    {403,  "Forbidden"},
    {404,  "Not Found"},
    {405,  "Method Not Allowed"},
    {406,  "Not Acceptable"},
    {407,  "Proxy Authentication Required"},
    {408,  "Request Timeout"},
    {409,  "Conflict"},
    {410,  "Gone"},
    {411,  "Length Required"},
    {412,  "Precondition Failed"},
    {413,  "Payload Too Large"},
    {414,  "URI Too Long"},
    {415,  "Unsupported Media Type"},
    {416,  "Range Not Satisfiable"},
    {417,  "Expectation Failed"},
    {418,  "I'm a teapot"},
    {421,  "Misdirected Request"},
    {422,  "Unprocessable Entity"},
    {423,  "Locked"},
    {424,  "Failed Dependency"},
    {425,  "Too Early"},
    {426,  "Upgrade Required"},
    {428,  "Precondition Required"},
    {429,  "Too Many Requests"},
    {431,  "Request Header Fields Too Large"},
    {451,  "Unavailable For Legal Reasons"},
    {501,  "Not Implemented"},
    {502,  "Bad Gateway"},
    {503,  "Service Unavailable"},
    {504,  "Gateway Timeout"},
    {505,  "HTTP Version Not Supported"},
    {506,  "Variant Also Negotiates"},
    {507,  "Insufficient Storage"},
    {508,  "Loop Detected"},
    {510,  "Not Extended"},
    {511,  "Network Authentication Required"}
};
std::unordered_map<std::string, std::string> _mime_msg = {
    {".aac",        "audio/aac"},
    {".abw",        "application/x-abiword"},
    {".arc",        "application/x-freearc"},
    {".avi",        "video/x-msvideo"},
    {".azw",        "application/vnd.amazon.ebook"},
    {".bin",        "application/octet-stream"},
    {".bmp",        "image/bmp"},
    {".bz",         "application/x-bzip"},
    {".bz2",        "application/x-bzip2"},
    {".csh",        "application/x-csh"},
    {".css",        "text/css"},
    {".csv",        "text/csv"},
    {".doc",        "application/msword"},
    {".docx",       "application/vnd.openxmlformatsofficedocument.wordprocessingml.document"},
    {".eot",        "application/vnd.ms-fontobject"},
    {".epub",       "application/epub+zip"},
    {".gif",        "image/gif"},
    {".htm",        "text/html"},
    {".html",       "text/html"},
    {".ico",        "image/vnd.microsoft.icon"},
    {".ics",        "text/calendar"},
    {".jar",        "application/java-archive"},
    {".jpeg",       "image/jpeg"},
    {".jpg",        "image/jpeg"},
    {".js",         "text/javascript"},
    {".json",       "application/json"},
    {".jsonld",     "application/ld+json"},
    {".mid",        "audio/midi"},
    {".midi",       "audio/x-midi"},
    {".mjs",        "text/javascript"},
    {".mp3",        "audio/mpeg"},
    {".mpeg",       "video/mpeg"},
    {".mpkg",       "application/vnd.apple.installer+xml"},
    {".odp",        "application/vnd.oasis.opendocument.presentation"},
    {".ods",        "application/vnd.oasis.opendocument.spreadsheet"},
    {".odt",        "application/vnd.oasis.opendocument.text"},
    {".oga",        "audio/ogg"},
    {".ogv",        "video/ogg"},
    {".ogx",        "application/ogg"},
    {".otf",        "font/otf"},
    {".png",        "image/png"},
    {".pdf",        "application/pdf"},
    {".ppt",        "application/vnd.ms-powerpoint"},
    {".pptx",       "application/vnd.openxmlformatsofficedocument.presentationml.presentation"},
    {".rar",        "application/x-rar-compressed"},
    {".rtf",        "application/rtf"},
    {".sh",         "application/x-sh"},
    {".svg",        "image/svg+xml"},
    {".swf",        "application/x-shockwave-flash"},
    {".tar",        "application/x-tar"},
    {".tif",        "image/tiff"},
    {".tiff",       "image/tiff"},
    {".ttf",        "font/ttf"},
    {".txt",        "text/plain"},
    {".vsd",        "application/vnd.visio"},
    {".wav",        "audio/wav"},
    {".weba",       "audio/webm"},
    {".webm",       "video/webm"},
    {".webp",       "image/webp"},
    {".woff",       "font/woff"},
    {".woff2",      "font/woff2"},
    {".xhtml",      "application/xhtml+xml"},
    {".xls",        "application/vnd.ms-excel"},
    {".xlsx",       "application/vnd.openxmlformatsofficedocument.spreadsheetml.sheet"},
    {".xml",        "application/xml"},
    {".xul",        "application/vnd.mozilla.xul+xml"},
    {".zip",        "application/zip"},
    {".3gp",        "video/3gpp"},
    {".3g2",        "video/3gpp2"},
    {".7z",         "application/x-7z-compressed"}
};

class Util
{
    public:
        Util();
        ~Util();
        static size_t Split(const std::string &src,const std::string &sep,std::vector<std::string> *arry)
        {
            size_t pos = 0,offset=0;
            while(offset < src.size())
            {
                pos = src.find(sep,offset);
                if(pos==std::string::npos)
                {
                    if(pos==src.size())
                    arry->push_back(src.substr(offset));
                    return arry->size();
                }
                if(pos==offset)
                {
                    offset = pos +sep.size();
                    continue;
                }
                arry->push_back(src.substr(offset,pos-offset));
                offset = pos+sep.size();
            }
            return arry->size();
        }
        //读取文件内容，将读取的内容放在一个Buffer中
        static bool ReadFile(const std::string &fiilename,std::string *buf)
        {
            std::ifstream ifs(fiilename,std::ios::binary);
            if(ifs.is_open()==false)
            {
                LOG(ERROR)<<"open "<<fiilename.c_str()<<" file failed!"<<std::endl;
            }
            size_t fsize = 0;
            ifs.seekg(0,ifs.end);//跳转读写位置到末尾
            fsize = ifs.tellg();//获取当前读写位置相对于起始位置的偏移量，从文件末尾偏移刚好就是文件大小
            ifs.seekg(0,ifs.beg);//跳转到起始位置
            std::string str;
            buf->resize(fsize);
            ifs.read(&(*buf)[0],fsize);
            if(ifs.good()==false)
            {
                LOG(ERROR)<<"read "<<str<<" file failed!"<<std::endl;
                ifs.close();
                return false;
            }
            ifs.close();
            return true;
        }
        static bool WriteFile(const std::string &filename,const std::string &buf)
        {
            std::ofstream ofs(filename,std::ios::binary | std::ios::trunc);
            if(ofs.is_open()==false)
            {
                LOG(ERROR)<<"OPEN " << filename<<"FILE FAILED!"<<std::endl;
                return false;
            }
            ofs.write(buf.c_str(),buf.size());
            if(ofs.good()==false)
            {
                LOG(ERROR)<<"WRITE "<<filename <<" FILE FAILED!"<<std::endl;
                ofs.close();
                return false;
            }
            ofs.close();
            return true;
        }
        //URL编码，避免URL中资源路径与查询字符中的特殊字符与HTTP请求中特殊字符产生歧义
        //编码格式：将特殊格式的ascii值，转换为两个16进制字符，前缀%， 例如：c++ -> c%2B%2B
        //不编码的特殊字符：RFC3986文档规定： . - _ ~ 字母，数字属于绝对不编码字符
        //RFC3986文档规定，编码格式 %HH
        //W3C标准中规定，查询字符串中的空格，需要被编码成 +  解码则是 +转空格
        static std::string Urlcode(const std::string &url,bool convert_space_to_plus)
        {
            std::string res;
            for(auto &c:url)
            {
                if(c=='.'||c=='-'||c=='_'||c=='~'||isalpha(c)||isdigit(c))
                {
                    res+=c;
                    continue;
                }
                if(c==' '&&convert_space_to_plus==true)
                {
                    res+='+';
                    continue;
                }
                //以下的字符都要编码为%HH格式
                char tmp[4]={0};
                snprintf(tmp,4, "%%%02X",c);
                res+=tmp;
            }
            return res;
        }
        static char HEXTOI(char c)
        {
            if(isdigit(c))
            {
                return c-'0';
            }
            else if(islower(c))
            {
                return c-'a'+10;
            }
            else if(isupper(c))
            {
                return c-'A'+10;
            }
            return -1;
        }
        static std::string UrlDecode(const std::string url ,bool convert_plus_to_space)
        {
            std::string res;
            //遇到了%，则将紧随其后的2个字符，转换为数字，然后按ascii值
            for(int i=0;i<url.size();i++)
            {
                if(url[i]=='+'&& convert_plus_to_space==true)
                {
                    res+=' ';
                }
                if(url[i]=='%'&&(i+2)<url.size())
                {
                    char v1 = HEXTOI(url[i+1]);
                    char v2 = HEXTOI(url[i+2]);
                    char v = (v1<<4)+v2;
                    res += v;
                    i+=2;
                    continue;
                }
                res+=url[i];
            }
            return res;
        }
        static std::string StatuDesc(int statu)
        {
            auto it = _statu_msg.find(statu);
            if(it!=_statu_msg.end())
            {
                return it->second;
            }
            return "Unknow";
        }
        //根据文件后缀名获取文件mime
        static std::string ExtMime(const std::string &filename)
        {
            size_t pos = filename.find_last_of('.');
            if(pos==std::string::npos)
            {
                return "application/octet-stream";
            }
            std::string ext = filename.substr(pos);
            auto it = _mime_msg.find(ext);
            if(it==_mime_msg.end())
            {
                return "application/octet-stream";
            }
            return it->second;
        }
        //判断一个文件是否是一个目录
        static bool IsDirectory(const std::string &filename)
        {
            struct stat st;
            int ret = stat(filename.c_str(),&st);
            if(ret<0)
            {
                return false;
            }
            return S_ISDIR(st.st_mode);
        }
        //判断一个文件是否是一个普通文件
        static bool IsRegular(const std::string &filename)
        {
            struct stat st;
            int ret = stat(filename.c_str(),&st);
            if(ret<0)
            {
                return false;
            }
            return S_ISREG(st.st_mode);
        }
        //http请求的资源路径有效性判断
        static bool ValidPath(const std::string &path)
        {
            std::vector<std::string> subdir;
            Split(path,"/",&subdir);
            int level = 0;
            for(auto &dir:subdir)
            {
                if(dir=="..")
                {
                    level--;
                    if(level<0)
                    {
                        return false;
                    }
                }
                level++;
            }
            return true;
        }
    private:
        
};



class HttpRequest
{
    public:
    std::string _method;//请求方法
    std::string _path;//请求路径
    std::string _version;//http版本
    std::string _body;//请求体
    std::smatch _matches;//匹配结果
    std::unordered_map<std::string,std::string> _headers;//请求头
    std::unordered_map<std::string,std::string> _params;//查询字符串

    public:
    HttpRequest():_version("HTTP/1.1"){}
    //重置
    void Reset()
    {
        _method.clear();
        _path.clear();
        _version = "HTTP/1.1";
        _body.clear();
        std::smatch matches;
        _matches.swap(matches);
        _headers.clear();
        _params.clear();
    }
    //插入请求头字段
    void SetHeader(const std::string &key,const std::string &val)
    {
        _headers[key] = val;
    }
    //判断请求头是否存在
    bool HasHeader(const std::string &key) const
    {
        return _headers.find(key)!=_headers.end();
    }
    //获取请求头字段值
    std::string GetHeader(const std::string &key) const
    {
        auto it = _headers.find(key);
        if(it!=_headers.end())
        {
            return it->second;
        }
        return "";
    }
    //插入查询字符串字段
    void SetParam(const std::string &key,const std::string &val)
    {
        _params.insert(std::make_pair(key,val));
    }
    //判断查询字符串是否存在
    bool HasParam(const std::string &key) const
    {
        return _params.find(key)!=_params.end();
    }
    //获取查询字符串字段
    std::string GetParam(const std::string &key) const
    {
        auto it = _params.find(key);
        if(it!=_params.end())
        {
            return it->second;  
        }
        return "";
    }
    //返回正文长度
    size_t ContentLength() const
    {
        bool ret = HasHeader("Content-Length");
        if(ret)
        {
            return std::stoi(GetHeader("Content-Length"));
        }
        return 0;
    }
    //判断是否是短链接
    bool Close() const
    {
        if(HasHeader("Connection")==true&&GetHeader("Connection")=="keep-alive")
        {
            return false;
        }
        return true;
    }
};


class HttpResponse
{
    public:
    int _status;//状态码
    std::string _body;//响应体
    bool _redirect_flag;//是否重定向
    std::string _redirect_url;//重定向路径
    std::unordered_map<std::string,std::string> _headers;//响应头

    public:
    HttpResponse():_status(200),_redirect_flag(false){}
    HttpResponse(int status):_status(status),_redirect_flag(false){}
    //重置
    void Reset()
    {
        _status = 200;
        _body.clear();
        _redirect_flag = false;
        _redirect_url.clear();
        _headers.clear();
    }
    //设置头部字段
    void SetHeader(const std::string &key,const std::string &val)
    {
        _headers.insert(std::make_pair(key,val));
    }
    //判断头部字段是否存在
    bool HasHeader(const std::string &key)
    {
        return _headers.find(key)!=_headers.end();
    }
    //获取头部字段值
    std::string GetHeader(const std::string &key)
    {
        auto it = _headers.find(key);
        if(it!=_headers.end())
        {
            return it->second;
        }
        return "";
    }
    //设置响应体
    void SetContent(const std::string &content,const std::string &type = "text/html")
    {
        _body = content;
        _headers["Content-Type"] = type;
    }
    //设置重定向
    void SetRedirect(const std::string &url,int status = 302)
    {
        _status = status;
        _redirect_flag = true;
        _redirect_url = url;
    }
    //判断是否是短链接
    bool Close()
    {
        if(HasHeader("Connection")==true&&GetHeader("Connection")=="keep-alive")
        {
            return false;
        }
        return true;
    }
};


typedef enum
{
    RECV_HTTP_ERRO,
    RECV_HTTP_LINE,
    RECV_HTTP_HEAD,
    RECV_HTTP_BODY,
    RECV_HTTP_END,
}HttpRecvStatu;
#define MAX_LINE 8192//最大一行数据长度

class HttpContext
{
    private:
    int _resp_statu;//响应状态码
    HttpRecvStatu _recv_statu;//当前接收及解析的阶段状态
    HttpRequest _request;//请求体
    private:
    //http请求行解析
    bool ParseHttpLine(const std::string &line)
    {

        std::smatch matches;
        std::regex e("(GET|HEAD|POST|PUT|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?", std::regex::icase);;//忽略大小写匹配
        bool ret = std::regex_match(line,matches,e);
        if(ret==false)
        {
            _resp_statu = RECV_HTTP_ERRO;
            _resp_statu = 400;//Bad Request
            return false;
        }

        _request._method = matches[1];
        std::transform(_request._method.begin(),_request._method.end(),_request._method.begin(),::toupper);
        _request._path = Util::UrlDecode(matches[2],false);
        _request._version = matches[4];
        std::vector<std::string> query_string_arry;
        std::string query_string = matches[3];
        Util::Split(query_string,"&",&query_string_arry);
        for(auto &param:query_string_arry)
        {
            size_t pos = param.find("=");
            if(pos==std::string::npos)
            {
                _recv_statu = RECV_HTTP_ERRO;
                _resp_statu = 400;//Bad Request
                return false;
            }
            std::string key = Util::UrlDecode(param.substr(0,pos),true);
            std::string val = Util::UrlDecode(param.substr(pos+1),true);//W3C标准中规定，查询字符串中的空格，需要被编码成 +  解码则是 +转空格
            _request.SetParam(key,val);
        }
        return true;
    }

    //http请求行获取（从Buffer中获取一行数据）
    bool RecvHttpLine(Buffer *buf)
    {
        if(_recv_statu!=RECV_HTTP_LINE) return false;
        //1.获取一行数据
        std::string line = buf->GetLineAndPop();
        //2.需要考虑的要素：缓冲区的数据不足一行，或者获取的数据超大
        if(line.size()==0)
        {
            if(buf->ReadAbleSize()>MAX_LINE)
            {
                _resp_statu = RECV_HTTP_ERRO;
                _resp_statu = 414;//URL Too Long
                return false;
            }
            //缓冲区中数据不足一行，也不多，就等新数据到来
            return true;
        }
        if(line.size()>MAX_LINE)
        {
            _resp_statu = RECV_HTTP_ERRO;
            _resp_statu = 414;//URL Too Long
            return false;
        }
        bool ret = ParseHttpLine(line);
        if(ret==false) return false;
        _recv_statu = RECV_HTTP_HEAD;
        return true;
    }

    //http请求头获取（从Buffer中获取一行数据）
    bool RecvHttpHeader(Buffer *buf)
    {
        if(_recv_statu!=RECV_HTTP_HEAD) return false;
        //一行一行换行，直到遇到空行为止
        while(1)
        {
            //1.获取一行数据
            std::string line = buf->GetLineAndPop();
            //2.需要考虑的要素：缓冲区的数据不足一行，或者获取的数据超大
            if(line.size()==0)
            {
                if(buf->ReadAbleSize()>MAX_LINE)
                {
                    _resp_statu = RECV_HTTP_ERRO;
                    _resp_statu = 414;//URL Too Long
                    return false;
                }
                //缓冲区中数据不足一行，也不多，就等新数据到来
                return true;
            }
            if(line.size()>MAX_LINE)
            {
                _resp_statu = RECV_HTTP_ERRO;
                _resp_statu = 414;//URL Too Long
                return false;
            }
            if(line=="\n"||line=="\r\n")
            {
                break;
            }
            bool ret = ParseHttpHeader(line);
            if(ret==false) return false;

        }
        //头部处理完毕，进入正文阶段
        _recv_statu = RECV_HTTP_BODY;
        return true;
    }
    //http请求头解析
    bool ParseHttpHeader(std::string &line)
    {
        if(line.back()=='\n')
        {
            line.pop_back();
        }
        if(line.back()=='\r')
        {
            line.pop_back();
        }
        size_t  pos = line.find(": ");
        if(pos==std::string::npos)
        {
            _recv_statu = RECV_HTTP_ERRO;
            _resp_statu = 400;//Bad Request
            return false;
        }
        std::string key = line.substr(0,pos);
        std::string val = line.substr(pos+2);
        _request.SetHeader(key,val);
        return true;
    }
    bool RecvHttpBody(Buffer *buf)
    {
        if(_recv_statu!=RECV_HTTP_BODY) return false;
        //1.获取正文长度
        size_t content_length = _request.ContentLength();
        if(content_length==0)
        {
            _recv_statu = RECV_HTTP_END;
            return true;
        }
        //2.当前已经接收了多少正文 _request._body
        size_t real_len = content_length - _request._body.size();
        //3.接收正文放到body中，但是也要考虑当前缓冲区中的数据，是否是全部的正文
        // 3.1缓冲区中数据，包含了当前请求的所有正文，则取出所需的数据
        if(buf->ReadAbleSize()>=real_len)
        {
            _request._body.append(buf->ReadPosition(),real_len);
            buf->MoveReadOffset(real_len);
            _recv_statu = RECV_HTTP_END;
            return true;
        }
        // 3.2缓冲区中数据，无法满足当前正文的需要，数据不足，取出数据，然后等待新数据的到来
        _request._body.append(buf->ReadPosition(),buf->ReadAbleSize());
        buf->MoveReadOffset(buf->ReadAbleSize());
        return true;
    }

    public:
    HttpContext():_resp_statu(200),_recv_statu(RECV_HTTP_LINE){}
    int RespStatu(){return _resp_statu;}
    HttpRecvStatu RecvStatu(){return _recv_statu;}
    HttpRequest &Request(){return _request;}
    //重置上下文
    void Reset(){
        _resp_statu = 200;
        _recv_statu = RECV_HTTP_LINE;
        _request.Reset();
    }
    //接收并解析http请求
    void RecvHttpRequest(Buffer *buf)
    {
        switch(_recv_statu)
        {
            case RECV_HTTP_LINE:
                RecvHttpLine(buf);
            case RECV_HTTP_HEAD:
                RecvHttpHeader(buf);
            case RECV_HTTP_BODY:
                RecvHttpBody(buf);
        }
        return ;
    }

};

class HttpServer
{
  private:
  using Handler = std::function<void(const HttpRequest &,HttpResponse *)>;
  using HandlerList = std::vector<std::pair<std::regex,Handler>>;
  HandlerList _get_handlers;
  HandlerList _post_handlers;
  HandlerList _put_handlers;
  HandlerList _delete_handlers;
  std::string _base_path;//静态资源根目录
  TcpServer _server;

  private:
  void ErrorHandle(const HttpRequest &request,HttpResponse *response)//错误处理函数
  {
        //1.组织一个错误显示页面
        std::string body;
        body+="<html>";
        body+="<head>";
        body+="<meta http-equiv='Content-Type' content='text/html;charset=utf-8'>";
        body+="</head>";
        body+="<body>";
        body+="<h1>";
        body+=std::to_string(response->_status);
        body+=" ";
        body+=Util::StatuDesc(response->_status);
        body+=" </h1>";
        body+="</body>";
        body+="</html>";
        //2.将页面数据，当作响应正文，放入rsponse
        response->SetContent(body,"text/html");
  }
  //将HttpResponse中的要素按照http协议格式进行组织，发送
  void WriteResponse(const PtrConnection &conn,const HttpRequest &request,HttpResponse &response)
  {
    //1.组织http响应行
    if(request.Close()==true)
    {
        response.SetHeader("Connection","close");
    }
    else
    {
        response.SetHeader("Connection","keep-alive");
    }
    if(response._body.empty()==false&&response.HasHeader("Content-Length")==false)
    {
        response.SetHeader("Content-Length",std::to_string(response._body.size()));
    }
    if(response._body.empty()==false&&response.HasHeader("Content-Type")==false)
    {
        response.SetHeader("Content-Type","application/octet-stream");
    }
    if(response._redirect_flag==true)
    {
        response.SetHeader("Location",response._redirect_url);
    }
    
    //2.将response中的要素，组织成http响应头
    std::stringstream rsp_str;
    rsp_str<<request._version+" "+std::to_string(response._status)+" "+Util::StatuDesc(response._status)+"\r\n";
    for(auto &header:response._headers)
    {
        rsp_str<<header.first<<": "<<header.second<<"\r\n";
    }
    rsp_str<<"\r\n";
    rsp_str<<response._body;
    //3.发送数据
    conn->Send(rsp_str.str().c_str(),rsp_str.str().size());

  }
    bool IsFileHandler(const HttpRequest &request)
    {
        //1.必须设置了静态资源根目录
        if(_base_path.empty())
        {
            return false;
        }
        //2.请求方法必须是GET,HEAD
        if(request._method!="GET"&&request._method!="HEAD")
        {
            return false;
        }
        //3.请求路径必须是一个合法的文件路径
        if(Util::ValidPath(request._path)==false)
        {
            return false;
        }
        //4.请求资源必须存在
        //有一种请求路径是目录，而不是文件，需要添加index.html后缀
        //不要忘了前缀的相对根目录
        std::string path = _base_path+request._path;//为了不改变原始请求路径，所以需要复制一份
        if(path.back()=='/')
        {
            path.append("index.html");
        }
        //1.根据资源路径，判断文件是否有效
        if(Util::IsRegular(path)==false)
        {
            return false;
        }
        return true;
    }
  //静态资源处理函数
  void FileHandler(const HttpRequest &request,HttpResponse *response)
  {
    std::string path = _base_path+request._path;//为了不改变原始请求路径，所以需要复制一份
        if(path.back()=='/')
        {
            path.append("index.html");
        }
    if(Util::ReadFile(path,&response->_body)==false)
    {
        return;
    }
    //2.根据文件后缀名，判断文件的mime
    std::string mime = Util::ExtMime(path);
    //3.设置响应头
    response->SetHeader("Content-Type",mime);
    return;
  }
  //功能性请求的分发处理
  void Dispatch(HttpRequest &request,HttpResponse *response,HandlerList &handlers)
  {
    //1.根据请求路径，查找对应的处理函数
    //思想：路由表存储的键值对--正则表达式 & 处理函数
    //使用正则表达式匹配请求路径，找到对应的处理函数
    // 1.1如果找到对应的处理函数，则调用处理函数
    // 1.2如果没有找到对应的处理函数，则返回404错误信息
    for(auto &handler : handlers)
    {
        const std::regex &e = handler.first;
        const Handler &functor = handler.second;
        if(std::regex_match(request._path,request._matches,e)==false)
        {
            continue;
        }
        //如果匹配成功，则调用处理函数
        return functor(request,response);
    }

    response->_status = 404;//Not Found
  }
  void Path(HttpRequest &request,HttpResponse *response)
  {
    //1.对请求进行分辨，是一个静态资源请求，还是一个功能性请求
    //  Get,Head默认是静态资源请求
    if(IsFileHandler(request))
    {
        //是一个静态资源请求，则调用静态资源处理函数
        return FileHandler(request,response);
    }
    if(request._method=="GET"||request._method=="HEAD")
    {
        //是一个功能性请求，则调用功能性请求处理函数
        return Dispatch(request,response,_get_handlers);
    }
    else if(request._method=="POST")
    {
        //是一个功能性请求，则调用功能性请求处理函数
        return Dispatch(request,response,_post_handlers);
    }
    else if(request._method=="PUT")
    {
        //是一个功能性请求，则调用功能性请求处理函数
        return Dispatch(request,response,_put_handlers);
    }
    else if(request._method=="DELETE")
    {
        //是一个功能性请求，则调用功能性请求处理函数
        return Dispatch(request,response,_delete_handlers);
    }
    response->_status = 405;//Method Not Allowed
  }
  //设置上下文
  void OnConnected(const PtrConnection &conn)
  {
    conn->SetContext(HttpContext());
    LOG(INFO)<<"OnConnected"<<std::endl;
  }
  //缓冲区区解析+处理
  //技巧：&输入输出，避免复制对象，const &纯输入参数,*输出参数
  void OnMessage(const PtrConnection &conn,Buffer *buf)
  {
    while(buf->ReadAbleSize()>0)
    {
        //1.获取上下文
        HttpContext *context = conn->GetContext()->get<HttpContext>();
        //2.通过上下文对缓冲区数据进行解析，得到HttpRequest对象
        //  2.1如果缓冲区的数据解析出错，返回错误信息
        //  2.2如果解析正常，且请求已经获取完毕，再进行处理
        context->RecvHttpRequest(buf);
        HttpResponse response(context->RespStatu());
        HttpRequest &request = context->Request();
        if(context->RespStatu()>=400)
        {
            //进行错误处理
            ErrorHandle(request,&response);//填充一个错误显示页面，数据到response中
            WriteResponse(conn,request,response);//将response中的要素按照http协议格式进行组织，发送到客户端
            context->Reset();
            buf->MoveReadOffset(buf->ReadAbleSize());//出错了之后，直接把缓冲区清空，关闭连接
            conn->Shutdown();//关闭连接
            return;
        }
        if(context->RecvStatu()!=RECV_HTTP_END)
        {
            //当请求正文未接收完毕，返回错误信息
            return;
        }
        //3.请求路由---查找处理方式    + 业务处理
        Path(request,&response);
        //4.对HttpResponse进行组织，发送到客户端
        WriteResponse(conn,request,response);
        //5.重置上下文
        bool keep_alive = !request.Close();  // 先保存原始长/短连接状态
        context->Reset();
        //6.根据是否是短链接，判断是否需要连接，关闭连接
        if (!keep_alive) {            
            conn->Shutdown();
        }
    }
  }
  public:
  HttpServer(int port,int timeout = DEFALT_TIMEOUT):_server(port)
  {
    _server.EnableInactiveRelease(timeout);
    _server.SetConnectedCallback(std::bind(&HttpServer::OnConnected,this,std::placeholders::_1));
    _server.SetMessageCallback(std::bind(&HttpServer::OnMessage,this,std::placeholders::_1,std::placeholders::_2));
  }
  void SetBasePath(const std::string &path){
    assert(Util::IsDirectory(path)==true);
    _base_path = path;
  }
  //添加请求（请求的正则表达式）与处理函数的映射关系
  void Get(const std::string &path,const Handler &handler)
  {
    _get_handlers.push_back({std::regex(path),handler});
  }
  //添加请求（请求的正则表达式）与处理函数的映射关系
  void Post(const std::string &path,const Handler &handler) 
  {
    _post_handlers.push_back({std::regex(path),handler});
  }
  //添加请求（请求的正则表达式）与处理函数的映射关系
  void Put(const std::string &path,const Handler &handler)
  {
    _put_handlers.push_back({std::regex(path),handler});
  }
  //添加请求（请求的正则表达式）与处理函数的映射关系
  void Delete(const std::string &path,const Handler &handler)
  {
    _delete_handlers.push_back({std::regex(path),handler});
  }
  void SetThreadCount(int count)
  {
    _server.SetThreadCount(count);
  }
  void Listen()
  {
    LOG(INFO)<<"Listen"<<std::endl;
    _server.Start();
  }
};