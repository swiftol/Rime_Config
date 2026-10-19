#pragma once
#include <boost/property_tree/json_parser.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <set>
struct CloudCandidateResult { std::string text; std::string reading; };
inline std::vector<CloudCandidateResult> ParseCloudCandidates(const std::string& response,
                                                           const std::string& spelling) {
  std::vector<CloudCandidateResult> result;
  try {
    boost::property_tree::ptree tree;
    std::istringstream stream(response);
    boost::property_tree::read_json(stream, tree);
    auto root = tree.begin();
    if (root == tree.end() || root->second.data() != "SUCCESS" || ++root == tree.end()) return {};
    if (root->second.empty()) return {};
    const auto& block = root->second.begin()->second;
    auto part = block.begin();
    if (part == block.end() || part->second.data() != spelling || ++part == block.end()) return {};
    const auto& words = part->second;
    std::vector<std::string> readings;
    for (const auto& field : block) {
      const auto annotation = field.second.get_child_optional("annotation");
      if (annotation) for (const auto& item : *annotation) readings.push_back(item.second.data());
    }
    std::set<std::string> seen;
    size_t index = 0;
    for (const auto& item : words) {
      const std::string text = item.second.data();
      std::string reading = index < readings.size() ? readings[index] : "";
      ++index;
      if (text.empty() || text.size() > 512 || text.find_first_of("\t\r\n") != std::string::npos ||
          !seen.insert(text).second) continue;
      if (reading.size() > 128 || reading.find_first_not_of("abcdefghijklmnopqrstuvwxyz ") != std::string::npos) reading.clear();
      result.push_back({text, reading});
      if (result.size() == 10) break;
    }
  } catch (...) { return {}; }
  return result;
}
