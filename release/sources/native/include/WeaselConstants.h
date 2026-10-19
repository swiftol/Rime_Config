#pragma once

#define WEASEL_CODE_NAME "Zhongri"
#define WEASEL_REG_KEY L"Software\\ZhongriInputMethod"
#define RIME_REG_KEY L"Software\\ZhongriInputMethod"

#define STRINGIZE(x) #x
#define VERSION_STR(x) STRINGIZE(x)
#define WEASEL_VERSION VERSION_STR(VERSION_MAJOR.VERSION_MINOR.VERSION_PATCH)
