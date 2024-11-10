#include "manualmira/rdma.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace manualmira::rdma {

namespace internal {

rdma_cm_event* await_cm_event(rdma_event_channel* ch,
                              rdma_cm_event_type event) {
  rdma_cm_event* evt;
  // TODO: handle error events
  do {
    if (rdma_get_cm_event(ch, &evt))
      throw std::runtime_error("Failed to get RDMA CM event");
  } while (evt->status != 0 || evt->event != event);
  return evt;
}

void await_ack_cm_event(rdma_event_channel* ch, rdma_cm_event_type event) {
  if (rdma_ack_cm_event(await_cm_event(ch, event)))
    throw std::runtime_error("Failed to acknowledge RDMA CM event");
}

}  // namespace internal

server::server(const char* addr, std::uint16_t port) {
  evt_ch_ = rdma_create_event_channel();
  if (!evt_ch_) throw std::runtime_error("Failed to create RDMA event channel");

  if (rdma_create_id(evt_ch_, &listen_id_, nullptr, RDMA_PS_TCP)) {
    rdma_destroy_event_channel(evt_ch_);
    throw std::runtime_error("Failed to create RDMA server listening ID");
  }

  sockaddr_in bind_addr;
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(port);
  bind_addr.sin_addr.s_addr = inet_addr(addr);
  if (rdma_bind_addr(listen_id_, reinterpret_cast<sockaddr*>(&bind_addr))) {
    rdma_destroy_id(listen_id_);
    rdma_destroy_event_channel(evt_ch_);
    throw std::runtime_error("Failed to bind RDMA server to address");
  }

  is_inited_ = true;
}

server::server(server&& other) noexcept {
  std::swap(is_inited_, other.is_inited_);

  std::swap(evt_ch_, other.evt_ch_);
  std::swap(listen_id_, other.listen_id_);
}

server::~server() {
  if (!is_inited_) return;

  rdma_destroy_id(listen_id_);
  rdma_destroy_event_channel(evt_ch_);

  is_inited_ = false;
}

server& server::operator=(server&& other) noexcept {
  std::swap(is_inited_, other.is_inited_);

  std::swap(evt_ch_, other.evt_ch_);
  std::swap(listen_id_, other.listen_id_);

  return *this;
}

void server::listen(int backlog) {
  if (rdma_listen(listen_id_, backlog))
    throw std::runtime_error("Failed to start RDMA listening");
}

server_connection server::accept() {
  rdma_cm_event* conn_req_evt =
      internal::await_cm_event(evt_ch_, RDMA_CM_EVENT_CONNECT_REQUEST);
  rdma_cm_id* conn_id = conn_req_evt->id;

  ibv_pd* pd = ibv_alloc_pd(conn_id->verbs);
  if (!pd) {
    rdma_ack_cm_event(conn_req_evt);
    throw std::runtime_error("Failed to allocate IBV PD");
  }

  ibv_cq* cq = ibv_create_cq(conn_id->verbs, 1, nullptr, nullptr, 0);
  if (!cq) {
    ibv_dealloc_pd(pd);
    rdma_ack_cm_event(conn_req_evt);
    throw std::runtime_error("Failed to create IBV CQ");
  }

  ibv_qp_init_attr attr;
  std::memset(&attr, 0, sizeof(attr));
  attr.recv_cq = attr.send_cq = cq;
  attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
  attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;
  attr.qp_type = IBV_QPT_RC;
  attr.sq_sig_all = true;
  if (rdma_create_qp(conn_id, pd, &attr)) {
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_ack_cm_event(conn_req_evt);
    throw std::runtime_error("Failed to create RDMA QP");
  }

  if (rdma_accept(conn_id, nullptr)) {
    rdma_destroy_qp(conn_id);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_ack_cm_event(conn_req_evt);
    throw std::runtime_error("Failed to accept RDMA connection");
  }

  rdma_ack_cm_event(conn_req_evt);

  internal::await_ack_cm_event(evt_ch_, RDMA_CM_EVENT_ESTABLISHED);

  return {evt_ch_, conn_id, pd, cq};
}

client_connection connect(const char* addr, const char* port_cstr) {
  rdma_addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_port_space = RDMA_PS_TCP;

  rdma_addrinfo* addr_info;
  if (rdma_getaddrinfo(addr, port_cstr, &hints, &addr_info))
    throw std::runtime_error("Failed to get server address information");

  rdma_event_channel* evt_ch = rdma_create_event_channel();
  if (!evt_ch) {
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to create RDMA event channel");
  }

  rdma_cm_id* id;
  if (rdma_create_id(evt_ch, &id, nullptr, RDMA_PS_TCP)) {
    rdma_destroy_event_channel(evt_ch);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to create RDMA server listening ID");
  }

  if (rdma_resolve_addr(id, nullptr, addr_info->ai_dst_addr, 2000)) {
    rdma_destroy_id(id);
    rdma_destroy_event_channel(evt_ch);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to resolve remote address");
  }

  internal::await_ack_cm_event(evt_ch, RDMA_CM_EVENT_ADDR_RESOLVED);

  ibv_pd* pd = ibv_alloc_pd(id->verbs);
  if (!pd) {
    rdma_destroy_id(id);
    rdma_destroy_event_channel(evt_ch);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to allocate IBV PD");
  }

  ibv_cq* cq = ibv_create_cq(id->verbs, 1, nullptr, nullptr, 0);
  if (!cq) {
    ibv_dealloc_pd(pd);
    rdma_destroy_id(id);
    rdma_destroy_event_channel(evt_ch);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to create IBV CQ");
  }

  ibv_qp_init_attr attr;
  std::memset(&attr, 0, sizeof(attr));
  attr.recv_cq = attr.send_cq = cq;
  attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
  attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;
  attr.qp_type = IBV_QPT_RC;
  attr.sq_sig_all = true;
  if (rdma_create_qp(id, pd, &attr)) {
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_destroy_id(id);
    rdma_destroy_event_channel(evt_ch);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to create RDMA QP");
  }

  if (rdma_resolve_route(id, 2000)) {
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_destroy_id(id);
    rdma_destroy_event_channel(evt_ch);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to resolve route to remote");
  }

  internal::await_ack_cm_event(evt_ch, RDMA_CM_EVENT_ROUTE_RESOLVED);

  if (rdma_connect(id, nullptr)) {
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_destroy_id(id);
    rdma_destroy_event_channel(evt_ch);
    rdma_freeaddrinfo(addr_info);
    throw std::runtime_error("Failed to connect to RDMA remote");
  }

  internal::await_ack_cm_event(evt_ch, RDMA_CM_EVENT_ESTABLISHED);

  return {evt_ch, id, pd, cq, addr_info};
}

}  // namespace manualmira::rdma
