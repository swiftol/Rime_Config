#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

// UTF-8 records are hex encoded, so tabs, newlines and surrogate-derived UTF-8
// cannot corrupt record boundaries. Only successful full translations enter it.
class TranslationDiskCache {
 public:
  explicit TranslationDiskCache(std::filesystem::path path) : path_(std::move(path)) {}
  template<class Fetch>
  std::string Resolve(const std::string& from, const std::string& to,
                      const std::string& source, Fetch fetch) {
    auto value = Get(from, to, source);
    if (!value.empty()) return value;
    value = fetch();
    if (!value.empty()) Put(from, to, source, value);
    return value;
  }
  std::string Get(const std::string& from, const std::string& to, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    Load();
    const auto found = entries_.find(Key(from, to, source));
    return found == entries_.end() ? std::string() : found->second;
  }
  bool Put(const std::string& from, const std::string& to, const std::string& source,
           const std::string& translation) {
    if (source.empty() || translation.empty() || source.size() > 4096 || translation.size() > 65536)
      return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Load();
    const auto key = Key(from, to, source);
    if (entries_.count(key)) return true;
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) return false;
    std::ofstream file(path_, std::ios::binary | std::ios::app);
    if (!file) return false;
    // A leading newline also isolates a truncated final record after a crash.
    file << '\n' << Encode(key) << '\t' << Encode(translation) << '\n';
    file.flush();
    if (!file.good()) return false;
    entries_[key] = translation;
    return true;
  }
 private:
  static std::string Key(const std::string& from, const std::string& to, const std::string& source) {
    return "niutrans-v2\n" + from + '\n' + to + '\n' + source;
  }
  static std::string Encode(const std::string& value) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (unsigned char ch : value) { result += digits[ch >> 4]; result += digits[ch & 15]; }
    return result;
  }
  static std::string Decode(const std::string& value) {
    if (value.size() % 2) return {};
    const auto digit = [](char ch) { return ch >= '0' && ch <= '9' ? ch - '0' :
        ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1; };
    std::string result;
    for (size_t i = 0; i < value.size(); i += 2) {
      const int a = digit(value[i]), b = digit(value[i + 1]);
      if (a < 0 || b < 0) return {};
      result += static_cast<char>(a * 16 + b);
    }
    return result;
  }
  void Load() {
    if (loaded_) return;
    loaded_ = true;
    std::ifstream file(path_, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
      if (line.size() > 150000) continue;
      const auto tab = line.find('\t');
      if (tab == std::string::npos) continue;
      const auto key = Decode(line.substr(0, tab)), text = Decode(line.substr(tab + 1));
      if (key.rfind("niutrans-v2\n", 0) == 0 && !text.empty()) entries_[key] = text;
    }
  }
  std::filesystem::path path_;
  std::mutex mutex_;
  bool loaded_ = false;
  std::map<std::string, std::string> entries_;
};
