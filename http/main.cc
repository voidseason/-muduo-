#include "http.hpp"

#define WWROOT "./wwwroot"

std::string RequesStr(const HttpRequest &req)
{
    std::stringstream ss;
    ss<<req._method<<" "<<req._path<<" "<<req._version<<std::endl;
    for(auto &it : req._params)
    {
        ss<<it.first<<": "<<it.second<<std::endl;
    }
    for(auto &it: req._headers)
    {
        ss<<it.first<<": "<<it.second<<std::endl;
    }
    ss<<"\r\n";
    ss<<req._body;
    return ss.str();
}
void HelloHandler(const HttpRequest &req,HttpResponse *resp)
{
    resp->SetContent(RequesStr(req), "text/plain");

}
void LoginHandler(const HttpRequest &req,HttpResponse *resp)
{
    resp->SetContent(RequesStr(req), "text/plain");
}
void PutFileHandler(const HttpRequest &req,HttpResponse *resp)
{
    resp->SetContent(RequesStr(req), "text/plain");
}
void DeleteFileHandler(const HttpRequest &req,HttpResponse *resp)
{
    resp->SetContent(RequesStr(req), "text/plain");
}
int main()
{
    HttpServer server(8085);
    server.SetThreadCount(3);
    server.SetBasePath(WWROOT);//设置静态资源根目录
    server.Get("/hello",HelloHandler);
    server.Post("/login",LoginHandler);
    server.Put("/1234.txt",PutFileHandler);
    server.Delete("/1234.txt",DeleteFileHandler);
    server.Listen();
    return 0;
}