#include <unistd.h>
#include <netdb.h>
#include <dirent.h>
#include <fcntl.h>

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

char pipebuf[0xF00];

const char
  *bot_owner = "bot_owner:uwanosora",
  *nick = "keisan-kun",
  *serv = "irc.livedoor.ne.jp",
  *chan;

unsigned char chname[20];

size_t w(std::string name, std::string command){
  using namespace std;
  {
    ofstream ofile("redirect", ios::trunc);
    ofile << command << endl;
  }

  FILE *fp = popen("wolfram < redirect", "r");

  size_t i;
  char c;
  for(int n = 0; n < 6; ++n){
    i = 0;
    do{
      if(i > sizeof(pipebuf)){
        sprintf(pipebuf, ">_<\n");
        i = 4;
        break;
      }
      fread((void*)&c, 1, 1, fp);
      pipebuf[i++] = c;
    }while(c != '\n');
  }
  pipebuf[i] = '\0';

  pclose(fp);

  return i;
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
  char buf[512];
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
  send(sock, buf, strlen(buf), 0);
  sprintf(buf, "NICK %s\r\n", nick);
  send(sock, buf, strlen(buf), 0);
  while (recv(sock, buf, 512, 0) > 0) {
    fputs(buf, stdout);
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
    idx++, idx++, idx++, idx++;
    size_t n = w("", msg_str.substr(idx, msg_str.size() - idx));
    if (n > 0) {
      string output;
      output += "NOTICE ";
      output += chan;
      output += " :";
      output += &pipebuf[8];
      output += "\r\n";
      send(sock, output.c_str(), output.size(), 0);
    }
//    if (!strncmp(first_space + 1, msgbuf, strlen(msgbuf))) {
//      string(first_space).find(" :w ");
//      const char
//        *command = strchr(buf + strlen(msgbuf) + 1, ':') + 1,
//        *name_begin = strchr(command + 1, ':') + 1,
//        *name_end = strchr(name_begin + 1, ' ');
//      if (strncmp(command, "w ", 2)) continue;
//      command = command + 2;
//      size_t n = w("", string(command, strchr(command + 1, '\n') - 1));
//      if (n > 0) {
//        std::string output;
//        output += "NOTICE ";
//        output += chan;
//        output += " :";
//        output += &pipebuf[8];
//        output += "\r\n";
//        send(sock, output.c_str(), output.size(), 0);
//      }
//      continue;
//    }
  }
  close(sock);
  return 0;
}
