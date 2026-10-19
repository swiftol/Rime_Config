// TestWeaselIPC.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <WeaselIPC.h>
#include <RimeWithWeasel.h>
#include <ResponseParser.h>

#include <boost/interprocess/streams/bufferstream.hpp>
using namespace boost::interprocess;

#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>

CAppModule _Module;

int console_main();
int client_main();
int server_main();

static HWND g_panel_window = NULL;
static BOOL CALLBACK FindPanelWindow(HWND hwnd, LPARAM server_pid) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != static_cast<DWORD>(server_pid)) return TRUE;
  LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  RECT rect = {};
  GetWindowRect(hwnd, &rect);
  if ((ex_style & WS_EX_LAYERED) && rect.right > rect.left + 100) {
    g_panel_window = hwnd;
    return FALSE;
  }
  return TRUE;
}

static DWORD FindServerPid() {
  HWND server = FindWindow(WEASEL_IPC_WINDOW, NULL);
  DWORD pid = 0;
  if (server) GetWindowThreadProcessId(server, &pid);
  return pid;
}

static int right_click_test() {
  weasel::Client client;
  if (!client.Connect()) return 20;
  client.StartSession();
  RECT input = {80, 80, 82, 110};
  client.UpdateInputPosition(input);
  client.FocusIn();
  for (wchar_t ch : std::wstring(L"dan"))
    client.ProcessKeyEvent(weasel::KeyEvent(ch, 0));
  Sleep(500);
  const DWORD pid = FindServerPid();
  if (!pid) return 21;
  EnumWindows(FindPanelWindow, static_cast<LPARAM>(pid));
  if (!g_panel_window) return 22;
  if (GetWindowLongPtr(g_panel_window, GWL_EXSTYLE) & WS_EX_TRANSPARENT)
    return 23;
  const LPARAM point = MAKELPARAM(55, 95);
  SendMessage(g_panel_window, WM_RBUTTONDOWN, MK_RBUTTON, point);
  Sleep(300);
  SendMessage(g_panel_window, WM_RBUTTONDOWN, MK_RBUTTON, point);
  Sleep(300);
  client.ClearComposition();
  client.EndSession();
  return 0;
}

static int expanded_grid_test() {
  weasel::Client client;
  if (!client.Connect()) return 30;
  client.StartSession();
  RECT input = {80, 80, 82, 110};
  client.UpdateInputPosition(input);
  client.FocusIn();
  for (wchar_t ch : std::wstring(L"ky"))
    client.ProcessKeyEvent(weasel::KeyEvent(ch, 0));
  Sleep(500);
  client.ProcessKeyEvent(
      weasel::KeyEvent(ibus::Keycode::Page_Down, 0));
  // Keep the panel alive long enough for the headless screenshot verifier.
  Sleep(5000);
  client.ClearComposition();
  client.EndSession();
  return 0;
}

static int expanded_grid_back_test() {
  weasel::Client client;
  if (!client.Connect()) return 40;
  client.StartSession();
  RECT input = {80, 80, 82, 110};
  client.UpdateInputPosition(input);
  client.FocusIn();
  for (wchar_t ch : std::wstring(L"ky"))
    client.ProcessKeyEvent(weasel::KeyEvent(ch, 0));
  Sleep(400);
  client.ProcessKeyEvent(weasel::KeyEvent(ibus::Keycode::Page_Up, 0));
  client.ProcessKeyEvent(weasel::KeyEvent(
      ibus::Keycode::Page_Up, ibus::Modifier::RELEASE_MASK));
  // Verify the panel remains expanded after the key has been released.
  Sleep(2500);
  client.ClearComposition();
  client.EndSession();
  return 0;
}

// IPC-only smoke test: no keyboard injection or foreground app interaction.
// The test cache must first contain a non-Google sentinel for this spelling.
static int cloud_candidate_test() {
  weasel::Client client;
  if (!client.Connect()) return 50;
  client.StartSession();
  client.FocusIn();
  std::cout << "IPC_ECHO=" << client.Echo() << "\n";
  const auto started = GetTickCount64();
  int eaten_count = 0;
  for (wchar_t ch : std::wstring(L"nekkuresu"))
    if (client.ProcessKeyEvent(weasel::KeyEvent(ch, 0))) ++eaten_count;
  std::cout << "IPC_EATEN=" << eaten_count << "\n";
  wchar_t local_app_data[MAX_PATH] = {};
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
    return 51;
  const auto cache = std::filesystem::path(local_app_data) /
      L"ZhongriInputMethod" / L"google_cloud_candidate.tsv";
  int result = 52;
  for (int elapsed = 0; elapsed < 5000; elapsed += 50) {
    std::string line;
    {
      std::ifstream stream(cache, std::ios::binary);
      std::getline(stream, line);
    }
    if (line == u8"nekkuresu\tネックレス") {
      std::cout << "CLOUD_READY_MS=" << GetTickCount64() - started << "\n";
      result = 0;
      break;
    }
    Sleep(50);
  }
  client.ClearComposition();
  client.EndSession();
  return result;
}

static int compact_long_comment_test() {
  weasel::Client client;
  if (!client.Connect()) return 60;
  client.StartSession();
  RECT input = {80, 80, 82, 110};
  client.UpdateInputPosition(input);
  client.FocusIn();
  for (wchar_t ch : std::wstring(L"sjz"))
    client.ProcessKeyEvent(weasel::KeyEvent(ch, 0));
  Sleep(350);
  g_panel_window = NULL;
  const DWORD pid = FindServerPid();
  if (pid) EnumWindows(FindPanelWindow, static_cast<LPARAM>(pid));
  RECT panel = {};
  const bool found = g_panel_window && GetWindowRect(g_panel_window, &panel);
  const int width = found ? panel.right - panel.left : 0;
  std::cout << "SJZ_PANEL_WIDTH=" << width << "\n";
  client.ClearComposition();
  client.EndSession();
  return found && width > 0 && width < 700 ? 0 : 61;
}

// IPC-only appearance test: no foreground window, keyboard injection, or
// candidate popup.  Other clients are not notified of the temporary value.
static int hot_appearance_test() {
  weasel::Client client;
  if (!client.Connect()) return 80;
  client.StartSession();
  const auto read_style = [&]() {
    weasel::UIStyle style;
    client.ProcessKeyEvent(weasel::KeyEvent(0, 0));
    weasel::ResponseParser parser(nullptr, nullptr, nullptr, nullptr, &style);
    return std::pair<bool, weasel::UIStyle>(
        client.GetResponseData(std::ref(parser)), style);
  };
  auto before = read_style();
  if (!before.first) { client.EndSession(); return 81; }

  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                      0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    client.EndSession();
    return 82;
  }
  struct SavedValue { const wchar_t* name; DWORD value; bool present; };
  SavedValue saved[] = {
      {L"CandidateWidth", 0, false},
      {L"CommentFontSize", 0, false},
      {L"CandidateSpacing", 0, false},
      {L"HighlightPadding", 0, false},
      {L"ChineseTextColor", 0, false},
      {L"ChineseBackgroundColor", 0, false},
      {L"JapaneseTextColor", 0, false},
      {L"JapaneseBackgroundColor", 0, false},
      {L"CommonPhraseTextColor", 0, false},
      {L"CommonPhraseBackgroundColor", 0, false},
      {L"AppearanceSettingsGeneration", 0, false},
  };
  for (auto& item : saved) {
    DWORD type = 0, size = sizeof(item.value);
    item.present = RegQueryValueExW(key, item.name, nullptr, &type,
                                    reinterpret_cast<BYTE*>(&item.value),
                                    &size) == ERROR_SUCCESS &&
                   type == REG_DWORD;
  }
  const DWORD expected[] = {
      720, 13, 24, 6, 0xff112233, 0xff223344, 0xff334455,
      0xff445566, 0xff556677, 0xff667788, saved[10].value + 1,
  };
  for (size_t i = 0; i < std::size(saved); ++i)
    RegSetValueExW(key, saved[i].name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&expected[i]),
                   sizeof(expected[i]));
  auto after = read_style();
  for (const auto& item : saved) {
    if (item.present)
      RegSetValueExW(key, item.name, 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&item.value), sizeof(item.value));
    else
      RegDeleteValueW(key, item.name);
  }
  RegCloseKey(key);
  client.EndSession();
  const auto& style = after.second;
  std::cout << "HOT_STYLE_BEFORE_WIDTH=" << before.second.min_width << "\n"
            << "HOT_STYLE_AFTER_WIDTH=" << after.second.min_width << "\n"
            << "HOT_STYLE_AFTER_CHINESE_COLOR=" << std::hex
            << after.second.chinese_candidate_text_color << std::dec << "\n";
  return after.first && style.min_width == 720 &&
                 style.comment_font_point == 13 &&
                 style.candidate_spacing == 24 &&
                 style.hilite_padding_x == 6 && style.hilite_padding_y == 6 &&
                 static_cast<DWORD>(style.chinese_candidate_text_color) == expected[4] &&
                 static_cast<DWORD>(style.chinese_candidate_back_color) == expected[5] &&
                 static_cast<DWORD>(style.japanese_candidate_text_color) == expected[6] &&
                 static_cast<DWORD>(style.japanese_candidate_back_color) == expected[7] &&
                 static_cast<DWORD>(style.common_phrase_candidate_text_color) == expected[8] &&
                 static_cast<DWORD>(style.common_phrase_candidate_back_color) == expected[9]
             ? 0 : 83;
}

// usage: TestWeaselIPC.exe [/start | /stop | /console]

int _tmain(int argc, _TCHAR* argv[]) {
  if (argc == 1)  // no args
  {
    return client_main();
  } else if (argc > 1 && !wcscmp(L"/start", argv[1])) {
    return server_main();
  } else if (argc > 1 && !wcscmp(L"/stop", argv[1])) {
    weasel::Client client;
    if (!client.Connect()) {
      std::cerr << "server not running." << std::endl;
      return 0;
    }
    client.ShutdownServer();
    return 0;
  } else if (argc > 1 && !wcscmp(L"/console", argv[1])) {
    return console_main();
    return 0;
  } else if (argc > 1 && !wcscmp(L"/rightclick", argv[1])) {
    return right_click_test();
  } else if (argc > 1 && !wcscmp(L"/gridlayout", argv[1])) {
    return expanded_grid_test();
  } else if (argc > 1 && !wcscmp(L"/gridback", argv[1])) {
    return expanded_grid_back_test();
  } else if (argc > 1 && !wcscmp(L"/cloudtest", argv[1])) {
    return cloud_candidate_test();
  } else if (argc > 1 && !wcscmp(L"/sjzlayout", argv[1])) {
    return compact_long_comment_test();
  } else if (argc > 1 && !wcscmp(L"/hotappearance", argv[1])) {
    return hot_appearance_test();
  }

  return -1;
}

bool launch_server() {
  int ret = (int)ShellExecute(NULL, L"open", L"TestWeaselIPC.exe", L"/start",
                              NULL, SW_NORMAL);
  if (ret <= 32) {
    std::cerr << "failed to launch server." << std::endl;
    return false;
  }
  return true;
}

bool read_buffer(LPWSTR buffer, UINT length, LPWSTR dest) {
  wbufferstream bs(buffer, length);
  bs.read(dest, WEASEL_IPC_BUFFER_LENGTH);
  return bs.good();
}

const char* wcstomb(const wchar_t* wcs) {
  const int buffer_len = 8192;
  static char buffer[buffer_len];
  WideCharToMultiByte(CP_UTF8, NULL, wcs, -1, buffer, buffer_len, NULL, FALSE);
  return buffer;
}

int console_main() {
  weasel::Client client;
  if (!client.Connect()) {
    std::cerr << "failed to connect to server." << std::endl;
    return -2;
  }
  client.StartSession();
  if (!client.Echo()) {
    std::cerr << "failed to start session." << std::endl;
    return -3;
  }

  while (std::cin.good()) {
    int ch = std::cin.get();
    if (!std::cin.good())
      break;
    bool eaten = client.ProcessKeyEvent(weasel::KeyEvent(ch, 0));
    std::cout << "server replies: " << eaten << std::endl;
    if (eaten) {
      WCHAR response[WEASEL_IPC_BUFFER_LENGTH];
      bool ret = client.GetResponseData(
          std::bind<bool>(read_buffer, std::placeholders::_1,
                          std::placeholders::_2, std::ref(response)));
      std::cout << "get response data: " << ret << std::endl;
      std::cout << "buffer reads: " << std::endl
                << wcstomb(response) << std::endl;
    }
  }

  client.EndSession();

  return 0;
}

int client_main() {
  // launch_server();
  Sleep(1000);
  weasel::Client client;
  if (!client.Connect()) {
    std::cerr << "failed to connect to server." << std::endl;
    return -2;
  }
  client.StartSession();
  if (!client.Echo()) {
    std::cerr << "failed to login." << std::endl;
    return -3;
  }
  bool eaten = client.ProcessKeyEvent(weasel::KeyEvent(L'a', 0));
  std::cout << "server replies: " << eaten << std::endl;
  if (eaten) {
    WCHAR response[WEASEL_IPC_BUFFER_LENGTH];
    bool ret = client.GetResponseData(
        std::bind<bool>(read_buffer, std::placeholders::_1,
                        std::placeholders::_2, std::ref(response)));
    std::cout << "get response data: " << ret << std::endl;
    std::cout << "buffer reads: " << std::endl
              << wcstomb(response) << std::endl;
  }
  client.EndSession();

  system("pause");
  return 0;
}

class TestRequestHandler : public weasel::RequestHandler {
 public:
  TestRequestHandler() : m_counter(0) {
    std::cerr << "handler ctor." << std::endl;
  }
  virtual ~TestRequestHandler() {
    std::cerr << "handler dtor: " << m_counter << std::endl;
  }
  virtual UINT FindSession(UINT session_id) {
    std::cerr << "FindSession: " << session_id << std::endl;
    return (session_id <= m_counter ? session_id : 0);
  }
  virtual UINT AddSession(LPWSTR buffer) {
    std::cerr << "AddSession: " << m_counter + 1 << std::endl;
    return ++m_counter;
  }
  virtual UINT RemoveSession(UINT session_id) {
    std::cerr << "RemoveClient: " << session_id << std::endl;
    return 0;
  }
  virtual BOOL ProcessKeyEvent(weasel::KeyEvent keyEvent,
                               UINT session_id,
                               EatLine eat) {
    std::cerr << "ProcessKeyEvent: " << session_id
              << " keycode: " << keyEvent.keycode << " mask: " << keyEvent.mask
              << std::endl;
    eat(std::wstring(L"Greeting=Hello, 小狼毫.\n"));
    return TRUE;
  }

 private:
  unsigned int m_counter;
};

int server_main() {
  HRESULT hRes = _Module.Init(NULL, GetModuleHandle(NULL));
  ATLASSERT(SUCCEEDED(hRes));

  weasel::Server server;
  // weasel::UI ui;
  // const std::unique_ptr<weasel::RequestHandler> handler(new
  // RimeWithWeaselHandler(&ui));
  const std::unique_ptr<weasel::RequestHandler> handler(new TestRequestHandler);

  server.SetRequestHandler(handler.get());
  if (!server.Start())
    return -4;
  std::cerr << "server running." << std::endl;
  int ret = server.Run();
  std::cerr << "server quitting." << std::endl;
  return ret;
}
