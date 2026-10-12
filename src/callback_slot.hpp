#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
namespace shared_recline {
enum class SlotResult { unchanged, installed, installedWritable };
// Linux ABI-specific: replace one aligned dispatch pointer, never executable
// instructions. The caller must pin both functions for the process lifetime,
// publish its original function pointer first, and never hot-uninstall.
// Installs from this module are serialized. Other modules must not concurrently
// modify protections or dispatch pointers on the same page.
inline SlotResult installCallback(void **slot,void *expected,void *replacement){
 static std::mutex installationMutex;
 std::lock_guard<std::mutex> installationLock(installationMutex);
 auto address=reinterpret_cast<std::uintptr_t>(slot);
 if(!slot||!expected||!replacement||address%alignof(void*))return SlotResult::unchanged;
 static_assert(__atomic_always_lock_free(sizeof(void*),nullptr));
 const long pageSize=sysconf(_SC_PAGESIZE);
 if(pageSize<=0)return SlotResult::unchanged;
 std::ifstream maps("/proc/self/maps");std::string line;int protection=-1;
 while(std::getline(maps,line)){
  unsigned long begin{},end{};char mode[5]{};
  if(std::sscanf(line.c_str(),"%lx-%lx %4s",&begin,&end,mode)!=3)continue;
  if(address>=begin&&address+sizeof(void*)<=end){
   // A vtable belongs in readable private data, not executable memory.
   if(mode[0]!='r'||mode[2]=='x'||mode[3]!='p')return SlotResult::unchanged;
   protection=PROT_READ|(mode[1]=='w'?PROT_WRITE:0);break;
  }
 }
 if(protection<0||__atomic_load_n(slot,__ATOMIC_ACQUIRE)!=expected)return SlotResult::unchanged;
 auto page=reinterpret_cast<void*>(address-address%static_cast<std::uintptr_t>(pageSize));
 if(mprotect(page,pageSize,protection|PROT_WRITE))return SlotResult::unchanged;
 bool changed=__atomic_compare_exchange_n(slot,&expected,replacement,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
 bool restored=mprotect(page,pageSize,protection)==0;
 if(!changed)return SlotResult::unchanged;
 return restored?SlotResult::installed:SlotResult::installedWritable;
}
}
