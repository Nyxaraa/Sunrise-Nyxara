#include "server_runtime.h"

#include "../../client/network/consumer.h"
#include "../../core/logging/log.h"
#include "../activity/host_runtime.h"
#include "../activity/mission/mission_script_runtime.h"
#include "../bap/runtime.h"
#include "../gameplay/gameplay_runtime.h"
#include "../http/server_http.h"
#include "../transport/bap_listener.h"
#include "../ui/runtime/server_ui_module_runtime.h"

namespace sunrise::server {

/** Registers Server consumers with the Client networking boundary. */
bool initialize() noexcept {
    activity::host::reset();
    activity::mission::initialize();
    if (!client::network::register_http_consumer(&http::consume)) {
        activity::mission::shutdown();
        return false;
    }
    if (client::network::register_bap_consumer(&bap::consume)) {
        // HTTP and UI remain useful when the local BAP port is already owned.
        if (!transport::initialize()) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=transport stage=listen result=fail");
        }
        // The gameplay endpoint must bind before any descriptor advertises it.
        if (!gameplay::initialize()) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=gameplay stage=init result=fail");
        }
        if (ui::runtime::initialize()) {
            return true;
        }
        gameplay::shutdown();
        transport::shutdown();
        client::network::unregister_bap_consumer(&bap::consume);
    }
    // BAP registration failure rolls back the earlier HTTP registration.
    client::network::unregister_http_consumer(&http::consume);
    activity::mission::shutdown();
    return false;
}

/** Runs one bounded server service slice. @param now Monotonic tick count. */
void service(std::uint64_t now) noexcept {
    transport::service(now);
    activity::host::service(now);
    activity::mission::service(now);
    gameplay::service(now);
}

/** Unregisters Server consumers in reverse registration order. */
void shutdown() noexcept {
    ui::runtime::shutdown();
    gameplay::shutdown();
    transport::shutdown();
    client::network::unregister_bap_consumer(&bap::consume);
    client::network::unregister_http_consumer(&http::consume);
    bap::shutdown();
    activity::mission::shutdown();
    activity::host::reset();
}

} // namespace sunrise::server
