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

static void winerror(const char* message)
{
	int err = GetLastError();
	fprintf(stderr, "%s: (%d) ", message, err);

	char buf[1024];
	if (!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (wchar_t*)buf, sizeof buf, NULL))
		strncpy_s(buf, sizeof(buf), "(unable to format errormessage)", _TRUNCATE);
	fprintf(stderr, "%s", buf);

	exit(1);
}



static void sync(HANDLE device)
{
	char packet[1518];
	DWORD packetlen;
  while (1) {
	  printf("Reading one packet\n");
	  if (ReadFile(device, packet, sizeof packet, &packetlen, NULL) == 0)
		  winerror("Unable to read packet");
    if (packet[0] == 0x40) {
      printf("Successfully read one packet of size %d\n", packetlen);
      
      for (int i = 0; i< 4; ++i) {
          byte tmp = packet[12+i]; packet[12+i] = packet[16+i]; packet[16+i] = tmp;
      }
	    
      
	    printf("Writing the packet back\n");
	    DWORD writelen;
	    if (WriteFile(device, packet, packetlen, &writelen, NULL) == 0)
		    winerror("Unable to write packet");
	    printf("Successfully wrote %d bytes\n", writelen);
      
    }
  }
}

static void async(HANDLE device)
{
	char packet[1518];
  char writepacket[1518];
	DWORD packetlen;
  while (1) {
	  {
		  printf("Reading one packet\n");
		  OVERLAPPED overlapped = {0};
		  overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		  if (ReadFile(device, packet, sizeof packet, &packetlen, &overlapped) == 0)
		  {
			  if (GetLastError() != ERROR_IO_PENDING)
				  winerror("Unable to read packet");
			  printf("Waiting for read event\n");
			  if (WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0)
				  winerror("Unable to wait on read event");
			  printf("Done waiting for read event, getting overlapped status\n");
			  if (GetOverlappedResult(device, &overlapped, &packetlen, FALSE) == 0)
				  winerror("Unable to get overlapped result");
		  }
		  printf("Successfully read one packet of size %d\n", packetlen);
		  CloseHandle(overlapped.hEvent);
	  }

	  {
		  printf("Writing the packet back\n");
		  OVERLAPPED overlapped = {0};
		  overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		  DWORD writelen;
      for (int i = 0; i< 4; ++i) {
          byte tmp = packet[12+i]; packet[12+i] = packet[16+i]; packet[16+i] = tmp;
      }
      memcpy(writepacket, packet, packetlen);
		  if (WriteFile(device, writepacket, packetlen, &writelen, &overlapped) == 0)
		  {
			  if (GetLastError() != ERROR_IO_PENDING)
				  winerror("Unable to write packet");
			  printf("Waiting for write event\n");
			  if (WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0)
				  winerror("Unable to wait on write event");
			  printf("Done waiting for write event, getting overlapped status\n");
			  if (GetOverlappedResult(device, &overlapped, &writelen, FALSE) == 0)
				  winerror("Unable to get overlapped result");
		  }
		  printf("Successfully wrote %d bytes\n", writelen);
		  CloseHandle(overlapped.hEvent);
	  }
  }
}

#define MATCH_IP(data, ip) (((data)[0] == (ip)[0] && (data)[1] == (ip)[1] && (data)[2] == (ip)[2] && (data)[3] == (ip)[3]))

static void event_loop(HANDLE device)
{
	char packet_read[1518];
	char packet_write[1518];
	DWORD packetlen = 0;
	DWORD writelen;
	HANDLE event_read = CreateEvent(NULL, FALSE, FALSE, NULL);
	HANDLE event_write = CreateEvent(NULL, FALSE, FALSE, NULL);
	OVERLAPPED overlapped_read = {0};
	OVERLAPPED overlapped_write = {0};

  while (1) {
  HANDLE event_read = CreateEvent(NULL, FALSE, FALSE, NULL);
	HANDLE event_write = CreateEvent(NULL, FALSE, FALSE, NULL);
	overlapped_read.hEvent = INVALID_HANDLE_VALUE;
	overlapped_write.hEvent = INVALID_HANDLE_VALUE;
  uint32_t correctIp = 0x0a030005;
  correctIp = htonl(correctIp);
	for (;;)
	{
		if (packetlen > 0 && overlapped_write.hEvent == INVALID_HANDLE_VALUE)
		{
      
      if ((uint8_t)(packet_read[0] & 0xf0) == 0x40) {
        /*
        if(MATCH_IP(&packet_read[16], &correctIp)) {
          for (int i = 0; i< 4; ++i) {
              byte tmp = packet_read[12+i]; packet_read[12+i] = packet_read[16+i]; packet_read[16+i] = tmp;
          }
			    printf("Writing the packet back\n");
			    memcpy(packet_write, packet_read, packetlen);
			    memset(&overlapped_write, 0, sizeof overlapped_write);
			    overlapped_write.hEvent = event_write;
			    if (WriteFile(device, packet_write, packetlen, &writelen, &overlapped_write) != 0)
			    {
				    printf("Successfully wrote %d bytes\n", writelen);
            packetlen = 0;
				    break;
			    }
			    else if (GetLastError() != ERROR_IO_PENDING)
				    winerror("Unable to write packet");
        }
        */
      }
      packetlen = 0;
		}

		if (overlapped_read.hEvent == INVALID_HANDLE_VALUE)
		{
			//printf("Reading one packet\n");
			packetlen = 0;
			memset(&overlapped_read, 0, sizeof overlapped_read);
			overlapped_read.hEvent = event_read;
			if (ReadFile(device, packet_read, sizeof packet_read, &packetlen, &overlapped_read) != 0)
			{
        if (packetlen > 0 && (uint8_t)(packet_read[0] & 0xf0) == 0x40)
        {
          if(MATCH_IP(&packet_read[16], &correctIp)) {
            std::cout << "Read bytes: " << packetlen << std::endl;
            FILE *outfp = fopen("output.bin", "ab+");
            fwrite(packet_read, packetlen, 1, outfp);
            fclose(outfp);
          }
        }
				//printf("Successfully read one packet of size %d\n", packetlen);
				overlapped_read.hEvent = INVALID_HANDLE_VALUE;
				continue;
			}
			else if (GetLastError() != ERROR_IO_PENDING)
				winerror("Unable to read packet");
		}

		printf("Waiting for events\n");
		HANDLE events[] = { event_read, event_write };
		const size_t event_count = sizeof(events) / sizeof(HANDLE);
		DWORD result = WaitForMultipleObjects(event_count, events, FALSE, INFINITE);
		if (result < WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + event_count)
			winerror("Unable to wait for multiple objects");
		result -= WAIT_OBJECT_0;

		if (events[result] == event_read)
		{
			//printf("Read event fired, getting overlapped results\n");
			if (GetOverlappedResult(device, &overlapped_read, &packetlen, FALSE) == 0)
				winerror("Unable to get overlapped result");
     
      
			//printf("Successfully read one packet of size %d\n", packetlen);
      if (packetlen > 0 && (uint8_t)(packet_read[0] & 0xf0) == 0x40) {
        printf("Protocol ver: %x\n", packet_read[0]);
        printf("Src ip: %d.%d.%d.%d\n", (unsigned char)packet_read[12], (unsigned char)packet_read[13], (unsigned char)packet_read[14], (unsigned char)packet_read[15]);
        printf("Dst ip: %d.%d.%d.%d\n", (unsigned char)packet_read[16], (unsigned char)packet_read[17], (unsigned char)packet_read[18], (unsigned char)packet_read[19]);
        if(MATCH_IP(&packet_read[16], (uint8_t*)&correctIp)) {
          std::cout << "Read bytes: " << packetlen << std::endl;
          FILE *outfp = fopen("output.bin", "ab+");
          fwrite(packet_read, packetlen, 1, outfp);
          fclose(outfp);
        }
      }
			overlapped_read.hEvent = INVALID_HANDLE_VALUE;
		}

		if (events[result] == event_write)
		{
			printf("Write event fired, getting overlapped results\n");
			if (GetOverlappedResult(device, &overlapped_write, &writelen, FALSE) == 0)
				winerror("Unable to get overlapped result");
			printf("Successfully wrote %d bytes\n", writelen);
      packetlen = 0;
			break;
		}
	}

	CloseHandle(overlapped_read.hEvent);
	CloseHandle(overlapped_write.hEvent);
  CloseHandle(event_read);
  CloseHandle(event_write);
  }
}

int main(int argc, char* argv[]) {
 
  // Input/output buffer
  #define BUFLEN 1024
  char buf[BUFLEN];
  int len;
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
  DWORD code2[3] = {0x0100030a, 0x0000030a, 0x00ffffff};
	DeviceIoControl (fp, TAP_WIN_IOCTL_CONFIG_TUN, code2, 12,	code2, 12, (LPDWORD)&readlen, NULL);

  DWORD bytesRead = 0;
  #define COPYBUFFERSIZE 4096
  BYTE   buffer[COPYBUFFERSIZE] = { 0 };

  event_loop(fp);
  /*
  do {
    overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (ReadFile(fp, buffer, COPYBUFFERSIZE, &bytesRead, &overlapped) == 0) {
      if (GetLastError() != ERROR_IO_PENDING) {
				std::cerr << "Readfile fail" << std::endl;
        return EXIT_FAILURE;
      }
			printf("Waiting for read event\n");
			if (WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0) {
				std::cerr << "Readfile fail" << std::endl;
        return EXIT_FAILURE;
      }
			printf("Done waiting for read event, getting overlapped status\n");
      if (GetOverlappedResult(fp, &overlapped, &bytesRead, FALSE) == 0) {
				std::cerr << "Readfile fail" << std::endl;
        return EXIT_FAILURE;
      }
    }
    CloseHandle(overlapped.hEvent);

    if (bytesRead > 0) {
      std::cout << "Read bytes: " << bytesRead << std::endl;
      FILE *outfp = fopen("output.bin", "wb+");
      fwrite(buffer, bytesRead, 1, outfp);
      fclose(outfp);
    }
  } while (bytesRead > 0);
  */
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