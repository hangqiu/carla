// Copyright (c) 2022 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/Logging.h"
#include "carla/multigpu/secondaryCommands.h"
#include "carla/streaming/detail/tcp/Message.h"

#include <chrono>

namespace carla {
namespace multigpu {

void SecondaryCommands::set_secondary(std::shared_ptr<Secondary> secondary) {
  double now = std::chrono::duration<double>(
                  std::chrono::system_clock::now().time_since_epoch()
                ).count();
  std::string event = "SetSecondary";
    std::string result = "Secondary_" + std::to_string(now) + "_" + event;
    log_error(result);
  _secondary = secondary;  
}

void SecondaryCommands::set_callback(callback_type callback) {
  double now = std::chrono::duration<double>(
                  std::chrono::system_clock::now().time_since_epoch()
                ).count();
  std::string event = "SetCallback";
  std::string result = "Secondary_" + std::to_string(now) + "_" + event;
  log_error(result);
  _callback = callback;
}

void SecondaryCommands::process_command(Buffer buffer) {
  // get the header
  CommandHeader *header;
  header = reinterpret_cast<CommandHeader *>(buffer.data());
  
  // send only data to the callback
  Buffer data(buffer.data() + sizeof(CommandHeader), header->size);
  _callback(header->id, std::move(data));

  double now = std::chrono::duration<double>(
                  std::chrono::system_clock::now().time_since_epoch()
                ).count();
                
  std::string event = "SendData";
  std::string result = "Secondary_" + std::to_string(now) + "_" + event;
  log_error(result);

  log_error("Secondary got a command to process");
  // log_info("Secondary got a command to process");
}


} // namespace multigpu
} // namespace carla
