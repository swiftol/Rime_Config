#pragma once
#include <WeaselIPC.h>
#include <WeaselUI.h>
#include <map>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>

#include <rime_api.h>
#include <JapaneseKeyActions.h>
#include <JapaneseHoldToken.h>
#include <set>

struct CaseInsensitiveCompare {
  bool operator()(const std::string& str1, const std::string& str2) const {
    std::string str1Lower, str2Lower;
    std::transform(str1.begin(), str1.end(), std::back_inserter(str1Lower),
                   [](char c) { return std::tolower(c); });
    std::transform(str2.begin(), str2.end(), std::back_inserter(str2Lower),
                   [](char c) { return std::tolower(c); });
    return str1Lower < str2Lower;
  }
};

typedef std::map<std::string, bool> AppOptions;
typedef std::map<std::string, AppOptions, CaseInsensitiveCompare>
    AppOptionsByAppName;

struct SessionStatus {
  SessionStatus() : style(weasel::UIStyle()), expanded_candidates(false),
                    physical_preedit_valid(false), __synced(false),
                    session_id(0), hot_appearance_generation(0),
                    hot_input_generation(0), hot_phrase_generation(0),
                    hot_fuzzy_generation(0),
                    tsf_client(false) {
    RIME_STRUCT(RimeStatus, status);
  }
  weasel::UIStyle style;
  bool expanded_candidates;
  // Absolute candidate-list window for the expanded grid.  Rime's own
  // 36-item pages remain an implementation detail, not a UI scroll boundary.
  size_t expanded_window_start = 0;
  int expanded_skipped_rows = 0;
  std::vector<size_t> expanded_shift_history;
  std::string expanded_input;
  // Rime's Japanese fuzzy processor intentionally rewrites Context::input()
  // to the matched spelling (for example paypay -> baypay).  Keep the actual
  // sequential letter keys separately so inline raw preedit can remain a
  // faithful view of what the user typed while candidate matching stays fuzzy.
  std::string physical_preedit;
  bool physical_preedit_valid;
  RimeStatus status;
  bool __synced;
  RimeSessionId session_id;
  DWORD hot_appearance_generation;
  DWORD hot_input_generation;
  DWORD hot_phrase_generation;
  DWORD hot_fuzzy_generation;
  DWORD hot_annotation_generation = 0;
  bool tsf_client;
  std::string client_app;
  JapaneseLongPressPending long_press;
  using HoldLabel = JapaneseHoldToken;
  std::vector<HoldLabel> hold_labels;
  void PruneHoldLabels() {
    hold_labels.erase(std::remove_if(hold_labels.begin(), hold_labels.end(),
        [&](const HoldLabel& label) { return !physical_preedit_valid ||
          physical_preedit.compare(0,label.prefix.size(),label.prefix)!=0; }),hold_labels.end());
  }
  std::string DisplayHoldLabels(const std::string& raw) const {
    std::string result; size_t offset=0;
    for(const auto& label:hold_labels) {
      if(label.start<offset || raw.compare(0,label.prefix.size(),label.prefix)!=0)continue;
      result.append(raw,offset,label.start-offset);
      result.push_back(label.key);result.append("\xCC\x84"); // combining macron, display only
      offset=label.prefix.size();
    }
    result.append(raw,offset,std::string::npos);return result;
  }
  std::set<int> long_press_released_later;
  bool long_press_replaying = false;
  bool defer_key_response = false;
  std::string literal_key_commit;
  std::set<int> feature_hotkeys_down;
};
typedef std::map<DWORD, SessionStatus> SessionStatusMap;
typedef DWORD WeaselSessionId;
class RimeWithWeaselHandler : public weasel::RequestHandler {
 public:
  RimeWithWeaselHandler(weasel::UI* ui);
  virtual ~RimeWithWeaselHandler();
  virtual void Initialize();
  virtual void Finalize();
  virtual DWORD FindSession(WeaselSessionId ipc_id);
  virtual DWORD AddSession(LPWSTR buffer, EatLine eat = 0);
  virtual DWORD RemoveSession(WeaselSessionId ipc_id);
  virtual BOOL ProcessKeyEvent(weasel::KeyEvent keyEvent,
                               WeaselSessionId ipc_id,
                               EatLine eat);
  virtual void CommitComposition(WeaselSessionId ipc_id);
  virtual void ClearComposition(WeaselSessionId ipc_id);
  virtual void SelectCandidateOnCurrentPage(size_t index,
                                            WeaselSessionId ipc_id);
  virtual bool HighlightCandidateOnCurrentPage(size_t index,
                                               WeaselSessionId ipc_id,
                                               EatLine eat);
  virtual bool ChangePage(bool backward, WeaselSessionId ipc_id, EatLine eat);
  virtual void FocusIn(DWORD param, WeaselSessionId ipc_id);
  virtual void FocusOut(DWORD param, WeaselSessionId ipc_id);
  virtual void UpdateInputPosition(RECT const& rc, WeaselSessionId ipc_id);
  virtual void StartMaintenance();
  virtual void EndMaintenance();
  virtual void SetOption(WeaselSessionId ipc_id,
                         const std::string& opt,
                         bool val);
  virtual void SetJapaneseReadingPreview(bool active,
                                         WeaselSessionId ipc_id) override;
  virtual void UpdateColorTheme(BOOL darkMode);

  void OnUpdateUI(std::function<void()> const& cb);
  void OnSentenceTranslationReady(std::function<void()> const& cb);
  void RefreshActiveUI();
  void PollLongPress();
  void GetTrayFeatures(bool& learning, bool& ascii_punct);

 private:
  void _DispatchLongPress(WeaselSessionId ipc_id, const JapaneseLongPressPending& pending);
  void _Setup();
  bool _IsDeployerRunning();
  void _UpdateUI(WeaselSessionId ipc_id);
  void _LoadSchemaSpecificSettings(WeaselSessionId ipc_id,
                                   const std::string& schema_id);
  void _LoadAppInlinePreeditSet(WeaselSessionId ipc_id,
                                bool ignore_app_name = false);
  bool _ShowMessage(weasel::Context& ctx, weasel::Status& status);
  bool _Respond(WeaselSessionId ipc_id, EatLine eat);
  void _ReadClientInfo(WeaselSessionId ipc_id, LPWSTR buffer);
  void _GetCandidateInfo(weasel::CandidateInfo& cinfo,
                         RimeContext& ctx,
                         RimeSessionId session_id,
                         bool escape_for_ipc);
  void _GetExpandedCandidateInfo(weasel::CandidateInfo& cinfo,
                                 RimeContext& ctx,
                                 RimeSessionId session_id,
                                 SessionStatus& session_status,
                                 bool escape_for_ipc);
  void _GetStatus(weasel::Status& stat,
                  WeaselSessionId ipc_id,
                  weasel::Context& ctx);
  void _GetContext(weasel::Context& ctx, RimeSessionId session_id,
                   SessionStatus& session_status);
  void _UpdateShowNotifications(RimeConfig* config, bool initialize = false);

  void _UpdateInlinePreeditStatus(WeaselSessionId ipc_id);
  void _QueueSentenceTranslation(const std::wstring& source);
  void _ApplySentenceTranslation(weasel::CandidateInfo& candidates,
                                 const std::wstring& source,
                                 bool escape_for_ipc);
  void _SentenceTranslationWorker();
  void _QueueCloudTranslation(const std::wstring& source, bool japanese = false);
  void _ApplyCloudTranslation(weasel::CandidateInfo& candidates,
                              const std::wstring& source, size_t index,
                              bool show_english, bool show_japanese,
                              bool escape_for_ipc, RimeSessionId session_id);
  void _CloudTranslationWorker();
  void _QueueCloudCandidate(const std::string& input, const std::string& chinese,
                            const std::string& japanese, bool prefer_japanese,
                            RimeSessionId session_id);
  void _CloudCandidateWorker();

  RimeSessionId to_session_id(WeaselSessionId ipc_id) {
    return m_session_status_map[ipc_id].session_id;
  }
  SessionStatus& get_session_status(WeaselSessionId ipc_id) {
    return m_session_status_map[ipc_id];
  }
  SessionStatus& new_session_status(WeaselSessionId ipc_id) {
    return m_session_status_map[ipc_id] = SessionStatus();
  }

  AppOptionsByAppName m_app_options;
  weasel::UI* m_ui;  // reference
  DWORD m_active_session;
  bool m_disabled;
  std::string m_last_schema_id;
  std::string m_last_app_name;
  weasel::UIStyle m_base_style;
  std::map<std::string, bool> m_show_notifications;
  std::map<std::string, bool> m_show_notifications_base;
  std::function<void()> _UpdateUICallback;
  std::function<void()> m_sentence_translation_callback;
  std::mutex m_sentence_translation_mutex;
  std::condition_variable m_sentence_translation_cv;
  std::thread m_sentence_translation_thread;
  bool m_sentence_translation_stopping = false;
  std::wstring m_sentence_translation_pending;
  std::wstring m_sentence_translation_source;
  std::wstring m_sentence_translation_lines;
  std::mutex m_cloud_translation_mutex;
  std::condition_variable m_cloud_translation_cv;
  std::thread m_cloud_translation_thread;
  bool m_cloud_translation_stopping = false;
  using AnnotationSource = std::pair<std::wstring, bool>;
  AnnotationSource m_cloud_translation_pending;
  std::deque<AnnotationSource> m_cloud_translation_queue;
  AnnotationSource m_cloud_translation_source;
  std::wstring m_cloud_translation_english;
  std::wstring m_cloud_translation_japanese;
  std::mutex m_cloud_candidate_mutex;
  std::condition_variable m_cloud_candidate_cv;
  std::thread m_cloud_candidate_thread;
  bool m_cloud_candidate_stopping = false;
  std::string m_cloud_candidate_pending;
  std::string m_cloud_candidate_published;
  std::string m_cloud_candidate_refresh;
  RimeSessionId m_cloud_candidate_pending_session = 0;
  RimeSessionId m_cloud_candidate_published_session = 0;
  RimeSessionId m_cloud_candidate_refresh_session = 0;

  static void OnNotify(void* context_object,
                       uintptr_t session_id,
                       const char* message_type,
                       const char* message_value);
  static std::string m_message_type;
  static std::string m_message_value;
  static std::string m_message_label;
  static std::string m_option_name;
  static std::mutex m_notifier_mutex;
  SessionStatusMap m_session_status_map;
  bool m_current_dark_mode;
  bool m_global_ascii_mode;
  int m_show_notifications_time;
  DWORD m_pid;
};
