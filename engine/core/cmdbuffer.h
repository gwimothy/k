#pragma once
// The command buffer: text typed into the console, read from .cfg files, or produced by
// key bindings is queued here and executed once per frame.
//
// Syntax (same as Source):
//   statements are separated by newlines or ';'
//   "double quotes" group words and protect ';'
//   // starts a comment that runs to the end of the line

#include <string>
#include <string_view>
#include <vector>

namespace k::cmd {

// Split text into statements, honouring quotes and // comments. Empty statements are dropped.
std::vector<std::string> splitStatements(std::string_view text);

// Split one statement into tokens. Quotes are removed from quoted tokens.
std::vector<std::string> tokenize(std::string_view statement);

// Queue text at the end of the buffer.
void append(std::string_view text);
// Queue text at the front of the buffer (runs before anything already queued). Used by exec.
void insert(std::string_view text);

// Run queued statements until the buffer is empty or a `wait` is hit.
void execute();

// Parse and run text immediately, bypassing the queue.
void executeNow(std::string_view text);

// Drop everything queued.
void clear();
bool empty();

// Aliases (alias name "commands").
void setAlias(std::string_view name, std::string_view commands);
const std::string* findAlias(std::string_view name);
std::vector<std::pair<std::string, std::string>> aliases();

} // namespace k::cmd
