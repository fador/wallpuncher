/*
  Copyright (c) 2016, Marko Viitanen (Fador)

  Permission to use, copy, modify, and/or distribute this software for any purpose 
  with or without fee is hereby granted, provided that the above copyright notice 
  and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH 
  REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY 
  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, 
  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
  LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
  OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR 
  PERFORMANCE OF THIS SOFTWARE.

*/

#include <thread>
#include <vector>
#include <cstdint>
#include <mutex>
#include <chrono>



class Connection {



public:
  Connection() :inFrame(0), outFrame(0), established(false), verbose(false),lastPing(std::chrono::steady_clock::now()) {};

  Connection(const Connection &obj) {};

  typedef struct { uint32_t frame; uint8_t* data; uint32_t dataLen; std::chrono::steady_clock::time_point sent; } sentFrame;
  typedef struct { uint32_t frame; uint8_t* data; uint32_t dataLen; } receivedFrame;

  void init() {
    inFrame = 0;
    outFrame = 0;
    established = false;    
  }

  bool addReceived(Connection::receivedFrame& newFrame) {
    std::unique_lock<std::mutex> inputLock(inputMutex);
    int i;

    // Check if the frame already exists and remove if it does
    for (i = 0; i < receivedFrames.size(); i++) {
      if (receivedFrames[i].frame == newFrame.frame) {
        delete [] newFrame.data;
        return true;
      }
    }

    // Arrange in frame order
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

    // Don't add duplicate frames (should not be posssible)
    for (i = 0; i < sentFrames.size(); i++) {
      if (sentFrames[i].frame == newFrame.frame) {
        delete [] newFrame.data;
        return true;
      }
    }

    sentFrames.push_back(newFrame);

    return true;
  }

  bool ack(uint32_t frame) {
    std::unique_lock<std::mutex> outputLock(outputMutex);
    int i;

    // We got an ack from the network, we can remove the associated frame
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
    // Add an ack to send
    toAck.push_back(frame);

    return false;
  }

  bool getAcks(std::vector<uint32_t> &ackOut) {
    std::unique_lock<std::mutex> outputLock(ackMutex);
    // Just grab all the acks to push to the network
    for (uint32_t ack : toAck) {
      ackOut.push_back(ack);
    }
    toAck.clear();

    return true;
  }

  bool sendReceivedToLocal(std::vector<Connection::receivedFrame> &toLocal) {
    std::unique_lock<std::mutex> inputLock(inputMutex);
    int toRemove = 0;

    // Just to make sure packets are sent to the local network in the order they were read in the other network
    for (int i = 0; i < receivedFrames.size(); i++) {
      if (receivedFrames[i].frame != inFrame) {
        break;
      }
      toLocal.push_back(receivedFrames[i]);
      toRemove ++;
      inFrame ++;
    }
    if (toRemove) {
      receivedFrames.erase(receivedFrames.begin(), receivedFrames.begin() + toRemove);
    }

    return true;
  }
  

  bool resendSent(std::vector<Connection::sentFrame> &toNet) {
    std::unique_lock<std::mutex> outputLock(outputMutex);

    // Grab a list of frames to be sent to the network
    for (auto &frame : sentFrames) {
      // Send again in 200ms intervals until we get ack
      if (std::chrono::duration<double>(std::chrono::steady_clock::now() - frame.sent).count() > 0.200) {
        toNet.push_back(frame);
        // Update sent time
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
    std::unique_lock<std::mutex> outputLock(outputMutex);
    return outFrame++;
  }

  uint32_t getInFrame() {
    return inFrame;
  }

  // Received ping, set connection activate state and update last ping time
  void ping() {    
    if (!established) std::cerr << "Connection established!!" << std::endl;
    established = true;
    lastPing = std::chrono::steady_clock::now();
  }


  bool established;

  bool verbose;

  uint32_t sentToLocal;
  uint32_t sentToNet;

  uint32_t receivedFromNet;
  uint32_t receivedFromLocal;

  std::chrono::steady_clock::time_point lastPing;

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