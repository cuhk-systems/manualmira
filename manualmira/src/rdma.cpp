#include "manualmira/rdma.h"

#include <cstring>
#include <stdexcept>

namespace manualmira::rdma {

std::pair<ibv_mr*, void*> endpoint::make_mr(std::size_t size) {
  std::vector<std::uint8_t> buf;
  buf.reserve(size);

  void* ptr = buf.data();

  ibv_mr* mr = rdma_reg_msgs(id_, ptr, size);
  if (!mr) return {nullptr, nullptr};

  mr_bufs_[mr] = std::move(buf);

  return {mr, ptr};
}

void endpoint::destroy_mr(ibv_mr* mr) {
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

endpoint server::get_conn_req() {
  rdma_cm_id* conn_id;
  if (rdma_get_request(listen_id_, &conn_id))
    throw std::runtime_error("Failed to get RDMA request");
  return endpoint(conn_id);
}

connection server::accept(const endpoint& ep) {
  if (rdma_accept(ep.id(), nullptr))
    throw std::runtime_error("Failed to accept RDMA connection request");
  return connection(ep.id());
}

void server::reject(const endpoint& ep) {
  if (rdma_reject(ep.id(), nullptr, 0))
    throw std::runtime_error("Failed to reject RDMA connection request");
}

addrinfo resolve(const char* addr, const char* port) {
  rdma_addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_port_space = RDMA_PS_TCP;

  rdma_addrinfo* addr_info;
  if (rdma_getaddrinfo(addr, port, &hints, &addr_info))
    throw std::runtime_error("Failed to get server address information");

  return addrinfo(addr_info);
}

endpoint prepare_connection(const addrinfo& addr) {
  ibv_qp_init_attr attr;
  std::memset(&attr, 0, sizeof(attr));
  attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
  attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;
  attr.sq_sig_all = true;

  rdma_cm_id* id;
  if (rdma_create_ep(&id, addr.base(), nullptr, &attr))
    throw std::runtime_error("Failed to create RDMA endpoint");

  return endpoint(id);
}

connection connect(const endpoint& ep) {
  if (rdma_connect(ep.id(), nullptr))
    throw std::runtime_error("RDMA connection rejected");
  return connection(ep.id());
}

}  // namespace manualmira::rdma
