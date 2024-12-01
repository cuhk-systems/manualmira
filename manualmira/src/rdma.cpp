#include "manualmira/rdma.h"

#include <cstring>
#include <stdexcept>

#include "rdma/rdma_verbs.h"

namespace manualmira::rdma {

connection::~connection() {
  rdma_disconnect(id_);
  for (const auto& mr_buf : mr_bufs_) rdma_dereg_mr(mr_buf.first);
  rdma_destroy_ep(id_);

  if (addr_info_) rdma_freeaddrinfo(addr_info_);
}

std::pair<ibv_mr*, void*> connection::make_mr(std::size_t size) {
  std::vector<std::uint8_t> buf;
  buf.reserve(size);

  void* ptr = buf.data();

  ibv_mr* mr = rdma_reg_msgs(id_, ptr, size);
  if (!mr) return {nullptr, nullptr};

  mr_bufs_[mr] = std::move(buf);

  return {mr, ptr};
}

void connection::destroy_mr(ibv_mr* mr) {
  if (!mr_bufs_.contains(mr)) return;
  rdma_dereg_mr(mr);
  mr_bufs_.erase(mr);
}

server::server(const char* addr, const char* port) {
  rdma_addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_flags = RAI_PASSIVE;
  hints.ai_port_space = RDMA_PS_TCP;

  if (rdma_getaddrinfo(addr, port, &hints, &addr_info_))
    throw std::runtime_error("Failed to get bind address information");

  ibv_qp_init_attr attr;
  std::memset(&attr, 0, sizeof(attr));
  attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
  attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;
  attr.sq_sig_all = true;

  if (rdma_create_ep(&listen_id_, addr_info_, nullptr, &attr)) {
    rdma_freeaddrinfo(addr_info_);
    throw std::runtime_error("Failed to create RDMA endpoint");
  }
}

server::~server() {
  rdma_destroy_ep(listen_id_);
  rdma_freeaddrinfo(addr_info_);
}

void server::listen() {
  if (rdma_listen(listen_id_, 0))
    throw std::runtime_error("Failed to start RDMA listening");
}

connection server::get_request() {
  rdma_cm_id* conn_id;
  if (rdma_get_request(listen_id_, &conn_id))
    throw std::runtime_error("Failed to get RDMA request");
  return connection(conn_id);
}

void server::accept(const connection& conn) {
  if (rdma_accept(conn.id(), nullptr))
    throw std::runtime_error("Failed to accept RDMA connection");
}

connection connect(const char* addr, const char* port) {
  rdma_addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_port_space = RDMA_PS_TCP;

  rdma_addrinfo* addr_info;
  if (rdma_getaddrinfo(addr, port, &hints, &addr_info))
    throw std::runtime_error("Failed to get server address information");

  ibv_qp_init_attr attr;
  std::memset(&attr, 0, sizeof(attr));
  attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
  attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;
  attr.sq_sig_all = true;

  rdma_cm_id* id;
  if (rdma_create_ep(&id, addr_info, nullptr, &attr)) {
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to create RDMA endpoint");
  }

  if (rdma_connect(id, nullptr)) {
    rdma_destroy_ep(id);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to connect to RDMA remote");
  }

  return {id, addr_info};
}

}  // namespace manualmira::rdma
