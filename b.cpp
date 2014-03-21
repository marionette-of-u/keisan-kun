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

  class server_stream : public std::basic_streambuf<char, std::char_traits<char>>{
  public:
    server_stream(int s, char *read_buffer, char *write_buffer) :
      std::basic_streambuf<char, std::char_traits<char>>(),
      s(s),
      read_buffer(read_buffer),
      write_buffer(write_buffer)
    {}

    server_stream() = default;
    server_stream(const server_stream&) = default;
    server_stream(server_stream&&) = default;

    ~server_stream(){}

  protected:
    std::streampos seekoff(std::streamoff,  std::ios::seek_dir, int = std::ios::in | std::ios::out){
      return std::char_traits<char>::eof();
    }

    std::streampos seekpos(std::streampos, int = std::ios::in | std::ios::out){
      return std::char_traits<char>::eof();
    }

    int overflow(char c = std::char_traits<char>::eof()){
      if(c != std::char_traits<char>::eof()){
        write_buffer[w_current++] = c;
        if(w_current >= buffer_size){
          w_current = 0;
          ::write(s, write_buffer, buffer_size);
        }
      }else if(w_current > 0){
        ::write(s, write_buffer, buffer_size);
        w_current = 0;
      }
      return 0;
    }

    int underflow(){
      if(r_current > 0){
        return read_buffer[r_size - r_current--];
      }else{
        if(r_size > 0){
          int ret = read_buffer[r_size - 1];
          r_size = ::read(s, read_buffer, buffer_size);
          r_current = 0;
          return ret;
        }else{
          return std::char_traits<char>::eof();
        }
      }
    }

  private:
    int s;
    char *read_buffer, *write_buffer;
    std::size_t r_current = 0, r_size = 0, w_current = 0;
  };

public:
  class stream : public std::basic_iostream<char, std::char_traits<char>>{
  public:
    stream() = delete;
    stream(const stream&) = delete;
    stream(stream&&) = delete;

    stream(int s, char *read_buffer, char *write_buffer) :
      std::basic_iostream<char, std::char_traits<char>>(new server_stream(s, read_buffer, write_buffer))
    {}

    ~stream(){}
  };

  http_server() : alive(true){
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

  http_server(const http_server&) = delete;
  http_server(http_server&&) = delete;

  ~http_server(){
    alive = false;
    close(listening_socket);
  }

  virtual bool proc(stream&) = 0;

  void launch(){
    auto f = [&](){
      timeval waitval;
      waitval.tv_sec = 2;
      waitval.tv_usec = 500;

      while(true){
    if(!std::atomic_load_explicit(&alive, std::memory_order_seq_cst)){
      return;
    }

    target_fds = org_target_fds;
        select(FD_SETSIZE, &target_fds, nullptr, nullptr, &waitval);

        for(auto iter = client_info_map.begin(); iter != client_info_map.end(); ++iter){
          std::pair<const int, client_info> &item(*iter);

          time_t now_time;
          time(&now_time);
          if(now_time - timeout > item.second.last_access){
            close(item.first);
            FD_CLR(item.first, &org_target_fds);
            iter = client_info_map.erase(iter);
            continue;
          }

          if(item.first == listening_socket){
            int new_socket = accept_new_client();
            if(new_socket != -1){ FD_SET(new_socket, &org_target_fds); }
          }else{
            stream s(item.first, item.second.read_buffer, item.second.write_buffer);
            if(!proc(s)){
              FD_CLR(item.first, &org_target_fds);
              iter = client_info_map.erase(iter);
              continue;
            }

            time(&item.second.last_access);
          }
        }
      }
    };

    thread_ptr.reset(new std::thread(f));
    thread_ptr->detach();
  }

  void halt(){
    alive = false;
  }

private:
  int accept_new_client(){
    int len = sizeof(sin);
    int new_socket = accept(listening_socket, (sockaddr*)&sin, (socklen_t*)&len);
    if(new_socket == -1){
      throw(exception("accept"));
    }
    hostent *peer_host;
    sockaddr_in peer_sin;
    len = sizeof(peer_sin);
    getpeername(new_socket, (sockaddr*)&peer_sin, (socklen_t*)&len);
    peer_host = gethostbyaddr((char*)&peer_sin.sin_addr.s_addr, sizeof(peer_sin.sin_addr), AF_INET);

    client_info info;
    info.hostname = peer_host->h_name;
    info.ipaddr = inet_ntoa(peer_sin.sin_addr);
    info.port = ntohs(peer_sin.sin_port);
    time(&info.last_access);
    client_info_map.insert(std::make_pair(new_socket, std::move(info)));

    return new_socket;
  }

  std::map<int, client_info> client_info_map;
  int listening_socket, port;
  sockaddr_in sin;
  fd_set org_target_fds, target_fds;
  std::unique_ptr<std::thread> thread_ptr;
  std::atomic<bool> alive;
};

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
  bool proc(stream &s){
    std::string str;
    s >> str;
    s << str;
    return false;
  }
};

int main(){
  echo_server server;
  server.launch();
  std::cout << "server.launch();\nsleep(3);" << std::endl;
  sleep(3);

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
