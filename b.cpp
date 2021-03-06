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
#include <arpa/inet.h>

#include <map>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
#include <exception>
#include <stdexcept>
#include <iterator>
#include <iostream>
#include <streambuf>
#include <thread>
#include <atomic>

namespace keisan_kun{
template<class TemplateTag = void>
class http_server{
public:
  class exception : public std::runtime_error{
  public:
    exception() : std::runtime_error("http_server"){}
    exception(const std::string &message) : std::runtime_error("http_server: " + message){}
  };

  static const std::size_t buffer_size = 0x100;
  static const std::size_t timeout = 3;
  static const std::size_t max_socket_num = FD_SETSIZE / 8;

private:
  struct client_info{
    client_info(int port = 80) : port(port){}
    client_info(const client_info&) = delete;

    client_info(client_info &&other) :
      hostname(std::move(other.hostname)),
      ipaddr(std::move(other.ipaddr)),
      port(std::move(other.port)),
      last_access(std::move(other.last_access)),
      read_buffer(other.read_buffer),
      write_buffer(other.write_buffer)
    {
      read_buffer = nullptr;
      write_buffer = nullptr;
    }

    ~client_info(){
      delete[] read_buffer;
      delete[] write_buffer;
    }

    std::string hostname, ipaddr;
    int port;
    time_t last_access;
    char
      *read_buffer = new char[buffer_size],
      *write_buffer = new char[buffer_size];
  };

public:
  class server_stream{
  public:
    server_stream(int s, char *read_buffer, char *write_buffer) :
      s(s),
      read_buffer(read_buffer),
      write_buffer(write_buffer)
    {}

    server_stream() = delete;
    server_stream(const server_stream&) = default;
    server_stream(server_stream&&) = default;
    ~server_stream() = default;

    int s;
    char *read_buffer, *write_buffer;
    std::size_t r_current = 0, r_size, w_current = 0;
    bool success;
  };

  template<class RHS>
  friend server_stream &operator >>(server_stream&, RHS&);

  template<class RHS>
  friend server_stream &operator <<(server_stream&, const RHS&);

  http_server(int port) : client_info_map(new client_info[max_socket_num]), port(port), alive(true){
    for(int i = 0; i < max_socket_num; ++i){
      client_info_map[i].port = port;
    }

    int sock_optval = 1;
    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &sock_optval, sizeof(sock_optval)) == -1){
      throw(exception("setsockopt"));
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(listening_socket, (sockaddr*)&sin, sizeof(sin)) < 0){
      throw(exception("bind"));
    }

    if(listen(listening_socket, SOMAXCONN) == -1){
      throw(exception("listen"));
    }

    FD_ZERO(&org_target_fds);
    FD_SET(listening_socket, &org_target_fds);
  }

  http_server() = delete;
  http_server(const http_server&) = delete;
  http_server(http_server&&) = delete;

  ~http_server(){
    alive = false;
    close(listening_socket);
  }

  virtual bool proc(server_stream&) = 0;

  void launch(){
    auto f = [&](){
      try{
        timeval waitval;
        waitval.tv_sec = 2;
        waitval.tv_usec = 500;

        while(true){
          if(!std::atomic_load_explicit(&alive, std::memory_order_seq_cst)){
            return;
          }

          target_fds = org_target_fds;
          select(max_socket_num, &target_fds, nullptr, nullptr, &waitval);
          for(int i = 0; i < max_socket_num; ++i){
            if(!FD_ISSET(i, &org_target_fds)){ continue; }
            if(i != listening_socket){
              time_t now_time;
              time(&now_time);
              if(now_time - timeout > client_info_map[i].last_access){
                close(i);
                FD_CLR(i, &org_target_fds);
                continue;
              }
              time(&client_info_map[i].last_access);
              server_stream s(i, client_info_map[i].read_buffer, client_info_map[i].write_buffer);
              if(!proc(s)){
                FD_CLR(i, &org_target_fds);
              }
            }else{
              int new_socket = accept_new_client();
              if(new_socket != -1){
                FD_SET(new_socket, &org_target_fds);
              }
            }
          }
        }
      }catch(...){
        std::cerr << "error" << std::endl;
      }
    };

    thread_ptr.reset(new std::thread(f));
    thread_ptr->detach();
  }

  void halt(){
    alive = false;
  }

  bool now_running() const{
    return static_cast<bool>(alive);
  }

private:
  int accept_new_client(){
    int new_socket = 0;
    int len = sizeof(sin);
    new_socket = accept(listening_socket, (sockaddr*)&sin, (socklen_t*)&len);
    if(new_socket == -1){
      throw(exception("accept"));
    }
    hostent *peer_host;
    sockaddr_in peer_sin;
    len = sizeof(peer_sin);
    getpeername(new_socket, (sockaddr*)&peer_sin, (socklen_t*)&len);
    peer_host = gethostbyaddr((char*)&peer_sin.sin_addr.s_addr, sizeof(peer_sin.sin_addr), AF_INET);

    client_info &info(client_info_map[new_socket]);
    info.hostname = peer_host->h_name;
    info.ipaddr = inet_ntoa(peer_sin.sin_addr);
    info.port = ntohs(peer_sin.sin_port);
    time(&info.last_access);

    return new_socket;
  }

  std::unique_ptr<client_info[]> client_info_map;
  int listening_socket, port;
  sockaddr_in sin;
  fd_set org_target_fds, target_fds;
  std::unique_ptr<std::thread> thread_ptr;
  std::atomic<bool> alive;
};

template<class RHS>
http_server<>::server_stream &operator >>(http_server<>::server_stream &s, RHS &rhs){
  while(true){
    int n = read(s.s, s.read_buffer, http_server<>::buffer_size);
    std::cout << n << std::endl;
    char c;
    for(int i = 0; i < n; ++i){
      rhs.push_back(s.read_buffer[i]);
    }
    if(n != http_server<>::buffer_size){ break; }
  }
  return s;
}

template<class RHS>
http_server<>::server_stream &operator <<(http_server<>::server_stream &s, const RHS &rhs){
  auto iter = rhs.begin(), end = rhs.end();
  while(iter != end){
    int n;
    for(n = 0; n < static_cast<int>(http_server<>::buffer_size) && iter != end; ++n, ++iter){
      s.write_buffer[n] = *iter;
    }
    write(s.s, s.write_buffer, n);
  }
  return s;
}

http_server<>::server_stream &operator <<(http_server<>::server_stream &s, const char *str){
  return s << std::string(str);
}

template<class TemplateTag = void>
class http_client{
  friend http_client<> &operator >>(http_client<> &i, std::string &str);

public:
  class exception : public std::runtime_error{
  public:
    exception() : std::runtime_error("http_client"){}
    exception(const std::string &message) : std::runtime_error("http_client: " + message){}
  };

  static const std::size_t buffer_size = 0x800;

  http_client() = delete;
  http_client(const http_client&) = delete;
  http_client(http_client &&) = delete;

  operator bool() const{
    return received_size > 0;
  }

  http_client(std::string str) : buffer(new char[buffer_size]){
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
    received_size = fread((void*)buffer.get(), sizeof(buffer_size), 1, fp);
  }

  int s; // file discriptor.
  hostent *servhost; // for hostname and IP address.
  servent *service;
  sockaddr_in server = { 0 }; // for socket.
  FILE *fp;
  std::unique_ptr<char> buffer;
  std::size_t received_size = 0;
  std::string host, path;
  unsigned port = 80;
};

http_client<> &operator >>(http_client<> &i, std::string &str){
  i.receive_once();
  str = std::string(i.buffer.get(), i.buffer.get() + i.received_size);
  return i;
}
} // namespace keisan_kun

#include <iostream>

struct echo_server : public keisan_kun::http_server<>{
  echo_server(int port) : keisan_kun::http_server<>(port){}

  bool proc(server_stream &s){
    std::string str;
    s >> str;
    std::cout << str << std::endl;
    return false;
  }
};

int main(){
  echo_server server(55555);
  server.launch();
  std::cout << "ready..." << std::endl;
  while(server.now_running()){
    sleep(1);
  }

  /*
  keisan_kun::http_client<> http_client("http://www.google.co.jp/");
  std::string str;

  http_client.start_receive();
  while(http_client >> str){
    std::cout << str;
  }
  */
  return 0;
} 
