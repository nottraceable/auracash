#pragma once
#include "Json.h"
#include <string>
#include <string_view>
#include <curl/curl.h>

namespace auracash {
namespace rpc {

using JsonValue = auracash::json::Value;
using JsonArray = JsonValue::Array;
using JsonObject = JsonValue::Object;

class RpcClient {
public:
    RpcClient(const std::string& host, uint16_t port);
    ~RpcClient();

    bool call(const std::string& method, const JsonArray& params, JsonValue& result);
    bool batch_call(const std::vector<std::pair<std::string, JsonArray>>& calls, std::vector<JsonValue>& results);

private:
    std::string m_host;
    uint16_t m_port = 0;
    CURL* m_curl = nullptr;
    struct curl_slist* m_headers = nullptr;

    std::string build_request(const std::string& method, const JsonArray& params);
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};

} // namespace rpc
} // namespace auracash
