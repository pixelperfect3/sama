#include "engine/input/ActionMapJson.h"

#include <cstring>
#include <string>
#include <utility>

#include "engine/io/Json.h"

namespace engine::input
{

// ---------------------------------------------------------------------------
// Key name <-> enum tables
// ---------------------------------------------------------------------------

struct KeyNameEntry
{
    const char* name;
    Key key;
};

// clang-format off
static const KeyNameEntry kKeyNames[] = {
    {"A", Key::A}, {"B", Key::B}, {"C", Key::C}, {"D", Key::D},
    {"E", Key::E}, {"F", Key::F}, {"G", Key::G}, {"H", Key::H},
    {"I", Key::I}, {"J", Key::J}, {"K", Key::K}, {"L", Key::L},
    {"M", Key::M}, {"N", Key::N}, {"O", Key::O}, {"P", Key::P},
    {"Q", Key::Q}, {"R", Key::R}, {"S", Key::S}, {"T", Key::T},
    {"U", Key::U}, {"V", Key::V}, {"W", Key::W}, {"X", Key::X},
    {"Y", Key::Y}, {"Z", Key::Z},
    {"Num0", Key::Num0}, {"Num1", Key::Num1}, {"Num2", Key::Num2},
    {"Num3", Key::Num3}, {"Num4", Key::Num4}, {"Num5", Key::Num5},
    {"Num6", Key::Num6}, {"Num7", Key::Num7}, {"Num8", Key::Num8},
    {"Num9", Key::Num9},
    {"F1", Key::F1}, {"F2", Key::F2}, {"F3", Key::F3}, {"F4", Key::F4},
    {"F5", Key::F5}, {"F6", Key::F6}, {"F7", Key::F7}, {"F8", Key::F8},
    {"F9", Key::F9}, {"F10", Key::F10}, {"F11", Key::F11}, {"F12", Key::F12},
    {"Space", Key::Space}, {"Enter", Key::Enter}, {"Escape", Key::Escape},
    {"Tab", Key::Tab}, {"Backspace", Key::Backspace}, {"Delete", Key::Delete},
    {"Insert", Key::Insert},
    {"Left", Key::Left}, {"Right", Key::Right}, {"Up", Key::Up}, {"Down", Key::Down},
    {"Home", Key::Home}, {"End", Key::End}, {"PageUp", Key::PageUp},
    {"PageDown", Key::PageDown},
    {"LeftShift", Key::LeftShift}, {"RightShift", Key::RightShift},
    {"LeftCtrl", Key::LeftCtrl}, {"RightCtrl", Key::RightCtrl},
    {"LeftAlt", Key::LeftAlt}, {"RightAlt", Key::RightAlt},
    {"LeftSuper", Key::LeftSuper}, {"RightSuper", Key::RightSuper},
    {"Apostrophe", Key::Apostrophe}, {"Comma", Key::Comma},
    {"Minus", Key::Minus}, {"Period", Key::Period}, {"Slash", Key::Slash},
    {"Semicolon", Key::Semicolon}, {"Equal", Key::Equal},
    {"LeftBracket", Key::LeftBracket}, {"Backslash", Key::Backslash},
    {"RightBracket", Key::RightBracket}, {"GraveAccent", Key::GraveAccent},
    {"Numpad0", Key::Numpad0}, {"Numpad1", Key::Numpad1},
    {"Numpad2", Key::Numpad2}, {"Numpad3", Key::Numpad3},
    {"Numpad4", Key::Numpad4}, {"Numpad5", Key::Numpad5},
    {"Numpad6", Key::Numpad6}, {"Numpad7", Key::Numpad7},
    {"Numpad8", Key::Numpad8}, {"Numpad9", Key::Numpad9},
    {"NumpadDecimal", Key::NumpadDecimal}, {"NumpadDivide", Key::NumpadDivide},
    {"NumpadMultiply", Key::NumpadMultiply}, {"NumpadSubtract", Key::NumpadSubtract},
    {"NumpadAdd", Key::NumpadAdd}, {"NumpadEnter", Key::NumpadEnter},
};
// clang-format on

static constexpr size_t kKeyNameCount = sizeof(kKeyNames) / sizeof(kKeyNames[0]);

static bool parseKey(const char* name, Key& out)
{
    if (!name)
        return false;
    for (size_t i = 0; i < kKeyNameCount; ++i)
    {
        if (std::strcmp(kKeyNames[i].name, name) == 0)
        {
            out = kKeyNames[i].key;
            return true;
        }
    }
    return false;
}

static const char* keyToString(Key k)
{
    for (size_t i = 0; i < kKeyNameCount; ++i)
    {
        if (kKeyNames[i].key == k)
            return kKeyNames[i].name;
    }
    return nullptr;
}

struct MouseButtonNameEntry
{
    const char* name;
    MouseButton button;
};

static const MouseButtonNameEntry kMouseButtonNames[] = {
    {"Left", MouseButton::Left},
    {"Right", MouseButton::Right},
    {"Middle", MouseButton::Middle},
};

static constexpr size_t kMouseButtonNameCount =
    sizeof(kMouseButtonNames) / sizeof(kMouseButtonNames[0]);

static bool parseMouseButton(const char* name, MouseButton& out)
{
    if (!name)
        return false;
    for (size_t i = 0; i < kMouseButtonNameCount; ++i)
    {
        if (std::strcmp(kMouseButtonNames[i].name, name) == 0)
        {
            out = kMouseButtonNames[i].button;
            return true;
        }
    }
    return false;
}

static const char* mouseButtonToString(MouseButton b)
{
    for (size_t i = 0; i < kMouseButtonNameCount; ++i)
    {
        if (kMouseButtonNames[i].button == b)
            return kMouseButtonNames[i].name;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

bool loadActionMap(const char* filepath, ActionMap& map,
                   std::deque<std::string>& ownedStrings)
{
    io::JsonDocument doc;
    if (!doc.parseFile(filepath))
    {
        return false;
    }

    auto root = doc.root();
    if (!root.isObject())
    {
        return false;
    }

    // keys
    auto keys = root["keys"];
    if (keys.isObject())
    {
        for (auto member : keys)
        {
            Key k;
            if (parseKey(member.memberName(), k) && member.isString())
            {
                ownedStrings.emplace_back(member.getString());
                map.bindKey(k, ownedStrings.back());
            }
        }
    }

    // mouseButtons
    auto mouseButtons = root["mouseButtons"];
    if (mouseButtons.isObject())
    {
        for (auto member : mouseButtons)
        {
            MouseButton b;
            if (parseMouseButton(member.memberName(), b) && member.isString())
            {
                ownedStrings.emplace_back(member.getString());
                map.bindMouseButton(b, ownedStrings.back());
            }
        }
    }

    // axes
    auto axes = root["axes"];
    if (axes.isArray())
    {
        for (size_t i = 0; i < axes.arraySize(); ++i)
        {
            auto axis = axes[i];
            if (!axis.isObject())
                continue;

            auto nameVal = axis["name"];
            auto negVal = axis["negative"];
            auto posVal = axis["positive"];

            if (!nameVal.isString() || !negVal.isString() || !posVal.isString())
                continue;

            Key neg, pos;
            if (!parseKey(negVal.getString(), neg) || !parseKey(posVal.getString(), pos))
                continue;

            ownedStrings.emplace_back(nameVal.getString());
            map.bindAxis(ownedStrings.back(), neg, pos);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

bool saveActionMap(const ActionMap& map, const char* filepath)
{
    io::JsonWriter w(true);
    w.startObject();

    // keys
    w.key("keys");
    w.startObject();
    for (size_t i = 0; i < static_cast<size_t>(Key::COUNT); ++i)
    {
        auto action = map.keyAction(static_cast<Key>(i));
        if (action.empty())
            continue;
        const char* name = keyToString(static_cast<Key>(i));
        if (!name)
            continue;
        w.key(name);
        w.writeString(std::string(action).c_str());
    }
    w.endObject();

    // mouseButtons
    w.key("mouseButtons");
    w.startObject();
    for (size_t i = 0; i < static_cast<size_t>(MouseButton::COUNT); ++i)
    {
        auto action = map.mouseButtonAction(static_cast<MouseButton>(i));
        if (action.empty())
            continue;
        const char* name = mouseButtonToString(static_cast<MouseButton>(i));
        if (!name)
            continue;
        w.key(name);
        w.writeString(std::string(action).c_str());
    }
    w.endObject();

    // axes
    w.key("axes");
    w.startArray();
    for (const auto& axis : map.axisBindings())
    {
        const char* negName = keyToString(axis.negative);
        const char* posName = keyToString(axis.positive);
        if (!negName || !posName)
            continue;

        w.startObject();
        w.key("name");
        w.writeString(std::string(axis.name).c_str());
        w.key("negative");
        w.writeString(negName);
        w.key("positive");
        w.writeString(posName);
        w.endObject();
    }
    w.endArray();

    w.endObject();

    return w.writeToFile(filepath);
}

}  // namespace engine::input
