#include "ucx_send_recv.hpp"

#include <ucp/api/ucp.h>

/**
 * Error handling callback.
 */
static void err_cb(void *arg, ucp_ep_h ep, ucs_status_t status) {
  std::cerr << "error handling callback was invoked with status " << ucs_status_string(status) << std::endl;
}

static void server_conn_handle_cb(ucp_conn_request_h conn_request, void *arg);

void UCXBase::CreateUcpContext() {
  ucp_params_t ucp_params;
  memset(&ucp_params, 0, sizeof(ucp_params));
  // the filed features is populated
  ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;
  // we are using the tag matching feature
  ucp_params.features = UCP_FEATURE_TAG;
  ucp_params.mt_workers_shared = 1;

  // initialize the configuration first, this will read the configuration from the environment and use
  // defaults for not specified configs
  ucp_config_t *config;
  ucs_status_t status = ucp_config_read(nullptr, nullptr, &config);
  if (status != UCS_OK) {
    throw std::runtime_error("UCX configuration failed");
  }
  // now initialize the context
  status = ucp_init(&ucp_params, config, &ucp_context_);
  ucp_config_release(config);
  if (status != UCS_OK) {
    throw std::runtime_error("UCX initialization failed");
  }
}

void UCXBase::CreateUcpWorker() {
  ucp_worker_params_t worker_params;
  std::memset(&worker_params, 0, sizeof(worker_params));
  // weather multiple threads can call ucx functions concurrently
  worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
  worker_params.thread_mode = UCS_THREAD_MODE_MULTI;

  ucs_status_t status = ucp_worker_create(ucp_context_, &worker_params, &ucp_worker_);
  if (status != UCS_OK) {
    throw std::runtime_error("UCX worker creation failed");
  }
}

void UCXBase::PrepareSocketAddress(struct sockaddr_storage *saddr, const char *address_str, uint16_t server_port) {
  struct sockaddr_in *sa_in;
  /* The server will listen on INADDR_ANY */
  memset(saddr, 0, sizeof(*saddr));
  sa_in = (struct sockaddr_in *) saddr;
  if (address_str != nullptr) {
    inet_pton(AF_INET, address_str, &sa_in->sin_addr);
  } else {
    sa_in->sin_addr.s_addr = INADDR_ANY;
  }
  sa_in->sin_family = AF_INET;
  sa_in->sin_port = htons(server_port);
}

ucs_status_t UCXBase::WaitForCompletion(void *request) {
  ucs_status_t status;
  /* if operation was completed immediately */
  if (request == nullptr) {
    return UCS_OK;
  }

  if (UCS_PTR_IS_ERR(request)) {
    std::cout << "Error " << std::endl;
    return UCS_PTR_STATUS(request);
  }

  // while there is work progress
  while (true) {
    unsigned int progress_made;
    do {
      progress_made = ucp_worker_progress(ucp_worker_);
    } while (progress_made != 0);

    status = ucp_request_check_status(request);
    if (status == UCS_OK) {
      ucp_request_free(request);
      break;
    } else if (status != UCS_INPROGRESS) {
      throw std::runtime_error("Failed to process request");
    }
  }
  return status;
}

UCXBase::~UCXBase() {
  if (ucp_worker_ != nullptr) {
    ucp_worker_destroy(ucp_worker_);
  }
  if (ucp_context_ != nullptr) {
    ucp_cleanup(ucp_context_);
  }
}

void *UCXClient::SendMessage(void *msg, size_t msg_length) {
  ucp_request_param_t param = {};
  param.op_attr_mask = UCP_OP_ATTR_FIELD_DATATYPE;
  param.datatype = ucp_dt_make_contig(1);
  return ucp_tag_send_nbx(client_ep_, msg, msg_length, 0, &param);
}

void UCXClient::Connect(const std::string &address_str, uint16_t server_port) {
  ucp_ep_params_t ep_params;
  struct sockaddr_storage connect_addr{};
  ucs_status_t status;
  // create the socket address to the server
  PrepareSocketAddress(&connect_addr, address_str.c_str(), server_port);
  ep_params.field_mask = UCP_EP_PARAM_FIELD_FLAGS | UCP_EP_PARAM_FIELD_SOCK_ADDR |
                         UCP_EP_PARAM_FIELD_ERR_HANDLER | UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE;
  ep_params.err_mode = UCP_ERR_HANDLING_MODE_PEER;
  ep_params.err_handler.cb = err_cb;
  ep_params.err_handler.arg = nullptr;
  ep_params.flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
  ep_params.sockaddr.addr = (struct sockaddr *) &connect_addr;
  ep_params.sockaddr.addrlen = sizeof(connect_addr);

  status = ucp_ep_create(ucp_worker_, &ep_params, &client_ep_);
  if (status != UCS_OK) {
    throw std::runtime_error("failed to create an endpoint");
  }
}

UCXClient::~UCXClient() {
  if (client_ep_ != nullptr) {
    ucp_ep_close_nb(client_ep_, UCP_EP_CLOSE_MODE_FLUSH);
  }
}

void *UCXServer::ReceiveMessage(void *msg, size_t msg_length) {
  ucp_request_param_t param = {};
  param.op_attr_mask = UCP_OP_ATTR_FIELD_DATATYPE;
  param.datatype = ucp_dt_make_contig(1);
  return ucp_tag_recv_nbx(ucp_worker_, msg, msg_length, 0, 0xFFFFFFFFFFFFFFFFULL,
                          &param);
}

void UCXServer::StartServer(const std::string &address_str, uint16_t server_port) {
  struct sockaddr_storage listen_addr{};
  ucp_listener_params_t params = {};
  ucs_status_t status;
  // prepare the socket address to listen for incoming connections
  PrepareSocketAddress(&listen_addr, address_str.c_str(), server_port);
  params.field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR | UCP_LISTENER_PARAM_FIELD_CONN_HANDLER;
  params.sockaddr.addr = (const struct sockaddr *) &listen_addr;
  params.sockaddr.addrlen = sizeof(listen_addr);
  params.conn_handler.cb = server_conn_handle_cb;
  params.conn_handler.arg = this;

  /* Create a listener on the server side to listen on the given address.*/
  status = ucp_listener_create(ucp_worker_, &params, &listener_);
  if (status != UCS_OK) {
    std::cerr << "Failed to listen " << ucs_status_string(status) << std::endl;
    throw std::runtime_error("Failed to listen");
  }

  // wait for the client to connect, this program only accepts one connection
  while (conn_request_ == nullptr) {
    ucp_worker_progress(ucp_worker_);
  }

  // create the ep at the server side using the connection request
  ucp_ep_params_t ep_params = {};
  ep_params.field_mask = UCP_EP_PARAM_FIELD_ERR_HANDLER | UCP_EP_PARAM_FIELD_CONN_REQUEST;
  ep_params.conn_request = conn_request_;
  ep_params.err_handler.cb = err_cb;
  ep_params.err_handler.arg = nullptr;
  // create the endpoint on the server side
  status = ucp_ep_create(ucp_worker_, &ep_params, &server_ep_);
  if (status != UCS_OK) {
    std::cerr << "Failed to create an endpoint on the server: " << ucs_status_string(status) << std::endl;
  }

  std::cout << "Connection established..." << std::endl;
}

UCXServer::~UCXServer() {
  if (server_ep_ != nullptr) {
    ucp_ep_close_nb(server_ep_, UCP_EP_CLOSE_MODE_FLUSH);
  }
  if (listener_ != nullptr) {
    ucp_listener_destroy(listener_);
  }
}

bool UCXServer::SetServerConnection(ucp_conn_request_h conn_request) {
  if (conn_request_ == nullptr) {
    conn_request_ = conn_request;
    return true;
  } else {
    return false;
  }
}

void UCXServer::RejectConnection(ucp_conn_request_h conn_request) {
  ucs_status_t status = ucp_listener_reject(listener_, conn_request);
  if (status != UCS_OK) {
    fprintf(stderr, "server failed to reject a connection request: (%s)\n",
            ucs_status_string(status));
  }
}

void server_conn_handle_cb(ucp_conn_request_h conn_request, void *arg) {
  auto *send_receive = static_cast<UCXServer *>(arg);
  ucp_conn_request_attr_t attr;
  ucs_status_t status;

  attr.field_mask = UCP_CONN_REQUEST_ATTR_FIELD_CLIENT_ADDR;
  status = ucp_conn_request_query(conn_request, &attr);
  if (status == UCS_OK) {
    std::cout << "Server received a connection request from client" << std::endl;
  } else if (status != UCS_ERR_UNSUPPORTED) {
    std::cerr << "Failed to query the connection request " <<
              ucs_status_string(status) << std::endl;
  }

  if (!send_receive->SetServerConnection(conn_request)) {
    /* The server is already handling a connection request from a client,
     * reject this new one */
    std::cout << "Rejecting a connection request. "
                 "Only one client at a time is supported." << std::endl;
    send_receive->RejectConnection(conn_request);
  }
}
