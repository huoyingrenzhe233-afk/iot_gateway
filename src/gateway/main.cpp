#include <mongoose.h>

#include "core/common/logger.h"

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

int main(void)
{
    Logger::instance().init("gateway.log");
    LOG_INFO("gateway starting");

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:8080", request_handler, NULL);
    LOG_INFO("http server listening on :8080");

    for (;;)
    {
        mg_mgr_poll(&mgr, 1000);
    }
    return 0;
}