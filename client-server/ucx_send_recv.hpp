#pragma once

#include <cstring>
#include <stdexcept>

#include <ucp/api/ucp.h>
#include <ucp/api/ucp_def.h>
#include <arpa/inet.h> /* inet_addr */
#include <iostream>

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
   * @brief Progress the worker.
   *
   * This function progresses the worker to handle outstanding operations.
   */
  void ProgressWorker();

private:
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

protected:
  ucp_worker_h ucp_worker_; /**< UCX worker handle */
  ucp_context_h ucp_context_; /**< UCX context handle */

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
  void *SendMessage(void *msg, size_t msg_length);

  /**
   * @brief Destructor for UCXClient.
   */
  ~UCXClient() override;

private:
  ucp_ep_h client_ep_; /**< UCX client endpoint handle */
};

/**
 * @brief UCX server class.
 *
 * This class provides the server-side functionality for UCX operations,
 * including starting the server, checking client connections, and receiving messages.
 */
class UCXServer : public UCXBase {
public:
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
  bool IsClientConnected();

  /**
   * @brief Destructor for UCXServer.
   */
  ~UCXServer() override;

  /**
   * @brief Receive a message from the client.
   *
   * @param msg The buffer to receive the message.
   * @param msg_length The length of the message buffer.
   * @return A request handle for the receive operation.
   */
  void *ReceiveMessage(void *msg, size_t msg_length);

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
};


