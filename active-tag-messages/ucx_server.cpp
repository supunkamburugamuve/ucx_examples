#include <iostream>
#include <array>

#include "ucx_send_recv.hpp"

// Server main function.
int main(int argc, char **argv) {
  std::cout << "Starting server" << std::endl;
  UCXServer server;
  // create a server and wait for client to connect
  server.StartServer("localhost", 13458);
  while (!server.IsClientConnected()) {
    server.ProgressWorker();
  }

  // allocate a buffer for receiving data
  server.ReceiveMessage([&](std::shared_ptr<Message> msg) {
    std::cout << "Received message with header size: " << msg->header.size() << std::endl;
    std::cout << "Received message with data size: " << msg->data.size() << std::endl;
  });

  server.ProgressWorker();
}