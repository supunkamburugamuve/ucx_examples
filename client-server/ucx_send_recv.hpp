#pragma once

#include <cstring>
#include <stdexcept>

#include <ucp/api/ucp.h>
#include <ucp/api/ucp_def.h>
#include <arpa/inet.h> /* inet_addr */
#include <iostream>

class UCXBase {
public:
  explicit UCXBase() {
    // create the ucp context
    CreateUcpContext();
    // create the ucp worker
    CreateUcpWorker();
  }

  virtual ~UCXBase();

  /**
   * Wait until the request is completed.
   */
  ucs_status_t WaitForCompletion(void *request);

  /**
   * Progress the worker.
   */
  void ProgressWorker() {
    ucp_worker_progress(ucp_worker_);
  }

private:
  void CreateUcpContext();
  void CreateUcpWorker();

protected:
  ucp_worker_h ucp_worker_ = nullptr;
  ucp_context_h ucp_context_ = nullptr;

  /**
    * Set an address for the server to listen on - INADDR_ANY on a well known port.
    */
  static void PrepareSocketAddress(struct sockaddr_storage *saddr, const char *address_str, uint16_t server_port);
};

class UCXClient : public UCXBase {
public:
  /**
   * Initialize the client side. Create an endpoint from the client side to be
   * connected to the remote server (to the given IP).
   */
  void Connect(const std::string &address_str, uint16_t server_port);

  /*
   * Send a message to the server.
   */
  void *SendMessage(void *msg, size_t msg_length);

  ~UCXClient() override;

private:
  ucp_ep_h client_ep_ = nullptr;
};

class UCXServer : public UCXBase {
public:
  /**
   * Start the server to listen on the given address and port.
   * @param address_str
   * @param server_port
   */
  void StartServer(const std::string &address_str, uint16_t server_port);

  /**
   * Check if the client is connected to the server.
   */
  bool IsClientConnected() {
    return conn_request_ != nullptr;
  }

  ~UCXServer() override;

  /**
   * Receive a message from the client.
   * @param msg
   * @param msg_length
   * @return
   */
  void *ReceiveMessage(void *msg, size_t msg_length);

  void RejectConnection(ucp_conn_request_h conn_request);

  bool SetServerConnection(ucp_conn_request_h conn_request);
private:
  ucp_ep_h server_ep_ = nullptr;
  ucp_listener_h listener_ = nullptr;
  ucp_conn_request_h conn_request_ = nullptr;
};


