#include <core/common/logger/logger.h>
#include <core/control/control.h>
#include <ctime>
#include <mongoose.h>

namespace gateway {

std::string Control::build_control_envelope(const std::string &body,
                                            const std::string &device_id) {
  struct mg_str payload =
      mg_json_get_tok(mg_str_n(body.data(), body.size(), "$.payload"));
  if (payload.len == 0) {
    return "";
  }
}
return nullptr;
} // namespace gateway