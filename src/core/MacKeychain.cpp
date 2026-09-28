#include "core/MacKeychain.h"
#include <QUuid>

namespace maxchat::core {
namespace {
template<class T> struct Cf {
    T value = nullptr;
    ~Cf() { if (value) CFRelease(value); }
    Cf() = default;
    Cf(const Cf&) = delete;
    Cf& operator=(const Cf&) = delete;
};
constexpr qsizetype MaximumBytes = 128 * 1024;
}
bool macKeychainRequest(const QString& operation, const QString& id, QByteArray& value,
                        SecKeychainRef keychain) {
    if (QUuid::fromString(id).isNull() || QUuid::fromString(id).toString(QUuid::WithoutBraces) != id ||
        value.size() > MaximumBytes ||
        (operation != QLatin1String("read") && operation != QLatin1String("write") &&
         operation != QLatin1String("remove"))) return false;
    // This flag affects only the short-lived helper/test process. A locked
    // keychain must return an error instead of displaying an SSH-inaccessible UI.
    if (SecKeychainSetUserInteractionAllowed(false) != errSecSuccess) return false;
    Cf<SecKeychainRef> defaultKeychain;
    if (!keychain) {
        if (SecKeychainCopyDefault(&defaultKeychain.value) != errSecSuccess) return false;
        keychain = defaultKeychain.value;
    }
    Cf<CFMutableDictionaryRef> query;
    query.value = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    const QByteArray accountBytes = id.toUtf8();
    Cf<CFStringRef> account;
    account.value = CFStringCreateWithBytes(nullptr,
        reinterpret_cast<const UInt8*>(accountBytes.constData()), accountBytes.size(), kCFStringEncodingUTF8, false);
    const void* item = keychain;
    Cf<CFArrayRef> search;
    search.value = CFArrayCreate(nullptr, &item, 1, &kCFTypeArrayCallBacks);
    if (!query.value || !account.value || !search.value) return false;
    CFDictionarySetValue(query.value, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query.value, kSecAttrService, CFSTR("org.maxchat.Credentials"));
    CFDictionarySetValue(query.value, kSecAttrAccount, account.value);
    CFDictionarySetValue(query.value, kSecMatchSearchList, search.value);
    if (operation == QLatin1String("remove")) {
        const OSStatus result = SecItemDelete(query.value);
        return result == errSecSuccess || result == errSecItemNotFound;
    }
    if (operation == QLatin1String("read")) {
        CFDictionarySetValue(query.value, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query.value, kSecMatchLimit, kSecMatchLimitOne);
        Cf<CFTypeRef> data;
        if (SecItemCopyMatching(query.value, &data.value) != errSecSuccess || !data.value ||
            CFGetTypeID(data.value) != CFDataGetTypeID()) return false;
        const auto bytes = static_cast<CFDataRef>(data.value);
        if (CFDataGetLength(bytes) > MaximumBytes) return false;
        value = QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(bytes)), CFDataGetLength(bytes));
        return true;
    }
    Cf<CFDataRef> data;
    data.value = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(value.constData()), value.size());
    Cf<CFMutableDictionaryRef> attributes;
    attributes.value = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                 &kCFTypeDictionaryValueCallBacks);
    if (!data.value || !attributes.value) return false;
    CFDictionarySetValue(attributes.value, kSecValueData, data.value);
    const OSStatus update = SecItemUpdate(query.value, attributes.value);
    if (update != errSecItemNotFound) return update == errSecSuccess;
    CFDictionaryRemoveValue(query.value, kSecMatchSearchList);
    CFDictionarySetValue(query.value, kSecUseKeychain, keychain);
    CFDictionarySetValue(query.value, kSecValueData, data.value);
    CFDictionarySetValue(query.value, kSecAttrLabel, CFSTR("MaxChat credentials"));
    return SecItemAdd(query.value, nullptr) == errSecSuccess;
}
} // namespace maxchat::core
