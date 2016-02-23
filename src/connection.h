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

#include <thread>
#include <vector>
#include <cstdint>
#include <mutex>
#include <chrono>



class Connection {



public:
  Connection() :inFrame(0), outFrame(0), established(false) {};

  Connection(const Connection &obj) {};

  typedef struct { uint32_t frame; uint8_t* data; uint32_t dataLen; std::chrono::steady_clock::time_point sent; } sentFrame;
  typedef struct { uint32_t frame; uint8_t* data; uint32_t dataLen; } receivedFrame;

  bool addReceived(Connection::receivedFrame& newFrame) {
    std::unique_lock<std::mutex> inputLock(inputMutex);
    int i;

    for (i = 0; i < receivedFrames.size(); i++) {
      if (receivedFrames[i].frame == newFrame.frame) {
        delete [] newFrame.data;
        return true;
      }
    }

    for (i = 0; i < receivedFrames.size(); i++) {
      if (receivedFrames[i].frame > newFrame.frame) {
        break;
      }
    }
    if (i == receivedFrames.size()) {
      receivedFrames.push_back(newFrame);
    } else {
      receivedFrames.insert(receivedFrames.begin()+i, newFrame);
    }

    return true;
  }

  bool addSent(Connection::sentFrame& newFrame) {
    std::unique_lock<std::mutex> outputLock(outputMutex);
    int i;

    for (i = 0; i < sentFrames.size(); i++) {
      if (sentFrames[i].frame == newFrame.frame) {
        break;
      }
    }

    sentFrames.push_back(newFrame);

    return true;
  }

  bool ack(uint32_t frame) {
    std::unique_lock<std::mutex> outputLock(outputMutex);
    int i;

    for (i = 0; i < sentFrames.size(); i++) {
      if (sentFrames[i].frame == frame) {
        sentFrames.erase(sentFrames.begin()+i);
        return true;
      }
    }

    return false;
  }

  bool sendAck(uint32_t frame) {
    std::unique_lock<std::mutex> outputLock(ackMutex);
    toAck.push_back(frame);

    return false;
  }

  bool getAcks(std::vector<uint32_t> &ackOut) {
    std::unique_lock<std::mutex> outputLock(ackMutex);
    for (uint32_t ack : toAck) {
      ackOut.push_back(ack);
    }
    toAck.clear();

    return true;
  }

  bool sendReceivedToLocal(std::vector<Connection::receivedFrame> &toLocal) {
    std::unique_lock<std::mutex> inputLock(inputMutex);
    int toRemove = 0;

    for (int i = 0; i < receivedFrames.size(); i++) {
      if (receivedFrames[i].frame != inFrame) {
        break;
      }
      toLocal.push_back(receivedFrames[i]);
      toRemove ++;
      inFrame ++;
    }

    receivedFrames.erase(receivedFrames.begin(), receivedFrames.begin() + toRemove);

    return true;
  }
  

  bool resendSent(std::vector<Connection::sentFrame> &toNet) {
    std::unique_lock<std::mutex> outputLock(outputMutex);
    for (auto frame : sentFrames) {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frame.sent).count() > 200) {
        toNet.push_back(frame);
        frame.sent = std::chrono::steady_clock::now();
      }
    }

    return true;
  }

  HANDLE getDevice() { return device; }
  bool setDevice(HANDLE _device) { device = _device; return true; }

  int getSocket() { return socket; }
  bool setSocket(int _socket) { socket = _socket; return true; }


  sockaddr_in addr_src;
  sockaddr_in addr_dst;

  uint32_t getIncOutFrame() {
    return outFrame++;
  }

  uint32_t getInFrame() {
    return inFrame;
  }

  void ping() {
    if (!established) std::cerr << "Connection established!!" << std::endl;
    established = true;
  }


  bool established;

private:

  std::vector<uint32_t> toAck;
  std::vector<Connection::sentFrame> sentFrames;
  std::vector<Connection::receivedFrame> receivedFrames;

  HANDLE device;

  int socket;
  std::mutex inputMutex;
  std::mutex outputMutex;

  std::mutex ackMutex;

  uint32_t inFrame;
  uint32_t outFrame;
};