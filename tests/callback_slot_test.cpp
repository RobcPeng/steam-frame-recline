#include "callback_slot.hpp"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
#include <iostream>
#include <cstring>
struct Host {virtual void other(){};virtual unsigned update(unsigned x);};
static std::atomic<unsigned long> delivered{},intercepted{};
__attribute__((noinline)) unsigned Host::update(unsigned x){++delivered;return x+7;}
using Update=unsigned(*)(void*,unsigned);static std::atomic<Update> original{};
static unsigned observe(void*h,unsigned x){auto forward=original.load(std::memory_order_acquire);assert(forward);++intercepted;return forward(h,x);}
__attribute__((noinline)) unsigned invoke(Host *h,unsigned x){return h->update(x);}
int main(){
 Host host;auto slot=&(*reinterpret_cast<void***>(&host))[1];
 auto target=reinterpret_cast<Update>(*slot);original.store(target,std::memory_order_release);
 unsigned char code[16];std::memcpy(code,reinterpret_cast<void*>(target),sizeof(code));
 assert(shared_recline::installCallback(nullptr,nullptr,nullptr)==shared_recline::SlotResult::unchanged);
 assert(shared_recline::installCallback(slot,reinterpret_cast<void*>(observe),reinterpret_cast<void*>(observe))==shared_recline::SlotResult::unchanged);
 std::atomic<bool> stop{};std::atomic<unsigned long> calls{};
 std::vector<std::thread> workers;
 for(int i=0;i<4;++i)workers.emplace_back([&]{while(!stop){assert(invoke(&host,31)==38);++calls;}});
 while(calls<1000)std::this_thread::yield();
 shared_recline::SlotResult results[2]{};
 std::thread first([&]{results[0]=shared_recline::installCallback(slot,reinterpret_cast<void*>(target),reinterpret_cast<void*>(observe));});
 std::thread second([&]{results[1]=shared_recline::installCallback(slot,reinterpret_cast<void*>(target),reinterpret_cast<void*>(observe));});
 first.join();second.join();
 assert((results[0]==shared_recline::SlotResult::installed&&results[1]==shared_recline::SlotResult::unchanged)||(results[1]==shared_recline::SlotResult::installed&&results[0]==shared_recline::SlotResult::unchanged));
 while(intercepted<10000)std::this_thread::yield();
 stop=true;for(auto&t:workers)t.join();
 assert(delivered==calls);assert(intercepted>0);
 assert(!std::memcmp(code,reinterpret_cast<void*>(target),sizeof(code)));
 assert(shared_recline::installCallback(slot,reinterpret_cast<void*>(target),reinterpret_cast<void*>(observe))==shared_recline::SlotResult::unchanged);
 auto n=intercepted.load();assert(invoke(&host,9)==16);assert(intercepted==n+1);
 std::cout<<"PASS: concurrent callback swap, original code unchanged, exact forwarding, conflict refusal\n";
}
