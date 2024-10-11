#include "manualmira/rdma.h"

#include <cstring>
#include <stdexcept>

#include "rdma/rdma_verbs.h"

namespace manualmira::rdma {

connection::connection(connection&& other) noexcept {
  std::swap(id_, other.id_);
  std::swap(mr_bufs_, other.mr_bufs_);

  std::swap(addr_info_, other.addr_info_);
}

connection::~connection() {
  if (id_) {
    rdma_disconnect(id_);
    for (const auto& mr_buf : mr_bufs_) rdma_dereg_mr(mr_buf.first);
    rdma_destroy_ep(id_);
  }

  if (addr_info_) rdma_freeaddrinfo(addr_info_);
}

connection& connection::operator=(connection&& other) noexcept {
  std::swap(id_, other.id_);
  std::swap(mr_bufs_, other.mr_bufs_);

  std::swap(addr_info_, other.addr_info_);

  return *this;
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

  is_inited_ = true;
}

server::server(server&& other) noexcept {
  std::swap(is_inited_, other.is_inited_);

  std::swap(addr_info_, other.addr_info_);
  std::swap(listen_id_, other.listen_id_);
}

server::~server() {
  if (!is_inited_) return;

  rdma_destroy_ep(listen_id_);
  rdma_freeaddrinfo(addr_info_);
}

server& server::operator=(server&& other) noexcept {
  std::swap(is_inited_, other.is_inited_);

  std::swap(addr_info_, other.addr_info_);
  std::swap(listen_id_, other.listen_id_);

  return *this;
}

int server::listen() { return rdma_listen(listen_id_, 0); }

connection server::accept() {
  rdma_cm_id* conn_id;
  if (rdma_get_request(listen_id_, &conn_id))
    throw std::runtime_error("Failed to get RDMA request");

  if (rdma_accept(conn_id, nullptr))
    throw std::runtime_error("Failed to accept RDMA connection");

  return connection(conn_id);
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
