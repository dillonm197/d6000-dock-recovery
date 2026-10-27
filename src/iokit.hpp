// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Thin RAII and property helpers over IOKit and Core Foundation. Every reader here uses the
// kernel's cached IORegistry properties only and never talks to a USB-C controller.
#ifndef D6000_IOKIT_HPP
#define D6000_IOKIT_HPP

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <cstdint>
#include <string>
#include <utility>

namespace d6000::iokit
{

// Owns one IOKit object reference (a service, registry entry or iterator) and releases it on scope
// exit (C++ Core Guidelines R.1). Move-only.
class Object
{
  public:
    Object() noexcept = default;
    explicit Object(io_object_t object) noexcept : object_(object)
    {
    }
    ~Object()
    {
        reset();
    }
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;
    Object(Object &&other) noexcept : object_(std::exchange(other.object_, IO_OBJECT_NULL))
    {
    }
    Object &operator=(Object &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            object_ = std::exchange(other.object_, IO_OBJECT_NULL);
        }
        return *this;
    }

    // Takes an additional reference to an object that is owned elsewhere.
    [[nodiscard]] static Object retain(io_object_t object) noexcept
    {
        if (object != IO_OBJECT_NULL)
        {
            static_cast<void>(IOObjectRetain(object));
        }
        return Object(object);
    }

    [[nodiscard]] io_object_t get() const noexcept
    {
        return object_;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return object_ != IO_OBJECT_NULL;
    }
    // Releases the current object and exposes the slot to an IOKit out-parameter.
    [[nodiscard]] io_object_t *put() noexcept
    {
        reset();
        return &object_;
    }
    void reset() noexcept
    {
        if (object_ != IO_OBJECT_NULL)
        {
            static_cast<void>(IOObjectRelease(object_));
            object_ = IO_OBJECT_NULL;
        }
    }

  private:
    io_object_t object_ = IO_OBJECT_NULL;
};

// Owns one Core Foundation reference obtained under the Create rule and releases it on scope exit.
class CFObject
{
  public:
    CFObject() noexcept = default;
    explicit CFObject(CFTypeRef object) noexcept : object_(object)
    {
    }
    ~CFObject()
    {
        reset();
    }
    CFObject(const CFObject &) = delete;
    CFObject &operator=(const CFObject &) = delete;
    CFObject(CFObject &&other) noexcept : object_(std::exchange(other.object_, nullptr))
    {
    }
    CFObject &operator=(CFObject &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            object_ = std::exchange(other.object_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] CFTypeRef get() const noexcept
    {
        return object_;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return object_ != nullptr;
    }
    void reset() noexcept
    {
        if (object_ != nullptr)
        {
            CFRelease(object_);
            object_ = nullptr;
        }
    }

  private:
    CFTypeRef object_ = nullptr;
};

// One cached registry property, or an empty CFObject when it is missing.
[[nodiscard]] CFObject property(io_registry_entry_t entry, const char *key);

// The value as UTF-8 when it is a CFString, otherwise empty.
[[nodiscard]] std::string toString(CFTypeRef value);

// Typed property readers. A missing or differently typed property returns the fallback.
[[nodiscard]] std::string stringProperty(io_registry_entry_t entry, const char *key);
[[nodiscard]] std::int64_t numberProperty(io_registry_entry_t entry, const char *key, std::int64_t fallback = -1);
[[nodiscard]] bool boolProperty(io_registry_entry_t entry, const char *key);
[[nodiscard]] std::string dataProperty(io_registry_entry_t entry, const char *key);

// The entry's path in the IOService plane, or empty.
[[nodiscard]] std::string registryPath(io_registry_entry_t entry);

// The entry's registry ID: unique for the life of the entry, so a re-created port gets a new one.
[[nodiscard]] std::uint64_t registryEntryId(io_registry_entry_t entry);

// The entry's parent in the IOService plane, or an empty Object.
[[nodiscard]] Object parent(io_registry_entry_t entry);

// Calls visit(entry) for every registered service of className until visit returns false.
template <typename Visit> void forEachService(const char *className, const Visit &visit)
{
    Object iterator;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(className), iterator.put()) != KERN_SUCCESS)
    {
        return;
    }
    while (const Object entry{IOIteratorNext(iterator.get())})
    {
        if (!visit(entry.get()))
        {
            return;
        }
    }
}

} // namespace d6000::iokit

#endif // D6000_IOKIT_HPP
