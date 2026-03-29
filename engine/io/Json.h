#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "engine/math/Types.h"

namespace engine::io
{

// ---------------------------------------------------------------------------
// JsonValue — opaque, read-only view into a parsed JSON tree.
//
// Lightweight (pointer-sized).  Only valid while the owning JsonDocument is
// alive.  A default-constructed or missing-member JsonValue wraps a null
// pointer; all type queries return false and default-value getters return
// the fallback.
// ---------------------------------------------------------------------------

class JsonDocument;

class JsonValue
{
public:
    JsonValue() = default;

    // Type queries
    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isUint() const;
    bool isFloat() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    // Typed accessors -- assert on type mismatch in debug, UB in release.
    bool getBool() const;
    int32_t getInt() const;
    uint32_t getUint() const;
    float getFloat() const;
    const char* getString() const;

    // Typed accessors with fallback -- return defaultVal on type mismatch.
    bool getBool(bool defaultVal) const;
    int32_t getInt(int32_t defaultVal) const;
    uint32_t getUint(uint32_t defaultVal) const;
    float getFloat(float defaultVal) const;
    const char* getString(const char* defaultVal) const;

    // Math helpers -- read directly into engine math types.
    // Expects JSON arrays of the correct length: [x,y], [x,y,z], [x,y,z,w].
    math::Vec2 getVec2() const;
    math::Vec3 getVec3() const;
    math::Vec4 getVec4() const;
    math::Quat getQuat() const;  // [x, y, z, w]

    // Object member access -- returns a null JsonValue if key not found.
    JsonValue operator[](const char* key) const;
    bool hasMember(const char* key) const;

    // Array access
    std::size_t arraySize() const;
    JsonValue operator[](std::size_t index) const;

    // Iteration (range-for support)
    class Iterator;
    Iterator begin() const;
    Iterator end() const;

    // For object iteration: name of current member (valid only during
    // object iteration via the Iterator).
    const char* memberName() const;

private:
    friend class JsonDocument;

    // Opaque pointer to rapidjson::Value; never dereferenced outside Json.cpp.
    const void* val_ = nullptr;

    // For object member iteration -- stores the member name from
    // rapidjson::Value::Member so that memberName() can return it.
    const char* memberName_ = nullptr;
};

// ---------------------------------------------------------------------------
// JsonValue::Iterator
// ---------------------------------------------------------------------------

class JsonValue::Iterator
{
public:
    Iterator() = default;

    JsonValue operator*() const;
    Iterator& operator++();
    bool operator!=(const Iterator& other) const;

private:
    friend class JsonValue;

    // These are opaque pointers to either:
    //   rapidjson::Value::ConstValueIterator  (array iteration)
    //   rapidjson::Value::ConstMemberIterator (object iteration)
    const void* ptr_ = nullptr;
    bool isObject_ = false;
};

// ---------------------------------------------------------------------------
// JsonDocument — owns a parsed JSON tree.
//
// Move-only.  After a successful parse(), root() returns the top-level
// JsonValue.  On failure, hasError() is true and errorMessage() /
// errorOffset() describe the problem.
// ---------------------------------------------------------------------------

class JsonDocument
{
public:
    JsonDocument();
    ~JsonDocument();

    // Move-only (owns the rapidjson allocator)
    JsonDocument(JsonDocument&&) noexcept;
    JsonDocument& operator=(JsonDocument&&) noexcept;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;

    // Parse from in-memory string. Returns false on parse error.
    bool parse(const char* json, std::size_t length);
    bool parse(std::string_view json);

    // Parse from file on disk. Returns false if file cannot be read or
    // parse fails.
    bool parseFile(const char* path);

    // Root value of the parsed document.
    JsonValue root() const;

    // Error reporting after a failed parse.
    const char* errorMessage() const;
    std::size_t errorOffset() const;
    bool hasError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// JsonWriter — SAX-style JSON builder.
//
// Move-only.  Produces either pretty-printed or compact JSON depending on
// the constructor flag.  Call getString() to retrieve the built string, or
// writeToFile() to flush directly to disk.
// ---------------------------------------------------------------------------

class JsonWriter
{
public:
    explicit JsonWriter(bool prettyPrint = true);
    ~JsonWriter();

    JsonWriter(JsonWriter&&) noexcept;
    JsonWriter& operator=(JsonWriter&&) noexcept;
    JsonWriter(const JsonWriter&) = delete;
    JsonWriter& operator=(const JsonWriter&) = delete;

    // Structure
    void startObject();
    void endObject();
    void startArray();
    void endArray();
    void key(const char* name);

    // Scalars
    void writeBool(bool v);
    void writeInt(int32_t v);
    void writeUint(uint32_t v);
    void writeFloat(float v);
    void writeString(const char* v);
    void writeNull();

    // Math helpers -- write as JSON arrays
    void writeVec2(const math::Vec2& v);
    void writeVec3(const math::Vec3& v);
    void writeVec4(const math::Vec4& v);
    void writeQuat(const math::Quat& v);  // [x, y, z, w]

    // Retrieve the built JSON string.
    const char* getString() const;
    std::size_t getLength() const;

    // Write directly to a file.  Returns false on I/O error.
    bool writeToFile(const char* path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::io
