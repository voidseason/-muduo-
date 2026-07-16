
#include "../server.hpp"

int main()
{
    for(int i=0;i<10;i++)
    {
        pid_t pid = fork();
        if(pid<0)
        {
            LOG(ERROR)<<"fork error"<<std::endl;
            return -1;
        }
        else if(pid==0)
        {
                Socket cli_sock;
                cli_sock.CreateClient(8085,"127.0.0.1");
                std::string req = "GET /hello HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length: 100\r\n\r\nI love youqinyu";
                while(1)
                {
                    assert(cli_sock.Send(req.c_str(),req.size())!=-1);
                    char buf[1024]={0};
                    assert(cli_sock.Recv(buf,1023));
                    LOG(DEBUG)<<buf<<std::endl;
                    sleep(3);
                }
                cli_sock.Close();
                exit(0);
        }
    }
    while(1) sleep(1);
    return 0;
}