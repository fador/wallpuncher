/*
  Copyright (c) 2016, Marko Viitanen (Fador)
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>

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

#include <cstdint>

#include "win32_tun.h"
#include "tap-windows.h"
#include "connection.h"

#define MATCH_IP(data, ip) (((data)[0] == (ip)[0] && (data)[1] == (ip)[1] && (data)[2] == (ip)[2] && (data)[3] == (ip)[3]))

#define CONNECTIONHEADERS 16
enum { TYPE_FRAME = 1, TYPE_PING = 2, TYPE_ACK = 3 };

static void syncRead(Connection* conn)
{
  #define INPUTBUFLEN 1518
	char packet[INPUTBUFLEN];
	DWORD packetlen;
  HANDLE device = conn->getDevice();
  while (1) {
	  printf("Reading one packet\n");
    if (ReadFile(device, packet, INPUTBUFLEN, &packetlen, NULL) == 0) {
      printf("Error reading packet!");
      return;
    }
    if (conn->established) {
      Connection::sentFrame frame;
      frame.data = new uint8_t[packetlen+CONNECTIONHEADERS];
      frame.dataLen = packetlen+CONNECTIONHEADERS;
      frame.frame = conn->getIncOutFrame();
	  
      frame.data[0] = TYPE_FRAME;
      *((uint16_t*)(&frame.data[1])) = htons(frame.frame);
      *((uint16_t*)(&frame.data[3])) = htons(packetlen);

      memcpy(&frame.data[CONNECTIONHEADERS], packet, packetlen);
      conn->addSent(frame);
    }
  }
}

static void syncReadSocket(Connection* conn)
{
  #define BUFLEN 1024
  char buf[BUFLEN];
  int len;
  int fd = conn->getSocket();
  int structLen = sizeof(sockaddr);

  while (1) {
    len = recvfrom(fd, buf, BUFLEN, 0, (sockaddr *)&conn->addr_dst, &structLen);

    // If data is received, print some info of it
    if (len > 0) {
      Connection::receivedFrame frame;
      frame.frame = ntohs(*((uint16_t*)(&buf[1])));
      switch(buf[0]) {
        case TYPE_FRAME:          
          if (frame.frame < conn->getInFrame()) {            
            continue;
          }
          frame.dataLen = ntohs(*((uint16_t*)(&buf[3])));
          
          frame.data = new uint8_t[frame.dataLen];          
          memcpy(frame.data, &buf[CONNECTIONHEADERS], frame.dataLen);
          conn->addReceived(frame);
          conn->sendAck(frame.frame);
          break;
        case TYPE_PING:
          conn->ping();
        break;
        case TYPE_ACK:
          conn->ack(frame.frame);
        break;
        default:
          printf("Invalid packet of type %d!\n", buf[0]);
      }
    } else {
      printf("Recv failed!\n");
      return;
    }
  }
}

static bool doWriting(Connection* conn) {
  HANDLE device = conn->getDevice();
  int fd = conn->getSocket();
  DWORD writelen;
  int structLen = sizeof(sockaddr);
  std::vector<Connection::receivedFrame> toLocalNet;
  std::vector<Connection::sentFrame> toNet;
  std::vector<uint32_t> toAck;
  char buf[CONNECTIONHEADERS];

  if (conn->established) {
    conn->sendReceivedToLocal(toLocalNet);

    for (auto frame : toLocalNet) {
	    if (WriteFile(device, frame.data, frame.dataLen, &writelen, NULL) == 0) {
        printf("Error writing a packet!");
        return false;
      }
      delete []frame.data;
    }

    conn->resendSent(toNet);
    for (auto frame : toNet) {
      if (sendto(fd, (const char *)frame.data, frame.dataLen, 0, (struct sockaddr*) &conn->addr_dst, structLen) == SOCKET_ERROR) {
        std::cerr << "Send failure" << std::endl;
        return false;
      }
    }
    conn->getAcks(toAck);
    for (uint32_t frame : toAck) {
      buf[0] = TYPE_ACK;
      *((uint16_t*)(&buf[1])) = htons(frame);
      *((uint16_t*)(&buf[3])) = htons(0);
      if (sendto(fd, (const char *)buf, CONNECTIONHEADERS, 0, (struct sockaddr*) &conn->addr_dst, structLen) == SOCKET_ERROR) {
        std::cerr << "Send failure" << std::endl;
        return false;
      }
    }
  }

  
  buf[0] = TYPE_FRAME;
  *((uint16_t*)(&buf[1])) = htons(0);
  *((uint16_t*)(&buf[3])) = htons(0);
  //Send ping
  if (sendto(fd, (const char *)buf, CONNECTIONHEADERS, 0, (struct sockaddr*) &conn->addr_dst, structLen) == SOCKET_ERROR) {
    std::cerr << "Send failure" << std::endl;
    return false;
  }

  return true;
}

static void timer(Connection* conn) {
  while (1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!doWriting(conn)) {
      return;
    }
  }
}

int main(int argc, char* argv[]) {
  
  // Input/output buffer
  std::string guid = "";
  int fd;
 
  guid = getGuid();
  if (guid == "") {
    std::cout << "TAP device not found!" << std::endl;
    return EXIT_FAILURE;
  }

  Connection conn;

  std::cout << "guid " << getGuid() << std::endl;

  std::string device = std::string(USERMODEDEVICEDIR+guid+TAP_WIN_SUFFIX);

	HANDLE fp= CreateFile(std::wstring(device.begin(), device.end()).c_str(),GENERIC_READ | GENERIC_WRITE,0,
                        NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, NULL);

  conn.setDevice(fp);
	DWORD readlen;
  DWORD code = 1;

  OVERLAPPED overlapped;
  ZeroMemory(&overlapped, sizeof (overlapped));
  

	DeviceIoControl (fp, TAP_WIN_IOCTL_SET_MEDIA_STATUS, &code, 4, &code, 4, (LPDWORD)&readlen, NULL);
  DWORD code2[3] = {0x0100030a, 0x0000030a, 0x00ffffff};
	DeviceIoControl (fp, TAP_WIN_IOCTL_CONFIG_TUN, code2, 12,	code2, 12, (LPDWORD)&readlen, NULL);

  DWORD bytesRead = 0;
  #define COPYBUFFERSIZE 4096
  BYTE   buffer[COPYBUFFERSIZE] = { 0 };

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


  conn.addr_src = addr_src;
  conn.addr_dst = addr_dst;

  conn.setSocket(fd);

  std::thread readThread(syncRead, &conn);
  std::thread readSocketThread(syncReadSocket, &conn);
  std::thread writeThread(timer, &conn);
    
  readThread.join();
  writeThread.join();
  readSocketThread.join();

  return EXIT_SUCCESS;
}