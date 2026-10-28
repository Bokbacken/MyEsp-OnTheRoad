#pragma once

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

namespace TestNvs {
inline std::map<std::string, uint64_t> numbers;
inline std::map<std::string, std::string> strings;
}

namespace MyNvs {
class Handle {
public:
    Handle(const char *, bool) {}
    explicit operator bool() const { return true; }
    uint8_t getU8(const char *key, uint8_t def) const
    {
        const auto it = TestNvs::numbers.find(key);
        return it == TestNvs::numbers.end() ? def : static_cast<uint8_t>(it->second);
    }
    uint16_t getU16(const char *key, uint16_t def) const
    {
        const auto it = TestNvs::numbers.find(key);
        return it == TestNvs::numbers.end() ? def : static_cast<uint16_t>(it->second);
    }
    bool getBool(const char *key, bool def) const { return getU8(key, def ? 1 : 0) != 0; }
    bool getStr(const char *key, char *out, size_t cap) const
    {
        const auto it = TestNvs::strings.find(key);
        if (it == TestNvs::strings.end() || cap == 0) return false;
        std::strncpy(out, it->second.c_str(), cap - 1);
        out[cap - 1] = 0;
        return true;
    }
    size_t blobSize(const char *) const { return 0; }
    bool getBlob(const char *, void *, size_t) const { return false; }
    bool setU8(const char *key, uint8_t value) { TestNvs::numbers[key] = value; return true; }
    bool setU16(const char *key, uint16_t value) { TestNvs::numbers[key] = value; return true; }
    bool setBool(const char *key, bool value) { return setU8(key, value ? 1 : 0); }
    bool setStr(const char *key, const char *value) { TestNvs::strings[key] = value; return true; }
    bool setBlob(const char *, const void *, size_t) { return true; }
    bool erase(const char *key)
    {
        TestNvs::numbers.erase(key);
        TestNvs::strings.erase(key);
        return true;
    }
    bool eraseAll()
    {
        TestNvs::numbers.clear();
        TestNvs::strings.clear();
        return true;
    }
    bool commit() { return true; }
};
}  // namespace MyNvs
