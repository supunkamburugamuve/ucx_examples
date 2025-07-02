#include <iostream>
#include <array>

#include "ucx_send_recv.hpp"

// Client main function.
int main(int argc, char **argv) {
  std::cout << "Starting client.." << std::endl;
  UCXClient client;
  try {
    client.Connect("localhost", 13458);
    std::shared_ptr<Message> msg = std::make_shared<Message>();
    msg->header.resize(4);
    msg->data.resize(1);
    msg->data[0].resize(40);
    std::unique_ptr<UCXSendDescriptor> requests = client.SendMessage(msg);
    std::cout << "Sent message with header size: " << msg->header.size() << std::endl;
    client.WaitForCompletionAll(requests->GetRequests());
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }
}