#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "japanese_sentence_decoder.h"

namespace {

constexpr wchar_t kInputPipeName[] = L"\\\\.\\pipe\\RimeMozcV2In";
constexpr wchar_t kOutputPipeName[] = L"\\\\.\\pipe\\RimeMozcV2Out";
std::unique_ptr<japanese::JapaneseSentenceDecoder> sentence_decoder;

std::string Utf8Path(const std::wstring& path) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, path.data(), int(path.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, path.data(), int(path.size()), out.data(), size, nullptr, nullptr);
  return out;
}

struct ChildProcess {
  HANDLE process = nullptr;
  HANDLE input = nullptr;
  HANDLE output = nullptr;
  std::string buffered;

  ~ChildProcess() {
    if (input) CloseHandle(input);
    if (output) CloseHandle(output);
    if (process) {
      TerminateProcess(process, 0);
      CloseHandle(process);
    }
  }
};

struct RomanRule {
  std::string output;
  std::string pending;
};

using RomanTable = std::map<std::string, RomanRule>;

RomanTable LoadRomanTable(const std::wstring& path) {
  RomanTable table;
  std::ifstream input(path, std::ios::binary);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto first = line.find('\t');
    if (first == std::string::npos) continue;
    const auto second = line.find('\t', first + 1);
    const std::string key = line.substr(0, first);
    RomanRule rule;
    rule.output = line.substr(first + 1,
                              second == std::string::npos
                                  ? std::string::npos
                                  : second - first - 1);
    if (second != std::string::npos) rule.pending = line.substr(second + 1);
    table.emplace(key, std::move(rule));
  }
  return table;
}

std::string RomanToHiragana(std::string input, const RomanTable& table) {
  std::replace(input.begin(), input.end(), 'q', '-');
  std::string result;
  while (!input.empty()) {
    // `nn` is ambiguous when the second n begins an n-row mora.  A plain
    // longest-match pass consumes both letters as ん, so `honnin` becomes
    // ほんいん (本院) instead of ほんにん (本人).  In `nn` + vowel/y, commit
    // only the first n as ん and leave the second one for ni/na/nya/etc.
    if (input.size() >= 3 && input[0] == 'n' && input[1] == 'n' &&
        (input[2] == 'a' || input[2] == 'i' || input[2] == 'u' ||
         input[2] == 'e' || input[2] == 'o' || input[2] == 'y')) {
      const auto single_n = table.find("n");
      result += single_n == table.end() ? "n" : single_n->second.output;
      input.erase(input.begin());
      continue;
    }
    auto best = table.end();
    size_t best_length = 0;
    for (auto it = table.begin(); it != table.end(); ++it) {
      if (it->first.size() > best_length &&
          input.rfind(it->first, 0) == 0) {
        best = it;
        best_length = it->first.size();
      }
    }
    if (best == table.end()) {
      result.push_back(input.front());
      input.erase(input.begin());
      continue;
    }
    result += best->second.output;
    input = best->second.pending + input.substr(best_length);
  }
  return result;
}

bool WriteAll(HANDLE handle, const std::string& data) {
  size_t offset = 0;
  while (offset < data.size()) {
    DWORD written = 0;
    if (!WriteFile(handle, data.data() + offset,
                   static_cast<DWORD>(data.size() - offset), &written,
                   nullptr))
      return false;
    offset += written;
  }
  return true;
}

bool ReadLine(HANDLE handle, std::string* buffered, std::string* line) {
  while (true) {
    const auto newline = buffered->find('\n');
    if (newline != std::string::npos) {
      *line = buffered->substr(0, newline);
      buffered->erase(0, newline + 1);
      if (!line->empty() && line->back() == '\r') line->pop_back();
      return true;
    }
    char chunk[4096];
    DWORD read = 0;
    if (!ReadFile(handle, chunk, sizeof(chunk), &read, nullptr) || read == 0)
      return false;
    buffered->append(chunk, read);
  }
}

std::wstring Quote(const std::wstring& value) {
  return L"\"" + value + L"\"";
}

bool StartConverter(const std::wstring& exe, const std::wstring& runfiles,
                    const std::wstring& profile, ChildProcess* child) {
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE child_stdout_read = nullptr;
  HANDLE child_stdout_write = nullptr;
  HANDLE child_stdin_read = nullptr;
  HANDLE child_stdin_write = nullptr;
  if (!CreatePipe(&child_stdout_read, &child_stdout_write, &security, 0) ||
      !CreatePipe(&child_stdin_read, &child_stdin_write, &security, 0))
    return false;
  SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  startup.hStdInput = child_stdin_read;
  startup.hStdOutput = child_stdout_write;
  HANDLE null_output = CreateFileW(L"NUL", GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, 0, nullptr);
  startup.hStdError = null_output;
  PROCESS_INFORMATION process{};
  std::wstring command = Quote(exe) + L" --engine_name=oss --user_profile_dir=" +
                         Quote(profile) + L" --max_candidates_to_show=9";
  std::vector<wchar_t> writable(command.begin(), command.end());
  writable.push_back(L'\0');
  const BOOL started = CreateProcessW(
      exe.c_str(), writable.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, runfiles.c_str(),
      &startup, &process);
  CloseHandle(child_stdin_read);
  CloseHandle(child_stdout_write);
  if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
  if (!started) {
    CloseHandle(child_stdout_read);
    CloseHandle(child_stdin_write);
    return false;
  }
  CloseHandle(process.hThread);
  child->process = process.hProcess;
  child->input = child_stdin_write;
  child->output = child_stdout_read;

  // converter_main always writes four startup lines before accepting input.
  std::string ignored;
  for (int i = 0; i < 4; ++i) {
    if (!ReadLine(child->output, &child->buffered, &ignored)) return false;
  }
  return true;
}

bool IsSegmentHeader(const std::string& line) {
  return !line.empty() && line.front() == '-' &&
         line.find(" Segment ") != std::string::npos;
}

bool ParseCandidate(const std::string& line, std::string* value) {
  const auto slash = line.find('/');
  if (slash == std::string::npos) return false;
  auto pos = slash + 1;
  while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])))
    ++pos;
  if (pos == slash + 1) return false;
  while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
    ++pos;
  if (pos >= line.size()) return false;
  *value = line.substr(pos);
  return true;
}

struct EditableSegment { std::string key; std::vector<std::string> words; };
std::vector<std::string> SplitFields(const std::string& text, char separator) {
  std::vector<std::string> out;size_t start=0;
  for(;;){auto end=text.find(separator,start);out.push_back(text.substr(start,end-start));if(end==std::string::npos)break;start=end+1;}
  return out;
}
std::string HexText(const std::string& text) {
  const char* digits="0123456789abcdef";std::string out;
  for(unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}return out;
}
bool UnhexText(const std::string& text,std::string* out) {
  out->clear();if(text.size()%2 || text.size()>2048)return false;
  auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
  for(size_t i=0;i<text.size();i+=2){int a=nibble(text[i]),b=nibble(text[i+1]);if(a<0||b<0)return false;out->push_back(char(a*16+b));}
  return true;
}
int KanaLength(const std::string& text) {
  int length=0;
  // Editable conversion accepts kana only; no converter command injection.
  for(size_t i=0;i<text.size();i+=3){
    if(i+2>=text.size())return -1;
    unsigned a=(unsigned char)text[i],b=(unsigned char)text[i+1],c=(unsigned char)text[i+2];
    if(a!=0xe3 || (b&0xc0)!=0x80 || (c&0xc0)!=0x80)return -1;
    unsigned cp=((a&15)<<12)|((b&63)<<6)|(c&63);
    if(!((cp>=0x3041&&cp<=0x3096)||(cp>=0x30a1&&cp<=0x30fa)||cp==0x30fc))return -1;
    ++length;
  }
  return length;
}
bool ReadEditable(ChildProcess* child,const std::string& command,std::vector<EditableSegment>* segments) {
  segments->clear();if(!WriteAll(child->input,command+"\n"))return false;
  bool key_next=false,failed=false;std::string line;
  while(ReadLine(child->output,&child->buffered,&line)) {
    if(line.empty() && (!segments->empty() || failed))break;
    if(line.find("ExecCommand() return false")!=std::string::npos){failed=true;continue;}
    if(IsSegmentHeader(line)){segments->push_back({});key_next=true;continue;}
    if(key_next){segments->back().key=line;key_next=false;continue;}
    std::string word;
    if(!segments->empty() && ParseCandidate(line,&word) && segments->back().words.size()<9)segments->back().words.push_back(word);
  }
  if(failed || segments->empty() || segments->size()>32)return false;
  for(const auto& segment:*segments)if(KanaLength(segment.key)<=0 || segment.words.empty())return false;
  return true;
}
std::vector<std::string> EditConversion(ChildProcess* child,const std::string& request) {
  // Stateless replay isolates simultaneous Rime sessions. User choices are
  // actual native segment candidates, never a Cartesian product or score.
  const auto fields=SplitFields(request,'|');if(fields.size()!=4)return {};
  std::string reading;if(!UnhexText(fields[0],&reading))return {};
  const int length=KanaLength(reading);if(length<=0||length>128)return {};
  std::vector<int> sizes;
  if(!fields[1].empty()){
    int total=0;
    for(const auto& part:SplitFields(fields[1],',')){
      if(part.empty()||part.size()>3||part.find_first_not_of("0123456789")!=std::string::npos)return {};
      int n=std::stoi(part);if(n<=0||n>128)return {};sizes.push_back(n);total+=n;
    }
    if(sizes.size()>32||total!=length)return {};
  }
  auto choices=fields[2].empty()?std::vector<std::string>{}:SplitFields(fields[2],',');
  if(choices.size()>32)return {};
  for(auto& choice:choices){std::string decoded;if(!UnhexText(choice,&decoded))return {};choice=decoded;}
  int resize_index=-1,delta=0;
  if(!fields[3].empty()){
    auto change=SplitFields(fields[3],',');
    if(change.size()!=2||change[0].empty()||change[0].size()>2||change[0].find_first_not_of("0123456789")!=std::string::npos || (change[1]!="1"&&change[1]!="-1"))return {};
    resize_index=std::stoi(change[0]);delta=std::stoi(change[1]);if(resize_index>=32)return {};
  }
  std::vector<EditableSegment> segments;
  if(!ReadEditable(child,"start "+reading,&segments))return {};
  if(!sizes.empty()){
    std::string command="resizes 0";
    for(int n:sizes)command+=" "+std::to_string(n);
    if(sizes.size()==1){int diff=sizes[0]-KanaLength(segments[0].key);command="resize 0 "+std::to_string(diff);if(!diff)command.clear();}
    if(!command.empty()&&!ReadEditable(child,command,&segments))return {};
    if(segments.size()!=sizes.size())return {};
    for(size_t i=0;i<sizes.size();++i)if(KanaLength(segments[i].key)!=sizes[i])return {};
  }
  for(size_t i=0;i<choices.size();++i){
    if(choices[i].empty())continue;
    if(i>=segments.size())return {};
    auto found=std::find(segments[i].words.begin(),segments[i].words.end(),choices[i]);if(found==segments[i].words.end())return {};
    int index=int(found-segments[i].words.begin());
    if(!ReadEditable(child,"commit "+std::to_string(i)+" "+std::to_string(index),&segments))return {};
  }
  if(resize_index>=0){
    if(resize_index>=int(segments.size()) || (delta<0&&KanaLength(segments[resize_index].key)<=1))return {};
    if(!ReadEditable(child,"resize "+std::to_string(resize_index)+" "+std::to_string(delta),&segments))return {};
  }
  std::vector<std::string> result;std::string joined;
  for(size_t i=0;i<segments.size();++i){
    const auto& s=segments[i];joined+=s.key;
    std::string chosen=resize_index>=0&&int(i)>=resize_index?"":i<choices.size()?choices[i]:"";
    if(chosen.empty())chosen=s.words[0];
    if(std::find(s.words.begin(),s.words.end(),chosen)==s.words.end())return {};
    std::string row="EDITSEG:"+std::to_string(i)+":"+HexText(s.key)+":"+HexText(chosen)+":";
    for(size_t n=0;n<s.words.size();++n){if(n)row+=",";row+=HexText(s.words[n]);}result.push_back(row);
  }
  if(joined!=reading)return {};
  return result;
}

std::vector<std::string> Convert(ChildProcess* child,
                                 const std::string& reading, bool include_predictions = false,
                                 bool native_only = false) {
  // Search full reading paths BEFORE flattening Mozc's fixed segments.
  // MSIME's dictionary word costs and POS connection matrix score both
  // segmentation and surface choices together. Only complete paths are
  // returned here: prefix lemmas must never masquerade as full-span words.
  std::vector<std::string> whole;
  bool lexical_reading = false;
  if (!native_only && sentence_decoder && sentence_decoder->ready() && reading.size() <= 384) {
    try {
      const auto paths = sentence_decoder->Decode(reading, 16);
      const auto exact = sentence_decoder->ExactLemmas(reading, 16);
      lexical_reading = !exact.empty();
      auto append = [&](const std::string& text) {
        if (!text.empty() && std::find(whole.begin(), whole.end(), text) == whole.end()) whole.push_back(text);
      };
      // Adapted from Metasequoia's JapaneseCandidateProvider ordering:
      // best full conversion / exact lemmas, dictionary predictions, then
      // plausible alternate paths. Never flatten all n-best segment products
      // into equally valid lexical candidates.
      // https://github.com/metasequoiaime/msime-windows/blob/develop/engine/providers/japanese_candidate_provider.cpp
      // Send best-path model evidence independently of exact-lemma/prediction
      // metadata. A productive sentence is not an invented word merely because
      // its entire reading is absent from the dictionary.
      if (!paths.empty()) append(paths.front().text + (include_predictions ?
          std::string(1, '\x1e') + std::to_string(paths.front().cost) + "," +
          std::to_string(paths.front().unknown_kana) : ""));
      for (const auto& lemma : exact) append(include_predictions ? lemma.surface + '\x1f' + lemma.reading : lemma.surface);
      if (include_predictions && lexical_reading && reading.size() >= 6) {
        size_t predictions = 0;
        std::vector<std::string> surfaces;
        for (const auto& lemma : exact) surfaces.push_back(lemma.surface);
        std::vector<std::string> predicted_surfaces;
        for (const auto& lemma : sentence_decoder->LongerPrefixLemmas(reading, 96)) {
          if (lemma.reading.size() <= reading.size() || lemma.reading.compare(0, reading.size(), reading) != 0) continue;
          if (std::none_of(surfaces.begin(), surfaces.end(), [&](const std::string& surface) {
                return lemma.surface.size() > surface.size() && lemma.surface.compare(0, surface.size(), surface) == 0;
              })) continue;
          if (std::find(predicted_surfaces.begin(), predicted_surfaces.end(), lemma.surface) != predicted_surfaces.end()) continue;
          predicted_surfaces.push_back(lemma.surface);
          append(lemma.surface + '\x1f' + lemma.reading);
          if (++predictions >= 12) break;
        }
      }
      for (size_t index = 1; index < paths.size(); ++index) {
        // With an attested whole word, low-probability resegmentation is not
        // a useful alternative. Without one, retain sentence conversion.
        // Unattested readings are sentence guesses, not whole-word evidence.
        // Do not present a long n-best list of near-identical invented phrases.
        if (!lexical_reading && index >= 3) break;
        if (paths[index].cost > paths.front().cost + 3000) continue;
        append(paths[index].text);
      }
    } catch (...) {
      // An optional model failure must not break the existing converter.
    }
  }
  const auto merge = [&](const std::vector<std::string>& fallback) {
    auto out = whole;
    // A complete dictionary reading must not acquire arbitrary concatenated
    // segment substitutions. The model's exact words and scored paths above
    // already cover lexical alternatives; Mozc's best sentence is a fallback.
    const size_t count = (std::min)(size_t{1}, fallback.size());
    for (size_t index = 0; index < count; ++index) {
      const auto& text = fallback[index];
      if (std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
    }
    return out;
  };
  if (!WriteAll(child->input, "start " + reading + "\n")) return whole;
  std::vector<std::vector<std::string>> segments;
  std::vector<std::vector<int>> costs;
  std::vector<std::string>* current = nullptr;
  bool saw_segment = false;
  while (true) {
    std::string line;
    if (!ReadLine(child->output, &child->buffered, &line)) return whole;
    if (IsSegmentHeader(line)) {
      saw_segment = true;
      segments.emplace_back();
      costs.emplace_back();
      current = &segments.back();
      continue;
    }
    if (saw_segment && line.empty()) break;
    if (!current) continue;
    std::string candidate;
    if (ParseCandidate(line, &candidate)) {
      current->push_back(candidate);costs.back().push_back(-1);
    } else if (native_only && !costs.back().empty()) {
      int cost;
      if (std::sscanf(line.c_str(), " cost: %d", &cost)==1) costs.back().back()=cost;
    }
  }
  if (segments.empty() ||
      std::any_of(segments.begin(), segments.end(),
                  [](const auto& segment) { return segment.empty(); }))
    return whole;

  std::vector<std::string> results;
  std::string top;
  for (const auto& segment : segments) top += segment.front();
  results.push_back(top);
  if (native_only) {
    results.clear();
    // Native alternatives of a single complete Mozc segment are genuine
    // context-scored candidates. Never form a Cartesian product of segments.
    if (segments.size()==1) {
      for(size_t i=0;i<segments[0].size();++i)
        results.push_back(segments[0][i]+'\x1d'+"MOZC:"+std::to_string(i)+":"+
            std::to_string(costs[0][i])+":1");
    } else {
      // Only the actual best complete path is available from this CLI for
      // multiple segments. Do not invent a whole-path cost by summing costs.
      results.push_back(top+'\x1d'+"MOZC:0:-1:"+std::to_string(segments.size()));
    }
    return results;
  }
  // Mozc's segment alternatives are not independently interchangeable.
  // Keep its best complete conversion; scored full paths supply alternatives.
  return merge(results);
}

int RunServer(ChildProcess* child, const RomanTable& table) {
  while (true) {
    HANDLE input_pipe = CreateNamedPipeW(
        kInputPipeName, PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0,
        nullptr);
    if (input_pipe == INVALID_HANDLE_VALUE) return 20;
    const bool input_connected = ConnectNamedPipe(input_pipe, nullptr) ||
                           GetLastError() == ERROR_PIPE_CONNECTED;
    if (!input_connected) {
      CloseHandle(input_pipe);
      continue;
    }
    HANDLE output_pipe = CreateNamedPipeW(
        kOutputPipeName, PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0,
        nullptr);
    if (output_pipe == INVALID_HANDLE_VALUE) {
      CloseHandle(input_pipe);
      return 21;
    }
    const bool output_connected = ConnectNamedPipe(output_pipe, nullptr) ||
                                  GetLastError() == ERROR_PIPE_CONNECTED;
    if (!output_connected) {
      CloseHandle(input_pipe);
      CloseHandle(output_pipe);
      continue;
    }
    std::string input_buffer;
    while (true) {
      std::string reading;
      if (!ReadLine(input_pipe, &input_buffer, &reading)) break;
      const auto candidates = Convert(child, RomanToHiragana(reading, table));
      std::string response = candidates.empty() ? "ERR" : "OK";
      for (const auto& candidate : candidates) {
        response.push_back('\t');
        response += candidate;
      }
      response.push_back('\n');
      if (!WriteAll(output_pipe, response)) break;
      FlushFileBuffers(output_pipe);
    }
    DisconnectNamedPipe(input_pipe);
    DisconnectNamedPipe(output_pipe);
    CloseHandle(input_pipe);
    CloseHandle(output_pipe);
  }
}

int RunMailbox(ChildProcess* child, const RomanTable& table,
               const std::wstring& mailbox) {
  CreateDirectoryW(mailbox.c_str(), nullptr);
  const std::wstring ready_path = mailbox + L"\\bridge.ready";
  ULONGLONG last_heartbeat = 0;
  while (true) {
    const ULONGLONG now = GetTickCount64();
    if (now - last_heartbeat >= 1000) {
      std::ofstream ready(ready_path, std::ios::binary | std::ios::trunc);
      ready << "ready\n";
      last_heartbeat = now;
    }
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((mailbox + L"\\request-*.tsv").c_str(),
                                   &found);
    if (search != INVALID_HANDLE_VALUE) {
      // Typing one long sentence creates a request for every prefix.  The old
      // loop converted every stale prefix in filesystem order, so the final
      // request could remain behind dozens of obsolete conversions.  Only the
      // newest request can affect the currently visible composition; discard
      // older requests before calling Mozc.
      std::vector<WIN32_FIND_DATAW> pending;
      do {
        pending.push_back(found);
      } while (FindNextFileW(search, &found));
      FindClose(search);

      std::sort(pending.begin(), pending.end(),
                [](const WIN32_FIND_DATAW& left,
                   const WIN32_FIND_DATAW& right) {
                  return CompareFileTime(&left.ftLastWriteTime,
                                         &right.ftLastWriteTime) > 0;
                });
      for (size_t index = 1; index < pending.size(); ++index) {
        const std::wstring stale_request_path =
            mailbox + L"\\" + pending[index].cFileName;
        std::ifstream stale_request(stale_request_path, std::ios::binary);
        std::string stale_id;
        std::getline(stale_request, stale_id);
        stale_request.close();
        if (!stale_id.empty() && stale_id.back() == '\r') stale_id.pop_back();
        if (!stale_id.empty()) {
          const std::wstring stale_id_w(stale_id.begin(), stale_id.end());
          const std::wstring stale_response_path =
              mailbox + L"\\response-" + stale_id_w + L".tsv";
          const std::wstring stale_response_temp =
              mailbox + L"\\response-" + stale_id_w + L".tmp";
          {
            std::ofstream response(stale_response_temp,
                                   std::ios::binary | std::ios::trunc);
            response << stale_id << "\tSTALE\n";
          }
          MoveFileExW(stale_response_temp.c_str(), stale_response_path.c_str(),
                      MOVEFILE_REPLACE_EXISTING);
        }
        DeleteFileW(stale_request_path.c_str());
      }

      if (!pending.empty()) {
        const std::wstring request_path =
            mailbox + L"\\" + pending.front().cFileName;
        std::string request_data;
        HANDLE request_file = CreateFileW(
            request_path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (request_file != INVALID_HANDLE_VALUE) {
          char chunk[4096];
          DWORD read = 0;
          while (ReadFile(request_file, chunk, sizeof(chunk), &read, nullptr) &&
                 read > 0)
            request_data.append(chunk, read);
          CloseHandle(request_file);

          const auto first_newline = request_data.find('\n');
          const auto second_newline =
              first_newline == std::string::npos
                  ? std::string::npos
                  : request_data.find('\n', first_newline + 1);
          if (first_newline != std::string::npos &&
              second_newline != std::string::npos) {
            std::string request_id = request_data.substr(0, first_newline);
            std::string input = request_data.substr(
                first_newline + 1, second_newline - first_newline - 1);
            if (!request_id.empty() && request_id.back() == '\r')
              request_id.pop_back();
            if (!input.empty() && input.back() == '\r') input.pop_back();

            if (!request_id.empty()) {
              // Imported key tables are decoded by Rime before conversion.
              // Keep their kana intact: standard q/nn normalization belongs
              // only to the default romanization route.
              const bool native_only=input.rfind("MOZC_KANA:",0)==0;
              const std::string reading = native_only ? input.substr(10) : input.rfind("KANA:", 0) == 0
                  ? input.substr(5) : RomanToHiragana(input, table);
              const auto candidates = input.rfind("MOZC_EDIT:",0)==0 ? EditConversion(child,input.substr(10)) :
                  Convert(child, reading, input.rfind("KANA:", 0) == 0, native_only);
              const std::wstring request_id_w(request_id.begin(),
                                               request_id.end());
              const std::wstring response_path =
                  mailbox + L"\\response-" + request_id_w + L".tsv";
              const std::wstring response_temp =
                  mailbox + L"\\response-" + request_id_w + L".tmp";
              {
                std::ofstream response(response_temp,
                                       std::ios::binary | std::ios::trunc);
                response << request_id
                         << (candidates.empty() ? "\tERR" : "\tOK");
                for (const auto& candidate : candidates)
                  response << '\t' << candidate;
                response << '\n';
              }
              MoveFileExW(response_temp.c_str(), response_path.c_str(),
                          MOVEFILE_REPLACE_EXISTING);
            }
          }
        }
        DeleteFileW(request_path.c_str());
      }
    }
    Sleep(1);
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 6 && argc != 7) {
    std::wcerr << L"usage: MozcBridge.exe <converter_main.exe> <runfiles> <profile> <romanji-hiragana.tsv> <mailbox> [sentence-model]\n";
    return 2;
  }
  const auto table = LoadRomanTable(argv[4]);
  if (table.empty()) return 4;
  try {
    sentence_decoder = std::make_unique<japanese::JapaneseSentenceDecoder>(
        Utf8Path(argc == 7 ? std::wstring(argv[6]) : std::wstring(argv[2]) + L"\\dict_japanese.dat"));
    std::cerr << "MSIME_WHOLE_SENTENCE=" << (sentence_decoder->ready() ? "ready" : "fallback") << "\n";
  } catch (...) {
    sentence_decoder.reset();
  }
  ChildProcess child;
  if (!StartConverter(argv[1], argv[2], argv[3], &child)) {
    std::wcerr << L"failed to start Mozc converter: " << GetLastError() << L"\n";
    return 3;
  }
  return RunMailbox(&child, table, argv[5]);
}
