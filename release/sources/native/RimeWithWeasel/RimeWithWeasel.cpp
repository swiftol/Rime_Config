#include "stdafx.h"
#include <CandidateLanguageSplit.h>
#include <JapaneseKeyActions.h>
#include <EnterBehavior.h>
#include <JapaneseLearningCommit.h>
#include "../WeaselUI/CloudBadge.h"
#include <logging.h>
#include <RimeWithWeasel.h>
#include <StringAlgorithm.hpp>
#include <WeaselConstants.h>
#include <WeaselUtility.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <array>
#include <vector>
#include <regex>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <rime_api.h>
#include <TranslationDiskCache.h>
#include <TranslationAnnotationPolicy.h>
#include <CloudCandidateJson.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

#define TRANSPARENT_COLOR 0x00000000
#define ARGB2ABGR(value)                                 \
  ((value & 0xff000000) | ((value & 0x000000ff) << 16) | \
   (value & 0x0000ff00) | ((value & 0x00ff0000) >> 16))
#define RGBA2ABGR(value)                                   \
  (((value & 0xff) << 24) | ((value & 0xff000000) >> 24) | \
   ((value & 0x00ff0000) >> 8) | ((value & 0x0000ff00) << 8))
typedef enum { COLOR_ABGR = 0, COLOR_ARGB, COLOR_RGBA } ColorFormat;

using namespace weasel;

static RimeApi* rime_api;
struct CandidateRefreshSelection {
  std::string input;
  std::string text;
  size_t index = 0;
  bool valid = false;
  explicit CandidateRefreshSelection(RimeSessionId session) {
    RIME_STRUCT(RimeContext, ctx);
    if (!rime_api->get_context(session, &ctx)) return;
    const int highlighted = ctx.menu.highlighted_candidate_index;
    if (highlighted >= 0 && highlighted < ctx.menu.num_candidates) {
      index = static_cast<size_t>(ctx.menu.page_no) * ctx.menu.page_size + highlighted;
      const char* spelling = rime_api->get_input(session);
      input = spelling ? spelling : "";
      const char* candidate = ctx.menu.candidates[highlighted].text;
      text = candidate ? candidate : "";
      valid = index > 0 && !input.empty() && !text.empty();
    }
    rime_api->free_context(&ctx);
  }
  void Restore(RimeSessionId session) const {
    if (!valid || !RIME_API_AVAILABLE(rime_api, candidate_list_from_index) ||
        !RIME_API_AVAILABLE(rime_api, highlight_candidate)) return;
    const char* spelling = rime_api->get_input(session);
    if (!spelling || input != spelling) return;
    RimeCandidateListIterator iterator{};
    if (!rime_api->candidate_list_from_index(session, &iterator, 0)) return;
    size_t count = 0, target = index, distance = static_cast<size_t>(-1);
    // Preserve candidate identity if new cloud items were inserted ahead of it.
    // Annotation changes are not part of identity. Bound malformed/huge lists.
    const size_t limit = min(static_cast<size_t>(4096), max(static_cast<size_t>(512), index + 64));
    while (count < limit && rime_api->candidate_list_next(&iterator)) {
      if (iterator.candidate.text && text == iterator.candidate.text) {
        const size_t delta = count > index ? count - index : index - count;
        if (delta < distance) { target = count; distance = delta; }
      }
      ++count;
      // Iteration is ordered by index. Once past the old position by the
      // best known distance, no later duplicate can be a nearer identity.
      if (distance != static_cast<size_t>(-1) && count > index &&
          count - index > distance) break;
    }
    rime_api->candidate_list_end(&iterator);
    if (count) rime_api->highlight_candidate(session, min(target, count - 1));
  }
};
static int GridWidthUnits(const std::wstring& text) {
  // Six base slots per row.  Keep one slot conservative: after the row number,
  // spacing and glyph overhang, three full-width characters are guaranteed to
  // fit.  Four to six use two slots; seven or more use three.
  int width_units = 0;
  for (wchar_t ch : text)
    width_units += (ch >= 0x2e80 || ch > 0xff) ? 2 : 1;
  return width_units;
}
static int CandidateGridSpan(const std::wstring& text,
                             const std::wstring& raw_comment,
                             bool include_comment) {
  int width_units = GridWidthUnits(text);
  if (!include_comment)
    return max(1, min(6, (width_units + 9) / 10));
  const std::wstring marker = L"[[RIME_JR:";
  const size_t begin = raw_comment.find(marker);
  if (begin != std::wstring::npos) {
    const size_t end = raw_comment.find(L"]]", begin + marker.length());
    if (end != std::wstring::npos) {
      const std::wstring reading = raw_comment.substr(
          begin + marker.length(), end - begin - marker.length());
      width_units = max(width_units,
                        GridWidthUnits(L"[" + reading + L"]"));
    }
  }
  const std::wstring fuzzy_marker = L"[JF_READING]";
  if (raw_comment.rfind(fuzzy_marker, 0) == 0) {
    std::wstring reading = raw_comment.substr(fuzzy_marker.length());
    const size_t hidden = reading.find(L"[[RIME_");
    if (hidden != std::wstring::npos) reading.erase(hidden);
    width_units = max(width_units,
                      GridWidthUnits(L"[" + reading + L"]"));
  } else {
    std::wstring visible = raw_comment;
    size_t marker_begin = 0;
    while ((marker_begin = visible.find(L"[[RIME_")) !=
           std::wstring::npos) {
      const size_t marker_end = visible.find(L"]]", marker_begin);
      visible.erase(marker_begin,
                    marker_end == std::wstring::npos
                        ? std::wstring::npos
                        : marker_end + 2 - marker_begin);
    }
    for (wchar_t& ch : visible)
      if (ch == static_cast<wchar_t>(0x1e)) ch = L'\n';
    size_t line_begin = 0;
    while (line_begin <= visible.size()) {
      const size_t line_end = visible.find(L'\n', line_begin);
      std::wstring line = visible.substr(
          line_begin, line_end == std::wstring::npos
                          ? std::wstring::npos
                          : line_end - line_begin);
      if (line.rfind(L"EN:", 0) == 0 || line.rfind(L"JA:", 0) == 0)
        line.erase(0, 3);
      width_units = max(width_units, GridWidthUnits(line));
      if (line_end == std::wstring::npos) break;
      line_begin = line_end + 1;
    }
  }
  return max(1, min(6, (width_units + 9) / 10));
}
static std::vector<std::vector<int>> BuildVisualCandidateRows(
    const RimeContext& ctx, bool include_comment) {
  std::vector<std::vector<int>> rows(1);
  int used_slots = 0;
  for (int i = 0; i < ctx.menu.num_candidates; ++i) {
    const std::wstring comment =
        ctx.menu.candidates[i].comment
            ? u8tow(ctx.menu.candidates[i].comment)
            : L"";
    const int span = CandidateGridSpan(
        u8tow(ctx.menu.candidates[i].text), comment, include_comment);
    if (used_slots > 0 && used_slots + span > 6) {
      rows.emplace_back();
      used_slots = 0;
    }
    rows.back().push_back(i);
    used_slots += span;
    if (used_slots == 6 && i + 1 < ctx.menu.num_candidates) {
      rows.emplace_back();
      used_slots = 0;
    }
  }
  if (!rows.empty() && rows.back().empty()) rows.pop_back();
  return rows;
}

static std::vector<std::vector<int>> BuildVisualCandidateRows(
    const CandidateInfo& candidates, bool include_comment, UI* ui=nullptr,const UIStyle* sessionStyle=nullptr) {
  const auto split=ReadCandidateSplitOptions();
  if(split.enabled) {
    if(ui){UIStyle style=sessionStyle?*sessionStyle:ui->style();style.layout_type=UIStyle::LAYOUT_HORIZONTAL;style.max_height=-max(1,abs(style.max_height));const auto measured=ui->MeasureCandidateRows(candidates,&style);if(CandidateRowPermutation(measured,int(candidates.candies.size())))return measured;}
    std::vector<bool> japanese;
    for(size_t i=0;i<candidates.candies.size();++i)
      japanese.push_back(SplitCandidateJapanese(candidates.candies[i].str,
          i<candidates.comments.size()?candidates.comments[i].str:L""));
    return BuildCandidateSplitPlan(japanese,split,candidates.currentPage==0).rows;
  }
  std::vector<std::vector<int>> rows(1);
  int used_slots = 0;
  for (size_t i = 0; i < candidates.candies.size(); ++i) {
    const std::wstring comment = i < candidates.comments.size() ?
        candidates.comments[i].str : L"";
    const int span = CandidateGridSpan(candidates.candies[i].str, comment,
                                       include_comment);
    if (used_slots > 0 && used_slots + span > 6) {
      rows.emplace_back();
      used_slots = 0;
    }
    rows.back().push_back(static_cast<int>(i));
    used_slots += span;
    if (used_slots == 6 && i + 1 < candidates.candies.size()) {
      rows.emplace_back();
      used_slots = 0;
    }
  }
  if (!rows.empty() && rows.back().empty()) rows.pop_back();
  return rows;
}

static bool CandidateRowsMatchCount(
    const std::vector<std::vector<int>>& rows, int candidate_count) {
  return CandidateRowPermutation(rows,candidate_count);
}

static void CandidateExpandTrace(const std::string& message) {
  // Keep only low-frequency navigation diagnostics.  Full context/UI tracing
  // on every keystroke is intentionally disabled because it hurts latency.
  if (message.rfind("ROW_", 0) != 0 &&
      message.rfind("NEXT_RIME_PAGE", 0) != 0 &&
      message.rfind("PREVIOUS_RIME_PAGE", 0) != 0 &&
      message.rfind("NEXT_EXPANDED_ROW", 0) != 0 &&
      message.rfind("PREVIOUS_EXPANDED_ROW", 0) != 0 &&
      message.rfind("ChangePage", 0) != 0)
    return;
  wchar_t temp_path[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temp_path))
    return;
  std::filesystem::path log_path(temp_path);
  log_path /= L"weasel_candidate_expand.log";
  HANDLE file = CreateFileW(log_path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;
  const std::string line = std::to_string(GetTickCount64()) +
                           " [SERVER pid=" +
                           std::to_string(GetCurrentProcessId()) + "] " +
                           message + "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
  CloseHandle(file);
}

static std::string HexBytes(const std::string& value) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(value.size() * 3);
  for (const unsigned char ch : value) {
    if (!result.empty()) result.push_back(' ');
    result.push_back(digits[ch >> 4]);
    result.push_back(digits[ch & 0x0f]);
  }
  return result;
}
WeaselSessionId _GenerateNewWeaselSessionId(SessionStatusMap sm, DWORD pid) {
  if (sm.empty())
    return (WeaselSessionId)(pid + 1);
  return (WeaselSessionId)(sm.rbegin()->first + 1);
}

int expand_ibus_modifier(int m) {
  return (m & 0xff) | ((m & 0xff00) << 16);
}

static std::vector<int> CloudCandidateIndices(const RimeContext& context,
                                             bool include_dictionary = false,
                                             int visible_limit = 12) {
  std::vector<int> indices;
  // Compact mode paints only the head of the page. Translate a small head
  // batch; expanded mode opts into its larger visible window. Cached comments
  // can still be read for every candidate without scheduling network work.
  for (int i = 0; i < (std::min)(context.menu.num_candidates, visible_limit); ++i) {
    const char* comment = context.menu.candidates[i].comment;
    const bool cloud = comment && std::strncmp(comment, "\xE2\x98\x81", 3) == 0;
    const bool dictionary = include_dictionary && comment &&
        (std::strstr(comment, "[[RIME_LANG:ZH]]") ||
         std::strstr(comment, "[[RIME_LANG:JA]]")) && context.menu.candidates[i].text &&
        !u8tow(context.menu.candidates[i].text).empty();
    if (cloud || dictionary)
      indices.push_back(i);
  }
  // Queue uses push_front: visit lower-ranked items first so the head wins.
  std::reverse(indices.begin(), indices.end());
  return indices;
}

static bool PreferNiuAnnotations() {
  DWORD value = 0, size = sizeof(value);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
               L"PreferNiuAnnotations", RRF_RT_REG_DWORD, nullptr, &value, &size);
  return value != 0;
}

static bool ShowSingleCharacterAnnotations() {
  DWORD value = 1, size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                  L"ShowSingleCharacterAnnotations", RRF_RT_REG_DWORD,
                  nullptr, &value, &size) == ERROR_SUCCESS)
    return value != 0;
  RimeConfig config = {NULL};
  Bool persisted = True;
  if (rime_api->user_config_open("user", &config)) {
    rime_api->config_get_bool(&config,
        "var/option/show_single_character_annotation", &persisted);
    rime_api->config_close(&config);
  }
  return persisted;
}

static bool HideJapaneseTranslationAnnotations() {
  DWORD value = 0, size = sizeof(value);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
               L"HideJapaneseTranslationAnnotations", RRF_RT_REG_DWORD,
               nullptr, &value, &size);
  return value != 0;
}

static bool CloudTranslationOption(RimeSessionId session_id,
                                   const char* option,
                                   const wchar_t* registry_name) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                   registry_name, RRF_RT_REG_DWORD, nullptr, &value,
                   &size) == ERROR_SUCCESS)
    return value != 0;
  return rime_api->get_option(session_id, option);
}

RimeWithWeaselHandler::RimeWithWeaselHandler(UI* ui)
    : m_ui(ui),
      m_active_session(0),
      m_disabled(true),
      m_current_dark_mode(false),
      m_global_ascii_mode(false),
      m_show_notifications_time(1200),
      _UpdateUICallback(NULL) {
  m_ui->InServer() = true;
  rime_api = rime_get_api();
  assert(rime_api);
  m_pid = GetCurrentProcessId();
  uint16_t msbit = 0;
  for (auto i = 31; i >= 0; i--) {
    if (m_pid & (1 << i)) {
      msbit = i;
      break;
    }
  }
  m_pid = (m_pid << (31 - msbit));
  _Setup();
  m_sentence_translation_thread =
      std::thread(&RimeWithWeaselHandler::_SentenceTranslationWorker, this);
  m_cloud_translation_thread =
      std::thread(&RimeWithWeaselHandler::_CloudTranslationWorker, this);
  m_cloud_candidate_thread =
      std::thread(&RimeWithWeaselHandler::_CloudCandidateWorker, this);
}

RimeWithWeaselHandler::~RimeWithWeaselHandler() {
  {
    std::lock_guard<std::mutex> lock(m_sentence_translation_mutex);
    m_sentence_translation_stopping = true;
  }
  m_sentence_translation_cv.notify_all();
  if (m_sentence_translation_thread.joinable())
    m_sentence_translation_thread.join();
  {
    std::lock_guard<std::mutex> lock(m_cloud_translation_mutex);
    m_cloud_translation_stopping = true;
  }
  m_cloud_translation_cv.notify_all();
  if (m_cloud_translation_thread.joinable())
    m_cloud_translation_thread.join();
  {
    std::lock_guard<std::mutex> lock(m_cloud_candidate_mutex);
    m_cloud_candidate_stopping = true;
  }
  m_cloud_candidate_cv.notify_all();
  if (m_cloud_candidate_thread.joinable())
    m_cloud_candidate_thread.join();
  m_show_notifications.clear();
  m_session_status_map.clear();
  m_app_options.clear();
}

bool add_session = false;
void _UpdateUIStyle(RimeConfig* config, UI* ui, bool initialize);
bool _UpdateUIStyleColor(RimeConfig* config,
                         UIStyle& style,
                         const std::string& color = std::string());
void _LoadAppOptions(RimeConfig* config, AppOptionsByAppName& app_options);

bool _ApplyHotAnnotationOptions(RimeSessionId session_id) {
  bool changed = false;
  struct HotOption {
    const wchar_t* registry_name;
    const char* rime_name;
  };
  constexpr HotOption options[] = {
      {L"ShowEnglishAnnotation", "show_english_annotation"},
      {L"ShowJapaneseAnnotation", "show_japanese_annotation"},
      {L"ShowDirectJapaneseReading", "show_direct_japanese_reading"},
      {L"ShowSingleCharacterAnnotations", "show_single_character_annotation"},
      {L"HideJapaneseTranslationAnnotations", "hide_japanese_translation_annotations"},
  };
  for (const auto& option : options) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                     option.registry_name, RRF_RT_REG_DWORD, nullptr, &value,
                     &size) == ERROR_SUCCESS &&
        rime_api->get_option(session_id, option.rime_name) != Bool(value != 0)) {
      rime_api->set_option(session_id, option.rime_name, Bool(value != 0));
      changed = true;
    }
  }
  return changed;
}

void _ApplyJapaneseInputTableState(RimeSessionId session_id) {
  std::ifstream state(WeaselUserDataPath() / L"japanese_input_table.state", std::ios::binary);
  std::string enabled, revision, mixed, letters;
  if (!std::getline(state, enabled) || !std::getline(state, revision) ||
      (enabled != "0" && enabled != "1") || revision.size() != 32)
    return;
  std::getline(state, mixed);
  std::getline(state, letters);
  const std::string token = "file:" + revision;
  char old_value[64] = {};
  const bool same_revision = rime_api->get_property(session_id,
      "japanese_input_table_generation", old_value, sizeof(old_value)) && token == old_value;
  const Bool desired = enabled == "1";
  const Bool desired_mixed = mixed == "1";
  const Bool desired_letters = letters == "1";
  if (same_revision && rime_api->get_option(session_id, "japanese_input_table_enabled") == desired &&
      rime_api->get_option(session_id, "japanese_input_table_mixed") == desired_mixed &&
      rime_api->get_option(session_id, "japanese_input_table_show_letters") == desired_letters)
    return;
  rime_api->set_property(session_id, "japanese_input_table_generation", token.c_str());
  std::ifstream table(WeaselUserDataPath() / L"japanese_input_table.tsv", std::ios::binary);
  std::string line, key_actions = ";", long_presses = ";";
  const std::string prefix = "# zhongri-key-action: ";
  const std::string hold_prefix = "# zhongri-key-long-press: ";
  while (std::getline(table, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, hold_prefix.size(), hold_prefix) == 0) {
      const auto entry = line.substr(hold_prefix.size());
      if (entry.find(';') == std::string::npos && long_presses.size() + entry.size() < 65000) long_presses += entry + ";";
    }
    if (line.compare(0, prefix.size(), prefix) != 0) continue;
    const std::string entry = line.substr(prefix.size());
    if (entry.find(';') == std::string::npos && key_actions.size() + entry.size() < 4090)
      key_actions += entry + ";";
  }
  rime_api->set_property(session_id, "japanese_input_table_key_actions", key_actions.c_str());
  rime_api->set_property(session_id, "japanese_input_table_long_presses", long_presses.c_str());
  rime_api->set_option(session_id, "japanese_input_table_mixed", desired_mixed);
  rime_api->set_option(session_id, "japanese_input_table_show_letters", desired_letters);
  rime_api->set_option(session_id, "japanese_input_table_enabled", desired);
  const Bool old = rime_api->get_option(session_id, "google_cloud_refresh");
  rime_api->set_option(session_id, "google_cloud_refresh", !old);
}

void _ApplyHotInputOptions(RimeSessionId session_id, DWORD& seen_generation,
                           DWORD& seen_phrase_generation,
                           DWORD& seen_fuzzy_generation) {
  constexpr wchar_t path[] = L"Software\\ZhongriInputMethod";
  _ApplyJapaneseInputTableState(session_id);
  DWORD generation = 0;
  DWORD size = sizeof(generation);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"InputSettingsGeneration",
                   RRF_RT_REG_DWORD, nullptr, &generation, &size) != ERROR_SUCCESS ||
      generation == seen_generation)
    return;
  struct HotOption { const wchar_t* registry_name; const char* rime_name; };
  constexpr HotOption options[] = {
      {L"Option_japanese_prefix_completion_disabled", "japanese_prefix_completion_disabled"},
      {L"Option_japanese_prefix_completion_suffix", "japanese_prefix_completion_suffix"},
      {L"Option_japanese_prefix_completion_japanese_first", "japanese_prefix_completion_japanese_first"},
      {L"Option_japanese_continuation_lock", "japanese_continuation_lock"},
      {L"Option_emoji_second_position", "emoji_second_position"},
      {L"Option_japanese_fuzzy_match", "japanese_fuzzy_match"},
      {L"Option_japanese_learning_mode", "japanese_learning_mode"},
      {L"Option_japanese_learning_kanji", "japanese_learning_kanji"},
      {L"Option_japanese_learning_space", "japanese_learning_space"},
      {L"Option_japanese_learning_newline", "japanese_learning_newline"},
      {L"Option_japanese_fuzzy_sokuon", "japanese_fuzzy_sokuon"},
      {L"Option_japanese_fuzzy_long_i", "japanese_fuzzy_long_i"},
      {L"Option_japanese_fuzzy_long_u", "japanese_fuzzy_long_u"},
      {L"Option_japanese_fuzzy_long_mark", "japanese_fuzzy_long_mark"},
      {L"Option_japanese_fuzzy_chi_ji", "japanese_fuzzy_chi_ji"},
      {L"Option_japanese_fuzzy_hu_fu", "japanese_fuzzy_hu_fu"},
      {L"Option_japanese_fuzzy_shu_sho", "japanese_fuzzy_shu_sho"},
      {L"Option_japanese_fuzzy_ke_kai", "japanese_fuzzy_ke_kai"},
      {L"Option_japanese_fuzzy_ke_kae_gae", "japanese_fuzzy_ke_kae_gae"},
      {L"Option_japanese_fuzzy_sei_sai", "japanese_fuzzy_sei_sai"},
      {L"Option_japanese_fuzzy_dakuten", "japanese_fuzzy_dakuten"},
      {L"Option_japanese_particle_wa_ha", "japanese_particle_wa_ha"},
      {L"Option_sentence_translation", "sentence_translation"},
      {L"Option_google_cloud_candidates", "google_cloud_candidates"},
      {L"Option_japanese_input_table_enabled", "japanese_input_table_enabled"},
  };
  for (const auto& option : options) {
    DWORD value = 0;
    size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, path, option.registry_name,
                     RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
      rime_api->set_option(session_id, option.rime_name, Bool(value != 0));
  }
  DWORD space_mode = 0;
  DWORD cloud_language = 0;
  size = sizeof(cloud_language);
  RegGetValueW(HKEY_CURRENT_USER, path, L"CloudLanguageMode", RRF_RT_REG_DWORD,
               nullptr, &cloud_language, &size);
  const char* cloud_language_text = cloud_language == 1 ? "zh" : cloud_language == 2 ? "ja" :
                                    cloud_language == 3 ? "both" : "auto";
  char old_cloud_language[16] = {};
  if (!rime_api->get_property(session_id, "cloud_language_mode", old_cloud_language, sizeof(old_cloud_language)) ||
      std::strcmp(old_cloud_language, cloud_language_text) != 0) {
    rime_api->set_property(session_id, "cloud_language_mode", cloud_language_text);
    const Bool old = rime_api->get_option(session_id, "google_cloud_refresh");
    rime_api->set_option(session_id, "google_cloud_refresh", !old);
  }
  DWORD cloud_count = 2;
  size = sizeof(cloud_count);
  RegGetValueW(HKEY_CURRENT_USER, path, L"CloudCandidateCount", RRF_RT_REG_DWORD,
               nullptr, &cloud_count, &size);
  const std::string cloud_count_text = std::to_string(max(1, min(10, static_cast<int>(cloud_count))));
  char old_cloud_count[16] = {};
  if (!rime_api->get_property(session_id, "cloud_candidate_count", old_cloud_count, sizeof(old_cloud_count)) ||
      cloud_count_text != old_cloud_count) {
    rime_api->set_property(session_id, "cloud_candidate_count", cloud_count_text.c_str());
    const Bool old = rime_api->get_option(session_id, "google_cloud_refresh");
    rime_api->set_option(session_id, "google_cloud_refresh", !old);
  }
  size = sizeof(space_mode);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"SpaceHotMode",
                   RRF_RT_REG_DWORD, nullptr, &space_mode, &size) == ERROR_SUCCESS) {
    const char* mode = space_mode == 2 ? "preview" :
                       space_mode == 1 ? "first" : "raw";
    rime_api->set_property(session_id, "space_hot_mode", mode);
  }
  DWORD rare_threshold = 0;
  size = sizeof(rare_threshold);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"RareSingleCharThreshold",
                   RRF_RT_REG_DWORD, nullptr, &rare_threshold, &size) == ERROR_SUCCESS) {
    const std::string value = std::to_string(rare_threshold);
    rime_api->set_property(session_id, "rare_single_char_threshold", value.c_str());
  }
  DWORD phrase_generation = 0;
  size = sizeof(phrase_generation);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"PhraseSettingsGeneration",
                   RRF_RT_REG_DWORD, nullptr, &phrase_generation, &size) == ERROR_SUCCESS &&
      phrase_generation != seen_phrase_generation) {
    seen_phrase_generation = phrase_generation;
    // Common-phrase Lua components read custom_phrase.txt on demand. Requery
    // an active composition so edited phrases appear without new window/session.
    const Bool old = rime_api->get_option(session_id, "google_cloud_refresh");
    rime_api->set_option(session_id, "google_cloud_refresh", !old);
  }
  DWORD fuzzy_generation = 0;
  size = sizeof(fuzzy_generation);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"JapaneseFuzzyRulesGeneration",
                   RRF_RT_REG_DWORD, nullptr, &fuzzy_generation, &size) == ERROR_SUCCESS &&
      fuzzy_generation != seen_fuzzy_generation) {
    seen_fuzzy_generation = fuzzy_generation;
    const std::string value = std::to_string(fuzzy_generation);
    rime_api->set_property(session_id, "japanese_fuzzy_rules_generation", value.c_str());
    const Bool old = rime_api->get_option(session_id, "google_cloud_refresh");
    rime_api->set_option(session_id, "google_cloud_refresh", !old);
  }
  DWORD table_generation = 0;
  size = sizeof(table_generation);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"JapaneseInputTableGeneration",
                   RRF_RT_REG_DWORD, nullptr, &table_generation, &size) == ERROR_SUCCESS) {
    const std::string value = std::to_string(table_generation);
    char old_value[32] = {};
    const bool has_old = rime_api->get_property(session_id,
        "japanese_input_table_generation", old_value, sizeof(old_value));
    if (!has_old || value != old_value) {
      rime_api->set_property(session_id, "japanese_input_table_generation", value.c_str());
      const Bool old = rime_api->get_option(session_id, "google_cloud_refresh");
      rime_api->set_option(session_id, "google_cloud_refresh", !old);
    }
  }
  rime_api->set_property(session_id,"chinese_correction_generation",std::to_string(generation).c_str());
  const Bool refresh = rime_api->get_option(session_id,"google_cloud_refresh");
  rime_api->set_option(session_id,"google_cloud_refresh",!refresh);
  seen_generation = generation;
  // Registry remains the wake-up mechanism, not an overriding second state.
  _ApplyJapaneseInputTableState(session_id);
}

void _ApplyHotAppearance(SessionStatus& session, bool force = false) {
  constexpr wchar_t registry_path[] = L"Software\\ZhongriInputMethod";
  DWORD generation = 0;
  DWORD size = sizeof(generation);
  if (RegGetValueW(HKEY_CURRENT_USER, registry_path,
                   L"AppearanceSettingsGeneration", RRF_RT_REG_DWORD,
                   nullptr, &generation, &size) != ERROR_SUCCESS ||
      (!force && generation == session.hot_appearance_generation))
    return;

  const auto read = [&](const wchar_t* name, int& target) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, registry_path, name,
                     RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS)
      target = static_cast<int>(value);
  };
  auto& style = session.style;
  const auto split=ReadCandidateSplitOptions();
  style.candidate_language_split=split.enabled?1:0;
  style.candidate_split_chinese_columns=split.left;
  style.candidate_split_japanese_columns=split.right;
  read(L"CandidateWidth", style.min_width);
  read(L"CandidateMinHeight",style.min_height);
  read(L"CommentFontSize", style.comment_font_point);
  read(L"CandidateFontSize",style.font_point);
  read(L"HighlightTextColor",style.hilited_candidate_text_color);
  read(L"HighlightBackgroundColor",style.hilited_candidate_back_color);
  read(L"CommentTextColor",style.comment_text_color);
  read(L"PanelBackgroundColor",style.back_color);
  read(L"CandidateSpacing", style.candidate_spacing);
  read(L"HighlightPadding", style.hilite_padding_x);
  style.hilite_padding_y = style.hilite_padding_x;
  style.candidate_spacing = max(style.candidate_spacing,
                                2 * style.hilite_padding_x);
  read(L"ChineseTextColor", style.chinese_candidate_text_color);
  read(L"ChineseBackgroundColor", style.chinese_candidate_back_color);
  read(L"JapaneseTextColor", style.japanese_candidate_text_color);
  read(L"JapaneseBackgroundColor", style.japanese_candidate_back_color);
  read(L"CommonPhraseTextColor", style.common_phrase_candidate_text_color);
  read(L"CommonPhraseBackgroundColor", style.common_phrase_candidate_back_color);
  const auto read_bool = [&](const wchar_t* name, bool& target) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, registry_path, name,
                     RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS)
      target = value != 0;
  };
  read_bool(L"InlinePreedit", style.inline_preedit);
  read_bool(L"InlinePreeditRawInput", style.inline_preedit_raw_input);
  read_bool(L"ExpandedCommentWidth", style.expanded_comment_width);
  read_bool(L"ExpandedCommentAlignLabel", style.expanded_comment_align_label);
  read_bool(L"HoverCommentScroll", style.hover_comment_scroll);
  session.hot_appearance_generation = generation;
  session.__synced = false;
}

void _RefreshTrayIcon(const RimeSessionId session_id,
                      const std::function<void()> _UpdateUICallback) {
  // Dangerous, don't touch
  static char app_name[256] = {0};
  auto ret = rime_api->get_property(session_id, "client_app", app_name,
                                    sizeof(app_name) - 1);
  if (!ret || u8tow(app_name) == std::wstring(L"explorer.exe"))
    boost::thread th([=]() {
      ::Sleep(100);
      if (_UpdateUICallback)
        _UpdateUICallback();
    });
  else if (_UpdateUICallback)
    _UpdateUICallback();
}

void RimeWithWeaselHandler::_Setup() {
  RIME_STRUCT(RimeTraits, weasel_traits);
  std::string shared_dir = wtou8(WeaselSharedDataPath().wstring());
  std::string user_dir = wtou8(WeaselUserDataPath().wstring());
  weasel_traits.shared_data_dir = shared_dir.c_str();
  weasel_traits.user_data_dir = user_dir.c_str();
  weasel_traits.prebuilt_data_dir = weasel_traits.shared_data_dir;
  std::string distribution_name = wtou8(get_weasel_ime_name());
  weasel_traits.distribution_name = distribution_name.c_str();
  weasel_traits.distribution_code_name = WEASEL_CODE_NAME;
  weasel_traits.distribution_version = WEASEL_VERSION;
  weasel_traits.app_name = "rime.zhongri";
  std::string log_dir = WeaselLogPath().u8string();
  weasel_traits.log_dir = log_dir.c_str();
  rime_api->setup(&weasel_traits);
  rime_api->set_notification_handler(&RimeWithWeaselHandler::OnNotify, this);
}

void RimeWithWeaselHandler::Initialize() {
  m_disabled = _IsDeployerRunning();
  if (m_disabled) {
    return;
  }

  LOG(INFO) << "Initializing la rime.";
  rime_api->initialize(NULL);
  if (rime_api->start_maintenance(/*full_check = */ False)) {
    m_disabled = true;
    rime_api->join_maintenance_thread();
  }

  RimeConfig config = {NULL};
  if (rime_api->config_open("weasel", &config)) {
    if (m_ui) {
      _UpdateUIStyle(&config, m_ui, true);
      _UpdateShowNotifications(&config, true);
      m_current_dark_mode = IsUserDarkMode();
      if (m_current_dark_mode) {
        const int BUF_SIZE = 255;
        char buffer[BUF_SIZE + 1] = {0};
        if (rime_api->config_get_string(&config, "style/color_scheme_dark",
                                        buffer, BUF_SIZE)) {
          std::string color_name(buffer);
          _UpdateUIStyleColor(&config, m_ui->style(), color_name);
        }
      }
      m_base_style = m_ui->style();
    }
    Bool global_ascii = false;
    if (rime_api->config_get_bool(&config, "global_ascii", &global_ascii))
      m_global_ascii_mode = !!global_ascii;
    if (!rime_api->config_get_int(&config, "show_notifications_time",
                                  &m_show_notifications_time))
      m_show_notifications_time = 1200;
    _LoadAppOptions(&config, m_app_options);
    rime_api->config_close(&config);
  }
  m_last_schema_id.clear();
}

void RimeWithWeaselHandler::Finalize() {
  m_active_session = 0;
  m_disabled = true;
  m_session_status_map.clear();
  LOG(INFO) << "Finalizing la rime.";
  rime_api->finalize();
}

DWORD RimeWithWeaselHandler::FindSession(WeaselSessionId ipc_id) {
  if (m_disabled)
    return 0;
  Bool found = rime_api->find_session(to_session_id(ipc_id));
  DLOG(INFO) << "Find session: session_id = " << to_session_id(ipc_id)
             << ", found = " << found;
  return found ? (ipc_id) : 0;
}

DWORD RimeWithWeaselHandler::AddSession(LPWSTR buffer, EatLine eat) {
  if (m_disabled) {
    DLOG(INFO) << "Trying to resume service.";
    EndMaintenance();
    if (m_disabled)
      return 0;
  }
  RimeSessionId session_id = (RimeSessionId)rime_api->create_session();
  // Restore settings-panel options for every newly created input session.
  // Arbitrary schema switches are not restored from user.yaml by librime.
  RimeConfig user_config = {NULL};
  if (rime_api->user_config_open("user", &user_config)) {
    const char* persisted_options[] = {
        "show_english_annotation", "show_japanese_annotation",
        "show_direct_japanese_reading", "show_single_character_annotation",
        "japanese_fuzzy_match", "japanese_fuzzy_sokuon",
        "japanese_fuzzy_long_i", "japanese_fuzzy_long_u",
        "japanese_fuzzy_long_mark", "japanese_fuzzy_chi_ji",
        "japanese_fuzzy_hu_fu", "japanese_fuzzy_shu_sho",
        "japanese_fuzzy_dakuten",
        "sentence_translation"};
    for (const char* option : persisted_options) {
      Bool value = False;
      const std::string path = std::string("var/option/") + option;
      if (rime_api->config_get_bool(&user_config, path.c_str(), &value))
        rime_api->set_option(session_id, option, value);
    }
    rime_api->config_close(&user_config);
  }
  _ApplyHotAnnotationOptions(session_id);
  if (m_global_ascii_mode) {
    for (const auto& pair : m_session_status_map) {
      if (pair.first) {
        rime_api->set_option(session_id, "ascii_mode",
                             !!pair.second.status.is_ascii_mode);
        break;
      }
    }
  }

  WeaselSessionId ipc_id =
      _GenerateNewWeaselSessionId(m_session_status_map, m_pid);
  DLOG(INFO) << "Add session: created session_id = " << session_id
             << ", ipc_id = " << ipc_id;
  SessionStatus& session_status = new_session_status(ipc_id);
  session_status.style = m_base_style;
  session_status.session_id = session_id;
  _ApplyHotInputOptions(session_id, session_status.hot_input_generation,
                        session_status.hot_phrase_generation,
                        session_status.hot_fuzzy_generation);
  _ReadClientInfo(ipc_id, buffer);

  RIME_STRUCT(RimeStatus, status);
  if (rime_api->get_status(session_id, &status)) {
    std::string schema_id = status.schema_id;
    m_last_schema_id = schema_id;
    _LoadSchemaSpecificSettings(ipc_id, schema_id);
    _LoadAppInlinePreeditSet(ipc_id, true);
    _UpdateInlinePreeditStatus(ipc_id);
    _RefreshTrayIcon(session_id, _UpdateUICallback);
    session_status.status = status;
    session_status.__synced = false;
    rime_api->free_status(&status);
  }
  const bool previous_inline_preedit = session_status.style.inline_preedit;
  _ApplyHotAppearance(session_status);
  if (previous_inline_preedit != session_status.style.inline_preedit)
    _UpdateInlinePreeditStatus(ipc_id);
  m_ui->style() = session_status.style;
  // show session's welcome message :-) if any
  if (eat) {
    _Respond(ipc_id, eat);
  }
  add_session = true;
  _UpdateUI(ipc_id);
  add_session = false;
  m_active_session = ipc_id;
  return ipc_id;
}

DWORD RimeWithWeaselHandler::RemoveSession(WeaselSessionId ipc_id) {
  if (m_ui && m_active_session == ipc_id)
    m_ui->Hide();
  if (m_disabled)
    return 0;
  DLOG(INFO) << "Remove session: session_id = " << to_session_id(ipc_id);
  // TODO: force committing? otherwise current composition would be lost
  rime_api->destroy_session(to_session_id(ipc_id));
  m_session_status_map.erase(ipc_id);
  if (m_active_session == ipc_id) m_active_session = 0;
  return 0;
}

void RimeWithWeaselHandler::UpdateColorTheme(BOOL darkMode) {
  RimeConfig config = {NULL};
  if (rime_api->config_open("weasel", &config)) {
    if (m_ui) {
      _UpdateUIStyle(&config, m_ui, true);
      m_current_dark_mode = darkMode;
      if (darkMode) {
        const int BUF_SIZE = 255;
        char buffer[BUF_SIZE + 1] = {0};
        if (rime_api->config_get_string(&config, "style/color_scheme_dark",
                                        buffer, BUF_SIZE)) {
          std::string color_name(buffer);
          _UpdateUIStyleColor(&config, m_ui->style(), color_name);
        }
      }
      m_base_style = m_ui->style();
    }
    rime_api->config_close(&config);
  }

  for (auto& pair : m_session_status_map) {
    RIME_STRUCT(RimeStatus, status);
    if (rime_api->get_status(to_session_id(pair.first), &status)) {
      _LoadSchemaSpecificSettings(pair.first, std::string(status.schema_id));
      _LoadAppInlinePreeditSet(pair.first, true);
      _UpdateInlinePreeditStatus(pair.first);
      pair.second.status = status;
      pair.second.__synced = false;
      rime_api->free_status(&status);
    }
  }
  m_ui->style() = get_session_status(m_active_session).style;
}

BOOL RimeWithWeaselHandler::ProcessKeyEvent(KeyEvent keyEvent,
                                            WeaselSessionId ipc_id,
                                            EatLine eat) {
  DLOG(INFO) << "Process key event: keycode = " << keyEvent.keycode
             << ", mask = " << keyEvent.mask << ", ipc_id = " << ipc_id;
  if (m_disabled)
    return FALSE;
  RimeSessionId session_id = to_session_id(ipc_id);
  SessionStatus& session_status = get_session_status(ipc_id);
  const bool previous_inline_preedit = session_status.style.inline_preedit;
  _ApplyHotAppearance(session_status);
  if (previous_inline_preedit != session_status.style.inline_preedit)
    _UpdateInlinePreeditStatus(ipc_id);
  _ApplyHotInputOptions(session_id, session_status.hot_input_generation,
                        session_status.hot_phrase_generation,
                        session_status.hot_fuzzy_generation);
  if (keyEvent.keycode != 0) {
    const int code = keyEvent.keycode >= 'a' && keyEvent.keycode <= 'z' ? keyEvent.keycode - 32 : keyEvent.keycode;
    const bool released = (keyEvent.mask & ibus::Modifier::RELEASE_MASK) != 0;
    if (session_status.feature_hotkeys_down.count(code)) {
      if (released) session_status.feature_hotkeys_down.erase(code);
      _Respond(ipc_id, eat); return TRUE;
    }
    if (!released && !(keyEvent.mask & (ibus::Modifier::SUPER_MASK | ibus::Modifier::META_MASK))) {
      const DWORD pressed = (DWORD(keyEvent.mask & (ibus::Modifier::SHIFT_MASK | ibus::Modifier::CONTROL_MASK | ibus::Modifier::ALT_MASK)) << 16) | code;
      const wchar_t* names[] = {L"Shortcut_Learning", L"Shortcut_NextProfile", L"Shortcut_PreviousProfile"};
      const DWORD defaults[] = {(12u << 16) | 'L', (12u << 16) | 0xff56, (12u << 16) | 0xff55};
      for (int index = 0; index < 3; ++index) {
        DWORD binding = defaults[index], size = sizeof(binding);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod", names[index], RRF_RT_REG_DWORD, nullptr, &binding, &size);
        if (!binding || binding != pressed) continue;
        session_status.feature_hotkeys_down.insert(code);
        if (index == 0) {
          DWORD enabled = !rime_api->get_option(session_id, "japanese_learning_mode");
          HKEY key;
          if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod", 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(key, L"Option_japanese_learning_mode", 0, REG_DWORD, reinterpret_cast<BYTE*>(&enabled), sizeof(enabled));
            DWORD generation = 0, bytes = sizeof(generation);
            RegQueryValueExW(key, L"InputSettingsGeneration", nullptr, nullptr, reinterpret_cast<BYTE*>(&generation), &bytes);
            ++generation;
            RegSetValueExW(key, L"InputSettingsGeneration", 0, REG_DWORD, reinterpret_cast<BYTE*>(&generation), sizeof(generation)); RegCloseKey(key);
          }
          rime_api->set_option(session_id, "japanese_learning_mode", enabled);
        } else {
          rime_api->commit_composition(session_id);
          wchar_t module[MAX_PATH] = {};
          GetModuleFileNameW(nullptr, module, MAX_PATH);
          std::wstring executable(module);
          executable = executable.substr(0, executable.find_last_of(L"\\")) + L"\\tools\\Settings-WinUI\\ZhongriSettingsV2.exe";
          ShellExecuteW(nullptr, L"open", executable.c_str(), index == 1 ? L"--ime-action next-profile" : L"--ime-action previous-profile", nullptr, SW_HIDE);
        }
        _Respond(ipc_id, eat); _UpdateUI(ipc_id); return TRUE;
      }
    }
  }
  // Candidate holds fire on the owning thread's timer; literal holds wait for release.
  // All other keys retain the old
  // immediate path. Flush a buffered tap before another key to preserve typing
  // order, and consume OS repeats so a hold emits only once.
  if (!session_status.long_press_replaying && keyEvent.keycode != 0) {
    const bool release = (keyEvent.mask & ibus::Modifier::RELEASE_MASK) != 0;
    const int physical = JapanesePhysicalKey(keyEvent.keycode);
    const bool hold_enabled = rime_api->get_option(session_id, "japanese_input_table_enabled") &&
        !rime_api->get_option(session_id, "ascii_mode") &&
        !(keyEvent.mask & (ibus::Modifier::CONTROL_MASK | ibus::Modifier::ALT_MASK |
            ibus::Modifier::SUPER_MASK | ibus::Modifier::HYPER_MASK | ibus::Modifier::META_MASK));
    auto replay_tap = [&](const JapaneseLongPressPending& pending) {
      session_status.long_press_replaying = true;
      session_status.defer_key_response = true;
      weasel::KeyEvent tap;
      tap.keycode = pending.keycode; tap.mask = pending.mask;
      const bool handled = ProcessKeyEvent(tap, ipc_id, eat) != FALSE;
      session_status.defer_key_response = false;
      session_status.long_press_replaying = false;
      if (!handled) session_status.literal_key_commit += static_cast<char>(pending.keycode);
    };
    if (session_status.long_press_released_later.count(physical)) {
      if (release) session_status.long_press_released_later.erase(physical);
      _Respond(ipc_id, eat); return TRUE;
    }
    if (session_status.long_press.keycode) {
      const auto pending = session_status.long_press;
      if (physical == JapanesePhysicalKey(pending.keycode)) {
        if (!release) { _Respond(ipc_id, eat); return TRUE; }
        session_status.long_press = {};
        if (hold_enabled && pending.IsLong(GetTickCount64())) {
          _DispatchLongPress(ipc_id,pending);
        } else replay_tap(pending);
        _Respond(ipc_id, eat); _UpdateUI(ipc_id); return TRUE;
      }
      if (!release) {
        session_status.long_press = {};
        session_status.long_press_released_later.insert(JapanesePhysicalKey(pending.keycode));
        if(hold_enabled && pending.definition.compose && pending.IsLong(GetTickCount64()))
          _DispatchLongPress(ipc_id,pending);
        else replay_tap(pending);
      }
    }
    if (!release && hold_enabled && keyEvent.keycode >= 33 && keyEvent.keycode <= 126) {
      char declarations[65536] = {};
      if (rime_api->get_property(session_id, "japanese_input_table_long_presses", declarations, sizeof(declarations))) {
        const auto definition = JapaneseLongPress(declarations, keyEvent.keycode);
        if (!definition.output.empty()) {
          session_status.long_press.keycode = keyEvent.keycode;
          session_status.long_press.mask = keyEvent.mask;
          session_status.long_press.started = GetTickCount64();
          session_status.long_press.definition = definition;
          m_active_session = ipc_id;
          _Respond(ipc_id, eat); return TRUE;
        }
      }
    }
  }
  // Keycode zero is a refresh request from an already-open TSF candidate
  // panel.  Apply the hot switches and return the current
  // composition; never send a synthetic key through the Rime engine.
  if (keyEvent.keycode == 0) {
    const CandidateRefreshSelection selection(session_id);
    const bool annotation_options_changed = _ApplyHotAnnotationOptions(session_id);
    DWORD annotation_generation = 0, annotation_size = sizeof(DWORD);
    const bool annotation_results_changed =
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                     L"AnnotationSettingsGeneration", RRF_RT_REG_DWORD, nullptr,
                     &annotation_generation, &annotation_size) == ERROR_SUCCESS &&
        annotation_generation != session_status.hot_annotation_generation;
    session_status.hot_annotation_generation = annotation_generation;
    const char* current_input = rime_api->get_input(session_id);
    bool refresh_cloud = false;
    {
      std::lock_guard<std::mutex> lock(m_cloud_candidate_mutex);
      // Another TSF client can poll the same hot-settings generation first.
      // Only the session whose spelling produced this result may consume it.
      if (!m_cloud_candidate_refresh.empty() &&
          m_cloud_candidate_refresh_session == session_id && current_input &&
          m_cloud_candidate_refresh.substr(0, m_cloud_candidate_refresh.find('\t')) == current_input &&
          rime_api->get_option(session_id, "google_cloud_candidates")) {
        refresh_cloud = true;
        m_cloud_candidate_refresh.clear();
        m_cloud_candidate_refresh_session = 0;
      }
    }
    if (refresh_cloud || (annotation_results_changed && !annotation_options_changed)) {
      // OnOptionUpdate refreshes only the unconfirmed segment.  Unlike
      // set_input, this preserves the user's current composition and caret.
      const Bool old = rime_api->get_option(session_id,
                                            "google_cloud_refresh");
      rime_api->set_option(session_id, "google_cloud_refresh", !old);
    }
    // Refreshing Rime's option preserves spelling/caret, but rebuilds the
    // menu with highlighted index zero. Keep the user's browsed candidate.
    if (annotation_options_changed || annotation_results_changed || refresh_cloud)
      selection.Restore(session_id);
    _Respond(ipc_id, eat);
    _UpdateUI(ipc_id);
    return TRUE;
  }
  const bool key_down =
      !(keyEvent.mask & ibus::Modifier::RELEASE_MASK);
  char segment_mode[4]={};
  const bool segment_edit=rime_api->get_property(session_id,"japanese_segment_active",segment_mode,sizeof(segment_mode)) && segment_mode[0]=='1';
  const bool plain_segment_enter=(keyEvent.keycode==ibus::Return || keyEvent.keycode==ibus::KP_Enter) &&
      !(keyEvent.mask & (ibus::SHIFT_MASK|ibus::CONTROL_MASK|ibus::ALT_MASK|ibus::SUPER_MASK|ibus::META_MASK));
  const bool segment_command=keyEvent.keycode==' ' || (keyEvent.keycode>='1'&&keyEvent.keycode<='9') ||
      keyEvent.keycode==ibus::Return || keyEvent.keycode==ibus::KP_Enter ||
      keyEvent.keycode==ibus::Keycode::Escape || keyEvent.keycode==ibus::Keycode::BackSpace ||
      (keyEvent.keycode>=0xff51 && keyEvent.keycode<=0xff54) || keyEvent.keycode==0xffca;
  if(segment_edit && segment_command && !plain_segment_enter) {
    // Segment digits choose locally; native expanded-row selection must not
    // commit them. Plain Enter retains the user's existing four-mode policy.
    const Bool handled=rime_api->process_key(session_id,keyEvent.keycode,expand_ibus_modifier(keyEvent.mask));
    if(session_status.expanded_candidates){session_status.expanded_candidates=false;session_status.__synced=false;}
    _Respond(ipc_id,eat);_UpdateUI(ipc_id);
    if(!session_status.status.is_composing){session_status.physical_preedit.clear();session_status.physical_preedit_valid=false;session_status.hold_labels.clear();}
    m_active_session=ipc_id;return handled;
  }
  if (key_down && (keyEvent.keycode==ibus::Return || keyEvent.keycode==ibus::KP_Enter) &&
      !(keyEvent.mask & (ibus::SHIFT_MASK | ibus::CONTROL_MASK | ibus::ALT_MASK |
                        ibus::SUPER_MASK | ibus::META_MASK)) &&
      !rime_api->get_option(session_id,"ascii_mode")) {
    DWORD mode=0,bytes=sizeof(mode);
    if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",L"EnterMode",
                    RRF_RT_REG_DWORD,nullptr,&mode,&bytes)==ERROR_SUCCESS && ValidEnterMode(mode)) {
      const auto action=ApplyEnterBehavior(rime_api,session_id,mode,
          session_status.expanded_candidates ? static_cast<int>(session_status.expanded_window_start) : -1);
      if (action.handled) {
        session_status.literal_key_commit+=action.raw;
        session_status.hold_labels.clear();session_status.physical_preedit.clear();
        session_status.physical_preedit_valid=false;
        session_status.expanded_candidates=false;session_status.__synced=false;
        _Respond(ipc_id,eat);_UpdateUI(ipc_id);
        // Old clients still apply the returned commit and then pass the real
        // Enter to the host. New clients gate one deferred replay on the edit
        // session, rather than submitting a newline string as application input.
        return action.forward ? FALSE : TRUE;
      }
    }
  }
  if(key_down && keyEvent.keycode==ibus::Keycode::BackSpace &&
      !(keyEvent.mask & (ibus::Modifier::CONTROL_MASK | ibus::Modifier::ALT_MASK |
          ibus::Modifier::SUPER_MASK | ibus::Modifier::HYPER_MASK | ibus::Modifier::META_MASK))) {
    session_status.PruneHoldLabels();
    if(!session_status.hold_labels.empty()) {
      RIME_STRUCT(RimeContext, context);
      const bool got_context=rime_api->get_context(session_id,&context);
      // Never reset a confirmed/partially selected prefix through set_input.
      const bool unconverted=got_context && context.composition.sel_start==0;
      if(got_context)rime_api->free_context(&context);
      const char* current=rime_api->get_input(session_id);
      std::string input=current?current:"";
      const size_t old_caret=rime_api->get_caret_pos(session_id);
      size_t caret=old_caret;
      auto tokens=session_status.hold_labels;
      if(unconverted && JapaneseEraseHoldToken(input,caret,tokens)) {
        // Apply a single logical edit, not N synthetic Backspace presses.
        // No intermediate Af -> A response or candidate update is exposed.
        if(input.empty())rime_api->clear_composition(session_id);
        else {
          rime_api->set_input(session_id,input.c_str());
          rime_api->set_caret_pos(session_id,caret);
        }
        const char* after=rime_api->get_input(session_id);
        const std::string actual=after?after:"";
        if(actual==input) {
          session_status.physical_preedit.erase(caret,old_caret-caret);
          session_status.hold_labels=std::move(tokens);
        }else {
          session_status.physical_preedit=actual;
          session_status.hold_labels.clear();
        }
        session_status.physical_preedit_valid=!actual.empty();
        _Respond(ipc_id,eat);_UpdateUI(ipc_id);return TRUE;
      }
    }
  }
  std::string custom_key_action;
  if (rime_api->get_option(session_id, "japanese_input_table_enabled") &&
      !rime_api->get_option(session_id, "ascii_mode") &&
      !(keyEvent.mask & (ibus::Modifier::CONTROL_MASK | ibus::Modifier::ALT_MASK |
                         ibus::Modifier::SUPER_MASK | ibus::Modifier::HYPER_MASK | ibus::Modifier::META_MASK))) {
    char actions[4096] = {};
    if (rime_api->get_property(session_id, "japanese_input_table_key_actions", actions, sizeof(actions))) {
      custom_key_action = JapaneseKeyAction(actions, keyEvent.keycode);
    }
  }
  const bool plain_minus =
      !(keyEvent.mask & (ibus::Modifier::SHIFT_MASK |
                         ibus::Modifier::CONTROL_MASK |
                         ibus::Modifier::ALT_MASK |
                         ibus::Modifier::SUPER_MASK |
                         ibus::Modifier::HYPER_MASK |
                         ibus::Modifier::META_MASK)) &&
      keyEvent.keycode == '-';
  const bool japanese_long_mark_key =
      plain_minus && session_status.status.is_composing && custom_key_action.empty();
  if (key_down) {
    const bool was_composing = session_status.status.is_composing;
    const bool plain_letters =
        !(keyEvent.mask & (ibus::Modifier::CONTROL_MASK |
                           ibus::Modifier::ALT_MASK |
                           ibus::Modifier::SUPER_MASK |
                           ibus::Modifier::HYPER_MASK |
                           ibus::Modifier::META_MASK)) &&
        ((keyEvent.keycode >= 'a' && keyEvent.keycode <= 'z') ||
         (keyEvent.keycode >= 'A' && keyEvent.keycode <= 'Z') ||
         (custom_key_action == "input" && keyEvent.keycode >= 33 && keyEvent.keycode <= 126));
    if (plain_letters) {
      if (!was_composing) {
        session_status.hold_labels.clear();
        session_status.physical_preedit.clear();
        session_status.physical_preedit_valid = true;
      }
      if (session_status.physical_preedit_valid) {
        session_status.physical_preedit.push_back(static_cast<char>(
            rime_api->get_option(session_id, "japanese_input_table_enabled") ? keyEvent.keycode :
            std::tolower(static_cast<unsigned char>(keyEvent.keycode))));
      }
    } else if (japanese_long_mark_key &&
               session_status.physical_preedit_valid) {
      // Preserve the key the user really pressed in the inline raw preedit.
      // The Japanese schema accepts '-' directly and Mozc converts it to ー.
      session_status.physical_preedit.push_back('-');
    } else if (keyEvent.keycode == ibus::Keycode::BackSpace &&
               session_status.physical_preedit_valid) {
      if (!session_status.physical_preedit.empty())
        session_status.physical_preedit.pop_back();
    } else if (keyEvent.keycode == ibus::Keycode::Escape) {
      session_status.physical_preedit.clear();
      session_status.physical_preedit_valid = false;
    }
    session_status.PruneHoldLabels();
  }
  if (japanese_long_mark_key &&
      !rime_api->get_option(session_id, "japanese_input_table_enabled")) {
    // Rime's mixed segmentation treats the physical minus as punctuation.
    // Route both press and release through the existing Japanese q spelling;
    // physical_preedit still records '-' for the user's visible input code.
    // Imported tables must receive the literal key: AZIK's q means ん, not
    // choon. Its Lua processor already handles contextual '-' -> ー.
    keyEvent.keycode = 'q';
  }
  const bool expand_key = JapaneseNextRowKey(keyEvent.keycode, custom_key_action);
  const bool expand_back_key = JapanesePreviousRowKey(keyEvent.keycode, custom_key_action);
  CandidateExpandTrace("ProcessKey key=" + std::to_string(keyEvent.keycode) +
                       " mask=" + std::to_string(keyEvent.mask) +
                       " expand_key=" + std::to_string(expand_key) +
                       " expanded=" +
                       std::to_string(session_status.expanded_candidates));
  // Brackets are ordinary punctuation when no candidate menu exists.  The
  // Expanded-row navigation must never consume brackets globally. Otherwise
  // `[` and `]` stop working in normal typing and in ASCII mode.
  bool navigation_menu_active = false;
  {
    RIME_STRUCT(RimeContext, trace_ctx);
    if (rime_api->get_context(session_id, &trace_ctx)) {
      navigation_menu_active = trace_ctx.menu.num_candidates > 0;
      CandidateExpandTrace(
          "KEY_CONTEXT_BEFORE highlighted=" +
          std::to_string(trace_ctx.menu.highlighted_candidate_index) +
          " count=" + std::to_string(trace_ctx.menu.num_candidates));
      rime_api->free_context(&trace_ctx);
    }
  }
  LOG(INFO) << "[candidate-expand] key=" << keyEvent.keycode
            << " mask=" << keyEvent.mask
            << " expand_key=" << expand_key
            << " expanded=" << session_status.expanded_candidates;
  // Either navigation key enters the persistent expanded grid.  Previously
  // the backward key was accepted only after expansion had already happened;
  // pressing it on the first row therefore produced merely a transient host
  // key-down layout and reverted on release.
  if ((expand_key || expand_back_key) && navigation_menu_active) {
    // Consume both key-down and key-up so the schema's original paging binding
    // cannot run after our row navigation.
    if (keyEvent.mask & ibus::Modifier::RELEASE_MASK) {
      // A consumed IPC key event still needs a complete response.  Returning
      // without _Respond() makes the TSF client parse an empty context and
      // replace the 100 expanded candidates with an empty list on key-up.
      // Preserve the current selection/menu; do not process or page again.
      CandidateExpandTrace("NAV_KEY_RELEASE_RESPOND_CURRENT_CONTEXT");
      _Respond(ipc_id, eat);
      return TRUE;
    }

    bool entered_expanded = false;
    if (!session_status.expanded_candidates) {
      session_status.expanded_candidates = true;
      session_status.__synced = false;
      entered_expanded = true;
      CandidateExpandTrace("EXPANDED_BY_KEY ipc=" + std::to_string(ipc_id));
      // Build the expanded DirectWrite layout before deciding where the first
      // navigation key should land.  Without this, the first key still uses
      // the preceding one-line layout and has to guess the row boundaries.
      _UpdateUI(ipc_id);
    }

    bool navigation_changed = false;
    RIME_STRUCT(RimeContext, nav_ctx);
    if (rime_api->get_context(session_id, &nav_ctx)) {
      CandidateInfo expanded_info;
      _GetExpandedCandidateInfo(expanded_info, nav_ctx, session_id,
                                session_status, false);
      const size_t base = session_status.expanded_window_start;
      const size_t absolute_current =
          static_cast<size_t>(nav_ctx.menu.page_no) * nav_ctx.menu.page_size +
          nav_ctx.menu.highlighted_candidate_index;
      const int current = static_cast<int>(absolute_current - base);
      const int count = static_cast<int>(expanded_info.candies.size());
      // DirectWrite is the single source of truth for variable-width rows.
      // Text, translation comments and hidden readings can all widen a cell;
      // a separate character-count estimate inevitably drifts from the row
      // the user can actually see.  Keep the old estimator only as a safety
      // fallback when no layout exists (for example during early startup).
      auto rows = m_ui ? m_ui->GetExpandedCandidateRows()
                       : std::vector<std::vector<int>>{};
      const bool using_rendered_rows = !ReadCandidateSplitOptions().enabled&&CandidateRowsMatchCount(rows, count);
      if (!using_rendered_rows)
        rows = BuildVisualCandidateRows(
            expanded_info, session_status.style.expanded_comment_width,m_ui,&session_status.style);
      std::string row_trace = std::string("ROW_MAP source=") +
                              (using_rendered_rows ? "ui" : "fallback") +
                              " starts=";
      for (size_t r = 0; r < rows.size(); ++r) {
        if (r) row_trace += ",";
        row_trace += std::to_string(rows[r].front());
      }
      CandidateExpandTrace(row_trace);
      int current_row = 0;
      for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const auto found = std::find(rows[r].begin(), rows[r].end(), current);
        if (found != rows[r].end()) {
          current_row = r;
          break;
        }
      }
      if (expand_key) {
        if (current_row + 1 < static_cast<int>(rows.size())) {
          const auto& next_row = rows[current_row + 1];
          const int target = next_row.front();
          rime_api->highlight_candidate(session_id, base + target);
          navigation_changed = target != current;
          CandidateExpandTrace("ROW_DOWN from=" + std::to_string(current) +
                               " to=" + std::to_string(target));
        } else {
          if (!expanded_info.is_last_page &&
              rime_api->highlight_candidate(session_id, base + count)) {
            const size_t shift = ReadCandidateSplitOptions().enabled ? count :
                (rows.empty() ? 6 : rows.front().size());
            session_status.expanded_window_start += shift;
            session_status.expanded_shift_history.push_back(shift);
            ++session_status.expanded_skipped_rows;
            if(ReadCandidateSplitOptions().enabled) {
              RIME_STRUCT(RimeContext, next);
              if(rime_api->get_context(session_id,&next)) {
                CandidateInfo info;
                _GetExpandedCandidateInfo(info,next,session_id,session_status,false);
                const auto next_rows=BuildVisualCandidateRows(info,true,m_ui,&session_status.style);
                if(!next_rows.empty())rime_api->highlight_candidate(session_id,
                    session_status.expanded_window_start+next_rows.front().front());
                rime_api->free_context(&next);
              }
            }
            navigation_changed = true;
          }
          CandidateExpandTrace("NEXT_EXPANDED_ROW from_window_end");
        }
      } else if (current_row > 0) {
        const auto& previous_row = rows[current_row - 1];
        const int target = previous_row.front();
        rime_api->highlight_candidate(session_id, base + target);
        navigation_changed = target != current;
        CandidateExpandTrace("ROW_UP from=" + std::to_string(current) +
                             " to=" + std::to_string(target));
      } else if (base > 0 &&
                 rime_api->highlight_candidate(session_id, base - 1)) {
        const size_t shift = session_status.expanded_shift_history.empty() ?
            (base < 6 ? base : 6) :
            session_status.expanded_shift_history.back();
        if (!session_status.expanded_shift_history.empty())
          session_status.expanded_shift_history.pop_back();
        session_status.expanded_window_start -= shift;
        session_status.expanded_skipped_rows =
            max(0, session_status.expanded_skipped_rows - 1);
        if(ReadCandidateSplitOptions().enabled) {
          RIME_STRUCT(RimeContext, previous);
          if(rime_api->get_context(session_id,&previous)) {
            CandidateInfo info;
            _GetExpandedCandidateInfo(info,previous,session_id,session_status,false);
            const auto previous_rows=BuildVisualCandidateRows(info,true,m_ui,&session_status.style);
            if(!previous_rows.empty())rime_api->highlight_candidate(session_id,
                session_status.expanded_window_start+previous_rows.back().front());
            rime_api->free_context(&previous);
          }
        }
        navigation_changed = true;
        CandidateExpandTrace("PREVIOUS_EXPANDED_ROW from_window_start");
      }
      rime_api->free_context(&nav_ctx);
    }
    {
      RIME_STRUCT(RimeContext, trace_ctx);
      if (rime_api->get_context(session_id, &trace_ctx)) {
        CandidateExpandTrace(
            "KEY_CONTEXT_AFTER_NAV highlighted=" +
            std::to_string(trace_ctx.menu.highlighted_candidate_index) +
            " count=" + std::to_string(trace_ctx.menu.num_candidates));
        rime_api->free_context(&trace_ctx);
      }
    }
    if (!entered_expanded && !navigation_changed) {
      // At the first/last available row there is nowhere to move.  The IPC
      // caller still expects a complete current-context response; returning
      // an empty response makes TSF interpret it as an ended composition and
      // hide the entire candidate window.
      CandidateExpandTrace("ROW_BOUNDARY_KEEP_CURRENT");
      _Respond(ipc_id, eat);
      return TRUE;
    }
    _Respond(ipc_id, eat);
    _UpdateUI(ipc_id);
    return TRUE;
  }
  if (!(keyEvent.mask & ibus::Modifier::RELEASE_MASK) &&
      custom_key_action.empty() && session_status.expanded_candidates && keyEvent.keycode >= '1' &&
      keyEvent.keycode <= '9') {
    RIME_STRUCT(RimeContext, number_ctx);
    if (rime_api->get_context(session_id, &number_ctx)) {
      CandidateInfo expanded_info;
      _GetExpandedCandidateInfo(expanded_info, number_ctx, session_id,
                                session_status, false);
      auto rows = m_ui ? m_ui->GetExpandedCandidateRows()
                       : std::vector<std::vector<int>>{};
      if (ReadCandidateSplitOptions().enabled||!CandidateRowsMatchCount(rows,
                                   static_cast<int>(expanded_info.candies.size())))
        rows = BuildVisualCandidateRows(
            expanded_info, session_status.style.expanded_comment_width,m_ui,&session_status.style);
      const size_t base = session_status.expanded_window_start;
      const int current = static_cast<int>(
          static_cast<size_t>(number_ctx.menu.page_no) *
              number_ctx.menu.page_size +
          number_ctx.menu.highlighted_candidate_index - base);
      for (const auto& row : rows) {
        if (std::find(row.begin(), row.end(), current) != row.end()) {
          const int ordinal = keyEvent.keycode - '1';
          if (ordinal < static_cast<int>(row.size()))
            rime_api->select_candidate(session_id, base + row[ordinal]);
          break;
        }
      }
      rime_api->free_context(&number_ctx);
    }
    _Respond(ipc_id, eat);
    _UpdateUI(ipc_id);
    return TRUE;
  }
  if (!(keyEvent.mask & ibus::Modifier::RELEASE_MASK) &&
      keyEvent.keycode == ibus::Keycode::Escape &&
      session_status.expanded_candidates) {
    session_status.expanded_candidates = false;
    session_status.__synced = false;
    _Respond(ipc_id, eat);
    _UpdateUI(ipc_id);
    return TRUE;
  }
  Bool handled = rime_api->process_key(session_id, keyEvent.keycode,
                                       expand_ibus_modifier(keyEvent.mask));
  char edited_mode[4]={};
  if(session_status.expanded_candidates && rime_api->get_property(session_id,"japanese_segment_active",edited_mode,sizeof(edited_mode)) && edited_mode[0]=='1') {
    session_status.expanded_candidates=false;session_status.__synced=false;
  }
  // vim_mode when keydown only
  if (!handled && !(keyEvent.mask & ibus::Modifier::RELEASE_MASK)) {
    bool isVimBackInCommandMode =
        (keyEvent.keycode == ibus::Keycode::Escape) ||
        ((keyEvent.mask & (1 << 2)) &&
         (keyEvent.keycode == ibus::Keycode::XK_c ||
          keyEvent.keycode == ibus::Keycode::XK_C ||
          keyEvent.keycode == ibus::Keycode::XK_bracketleft));
    if (isVimBackInCommandMode &&
        rime_api->get_option(session_id, "vim_mode") &&
        !rime_api->get_option(session_id, "ascii_mode")) {
      rime_api->set_option(session_id, "ascii_mode", True);
    }
  }
  _Respond(ipc_id, eat);
  _UpdateUI(ipc_id);
  if (!session_status.status.is_composing) {
    session_status.physical_preedit.clear();
    session_status.physical_preedit_valid = false;
  }
  m_active_session = ipc_id;
  return (BOOL)handled;
}

void RimeWithWeaselHandler::CommitComposition(WeaselSessionId ipc_id) {
  DLOG(INFO) << "Commit composition: ipc_id = " << ipc_id;
  if (m_disabled)
    return;
  rime_api->commit_composition(to_session_id(ipc_id));
  SessionStatus& session_status = get_session_status(ipc_id);
  session_status.physical_preedit.clear();
  session_status.physical_preedit_valid = false;
  _UpdateUI(ipc_id);
  m_active_session = ipc_id;
}

void RimeWithWeaselHandler::ClearComposition(WeaselSessionId ipc_id) {
  DLOG(INFO) << "Clear composition: ipc_id = " << ipc_id;
  if (m_disabled)
    return;
  // Losing focus/clicking away clears the Rime composition through this IPC
  // command without going through _Respond(), where expanded mode is normally
  // reset.  Clear it here as part of the same composition lifetime so the next
  // input in this window always starts in the ordinary one-line layout.
  SessionStatus& session_status = get_session_status(ipc_id);
  session_status.long_press = {};
  session_status.hold_labels.clear();
  session_status.physical_preedit.clear();
  session_status.physical_preedit_valid = false;
  if (session_status.expanded_candidates) {
    session_status.expanded_candidates = false;
    session_status.__synced = false;
    CandidateExpandTrace("RESET_AFTER_CLEAR_COMPOSITION ipc=" +
                         std::to_string(ipc_id));
  }
  rime_api->clear_composition(to_session_id(ipc_id));
  _UpdateUI(ipc_id);
  m_active_session = ipc_id;
}

void RimeWithWeaselHandler::SelectCandidateOnCurrentPage(
    size_t index,
    WeaselSessionId ipc_id) {
  DLOG(INFO) << "select candidate on current page, ipc_id = " << ipc_id
             << ", index = " << index;
  if (m_disabled)
    return;
  char segment_mode[4]={};
  const auto session_id=to_session_id(ipc_id);
  if(rime_api->get_property(session_id,"japanese_segment_active",segment_mode,sizeof(segment_mode)) && segment_mode[0]=='1') {
    if(index<9){
      rime_api->set_property(session_id,"japanese_segment_pick",std::to_string(index).c_str());
      rime_api->process_key(session_id,0xffca,0);
      _UpdateUI(ipc_id);
    }
    return;
  }
  const SessionStatus& status = get_session_status(ipc_id);
  if (status.expanded_candidates)
    rime_api->select_candidate(to_session_id(ipc_id),
                               status.expanded_window_start + index);
  else
    rime_api->select_candidate_on_current_page(to_session_id(ipc_id), index);
}

bool RimeWithWeaselHandler::HighlightCandidateOnCurrentPage(
    size_t index,
    WeaselSessionId ipc_id,
    EatLine eat) {
  DLOG(INFO) << "highlight candidate on current page, ipc_id = " << ipc_id
             << ", index = " << index;
  const SessionStatus& status = get_session_status(ipc_id);
  bool res = status.expanded_candidates ?
      rime_api->highlight_candidate(to_session_id(ipc_id),
                                    status.expanded_window_start + index) :
      rime_api->highlight_candidate_on_current_page(to_session_id(ipc_id),
                                                     index);
  _Respond(ipc_id, eat);
  _UpdateUI(ipc_id);
  return res;
}

bool RimeWithWeaselHandler::ChangePage(bool backward,
                                       WeaselSessionId ipc_id,
                                       EatLine eat) {
  DLOG(INFO) << "change page, ipc_id = " << ipc_id
             << (backward ? "backward" : "foreward");
  SessionStatus& session_status = get_session_status(ipc_id);
  CandidateExpandTrace("ChangePage backward=" + std::to_string(backward) +
                       " expanded=" +
                       std::to_string(session_status.expanded_candidates));
  LOG(INFO) << "[candidate-expand] ChangePage backward=" << backward
            << " expanded=" << session_status.expanded_candidates
            << " ipc_id=" << ipc_id;
  if (!backward && !session_status.expanded_candidates) {
    session_status.expanded_candidates = true;
    session_status.__synced = false;
    CandidateExpandTrace("EXPANDED_BY_CHANGEPAGE");
    LOG(INFO) << "[candidate-expand] expanded by ChangePage";
    _Respond(ipc_id, eat);
    _UpdateUI(ipc_id);
    return true;
  }
  if (session_status.expanded_candidates) {
    KeyEvent navigation{0, 0};
    navigation.keycode = backward ? ibus::Page_Up : ibus::Page_Down;
    return ProcessKeyEvent(navigation, ipc_id, eat) == TRUE;
  }
  bool res = rime_api->change_page(to_session_id(ipc_id), backward);
  _Respond(ipc_id, eat);
  _UpdateUI(ipc_id);
  return res;
}

void RimeWithWeaselHandler::FocusIn(DWORD client_caps, WeaselSessionId ipc_id) {
  DLOG(INFO) << "Focus in: ipc_id = " << ipc_id
             << ", client_caps = " << client_caps;
  if (m_disabled)
    return;
  _UpdateUI(ipc_id);
  m_active_session = ipc_id;
}

void RimeWithWeaselHandler::FocusOut(DWORD param, WeaselSessionId ipc_id) {
  DLOG(INFO) << "Focus out: ipc_id = " << ipc_id;
  auto& session = get_session_status(ipc_id);
  session.long_press = {};
  session.long_press_released_later.clear();
  session.feature_hotkeys_down.clear();
  // Delayed notifications from another host must not hide or disable the
  // current session's timed hold. Only its owner can release active focus.
  if (m_active_session == ipc_id) {
    if (m_ui) m_ui->Hide();
    m_active_session = 0;
  }
}

void RimeWithWeaselHandler::UpdateInputPosition(RECT const& rc,
                                                WeaselSessionId ipc_id) {
  DLOG(INFO) << "Update input position: (" << rc.left << ", " << rc.top
             << "), ipc_id = " << ipc_id
             << ", m_active_session = " << m_active_session;
  if (m_ui)
    m_ui->UpdateInputPosition(rc);
  if (m_disabled)
    return;
  SessionStatus& traced_session = get_session_status(ipc_id);
  CandidateExpandTrace(
      "INPUT_POS ipc=" + std::to_string(ipc_id) + " app=" +
      traced_session.client_app + " rc=" + std::to_string(rc.left) + ":" +
      std::to_string(rc.top) + ":" + std::to_string(rc.right) + ":" +
      std::to_string(rc.bottom));
  if (m_active_session != ipc_id) {
    _UpdateUI(ipc_id);
    m_active_session = ipc_id;
  }
}

std::string RimeWithWeaselHandler::m_message_type;
std::string RimeWithWeaselHandler::m_message_value;
std::string RimeWithWeaselHandler::m_message_label;
std::string RimeWithWeaselHandler::m_option_name;
std::mutex RimeWithWeaselHandler::m_notifier_mutex;

void RimeWithWeaselHandler::OnNotify(void* context_object,
                                     uintptr_t session_id,
                                     const char* message_type,
                                     const char* message_value) {
  // may be running in a thread when deploying rime
  RimeWithWeaselHandler* self =
      reinterpret_cast<RimeWithWeaselHandler*>(context_object);
  if (!self || !message_type || !message_value)
    return;
  std::lock_guard<std::mutex> lock(m_notifier_mutex);
  m_message_type = message_type;
  m_message_value = message_value;
  if (RIME_API_AVAILABLE(rime_api, get_state_label) &&
      !strcmp(message_type, "option")) {
    Bool state = message_value[0] != '!';
    const char* option_name = message_value + !state;
    m_option_name = option_name;
    const char* state_label =
        rime_api->get_state_label(session_id, option_name, state);
    if (state_label) {
      m_message_label = std::string(state_label);
    }
  }
}

void RimeWithWeaselHandler::_ReadClientInfo(WeaselSessionId ipc_id,
                                            LPWSTR buffer) {
  std::string app_name;
  bool tsf_client = false;
  // parse request text
  wbufferstream bs(buffer, WEASEL_IPC_BUFFER_LENGTH);
  std::wstring line;
  while (bs.good()) {
    std::getline(bs, line);
    if (!bs.good())
      break;
    // file ends
    if (line == L".")
      break;
    const std::wstring kClientAppKey = L"session.client_app=";
    if (starts_with(line, kClientAppKey)) {
      std::wstring lwr = line;
      to_lower(lwr);
      app_name = wtou8(lwr.substr(kClientAppKey.length()));
    }
    const std::wstring kClientTypeKey = L"session.client_type=";
    if (starts_with(line, kClientTypeKey))
      tsf_client = line.substr(kClientTypeKey.length(), 3) == L"tsf";
  }
  SessionStatus& session_status = get_session_status(ipc_id);
  session_status.client_app = app_name;
  session_status.tsf_client = tsf_client;
  CandidateExpandTrace("SESSION_CLIENT ipc=" + std::to_string(ipc_id) +
                       " app=" + app_name);
  RimeSessionId session_id = session_status.session_id;
  // set app specific options
  if (!app_name.empty()) {
    rime_api->set_property(session_id, "client_app", app_name.c_str());

    auto it = m_app_options.find(app_name);
    if (it != m_app_options.end()) {
      AppOptions& options(m_app_options[it->first]);
      for (const auto& pair : options) {
        DLOG(INFO) << "set app option: " << pair.first << " = " << pair.second;
        rime_api->set_option(session_id, pair.first.c_str(), Bool(pair.second));
      }
    }
  }
  // inline preedit
  bool inline_preedit = session_status.style.inline_preedit;
  rime_api->set_option(session_id, "inline_preedit", Bool(inline_preedit));
  // show soft cursor on weasel panel but not inline
  rime_api->set_option(session_id, "soft_cursor", Bool(!inline_preedit));
}

void RimeWithWeaselHandler::_GetCandidateInfo(CandidateInfo& cinfo,
                                               RimeContext& ctx,
                                               RimeSessionId session_id,
                                               bool escape_for_ipc) {
  bool show_english_annotation =
      rime_api->get_option(session_id, "show_english_annotation");
  bool show_japanese_annotation =
      rime_api->get_option(session_id, "show_japanese_annotation");
  // The registry values are the live settings channel.  Keep user.yaml as the
  // startup fallback for configurations created before hot settings existed.
  DWORD hot_value = 0;
  DWORD hot_size = sizeof(hot_value);
  const bool hot_english =
      RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                   L"ShowEnglishAnnotation", RRF_RT_REG_DWORD, nullptr,
                   &hot_value, &hot_size) == ERROR_SUCCESS;
  if (hot_english) show_english_annotation = hot_value != 0;
  hot_size = sizeof(hot_value);
  const bool hot_japanese =
      RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                   L"ShowJapaneseAnnotation", RRF_RT_REG_DWORD, nullptr,
                   &hot_value, &hot_size) == ERROR_SUCCESS;
  if (hot_japanese) show_japanese_annotation = hot_value != 0;
  RimeConfig user_config = {NULL};
  if ((!hot_english || !hot_japanese) &&
      rime_api->user_config_open("user", &user_config)) {
    Bool persisted = False;
    if (!hot_english && rime_api->config_get_bool(
            &user_config, "var/option/show_english_annotation", &persisted)) {
      show_english_annotation = persisted;
    }
    if (!hot_japanese && rime_api->config_get_bool(
            &user_config, "var/option/show_japanese_annotation", &persisted)) {
      show_japanese_annotation = persisted;
    }
    rime_api->config_close(&user_config);
  }
  CandidateExpandTrace(
      "ANNOTATION_OPTIONS en=" +
      std::to_string(show_english_annotation) + " ja=" +
      std::to_string(show_japanese_annotation) + " session=" +
      std::to_string(session_id));
  cinfo.candies.resize(ctx.menu.num_candidates);
  cinfo.comments.resize(ctx.menu.num_candidates);
  cinfo.labels.resize(ctx.menu.num_candidates);
  for (int i = 0; i < ctx.menu.num_candidates; ++i) {
    std::wstring displayed_text = u8tow(ctx.menu.candidates[i].text);
    bool is_common_phrase = false;
    bool has_fuzzy_reading = false;
    std::wstring fuzzy_reading_comment;
    std::wstring fuzzy_typed_kana;
    std::wstring fuzzy_corrected_kana;
    std::wstring hidden_language_marker;
    if (ctx.menu.candidates[i].comment) {
      const std::string raw_comment(ctx.menu.candidates[i].comment);
      if (raw_comment.find("[[RIME_LANG:CP]]") != std::string::npos)
        hidden_language_marker = L"[[RIME_LANG:CP]]";
      else if (raw_comment.find("[[RIME_LANG:JA]]") != std::string::npos)
        hidden_language_marker = L"[[RIME_LANG:JA]]";
      else if (raw_comment.find("[[RIME_LANG:ZH]]") != std::string::npos)
        hidden_language_marker = L"[[RIME_LANG:ZH]]";
      if (i < 8) {
        CandidateExpandTrace("ANNOTATION_RAW i=" + std::to_string(i) +
                             " text_hex=" +
                             HexBytes(ctx.menu.candidates[i].text) +
                             " comment_hex=" + HexBytes(raw_comment));
      }
      is_common_phrase =
          raw_comment.rfind(std::string(1, static_cast<char>(0x1f)) +
                                "COMMON_PHRASE",
                            0) == 0;
      const std::string fuzzy_reading_marker = "[JF_READING]";
      if (raw_comment.rfind(fuzzy_reading_marker, 0) == 0) {
        std::string clean_reading =
            raw_comment.substr(fuzzy_reading_marker.length());
        const std::string typed_marker = "[[RIME_FUZZY_TYPED:";
        const size_t typed_begin = clean_reading.find(typed_marker);
        if (typed_begin != std::string::npos) {
          const size_t typed_end = clean_reading.find("]]", typed_begin);
          if (typed_end != std::string::npos) {
            fuzzy_typed_kana = u8tow(clean_reading.substr(
                typed_begin + typed_marker.length(),
                typed_end - typed_begin - typed_marker.length()));
            clean_reading.erase(typed_begin, typed_end + 2 - typed_begin);
          }
        }
        for (const std::string marker : {std::string("[[RIME_LANG:CP]]"),
                                         std::string("[[RIME_LANG:JA]]"),
                                         std::string("[[RIME_LANG:ZH]]")}) {
          size_t marker_pos = 0;
          while ((marker_pos = clean_reading.find(marker, marker_pos)) !=
                 std::string::npos) {
            clean_reading.erase(marker_pos, marker.length());
          }
        }
        const std::wstring reading = u8tow(clean_reading);
        if (!reading.empty()) {
          // Display-only decoration. The genuine Rime candidate remains
          // unchanged, so committing never inserts the parenthesized reading.
          fuzzy_reading_comment = L"[" + reading + L"]";
          fuzzy_corrected_kana = reading;
          has_fuzzy_reading = true;
        }
      } else if (raw_comment.rfind("[JF:", 0) == 0) {
        // A fuzzy translator exposes its genuine dictionary spelling as
        // [JF:RULE]romaji.  Render only the useful spelling; never leak the
        // internal rule name into the candidate panel.
        const size_t marker_end = raw_comment.find(']');
        if (marker_end != std::string::npos &&
            marker_end + 1 < raw_comment.length()) {
          std::string clean_romaji = raw_comment.substr(marker_end + 1);
          for (const std::string marker : {std::string("[[RIME_LANG:CP]]"),
                                           std::string("[[RIME_LANG:JA]]"),
                                           std::string("[[RIME_LANG:ZH]]")}) {
            size_t marker_pos = 0;
            while ((marker_pos = clean_romaji.find(marker, marker_pos)) !=
                   std::string::npos) {
              clean_romaji.erase(marker_pos, marker.length());
            }
          }
          const std::wstring romaji = u8tow(clean_romaji);
          if (!romaji.empty()) {
            fuzzy_reading_comment = L"[" + romaji + L"]";
            fuzzy_corrected_kana = romaji;
            has_fuzzy_reading = true;
          }
        }
      }
    }
    // Keep the complete candidate inside librime (and therefore commit the
    // complete phrase), while presenting a compact WeChat-like preview.
    if (is_common_phrase && displayed_text.length() > 8) {
      displayed_text = displayed_text.substr(0, 8) + L"…";
    }
    cinfo.candies[i].str =
        escape_for_ipc ? escape_string(displayed_text) : displayed_text;
    
    // 🔍 DEBUG: 记录从 Rime API 获取的 comment
    if (has_fuzzy_reading) {
      if (!fuzzy_typed_kana.empty())
        fuzzy_reading_comment += L"[[RIME_FUZZY_TYPED:" + fuzzy_typed_kana + L"]]";
      if (!fuzzy_corrected_kana.empty())
        fuzzy_reading_comment += L"[[RIME_FUZZY_READING:" + fuzzy_corrected_kana + L"]]";
      cinfo.comments[i].str = escape_for_ipc
                                  ? escape_string(fuzzy_reading_comment)
                                  : fuzzy_reading_comment;
    } else if (ctx.menu.candidates[i].comment && !is_common_phrase) {
      std::string comment_utf8(ctx.menu.candidates[i].comment);
      // std::cerr << "[9_WEASEL_UI] text='" << ctx.menu.candidates[i].text 
      //          << "' comment='" << comment_utf8 << "'" << std::endl;
      std::wstring displayed_comment =
          u8tow(ctx.menu.candidates[i].comment);
      // Preserve the private language tag separately.  Annotation parsing
      // below rebuilds the visible comment and would otherwise discard it.
      // The UI consumes this suffix for per-language colors and strips it
      // before drawing, so users never see the marker itself.
      for (const std::wstring marker : {std::wstring(L"[[RIME_LANG:CP]]"),
                                        std::wstring(L"[[RIME_LANG:JA]]"),
                                        std::wstring(L"[[RIME_LANG:ZH]]")}) {
        size_t marker_pos = 0;
        while ((marker_pos = displayed_comment.find(marker, marker_pos)) !=
               std::wstring::npos) {
          displayed_comment.erase(marker_pos, marker.length());
        }
      }
      const std::wstring record_separator(1, static_cast<wchar_t>(0x1e));
      const std::wstring en_marker = record_separator + L"EN:";
      const std::wstring ja_marker = record_separator + L"JA:";
      const std::wstring reading_marker = L"[[RIME_JR:";
      const std::wstring reading_marker_end = L"]]";
      const size_t reading_pos = displayed_comment.find(reading_marker);
      std::wstring hidden_reading;
      if (reading_pos != std::wstring::npos) {
        const size_t reading_end = displayed_comment.find(
            reading_marker_end, reading_pos + reading_marker.length());
        if (reading_end != std::wstring::npos) {
          hidden_reading = displayed_comment.substr(
              reading_pos + reading_marker.length(),
              reading_end - (reading_pos + reading_marker.length()));
          displayed_comment.erase(
              reading_pos,
              reading_end + reading_marker_end.length() - reading_pos);
        }
      }
      const size_t en_pos = displayed_comment.find(en_marker);
      const size_t ja_pos = displayed_comment.find(ja_marker);
      if (en_pos != std::wstring::npos || ja_pos != std::wstring::npos) {
        std::wstring english;
        std::wstring japanese;
        if (en_pos != std::wstring::npos) {
          const size_t begin = en_pos + en_marker.length();
          const size_t end = ja_pos != std::wstring::npos && ja_pos > begin
                                 ? ja_pos
                                 : displayed_comment.length();
          english = displayed_comment.substr(begin, end - begin);
          while (!english.empty() &&
                 (english.back() == L'\n' || english.back() == L'\r'))
            english.pop_back();
        }
        if (ja_pos != std::wstring::npos) {
          const size_t begin = ja_pos + ja_marker.length();
          japanese = displayed_comment.substr(begin);
          while (!japanese.empty() &&
                 (japanese.front() == L'\n' || japanese.front() == L'\r'))
            japanese.erase(japanese.begin());
        }
        displayed_comment.clear();
        if (show_english_annotation && !english.empty())
          displayed_comment = english;
        if (show_japanese_annotation && !japanese.empty()) {
          if (!displayed_comment.empty()) displayed_comment += L"\n";
          displayed_comment += japanese;
        }
      } else if (show_english_annotation != show_japanese_annotation) {
        // OpenCC/Lua may rebuild a candidate and remove the hidden EN:/JA:
        // markers while preserving the documented line order: English first,
        // Japanese second.  Use that representation as a fallback.
        const size_t newline = displayed_comment.find(L'\n');
        if (newline != std::wstring::npos) {
          std::wstring english = displayed_comment.substr(0, newline);
          std::wstring japanese = displayed_comment.substr(newline + 1);
          while (!english.empty() && english.back() == L'\r')
            english.pop_back();
          while (!japanese.empty() && japanese.front() == L'\r')
            japanese.erase(japanese.begin());
          displayed_comment = show_english_annotation ? english : japanese;
        }
      }
      // Translation annotations are already shown on separate lines; the
      // language prefixes waste scarce grid width.  Remove every occurrence
      // here, before IPC escaping/serialization, so all UI implementations see
      // the same clean comment.
      for (const std::wstring prefix : {std::wstring(L"EN:"),
                                        std::wstring(L"JA:")}) {
        size_t pos = 0;
        while ((pos = displayed_comment.find(prefix, pos)) !=
               std::wstring::npos) {
          displayed_comment.erase(pos, prefix.length());
        }
      }
      // Keep the reading in the existing comment string instead of extending
      // CandidateInfo's serialized layout.  Old and new client DLLs therefore
      // decode labels identically; the panel strips this private suffix.
      if (!hidden_reading.empty())
        displayed_comment +=
            reading_marker + hidden_reading + reading_marker_end;
      cinfo.comments[i].str = escape_for_ipc
                                  ? escape_string(displayed_comment)
                                  : displayed_comment;
    } else {
      // std::cerr << "[9_WEASEL_UI] text='" << ctx.menu.candidates[i].text
      //          << "' comment=NULL" << std::endl;
    }
    if (!hidden_language_marker.empty())
      cinfo.comments[i].str += hidden_language_marker;
    if ((!ShowSingleCharacterAnnotations() &&
         IsSingleAnnotationCharacter(u8tow(ctx.menu.candidates[i].text))) ||
        (HideJapaneseTranslationAnnotations() &&
         IsJapaneseAnnotationSource(displayed_text, cinfo.comments[i].str)))
      cinfo.comments[i].str = JapanesePronunciationOnlyComment(cinfo.comments[i].str);

    // Persisted translations belong to the genuine word, not its spelling,
    // ordinal, cloud badge or current page. Apply them on the shared compact /
    // expanded serialization path. This is a memory-cache read only: ordinary
    // local candidates must not enqueue an unbounded network translation batch.
    // Japanese language identity overrides script detection, including words
    // containing only Han characters; their reading remains a separate tag.
    if (show_english_annotation || show_japanese_annotation) {
      _ApplyCloudTranslation(cinfo, u8tow(ctx.menu.candidates[i].text), i,
                             show_english_annotation, show_japanese_annotation,
                             escape_for_ipc, session_id);
    }

    // Wire-compatible presentation for both old and new client DLLs. Strip
    // the badge-only line after all cached annotations have been applied.
    // Actual Rime text and index-based candidate selection remain untouched.
    const bool source_cloud = ctx.menu.candidates[i].comment &&
        std::string(ctx.menu.candidates[i].comment).find(u8"☁") != std::string::npos;
    std::wstring clean_comment = escape_for_ipc
        ? unescape_string(cinfo.comments[i].str) : cinfo.comments[i].str;
    PresentJapanesePrefixHint(clean_comment,ctx.menu.candidates[i].comment ?
        u8tow(ctx.menu.candidates[i].comment) : L"");
    PreserveReservedSpanMetadata(clean_comment,ctx.menu.candidates[i].comment ?
        u8tow(ctx.menu.candidates[i].comment) : std::wstring());
    PresentInlineCloudBadge(displayed_text, clean_comment, source_cloud);
    cinfo.candies[i].str = escape_for_ipc ? escape_string(displayed_text) : displayed_text;
    cinfo.comments[i].str = escape_for_ipc ? escape_string(clean_comment) : clean_comment;

    if (RIME_STRUCT_HAS_MEMBER(ctx, ctx.select_labels) && ctx.select_labels) {
      const std::wstring label = u8tow(ctx.select_labels[i]);
      cinfo.labels[i].str = escape_for_ipc ? escape_string(label) : label;
    } else if (ctx.menu.select_keys) {
      const std::wstring label(1, ctx.menu.select_keys[i]);
      cinfo.labels[i].str = escape_for_ipc ? escape_string(label) : label;
    } else {
      cinfo.labels[i].str = std::to_wstring((i + 1) % 10);
    }
  }
  cinfo.highlighted = ctx.menu.highlighted_candidate_index;
  cinfo.currentPage = ctx.menu.page_no;
  cinfo.is_last_page = ctx.menu.is_last_page;
}

void RimeWithWeaselHandler::_GetExpandedCandidateInfo(
    CandidateInfo& cinfo, RimeContext& ctx, RimeSessionId session_id,
    SessionStatus& session_status, bool escape_for_ipc) {
  if (!session_status.expanded_candidates ||
      !RIME_API_AVAILABLE(rime_api, candidate_list_from_index)) {
    _GetCandidateInfo(cinfo, ctx, session_id, escape_for_ipc);
    return;
  }
  const char* raw_input = rime_api->get_input(session_id);
  const std::string input = raw_input ? raw_input : "";
  if (session_status.expanded_input != input) {
    session_status.expanded_input = input;
    session_status.expanded_window_start = 0;
    session_status.expanded_skipped_rows = 0;
    session_status.expanded_shift_history.clear();
  }
  constexpr size_t kWindowCandidates = 100;
  const size_t absolute_highlight =
      static_cast<size_t>(ctx.menu.page_no) * ctx.menu.page_size +
      ctx.menu.highlighted_candidate_index;
  if (absolute_highlight < session_status.expanded_window_start) {
    session_status.expanded_window_start = absolute_highlight;
    session_status.expanded_skipped_rows = 0;
    session_status.expanded_shift_history.clear();
  }
  while (absolute_highlight >= session_status.expanded_window_start +
                                 kWindowCandidates) {
    session_status.expanded_window_start += 6;
    ++session_status.expanded_skipped_rows;
  }
  RimeCandidateListIterator iterator{};
  if (!rime_api->candidate_list_from_index(
          session_id, &iterator,
          static_cast<int>(session_status.expanded_window_start))) {
    _GetCandidateInfo(cinfo, ctx, session_id, escape_for_ipc);
    return;
  }
  std::vector<std::string> texts, comments;
  texts.reserve(kWindowCandidates);
  comments.reserve(kWindowCandidates);
  while (texts.size() < kWindowCandidates &&
         rime_api->candidate_list_next(&iterator)) {
    texts.emplace_back(iterator.candidate.text ? iterator.candidate.text : "");
    comments.emplace_back(iterator.candidate.comment ?
                          iterator.candidate.comment : "");
  }
  // Probe one item beyond the window without keeping it in the UI payload.
  const bool has_more = texts.size() == kWindowCandidates &&
                        rime_api->candidate_list_next(&iterator);
  rime_api->candidate_list_end(&iterator);
  if (texts.empty()) {
    _GetCandidateInfo(cinfo, ctx, session_id, escape_for_ipc);
    return;
  }
  std::vector<RimeCandidate> candidates(texts.size());
  for (size_t i = 0; i < texts.size(); ++i) {
    candidates[i].text = const_cast<char*>(texts[i].c_str());
    candidates[i].comment = comments[i].empty() ? nullptr :
                            const_cast<char*>(comments[i].c_str());
    candidates[i].reserved = nullptr;
  }
  RimeContext virtual_ctx = ctx;
  virtual_ctx.menu.candidates = candidates.data();
  virtual_ctx.menu.num_candidates = static_cast<int>(candidates.size());
  virtual_ctx.menu.highlighted_candidate_index = static_cast<int>(
      absolute_highlight - session_status.expanded_window_start);
  virtual_ctx.menu.page_no = 0;
  virtual_ctx.menu.is_last_page = !has_more;
  virtual_ctx.menu.select_keys = nullptr;
  virtual_ctx.select_labels = nullptr;
  _GetCandidateInfo(cinfo, virtual_ctx, session_id, escape_for_ipc);
  // In expanded mode currentPage is the number of rows skipped before this
  // rolling window.  The compact panel still receives Rime's real page.
  cinfo.currentPage = session_status.expanded_skipped_rows;
}

void RimeWithWeaselHandler::StartMaintenance() {
  m_session_status_map.clear();
  Finalize();
  _UpdateUI(0);
}

void RimeWithWeaselHandler::EndMaintenance() {
  if (m_disabled) {
    Initialize();
    _UpdateUI(0);
  }
  m_session_status_map.clear();
}

void RimeWithWeaselHandler::SetOption(WeaselSessionId ipc_id,
                                      const std::string& opt,
                                      bool val) {
  // from no-session client, not actual typing session
  if (!ipc_id) {
    if (m_global_ascii_mode && opt == "ascii_mode") {
      for (auto& pair : m_session_status_map)
        rime_api->set_option(to_session_id(pair.first), "ascii_mode", val);
    } else {
      rime_api->set_option(to_session_id(m_active_session), opt.c_str(), val);
    }
  } else {
    rime_api->set_option(to_session_id(ipc_id), opt.c_str(), val);
  }
}

void RimeWithWeaselHandler::OnUpdateUI(std::function<void()> const& cb) {
  _UpdateUICallback = cb;
}

void RimeWithWeaselHandler::SetJapaneseReadingPreview(bool active,
                                                       WeaselSessionId ipc_id) {
  if (m_ui)
    m_ui->SetJapaneseReadingPreview(active);
}

void RimeWithWeaselHandler::OnSentenceTranslationReady(
    std::function<void()> const& cb) {
  m_sentence_translation_callback = cb;
}

void RimeWithWeaselHandler::RefreshActiveUI() {
  if (!m_active_session) return;
  auto found=m_session_status_map.find(m_active_session);
  if(found==m_session_status_map.end()) return;
  auto& session=found->second;
  _ApplyHotInputOptions(session.session_id,session.hot_input_generation,
                       session.hot_phrase_generation,session.hot_fuzzy_generation);
  _UpdateUI(m_active_session);
}


static bool HasHanOrKanaText(const std::wstring& text) {
  for (wchar_t ch : text) {
    if ((ch >= 0x3400 && ch <= 0x9fff) || (ch >= 0xf900 && ch <= 0xfaff) ||
        (ch >= 0x3040 && ch <= 0x30ff))
      return true;
  }
  return false;
}

static bool HasJapaneseKana(const std::wstring& text) {
  return std::any_of(text.begin(), text.end(), [](wchar_t ch) {
    return ch >= 0x3040 && ch <= 0x30ff;
  });
}

static void CloudTrace(const std::string& event, DWORD error = 0) {
  wchar_t enabled[2] = {};
  if (GetEnvironmentVariableW(L"ZHONGRI_CLOUD_TRACE", enabled, 2) != 1 ||
      enabled[0] != L'1') return;
  wchar_t temp_path[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temp_path)) return;
  std::ofstream stream(std::filesystem::path(temp_path) /
                       L"zhongri_cloud_trace.log", std::ios::app);
  stream << GetTickCount64() << ' ' << event << " error=" << error << '\n';
}

// Optional spelling-only supplement.  Neither HTTP nor debounce runs on the
// key-processing thread; failure leaves local Rime/Mozc candidates intact.
static std::string CloudUrlEncode(const std::string& spelling) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string encoded;
  for (unsigned char ch : spelling) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '-') {
      encoded += static_cast<char>(ch);
    } else {
      encoded += '%';
      encoded += digits[ch >> 4];
      encoded += digits[ch & 15];
    }
  }
  return encoded;
}

static std::filesystem::path CloudCandidateDirectory() {
  wchar_t local_app_data[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data,
                                                MAX_PATH);
  if (!length || length >= MAX_PATH) return {};
  return std::filesystem::path(local_app_data) / L"ZhongriInputMethod";
}

static std::vector<CloudCandidateResult> ReadCachedGoogleCandidate(
    const std::string& spelling, bool japanese) {
  const auto directory = CloudCandidateDirectory();
  if (directory.empty()) return {};
  const std::string prefix = std::string(japanese ? "ja\t" : "zh\t") +
                             spelling + '\t';
  for (const wchar_t* filename : {L"google_cloud_candidate_cache.tsv",
                                  L"google_cloud_candidate.tsv"}) {
    std::vector<CloudCandidateResult> results;
    std::ifstream stream(directory / filename, std::ios::binary);
    std::string line;
    for (size_t count = 0; count < 2560 && std::getline(stream, line); ++count) {
      if (line.rfind(prefix, 0) != 0) continue;
      // Old single-result entries are usable by Lua, but not complete batches.
      if (line.size() < 3 || line.substr(line.size() - 3) != "\t10") continue;
      const size_t tab = line.find('\t', prefix.size());
      if (tab == std::string::npos) continue;
      CloudCandidateResult result{line.substr(prefix.size(), tab - prefix.size()),
                                  line.substr(tab + 1, line.size() - tab - 4)};
      if (!result.text.empty()) results.push_back(result);
      if (results.size() == 10) break;
    }
    if (!results.empty()) return results;
  }
  return {};
}

static std::vector<CloudCandidateResult> RequestGoogleCandidate(const std::string& spelling,
                                                   bool japanese) {
  const std::string path_utf8 = "/request?text=" + CloudUrlEncode(spelling) +
      (japanese ? "&itc=ja-t-i0-und" : "&itc=zh-t-i0-pinyin") +
      "&num=10&ie=utf-8&oe=utf-8";
  const std::wstring path(path_utf8.begin(), path_utf8.end());
  HINTERNET session = WinHttpOpen(L"ZhongriCloudCandidate/0.1",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) { CloudTrace("open", GetLastError()); return {}; }
  WinHttpSetTimeouts(session, 300, 500, 800, 1600);
  HINTERNET connection = WinHttpConnect(session, L"inputtools.google.com",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET",
      path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
  bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS,
      0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr);
  CloudTrace(ok ? "received" : "request", ok ? 0 : GetLastError());
  std::string response;
  while (ok && response.size() < 32768) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available) || !available) break;
    available = min(available, static_cast<DWORD>(32768 - response.size()));
    const size_t old_size = response.size();
    response.resize(old_size + available);
    DWORD read = 0;
    if (!WinHttpReadData(request, response.data() + old_size, available, &read)) {
      response.clear();
      break;
    }
    response.resize(old_size + read);
  }
  if (request) WinHttpCloseHandle(request);
  if (connection) WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return ok ? ParseCloudCandidates(response, spelling) : std::vector<CloudCandidateResult>();
}

static bool PublishGoogleCandidate(const std::string& spelling,
                                   const std::vector<CloudCandidateResult>& candidates,
                                   bool japanese) {
  const auto directory = CloudCandidateDirectory();
  if (directory.empty()) return false;
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) { CloudTrace("directory", error.value()); return false; }
  const std::string mode = japanese ? "ja" : "zh";
  const std::string prefix = mode + '\t' + spelling + '\t';
  std::string entry;
  for (const auto& candidate : candidates)
    entry += prefix + candidate.text + '\t' + candidate.reading + "\t10\n";
  const auto cache = directory / L"google_cloud_candidate_cache.tsv";
  const auto cache_temp = directory / L"google_cloud_candidate_cache.tmp";
  {
    std::ifstream existing(cache, std::ios::binary);
    std::ofstream stream(cache_temp, std::ios::binary | std::ios::trunc);
    if (!stream) { CloudTrace("cache_open"); return false; }
    stream << entry;
    std::string line;
    size_t kept = candidates.size();
    while (kept < 2560 && std::getline(existing, line)) {
      if (line.rfind(prefix, 0) == 0 || line.size() > 1024) continue;
      stream << line << '\n';
      ++kept;
    }
    if (!stream.good()) { CloudTrace("cache_write"); return false; }
  }
  if (!MoveFileExW(cache_temp.c_str(), cache.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    CloudTrace("cache_move", GetLastError());
    return false;
  }
  const auto destination = directory / L"google_cloud_candidate.tsv";
  const auto temporary = directory / L"google_cloud_candidate.tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) { CloudTrace("temporary_open"); return false; }
    stream << entry;
    if (!stream.good()) { CloudTrace("temporary_write"); return false; }
  }
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return true;
    CloudTrace("move", GetLastError());
    Sleep(15);
  }
  return false;
}

void RimeWithWeaselHandler::_QueueCloudCandidate(const std::string& input,
                                                  const std::string& chinese,
                                                  const std::string& japanese,
                                                  bool prefer_japanese,
                                                  RimeSessionId session_id) {
  auto safe = [](const std::string& value) {
    return value.size() <= 384 && value.find_first_of("\t\r\n") == std::string::npos;
  };
  const std::string key = input.empty() || (chinese.empty() && japanese.empty()) ||
                         !safe(input) || !safe(chinese) || !safe(japanese) ? std::string() :
                         input + '\t' + chinese + '\t' + japanese + '\t' + (prefer_japanese ? "ja" : "zh");
  {
    std::lock_guard<std::mutex> lock(m_cloud_candidate_mutex);
    if (key == m_cloud_candidate_pending &&
        session_id == m_cloud_candidate_pending_session) return;
    m_cloud_candidate_pending = key;
    m_cloud_candidate_pending_session = session_id;
  }
  CloudTrace("queued", static_cast<DWORD>(input.size()));
  m_cloud_candidate_cv.notify_one();
}

void RimeWithWeaselHandler::_CloudCandidateWorker() {
  while (true) {
    std::string spelling;
    RimeSessionId request_session = 0;
    {
      std::unique_lock<std::mutex> lock(m_cloud_candidate_mutex);
      m_cloud_candidate_cv.wait(lock, [&] {
        return m_cloud_candidate_stopping ||
               (!m_cloud_candidate_pending.empty() &&
                (m_cloud_candidate_pending != m_cloud_candidate_published ||
                 m_cloud_candidate_pending_session !=
                     m_cloud_candidate_published_session));
      });
      if (m_cloud_candidate_stopping) return;
      spelling = m_cloud_candidate_pending;
      request_session = m_cloud_candidate_pending_session;
    }
    const size_t t1 = spelling.find('\t'), t2 = spelling.find('\t', t1 + 1),
                 t3 = spelling.find('\t', t2 + 1);
    if (t1 == std::string::npos || t2 == std::string::npos || t3 == std::string::npos) continue;
    const std::string raw = spelling.substr(0, t1);
    const std::string zh = spelling.substr(t1 + 1, t2 - t1 - 1);
    const std::string ja = spelling.substr(t2 + 1, t3 - t2 - 1);
    const bool prefer_japanese = spelling.substr(t3 + 1) == "ja";
    bool all_completed = true;
    for (int route = 0; route < 2; ++route) {
    const bool japanese = route == 0 ? prefer_japanese : !prefer_japanese;
    const std::string& input = japanese ? ja : zh;
    if (input.empty()) continue;
    std::vector<CloudCandidateResult> candidate = ReadCachedGoogleCandidate(input, japanese);
    if (candidate.empty()) {
      std::unique_lock<std::mutex> lock(m_cloud_candidate_mutex);
      if (m_cloud_candidate_cv.wait_for(lock, std::chrono::milliseconds(280),
          [&] { return m_cloud_candidate_stopping ||
                       m_cloud_candidate_pending != spelling ||
                       m_cloud_candidate_pending_session != request_session;
          })) { all_completed = false; break; }
      lock.unlock();
      candidate = RequestGoogleCandidate(input, japanese);
    }
    CloudTrace("completed", static_cast<DWORD>(candidate.size()));
    bool published = false;
    {
      std::lock_guard<std::mutex> lock(m_cloud_candidate_mutex);
      if (spelling == m_cloud_candidate_pending &&
          request_session == m_cloud_candidate_pending_session) {
        if (!candidate.empty())
          published = PublishGoogleCandidate(input, candidate, japanese);
        CloudTrace("published", published ? 1 : 0);
        if (published) {
          m_cloud_candidate_refresh = spelling;
          m_cloud_candidate_refresh_session = request_session;
        }
      }
    }
    // The TSF-owned candidate panel polls this generation while visible.
    // Do not touch librime or the active composition from the HTTP worker.
    if (published) {
      HKEY key = nullptr;
      if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                          0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                          nullptr) == ERROR_SUCCESS) {
        const DWORD generation = GetTickCount();
        // Reuse the already-installed TSF panel's 150 ms refresh channel.
        // The cloud result is a candidate-data change, just like a hot
        // annotation change; this avoids requiring a new TSF DLL deployment.
        RegSetValueExW(key, L"AnnotationSettingsGeneration", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&generation),
                       sizeof(generation));
        RegCloseKey(key);
      }
    } else {
      all_completed = false;
    }
    } // Both routes have their own cache and publish/refresh independently.
    if (all_completed) {
      std::lock_guard<std::mutex> lock(m_cloud_candidate_mutex);
      if (spelling == m_cloud_candidate_pending && request_session == m_cloud_candidate_pending_session) {
        m_cloud_candidate_published = spelling;
        m_cloud_candidate_published_session = request_session;
      }
    } else {
      // Retry failed routes; successful routes hit cache rather than HTTP.
      std::unique_lock<std::mutex> lock(m_cloud_candidate_mutex);
      m_cloud_candidate_cv.wait_for(lock, std::chrono::seconds(3), [&] {
        return m_cloud_candidate_stopping ||
               m_cloud_candidate_pending != spelling ||
               m_cloud_candidate_pending_session != request_session;
      });
    }
  }
}

static std::string ReadNiuApiKey() {
  constexpr wchar_t path[] = L"Software\\ZhongriInputMethod";
  constexpr wchar_t name[] = L"NiuApiKeyProtected";
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_BINARY,
                   nullptr, nullptr, &size) != ERROR_SUCCESS ||
      !size || size > 4096) return {};
  std::vector<BYTE> encrypted(size);
  if (RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_BINARY,
                   nullptr, encrypted.data(), &size) != ERROR_SUCCESS) return {};
  DATA_BLOB input{size, encrypted.data()};
  DATA_BLOB output{};
  if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                          0, &output)) return {};
  std::string key(reinterpret_cast<char*>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  if (key.empty() || key.size() > 512 ||
      key.find_first_of(" \t\r\n") != std::string::npos) return {};
  return key;
}

static std::string ReadNiuAppId() {
  wchar_t value[256] = {};
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                   L"NiuAppId", RRF_RT_REG_SZ, nullptr,
                   value, &size) != ERROR_SUCCESS) return {};
  const std::string app_id = wtou8(value);
  if (app_id.empty() || app_id.size() > 128 ||
      app_id.find_first_of(" \t\r\n") != std::string::npos) return {};
  return app_id;
}

static std::string NiuMd5Hex(const std::string& value) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM,
                                  nullptr, 0) != 0) return {};
  const auto created = BCryptCreateHash(algorithm, &hash, nullptr, 0,
                                        nullptr, 0, 0);
  const auto updated = created == 0 ? BCryptHashData(hash,
      reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
      static_cast<ULONG>(value.size()), 0) : created;
  BYTE digest[16] = {};
  const auto finished = updated == 0 ? BCryptFinishHash(hash, digest,
      sizeof(digest), 0) : updated;
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (finished != 0) return {};
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (BYTE byte : digest) {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}

static int JsonHexDigit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static std::wstring NiuTranslationFromJson(const std::string& response) {
  const auto field = response.find("\"tgtText\"");
  if (field == std::string::npos) return {};
  auto pos = response.find(':', field + sizeof("\"tgtText\"") - 1);
  if (pos == std::string::npos) return {};
  pos = response.find_first_not_of(" \t\r\n", pos + 1);
  if (pos == std::string::npos || response[pos++] != '"') return {};
  std::string decoded;
  while (pos < response.size() && decoded.size() < 8192) {
    const char ch = response[pos++];
    if (ch == '"') return u8tow(decoded);
    if (ch != '\\') { decoded.push_back(ch); continue; }
    if (pos >= response.size()) return {};
    const char escape = response[pos++];
    if (escape == 'u') {
      if (pos + 4 > response.size()) return {};
      unsigned code = 0;
      for (int i = 0; i < 4; ++i) {
        const int digit = JsonHexDigit(response[pos++]);
        if (digit < 0) return {};
        code = code * 16 + digit;
      }
      std::wstring unit(1, static_cast<wchar_t>(code));
      if (code >= 0xd800 && code <= 0xdbff &&
          response.compare(pos, 2, "\\u") == 0 &&
          pos + 6 <= response.size()) {
        unsigned low = 0;
        bool valid = true;
        for (int i = 2; i < 6; ++i) {
          const int digit = JsonHexDigit(response[pos + i]);
          if (digit < 0) { valid = false; break; }
          low = low * 16 + digit;
        }
        if (valid && low >= 0xdc00 && low <= 0xdfff) {
          unit.push_back(static_cast<wchar_t>(low));
          pos += 6;
        }
      }
      decoded += wtou8(unit);
    } else if (escape == 'n') decoded.push_back('\n');
    else if (escape == 'r') decoded.push_back('\r');
    else if (escape == 't') decoded.push_back('\t');
    else if (escape == '"' || escape == '\\' || escape == '/')
      decoded.push_back(escape);
    else return {};
  }
  return {};
}

static std::wstring RequestNiuTranslationOnline(const std::wstring& source,
                                          const std::string& language,
                                          const std::string& source_language = "") {
  const std::string from = source_language.empty() ?
      (HasJapaneseKana(source) ? "ja" : "zh") : source_language;
  const std::string text = wtou8(source);
  std::string key = ReadNiuApiKey();
  const std::string app_id = ReadNiuAppId();
  if (key.empty() || app_id.empty() || source.empty() ||
      source.size() > 500) return {};
  const std::string timestamp = std::to_string(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
  std::string auth_parameters = "apikey=" + key + "&appId=" + app_id +
      "&from=" + from + "&srcText=" + text + "&timestamp=" + timestamp +
      "&to=" + language;
  const std::string auth = NiuMd5Hex(auth_parameters);
  const std::string body = "from=" + from + "&to=" + language +
      "&appId=" + CloudUrlEncode(app_id) + "&timestamp=" + timestamp +
      "&srcText=" + CloudUrlEncode(text) + "&authStr=" + auth;
  SecureZeroMemory(key.data(), key.size());
  SecureZeroMemory(auth_parameters.data(), auth_parameters.size());
  if (auth.empty()) return {};
  HINTERNET session = WinHttpOpen(L"ZhongriNiuTrans/1.0",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return {};
  WinHttpSetTimeouts(session, 500, 500, 1000, 3500);
  HINTERNET connection = WinHttpConnect(session, L"api.niutrans.com",
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST",
      L"/v2/text/translate", nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
  const wchar_t* headers =
      L"Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n";
  bool ok = request && WinHttpSendRequest(request, headers, (DWORD)-1L,
      (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
      WinHttpReceiveResponse(request, nullptr);
  DWORD status = 0, status_size = sizeof(status);
  ok = ok && WinHttpQueryHeaders(request,
      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
      WINHTTP_NO_HEADER_INDEX) && status == 200;
  std::string response;
  while (ok && response.size() < 65536) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available) || !available) break;
    available = min(available, static_cast<DWORD>(65536 - response.size()));
    const size_t old_size = response.size();
    response.resize(old_size + available);
    DWORD read = 0;
    if (!WinHttpReadData(request, response.data() + old_size, available, &read)) {
      response.clear(); break;
    }
    response.resize(old_size + read);
  }
  if (request) WinHttpCloseHandle(request);
  if (connection) WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  if (!ok) return {};
  const auto translated = NiuTranslationFromJson(response);
  return translated;
}

static TranslationDiskCache* NiuDiskCache() {
  const auto directory = CloudCandidateDirectory();
  if (directory.empty()) return nullptr;
  static TranslationDiskCache disk_cache(directory / L"niutrans_translations_v2.tsv");
  return &disk_cache;
}

static std::wstring CachedNiuTranslation(const std::wstring& source,
                                         const std::string& language,
                                         const std::string& source_language = "") {
  auto cache = NiuDiskCache();
  return cache ? u8tow(cache->Get(source_language.empty() ?
                                (HasJapaneseKana(source) ? "ja" : "zh") : source_language,
                                language, wtou8(source))) : std::wstring();
}

static std::wstring RequestNiuTranslation(const std::wstring& source,
                                        const std::string& language,
                                        const std::string& source_language = "") {
  auto cache = NiuDiskCache();
  if (!cache) return RequestNiuTranslationOnline(source, language, source_language);
  return u8tow(cache->Resolve(source_language.empty() ?
      (HasJapaneseKana(source) ? "ja" : "zh") : source_language, language, wtou8(source), [&] {
    CandidateExpandTrace("NIU_ONLINE_REQUEST chars=" + std::to_string(source.size()) + " to=" + language);
    return wtou8(RequestNiuTranslationOnline(source, language, source_language));
  }));
}

void RimeWithWeaselHandler::_QueueSentenceTranslation(
    const std::wstring& source) {
  if (!HasHanOrKanaText(source) || source.size() < 2 || source.size() > 500)
    return;
  if (!CachedNiuTranslation(source, HasJapaneseKana(source) ? "zh" : "ja").empty() &&
      !CachedNiuTranslation(source, "en").empty()) return;
  {
    std::lock_guard<std::mutex> lock(m_sentence_translation_mutex);
    if (source == m_sentence_translation_pending ||
        source == m_sentence_translation_source)
      return;
    m_sentence_translation_pending = source;
  }
  CandidateExpandTrace("TRANSLATION_QUEUE chars=" +
                       std::to_string(source.size()));
  m_sentence_translation_cv.notify_one();
}

void RimeWithWeaselHandler::_ApplySentenceTranslation(
    CandidateInfo& candidates, const std::wstring& source,
    bool escape_for_ipc) {
  if (!ShowSingleCharacterAnnotations() && IsSingleAnnotationCharacter(source)) return;
  std::wstring translation = CachedNiuTranslation(source, HasJapaneseKana(source) ? "zh" : "ja");
  const auto english = CachedNiuTranslation(source, "en");
  if (!english.empty()) {
    if (!translation.empty()) translation += L"\n";
    translation += english;
  }
  if (translation.empty()) {
    std::lock_guard<std::mutex> lock(m_sentence_translation_mutex);
    if (source == m_sentence_translation_source) translation = m_sentence_translation_lines;
  }
  if (!translation.empty() && !candidates.comments.empty()) {
    // A candidate annotation is the same surface used by dictionary and
    // cloud translations.  The TSF panel can refresh it without altering the
    // composition or creating an auxiliary-text layout during a timer event.
    const auto match = std::find_if(candidates.candies.begin(), candidates.candies.end(),
        [&](const Text& candidate) { return candidate.str ==
            (escape_for_ipc ? escape_string(source) : source); });
    if (match == candidates.candies.end()) return;
    const size_t index = static_cast<size_t>(match - candidates.candies.begin());
    if (index >= candidates.comments.size()) return;
    std::wstring& comment = candidates.comments[index].str;
    if (HideJapaneseTranslationAnnotations() && IsJapaneseAnnotationSource(source, comment)) return;
    SetNiuTranslationAnnotation(comment,
        escape_for_ipc ? escape_string(translation) : translation,
        PreferNiuAnnotations(), escape_for_ipc);
  }
}

void RimeWithWeaselHandler::_SentenceTranslationWorker() {
  const auto refresh_panel = [] {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) == ERROR_SUCCESS) {
      const DWORD generation = GetTickCount();
      RegSetValueExW(key, L"AnnotationSettingsGeneration", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&generation),
                     sizeof(generation));
      RegCloseKey(key);
    }
  };
  while (true) {
    std::wstring source;
    {
      std::unique_lock<std::mutex> lock(m_sentence_translation_mutex);
      m_sentence_translation_cv.wait(lock, [&] {
        return m_sentence_translation_stopping ||
               (!m_sentence_translation_pending.empty() &&
                m_sentence_translation_pending != m_sentence_translation_source);
      });
      if (m_sentence_translation_stopping)
        return;
      source = m_sentence_translation_pending;
      if (m_sentence_translation_cv.wait_for(lock, std::chrono::milliseconds(150),
          [&] { return m_sentence_translation_stopping ||
                       m_sentence_translation_pending != source; })) continue;
    }
    std::wstring result;
    const auto request_started = std::chrono::steady_clock::now();
    const auto publish = [&](const std::wstring& value) {
      if (value.empty()) return;
      bool current = false;
      {
        std::lock_guard<std::mutex> lock(m_sentence_translation_mutex);
        if (source == m_sentence_translation_pending) {
          m_sentence_translation_source = source;
          m_sentence_translation_lines = value;
          current = true;
        }
      }
      if (current) {
        CandidateExpandTrace("TRANSLATION_READY chars=" +
                             std::to_string(source.size()) + " ms=" +
                             std::to_string(std::chrono::duration_cast<
                                 std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() -
                                 request_started).count()));
        refresh_panel();
      }
    };
    {
      const std::string primary_language =
          HasJapaneseKana(source) ? "zh" : "ja";
      std::mutex result_mutex;
      std::condition_variable result_cv;
      std::wstring english, primary;
      unsigned completed = 0;
      const auto request_language = [&](std::wstring& target,
                                        const std::string& language) {
        std::wstring text = RequestNiuTranslation(source, language);
        if (text.size() > 100) text = text.substr(0, 99) + L"…";
        {
          std::lock_guard<std::mutex> lock(result_mutex);
          target = std::move(text);
          ++completed;
        }
        result_cv.notify_one();
      };
      std::thread english_request([&] { request_language(english, "en"); });
      std::thread primary_request([&] {
        request_language(primary, primary_language);
      });
      std::wstring first_english, first_primary;
      {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait(lock, [&] { return completed >= 1; });
        first_english = english;
        first_primary = primary;
      }
      result = first_primary;
      if (!first_english.empty()) {
        if (!result.empty()) result += L"\n";
        result += first_english;
      }
      publish(result);
      english_request.join();
      primary_request.join();
      result = primary;
      if (!english.empty()) {
        if (!result.empty()) result += L"\n";
        result += english;
      }
      std::wstring first_result = first_primary;
      if (!first_english.empty()) {
        if (!first_result.empty()) first_result += L"\n";
        first_result += first_english;
      }
      if (result != first_result) publish(result);
    }
  }
}

void RimeWithWeaselHandler::_QueueCloudTranslation(
    const std::wstring& text, bool japanese) {
  if (!HasHanOrKanaText(text) || text.empty() || text.size() > 500) return;
  japanese = japanese || HasJapaneseKana(text);
  const AnnotationSource source(text, japanese);
  const std::string from = japanese ? "ja" : "zh";
  if (!CachedNiuTranslation(text, "en", from).empty() &&
      !CachedNiuTranslation(text, japanese ? "zh" : "ja", from).empty()) return;
  {
    std::lock_guard<std::mutex> lock(m_cloud_translation_mutex);
    if (source == m_cloud_translation_pending) return;
    const auto queued = std::find(m_cloud_translation_queue.begin(), m_cloud_translation_queue.end(), source);
    if (queued != m_cloud_translation_queue.end()) m_cloud_translation_queue.erase(queued);
    // Visible candidate requests are a bounded batch, not one overwritten
    // pending string. Otherwise every refresh starves the second candidate.
    if (m_cloud_translation_queue.size() >= 64) m_cloud_translation_queue.pop_back();
    m_cloud_translation_queue.push_front(source);
  }
  m_cloud_translation_cv.notify_one();
}

void RimeWithWeaselHandler::_ApplyCloudTranslation(
    CandidateInfo& candidates, const std::wstring& source, size_t index,
    bool show_english, bool show_japanese, bool escape_for_ipc,
    RimeSessionId session_id) {
  if (index >= candidates.comments.size()) return;
  if (!ShowSingleCharacterAnnotations() && IsSingleAnnotationCharacter(source)) return;
  const bool source_japanese = HasJapaneseKana(source) ||
      candidates.comments[index].str.find(L"[[RIME_LANG:JA]]") != std::wstring::npos;
  if (source_japanese && HideJapaneseTranslationAnnotations()) return;
  const std::string from = source_japanese ? "ja" : "zh";
  auto english = CachedNiuTranslation(source, "en", from);
  auto japanese = CachedNiuTranslation(source, source_japanese ? "zh" : "ja", from);
  {
    std::lock_guard<std::mutex> lock(m_cloud_translation_mutex);
    if (AnnotationSource(source, source_japanese) == m_cloud_translation_source) {
      if (english.empty()) english = m_cloud_translation_english;
      if (japanese.empty()) japanese = m_cloud_translation_japanese;
    }
  }
  std::wstring lines;
  if (show_english) lines = english;
  if (show_japanese && !japanese.empty()) {
    if (!lines.empty()) lines += L"\n";
    lines += japanese;
  }
  if (lines.empty()) return;
  std::wstring& comment = candidates.comments[index].str;
  std::wstring translated_reading;
  if (!source_japanese && show_japanese && !japanese.empty()) {
    rime_api->set_property(session_id, "annotation_reading_result", "");
    // Clear the request first: repeated identical surfaces must also resolve
    // after the result was cleared. No option toggles or input refresh here.
    rime_api->set_property(session_id, "annotation_reading_surface", "");
    rime_api->set_property(session_id, "annotation_reading_surface", wtou8(japanese).c_str());
    char reading[4096] = {};
    if (rime_api->get_property(session_id, "annotation_reading_result", reading, sizeof(reading)))
      translated_reading = u8tow(reading);
  }
  SetNiuTranslationAnnotation(comment,
      escape_for_ipc ? escape_string(lines) : lines,
      PreferNiuAnnotations(), escape_for_ipc, translated_reading);
}

void RimeWithWeaselHandler::_CloudTranslationWorker() {
  using TranslationPair = std::pair<std::wstring, std::wstring>;
  const auto refresh_panel = [] {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) == ERROR_SUCCESS) {
      const DWORD generation = GetTickCount();
      RegSetValueExW(key, L"AnnotationSettingsGeneration", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&generation),
                     sizeof(generation));
      RegCloseKey(key);
    }
  };
  while (true) {
    AnnotationSource source;
    {
      std::unique_lock<std::mutex> lock(m_cloud_translation_mutex);
      m_cloud_translation_cv.wait(lock, [&] {
        return m_cloud_translation_stopping ||
            !m_cloud_translation_queue.empty();
      });
      if (m_cloud_translation_stopping) return;
      source = m_cloud_translation_queue.front();
      m_cloud_translation_queue.pop_front();
      m_cloud_translation_pending = source;
    }
    TranslationPair result;
    const auto request_started = std::chrono::steady_clock::now();
    const auto publish = [&](const TranslationPair& value) {
      if (value.first.empty() && value.second.empty()) return;
      bool current = false;
      {
        std::lock_guard<std::mutex> lock(m_cloud_translation_mutex);
        if (source == m_cloud_translation_pending) {
          m_cloud_translation_source = source;
          m_cloud_translation_english = value.first;
          m_cloud_translation_japanese = value.second;
          current = true;
        }
      }
      if (current) {
        CandidateExpandTrace("CLOUD_TRANSLATION_READY chars=" +
                             std::to_string(source.first.size()) + " ms=" +
                             std::to_string(std::chrono::duration_cast<
                                 std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() -
                                 request_started).count()));
        refresh_panel();
      }
    };
    {
      std::mutex result_mutex;
      std::condition_variable result_cv;
      unsigned completed = 0;
      const auto request_language = [&](size_t index, const char* language) {
        std::wstring text = RequestNiuTranslation(source.first, language,
                                                 source.second ? "ja" : "zh");
        if (text.size() > 100) text = text.substr(0, 99) + L"…";
        {
          std::lock_guard<std::mutex> lock(result_mutex);
          if (index == 0) result.first = std::move(text);
          else result.second = std::move(text);
          ++completed;
        }
        result_cv.notify_one();
      };
      std::thread english_request([&] { request_language(0, "en"); });
      std::thread japanese_request([&] { request_language(1, source.second ? "zh" : "ja"); });
      TranslationPair first_result;
      {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait(lock, [&] { return completed >= 1; });
        first_result = result;
      }
      publish(first_result);
      english_request.join();
      japanese_request.join();
      if (result != first_result) publish(result);
    }
  }
}

bool RimeWithWeaselHandler::_IsDeployerRunning() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"ZhongriDeployerMutex");
  bool deployer_detected = hMutex && GetLastError() == ERROR_ALREADY_EXISTS;
  if (hMutex) {
    CloseHandle(hMutex);
  }
  return deployer_detected;
}

#include <JapaneseHoldComposition.h>

void RimeWithWeaselHandler::_DispatchLongPress(WeaselSessionId ipc_id,
                                            const JapaneseLongPressPending& pending) {
  auto& state=get_session_status(ipc_id);
  const auto session=state.session_id;
  JapaneseDispatchLongPress(pending.definition,[&](const std::string& output) {
    if(!rime_api->commit_composition(session))rime_api->clear_composition(session);
    state.literal_key_commit+=output;
    state.physical_preedit.clear();state.physical_preedit_valid=false;
    state.hold_labels.clear();
  },[&](const std::string& spelling) {
    const char* before=rime_api->get_input(session);
    const std::string original=before?before:"";
    if(original.empty()){state.physical_preedit.clear();state.hold_labels.clear();}
    else if(!state.physical_preedit_valid)state.physical_preedit=original;
    const size_t start=state.physical_preedit.size();
    const auto started=GetTickCount64();
    const bool whole_unit=JapaneseAppendHoldComposition(rime_api,session,spelling);
    LOG(INFO)<<"[hold-compose] key="<<pending.keycode<<" whole_unit="<<whole_unit
        <<" input_keys="<<spelling.size()<<" engine_ms="<<(GetTickCount64()-started);
    const char* after=rime_api->get_input(session);
    // Register only a completed input insertion, never a candidate's spelling.
    if(after && std::string(after)==original+spelling) {
      state.physical_preedit+=spelling;state.physical_preedit_valid=true;
      state.hold_labels.push_back({state.physical_preedit,start,
        static_cast<char>(pending.keycode)});
    } else {state.hold_labels.clear();state.physical_preedit=after?after:"";state.physical_preedit_valid=true;}
    rime_api->get_status(session,&state.status);
    rime_api->free_status(&state.status);
  });
}

void RimeWithWeaselHandler::PollLongPress() {
  if(m_disabled || !m_active_session)return;
  const auto found=m_session_status_map.find(m_active_session);
  if(found==m_session_status_map.end())return;
  auto& state=found->second;
  const auto pending=state.long_press;
  if(!pending.definition.compose || !pending.IsLong(GetTickCount64()))return;
  if(!rime_api->get_option(state.session_id,"japanese_input_table_enabled") ||
      rime_api->get_option(state.session_id,"ascii_mode")) {state.long_press={};return;}
  state.long_press={};
  state.long_press_released_later.insert(JapanesePhysicalKey(pending.keycode));
  _DispatchLongPress(m_active_session,pending);
  _UpdateUI(m_active_session);
}

void RimeWithWeaselHandler::GetTrayFeatures(bool& learning, bool& ascii_punct) {
  DWORD value = 0, bytes = sizeof(value);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod", L"Option_japanese_learning_mode",
               RRF_RT_REG_DWORD, nullptr, &value, &bytes);
  learning = value != 0;
  value=0; bytes=sizeof(value);
  RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",L"TrayAsciiPunct",RRF_RT_REG_DWORD,nullptr,&value,&bytes);
  ascii_punct=value!=0;
}

void RimeWithWeaselHandler::_UpdateUI(WeaselSessionId ipc_id) {
  // if m_ui nullptr, _UpdateUI meaningless
  if (!m_ui)
    return;

  Status& weasel_status = m_ui->status();
  Context weasel_context;

  RimeSessionId session_id = to_session_id(ipc_id);

  if (ipc_id == 0)
    weasel_status.disabled = m_disabled;

  _GetStatus(weasel_status, ipc_id, weasel_context);
  SessionStatus& session_status = get_session_status(ipc_id);
  _GetContext(weasel_context, session_id, session_status);

  const UIStyle previous_ui_style = m_ui->style();
  m_ui->style() = session_status.style;
  CandidateExpandTrace("UpdateUI expanded=" +
                       std::to_string(session_status.expanded_candidates) +
                       " base_layout=" +
                       std::to_string(session_status.style.layout_type) +
                       " ipc=" + std::to_string(ipc_id) + " app=" +
                       session_status.client_app + " maxh=" +
                       std::to_string(session_status.style.max_height) +
                       " minw=" +
                       std::to_string(session_status.style.min_width));
  LOG(INFO) << "[candidate-expand] UpdateUI expanded="
            << session_status.expanded_candidates
            << " base_layout=" << session_status.style.layout_type;
  if (session_status.expanded_candidates) {
    m_ui->style().layout_type = UIStyle::LAYOUT_HORIZONTAL;
    // Preserve the compact width cutoff in the negative expansion marker.
    // HorizontalLayout reuses it for row zero; lower rows have their own
    // six-column width. -1 represents an unbounded compact row.
    m_ui->style().max_height = -max(1, session_status.style.max_height);
    CandidateExpandTrace("FORCED_HORIZONTAL_GRID");
    LOG(INFO) << "[candidate-expand] forcing vertical layout";
  }
  const bool ui_style_changed =
      previous_ui_style.layout_type != m_ui->style().layout_type ||
      previous_ui_style.max_height != m_ui->style().max_height ||
      previous_ui_style.min_width != m_ui->style().min_width ||
      previous_ui_style.comment_font_point !=
          m_ui->style().comment_font_point;
  if (rime_api->get_option(session_id, "inline_preedit"))
    session_status.style.client_caps |= INLINE_PREEDIT_CAPABLE;
  else
    session_status.style.client_caps &= ~INLINE_PREEDIT_CAPABLE;

  if (session_status.tsf_client) {
    // TSF hosts own their candidate surface. Keeping the server window alive
    // causes an intermittent second popup at the desktop's top-left corner.
    if (m_ui->IsShown()) m_ui->Hide();
  } else if (!_ShowMessage(weasel_context, weasel_status)) {
    if (!session_status.expanded_candidates) {
      CandidateExpandTrace("UI_PHASE HIDE_BEGIN");
      m_ui->Hide();
      CandidateExpandTrace("UI_PHASE HIDE_END");
    }
    CandidateExpandTrace("UI_PHASE UPDATE_BEGIN");
    m_ui->Update(weasel_context, weasel_status);
    CandidateExpandTrace("UI_PHASE UPDATE_END");
    // UI::Update intentionally skips identical context/status.  Expansion is
    // a style-only transition, so without an explicit refresh the state says
    // expanded while the window remains the old 600x118 single-row layout.
    if (ui_style_changed) {
      CandidateExpandTrace("UI_PHASE STYLE_REFRESH");
      m_ui->Refresh();
    }
  }
  // A layout-only transition keeps the same Rime context/status.  Force the
  // panel to rebuild its Layout object even if UI::Update considers the data
  // unchanged.
  // Expanded candidates are owned by the TSF-side surface for every host.
  // That surface has the live client callback required for mouse selection;
  // the server surface can draw the grid but cannot deliver an asynchronous
  // selection result back into the application's active TSF composition.
  if (session_status.expanded_candidates) {
    CandidateExpandTrace("SERVER_EXPANDED_WINDOW_SUPPRESSED_FOR_TSF_CLIENT");
    if (m_ui->IsShown())
      m_ui->Hide();
  }

  _RefreshTrayIcon(session_id, _UpdateUICallback);

  {
    std::lock_guard<std::mutex> lock(m_notifier_mutex);
    m_message_type.clear();
    m_message_value.clear();
    m_message_label.clear();
    m_option_name.clear();
  }
}

void RimeWithWeaselHandler::_LoadSchemaSpecificSettings(
    WeaselSessionId ipc_id,
    const std::string& schema_id) {
  if (!m_ui)
    return;
  RimeConfig config;
  if (!rime_api->schema_open(schema_id.c_str(), &config))
    return;
  _UpdateShowNotifications(&config);
  m_ui->style() = m_base_style;
  _UpdateUIStyle(&config, m_ui, false);
  SessionStatus& session_status = get_session_status(ipc_id);
  session_status.style = m_ui->style();
  UIStyle& style = session_status.style;
  // load schema color style config
  const int BUF_SIZE = 255;
  char buffer[BUF_SIZE + 1] = {0};
  const auto update_color_scheme = [&]() {
    std::string color_name(buffer);
    RimeConfigIterator preset = {0};
    if (rime_api->config_begin_map(
            &preset, &config, ("preset_color_schemes/" + color_name).c_str())) {
      _UpdateUIStyleColor(&config, style, color_name);
      rime_api->config_end(&preset);
    } else {
      RimeConfig weaselconfig;
      if (rime_api->config_open("weasel", &weaselconfig)) {
        _UpdateUIStyleColor(&weaselconfig, style, color_name);
        rime_api->config_close(&weaselconfig);
      }
    }
  };
  const char* key =
      m_current_dark_mode ? "style/color_scheme_dark" : "style/color_scheme";
  if (rime_api->config_get_string(&config, key, buffer, BUF_SIZE))
    update_color_scheme();
  // load schema icon start
  {
    const auto load_icon = [](RimeConfig& config, const char* key1,
                              const char* key2) {
      const auto user_dir = WeaselUserDataPath();
      const auto shared_dir = WeaselSharedDataPath();
      const int BUF_SIZE = 255;
      char buffer[BUF_SIZE + 1] = {0};
      if (rime_api->config_get_string(&config, key1, buffer, BUF_SIZE) ||
          (key2 != NULL &&
           rime_api->config_get_string(&config, key2, buffer, BUF_SIZE))) {
        auto resource = u8tow(buffer);
        if (fs::is_regular_file(user_dir / resource))
          return (user_dir / resource).wstring();
        else if (fs::is_regular_file(shared_dir / resource))
          return (shared_dir / resource).wstring();
      }
      return std::wstring();
    };
    style.current_zhung_icon =
        load_icon(config, "schema/icon", "schema/zhung_icon");
    style.current_ascii_icon = load_icon(config, "schema/ascii_icon", NULL);
    style.current_full_icon = load_icon(config, "schema/full_icon", NULL);
    style.current_half_icon = load_icon(config, "schema/half_icon", NULL);
  }
  // load schema icon end
  rime_api->config_close(&config);
  _ApplyHotAppearance(session_status, true);
}

void RimeWithWeaselHandler::_LoadAppInlinePreeditSet(WeaselSessionId ipc_id,
                                                     bool ignore_app_name) {
  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  static char _app_name[50];
  rime_api->get_property(session_id, "client_app", _app_name,
                         sizeof(_app_name) - 1);
  std::string app_name(_app_name);
  if (!ignore_app_name && m_last_app_name == app_name)
    return;
  m_last_app_name = app_name;
  bool inline_preedit = session_status.style.inline_preedit;
  bool found = false;
  if (!app_name.empty()) {
    auto it = m_app_options.find(app_name);
    if (it != m_app_options.end()) {
      AppOptions& options(m_app_options[it->first]);
      for (const auto& pair : options) {
        if (pair.first == "inline_preedit") {
          rime_api->set_option(session_id, pair.first.c_str(),
                               Bool(pair.second));
          session_status.style.inline_preedit = Bool(pair.second);
          found = true;
          break;
        }
      }
    }
  }
  if (!found) {
    session_status.style.inline_preedit = m_base_style.inline_preedit;
    // load from schema.
    RIME_STRUCT(RimeStatus, status);
    if (rime_api->get_status(session_id, &status)) {
      std::string schema_id = status.schema_id;
      RimeConfig config;
      if (rime_api->schema_open(schema_id.c_str(), &config)) {
        Bool value = False;
        if (rime_api->config_get_bool(&config, "style/inline_preedit",
                                      &value)) {
          session_status.style.inline_preedit = value;
        }
        rime_api->config_close(&config);
      }
      rime_api->free_status(&status);
    }
  }
  if (session_status.style.inline_preedit != inline_preedit)
    _UpdateInlinePreeditStatus(ipc_id);
}

bool RimeWithWeaselHandler::_ShowMessage(Context& ctx, Status& status) {
  std::lock_guard<std::mutex> lock(m_notifier_mutex);
  if (m_message_type.empty() || m_message_value.empty())
    return m_ui->IsCountingDown();
  // show as auxiliary string
  std::wstring& tips(ctx.aux.str);
  bool show_icon = false;
  if (m_message_type == "deploy") {
    if (m_message_value == "start")
      if (GetThreadUILanguage() == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US))
        tips = L"Deploying RIME";
      else
        tips = L"正在部署 RIME";
    else if (m_message_value == "success")
      if (GetThreadUILanguage() == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US))
        tips = L"Deployed";
      else
        tips = L"部署完成";
    else if (m_message_value == "failure") {
      if (GetThreadUILanguage() ==
          MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL))
        tips = L"有錯誤，請查看日誌 %TEMP%\\rime.zhongri\\rime.zhongri.*.INFO";
      else if (GetThreadUILanguage() ==
               MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
        tips = L"有错误，请查看日志 %TEMP%\\rime.zhongri\\rime.zhongri.*.INFO";
      else
        tips =
            L"There is an error, please check the logs "
            L"%TEMP%\\rime.zhongri\\rime.zhongri.*.INFO";
    }
  } else if (m_message_type == "schema") {
    tips = /*L"【" + */ status.schema_name /* + L"】"*/;
  } else if (m_message_type == "option") {
    status.type = SCHEMA;
    if (m_message_value == "!ascii_mode") {
      show_icon = true;
    } else if (m_message_value == "ascii_mode") {
      show_icon = true;
    } else
      tips = u8tow(m_message_label);

    if (m_message_value == "full_shape" || m_message_value == "!full_shape")
      status.type = FULL_SHAPE;
  }
  auto counter = m_ui->IsCountingDown();
  if (!show_icon && counter)
    return counter;
  auto foption = m_show_notifications.find(m_option_name);
  auto falways = m_show_notifications.find("always");
  if ((!add_session && (foption != m_show_notifications.end() ||
                        falways != m_show_notifications.end())) ||
      m_message_type == "deploy") {
    m_ui->Update(ctx, status);
    if (m_show_notifications_time)
      m_ui->ShowWithTimeout(m_show_notifications_time);
    return true;
  } else {
    return m_ui->IsCountingDown();
  }
}
inline std::string _GetLabelText(const std::vector<Text>& labels,
                                 int id,
                                 const wchar_t* format) {
  wchar_t buffer[128];
  swprintf_s<128>(buffer, format, labels.at(id).str.c_str());
  return wtou8(std::wstring(buffer));
}

bool RimeWithWeaselHandler::_Respond(WeaselSessionId ipc_id, EatLine eat) {
  if (get_session_status(ipc_id).defer_key_response) return true;
  std::wstring body;
  body.reserve(4096);
  std::vector<const char*> actions;
  actions.reserve(8);

  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  RIME_STRUCT(RimeCommit, commit);
  std::string committed;
  if (rime_api->get_commit(session_id, &commit)) {
    committed = commit.text;
    rime_api->free_commit(&commit);
    char surface[8192] = {}, reading[8192] = {};
    if (rime_api->get_option(session_id, "japanese_learning_mode") &&
        rime_api->get_property(session_id, "japanese_learning_commit_text", surface, sizeof(surface)) && *surface &&
        rime_api->get_property(session_id, "japanese_learning_commit_reading", reading, sizeof(reading)) && *reading &&
        committed.compare(0, std::strlen(surface), surface) == 0) {
      char annotated[32768] = {};
      rime_api->get_property(session_id, "japanese_learning_commit_annotated", annotated, sizeof(annotated));
      FormatJapaneseLearningCommit(committed, true, surface, reading, annotated,
          rime_api->get_option(session_id, "japanese_learning_kanji"),
          rime_api->get_option(session_id, "japanese_learning_space"),
          rime_api->get_option(session_id, "japanese_learning_newline"));
    }
    rime_api->set_property(session_id, "japanese_learning_commit_text", "");
    rime_api->set_property(session_id, "japanese_learning_commit_reading", "");
    rime_api->set_property(session_id, "japanese_learning_commit_annotated", "");
  }
  committed += session_status.literal_key_commit;
  session_status.literal_key_commit.clear();
  if (!committed.empty()) {
    actions.push_back("commit");
    body.append(L"commit=").append(escape_string(u8tow(committed))).append(L"\n");
  }

  bool is_composing = false;
  RIME_STRUCT(RimeStatus, status);
  static const std::wstring Bool_wstring[] = {L"0", L"1"};
  if (rime_api->get_status(session_id, &status)) {
    is_composing = !!status.is_composing;
    if (!is_composing && session_status.expanded_candidates) {
      session_status.expanded_candidates = false;
      session_status.__synced = false;
      CandidateExpandTrace("RESET_AFTER_COMPOSITION");
    }
    actions.push_back("status");
    // Read-only runtime evidence for hot learning options. Older clients ignore
    // unknown status fields; no text, key events or application actions here.
    for (const auto* option : {"japanese_learning_mode", "japanese_learning_kanji",
                              "japanese_learning_space", "japanese_learning_newline"}) {
      body.append(L"status.").append(u8tow(option)).append(L"=")
          .append(rime_api->get_option(session_id, option) ? L"1\n" : L"0\n");
    }
    body.append(L"status.hold_pending=").append(
        session_status.long_press.keycode && session_status.long_press.definition.compose ? L"1\n" : L"0\n");
    body.append(L"status.ascii_mode=")
        .append(Bool_wstring[!!status.is_ascii_mode])
        .append(L"\n")
        .append(L"status.composing=")
        .append(Bool_wstring[!!status.is_composing])
        .append(L"\n")
        .append(L"status.disabled=")
        .append(Bool_wstring[!!status.is_disabled])
        .append(L"\n")
        .append(L"status.full_shape=")
        .append(Bool_wstring[!!status.is_full_shape])
        .append(L"\n")
        .append(L"status.schema_id=")
        .append(status.schema_id ? u8tow(status.schema_id) : std::wstring())
        .append(L"\n");
    if (m_global_ascii_mode &&
        (session_status.status.is_ascii_mode != status.is_ascii_mode)) {
      for (auto& pair : m_session_status_map) {
        if (pair.first != ipc_id)
          rime_api->set_option(to_session_id(pair.first), "ascii_mode",
                               !!status.is_ascii_mode);
      }
    }
    session_status.status = status;
    rime_api->free_status(&status);
  }

  RIME_STRUCT(RimeContext, ctx);
  if (rime_api->get_context(session_id, &ctx)) {
    bool has_candidates = ctx.menu.num_candidates > 0;
    CandidateInfo cinfo;
    std::vector<std::pair<int, std::wstring>> cloud_sources;
    const bool cloud_en = CloudTranslationOption(
        session_id, "show_english_annotation", L"ShowEnglishAnnotation");
    const bool cloud_ja = CloudTranslationOption(
        session_id, "show_japanese_annotation", L"ShowJapaneseAnnotation");
    if (has_candidates) {
      _GetExpandedCandidateInfo(cinfo, ctx, session_id, session_status, true);
      if (cloud_en || cloud_ja) {
        for (int index : CloudCandidateIndices(ctx,
            rime_api->get_option(session_id, "sentence_translation"),
            session_status.expanded_candidates ? 64 : 12)) {
          const auto source = u8tow(ctx.menu.candidates[index].text);
          cloud_sources.emplace_back(index, source);
          const char* comment = ctx.menu.candidates[index].comment;
          _QueueCloudTranslation(source, comment && std::strstr(comment, "[[RIME_LANG:JA]]"));
        }
      }
    }
    if (is_composing) {
      const auto& preedit = ctx.composition.preedit;
      const auto& start = ctx.composition.sel_start;
      const auto& end = ctx.composition.sel_end;
      const auto& cursor = ctx.composition.cursor_pos;
      static const auto u8towstring = [](const char* u8str, int len = 0) {
        return std::to_wstring(utf8towcslen(u8str, len));
      };
      actions.push_back("ctx");
      // Per-candidate translation is queued above. commit_text_preview follows
      // the highlight, so it must never be attached to candidate zero.
      session_status.PruneHoldLabels();
      char editing_flag[4]={};
      const bool editing=rime_api->get_property(session_id,"japanese_segment_active",editing_flag,sizeof(editing_flag)) && editing_flag[0]=='1';
      if(editing) {
        const auto display=u8tow(preedit);
        body.append(L"ctx.preedit=").append(escape_string(display)).append(L"\n")
            .append(L"ctx.preedit.cursor=0,").append(std::to_wstring(display.size()))
            .append(L",").append(std::to_wstring(display.size())).append(L"\n");
      } else if(start==0 && !session_status.hold_labels.empty() &&
          rime_api->get_option(session_id,"japanese_input_table_show_letters")) {
        const auto display=u8tow(session_status.DisplayHoldLabels(session_status.physical_preedit));
        body.append(L"ctx.preedit=").append(escape_string(display)).append(L"\n")
            .append(L"ctx.preedit.cursor=0,").append(std::to_wstring(display.size()))
            .append(L",").append(std::to_wstring(display.size())).append(L"\n");
      } else switch (session_status.style.preedit_type) {
        case UIStyle::PREVIEW: {
          // Inline raw mode belongs to the application input field.  A fuzzy
          // candidate's commit preview may use its normalized spelling
          // (paypay -> baypay), but that must never replace the physical keys
          // already shown in the host application.
          if (session_status.style.inline_preedit && start == 0 &&
              session_status.physical_preedit_valid &&
              !session_status.physical_preedit.empty()) {
            const std::wstring physical =
                u8tow(session_status.physical_preedit);
            body.append(L"ctx.preedit=")
                .append(escape_string(physical))
                .append(L"\n")
                .append(L"ctx.preedit.cursor=0,")
                .append(std::to_wstring(physical.size()))
                .append(L",")
                .append(std::to_wstring(physical.size()))
                .append(L"\n");
            break;
          }
          if (ctx.commit_text_preview) {
            const char* first_utf8 = ctx.commit_text_preview;
            const size_t first_len = std::strlen(first_utf8);
            const std::wstring first_w = escape_string(u8tow(first_utf8));
            const std::wstring tmp = u8towstring(first_utf8, (int)first_len);
            body.append(L"ctx.preedit=")
                .append(first_w)
                .append(L"\n")
                .append(L"ctx.preedit.cursor=")
                .append(u8towstring(first_utf8, 0))
                .append(L",")
                .append(tmp)
                .append(L",")
                .append(tmp)
                .append(L"\n");
            break;
          }
          // no preview, fall back to composition
        }
        case UIStyle::COMPOSITION: {
          // The server routes a physical '-' through the internal q spelling
          // so Mozc receives the long-vowel mark.  The
          // candidate header should still show the key actually pressed.
          // Keep converted prefixes intact after a partial selection.
          if (!session_status.style.inline_preedit && start == 0 &&
              session_status.physical_preedit_valid &&
              session_status.physical_preedit.find('-') != std::string::npos &&
              !session_status.physical_preedit.empty()) {
            const std::wstring physical =
                u8tow(session_status.physical_preedit);
            body.append(L"ctx.preedit=")
                .append(escape_string(physical))
                .append(L"\n")
                .append(L"ctx.preedit.cursor=0,")
                .append(std::to_wstring(physical.size()))
                .append(L",")
                .append(std::to_wstring(physical.size()))
                .append(L"\n");
            break;
          }
          // Preserve Rime's converted prefix after partial candidate
          // selection, while removing only schema-inserted syllable
          // separators from the still-unconverted input.  Reading get_input()
          // here loses segmentation state and incorrectly keeps the complete
          // original key sequence after the user has selected the first part.
          if (session_status.style.inline_preedit && preedit && *preedit) {
            const auto compact = [](std::wstring value) {
              value.erase(std::remove_if(value.begin(), value.end(),
                                         [](wchar_t ch) {
                                           return ch == L' ' || ch == L'\'';
                                         }),
                          value.end());
              return value;
            };
            // Before any segment has been selected, a fuzzy translator may
            // expose its matched spelling as `composition.preedit` (for
            // example p<->b can turn the displayed `paypay` into `baypay`).
            // The inline raw-input mode must never let candidate spelling
            // rewrite what the user actually typed.  Once sel_start moves
            // past zero, however, the preedit contains a converted prefix
            // produced by partial selection, which must be preserved.
            const bool has_converted_prefix = start > 0;
            const char* raw_input = rime_api->get_input(session_id);
            const bool has_physical_preedit =
                !has_converted_prefix &&
                session_status.physical_preedit_valid &&
                !session_status.physical_preedit.empty();
            const std::wstring display_preedit = has_physical_preedit
                ? u8tow(session_status.physical_preedit)
                : ((!has_converted_prefix && raw_input && *raw_input)
                       ? u8tow(raw_input)
                       : u8tow(preedit));
            const std::wstring compact_preedit = compact(display_preedit);
            const auto compact_offset = [&](size_t byte_offset) {
              return compact(u8tow(std::string(preedit, byte_offset))).size();
            };
            const size_t compact_start =
                has_converted_prefix ? compact_offset(start) : 0;
            const size_t compact_end = has_converted_prefix
                                           ? compact_offset(end)
                                           : compact_preedit.size();
            const size_t compact_cursor = has_converted_prefix
                                              ? compact_offset(cursor)
                                              : compact_preedit.size();
            body.append(L"ctx.preedit=")
                .append(escape_string(compact_preedit))
                .append(L"\n")
                .append(L"ctx.preedit.cursor=")
                .append(std::to_wstring(compact_start))
                .append(L",")
                .append(std::to_wstring(compact_end))
                .append(L",")
                .append(std::to_wstring(compact_cursor))
                .append(L"\n");
            break;
          }
          body.append(L"ctx.preedit=")
              .append(escape_string(u8tow(preedit)))
              .append(L"\n");
          if (start <= end) {
            body.append(L"ctx.preedit.cursor=")
                .append(u8towstring(preedit, start))
                .append(L",")
                .append(u8towstring(preedit, end))
                .append(L",")
                .append(u8towstring(preedit, cursor))
                .append(L"\n");
          }
          break;
        }
        case UIStyle::PREVIEW_ALL: {
          body.append(L"ctx.preedit=")
              .append(escape_string(u8tow(preedit)))
              .append(L"  [");
          auto label_valid = session_status.style.label_font_point > 0;
          auto comment_valid = session_status.style.comment_font_point > 0;
          const std::wstring mark_text_w =
              session_status.style.mark_text.empty()
                  ? std::wstring(L"*")
                  : session_status.style.mark_text;
          for (auto i = 0; i < ctx.menu.num_candidates; i++) {
            std::wstring label_w;
            if (label_valid) {
              wchar_t buf_lbl[128];
              swprintf_s<128>(buf_lbl,
                              session_status.style.label_text_format.c_str(),
                              cinfo.labels.at(i).str.c_str());
              label_w = std::wstring(buf_lbl);
            }
            std::wstring comment_w =
                comment_valid ? cinfo.comments.at(i).str : std::wstring();
            std::wstring prefix_w = (i != ctx.menu.highlighted_candidate_index)
                                        ? std::wstring()
                                        : mark_text_w;
            body.append(L" ")
                .append(prefix_w)
                .append(escape_string(label_w))
                .append(escape_string(u8tow(ctx.menu.candidates[i].text)))
                .append(L" ")
                .append(escape_string(comment_w));
          }
          body.append(L" ]\n");
          if (start <= end) {
            body.append(L"ctx.preedit.cursor=")
                .append(u8towstring(preedit, start))
                .append(L",")
                .append(u8towstring(preedit, end))
                .append(L",")
                .append(u8towstring(preedit, cursor))
                .append(L"\n");
          }
          break;
        }
      }
    }
    if (has_candidates) {
      for (const auto& cloud : cloud_sources) {
      const int cloud_index = cloud.first;
      const auto& cloud_source = cloud.second;
      const size_t cloud_absolute_index =
          static_cast<size_t>(ctx.menu.page_no) * ctx.menu.page_size +
          static_cast<size_t>(max(0, cloud_index));
      const size_t cloud_visible_index = session_status.expanded_candidates ?
          cloud_absolute_index - session_status.expanded_window_start :
          static_cast<size_t>(max(0, cloud_index));
      if (cloud_index >= 0 && !cloud_source.empty() &&
          (!session_status.expanded_candidates ||
           cloud_absolute_index >= session_status.expanded_window_start))
        _ApplyCloudTranslation(cinfo, cloud_source, cloud_visible_index,
                               cloud_en, cloud_ja, true, session_id);
      }
      std::wstringstream ss;
      boost::archive::text_woarchive oa(ss);

      oa << cinfo;

      auto s = ss.str();
      body.append(L"ctx.cand=").append(std::move(s)).append(L"\n");
    }
    rime_api->free_context(&ctx);
  }

  // configuration information
  actions.push_back("config");
  body.append(L"config.inline_preedit=")
      .append(std::to_wstring((int)session_status.style.inline_preedit))
      .append(L"\n");

  // style
  if (!session_status.__synced) {
    std::wstringstream ss;
    boost::archive::text_woarchive oa(ss);
    UIStyle response_style = session_status.style;
    if (session_status.expanded_candidates) {
      response_style.layout_type = UIStyle::LAYOUT_HORIZONTAL;
      response_style.max_height = -max(1, session_status.style.max_height);
    }
    CandidateExpandTrace("RESPOND_STYLE layout=" +
                         std::to_string(response_style.layout_type) +
                         " expanded=" +
                         std::to_string(session_status.expanded_candidates));
    oa << response_style;

    actions.push_back("style");
    body.append(L"style=").append(ss.str()).append(L"\n");
    session_status.__synced = true;
  }

  // summarize: send header first to avoid vector head-insert cost
  std::wstring header;
  if (actions.empty()) {
    header = L"action=noop\n";
  } else {
    std::string actionList;
    actionList.reserve(64);
    for (size_t i = 0; i < actions.size(); ++i) {
      if (i > 0)
        actionList += ',';
      actionList += actions[i];
    }
    header = std::wstring(L"action=") + u8tow(actionList) + L"\n";
  }
  if (!eat(header))
    return false;

  body.append(L".\n");
  if (!eat(body))
    return false;

  return true;
}

// Blend foreground and background ARGB colors taking alpha into account.
// Returns an ABGR COLORREF with premultiplied alpha blended result.
static inline COLORREF blend_colors(COLORREF fcolor, COLORREF bcolor) {
  // Extract ARGB channels from both colors.
  BYTE fA = (fcolor >> 24) & 0xFF;
  BYTE fB = (fcolor >> 16) & 0xFF;
  BYTE fG = (fcolor >> 8) & 0xFF;
  BYTE fR = fcolor & 0xFF;
  BYTE bA = (bcolor >> 24) & 0xFF;
  BYTE bB = (bcolor >> 16) & 0xFF;
  BYTE bG = (bcolor >> 8) & 0xFF;
  BYTE bR = bcolor & 0xFF;
  // Convert alpha to [0,1]
  float fAlpha = fA / 255.0f;
  float bAlpha = bA / 255.0f;
  // Result alpha
  float retAlpha = fAlpha + (1 - fAlpha) * bAlpha;
  if (retAlpha <= 1e-6f) {
    // Fully transparent result — return background unchanged as fallback.
    return bcolor;
  }
  auto mix = [&](float fc, float bc) -> BYTE {
    return static_cast<BYTE>((fc * fAlpha + bc * bAlpha * (1 - fAlpha)) /
                             retAlpha);
  };
  BYTE retR = mix(fR, bR);
  BYTE retG = mix(fG, bG);
  BYTE retB = mix(fB, bB);
  BYTE outA = static_cast<BYTE>(retAlpha * 255.0f);
  return (static_cast<COLORREF>(outA) << 24) | (retB << 16) | (retG << 8) |
         retR;
}
// parse color value, with fallback value
static Bool _RimeGetColor(RimeConfig* config,
                          const std::string& key,
                          int& value,
                          const ColorFormat& fmt,
                          const unsigned int& fallback) {
  char color[256] = {0};
  if (!rime_api->config_get_string(config, key.c_str(), color, 256)) {
    value = fallback;
    return False;
  }
  const auto color_str = std::string(color);
  // adjudge if str is 0x 0X # hex color format, return trimmed hex part
  // out part is 6 or 8 length hex string without white space
  const auto parse_color_code = [](const std::string& str, std::string& out) {
    if (str.empty())
      return false;
    size_t start = 0;
    if (str[0] == '#') {
      start = 1;
    } else if (str.size() >= 2 &&
               (str.compare(0, 2, "0x") == 0 || str.compare(0, 2, "0X") == 0)) {
      start = 2;
    } else {
      return false;
    }
    const std::string hex_part = str.substr(start);
    if (hex_part.empty())
      return false;
    if ((start == 1 || start == 2) && hex_part.length() != 3 &&
        hex_part.length() != 4 && hex_part.length() != 6 &&
        hex_part.length() != 8) {
      return false;
    }
    for (char c : hex_part) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F')))
        return false;
    }
    out = str.substr(start).substr(0, 8);
#define _2C(c) std::string(2, c)
    if (out.size() == 3)
      out = _2C(out[0]) + _2C(out[1]) + _2C(out[2]);
    else if (out.size() == 4)
      out = _2C(out[0]) + _2C(out[1]) + _2C(out[2]) + _2C(out[3]);
#undef _2C
    return true;
  };
  auto hex_color = std::string();
  if (parse_color_code(color_str, hex_color)) {
    value = std::stoul(hex_color, 0, 16);
    if (hex_color.length() == 6)
      value = (fmt != COLOR_RGBA) ? (value | 0xff000000)
                                  : (((unsigned int)value << 8) | 0x000000ff);
  } else {
    if (!rime_api->config_get_int(config, key.c_str(), &value)) {
      value = fallback;
      return False;
    }
    if (value <= 0xffffff)
      value = (fmt != COLOR_RGBA) ? (value | 0xff000000)
                                  : (((unsigned int)value << 8) | 0x000000ff);
    else if (value > 0xffffffff)
      value &= 0xffffffff;
  }
  if (fmt == COLOR_ARGB)
    value = ARGB2ABGR(value);
  else if (fmt == COLOR_RGBA)
    value = RGBA2ABGR(value);
  value &= 0xffffffff;
  return True;
}

template <typename T, size_t N>
using Array = std::array<std::pair<const char*, T>, N>;

// parset bool type configuration to T type value trueValue / falseValue
template <typename T>
void _RimeGetBool(RimeConfig* config,
                  const char* key,
                  bool cond,
                  T& value,
                  const T& trueValue = true,
                  const T& falseValue = false) {
  Bool tempb = False;
  if (rime_api->config_get_bool(config, key, &tempb) || cond)
    value = (!!tempb) ? trueValue : falseValue;
}
// parse string option to T type value, with fallback
template <typename T, size_t N>
void _RimeParseStringOptWithFallback(RimeConfig* config,
                                     const char* key,
                                     T& value,
                                     const Array<T, N>& arr,
                                     const T& fallback) {
  char str_buff[256] = {0};
  if (rime_api->config_get_string(config, key, str_buff, 255)) {
    for (size_t i = 0; i < N; ++i) {
      if (strcmp(arr[i].first, str_buff) == 0) {
        value = arr[i].second;
        return;
      }
    }
  }
  value = fallback;
}

template <typename T>
void _RimeGetIntStr(RimeConfig* config,
                    const char* key,
                    T& value,
                    const char* fb_key = nullptr,
                    const void* fb_value = nullptr,
                    const std::function<void(T&)>& func = nullptr) {
  if constexpr (std::is_same<T, int>::value) {
    if (!rime_api->config_get_int(config, key, &value) && fb_key != 0)
      rime_api->config_get_int(config, fb_key, &value);
  } else if constexpr (std::is_same<T, std::wstring>::value) {
    const int BUF_SIZE = 2047;
    char buffer[BUF_SIZE + 1] = {0};
    if (rime_api->config_get_string(config, key, buffer, BUF_SIZE) ||
        rime_api->config_get_string(config, fb_key, buffer, BUF_SIZE)) {
      value = u8tow(buffer);
    } else if (fb_value) {
      value = *(T*)fb_value;
    }
  }
  if (func)
    func(value);
}

// Helper to iterate a Rime map and invoke callback with key/path
static void ForEachRimeMap(
    RimeConfig* config,
    const std::string& path,
    const std::function<void(const char* key, const char* child_path)>& cb) {
  RimeConfigIterator iter;
  if (!rime_api->config_begin_map(&iter, config, path.c_str()))
    return;
  while (rime_api->config_next(&iter)) {
    cb(iter.key, iter.path);
  }
  rime_api->config_end(&iter);
}

// Helper to iterate a Rime list and invoke callback with item path
static void ForEachRimeList(
    RimeConfig* config,
    const std::string& path,
    const std::function<void(const char* item_path)>& cb) {
  RimeConfigIterator iter;
  if (!rime_api->config_begin_list(&iter, config, path.c_str()))
    return;
  while (rime_api->config_next(&iter)) {
    cb(iter.path);
  }
  rime_api->config_end(&iter);
}

void RimeWithWeaselHandler::_UpdateShowNotifications(RimeConfig* config,
                                                     bool initialize) {
  Bool show_notifications = true;
  if (initialize)
    m_show_notifications_base.clear();
  m_show_notifications.clear();

  if (rime_api->config_get_bool(config, "show_notifications",
                                &show_notifications)) {
    // config read as bool, for global all on or off
    if (show_notifications)
      m_show_notifications["always"] = true;
    if (initialize)
      m_show_notifications_base = m_show_notifications;
  } else {
    // read as list using helper
    ForEachRimeList(config, "show_notifications", [&](const char* item_path) {
      char buffer[256] = {0};
      if (rime_api->config_get_string(config, item_path, buffer, 256))
        m_show_notifications[std::string(buffer)] = true;
    });
    if (initialize)
      m_show_notifications_base = m_show_notifications;
    if (m_show_notifications.empty()) {
      // not configured, or incorrect type
      if (initialize)
        m_show_notifications_base["always"] = true;
      m_show_notifications = m_show_notifications_base;
    }
  }
}

// update ui's style parameters, ui has been check before referenced
static void _UpdateUIStyle(RimeConfig* config, UI* ui, bool initialize) {
  UIStyle& style(ui->style());
  const std::function<void(std::wstring&)> rmspace = [](std::wstring& str) {
    str = std::regex_replace(str, std::wregex(L"\\s*(,|:|^|$)\\s*"), L"$1");
  };
  const std::function<void(int&)> _abs = [](int& value) { value = abs(value); };
  // get font faces
  _RimeGetIntStr(config, "style/font_face", style.font_face, 0, 0, rmspace);
  std::wstring* const pFallbackFontFace = initialize ? &style.font_face : NULL;
  _RimeGetIntStr(config, "style/label_font_face", style.label_font_face, 0,
                 pFallbackFontFace, rmspace);
  _RimeGetIntStr(config, "style/comment_font_face", style.comment_font_face, 0,
                 pFallbackFontFace, rmspace);
  // able to set label font/comment font empty, force fallback to font face.
  if (style.label_font_face.empty())
    style.label_font_face = style.font_face;
  if (style.comment_font_face.empty())
    style.comment_font_face = style.font_face;
  // get font points
  _RimeGetIntStr(config, "style/font_point", style.font_point);
  if (style.font_point <= 0)
    style.font_point = 12;
  _RimeGetIntStr(config, "style/label_font_point", style.label_font_point,
                 "style/font_point", 0, _abs);
  _RimeGetIntStr(config, "style/comment_font_point", style.comment_font_point,
                 "style/font_point", 0, _abs);
  _RimeGetIntStr(config, "style/candidate_abbreviate_length",
                 style.candidate_abbreviate_length, 0, 0, _abs);
  _RimeGetBool(config, "style/inline_preedit", initialize,
               style.inline_preedit);
  _RimeGetBool(config, "style/inline_preedit_raw_input", initialize,
               style.inline_preedit_raw_input);
  _RimeGetBool(config, "style/vertical_auto_reverse", initialize,
               style.vertical_auto_reverse);
  static constexpr Array<UIStyle::PreeditType, 3> _preeditArr = {
      {{"composition", UIStyle::COMPOSITION},
       {"preview", UIStyle::PREVIEW},
       {"preview_all", UIStyle::PREVIEW_ALL}}};
  _RimeParseStringOptWithFallback(config, "style/preedit_type",
                                  style.preedit_type, _preeditArr,
                                  style.preedit_type);
  static constexpr Array<UIStyle::AntiAliasMode, 5> _aliasModeArr = {
      {{"force_dword", UIStyle::FORCE_DWORD},
       {"cleartype", UIStyle::CLEARTYPE},
       {"grayscale", UIStyle::GRAYSCALE},
       {"aliased", UIStyle::ALIASED},
       {"default", UIStyle::DEFAULT}}};
  _RimeParseStringOptWithFallback(config, "style/antialias_mode",
                                  style.antialias_mode, _aliasModeArr,
                                  style.antialias_mode);
  static constexpr Array<UIStyle::HoverType, 3> _hoverTypeArr = {
      {{"none", UIStyle::HoverType::NONE},
       {"semi_hilite", UIStyle::HoverType::SEMI_HILITE},
       {"hilite", UIStyle::HoverType::HILITE}}};
  _RimeParseStringOptWithFallback(config, "style/hover_type", style.hover_type,
                                  _hoverTypeArr, style.hover_type);
  static constexpr Array<UIStyle::LayoutAlignType, 3> _alignType = {
      {{"top", UIStyle::ALIGN_TOP},
       {"center", UIStyle::ALIGN_CENTER},
       {"bottom", UIStyle::ALIGN_BOTTOM}}};
  _RimeParseStringOptWithFallback(config, "style/layout/align_type",
                                  style.align_type, _alignType,
                                  style.align_type);
  _RimeGetBool(config, "style/display_tray_icon", initialize,
               style.display_tray_icon);
  _RimeGetBool(config, "style/ascii_tip_follow_cursor", initialize,
               style.ascii_tip_follow_cursor);
  _RimeGetBool(config, "style/horizontal", initialize, style.layout_type,
               UIStyle::LAYOUT_HORIZONTAL, UIStyle::LAYOUT_VERTICAL);
  _RimeGetBool(config, "style/paging_on_scroll", initialize,
               style.paging_on_scroll);
  _RimeGetBool(config, "style/click_to_capture", initialize,
               style.click_to_capture, true, false);
  _RimeGetBool(config, "style/fullscreen", false, style.layout_type,
               ((style.layout_type == UIStyle::LAYOUT_HORIZONTAL)
                    ? UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN
                    : UIStyle::LAYOUT_VERTICAL_FULLSCREEN),
               style.layout_type);
  _RimeGetBool(config, "style/vertical_text", false, style.layout_type,
               UIStyle::LAYOUT_VERTICAL_TEXT, style.layout_type);
  _RimeGetBool(config, "style/vertical_text_left_to_right", false,
               style.vertical_text_left_to_right);
  _RimeGetBool(config, "style/vertical_text_with_wrap", false,
               style.vertical_text_with_wrap);
  // Keep the historical behavior when the key is absent.  Passing false as
  // the condition preserves UIStyle's true default instead of forcing false.
  _RimeGetBool(config, "style/expanded_comment_width", false,
               style.expanded_comment_width);
  _RimeGetBool(config, "style/expanded_comment_align_label", false,
               style.expanded_comment_align_label);
  _RimeGetBool(config, "style/hover_comment_scroll", false,
               style.hover_comment_scroll);
  static constexpr Array<bool, 2> _text_orientation = {
      {{"horizontal", false}, {"vertical", true}}};
  bool _text_orientation_bool = false;
  _RimeParseStringOptWithFallback(config, "style/text_orientation",
                                  _text_orientation_bool, _text_orientation,
                                  _text_orientation_bool);
  if (_text_orientation_bool)
    style.layout_type = UIStyle::LAYOUT_VERTICAL_TEXT;
  _RimeGetIntStr(config, "style/label_format", style.label_text_format);
  _RimeGetIntStr(config, "style/mark_text", style.mark_text);
  _RimeGetIntStr(config, "style/layout/baseline", style.baseline, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/linespacing", style.linespacing, 0, 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/min_width", style.min_width, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/max_width", style.max_width, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/min_height", style.min_height, 0, 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/max_height", style.max_height, 0, 0,
                 _abs);
  // layout (alternative to style/horizontal)
  static constexpr Array<UIStyle::LayoutType, 5> _layoutArr = {
      {{"vertical", UIStyle::LAYOUT_VERTICAL},
       {"horizontal", UIStyle::LAYOUT_HORIZONTAL},
       {"vertical_text", UIStyle::LAYOUT_VERTICAL_TEXT},
       {"vertical+fullscreen", UIStyle::LAYOUT_VERTICAL_FULLSCREEN},
       {"horizontal+fullscreen", UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN}}};
  _RimeParseStringOptWithFallback(config, "style/layout/type",
                                  style.layout_type, _layoutArr,
                                  style.layout_type);
  // disable max_width when full screen
  if (style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN ||
      style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN) {
    style.max_width = 0;
    style.inline_preedit = false;
  }
  _RimeGetIntStr(config, "style/layout/border", style.border,
                 "style/layout/border_width", 0, _abs);
  _RimeGetIntStr(config, "style/layout/margin_x", style.margin_x);
  _RimeGetIntStr(config, "style/layout/margin_y", style.margin_y);
  _RimeGetIntStr(config, "style/layout/spacing", style.spacing, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/candidate_spacing",
                 style.candidate_spacing, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/hilite_spacing", style.hilite_spacing, 0,
                 0, _abs);
  _RimeGetIntStr(config, "style/layout/hilite_padding_x",
                 style.hilite_padding_x, "style/layout/hilite_padding", 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/hilite_padding_y",
                 style.hilite_padding_y, "style/layout/hilite_padding", 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/shadow_radius", style.shadow_radius, 0,
                 0, _abs);
  // disable shadow for fullscreen layout
  style.shadow_radius *=
      (!(style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN ||
         style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN));
  _RimeGetIntStr(config, "style/layout/shadow_offset_x", style.shadow_offset_x);
  _RimeGetIntStr(config, "style/layout/shadow_offset_y", style.shadow_offset_y);
  // round_corner as alias of hilited_corner_radius
  _RimeGetIntStr(config, "style/layout/hilited_corner_radius",
                 style.round_corner, "style/layout/round_corner", 0, _abs);
  // corner_radius not set, fallback to round_corner
  _RimeGetIntStr(config, "style/layout/corner_radius", style.round_corner_ex,
                 "style/layout/round_corner", 0, _abs);
  // fix padding and spacing settings
  if (style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT) {
    // hilite_padding vs spacing
    // if hilite_padding over spacing, increase spacing
    style.spacing = max(style.spacing, style.hilite_padding_y * 2);
    // hilite_padding vs candidate_spacing
    if (style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN ||
        style.layout_type == UIStyle::LAYOUT_VERTICAL) {
      // vertical, if hilite_padding_y over candidate spacing,
      // increase candidate spacing
      style.candidate_spacing =
          max(style.candidate_spacing, style.hilite_padding_y * 2);
    } else {
      // horizontal, if hilite_padding_x over candidate
      // spacing, increase candidate spacing
      style.candidate_spacing =
          max(style.candidate_spacing, style.hilite_padding_x * 2);
    }
    // hilite_padding_x vs hilite_spacing
    if (!style.inline_preedit)
      style.hilite_spacing = max(style.hilite_spacing, style.hilite_padding_x);
  } else  // LAYOUT_VERTICAL_TEXT
  {
    // hilite_padding_x vs spacing
    // if hilite_padding over spacing, increase spacing
    style.spacing = max(style.spacing, style.hilite_padding_x * 2);
    // hilite_padding vs candidate_spacing
    // if hilite_padding_x over candidate
    // spacing, increase candidate spacing
    style.candidate_spacing =
        max(style.candidate_spacing, style.hilite_padding_x * 2);
    // vertical_text_with_wrap and hilite_padding_y over candidate_spacing
    if (style.vertical_text_with_wrap)
      style.candidate_spacing =
          max(style.candidate_spacing, style.hilite_padding_y * 2);
    // hilite_padding_y vs hilite_spacing
    if (!style.inline_preedit)
      style.hilite_spacing = max(style.hilite_spacing, style.hilite_padding_y);
  }
  // fix padding and margin settings
  int scale = style.margin_x < 0 ? -1 : 1;
  style.margin_x = scale * max(style.hilite_padding_x, abs(style.margin_x));
  scale = style.margin_y < 0 ? -1 : 1;
  style.margin_y = scale * max(style.hilite_padding_y, abs(style.margin_y));
  // get enhanced_position
  _RimeGetBool(config, "style/enhanced_position", initialize,
               style.enhanced_position, true, false);
  // get color scheme
  const int BUF_SIZE = 255;
  char buffer[BUF_SIZE + 1] = {0};
  if (initialize && rime_api->config_get_string(config, "style/color_scheme",
                                                buffer, BUF_SIZE))
    _UpdateUIStyleColor(config, style);
}
// load color configs to style, by "style/color_scheme" or specific scheme name
// "color" which is default empty
static bool _UpdateUIStyleColor(RimeConfig* config,
                                UIStyle& style,
                                const std::string& color) {
  const int BUF_SIZE = 255;
  char buffer[BUF_SIZE + 1] = {0};
  std::string color_mark = "style/color_scheme";
  // color scheme
  if (rime_api->config_get_string(config, color_mark.c_str(), buffer,
                                  BUF_SIZE) ||
      !color.empty()) {
    std::string prefix("preset_color_schemes/");
    prefix += (color.empty()) ? buffer : color;
    // define color format, default abgr if not set
    ColorFormat fmt = COLOR_ABGR;
    static constexpr Array<ColorFormat, 3> _colorFmt = {
        {{"argb", COLOR_ARGB}, {"rgba", COLOR_RGBA}, {"abgr", COLOR_ABGR}}};
    _RimeParseStringOptWithFallback(config, (prefix + "/color_format").c_str(),
                                    fmt, _colorFmt, COLOR_ABGR);
#define COLOR(key, value, fallback) \
  _RimeGetColor(config, (prefix + "/" + key), value, fmt, fallback)
    COLOR("back_color", style.back_color, 0xffffffff);
    COLOR("shadow_color", style.shadow_color, 0);
    COLOR("prevpage_color", style.prevpage_color, 0);
    COLOR("nextpage_color", style.nextpage_color, 0);
    COLOR("text_color", style.text_color, 0xff000000);
    COLOR("candidate_text_color", style.candidate_text_color, style.text_color);
    COLOR("candidate_back_color", style.candidate_back_color, 0);
    // Language colors are user-level preferences shared by the normal, dark
    // and per-application schemes.  A secondary scheme that does not define
    // them must inherit the values already loaded from the base scheme rather
    // than resetting them to transparent.
    COLOR("chinese_candidate_text_color", style.chinese_candidate_text_color,
          style.chinese_candidate_text_color);
    COLOR("chinese_candidate_back_color", style.chinese_candidate_back_color,
          style.chinese_candidate_back_color);
    COLOR("japanese_candidate_text_color", style.japanese_candidate_text_color,
          style.japanese_candidate_text_color);
    COLOR("japanese_candidate_back_color", style.japanese_candidate_back_color,
          style.japanese_candidate_back_color);
    COLOR("common_phrase_candidate_text_color",
          style.common_phrase_candidate_text_color,
          style.common_phrase_candidate_text_color);
    COLOR("common_phrase_candidate_back_color",
          style.common_phrase_candidate_back_color,
          style.common_phrase_candidate_back_color);
    COLOR("border_color", style.border_color, style.text_color);
    COLOR("hilited_text_color", style.hilited_text_color, style.text_color);
    COLOR("hilited_back_color", style.hilited_back_color, style.back_color);
    COLOR("hilited_candidate_text_color", style.hilited_candidate_text_color,
          style.hilited_text_color);
    COLOR("hilited_candidate_back_color", style.hilited_candidate_back_color,
          style.hilited_back_color);
    COLOR("hilited_candidate_shadow_color",
          style.hilited_candidate_shadow_color, 0);
    COLOR("hilited_shadow_color", style.hilited_shadow_color, 0);
    COLOR("candidate_shadow_color", style.candidate_shadow_color, 0);
    COLOR("candidate_border_color", style.candidate_border_color, 0);
    COLOR("hilited_candidate_border_color",
          style.hilited_candidate_border_color, 0);
    COLOR("label_color", style.label_text_color,
          blend_colors(style.candidate_text_color, style.candidate_back_color));
    COLOR("hilited_label_color", style.hilited_label_text_color,
          blend_colors(style.hilited_candidate_text_color,
                       style.hilited_candidate_back_color));
    COLOR("comment_text_color", style.comment_text_color,
          style.label_text_color);
    COLOR("hilited_comment_text_color", style.hilited_comment_text_color,
          style.hilited_label_text_color);
    COLOR("hilited_mark_color", style.hilited_mark_color, 0);
#undef COLOR
    return true;
  }
  return false;
}
static void _LoadAppOptions(RimeConfig* config,
                            AppOptionsByAppName& app_options) {
  app_options.clear();
  ForEachRimeMap(
      config, "app_options", [&](const char* app_key, const char* app_path) {
        AppOptions& options(app_options[app_key]);
        ForEachRimeMap(
            config, app_path, [&](const char* opt_key, const char* opt_path) {
              Bool value = False;
              if (rime_api->config_get_bool(config, opt_path, &value)) {
                options[opt_key] = !!value;
              }
            });
      });
}

void RimeWithWeaselHandler::_GetStatus(Status& stat,
                                       WeaselSessionId ipc_id,
                                       Context& ctx) {
  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  RIME_STRUCT(RimeStatus, status);
  if (rime_api->get_status(session_id, &status)) {
    std::string schema_id = "";
    if (status.schema_id)
      schema_id = status.schema_id;
    stat.schema_name = u8tow(status.schema_name);
    stat.schema_id = u8tow(status.schema_id);
    stat.ascii_mode = !!status.is_ascii_mode;
    if (ipc_id == m_active_session) {
      const DWORD punct=status.is_ascii_mode || rime_api->get_option(session_id,"ascii_punct");
      HKEY key=nullptr;
      if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
        DWORD previous=2,bytes=sizeof(previous);
        RegQueryValueExW(key,L"TrayAsciiPunct",nullptr,nullptr,reinterpret_cast<BYTE*>(&previous),&bytes);
        if(previous!=punct) RegSetValueExW(key,L"TrayAsciiPunct",0,REG_DWORD,reinterpret_cast<const BYTE*>(&punct),sizeof(punct));
        RegCloseKey(key);
      }
    }
    stat.composing = !!status.is_composing;
    stat.disabled = !!status.is_disabled;
    stat.full_shape = !!status.is_full_shape;
    if (schema_id != m_last_schema_id) {
      session_status.__synced = false;
      m_last_schema_id = schema_id;
      if (schema_id != ".default") {  // don't load for schema select menu
        bool inline_preedit = session_status.style.inline_preedit;
        _LoadSchemaSpecificSettings(ipc_id, schema_id);
        _LoadAppInlinePreeditSet(ipc_id, true);
        if (session_status.style.inline_preedit != inline_preedit)
          // in case of inline_preedit set in schema
          _UpdateInlinePreeditStatus(ipc_id);
        // refresh icon after schema changed
        _RefreshTrayIcon(session_id, _UpdateUICallback);
        m_ui->style() = session_status.style;
        if (m_show_notifications.find("schema") != m_show_notifications.end() &&
            m_show_notifications_time > 0) {
          ctx.aux.str = stat.schema_name;
          m_ui->Update(ctx, stat);
          m_ui->ShowWithTimeout(m_show_notifications_time);
        }
      }
    }
    rime_api->free_status(&status);
  }
}

void RimeWithWeaselHandler::_GetContext(Context& weasel_context,
                                        RimeSessionId session_id,
                                        SessionStatus& session_status) {
  RIME_STRUCT(RimeContext, ctx);
  if (rime_api->get_context(session_id, &ctx)) {
    // Contexts for background TSF clients can be refreshed while another app
    // is composing.  They must not erase that app's pending cloud request.
    if (m_active_session && to_session_id(m_active_session) == session_id) {
      if (rime_api->get_option(session_id, "google_cloud_candidates") &&
          ctx.composition.length > 0) {
        const char* raw_input = rime_api->get_input(session_id);
        char planned_input[512] = {}, chinese[512] = {}, japanese[512] = {}, priority[16] = {};
        rime_api->get_property(session_id, "cloud_query_input", planned_input, sizeof(planned_input));
        rime_api->get_property(session_id, "cloud_query_zh", chinese, sizeof(chinese));
        rime_api->get_property(session_id, "cloud_query_ja", japanese, sizeof(japanese));
        rime_api->get_property(session_id, "cloud_query_priority", priority, sizeof(priority));
        if (raw_input && std::strcmp(raw_input, planned_input) == 0)
          _QueueCloudCandidate(raw_input, chinese, japanese, std::strcmp(priority, "ja") == 0, session_id);
        else _QueueCloudCandidate("", "", "", false, session_id);
      } else {
        _QueueCloudCandidate("", "", "", false, session_id);
      }
    }
    if (ctx.composition.length > 0) {
      weasel_context.preedit.str = u8tow(ctx.composition.preedit);
      char editing_flag[4]={};
      const bool editing=rime_api->get_property(session_id,"japanese_segment_active",editing_flag,sizeof(editing_flag)) && editing_flag[0]=='1';
      const bool raw_table_preedit = !editing && rime_api->get_option(session_id, "japanese_input_table_enabled") &&
          rime_api->get_option(session_id, "japanese_input_table_show_letters");
      if (raw_table_preedit) {
        const char* raw = rime_api->get_input(session_id);
        session_status.PruneHoldLabels();
        if (raw) weasel_context.preedit.str = u8tow(session_status.DisplayHoldLabels(
            session_status.physical_preedit_valid ? session_status.physical_preedit : std::string(raw)));
      }
      if (!raw_table_preedit && ctx.composition.sel_start < ctx.composition.sel_end) {
        TextAttribute attr;
        attr.type = HIGHLIGHTED;
        attr.range.start =
            utf8towcslen(ctx.composition.preedit, ctx.composition.sel_start);
        attr.range.end =
            utf8towcslen(ctx.composition.preedit, ctx.composition.sel_end);

        weasel_context.preedit.attributes.push_back(attr);
      }
    }
    if (ctx.menu.num_candidates) {
      CandidateInfo& cinfo(weasel_context.cinfo);
      _GetExpandedCandidateInfo(cinfo, ctx, session_id, session_status, false);
      for (const int cloud_index : CloudCandidateIndices(ctx,
          rime_api->get_option(session_id, "sentence_translation"),
          session_status.expanded_candidates ? 64 : 12)) {
        const bool cloud_en = CloudTranslationOption(
            session_id, "show_english_annotation", L"ShowEnglishAnnotation");
        const bool cloud_ja = CloudTranslationOption(
            session_id, "show_japanese_annotation", L"ShowJapaneseAnnotation");
        if (cloud_en || cloud_ja) {
          const std::wstring source =
              u8tow(ctx.menu.candidates[cloud_index].text);
          const char* comment = ctx.menu.candidates[cloud_index].comment;
          _QueueCloudTranslation(source, comment && std::strstr(comment, "[[RIME_LANG:JA]]"));
          const size_t cloud_absolute_index =
              static_cast<size_t>(ctx.menu.page_no) * ctx.menu.page_size +
              static_cast<size_t>(cloud_index);
          if (!session_status.expanded_candidates ||
              cloud_absolute_index >= session_status.expanded_window_start)
            _ApplyCloudTranslation(cinfo, source,
                session_status.expanded_candidates ?
                    cloud_absolute_index - session_status.expanded_window_start :
                    static_cast<size_t>(cloud_index),
                cloud_en, cloud_ja, false, session_id);
        }
      }
    }
    rime_api->free_context(&ctx);
  }
}

void RimeWithWeaselHandler::_UpdateInlinePreeditStatus(WeaselSessionId ipc_id) {
  if (!m_ui)
    return;
  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  // set inline_preedit option
  bool inline_preedit = session_status.style.inline_preedit;
  rime_api->set_option(session_id, "inline_preedit", Bool(inline_preedit));
  // show soft cursor on weasel panel but not inline
  rime_api->set_option(session_id, "soft_cursor", Bool(!inline_preedit));
}

