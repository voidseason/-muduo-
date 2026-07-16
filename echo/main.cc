#include "echo.hpp"

int main()
{
    EchoServer echo_server(8500);
    echo_server.Start();
    return 0;
}