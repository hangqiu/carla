// Copyright (c) 2022 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/multigpu/router.h"

#include "carla/multigpu/commands.h"
#include "carla/multigpu/listener.h"
#include "carla/streaming/EndPoint.h"

#include <cstring>

namespace carla {
namespace multigpu {

Router::Router(void) :
  _next(0) { }

Router::~Router() {
  Stop();
}

void Router::Stop() {
  ClearSessions();
  _listener->Stop();
  _listener.reset();
  _pool.Stop();
}

Router::Router(uint16_t port) :
  _next(0) {

  _endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("0.0.0.0"), port);
  _listener = std::make_shared<carla::multigpu::Listener>(_pool.io_context(), _endpoint);
  _route_ID = "NONE";
  _port = port;
}

Router::Router(uint16_t port, std::string route_ID) :
  _next(0) {

  _endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("0.0.0.0"), port);
  _listener = std::make_shared<carla::multigpu::Listener>(_pool.io_context(), _endpoint);
  _route_ID = route_ID;
  _port = port;
  _route_ID = "NONE";
  _port = port;
}

Router::Router(uint16_t port, std::string route_ID) :
  _next(0) {

  _endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("0.0.0.0"), port);
  _listener = std::make_shared<carla::multigpu::Listener>(_pool.io_context(), _endpoint);
  _route_ID = route_ID;
  _port = port;
}

void Router::SetCallbacks(std::string route_ID) {
void Router::SetCallbacks(std::string route_ID) {
  // prepare server
  std::weak_ptr<Router> weak = shared_from_this();

  carla::multigpu::Listener::callback_function_type on_open = [=](std::shared_ptr<carla::multigpu::Primary> session) {
    auto self = weak.lock();
    if (!self) return;
    self->ConnectSession(session, route_ID);
    self->ConnectSession(session, route_ID);
  };

  carla::multigpu::Listener::callback_function_type on_close = [=](std::shared_ptr<carla::multigpu::Primary> session) {
    auto self = weak.lock();
    if (!self) return;
    self->DisconnectSession(session, route_ID);
    self->DisconnectSession(session, route_ID);
  };

  carla::multigpu::Listener::callback_function_type_response on_response =
    [=](std::shared_ptr<carla::multigpu::Primary> session, carla::Buffer buffer) {
      auto self = weak.lock();
      if (!self) return;
      std::lock_guard<std::mutex> lock(self->_mutex);
      auto prom = self->_promises.find(session.get());
      if (prom != self->_promises.end()) {
        log_info("Got data from secondary (with promise): ", buffer.size());
        prom->second->set_value({session, std::move(buffer)});
        self->_promises.erase(prom);
      } else if (buffer.size() >= sizeof(CommandHeader)) {
        CommandHeader hdr;
        std::memcpy(&hdr, buffer.data(), sizeof(CommandHeader));
        if (hdr.id == MultiGPUCommand::REGISTER_ROUTE_ID && hdr.size > 0 &&
            buffer.size() >= sizeof(CommandHeader) + hdr.size) {
          std::string route_id(
              reinterpret_cast<const char *>(buffer.data() + sizeof(CommandHeader)),
              hdr.size);
          self->UpdateSessionRouteID(session, route_id);
        } else {
          log_info("Got data from secondary (without promise): ", buffer.size());
        }
      }
    };

  _commander.set_router(shared_from_this());

  _listener->Listen(on_open, on_close, on_response);
  log_info("Listening at ", _endpoint);
}

void Router::SetNewConnectionCallback(std::function<void(void)> func)
{
  _callback = func;
}

void Router::AsyncRun(size_t worker_threads) {
  _pool.AsyncRun(worker_threads);
}

boost::asio::ip::tcp::endpoint Router::GetLocalEndpoint() const {
  return _endpoint;
}

void Router::ConnectSession(std::shared_ptr<Primary> session, std::string route_ID) {
void Router::ConnectSession(std::shared_ptr<Primary> session, std::string route_ID) {
  DEBUG_ASSERT(session != nullptr);
  std::lock_guard<std::mutex> lock(_mutex);
  _sessions.emplace_back(std::move(session));
  _connected_route_ids.emplace_back(route_ID);
  _connected_route_ids.emplace_back(route_ID);
  log_info("Connected secondary servers:", _sessions.size());
  // run external callback for new connections
  if (_callback)
    _callback();
}

void Router::DisconnectSession(std::shared_ptr<Primary> session, std::string route_ID) {
void Router::DisconnectSession(std::shared_ptr<Primary> session, std::string route_ID) {
  DEBUG_ASSERT(session != nullptr);
  std::lock_guard<std::mutex> lock(_mutex);
  if (_sessions.size() == 0) return;
  auto it = std::find(_sessions.begin(), _sessions.end(), session);
  if (it != _sessions.end()) {
    auto idx = std::distance(_sessions.begin(), it);
    _sessions.erase(it);
    if (idx < static_cast<decltype(idx)>(_connected_route_ids.size())) {
      _connected_route_ids.erase(_connected_route_ids.begin() + idx);
    }
  }
  auto it = std::find(_sessions.begin(), _sessions.end(), session);
  if (it != _sessions.end()) {
    auto idx = std::distance(_sessions.begin(), it);
    _sessions.erase(it);
    if (idx < static_cast<decltype(idx)>(_connected_route_ids.size())) {
      _connected_route_ids.erase(_connected_route_ids.begin() + idx);
    }
  }
  log_info("Connected secondary servers:", _sessions.size());
}

void Router::ClearSessions() {
  std::lock_guard<std::mutex> lock(_mutex);
  _sessions.clear();
  log_info("Disconnecting all secondary servers");
}

void Router::Write(MultiGPUCommand id, Buffer &&buffer) {
  // define the command header
  CommandHeader header;
  header.id = id;
  header.size = buffer.size();
  Buffer buf_header((uint8_t *) &header, sizeof(header));

  auto view_header = carla::BufferView::CreateFrom(std::move(buf_header));
  auto view_data = carla::BufferView::CreateFrom(std::move(buffer));
  auto message = Primary::MakeMessage(view_header, view_data);

  // write to multiple servers
  std::lock_guard<std::mutex> lock(_mutex);
  for (auto &s : _sessions) {
    if (s != nullptr) {
      s->Write(message);
    }
  }
}

std::future<SessionInfo> Router::WriteToNext(MultiGPUCommand id, Buffer &&buffer) {
  // define the command header
  CommandHeader header;
  header.id = id;
  header.size = buffer.size();
  Buffer buf_header((uint8_t *) &header, sizeof(header));

  auto view_header = carla::BufferView::CreateFrom(std::move(buf_header));
  auto view_data = carla::BufferView::CreateFrom(std::move(buffer));
  auto message = Primary::MakeMessage(view_header, view_data);

  // create the promise for the posible answer
  auto response = std::make_shared<std::promise<SessionInfo>>();

  // write to the next server only
  std::lock_guard<std::mutex> lock(_mutex);
  if (_next >= _sessions.size()) {
    _next = 0;
  }
  if (_next < _sessions.size()) {
    // std::cout << "Sending to session " << _next << std::endl;
    auto s = _sessions[_next];
    if (s != nullptr) {
      _promises[s.get()] = response;
      std::cout << "Updated promise into map: " << _promises.size() << std::endl;
      s->Write(message);
    }
  }
  ++_next;
  return response->get_future();
}

std::future<SessionInfo> Router::WriteToOne(std::weak_ptr<Primary> server, MultiGPUCommand id, Buffer &&buffer) {
  // define the command header
  CommandHeader header;
  header.id = id;
  header.size = buffer.size();
  Buffer buf_header((uint8_t *) &header, sizeof(header));

  auto view_header = carla::BufferView::CreateFrom(std::move(buf_header));
  auto view_data = carla::BufferView::CreateFrom(std::move(buffer));
  auto message = Primary::MakeMessage(view_header, view_data);

  // create the promise for the posible answer
  auto response = std::make_shared<std::promise<SessionInfo>>();

  // write to the specific server only
  std::lock_guard<std::mutex> lock(_mutex);
  auto s = server.lock();
  if (s) {
    _promises[s.get()] = response;
    s->Write(message);
  }
  return response->get_future();
}

std::weak_ptr<Primary> Router::GetNextServer() {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_next >= _sessions.size()) {
    _next = 0;
  }
  if (_next < _sessions.size()) {
    auto server = std::weak_ptr<Primary>(_sessions[_next]);
    ++_next;
    if (_next >= _sessions.size()) _next = 0;
    return server;
    auto server = std::weak_ptr<Primary>(_sessions[_next]);
    ++_next;
    if (_next >= _sessions.size()) _next = 0;
    return server;
  } else {
    return std::weak_ptr<Primary>();
  }
}

std::string Router::GetRouteIDFromSession() {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_connected_route_ids.empty()) return "NONE";
  size_t idx = (_next > 0) ? (_next - 1) : (_connected_route_ids.size() - 1);
  if (_connected_route_ids[idx].empty()) return std::to_string(idx);
  return _connected_route_ids[idx];
}

void Router::UpdateSessionRouteID(std::shared_ptr<Primary> session, std::string route_id) {
  auto it = std::find(_sessions.begin(), _sessions.end(), session);
  if (it != _sessions.end()) {
    size_t idx = static_cast<size_t>(std::distance(_sessions.begin(), it));
    if (idx < _connected_route_ids.size()) {
      _connected_route_ids[idx] = route_id;
      std::cout << "Registered route ID '" << route_id << "' for secondary " << idx << std::endl;
    }
  }
}

} // namespace multigpu
} // namespace carla
