#include <iostream>
#include <array>

#include "ucx_send_recv.hpp"

// Client main function.
int main(int argc, char **argv) {
  std::cout << "Starting client.." << std::endl;
  UCXClient client;
  try {
    client.Connect("localhost", 12353);
    std::array<uint32_t, 10> send_data{};
    for (uint16_t i = 0; i < 10; i++) {
      send_data[i] = i + 10;
    }
    void *request = client.SendMessage(send_data.data(), 40);
    client.WaitForCompletion(request);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }
}