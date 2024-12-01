#ifndef MANUALMIRA_RDMA_H_
#define MANUALMIRA_RDMA_H_

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "infiniband/verbs.h"
#include "rdma/rdma_cma.h"

namespace manualmira::rdma {

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

class connection {
 public:
  friend class server;
  friend connection connect(const addrinfo& addr);

  connection(const connection&) = delete;

  ~connection();

  connection& operator=(const connection&) = delete;

  rdma_cm_id* id() const { return id_; }

  std::pair<ibv_mr*, void*> make_mr(std::size_t size);

  // Deregister a memory region and delete the associated memory buffer.
  //
  // Memory regions are destroyed automatically when `connection` deconstructs.
  // It is not necessary to destroy them manually.
  void destroy_mr(ibv_mr* mr);

 protected:
  explicit connection(rdma_cm_id* id) : id_(id) {}

 private:
  rdma_cm_id* id_ = nullptr;
  std::unordered_map<ibv_mr*, std::vector<std::uint8_t>> mr_bufs_;
};

class server {
 public:
  server(const char* addr, const char* port);

  server(const server&) = delete;

  ~server();

  server& operator=(const server&) = delete;

  void listen();
  connection get_request();
  void accept(const connection& conn);

 private:
  rdma_addrinfo* addr_info_;
  rdma_cm_id* listen_id_;
};

addrinfo resolve(const char* addr, const char* port);
connection connect(const addrinfo& addr);

}  // namespace manualmira::rdma

#endif
