#pragma once
#include "rime_api.h"
#include <string>

// One held physical key is one logical insertion. Don't run a complete
// candidate translation for each private encoder byte (e.g. A, then Af).
// Preserve the existing processor path when a converted prefix/caret edit
// must not be reconstructed through Context::set_input.
inline bool JapaneseAppendHoldComposition(RimeApi* api,RimeSessionId session,
                                          const std::string& spelling) {
  const char* raw=api->get_input(session);
  const std::string original=raw?raw:"";
  const size_t caret=api->get_caret_pos(session);
  RIME_STRUCT(RimeContext,context);
  const bool got_context=api->get_context(session,&context);
  const bool unconverted=original.empty() || (got_context && context.composition.sel_start==0);
  if(got_context)api->free_context(&context);
  if(unconverted && caret==original.size()) {
    const std::string input=original+spelling;
    api->set_input(session,input.c_str());
    return true;
  }
  for(unsigned char ch:spelling)api->process_key(session,ch,ch>='A' && ch<='Z'?1:0);
  return false;
}
