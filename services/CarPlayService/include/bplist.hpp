// Minimal binary-plist (bplist00) value model, writer and reader. Enough for AirPlay RTSP bodies.
// Writer: ASCII strings only (non-ASCII is rejected -> empty result). Reader: ASCII + UTF-16BE (BMP) strings.
// Validated against Python plistlib output (see tests/test_airplay.cpp).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace s10::bplist {

struct Value {
    enum class T { Bool, Int, Real, Str, Data, Array, Dict };
    T t = T::Int;
    bool b = false;
    int64_t i = 0;
    double r = 0;
    std::string s;
    std::vector<uint8_t> d;
    std::vector<Value> a;
    std::vector<std::pair<std::string, Value>> m;

    static Value Bool(bool v) { Value x; x.t = T::Bool; x.b = v; return x; }
    static Value Int(int64_t v) { Value x; x.t = T::Int; x.i = v; return x; }
    static Value Real(double v) { Value x; x.t = T::Real; x.r = v; return x; }
    static Value Str(std::string v) { Value x; x.t = T::Str; x.s = std::move(v); return x; }
    static Value Data(std::vector<uint8_t> v) { Value x; x.t = T::Data; x.d = std::move(v); return x; }
    static Value Array() { Value x; x.t = T::Array; return x; }
    static Value Dict() { Value x; x.t = T::Dict; return x; }

    Value& set(const std::string& k, Value v) {
        for (auto& kv : m) if (kv.first == k) { kv.second = std::move(v); return *this; }
        m.emplace_back(k, std::move(v));
        return *this;
    }
    Value& push(Value v) { a.push_back(std::move(v)); return *this; }
    const Value* get(const std::string& k) const {
        for (const auto& kv : m) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    // Tolerant integer: Int, decimal string, or 0x hex string (some head units send numbers as strings).
    std::optional<int64_t> asInt() const;
    // Tolerant bool: Bool, Int != 0, "true"/"1".
    std::optional<bool> asBool() const;
};

std::vector<uint8_t> write(const Value& root);                    // empty on failure
std::optional<Value> read(const uint8_t* data, size_t len);       // bounds-checked, depth-limited

}  // namespace s10::bplist
