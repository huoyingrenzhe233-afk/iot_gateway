#include <mongoose.h>

static void request_handler(struct mg_connection *connect, int event, void *event_data)
{
    if (event == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)event_data;
        if (mg_match(hm->uri, mg_str("/api/health"), NULL) == true)
        {
            mg_http_reply(connect, 200, "Content-Type: application/json\r\n", "{\"status\":\"healthy\"}");
        }
        else
        {
            mg_http_reply(connect, 404, "", "NOT_FOUND");
        }
    }
}

int main(void)
{
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:8080", request_handler, NULL);
    for (;;)
    {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
    return 0;
}