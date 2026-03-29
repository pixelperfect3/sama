#include "engine/io/Json.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// rapidjson headers -- only included in this translation unit.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#pragma GCC diagnostic pop

namespace engine::io
{

// ===========================================================================
// Helpers — cast between opaque void* and rapidjson types
// ===========================================================================

static const rapidjson::Value* asRjValue(const void* p)
{
    return static_cast<const rapidjson::Value*>(p);
}

// ===========================================================================
// JsonValue
// ===========================================================================

bool JsonValue::isNull() const
{
    return val_ == nullptr || asRjValue(val_)->IsNull();
}

bool JsonValue::isBool() const
{
    return val_ != nullptr && asRjValue(val_)->IsBool();
}

bool JsonValue::isInt() const
{
    return val_ != nullptr && asRjValue(val_)->IsInt();
}

bool JsonValue::isUint() const
{
    return val_ != nullptr && asRjValue(val_)->IsUint();
}

bool JsonValue::isFloat() const
{
    return val_ != nullptr && (asRjValue(val_)->IsNumber());
}

bool JsonValue::isString() const
{
    return val_ != nullptr && asRjValue(val_)->IsString();
}

bool JsonValue::isArray() const
{
    return val_ != nullptr && asRjValue(val_)->IsArray();
}

bool JsonValue::isObject() const
{
    return val_ != nullptr && asRjValue(val_)->IsObject();
}

// ---------- asserting getters ----------

bool JsonValue::getBool() const
{
    assert(val_ && asRjValue(val_)->IsBool());
    return asRjValue(val_)->GetBool();
}

int32_t JsonValue::getInt() const
{
    assert(val_ && asRjValue(val_)->IsInt());
    return asRjValue(val_)->GetInt();
}

uint32_t JsonValue::getUint() const
{
    assert(val_ && asRjValue(val_)->IsUint());
    return asRjValue(val_)->GetUint();
}

float JsonValue::getFloat() const
{
    assert(val_ && asRjValue(val_)->IsNumber());
    return static_cast<float>(asRjValue(val_)->GetDouble());
}

const char* JsonValue::getString() const
{
    assert(val_ && asRjValue(val_)->IsString());
    return asRjValue(val_)->GetString();
}

// ---------- default-value getters ----------

bool JsonValue::getBool(bool defaultVal) const
{
    if (!val_ || !asRjValue(val_)->IsBool())
        return defaultVal;
    return asRjValue(val_)->GetBool();
}

int32_t JsonValue::getInt(int32_t defaultVal) const
{
    if (!val_ || !asRjValue(val_)->IsInt())
        return defaultVal;
    return asRjValue(val_)->GetInt();
}

uint32_t JsonValue::getUint(uint32_t defaultVal) const
{
    if (!val_ || !asRjValue(val_)->IsUint())
        return defaultVal;
    return asRjValue(val_)->GetUint();
}

float JsonValue::getFloat(float defaultVal) const
{
    if (!val_ || !asRjValue(val_)->IsNumber())
        return defaultVal;
    return static_cast<float>(asRjValue(val_)->GetDouble());
}

const char* JsonValue::getString(const char* defaultVal) const
{
    if (!val_ || !asRjValue(val_)->IsString())
        return defaultVal;
    return asRjValue(val_)->GetString();
}

// ---------- math helpers ----------

math::Vec2 JsonValue::getVec2() const
{
    assert(val_ && asRjValue(val_)->IsArray() && asRjValue(val_)->Size() >= 2);
    const auto& arr = *asRjValue(val_);
    return math::Vec2(static_cast<float>(arr[0u].GetDouble()),
                      static_cast<float>(arr[1u].GetDouble()));
}

math::Vec3 JsonValue::getVec3() const
{
    assert(val_ && asRjValue(val_)->IsArray() && asRjValue(val_)->Size() >= 3);
    const auto& arr = *asRjValue(val_);
    return math::Vec3(static_cast<float>(arr[0u].GetDouble()),
                      static_cast<float>(arr[1u].GetDouble()),
                      static_cast<float>(arr[2u].GetDouble()));
}

math::Vec4 JsonValue::getVec4() const
{
    assert(val_ && asRjValue(val_)->IsArray() && asRjValue(val_)->Size() >= 4);
    const auto& arr = *asRjValue(val_);
    return math::Vec4(
        static_cast<float>(arr[0u].GetDouble()), static_cast<float>(arr[1u].GetDouble()),
        static_cast<float>(arr[2u].GetDouble()), static_cast<float>(arr[3u].GetDouble()));
}

math::Quat JsonValue::getQuat() const
{
    assert(val_ && asRjValue(val_)->IsArray() && asRjValue(val_)->Size() >= 4);
    const auto& arr = *asRjValue(val_);
    // glm::quat constructor order: w, x, y, z
    // JSON order: [x, y, z, w]
    return math::Quat(
        static_cast<float>(arr[3u].GetDouble()), static_cast<float>(arr[0u].GetDouble()),
        static_cast<float>(arr[1u].GetDouble()), static_cast<float>(arr[2u].GetDouble()));
}

// ---------- object member access ----------

JsonValue JsonValue::operator[](const char* key) const
{
    if (!val_ || !asRjValue(val_)->IsObject())
        return {};
    auto it = asRjValue(val_)->FindMember(key);
    if (it == asRjValue(val_)->MemberEnd())
        return {};
    JsonValue jv;
    jv.val_ = &it->value;
    return jv;
}

bool JsonValue::hasMember(const char* key) const
{
    if (!val_ || !asRjValue(val_)->IsObject())
        return false;
    return asRjValue(val_)->HasMember(key);
}

// ---------- array access ----------

std::size_t JsonValue::arraySize() const
{
    if (!val_ || !asRjValue(val_)->IsArray())
        return 0;
    return asRjValue(val_)->Size();
}

JsonValue JsonValue::operator[](std::size_t index) const
{
    if (!val_ || !asRjValue(val_)->IsArray())
        return {};
    if (index >= asRjValue(val_)->Size())
        return {};
    JsonValue jv;
    jv.val_ = &(*asRjValue(val_))[static_cast<rapidjson::SizeType>(index)];
    return jv;
}

// ---------- iteration ----------

JsonValue::Iterator JsonValue::begin() const
{
    Iterator it;
    if (!val_)
        return it;

    if (asRjValue(val_)->IsArray())
    {
        it.ptr_ = asRjValue(val_)->Begin();
        it.isObject_ = false;
    }
    else if (asRjValue(val_)->IsObject())
    {
        it.isObject_ = true;
        if (asRjValue(val_)->MemberCount() > 0)
        {
            it.ptr_ = static_cast<const void*>(&*asRjValue(val_)->MemberBegin());
        }
    }
    return it;
}

JsonValue::Iterator JsonValue::end() const
{
    Iterator it;
    if (!val_)
        return it;

    if (asRjValue(val_)->IsArray())
    {
        it.ptr_ = asRjValue(val_)->End();
        it.isObject_ = false;
    }
    else if (asRjValue(val_)->IsObject())
    {
        it.isObject_ = true;
        if (asRjValue(val_)->MemberCount() > 0)
        {
            // MemberBegin points to first member; advance by MemberCount
            // to get past-the-end.
            auto count = asRjValue(val_)->MemberCount();
            it.ptr_ = static_cast<const void*>(&*(asRjValue(val_)->MemberBegin()) + count);
        }
    }
    return it;
}

const char* JsonValue::memberName() const
{
    return memberName_;
}

// ===========================================================================
// JsonValue::Iterator
// ===========================================================================

// rapidjson::GenericMember is the public type for object members.
using RjMember = rapidjson::GenericMember<rapidjson::UTF8<>, rapidjson::MemoryPoolAllocator<>>;

JsonValue JsonValue::Iterator::operator*() const
{
    JsonValue jv;
    if (isObject_)
    {
        auto* member = static_cast<const RjMember*>(ptr_);
        jv.val_ = &member->value;
        jv.memberName_ = member->name.GetString();
    }
    else
    {
        jv.val_ = static_cast<const rapidjson::Value*>(ptr_);
    }
    return jv;
}

JsonValue::Iterator& JsonValue::Iterator::operator++()
{
    if (isObject_)
    {
        auto* member = static_cast<const RjMember*>(ptr_);
        ptr_ = member + 1;
    }
    else
    {
        auto* val = static_cast<const rapidjson::Value*>(ptr_);
        ptr_ = val + 1;
    }
    return *this;
}

bool JsonValue::Iterator::operator!=(const Iterator& other) const
{
    return ptr_ != other.ptr_;
}

// ===========================================================================
// JsonDocument::Impl
// ===========================================================================

struct JsonDocument::Impl
{
    rapidjson::Document doc;
    bool hasError = false;
    std::string errorMsg;
    std::size_t errorOffset = 0;
};

// ===========================================================================
// JsonDocument
// ===========================================================================

JsonDocument::JsonDocument() : impl_(std::make_unique<Impl>()) {}

JsonDocument::~JsonDocument() = default;

JsonDocument::JsonDocument(JsonDocument&&) noexcept = default;
JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;

bool JsonDocument::parse(const char* json, std::size_t length)
{
    impl_->doc.Parse(json, length);
    if (impl_->doc.HasParseError())
    {
        impl_->hasError = true;
        impl_->errorMsg = rapidjson::GetParseError_En(impl_->doc.GetParseError());
        impl_->errorOffset = impl_->doc.GetErrorOffset();
        return false;
    }
    impl_->hasError = false;
    impl_->errorMsg.clear();
    impl_->errorOffset = 0;
    return true;
}

bool JsonDocument::parse(std::string_view json)
{
    return parse(json.data(), json.size());
}

bool JsonDocument::parseFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        impl_->hasError = true;
        impl_->errorMsg = "Cannot open file: " + std::string(path);
        impl_->errorOffset = 0;
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    return parse(content.data(), content.size());
}

JsonValue JsonDocument::root() const
{
    JsonValue jv;
    if (!impl_->hasError)
    {
        jv.val_ = static_cast<const rapidjson::Value*>(&impl_->doc);
    }
    return jv;
}

const char* JsonDocument::errorMessage() const
{
    return impl_->errorMsg.c_str();
}

std::size_t JsonDocument::errorOffset() const
{
    return impl_->errorOffset;
}

bool JsonDocument::hasError() const
{
    return impl_->hasError;
}

// ===========================================================================
// JsonWriter::Impl
// ===========================================================================

struct JsonWriter::Impl
{
    rapidjson::StringBuffer buffer;

    // We use a variant approach: if prettyPrint, we use PrettyWriter,
    // otherwise Writer.  Both share the same API so we dispatch via
    // function pointers stored at construction time.
    bool prettyPrint = true;

    // Only one of these is active.
    std::unique_ptr<rapidjson::Writer<rapidjson::StringBuffer>> writer;
    std::unique_ptr<rapidjson::PrettyWriter<rapidjson::StringBuffer>> prettyWriter;

    // Convenience: call through to whichever writer is active.
    template <typename Fn>
    void dispatch(Fn fn)
    {
        if (prettyPrint)
            fn(*prettyWriter);
        else
            fn(*writer);
    }
};

// ===========================================================================
// JsonWriter
// ===========================================================================

JsonWriter::JsonWriter(bool prettyPrint) : impl_(std::make_unique<Impl>())
{
    impl_->prettyPrint = prettyPrint;
    if (prettyPrint)
    {
        impl_->prettyWriter =
            std::make_unique<rapidjson::PrettyWriter<rapidjson::StringBuffer>>(impl_->buffer);
    }
    else
    {
        impl_->writer = std::make_unique<rapidjson::Writer<rapidjson::StringBuffer>>(impl_->buffer);
    }
}

JsonWriter::~JsonWriter() = default;

JsonWriter::JsonWriter(JsonWriter&&) noexcept = default;
JsonWriter& JsonWriter::operator=(JsonWriter&&) noexcept = default;

void JsonWriter::startObject()
{
    impl_->dispatch([](auto& w) { w.StartObject(); });
}

void JsonWriter::endObject()
{
    impl_->dispatch([](auto& w) { w.EndObject(); });
}

void JsonWriter::startArray()
{
    impl_->dispatch([](auto& w) { w.StartArray(); });
}

void JsonWriter::endArray()
{
    impl_->dispatch([](auto& w) { w.EndArray(); });
}

void JsonWriter::key(const char* name)
{
    impl_->dispatch([name](auto& w) { w.Key(name); });
}

void JsonWriter::writeBool(bool v)
{
    impl_->dispatch([v](auto& w) { w.Bool(v); });
}

void JsonWriter::writeInt(int32_t v)
{
    impl_->dispatch([v](auto& w) { w.Int(v); });
}

void JsonWriter::writeUint(uint32_t v)
{
    impl_->dispatch([v](auto& w) { w.Uint(v); });
}

void JsonWriter::writeFloat(float v)
{
    impl_->dispatch([v](auto& w) { w.Double(static_cast<double>(v)); });
}

void JsonWriter::writeString(const char* v)
{
    impl_->dispatch([v](auto& w) { w.String(v); });
}

void JsonWriter::writeNull()
{
    impl_->dispatch([](auto& w) { w.Null(); });
}

void JsonWriter::writeVec2(const math::Vec2& v)
{
    startArray();
    writeFloat(v.x);
    writeFloat(v.y);
    endArray();
}

void JsonWriter::writeVec3(const math::Vec3& v)
{
    startArray();
    writeFloat(v.x);
    writeFloat(v.y);
    writeFloat(v.z);
    endArray();
}

void JsonWriter::writeVec4(const math::Vec4& v)
{
    startArray();
    writeFloat(v.x);
    writeFloat(v.y);
    writeFloat(v.z);
    writeFloat(v.w);
    endArray();
}

void JsonWriter::writeQuat(const math::Quat& v)
{
    // JSON order: [x, y, z, w]
    startArray();
    writeFloat(v.x);
    writeFloat(v.y);
    writeFloat(v.z);
    writeFloat(v.w);
    endArray();
}

const char* JsonWriter::getString() const
{
    return impl_->buffer.GetString();
}

std::size_t JsonWriter::getLength() const
{
    return impl_->buffer.GetSize();
}

bool JsonWriter::writeToFile(const char* path) const
{
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    file.write(impl_->buffer.GetString(), static_cast<std::streamsize>(impl_->buffer.GetSize()));
    return file.good();
}

}  // namespace engine::io
