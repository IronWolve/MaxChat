#include "spell/Speller.h"

// Stub OS-speller factory for builds WITHOUT a native engine (the default, and
// every non-Windows/macOS build). The real factory is defined in the platform
// TU (e.g. WindowsSpeller.cpp) which is only compiled with MAXCHAT_OS_SPELL.
#ifndef MAXCHAT_OS_SPELL

namespace maxchat::spell {

std::unique_ptr<Speller> createOsSpeller(const QString & /*languageCode*/) {
  return nullptr;
}

} // namespace maxchat::spell

#endif // MAXCHAT_OS_SPELL
