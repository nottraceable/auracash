#include "RpcClient.h"
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace auracash {
namespace rpc {

static std::mutex g_curlMutex;

RpcClient::RpcClient(const std::string& host, uint16_t port)
    : m_host(host), m_port(port), m_curl(nullptr), m_headers(nullptr) {
    std::lock_guard<std::mutex> lock(g_curlMutex);
    m_curl = curl_easy_init();
    if (!m_curl) {
        throw std::runtime_error("Failed to initialize Curl");
    }
    std::string url = "http://" + m_host + ":" + std::to_string(m_port);
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, RpcClient::write_callback);
    curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "auracash-client/3.0");
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
    m_headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_headers);
}

RpcClient::~RpcClient() {
    std::lock_guard<std::mutex> lock(g_curlMutex);
    if (m_curl) {
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
    if (m_headers) {
        curl_slist_free_all(m_headers);
        m_headers = nullptr;
    }
}

std::string RpcClient::build_request(const std::string& method, const JsonArray& params) {
    JsonObject reqObj;
    reqObj["jsonrpc"] = JsonValue("2.0");
    reqObj["method"] = JsonValue(method);
    reqObj["params"] = JsonValue(params);
    JsonValue wrapper(std::move(reqObj));
    return wrapper.dump();
}

size_t RpcClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    const size_t totalSize = size * nmemb;
    out->append(ptr, totalSize);
    return totalSize;
}

bool RpcClient::call(const std::string& method, const JsonArray& params, JsonValue& result) {
    std::string requestBody = build_request(method, params);
    std::lock_guard<std::mutex> lock(g_curlMutex);
    std::string responseBody;
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, requestBody.size());
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, requestBody.c_str());
    CURLcode res = curl_easy_perform(m_curl);
    if (res != CURLE_OK) {
        return false;
    }
    JsonValue raw = auracash::json::parse(responseBody);
    if (!raw.is_object()) {
        return false;
    }
    if (raw.as_object().contains("error")) {
        return false;
    }
    result = raw.as_object().at("result");
    return true;
}

bool RpcClient::batch_call(
    const std::vector<std::pair<std::string, JsonArray>>& calls,
    std::vector<JsonValue>& results
) {
    std::lock_guard<std::mutex> lock(g_curlMutex);
    std::string responseBody;
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, 0);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, nullptr);
    std::ostringstream oss;
    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& [method, params] = calls[i];
        JsonObject reqObj;
        reqObj["jsonrpc"] = JsonValue("2.0");
        reqObj["method"] = JsonValue(method);
        reqObj["params"] = JsonValue(params);
        reqObj["id"] = JsonValue(static_cast<long long>(i));
        JsonValue wrapper(std::move(reqObj));
        oss << wrapper.dump();
        if (i + 1 < calls.size()) {
            oss << "\n";
        }
    }
    std::string batchStr = oss.str();
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, batchStr.size());
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, batchStr.c_str());
    CURLcode res = curl_easy_perform(m_curl);
    if (res != CURLE_OK) {
        return false;
    }
    JsonValue raw = auracash::json::parse(responseBody);
    if (!raw.is_array()) {
        return false;
    }
    results.clear();
    for (const auto& item : raw.as_array()) {
        if (item.is_object()) {
            const auto& obj = item.as_object();
            if (obj.contains("error")) {
                results.emplace_back();
            } else {
                results.emplace_back(obj.at("result"));
            }
        } else {
            results.emplace_back();
        }
    }
    return true;
}

} // namespace rpc
} // namespace auracash
