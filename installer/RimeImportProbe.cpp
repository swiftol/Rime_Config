#include <windows.h>
#include <iostream>
#include <string>

static std::string Utf8(const std::wstring& value) {
  if (value.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string result(size - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
  return result;
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) { std::cerr << "usage: RimeImportProbe <rime.dll>\n"; return 64; }
  HMODULE image = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (!image) { std::cerr << "MAP_IMAGE_FAILED=" << GetLastError() << "\n"; return 65; }
  auto base = reinterpret_cast<unsigned char*>(image);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { std::cerr << "BAD_DOS_HEADER\n"; return 66; }
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    std::cerr << "BAD_OR_NON_X64_PE\n"; return 67;
  }
  auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
  int missingDlls = 0, missingImports = 0, checkedImports = 0;
  for (; imports->Name; ++imports) {
    const char* dllName = reinterpret_cast<const char*>(base + imports->Name);
    HMODULE dependency = LoadLibraryExA(dllName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dependency) dependency = LoadLibraryA(dllName);
    if (!dependency) {
      std::cout << "MISSING_DLL=" << dllName << ";ERROR=" << GetLastError() << "\n";
      ++missingDlls; continue;
    }
    auto thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base +
        (imports->OriginalFirstThunk ? imports->OriginalFirstThunk : imports->FirstThunk));
    for (; thunk->u1.AddressOfData; ++thunk) {
      FARPROC address = nullptr;
      if (IMAGE_SNAP_BY_ORDINAL64(thunk->u1.Ordinal)) {
        auto ordinal = static_cast<WORD>(IMAGE_ORDINAL64(thunk->u1.Ordinal));
        address = GetProcAddress(dependency, reinterpret_cast<const char*>(static_cast<uintptr_t>(ordinal)));
        if (!address) { std::cout << "MISSING_ORDINAL=" << dllName << "!" << ordinal << "\n"; ++missingImports; }
      } else {
        auto item = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
        const char* name = reinterpret_cast<const char*>(item->Name);
        address = GetProcAddress(dependency, name);
        if (!address) { std::cout << "MISSING_IMPORT=" << dllName << "!" << name << "\n"; ++missingImports; }
      }
      ++checkedImports;
    }
    FreeLibrary(dependency);
  }
  FreeLibrary(image);
  std::cout << "CHECKED_IMPORTS=" << checkedImports << "\nMISSING_DLLS=" << missingDlls
            << "\nMISSING_IMPORTS=" << missingImports << "\n";
  return (missingDlls || missingImports) ? 1 : 0;
}
