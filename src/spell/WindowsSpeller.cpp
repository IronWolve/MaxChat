#include "spell/Speller.h"

// Native Windows speller via the COM Spell Checking API (Win8+). Compiled ONLY
// when MAXCHAT_OS_SPELL is defined (build.bat osspell) on Windows, so it can
// never affect the default build. NOTE: written against the documented API but
// NOT compile-tested from the Linux dev box — verify on the Windows build.
#if defined(MAXCHAT_OS_SPELL) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <objbase.h>
#include <spellcheck.h>
#include <windows.h>

#include <string>

namespace maxchat::spell {
namespace {

std::wstring toWide(const QString &text) {
  return std::wstring(reinterpret_cast<const wchar_t *>(text.utf16()),
                      static_cast<size_t>(text.size()));
}

// "en" / "en_US" → BCP-47 "en"/"en-US" as Windows expects.
QString toBcp47(QString code) {
  code.replace(QLatin1Char('_'), QLatin1Char('-'));
  return code;
}

class WindowsSpeller final : public Speller {
public:
  explicit WindowsSpeller(const QString &languageCode) {
    // The GUI thread may already have COM initialized; either result is fine.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ISpellCheckerFactory *factory = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr,
                                CLSCTX_INPROC_SERVER,
                                __uuidof(ISpellCheckerFactory),
                                reinterpret_cast<void **>(&factory))) ||
        factory == nullptr) {
      return;
    }

    const QString bcp = toBcp47(languageCode);
    std::wstring lang = toWide(bcp);
    BOOL supported = FALSE;
    factory->IsSupported(lang.c_str(), &supported);
    if (!supported) {
      const int dash = bcp.indexOf(QLatin1Char('-'));
      if (dash > 0) {
        const std::wstring base = toWide(bcp.left(dash));
        if (SUCCEEDED(factory->IsSupported(base.c_str(), &supported)) &&
            supported) {
          lang = base;
        }
      }
    }
    if (supported) {
      factory->CreateSpellChecker(lang.c_str(), &checker_);
    }
    factory->Release();
  }

  ~WindowsSpeller() override {
    if (checker_ != nullptr) {
      checker_->Release();
    }
    // Intentionally not calling CoUninitialize — balancing per-thread COM init
    // across the app's lifetime is fragile and leaving it up is harmless here.
  }

  [[nodiscard]] bool isLoaded() const override { return checker_ != nullptr; }

  [[nodiscard]] bool isCorrect(const QString &word) const override {
    if (checker_ == nullptr || word.isEmpty()) {
      return true;
    }
    const std::wstring wide = toWide(word);
    IEnumSpellingError *errors = nullptr;
    if (FAILED(checker_->Check(wide.c_str(), &errors)) || errors == nullptr) {
      return true; // can't check → don't flag
    }
    ISpellingError *error = nullptr;
    const bool misspelled = (errors->Next(&error) == S_OK && error != nullptr);
    if (error != nullptr) {
      error->Release();
    }
    errors->Release();
    return !misspelled;
  }

  [[nodiscard]] QStringList suggestions(const QString &word,
                                        int maxSuggestions) const override {
    QStringList out;
    if (checker_ == nullptr || word.isEmpty()) {
      return out;
    }
    const std::wstring wide = toWide(word);
    IEnumString *strings = nullptr;
    if (FAILED(checker_->Suggest(wide.c_str(), &strings)) ||
        strings == nullptr) {
      return out;
    }
    LPOLESTR item = nullptr;
    ULONG fetched = 0;
    while (out.size() < maxSuggestions &&
           strings->Next(1, &item, &fetched) == S_OK && fetched == 1 &&
           item != nullptr) {
      out.append(QString::fromWCharArray(item));
      CoTaskMemFree(item);
      item = nullptr;
    }
    strings->Release();
    return out;
  }

private:
  ISpellChecker *checker_ = nullptr;
};

} // namespace

std::unique_ptr<Speller> createOsSpeller(const QString &languageCode) {
  auto speller = std::make_unique<WindowsSpeller>(languageCode);
  if (!speller->isLoaded()) {
    return nullptr; // language unsupported / API unavailable → caller falls back
  }
  return speller;
}

} // namespace maxchat::spell

#endif // MAXCHAT_OS_SPELL && _WIN32
