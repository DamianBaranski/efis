/// \file polish_speech.h
/// Polish place names rewritten so an English voice can pronounce them.
#ifndef POLISH_SPEECH_H
#define POLISH_SPEECH_H

#include <string>

/// True when the text is Polish spelling rather than Czech or plain English.
bool polishSpelling(const std::string &text);

/// Lowercase, then the PyTDM repolonise and anglicise passes.
/// Capitals are folded first so "Łódź" is transcribed, not only "łódź".
std::string anglicizePolish(const std::string &text);

#endif
