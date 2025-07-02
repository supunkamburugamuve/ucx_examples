#include "ucx_send_recv.hpp"

#include <ucp/api/ucp.h>
#include <fmt/format.h>

static void err_cb(void *arg, ucp_ep_h ep, ucs_status_t status) {
  std::cerr << "error handling callback was invoked with status " << ucs_status_string(status)
            << std::endl;
}

static void server_conn_handle_cb(ucp_conn_request_h conn_request, void *arg);

UCXSendDescriptor::UCXSendDescriptor(std::shared_ptr<Message> message, uint64_t tag)
: msg(std::move(message)), tag(tag) {
  int number_of_buffers = msg->data.size();
  // create the header, first 4 bytes are the number of data buffers to send
  header.resize(sizeof(int32_t) + sizeof(uint64_t) * number_of_buffers + msg->header.size());
  std::memcpy(header.data(), &number_of_buffers, sizeof(int32_t));
  // copy the size of each buffer
  for (int i = 0; i < number_of_buffers; i++) {
    size_t size = msg->data[i].size();
    std::memcpy(header.data() + sizeof(int32_t) + i * sizeof(uint64_t), &size,
                sizeof(uint64_t));
  }
  // now copy the header
  std::memcpy(header.data() + sizeof(int32_t) + number_of_buffers * sizeof(uint64_t),
              msg->header.data(), msg->header.size());
}

UCXBase::UCXBase() {
  // create the ucp context
  CreateUcpContext();
  // create the ucp worker
  CreateUcpWorker();
}

void UCXBase::CreateUcpContext() {
  ucp_params_t ucp_params;
  memset(&ucp_params, 0, sizeof(ucp_params));
  // the filed features is populated
  ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;
  // we are using the tag matching feature and active message feature
  ucp_params.features = UCP_FEATURE_TAG | UCP_FEATURE_AM;
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

ucs_status_t AmReceiveHandler(void *param_ptr, const void *header, size_t header_length,
                              void *data, size_t length, const ucp_am_recv_param_t *param) {
  auto *server = static_cast<UCXServer *>(param_ptr);
  if (sizeof(ucp_tag_t) != header_length) {
    return UCS_ERR_INVALID_PARAM;
  }

  if (param->recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV) {
    return UCS_ERR_INVALID_PARAM;
  }
  // now decode the header, in this case it contains the tag of the first data buffer
  ucp_tag_t sender_tag = *static_cast<const ucp_tag_t *>(header);

  // now decode the header to get the information about the data buffers
  // first 4 bytes of data is the number of buffers
  int32_t num_buffers;
  std::memcpy(&num_buffers, data, sizeof(int32_t));
  std::vector <uint64_t> buffer_sizes(num_buffers);
  // now copy the sizes of the buffers
  for (int i = 0; i < num_buffers; i++) {
    std::memcpy(&buffer_sizes[i],
                static_cast<const char *>(data) + sizeof(int32_t) + i * sizeof(uint64_t),
                sizeof(uint64_t));
  }
  // now copy the rest of the buffer as header
  std::shared_ptr <Message> msg = std::make_shared<Message>();
  msg->header.resize(buffer_sizes[0]);
  std::memcpy(msg->header.data(), static_cast<const char *>(data) + sizeof(int32_t) +
                                  num_buffers * sizeof(uint64_t), buffer_sizes[0]);
  std::unique_ptr <UCXReceiveDescriptor> ucx_recv_msg = std::make_unique<UCXReceiveDescriptor>(msg,
                                                                                               sender_tag,
                                                                                               buffer_sizes);

  return UCS_OK;
}

void UCXBase::CreateActiveMessageHandler() {
  ucp_am_handler_param_t param;
  param.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_CB |
                     UCP_AM_HANDLER_PARAM_FIELD_ARG | UCP_AM_HANDLER_PARAM_FIELD_FLAGS;
  param.id = am_channel_;
  param.cb = AmReceiveHandler;
  // todo: set the receive data
  param.arg = this;
  param.flags = UCP_AM_FLAG_WHOLE_MSG;
  auto status = ucp_worker_set_am_recv_handler(ucp_worker_, &param);
  if (status != UCS_OK) {
    throw std::runtime_error("Failed to create the message handler");
  }
}

void UCXBase::PrepareSocketAddress(struct sockaddr_storage *saddr, const char *address_str,
                                   uint16_t server_port) {
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

ucs_status_t UCXBase::WaitForCompletionAll(std::vector<void *> requests) {
  ucs_status_t status;
  std::cout << "Waiting for completion of all requests" << std::endl;
  // while there is work progress
  while (true) {
    unsigned int progress_made;
    do {
      progress_made = ucp_worker_progress(ucp_worker_);
    } while (progress_made != 0);

    bool all_completed = true;
    for (auto request: requests) {
      status = ucp_request_check_status(request);
      if (status == UCS_OK) {
      } else if (status != UCS_INPROGRESS) {
        throw std::runtime_error("Failed to process request");
      } else {
        all_completed = false;
      }
    }
    if (all_completed) {
      // free the requests
      for (auto request: requests) {
        ucp_request_free(request);
      }
      break;
    }
  }
  return status;
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
    unsigned int progress_made = 0;
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

ucs_status_ptr_t UCXClient::SendHeader(struct UCXSendDescriptor *msg) {
  // we use an active message to send the metadata
  ucp_request_param_t param = {};
  // we are interested in setting memory type to use for the active message and
  // the datatype
  param.op_attr_mask = UCP_OP_ATTR_FIELD_MEMORY_TYPE | UCP_OP_ATTR_FIELD_FLAGS |
                       UCP_OP_ATTR_FIELD_DATATYPE;
  param.memory_type = UCS_MEMORY_TYPE_HOST;
  param.flags = UCP_AM_SEND_FLAG_EAGER | UCP_AM_SEND_FLAG_REPLY;
  param.datatype = ucp_dt_make_contig(1);
  std::cout << "Sending header with tag " << msg->GetTag() << std::endl;
  // send the header of the active message the tag
  // send the actual header of the message as the data
  return ucp_am_send_nbx(client_ep_, am_channel_, msg->GetTagAddr(),
                         sizeof(msg->GetTag()), msg->GetHeader().data(),
                         msg->GetHeader().size(), &param);
}

std::unique_ptr <UCXSendDescriptor> UCXClient::SendMessage(std::shared_ptr<Message> msg) {
  std::vector<void *> requests;
  // send the header
  // we add the number of tag so we reserve them
  uint64_t tag_start = tag_.fetch_add(msg->data.size(), std::memory_order_relaxed);
  std::unique_ptr <UCXSendDescriptor> ucx_msg = std::make_unique<UCXSendDescriptor>(msg,
                                                                                    tag_start);
  // send the header as an active message
  auto r = SendHeader(ucx_msg.get());
  if (r != nullptr) {
    requests.push_back(r);
  }

  std::cout << "Sending buffers " << msg->data.size() << std::endl;
  // send the data
  for (int i = 0; i < msg->data.size(); i++) {
    ucp_request_param_t param = {};
    param.op_attr_mask = UCP_OP_ATTR_FIELD_DATATYPE;
    param.datatype = ucp_dt_make_contig(1);
    // use incrementing tags from tag start
    r = ucp_tag_send_nbx(client_ep_, msg->data[i].data(), msg->data[i].size(), tag_start + i,
                         &param);
    if (r != nullptr) {
      requests.push_back(r);
    }
  }
  ucx_msg->SetRequests(std::move(requests));
  return ucx_msg;
}

void UCXClient::Connect(const std::string &address_str, uint16_t server_port) {
  ucp_ep_params_t ep_params;
  struct sockaddr_storage connect_addr{};
  ucs_status_t status;
  // create the socket address to the server
  PrepareSocketAddress(&connect_addr, address_str.c_str(), server_port);
  ep_params.field_mask = UCP_EP_PARAM_FIELD_FLAGS | UCP_EP_PARAM_FIELD_SOCK_ADDR |
                         UCP_EP_PARAM_FIELD_ERR_HANDLER | UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE;
  ep_params.err_mode = UCP_ERR_HANDLING_MODE_NONE;
  ep_params.err_handler.cb = err_cb;
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

void UCXServer::MessageReceived(std::unique_ptr <UCXReceiveDescriptor> msg) {
  std::vector<void *> requests;
  for (int i = 0; i < msg->GetBufferSizes().size(); i++) {
    ucp_request_param_t param = {};
    param.op_attr_mask = UCP_OP_ATTR_FIELD_DATATYPE;
    param.datatype = ucp_dt_make_contig(1);
    msg->GetMessage()->data[i].resize(msg->GetBufferSizes()[i]);
    auto r = ucp_tag_recv_nbx(ucp_worker_, msg->GetMessage()->data[i].data(),
                              msg->GetBufferSizes()[i], msg->GetTag() + i, 0xFFFFFFFFFFFFFFFFULL,
                              &param);
    requests.push_back(r);
  }
  // wait for all messages to arrive
  WaitForCompletionAll(requests);
  receive_func_(msg->GetMessage());
}

void UCXServer::StartServer(const std::string &address_str, uint16_t server_port) {
  struct sockaddr_storage listen_addr{};
  ucp_listener_params_t params = {};
  ucs_status_t status;
  // create the am handler
  CreateActiveMessageHandler();
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
  ep_params.err_mode = UCP_ERR_HANDLING_MODE_PEER;
  ep_params.err_handler.cb = err_cb;
  // create the endpoint on the server side
  status = ucp_ep_create(ucp_worker_, &ep_params, &server_ep_);
  if (status != UCS_OK) {
    std::cerr << "Failed to create an endpoint on the server: " << ucs_status_string(status)
              << std::endl;
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
