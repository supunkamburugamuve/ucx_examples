#pragma once

#include <cstring>
#include <stdexcept>

#include <ucp/api/ucp.h>
#include <ucp/api/ucp_def.h>
#include <arpa/inet.h> /* inet_addr */
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <functional>

/** The user message */
struct Message {
  std::vector<uint8_t> header;
  std::vector<std::vector<uint8_t>> data;
};

class UCXReceiveDescriptor {
public:
  /**
   * @brief Constructor for UCXReceiveDescriptor.
   *
   * Initializes the message with a given tag and header.
   *
   * @param msg The message to be received.
   * @param tag The tag for the message.
   */
  UCXReceiveDescriptor(std::shared_ptr <Message> msg, uint64_t tag,
                       std::vector <uint64_t> buffer_sizes)
      : msg_(std::move(msg)), tag_(tag), buffer_sizes_(std::move(buffer_sizes)) {}

  /**
   * @brief Destructor for UCXReceiveDescriptor.
   */
  ~UCXReceiveDescriptor() = default;

  /**
   * @brief Get the message data.
   *
   * @return The message data.
   */
  std::shared_ptr<Message> GetMessage() {
    return msg_;
  }

  /**
   * @brief Get the buffer sizes.
   *
   * @return The message data.
   */
  std::vector<uint64_t> GetBufferSizes() {
    return buffer_sizes_;
  }

  /**
   * @brief Get the tag of the message.
   *
   * @return The tag of the message.
   */
  const uint64_t GetTag() const {
    return tag_;
  }
private:
  std::shared_ptr<Message> msg_;
  uint64_t tag_;
  // the header with additional data
  std::vector<uint64_t> buffer_sizes_;
};

/**
 * @brief Structure to hold UCX message information.
 *
 * This structure contains a pointer to a Message object and a vector of
 * bytes representing the header.
 */
class UCXSendDescriptor {
public:
  /**
   * @brief Constructor for UCXSendDescriptor.
   *
   * Initializes the message with a given tag and header.
   *
   * @param msg The message to be sent.
   * @param tag The tag for the message.
   */
  UCXSendDescriptor(std::shared_ptr<Message> msg, uint64_t tag);

  /**
   * @brief Destructor for UCXSendDescriptor.
   */
  ~UCXSendDescriptor() = default;

  /**
   * @brief Get the message data.
   *
   * @return The message data.
   */
  std::shared_ptr<Message> GetMessage() {
    return msg;
  }

  /**
   * @brief Get the tag of the message.
   *
   * @return The tag of the message.
   */
  const uint64_t* GetTagAddr() const {
    return &tag;
  }

  /**
   * @brief Get the tag of the message.
   *
   * @return The tag of the message.
   */
  uint64_t GetTag() const {
    return tag;
  }
  /**
   * Set the requests for the message.
   */
  void SetRequests(std::vector<void*> requests) {
    this->requests = std::move(requests);
  }

  /**
   * Get the requests for the message.
   *
   * @return The requests for the message.
   */
  std::vector<void*> GetRequests() const {
    return requests;
  }

  /**
   * @brief Get the header of the message.
   *
   * @return The header of the message.
   */
  std::vector<uint8_t> GetHeader() const {
    return header;
  }
private:
  std::shared_ptr<Message> msg;
  uint64_t tag;
  std::vector<void*> requests;
  // the header with additional data
  std::vector<uint8_t> header;
};

/**
 * @brief Base class for UCX operations.
 *
 * This class provides the basic setup for UCX operations, including creating
 * the UCX context and worker. It also provides utility functions for waiting
 * for request completion and progressing the worker.
 */
class UCXBase {
public:
  /**
   * @brief Constructor for UCXBase.
   *
   * Initializes the UCX context and worker.
   */
  explicit UCXBase();

  /**
   * @brief Destructor for UCXBase.
   */
  virtual ~UCXBase();

  /**
   * @brief Wait until the request is completed.
   *
   * @param request The request to wait for.
   * @return Status of the request completion.
   */
  ucs_status_t WaitForCompletion(void *request);

  /**
   * @brief Wait until all requests are completed.
   *
   * @param requests The requests to wait for.
   * @return Status of the request completion.
   */
  ucs_status_t WaitForCompletionAll(std::vector<void*> requests);

  /**
   * @brief Progress the worker.
   *
   * This function progresses the worker to handle outstanding operations.
   */
  void ProgressWorker() {
    ucp_worker_progress(ucp_worker_);
  }

protected:
  /**
   * @brief Create the UCX context.
   *
   * This function initializes the UCX context.
   */
  void CreateUcpContext();

  /**
   * @brief Create the UCX worker.
   *
   * This function initializes the UCX worker.
   */
  void CreateUcpWorker();

  /**
   * @brief Create the active message handler.
   *
   * This function initializes the active message handler.
   */
  void CreateActiveMessageHandler();

  ucp_worker_h ucp_worker_; /**< UCX worker handle */
  ucp_context_h ucp_context_; /**< UCX context handle */
  const uint32_t am_channel_{26};

  /**
   * @brief Prepare a socket address for the server to listen on.
   *
   * This function sets an address for the server to listen on, using INADDR_ANY
   * on a well-known port.
   *
   * @param saddr The socket address structure to prepare.
   * @param address_str The address string.
   * @param server_port The server port.
   */
  static void PrepareSocketAddress(struct sockaddr_storage *saddr, const char *address_str, uint16_t server_port);
};

/**
 * @brief UCX client class.
 *
 * This class provides the client-side functionality for UCX operations,
 * including connecting to a server and sending messages.
 */
class UCXClient : public UCXBase {
public:
  /** Constructor for UCXClient. */
  UCXClient() : UCXBase(), client_ep_(nullptr) {}
  /**
   * @brief Connect to a remote server.
   *
   * This function creates an endpoint from the client side to be connected to
   * the remote server at the given IP address and port.
   *
   * @param address_str The IP address of the server.
   * @param server_port The port of the server.
   */
  void Connect(const std::string &address_str, uint16_t server_port);

  /**
   * @brief Send a message to the server.
   *
   * @param msg The message to send.
   * @param msg_length The length of the message.
   * @return A request handle for the send operation.
   */
  std::unique_ptr<UCXSendDescriptor> SendMessage(std::shared_ptr<Message> msg);

  /**
   * @brief Destructor for UCXClient.
   */
  ~UCXClient() override;

private:
  /**
   * @brief Send a header to the server.
   *
   * @param msg The message to send.
   * @return A request handle for the send operation.
   */
  ucs_status_ptr_t SendHeader(struct UCXSendDescriptor *msg);

  ucp_ep_h client_ep_; /**< UCX client endpoint handle */
  /* Incrementing tag for each message */
  std::atomic<uint64_t> tag_{0};
};

/**
 * @brief UCX server class.
 *
 * This class provides the server-side functionality for UCX operations,
 * including starting the server, checking client connections, and receiving messages.
 */
class UCXServer : public UCXBase {
public:
  /** Constructor for UCXServer. */
  UCXServer() : UCXBase(), server_ep_(nullptr), listener_(nullptr), conn_request_(nullptr) {}
  /**
   * @brief Start the server to listen on the given address and port.
   *
   * @param address_str The address to listen on.
   * @param server_port The port to listen on.
   */
  void StartServer(const std::string &address_str, uint16_t server_port);

  /**
   * @brief Check if the client is connected to the server.
   *
   * @return True if the client is connected, false otherwise.
   */
  bool IsClientConnected() {
    return conn_request_ != nullptr;
  }

  /**
   * @brief Destructor for UCXServer.
   */
  ~UCXServer() override;

  void MessageReceived(std::unique_ptr<UCXReceiveDescriptor> msg);

  /**
   * @brief register a function to receive messages
   */
  void ReceiveMessage(std::function<void(std::shared_ptr<Message> msg)> func) {
    receive_func_ = std::move(func);
  }

  /**
   * @brief Reject a connection request from a client.
   *
   * @param conn_request The connection request to reject.
   */
  void RejectConnection(ucp_conn_request_h conn_request);

  /**
   * @brief Set the server connection with a client.
   *
   * @param conn_request The connection request from the client.
   * @return True if the connection was successfully set, false otherwise.
   */
  bool SetServerConnection(ucp_conn_request_h conn_request);

private:
  ucp_ep_h server_ep_; /**< UCX server endpoint handle */
  ucp_listener_h listener_; /**< UCX listener handle */
  ucp_conn_request_h conn_request_; /**< UCX connection request handle */
  std::function<void(std::shared_ptr<Message> msg)> receive_func_; /**< Function to receive messages */
};


