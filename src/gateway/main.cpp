#include <cstdio>
#include <cstdlib>

#include <mongoose.h>

#include "core/common/logger/logger.h"

using gateway::Logger;
using gateway::LogLevel;

static void request_handler(struct mg_connection *connect, int event, void *event_data)
{
    if (event == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)event_data;
        LOG_INFO("HTTP %.*s %.*s", (int)hm->method.len, hm->method.buf,
                 (int)hm->uri.len, hm->uri.buf);

        if (mg_match(hm->uri, mg_str("/api/health"), NULL) == true)
        {
            mg_http_reply(connect, 200, "Content-Type: application/json\r\n",
                          "{\"status\":\"ok\"}");
        }
        else
        {
            LOG_WARN("404: %.*s", (int)hm->uri.len, hm->uri.buf);
            mg_http_reply(connect, 404, "", "NOT_FOUND");
        }
    }
}

int main(int argc, char *argv[])
{
    Logger::instance().init("gateway.log");
    LOG_INFO("gateway starting");

    // Port is configurable via argv[1] (default 8080). The board's 8080 is
    // occupied by mjpg_streamer, so pass an alternative port when deploying.
    int port = 8080;
    if (argc > 1)
    {
        int p = std::atoi(argv[1]);
        if (p > 0 && p <= 65535)
        {
            port = p;
        }
    }
    char listen_addr[64];
    std::snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%d", port);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, listen_addr, request_handler, NULL);
    LOG_INFO("http server listening on :%d", port);

    for (;;)
    {
        mg_mgr_poll(&mgr, 1000);
    }
    return 0;
}