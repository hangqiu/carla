// Copyright (c) 2017 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/streaming/detail/tcp/ServerSession.h"
#include "carla/streaming/detail/tcp/Server.h"

#include "carla/Debug.h"
#include "carla/Logging.h"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>

#include <atomic>

namespace carla {
namespace streaming {
namespace detail {
namespace tcp {

  static std::atomic_size_t SESSION_COUNTER{0u};

  ServerSession::ServerSession(
      boost::asio::io_context &io_context,
      const time_duration timeout,
      Server &server)
    : LIBCARLA_INITIALIZE_LIFETIME_PROFILER(
          std::string("tcp server session ") + std::to_string(SESSION_COUNTER)),
      _server(server),
      _session_id(SESSION_COUNTER++),
      _socket(io_context),
      _timeout(timeout),
      _deadline(io_context),
      _strand(io_context) {}

  void ServerSession::Open(
      callback_function_type on_opened,
      callback_function_type on_closed) {
    DEBUG_ASSERT(on_opened && on_closed);
    _on_closed = std::move(on_closed);

    // This forces not using Nagle's algorithm.
    // Improves the sync mode velocity on Linux by a factor of ~3.
    const boost::asio::ip::tcp::no_delay option(true);
    _socket.set_option(option);

    StartTimer();
    auto self = shared_from_this(); // To keep myself alive.
    boost::asio::post(_strand, [=]() {

      auto handle_query = [this, self, callback=std::move(on_opened)](
          const boost::system::error_code &ec,
          size_t DEBUG_ONLY(bytes_received)) {
        if (!ec) {
          DEBUG_ASSERT_EQ(bytes_received, sizeof(_stream_id));
          log_debug("session", _session_id, "for stream", _stream_id, " started");
          boost::asio::post(_strand.context(), [=]() { callback(self); });
        } else {
          log_error("session", _session_id, ": error retrieving stream id :", ec.message());
          CloseNow(ec);
        }
      };

      // Read the stream id.
      _deadline.expires_from_now(_timeout);
      boost::asio::async_read(
          _socket,
          boost::asio::buffer(&_stream_id, sizeof(_stream_id)),
          boost::asio::bind_executor(_strand, handle_query));
    });
  }

  void ServerSession::Write(std::shared_ptr<const Message> message) {
    DEBUG_ASSERT(message != nullptr);
    DEBUG_ASSERT(!message->empty());
    auto self = shared_from_this();
    boost::asio::post(_strand, [=]() {
      if (!_socket.is_open()) {
        return;
      }

      // A message that arrives while a write is in flight waits in the queue
      // instead of occupying this worker. The completion handler drains it, so
      // no io_context worker is ever held for the duration of a write and the
      // pool cannot be starved of the very threads that would release it.
      //
      // The cap is what distinguishes the two modes. Asynchronously the stream
      // is a live feed with nothing throttling the producer: a stale frame is
      // worthless and an unbounded queue would delay every frame behind it, so
      // one in flight and no backlog reproduces the previous discard behaviour.
      // Synchronously the tick barrier bounds the producer, every payload is
      // owed to the client, and any backlog is a short startup burst that
      // drains as soon as the world blocks at the barrier.
      const std::size_t max_queued = _server.IsSynchronousMode() ? 8u : 1u;
      if (_write_queue.size() >= max_queued) {
        if (_server.IsSynchronousMode()) {
          // Not expected: the producer is barrier-bound. Loud, because losing a
          // payload here stalls whichever frame the client is waiting on.
          log_warning(
              "session", _session_id,
              ": write queue full (", max_queued, "), message discarded");
        } else {
          log_debug("session", _session_id, ": connection too slow: message discarded");
        }
        return;
      }

      _write_queue.push_back(message);

      // Only start a write when nothing is in flight; otherwise the completion
      // handler will pick up what was just queued.
      if (_write_queue.size() == 1u) {
        DoWrite();
      }
    });
  }

  void ServerSession::DoWrite() {
    auto self = shared_from_this();
    // Keep the message alive independently of the queue, which the completion
    // handler pops before the buffers are done with.
    auto message = _write_queue.front();

    log_debug("session", _session_id, ": sending message of", message->size(), "bytes");

    _deadline.expires_from_now(_timeout);
    boost::asio::async_write(
        _socket,
        message->GetBufferSequence(),
        // Bound to the strand so the queue is only ever touched from one place.
        boost::asio::bind_executor(_strand,
            [this, self, message](const boost::system::error_code &ec, size_t DEBUG_ONLY(bytes)) {
              if (ec) {
                log_info("session", _session_id, ": error sending data :", ec.message());
                _write_queue.clear();
                CloseNow(ec);
                return;
              }
              DEBUG_ONLY(log_debug("session", _session_id, ": successfully sent", bytes, "bytes"));
              DEBUG_ASSERT_EQ(bytes, sizeof(message_size_type) + message->size());
              _write_queue.pop_front();
              if (!_write_queue.empty()) {
                DoWrite();
              }
            }));
  }

  void ServerSession::Close() {
    boost::asio::post(_strand, [self=shared_from_this()]() { self->CloseNow(); });
  }

  void ServerSession::StartTimer() {
    if (_deadline.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
      log_debug("session", _session_id, "timed out");
      Close();
    } else {
      _deadline.async_wait([this, self=shared_from_this()](boost::system::error_code ec) {
        if (!ec) {
          StartTimer();
        } else {
          log_debug("session", _session_id, "timed out error:", ec.message());
        }
      });
    }
  }

  void ServerSession::CloseNow(boost::system::error_code ec) {
    _deadline.cancel();
    if (!ec)
    {
      if (_socket.is_open()) {
        boost::system::error_code ec2;
        _socket.shutdown(boost::asio::socket_base::shutdown_both, ec2);
        _socket.close();
      }
    }
    _on_closed(shared_from_this());
    log_debug("session", _session_id, "closed");
  }

} // namespace tcp
} // namespace detail
} // namespace streaming
} // namespace carla
