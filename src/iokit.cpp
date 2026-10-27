// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "iokit.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace d6000::iokit
{

CFObject property(io_registry_entry_t entry, const char *key)
{
    const CFObject name{CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8)};
    if (!name)
    {
        return {};
    }
    return CFObject{
        IORegistryEntryCreateCFProperty(entry, static_cast<CFStringRef>(name.get()), kCFAllocatorDefault, 0)};
}

std::string toString(CFTypeRef value)
{
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID())
    {
        return {};
    }
    const auto *const string = static_cast<CFStringRef>(value);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8) + 1;
    std::string text(static_cast<std::size_t>(capacity), '\0');
    if (CFStringGetCString(string, text.data(), capacity, kCFStringEncodingUTF8) == 0)
    {
        return {};
    }
    text.resize(std::char_traits<char>::length(text.c_str()));
    return text;
}

std::string stringProperty(io_registry_entry_t entry, const char *key)
{
    return toString(property(entry, key).get());
}

std::int64_t numberProperty(io_registry_entry_t entry, const char *key, std::int64_t fallback)
{
    const CFObject value = property(entry, key);
    std::int64_t result = fallback;
    if (value && CFGetTypeID(value.get()) == CFNumberGetTypeID())
    {
        static_cast<void>(CFNumberGetValue(static_cast<CFNumberRef>(value.get()), kCFNumberSInt64Type, &result));
    }
    return result;
}

bool boolProperty(io_registry_entry_t entry, const char *key)
{
    const CFObject value = property(entry, key);
    return value && CFGetTypeID(value.get()) == CFBooleanGetTypeID() &&
           CFBooleanGetValue(static_cast<CFBooleanRef>(value.get())) != 0;
}

std::string dataProperty(io_registry_entry_t entry, const char *key)
{
    const CFObject value = property(entry, key);
    if (!value || CFGetTypeID(value.get()) != CFDataGetTypeID())
    {
        return {};
    }
    const auto *const data = static_cast<CFDataRef>(value.get());
    const std::span bytes{CFDataGetBytePtr(data), static_cast<std::size_t>(CFDataGetLength(data))};
    return {bytes.begin(), bytes.end()};
}

std::string registryPath(io_registry_entry_t entry)
{
    std::array<char, sizeof(io_string_t)> path{};
    if (IORegistryEntryGetPath(entry, kIOServicePlane, path.data()) != KERN_SUCCESS)
    {
        return {};
    }
    return path.data();
}

std::uint64_t registryEntryId(io_registry_entry_t entry)
{
    std::uint64_t id = 0;
    static_cast<void>(IORegistryEntryGetRegistryEntryID(entry, &id));
    return id;
}

Object parent(io_registry_entry_t entry)
{
    io_registry_entry_t result = IO_OBJECT_NULL;
    if (IORegistryEntryGetParentEntry(entry, kIOServicePlane, &result) != KERN_SUCCESS)
    {
        return {};
    }
    return Object{result};
}

} // namespace d6000::iokit
