#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "rime_api.h"

static std::string Utf8(const std::wstring& value) {
  if (value.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0,
                                 nullptr, nullptr);
  std::string result(size - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], size,
                      nullptr, nullptr);
  return result;
}

static std::string WinError(DWORD code) {
  wchar_t* message = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS;
  FormatMessageW(flags, nullptr, code, 0,
                 reinterpret_cast<wchar_t*>(&message), 0, nullptr);
  std::string result = message ? Utf8(message) : std::string();
  if (message) LocalFree(message);
  while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
    result.pop_back();
  return result;
}

static void ProbeSystemDll(const wchar_t* name) {
  SetLastError(ERROR_SUCCESS);
  HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  DWORD error = module ? ERROR_SUCCESS : GetLastError();
  std::cout << "DEPENDENCY_" << Utf8(name) << "="
            << (module ? "OK" : "FAILED") << ";ERROR=" << error;
  if (error) std::cout << ";MESSAGE=" << WinError(error);
  std::cout << "\n";
  if (module) FreeLibrary(module);
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    std::cerr << "usage: RimeCandidateSelfTest <install-root> <user-dir> [--deploy] [--trace-keys] [--repeat=N] [--schema=name] [--option=name=0|1] [--hot-option=name=0|1] [--audit-file=code-text.tsv] [--passage-file=code-text.tsv] [--audit-max-candidates=N] [input]\n";
    return 64;
  }
  std::wstring root = argv[1];
  std::string shared = Utf8(root + L"\\data");
  std::string user = Utf8(argv[2]);

  HMODULE dll = LoadLibraryW((root + L"\\rime.dll").c_str());
  if (!dll) {
    DWORD error = GetLastError();
    std::cerr << "LOAD_RIME_DLL_FAILED=" << error
              << ";MESSAGE=" << WinError(error) << "\n";
    ProbeSystemDll(L"bcrypt.dll");
    ProbeSystemDll(L"dbghelp.dll");
    ProbeSystemDll(L"kernel32.dll");
    ProbeSystemDll(L"user32.dll");
    return 65;
  }
  using GetApi = RimeApi* (*)();
  auto get_api = reinterpret_cast<GetApi>(GetProcAddress(dll, "rime_get_api"));
  if (!get_api) {
    std::cerr << "RIME_GET_API_MISSING\n";
    return 66;
  }
  RimeApi* rime = get_api();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = shared.c_str();
  traits.user_data_dir = user.c_str();
  traits.prebuilt_data_dir = shared.c_str();
  traits.distribution_name = "Rime Chinese Japanese";
  traits.distribution_code_name = "Weasel";
  traits.distribution_version = "1.0.1";
  traits.app_name = "rime.cnjp.selftest";
  traits.min_log_level = 1;
  traits.log_dir = "";
  rime->setup(&traits);
  rime->initialize(nullptr);

  bool deploy = false;
  bool trace_keys = false;
  bool probe_expanded = false;
  std::string audit_file;
  std::string passage_file;
  int audit_max_candidates = 256;
  int repeat = 1;
  std::string input = "nihao";
  std::string schema = "rime_ice_japanese";
  std::vector<std::pair<std::string, bool>> options;
  std::vector<std::pair<std::string, bool>> hot_options;
  for (int i = 3; i < argc; ++i) {
    std::wstring argument(argv[i]);
    if (argument == L"--deploy")
      deploy = true;
    else if (argument == L"--trace-keys")
      trace_keys = true;
    else if (argument == L"--probe-expanded")
      probe_expanded = true;
    else if (argument.rfind(L"--repeat=", 0) == 0) {
      repeat = _wtoi(argument.substr(9).c_str());
      if (repeat < 1) repeat = 1;
    }
    else if (argument.rfind(L"--schema=", 0) == 0)
      schema = Utf8(argument.substr(9));
    else if (argument.rfind(L"--audit-file=", 0) == 0)
      audit_file = Utf8(argument.substr(13));
    else if (argument.rfind(L"--passage-file=", 0) == 0)
      passage_file = Utf8(argument.substr(15));
    else if (argument.rfind(L"--audit-max-candidates=", 0) == 0) {
      audit_max_candidates = _wtoi(argument.substr(23).c_str());
      if (audit_max_candidates < 1) audit_max_candidates = 256;
    }
    else if (argument.rfind(L"--option=", 0) == 0) {
      std::wstring setting = argument.substr(9);
      size_t split = setting.rfind(L'=');
      if (split == std::wstring::npos) {
        std::cerr << "INVALID_OPTION=" << Utf8(setting) << "\n";
        return 71;
      }
      options.emplace_back(Utf8(setting.substr(0, split)),
                           setting.substr(split + 1) != L"0");
    }
    else if (argument.rfind(L"--hot-option=", 0) == 0) {
      std::wstring setting = argument.substr(13);
      size_t split = setting.rfind(L'=');
      if (split == std::wstring::npos) {
        std::cerr << "INVALID_HOT_OPTION=" << Utf8(setting) << "\n";
        return 71;
      }
      hot_options.emplace_back(Utf8(setting.substr(0, split)),
                               setting.substr(split + 1) != L"0");
    }
    else
      input = Utf8(argument);
  }
  if (deploy) {
    std::cout << "DEPLOY_START=1\n";
    if (rime->start_maintenance(true))
      rime->join_maintenance_thread();
    std::cout << "DEPLOY_FINISH=1\n";
  }

  RimeSessionId session = rime->create_session();
  if (!session) {
    std::cerr << "CREATE_SESSION_FAILED\n";
    rime->finalize();
    return 67;
  }
  if (!rime->select_schema(session, schema.c_str())) {
    std::cerr << "SELECT_SCHEMA_FAILED\n";
    rime->destroy_session(session);
    rime->finalize();
    return 68;
  }
  for (const auto& option : options)
    rime->set_option(session, option.first.c_str(), option.second);
  if (!passage_file.empty()) {
    std::ifstream words(passage_file, std::ios::binary);
    if (!words) {
      std::cerr << "PASSAGE_FILE_OPEN_FAILED=" << passage_file << "\n";
      rime->destroy_session(session);
      rime->finalize();
      return 74;
    }
    int total = 0, failures = 0, empty_frames = 0;
    std::string line, committed_passage;
    while (std::getline(words, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      auto split = line.find('\t');
      if (split == std::string::npos || split == 0 ||
          split + 1 >= line.size()) continue;
      std::string code = line.substr(0, split);
      std::string expected = line.substr(split + 1);
      ++total;
      bool accepted = true;
      for (unsigned char key : code) {
        if (!rime->process_key(session, key, 0)) accepted = false;
        RIME_STRUCT(RimeContext, step_context);
        if (rime->get_context(session, &step_context)) {
          if (step_context.menu.num_candidates == 0) ++empty_frames;
          rime->free_context(&step_context);
        } else {
          ++empty_frames;
        }
      }
      int selected_index = -1;
      std::string first;
      RimeCandidateListIterator iterator = {};
      if (rime->candidate_list_begin(session, &iterator)) {
        int scanned = 0;
        while (scanned < audit_max_candidates &&
               rime->candidate_list_next(&iterator)) {
          if (scanned == 0 && iterator.candidate.text)
            first = iterator.candidate.text;
          if (selected_index < 0 && iterator.candidate.text &&
              expected == iterator.candidate.text)
            selected_index = iterator.index;
          ++scanned;
        }
        rime->candidate_list_end(&iterator);
      }
      std::string committed;
      if (selected_index >= 0 &&
          rime->select_candidate(session, selected_index)) {
        RIME_STRUCT(RimeCommit, commit);
        if (rime->get_commit(session, &commit)) {
          committed = commit.text ? commit.text : "";
          rime->free_commit(&commit);
        }
      }
      bool passed = accepted && selected_index >= 0 &&
                    committed == expected;
      if (!passed) ++failures;
      if (!committed_passage.empty()) committed_passage += " ";
      committed_passage += committed;
      std::cout << "PASSAGE_WORD=" << code << ";FIRST=" << first
                << ";EXPECTED=" << expected
                << ";SELECTED_INDEX=" << selected_index
                << ";COMMITTED=" << committed
                << ";PASS=" << (passed ? 1 : 0) << "\n";
      if (!passed) rime->clear_composition(session);
    }
    std::cout << "PASSAGE_SUMMARY=WORDS:" << total
              << ";FAILURES:" << failures
              << ";EMPTY_FRAMES:" << empty_frames
              << ";TEXT:" << committed_passage << "\n";
    rime->destroy_session(session);
    rime->finalize();
    FreeLibrary(dll);
    return failures || empty_frames ? 75 : 0;
  }
  if (!audit_file.empty()) {
    std::ifstream cases(audit_file, std::ios::binary);
    if (!cases) {
      std::cerr << "AUDIT_FILE_OPEN_FAILED=" << audit_file << "\n";
      rime->destroy_session(session);
      rime->finalize();
      return 72;
    }
    int tested = 0, exact_missing = 0, fuzzy_only = 0, missing_both = 0;
    std::string line;
    while (std::getline(cases, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      auto split = line.find('\t');
      if (split == std::string::npos || split == 0 ||
          split + 1 >= line.size()) continue;
      std::string code = line.substr(0, split);
      std::string expected = line.substr(split + 1);
      bool found[2] = {false, false};
      for (int fuzzy = 0; fuzzy <= 1; ++fuzzy) {
        rime->clear_composition(session);
        rime->set_option(session, "japanese_fuzzy_match", fuzzy != 0);
        if (!rime->simulate_key_sequence(session, code.c_str())) continue;
        RimeCandidateListIterator iterator = {};
        if (!rime->candidate_list_begin(session, &iterator)) continue;
        int scanned = 0;
        while (scanned < audit_max_candidates &&
               rime->candidate_list_next(&iterator)) {
          ++scanned;
          if (iterator.candidate.text &&
              expected == iterator.candidate.text) {
            found[fuzzy] = true;
            break;
          }
        }
        rime->candidate_list_end(&iterator);
      }
      ++tested;
      if (!found[0]) {
        ++exact_missing;
        if (found[1]) {
          ++fuzzy_only;
          std::cout << "AUDIT_FUZZY_ONLY=" << code << "\t" << expected << "\n";
        } else {
          ++missing_both;
          std::cout << "AUDIT_MISSING_BOTH=" << code << "\t" << expected << "\n";
        }
      }
    }
    std::cout << "AUDIT_SUMMARY=TESTED:" << tested
              << ";EXACT_MISSING:" << exact_missing
              << ";FUZZY_ONLY:" << fuzzy_only
              << ";MISSING_BOTH:" << missing_both << "\n";
    rime->destroy_session(session);
    rime->finalize();
    FreeLibrary(dll);
    return exact_missing ? 73 : 0;
  }
  bool processed = true;
  if (trace_keys) {
    for (int run = 1; run <= repeat && processed; ++run) {
      if (run > 1) rime->clear_composition(session);
      std::string prefix;
      for (unsigned char key : input) {
        prefix.push_back(static_cast<char>(key));
        auto started = std::chrono::steady_clock::now();
        bool accepted = rime->process_key(session, key, 0);
        RIME_STRUCT(RimeContext, step_context);
        int step_count = 0;
        std::string step_preedit;
        if (rime->get_context(session, &step_context)) {
          step_count = step_context.menu.num_candidates;
          step_preedit = step_context.composition.preedit
                             ? step_context.composition.preedit
                             : "";
          rime->free_context(&step_context);
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (repeat == 1) {
          std::cout << "KEY_TIMING=" << prefix << ";MS=" << elapsed;
        } else {
          std::cout << "KEY_TIMING_RUN=" << run << ";PREFIX=" << prefix
                    << ";MS=" << elapsed;
        }
        std::cout << ";CANDIDATES=" << step_count
                  << ";PREEDIT=" << step_preedit << "\n";
        if (!accepted) {
          processed = false;
          break;
        }
      }
    }
  } else {
    processed = rime->simulate_key_sequence(session, input.c_str());
  }
  if (!processed) {
    std::cerr << "PROCESS_INPUT_FAILED=" << input << "\n";
    rime->destroy_session(session);
    rime->finalize();
    return 69;
  }

  if (probe_expanded) {
    RimeCandidateListIterator iterator = {};
    int available = 0;
    if (rime->candidate_list_begin(session, &iterator)) {
      while (available < 150 && rime->candidate_list_next(&iterator)) {
        if (available == 0 || available == 35 || available == 36 ||
            available == 42 ||
            available == 99 || available == 100)
          std::cout << "ABSOLUTE_" << available << "="
                    << (iterator.candidate.text ? iterator.candidate.text : "")
                    << "\n";
        ++available;
      }
      rime->candidate_list_end(&iterator);
    }
    std::cout << "ABSOLUTE_AVAILABLE_UP_TO_150=" << available << "\n";
    for (const size_t index : {size_t(36), size_t(99), size_t(100)}) {
      const bool highlighted = rime->highlight_candidate(session, index);
      RIME_STRUCT(RimeContext, probe_context);
      if (rime->get_context(session, &probe_context)) {
        std::cout << "HIGHLIGHT_" << index << "=" << highlighted
                  << ";PAGE=" << probe_context.menu.page_no
                  << ";INDEX=" << probe_context.menu.highlighted_candidate_index
                  << "\n";
        rime->free_context(&probe_context);
      }
    }
    rime->destroy_session(session);
    rime->finalize();
    FreeLibrary(dll);
    return available > 100 ? 0 : 76;
  }

  if (!hot_options.empty()) {
    RIME_STRUCT(RimeContext, before);
    if (rime->get_context(session, &before)) {
      for (int i = 0; i < before.menu.num_candidates && i < 12; ++i)
        std::cout << "HOT_BEFORE_COMMENT_" << (i + 1) << "="
                  << (before.menu.candidates[i].comment
                          ? before.menu.candidates[i].comment : "") << "\n";
      rime->free_context(&before);
    }
    for (const auto& option : hot_options)
      rime->set_option(session, option.first.c_str(), option.second);
  }

  RIME_STRUCT(RimeContext, context);
  int count = 0;
  if (rime->get_context(session, &context)) {
    count = context.menu.num_candidates;
    std::cout << "SCHEMA=" << schema << "\nINPUT=" << input
              << "\nPREEDIT="
              << (context.composition.preedit ? context.composition.preedit : "")
              << "\nCANDIDATE_COUNT=" << count << "\n";
    for (int i = 0; i < count; ++i)
      std::cout << "CANDIDATE_" << (i + 1) << "="
                << (context.menu.candidates[i].text
                        ? context.menu.candidates[i].text
                        : "")
                << "\n";
    for (int i = 0; i < count; ++i)
      std::cout << "COMMENT_" << (i + 1) << "="
                << (context.menu.candidates[i].comment
                        ? context.menu.candidates[i].comment
                        : "")
                << "\n";
    rime->free_context(&context);
  } else {
    std::cerr << "GET_CONTEXT_FAILED\n";
  }
  rime->destroy_session(session);
  rime->finalize();
  FreeLibrary(dll);
  return count > 0 ? 0 : 70;
}
