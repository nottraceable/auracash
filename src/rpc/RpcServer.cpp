#include "RpcServer.h"
#include <iostream>
#include <sstream>
#include <cstring>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>

namespace auracash {
namespace rpc {

RpcServer::RpcServer(uint16_t port) : m_port(port) {}

RpcServer::~RpcServer() {
    stop();
}

void RpcServer::register_method(const std::string& name, std::function<JsonValue(const JsonArray&)> handler, bool requires_auth) {
    std::lock_guard<std::mutex> lock(m_methodsMutex);
    Method m;
    m.name = name;
    m.handler = std::move(handler);
    m.requires_auth = requires_auth;
    m_methods[name] = std::move(m);
}

bool RpcServer::start() {
    if (m_running.load()) return true;
    m_base = event_base_new();
    if (!m_base) {
        std::cerr << "[rpc] Failed to create event_base\n";
        return false;
    }
    m_http = evhttp_new(m_base);
    if (!m_http) {
        std::cerr << "[rpc] Failed to create evhttp\n";
        event_base_free(m_base);
        return false;
    }
    int ret = evhttp_bind_socket(m_http, "0.0.0.0", m_port);
    if (ret != 0) {
        std::cerr << "[rpc] Failed to bind on port " << m_port << "\n";
        evhttp_free(m_http);
        event_base_free(m_base);
        return false;
    }
    evhttp_set_gencb(m_http, http_callback, this);
    m_running.store(true);
    m_thread = std::thread([this]() {
        std::cerr << "[rpc] JSON-RPC server listening on port " << m_port << "\n";
        event_base_dispatch(m_base);
    });
    return true;
}

void RpcServer::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_base) event_base_loopbreak(m_base);
    if (m_thread.joinable()) m_thread.join();
    if (m_http) evhttp_free(m_http);
    if (m_base) event_base_free(m_base);
    m_http = nullptr;
    m_base = nullptr;
}

void RpcServer::http_callback(struct evhttp_request* req, void* arg) {
    RpcServer* server = static_cast<RpcServer*>(arg);
    server->handle_request(req);
}

void RpcServer::handle_request(struct evhttp_request* req) {
    const char* uri = evhttp_request_get_uri(req);
    if (!uri) {
        evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
        return;
    }
    // Only allow / or /jsonrpc
    if (strcmp(uri, "/") != 0 && strcmp(uri, "/jsonrpc") != 0) {
        evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
        return;
    }
    // Check content-type
    const char* ctype = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Type");
    if (!ctype || strstr(ctype, "application/json") == nullptr) {
        evhttp_send_error(req, HTTP_BADREQUEST, "Content-Type must be application/json");
        return;
    }
    // Read body
    struct evbuffer* buf = evhttp_request_get_input_buffer(req);
    size_t len = evbuffer_get_length(buf);
    std::string body;
    if (len > 0) {
        body.resize(len);
        evbuffer_copyout(buf, &body[0], len);
    }
    if (body.empty()) {
        evhttp_send_error(req, HTTP_BADREQUEST, "Empty request");
        return;
    }
    // Parse JSON
    JsonValue request = json::parse(body);
    if (request.is_null()) {
        evhttp_send_error(req, HTTP_BADREQUEST, "Invalid JSON");
        return;
    }
    // JSON-RPC 2.0 structure
    if (!request.is_object()) {
        send_error(req, -32600, "Invalid Request", JsonValue(nullptr));
        return;
    }
    const JsonObject& obj = request.as_object();
    // Check jsonrpc field
    if (obj.find("jsonrpc") == obj.end() || obj.at("jsonrpc").as_string() != "2.0") {
        send_error(req, -32600, "Invalid jsonrpc version", obj.contains("id") ? obj.at("id") : JsonValue(nullptr));
        return;
    }
    JsonValue id = obj.contains("id") ? obj.at("id") : JsonValue(nullptr);
    std::string method = obj.contains("method") ? obj.at("method").as_string() : "";
    JsonArray params;
    if (obj.contains("params")) {
        const JsonValue& p = obj.at("params");
        if (p.is_array()) {
            params = p.as_array();
        } else {
            send_error(req, -32602, "Invalid params", id);
            return;
        }
    }
    // Find handler
    std::lock_guard<std::mutex> lock(m_methodsMutex);
    auto it = m_methods.find(method);
    if (it == m_methods.end()) {
        send_error(req, -32601, "Method not found", id);
        return;
    }
    const Method& m = it->second;
    // TODO: auth check
    try {
        JsonValue result = m.handler(params);
        JsonValue response;
        JsonObject respObj;
        respObj["jsonrpc"] = JsonValue("2.0");
        respObj["result"] = std::move(result);
        respObj["id"] = id;
        response = JsonValue(std::move(respObj));
        send_json_response(req, response);
    } catch (const std::exception& e) {
        send_error(req, -32603, std::string("Internal error: ") + e.what(), id);
    }
}

void RpcServer::send_json_response(struct evhttp_request* req, const JsonValue& response, int status) {
    std::string json = response.dump();
    struct evbuffer* buf = evbuffer_new();
    evbuffer_add(buf, json.c_str(), json.size());
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "application/json");
    evhttp_send_reply(req, status, "OK", buf);
    evbuffer_free(buf);
}

void RpcServer::send_error(struct evhttp_request* req, int code, const std::string& message, const JsonValue& id) {
    JsonValue error;
    JsonObject errObj;
    errObj["code"] = JsonValue(static_cast<double>(code));
    errObj["message"] = JsonValue(message);
    error = JsonValue(std::move(errObj));
    JsonValue response;
    JsonObject respObj;
    respObj["jsonrpc"] = JsonValue("2.0");
    respObj["error"] = std::move(error);
    respObj["id"] = id;
    response = JsonValue(std::move(respObj));
    send_json_response(req, response, 200);
}

} // namespace rpc
} // namespace auracash
