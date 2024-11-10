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

namespace internal {

rdma_cm_event* await_cm_event(rdma_event_channel* ch, rdma_cm_event_type event);
void await_ack_cm_event(rdma_event_channel* ch, rdma_cm_event_type event);

}  // namespace internal

template <bool IsServer>
class connection;

using server_connection = connection<true>;
using client_connection = connection<false>;

template <bool IsServer>
class connection {
 public:
  friend class server;
  friend client_connection connect(const char* addr, const char* port);

  connection(const connection&) = delete;

  connection(connection&& other) noexcept {
    std::swap(evt_ch_, other.evt_ch_);
    std::swap(id_, other.id_);

    std::swap(pd_, other.pd_);
    std::swap(cq_, other.cq_);

    std::swap(mr_bufs_, other.mr_bufs_);

    std::swap(addr_info_, other.addr_info_);
  }

  ~connection() {
    if (IsServer) {
      internal::await_ack_cm_event(evt_ch_, RDMA_CM_EVENT_DISCONNECTED);
      rdma_disconnect(id_);
    } else {
      rdma_disconnect(id_);
      internal::await_ack_cm_event(evt_ch_, RDMA_CM_EVENT_DISCONNECTED);
    }

    for (const auto& mr_buf : mr_bufs_) rdma_dereg_mr(mr_buf.first);

    rdma_destroy_qp(id_);
    ibv_destroy_cq(cq_);
    ibv_dealloc_pd(pd_);

    rdma_destroy_id(id_);

    if (addr_info_) rdma_freeaddrinfo(addr_info_);
  }

  connection& operator=(const connection&) = delete;

  connection& operator=(connection&& other) noexcept {
    std::swap(evt_ch_, other.evt_ch_);
    std::swap(id_, other.id_);

    std::swap(pd_, other.pd_);
    std::swap(cq_, other.cq_);

    std::swap(mr_bufs_, other.mr_bufs_);

    std::swap(addr_info_, other.addr_info_);

    return *this;
  }

  rdma_event_channel* event_channel() { return evt_ch_; }
  rdma_cm_id* id() { return id_; }

  ibv_pd* pd() { return pd_; }
  ibv_cq* cq() { return cq_; }

  std::pair<ibv_mr*, void*> make_mr(std::size_t size) {
    std::vector<std::uint8_t> buf;
    buf.reserve(size);

    void* ptr = buf.data();

    ibv_mr* mr = rdma_reg_msgs(id_, ptr, size);
    if (!mr) return {nullptr, nullptr};

    mr_bufs_[mr] = std::move(buf);

    return {mr, ptr};
  }

  // Deregister a memory region and delete the associated memory buffer.
  //
  // Memory regions are destroyed automatically when `connection` deconstructs.
  // It is not necessary to destroy them manually.
  void destroy_mr(ibv_mr* mr) {
    if (!mr_bufs_.contains(mr)) return;
    rdma_dereg_mr(mr);
    mr_bufs_.erase(mr);
  }

 protected:
  connection(rdma_event_channel* evt_ch, rdma_cm_id* id, ibv_pd* pd, ibv_cq* cq,
             rdma_addrinfo* addr_info = nullptr)
      : evt_ch_(evt_ch), id_(id), pd_(pd), cq_(cq), addr_info_(addr_info) {}

 private:
  // DO NOT destroy the event channel in `connection`, its lifetime is bonded
  // with the server/client.
  rdma_event_channel* evt_ch_;

  rdma_cm_id* id_;

  ibv_pd* pd_;
  ibv_cq* cq_;

  std::unordered_map<ibv_mr*, std::vector<std::uint8_t>> mr_bufs_;

  rdma_addrinfo* addr_info_;
};

class server {
 public:
  server(const char* addr, std::uint16_t port);
  server(const server&) = delete;
  server(server&& other) noexcept;

  ~server();

  server& operator=(const server&) = delete;
  server& operator=(server&& other) noexcept;

  void listen(int backlog = 0);
  server_connection accept();

 private:
  bool is_inited_ = false;

  rdma_event_channel* evt_ch_;
  rdma_cm_id* listen_id_;
};

client_connection connect(const char* addr, const char* port_cstr);

}  // namespace manualmira::rdma

#endif
