#pragma once

#include <QString>

namespace maxchat::comic {

// Guess one of the 9 comic emotions (neutral, happy, sad, angry, laughing,
// coy, scared, shouting, bored) from a chat message body. Pure text heuristics
// over emoticons, emoji, keywords, and an all-caps shouting check.
[[nodiscard]] QString guessEmotion(const QString& body);

} // namespace maxchat::comic
