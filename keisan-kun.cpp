#include <unistd.h>
#include <netdb.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <string>
#include <map>

#define R 0
#define W 1

char pipebuf[0x1000];

int popen2(const char *command, int *pfd){
  int pipe_c2p[2], pipe_p2c[2];
  pid_t pid;

  if(pipe(pipe_c2p) < 0){
    perror("popen2");
    return -1;
  }

  if(pipe(pipe_p2c) < 0){
    perror("popen2");
    close(pipe_c2p[R]), close(pipe_c2p[W]);
    return -1;
  }

  if((pid = fork()) < 0){
    perror("popen2");
    close(pipe_c2p[R]), close(pipe_c2p[W]);
    close(pipe_p2c[R]), close(pipe_p2c[W]);
    return -1;
  }

  if(pid == 0){
    close(pipe_p2c[W]), close(pipe_c2p[R]);
    dup2(pipe_p2c[R], STDIN_FILENO);
    dup2(pipe_c2p[W], STDOUT_FILENO);
    close(pipe_p2c[R]), close(pipe_c2p[W]);
    if(execlp("sh", "sh", "-c", command, NULL) < 0){
      perror("popen2");
      close(pipe_p2c[R]), close(pipe_c2p[W]);
      exit(-1);
    }
  }

  close(pipe_p2c[R]), close(pipe_c2p[W]);
  pfd[R] = pipe_c2p[R];
  pfd[W] = pipe_p2c[W];

  return pid;
}

const char
  *wolfram_command = "wolfram",
  *bot_owner = "bot_owner:uwanosora",
  *nick = "keisan-kun",
  *serv = "irc.livedoor.ne.jp",
  *chan;

unsigned char chname[20];

struct session{
  int fd[2];
};

typedef std::map<std::string, session> session_map_type;
session_map_type session_map;

size_t read_line(int rfd, char terminal_symbol = '\n'){
  size_t i;
  char c;
  i = 0;
  do{
    if(i > sizeof(pipebuf)){
      sprintf(pipebuf, ">_<\n");
      i = 4;
      break;
    }
    read(rfd, (void*)&c, 1);
    pipebuf[i++] = c;
  }while(c != terminal_symbol);
  pipebuf[i] = '\0';
  printf(pipebuf);
  return i;
}

int pfd[2];

size_t w2(std::string name, std::string command){
  read_line(pfd[R], '=');
  write(pfd[W], command.c_str(), command.size());
  read_line(pfd[R], '\n');
  return read_line(pfd[R], '\n');
}

int main(int argc, char *argv[]) {
  int chname_count = 0;
  chname[chname_count++] = (unsigned char)'#';
  chname[chname_count++] = 27;
  chname[chname_count++] = 36;
  chname[chname_count++] = 66;
  chname[chname_count++] = 74;
  chname[chname_count++] = 73;
  chname[chname_count++] = 58;
  chname[chname_count++] = 93;
  chname[chname_count++] = 37;
  chname[chname_count++] = 82;
  chname[chname_count++] = 37;
  chname[chname_count++] = 67;
  chname[chname_count++] = 37;
  chname[chname_count++] = 45;
  chname[chname_count++] = 33;
  chname[chname_count++] = 60;
  chname[chname_count++] = 27;
  chname[chname_count++] = 40;
  chname[chname_count++] = 66;
  chname[chname_count++] = 0;
  chan = (const char*)&chname[0];
  //chan = "#namekuji_proc";

  char msgbuf[0x100];
  sprintf(msgbuf, "PRIVMSG");

  int ret;
  char buf[0x800];
  int sock;
  struct addrinfo hints, *ai;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if (ret = getaddrinfo(serv, "6667", &hints, &ai)) {
    puts(gai_strerror(ret));
    return 1;
  }
  sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (ret = connect(sock, ai->ai_addr, ai->ai_addrlen)) {
    puts(gai_strerror(ret));
    return 1;
  }
  freeaddrinfo(ai);
  sprintf(buf, "USER %s 0 * :%s\r\n", nick, bot_owner);
  printf(buf), printf("\n");
  send(sock, buf, strlen(buf), 0);
  sprintf(buf, "NICK %s\r\n", nick);
  printf(buf), printf("\n");
  send(sock, buf, strlen(buf), 0);

  popen2(wolfram_command, pfd);
  for(int i = 0; i < 4; ++i){ read_line(pfd[R]); }

  while (recv(sock, buf, sizeof(buf), 0) > 0) {
    //fputs(buf, stdout);
    if (!strncmp(buf, "PING ", 5)) {
      buf[1] = 'O';
      send(sock, buf, strlen(buf), 0);
      continue;
    }
    if (buf[0] != ':') continue;
    if (!strncmp(strchr(buf, ' ') + 1, "001", 3)) {
      sprintf(buf, "MODE %s +B\r\nJOIN %s\r\n", nick, chan);
      send(sock, buf, strlen(buf), 0);
      continue;
    }
    using namespace std;
    char *first_space = strchr(buf, ' ');
    string msg_str(first_space, strchr(buf, '\n'));
    size_t idx = msg_str.find(" :w ");
    if (idx == -1) { continue; }
    idx += 4;
    size_t n = w2("", msg_str.substr(idx, msg_str.size() - idx));
    if (n > 0) {
      string output;
      output += "NOTICE ";
      output += chan;
      output += " :";
      output += pipebuf;
      output += "\r\n";
      send(sock, output.c_str(), output.size(), 0);
    }
  }
  close(sock);
  return 0;
}
