#pragma once
#include "Json.h"
#include <functional>
#include <mutex>
#include <map>
#include <memory>
#include <atomic>
#include <thread>

#include <event2/event.h>
#include <event2/http.h>

namespace auracash {
namespace rpc {

using JsonValue = auracash::json::Value;
using JsonArray = JsonValue::Array;
using JsonObject = JsonValue::Object;

struct Method {
    std::string name;
    std::function<JsonValue(const JsonArray&)> handler;
    bool requires_auth;
};

class RpcServer {
public:
    RpcServer(uint16_t port);
    ~RpcServer();

    void register_method(
        const std::string& name,
        std::function<JsonValue(const JsonArray&)> handler,
        bool requires_auth = false
    );

    bool start();
    void stop();

private:
    void handle_request(struct evhttp_request* req);
    static void http_callback(struct evhttp_request* req, void* arg);
    void send_json_response(struct evhttp_request* req, const JsonValue& response, int status = 200);
    void send_error(struct evhttp_request* req, int code, const std::string& message, const JsonValue& id);

    uint16_t m_port;
    std::atomic<bool> m_running;
    std::mutex m_methodsMutex;
    struct event_base* m_base = nullptr;
    struct evhttp* m_http = nullptr;
    std::thread m_thread;
    std::map<std::string, Method> m_methods;
};

} // namespace rpc
} // namespace auracash
