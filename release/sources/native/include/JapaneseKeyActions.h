#pragma once
#include <string>
#include <cstdint>
inline std::string JapaneseKeyAction(const std::string& actions, int keycode);

struct JapaneseLongPressDefinition {
  unsigned milliseconds = 500;
  std::string output;
  bool compose = false;
};
inline JapaneseLongPressDefinition JapaneseLongPress(const std::string& definitions, int keycode) {
  JapaneseLongPressDefinition result;
  const auto entry = JapaneseKeyAction(definitions, keycode);
  const auto comma = entry.find(',');
  if (comma == std::string::npos) return result;
  try {
    size_t used = 0;
    const auto duration = std::stoul(entry.substr(0, comma), &used);
    if (used != comma || duration < 100 || duration > 3000) return result;
    const auto mode = entry.find(',',comma+1);
    if(mode!=std::string::npos && entry.substr(mode+1)!="input")return {};
    result.compose=mode!=std::string::npos;
    const auto hex = entry.substr(comma + 1,mode==std::string::npos?std::string::npos:mode-comma-1);
    if (hex.empty() || hex.size() % 2 || hex.size() > 1024) return result;
    auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'A' && c <= 'F') return c - 'A' + 10; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
    for (size_t i = 0; i < hex.size(); i += 2) {
      const int hi = digit(hex[i]), lo = digit(hex[i + 1]);
      if (hi < 0 || lo < 0 || hi * 16 + lo < 32 || hi * 16 + lo == 127) return {};
      result.output.push_back(static_cast<char>(hi * 16 + lo));
    }
    result.milliseconds = static_cast<unsigned>(duration);
    if(result.compose)for(unsigned char ch:result.output)if(ch<33 || ch>126)return {};
  } catch (...) { return {}; }
  return result;
}
template<class Commit,class Compose>
inline void JapaneseDispatchLongPress(const JapaneseLongPressDefinition& definition,Commit commit,Compose compose) {
  if(definition.compose)compose(definition.output);else commit(definition.output);
}
inline int JapanesePhysicalKey(int keycode) {
  if (keycode >= 'A' && keycode <= 'Z') return keycode + ('a' - 'A');
  const std::string shifted = "~!@#$%^&*()_+{}|:\"<>?", normal = "`1234567890-=[]\\;',./";
  const auto index = shifted.find(static_cast<char>(keycode));
  return keycode >= 33 && keycode <= 126 && index != std::string::npos ? normal[index] : keycode;
}
struct JapaneseLongPressPending {
  int keycode = 0, mask = 0;
  uint64_t started = 0;
  JapaneseLongPressDefinition definition;
  bool IsLong(uint64_t now) const { return keycode != 0 && now >= started && now - started >= definition.milliseconds; }
};

// The native row navigator and imported-table processor share explicit key
// actions. No action retains the established PageUp/PageDown and bracket keys.
inline std::string JapaneseKeyAction(const std::string& actions, int keycode) {
  const std::string marker = ";" + std::to_string(keycode) + "=";
  const auto position = actions.find(marker);
  if (position == std::string::npos) return {};
  const auto start = position + marker.size();
  const auto end = actions.find(';', start);
  return end == std::string::npos ? std::string() : actions.substr(start, end - start);
}
inline bool JapaneseNextRowKey(int keycode, const std::string& action) {
  return action == "next" || (action.empty() && (keycode == 0xff56 || keycode == ']'));
}
inline bool JapanesePreviousRowKey(int keycode, const std::string& action) {
  return action == "previous" || (action.empty() && (keycode == 0xff55 || keycode == '['));
}
