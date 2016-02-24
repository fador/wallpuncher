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
#include <deque>

#include "win32_tun.h"
#include "tap-windows.h"
#include "connection.h"

uint32_t hexToByte(char hex)
{
  if (hex >= '0' && hex <= '9')
    return hex - '0';
  if (hex >= 'a' && hex <= 'f')
    return 10+(hex - 'a');
  if (hex >= 'A' && hex <= 'F')
    return 10+(hex - 'A');

  return 0;
}

#define MATCH_IP(data, ip) (((data)[0] == (ip)[0] && (data)[1] == (ip)[1] && (data)[2] == (ip)[2] && (data)[3] == (ip)[3]))

#define CONNECTIONHEADERS 8
enum { TYPE_FRAME = 1, TYPE_PING = 2, TYPE_ACK = 3 };

static void syncRead(Connection* conn)
{
  #define INPUTBUFLEN 2048
	char packet[INPUTBUFLEN];
	DWORD packetlen;
  HANDLE device = conn->getDevice();

  while (1) {    
    OVERLAPPED overlapped = {0};
		overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (ReadFile(device, packet, INPUTBUFLEN, &packetlen, &overlapped) == 0)
		{
      if (GetLastError() != ERROR_IO_PENDING) {
				return;
      }
			if (WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0) {
        CloseHandle(overlapped.hEvent);
				continue;
      }
			if (GetOverlappedResult(device, &overlapped, &packetlen, FALSE) == 0)
				return;
		}
		CloseHandle(overlapped.hEvent);

    if (conn->established && packetlen) {
      Connection::sentFrame frame;
      frame.data = new uint8_t[packetlen+CONNECTIONHEADERS];
      frame.dataLen = packetlen+CONNECTIONHEADERS;
      frame.frame = conn->getIncOutFrame();
	  
      frame.data[0] = TYPE_FRAME;
      *((uint32_t*)(&frame.data[1])) = htonl(frame.frame);
      *((uint16_t*)(&frame.data[5])) = htons(packetlen);

      memcpy(&frame.data[CONNECTIONHEADERS], packet, packetlen);
      conn->addSent(frame);
    }
    
  }
}

static void syncReadSocket(Connection* conn)
{
  #define BUFLEN 2048
  char buf[BUFLEN];
  char *bufptr;
  int len;
  int fd = conn->getSocket();
  int structLen = sizeof(sockaddr);
  std::deque<uint8_t> inBuf;

  while (1) {
    len = recvfrom(fd, buf, BUFLEN, 0, (sockaddr *)&conn->addr_dst, &structLen);

    // If data is received, print some info of it
    if (len > 0) {
      
      inBuf.insert(inBuf.end(), buf, buf+len);
      while (inBuf.size() >= CONNECTIONHEADERS) {
        Connection::receivedFrame frame;
        char temp[6];
        std::copy(inBuf.begin()+1, inBuf.begin()+7, temp);
        frame.frame = ntohl(*((uint32_t*)(&temp[0])));
        frame.dataLen = ntohs(*((uint16_t*)(&temp[4])));

        if(inBuf.size() < CONNECTIONHEADERS+frame.dataLen) { 
          if (conn->verbose)
            printf("Waiting for data %d/%d\n", inBuf.size(), CONNECTIONHEADERS+frame.dataLen);
          continue;
        }

        switch(inBuf[0]) {
          case TYPE_FRAME:
            if (conn->verbose)
              printf("Got FRAME %d\n",frame.frame);
            conn->sendAck(frame.frame);
            if (frame.frame < conn->getInFrame()) {
              if (conn->verbose)
                printf("Old frame..%d < %d\n", frame.frame, conn->getInFrame());
              inBuf.erase(inBuf.begin(), inBuf.begin()+CONNECTIONHEADERS+frame.dataLen);
              continue;
            }
            
            frame.data = new uint8_t[frame.dataLen];
            std::copy(inBuf.begin()+CONNECTIONHEADERS, inBuf.begin()+CONNECTIONHEADERS+frame.dataLen, frame.data);
            conn->addReceived(frame);            
            break;
          case TYPE_PING:
            //printf("Got PING\n");
            conn->ping();
          break;
          case TYPE_ACK:
            conn->ack(frame.frame);
            if (conn->verbose)
              printf("Got ACK %d\n", frame.frame);
          break;
          default:
            printf("Invalid packet of type %d!\n", inBuf[0]);
            return;
        }
        inBuf.erase(inBuf.begin(), inBuf.begin()+CONNECTIONHEADERS+frame.dataLen);

        //printf("Inbuf len: %d\n", inBuf.size());
      }
    }
  }
}


static bool doWritingLocal(Connection* conn) {
  HANDLE device = conn->getDevice();
  DWORD writelen;
  std::vector<Connection::receivedFrame> toLocalNet;

  if (conn->established) {
    
    conn->sendReceivedToLocal(toLocalNet);
    for (auto frame : toLocalNet) {
      DWORD out;
      if (conn->verbose)
        printf("Writing to local net..%d\n", frame.dataLen);
      /*
	    if (WriteFile(device, frame.data, frame.dataLen, &writelen, NULL) == 0) {
        printf("Error writing a packet!");
        return false;
      }*/
      OVERLAPPED overlapped = {0};
		  overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		  DWORD writelen;
		  if (WriteFile(device, frame.data, frame.dataLen, &writelen, &overlapped) == 0)
		  {
			  if (GetLastError() != ERROR_IO_PENDING)
				  return false;
			  if (WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0)
				  return false;
			  if (GetOverlappedResult(device, &overlapped, &writelen, FALSE) == 0)
				  return false;
		  }
		  CloseHandle(overlapped.hEvent);

      delete []frame.data;
      if (conn->verbose)
        printf("Written to local net %d\n", writelen);
    }    
  }

  return true;
}

static bool doWriting(Connection* conn) {
  int fd = conn->getSocket();
  int structLen = sizeof(sockaddr);
  std::vector<Connection::sentFrame> toNet;
  std::vector<uint32_t> toAck;
  char buf[CONNECTIONHEADERS];
  static int lastPing = time(0);
  static uint32_t timer = 0;

  if (conn->established) {
   

    conn->resendSent(toNet);
    for (auto frame : toNet) {
      if (conn->verbose)
        printf("Sending to net..%d len %d %x %x %x \n", frame.frame, frame.dataLen, frame.data[0], frame.data[1], frame.data[2]);
      size_t sentLen = sendto(fd, (const char *)frame.data, frame.dataLen, 0, (struct sockaddr*) &conn->addr_dst, structLen);
      if (sentLen == SOCKET_ERROR) {
        std::cerr << "Send failure" << std::endl;
        return false;
      }
      if (sentLen < frame.dataLen) {
        printf("Send failed!!!!\n");
      }
    }


    conn->getAcks(toAck);
    for (uint32_t frame : toAck) {
      if (conn->verbose)
        printf("Sending ACK %d..\n", frame);
      buf[0] = TYPE_ACK;
      *((uint32_t*)(&buf[1])) = htonl(frame);
      *((uint16_t*)(&buf[5])) = htons(0);
      size_t sentLen = sendto(fd, (const char *)buf, CONNECTIONHEADERS, 0, (struct sockaddr*) &conn->addr_dst, structLen);
      if (sentLen == SOCKET_ERROR) {
        std::cerr << "Send failure" << std::endl;
        return false;
      }
      if (sentLen < CONNECTIONHEADERS) {
        printf("Send failed (ACK)!!!!\n");
      }
      if (conn->verbose)
        printf("ACK sent..\n", frame);
    }
  }

  // Ping once a second
  if (lastPing != time(0)) {
    buf[0] = TYPE_PING;
    *((uint32_t*)(&buf[1])) = htonl(0);
    *((uint16_t*)(&buf[5])) = htons(0);
    //Send ping
    if (sendto(fd, (const char *)buf, CONNECTIONHEADERS, 0, (struct sockaddr*) &conn->addr_dst, structLen) == SOCKET_ERROR) {
      std::cerr << "Send failure" << std::endl;
      return false;
    }
    lastPing = time(0);

    // Check incoming pings, 10s timeout
    if (conn->established) {
      if (std::chrono::duration<double>(std::chrono::steady_clock::now() - conn->lastPing).count() > 10.000) {
        std::cerr << "Connection lost, ping timeout" << std::endl;
        conn->init();
      }
    }
  }

  return true;
}

static void timer(Connection* conn) {
  bool done = false;

  std::thread netThread = std::thread([&] {
    while (!done) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
      if (!doWriting(conn)) {
        done = true;
      }} });
  std::thread localThread = std::thread([&] {
    while (!done) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
      if (!doWritingLocal(conn)) {
        done = true;
      }} });

  netThread.join();
  localThread.join();
}


static void usage(std::string name)
{
    std::cerr << "Usage: " << name << " <options>"
              << "Options:"  << std::endl
              << "  -h,--help: this help" << std::endl
              << "  -i,--port-in <port> : input port to use"  << std::endl
              << "  -o,--port-out <port> : remote port to connect"  << std::endl
              << "  -a,--addr <ip> : remote ip to connect"  << std::endl
              << std::endl;
}

int main(int argc, char* argv[]) {
  
  // Local and destination ip
  const std::string ipLocal = "0.0.0.0";
  std::string ipRemote = "";
  
  int portOut = 0;
  int portIn = 0;
  DWORD localIp = 0;
  Connection conn;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "-h") || (arg == "--help")) {
      usage(argv[0]);
      return 0;
    } else if ((arg == "-o") || (arg == "--port-out")) {
      if (i + 1 < argc) {
          portOut = atoi(argv[++i]);
      } else {
        std::cerr << "--port-out option requires one argument." << std::endl;
        return EXIT_FAILURE;
      }
    } else if ((arg == "-i") || (arg == "--port-in")) {
      if (i + 1 < argc) {
        portIn = atoi(argv[++i]);
      } else {
        std::cerr << "--port-in option requires one argument." << std::endl;
        return EXIT_FAILURE;
      }  
    } else if ((arg == "-a") || (arg == "--addr")) {
      if (i + 1 < argc) {
        ipRemote = std::string(argv[++i]);
      } else {
        std::cerr << "--addr option requires one argument." << std::endl;
        return EXIT_FAILURE;
      }  
    } else if ((arg == "-l") || (arg == "--local-ip")) {
      if (i + 1 < argc) {
        std::string ip = argv[++i];
        for (int ii = 0; ii < 4; ii++) {
          localIp |= ((hexToByte(ip[ii*2])<<4)+hexToByte(ip[ii*2+1]))<<(8*(3-ii));
        }
      } else {
        std::cerr << "--local-ip option requires one argument." << std::endl;
        return EXIT_FAILURE;
      }  
    } else if ((arg == "-v") || (arg == "--verbose")) {
      conn.verbose = true;
    }
  }

  if (portOut == 0 || portIn == 0 || ipRemote == "") {
    std::cerr << portOut << " " << portIn << " " << ipRemote << std::endl;
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  printf("Local ip %x\n",localIp);

  // Input/output buffer
  std::string guid = "";
  int fd;
 
  guid = getGuid();
  if (guid == "") {
    std::cout << "TAP device not found!" << std::endl;
    return EXIT_FAILURE;
  }



  std::cout << "guid " << getGuid() << std::endl;

  std::string device = std::string(USERMODEDEVICEDIR+guid+TAP_WIN_SUFFIX);

	HANDLE fp= CreateFile(std::wstring(device.begin(), device.end()).c_str(),GENERIC_READ | GENERIC_WRITE,0,
                        NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, NULL);


	DWORD readlen;
  DWORD code = 1;

  OVERLAPPED overlapped;
  ZeroMemory(&overlapped, sizeof (overlapped));
  

	DeviceIoControl (fp, TAP_WIN_IOCTL_SET_MEDIA_STATUS, &code, 4, &code, 4, (LPDWORD)&readlen, NULL);
  DWORD code2[3] = {localIp /*0x0100030a*/, 0x0000030a, 0x00ffffff};
	DeviceIoControl (fp, TAP_WIN_IOCTL_CONFIG_TUN, code2, 12,	code2, 12, (LPDWORD)&readlen, NULL);


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
  addr_src.sin_port        = htons(portIn);

  // Bind source port
  if (bind(fd, (sockaddr*)&addr_src, sizeof(addr_src)) < 0)  {
    std::cerr << "Bind failure" << std::endl;
    return EXIT_FAILURE;
  }

  // Set destination port and host
  memset(&addr_dst, 0, sizeof(addr_dst));
  addr_dst.sin_family = AF_INET;
  addr_dst.sin_port = htons(portOut);
  addr_dst.sin_addr.s_addr = inet_addr(ipRemote.c_str());

  /*
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
  */
  conn.addr_src = addr_src;
  conn.addr_dst = addr_dst;

  conn.setDevice(fp);
  conn.setSocket(fd);

  std::thread readThread(syncRead, &conn);
  std::thread readSocketThread(syncReadSocket, &conn);
  std::thread writeThread(timer, &conn);
    
  readThread.join();
  writeThread.join();
  readSocketThread.join();

  return EXIT_SUCCESS;
}