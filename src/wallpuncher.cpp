#include <string>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifdef  _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment(lib,"ws2_32.lib")
#define NOMINMAX
#include <winsock2.h>
#include <process.h>
#include <direct.h>
#else
#define SOCKET_ERROR -1
#include <netdb.h>  // for gethostbyname()
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

int main(int argc, char* argv[]) {
 
  // Input/output buffer
  #define BUFLEN 1024
  char buf[BUFLEN];
  int len;

  int fd;
 
  // Local and destination ip
  const std::string ipLocal = "0.0.0.0";
  const std::string ipRemote = "146.185.175.121";
  
  const int port = 10001;

  // Initialize winsock
#ifdef WIN32
  WSADATA wsaData;
  int iResult;
  // Initialize Winsock
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    std::cerr << "WSAStartup failure" << std::endl;
    return EXIT_FAILURE;
  }
#endif

  sockaddr_in addr_src;
  sockaddr_in addr_dst;

  // Create UDP socket
  fd = socket(AF_INET, SOCK_DGRAM, 0);

  if (fd < 0)  {
    std::cerr << "Socket creation failure" << std::endl;
    return EXIT_FAILURE;
  }

  // Set local ip and port info
  memset(&addr_src, 0, sizeof(addr_src));
  addr_src.sin_family      = AF_INET;
  addr_src.sin_addr.s_addr = inet_addr(ipLocal.c_str());
  addr_src.sin_port        = htons(port);

  // Bind source port
  if (bind(fd, (sockaddr*)&addr_src, sizeof(addr_src)) < 0)  {
    std::cerr << "Bind failure" << std::endl;
    return EXIT_FAILURE;
  }

  // Set destination port and host
  memset(&addr_dst, 0, sizeof(addr_dst));
  addr_dst.sin_family = AF_INET;
  addr_dst.sin_port = htons(port);
  addr_dst.sin_addr.s_addr = inet_addr(ipRemote.c_str());

  int structLen = sizeof(sockaddr);

  memcpy(buf, "Hello World\0", 12);

  if (sendto(fd, buf, 12, 0, (struct sockaddr*) &addr_dst, structLen) == SOCKET_ERROR) {
    std::cerr << "Send failure" << std::endl;
    return EXIT_FAILURE;
  }

  // Set timeout for waiting for a connection
  #ifdef _WIN32
  DWORD dwTime = 1000;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&dwTime, sizeof(dwTime)) < 0) {
    std::cerr << "Set timeout failure" << std::endl;
    return EXIT_FAILURE;
  }
  #else
  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv,sizeof(tv)) < 0) {
    std::cerr << "Set timeout failure" << std::endl;
    return EXIT_FAILURE;
  }
  #endif

  while(1) {
    std::cout << "Waiting..." << std::endl;
    
    // Try to receive data, timeout after 1s
    len = recvfrom(fd, buf, BUFLEN, 0, (sockaddr *)&addr_dst, &structLen);

    // If data is received, print some info of it
    if (len > 0) {      
      std::cout <<  "Received packet from " <<  inet_ntoa(addr_dst.sin_addr) << ":" << ntohs(addr_dst.sin_port) << std::endl;
      std::cout <<  " " << buf << std::endl;
    } else {
      std::cout << "Revc timeout.." << std::endl;
    }
    
    // Send data to the other direction
    memcpy(buf, "Hello World\0", 12);
    if (sendto(fd, buf, 12, 0, (struct sockaddr*) &addr_dst, structLen) == SOCKET_ERROR) {
      std::cerr << "Send failure" << std::endl;
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}