#include <iostream>
#include <array>

#include "ucx_send_recv.hpp"

// Server main function.
int main(int argc, char **argv) {
  std::cout << "Starting server" << std::endl;
  UCXServer server;
  // create a server and wait for client to connect
  server.StartServer("localhost", 12353);
  while (!server.IsClientConnected()) {
    server.ProgressWorker();
  }

  // allocate a buffer for receiving data
  std::array<uint32_t, 10> recv_data{0};
  void *request = server.ReceiveMessage(recv_data.data(), 40);
  server.WaitForCompletion(request);

  // print the received data
  for (uint64_t i: recv_data) {
    std::cout << i << " ";
  }
  std::cout << std::endl;
}