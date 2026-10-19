#pragma once
#include <mutex>
#include <utility>

// A message thread must keep pumping when the request owner might be waiting
// for a synchronous window call. A busy timer tick is deferred, never queued
// behind that owner while blocking the message pump.
template<class Callback>
bool TrySerializedCallback(std::mutex& mutex, Callback&& callback) {
  std::unique_lock<std::mutex> guard(mutex,std::try_to_lock);
  if(!guard.owns_lock())return false;
  std::forward<Callback>(callback)();
  return true;
}
