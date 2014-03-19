#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <exception>

class http_client{
public:
  class exception : public std::runtime_error{};

  const std::size_t buffer_size = 0x800;

  http_client() = delete;
  http_client(const http_client&) = delete;
  http_client(http_client &&) = delete;

  http_client(std::string str){
    if(!strstr(str.c_str(), "http://")){ throw(exception()); }
    std::string host_path = str.substr(7, str.size() - 7);
    if(host_path.size() != 0){ throw(exception()); }

    char *p = strchr(host_path, '/');
    if(p){
      path = p;
      *p = '\0';
      host = host_path;
    }

private:
  int s; // file discriptor.
  hostent *servhost; // for hostname and IP address.
  sockaddr_in server; // for socket.
  FILE *fp;
  char *buffer = new char[buffer_size];
  std::string host, path;
  unsigned port = 80;  
};

