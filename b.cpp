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

#include <algorithm>
#include <memory>
#include <string>
#include <exception>
#include <stdexcept>
#include <iterator>
#include <iostream>

namespace keisan_kun{
class http_client{
  friend http_client &operator >>(http_client &i, std::string &str);

public:
  class exception : public std::runtime_error{
  public:
    exception() : std::runtime_error("http_client"){}
    exception(const std::string &message) : std::runtime_error(message){}
  };

  const std::size_t buffer_size = 0x800;

  http_client() = delete;
  http_client(const http_client&) = delete;
  http_client(http_client &&) = delete;

  operator bool() const{
    return received_size > 0;
  }

  http_client(std::string str){
    if(!strstr(str.c_str(), "http://")){ throw(exception("address")); }
    std::string host_path = str.substr(7, str.size() - 7);
    if(host_path.size() == 0){ throw(exception("address")); }

    std::string::iterator p = std::find(host_path.begin(), host_path.end(), '/');
    if(p != host_path.end()){
      path = std::string(p, host_path.end());
    }
    host = std::string(host_path.begin(), p);

    p = std::find(host.begin(), host.end(), ':');
    if(p != host.end()){
      port = atoi(std::string(p, host.end()).c_str());
      if(port < 0){ port = 80; }
    }

    servhost = gethostbyname(host.c_str());
    if(!servhost){
      throw(exception("gethostbyname"));
    }
    
    server.sin_family = AF_INET;
    bcopy(servhost->h_addr, (char*)&server.sin_addr, servhost->h_length);

    if(port != 0){
      server.sin_port = htons(port);
    }else{
      service = getservbyname("http", "tcp");
      if(service){
	server.sin_port = service->s_port;
      }else{
	server.sin_port = htons(80);
      }
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0){
      throw(exception("socket"));
    }

    if(connect(s, (sockaddr*)&server, sizeof(server)) == -1){
      throw(exception("connect"));
    }

    fp = fdopen(s, "r+");
    if(!fp){
      throw(exception("fdopen"));
    }
    setvbuf(fp, nullptr, _IONBF, 0);
  }
  
  ~http_client(){
    fclose(fp);
    close(s);
  }

  void start_receive(){
    fprintf(fp, "GET %s HTTP/1.0\r\n", path.c_str());
    fprintf(fp, "Host: %s:%d\r\n", host.c_str(), port);
    fprintf(fp, "\r\n");
  }

private:
  void receive_once(){
    received_size = fread((void*)buffer, sizeof(buffer_size), 1, fp);
  }

  int s; // file discriptor.
  hostent *servhost; // for hostname and IP address.
  servent *service;
  sockaddr_in server = { 0 }; // for socket.
  FILE *fp;
  char *buffer = new char[buffer_size];
  std::size_t received_size = 0;
  std::string host, path;
  unsigned port = 80;  
};

http_client &operator >>(http_client &i, std::string &str){
  i.receive_once();
  str = std::string(&i.buffer[0], &i.buffer[i.received_size]);
  return i;
}
} // namespace keisan_kun

#include <iostream>

int main(){
  keisan_kun::http_client http_client("http://www.google.co.jp/");
  std::string str;

  http_client.start_receive();
  while(http_client >> str){
    std::cout << str;
  }
  return 0;
} 
