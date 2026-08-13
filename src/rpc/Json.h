#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <cstdint>
#include <type_traits>

namespace auracash {
namespace json {

class Value {
public:
    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;

    Value() : m_data(std::monostate{}) {}
    Value(std::nullptr_t) : m_data(std::monostate{}) {}
    Value(bool b) : m_data(b) {}
    Value(double d) : m_data(d) {}

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
    Value(T n) {
        if constexpr (std::is_signed_v<T>) {
            m_data = static_cast<int64_t>(n);
        } else {
            m_data = static_cast<uint64_t>(n);
        }
    }

    Value(const std::string& s) : m_data(s) {}
    Value(const char* s) : m_data(std::string(s)) {}
    Value(Object o) : m_data(std::move(o)) {}
    Value(Array a) : m_data(std::move(a)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(m_data); }
    bool is_bool() const { return std::holds_alternative<bool>(m_data); }
    bool is_number() const {
        return std::holds_alternative<double>(m_data) ||
               std::holds_alternative<int64_t>(m_data) ||
               std::holds_alternative<uint64_t>(m_data);
    }
    bool is_int64() const { return std::holds_alternative<int64_t>(m_data); }
    bool is_uint64() const { return std::holds_alternative<uint64_t>(m_data); }
    bool is_string() const { return std::holds_alternative<std::string>(m_data); }
    bool is_object() const { return std::holds_alternative<Object>(m_data); }
    bool is_array() const { return std::holds_alternative<Array>(m_data); }

    bool as_bool() const { return std::get<bool>(m_data); }
    double as_number() const {
        if (std::holds_alternative<double>(m_data)) return std::get<double>(m_data);
        if (std::holds_alternative<int64_t>(m_data)) return static_cast<double>(std::get<int64_t>(m_data));
        if (std::holds_alternative<uint64_t>(m_data)) return static_cast<double>(std::get<uint64_t>(m_data));
        throw std::bad_variant_access();
    }
    int64_t as_int64() const {
        if (std::holds_alternative<int64_t>(m_data)) return std::get<int64_t>(m_data);
        if (std::holds_alternative<uint64_t>(m_data)) return static_cast<int64_t>(std::get<uint64_t>(m_data));
        if (std::holds_alternative<double>(m_data)) return static_cast<int64_t>(std::get<double>(m_data));
        throw std::bad_variant_access();
    }
    uint64_t as_uint64() const {
        if (std::holds_alternative<uint64_t>(m_data)) return std::get<uint64_t>(m_data);
        if (std::holds_alternative<int64_t>(m_data)) return static_cast<uint64_t>(std::get<int64_t>(m_data));
        if (std::holds_alternative<double>(m_data)) return static_cast<uint64_t>(std::get<double>(m_data));
        throw std::bad_variant_access();
    }
    const std::string& as_string() const { return std::get<std::string>(m_data); }
    const Object& as_object() const { return std::get<Object>(m_data); }
    const Array& as_array() const { return std::get<Array>(m_data); }

    Value& operator[](const std::string& key) { return std::get<Object>(m_data)[key]; }
    const Value& at(const std::string& key) const {
        static const Value none;
        auto it = std::get<Object>(m_data).find(key);
        return it == std::get<Object>(m_data).end() ? none : it->second;
    }

    std::string dump() const;

private:
    std::variant<std::monostate, bool, double, int64_t, uint64_t, std::string, Object, Array> m_data;
};

// Parse a JSON string. Returns empty Value (null) on error.
Value parse(const std::string& text);

// Escape a string for JSON output.
std::string escape(const std::string& s);

} // namespace json
} // namespace auracash
