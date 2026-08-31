#include <windows.h>
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
    std::cerr << "usage: RimeCandidateSelfTest <install-root> <user-dir> [--deploy] [--option=name=0|1] [input]\n";
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
  std::string input = "nihao";
  std::vector<std::pair<std::string, bool>> options;
  for (int i = 3; i < argc; ++i) {
    std::wstring argument(argv[i]);
    if (argument == L"--deploy")
      deploy = true;
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
  if (!rime->select_schema(session, "rime_ice_japanese")) {
    std::cerr << "SELECT_SCHEMA_FAILED\n";
    rime->destroy_session(session);
    rime->finalize();
    return 68;
  }
  for (const auto& option : options)
    rime->set_option(session, option.first.c_str(), option.second);
  if (!rime->simulate_key_sequence(session, input.c_str())) {
    std::cerr << "PROCESS_INPUT_FAILED=" << input << "\n";
    rime->destroy_session(session);
    rime->finalize();
    return 69;
  }

  RIME_STRUCT(RimeContext, context);
  int count = 0;
  if (rime->get_context(session, &context)) {
    count = context.menu.num_candidates;
    std::cout << "SCHEMA=rime_ice_japanese\nINPUT=" << input
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
