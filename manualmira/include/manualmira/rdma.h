#ifndef MANUALMIRA_RDMA_H_
#define MANUALMIRA_RDMA_H_

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "infiniband/verbs.h"
#include "rdma/rdma_cma.h"
#include "rdma/rdma_verbs.h"

namespace manualmira::rdma {

class endpoint {
 public:
  explicit endpoint(rdma_cm_id* id) : id_(id) {}

  endpoint(const endpoint&) = delete;

  ~endpoint() {
    for (const auto& mr_buf : mr_bufs_) rdma_dereg_mr(mr_buf.first);
    rdma_destroy_ep(id_);
  }

  endpoint& operator=(const endpoint&) = delete;

  rdma_cm_id* id() const { return id_; }

  std::pair<ibv_mr*, void*> make_mr(std::size_t size);

  // Deregister a memory region and delete the associated memory buffer.
  //
  // Memory regions are destroyed automatically when `endpoint` deconstructs.
  // It is not necessary to destroy them manually.
  void destroy_mr(ibv_mr* mr);

 private:
  rdma_cm_id* id_;
  std::unordered_map<ibv_mr*, std::vector<std::uint8_t>> mr_bufs_;
};

class connection {
 public:
  explicit connection(rdma_cm_id* id) : id_(id) {}

  connection(const connection&) = delete;

  ~connection() { rdma_disconnect(id_); }

  connection& operator=(const connection&) = delete;

  rdma_cm_id* id() const { return id_; }

 private:
  rdma_cm_id* id_;
};

class server {
 public:
  server(const char* addr, const char* port);

  server(const server&) = delete;

  ~server();

  server& operator=(const server&) = delete;

  void listen();
  endpoint get_conn_req();

  connection accept(const endpoint& ep);
  void reject(const endpoint& ep);

 private:
  rdma_addrinfo* addr_info_;
  rdma_cm_id* listen_id_;
};

class addrinfo {
 public:
  explicit addrinfo(rdma_addrinfo* base) : base_(base) {}

  addrinfo(const addrinfo&) = delete;

  ~addrinfo() { rdma_freeaddrinfo(base_); }

  addrinfo& operator=(const addrinfo&) = delete;

  rdma_addrinfo* base() const { return base_; }

 private:
  rdma_addrinfo* base_;
};

addrinfo resolve(const char* addr, const char* port);
endpoint prepare_connection(const addrinfo& addr);
connection connect(const endpoint& ep);

}  // namespace manualmira::rdma

#endif
