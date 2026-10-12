#include "lease.hpp"
#include <cassert>
#include <iostream>
using namespace shared_recline;
int main(){
 auto now=Clock::now();Heartbeat hb;
 assert(hb.observe(100,now));assert(!hb.observe(100,now+std::chrono::milliseconds(300)));
 assert(hb.deadline(now)==now+std::chrono::milliseconds(500));
 assert(hb.deadline(now+std::chrono::milliseconds(800))==now+std::chrono::seconds(1));
 Adjustment a;a.active=true;a.expires=hb.deadline(now);
 assert(latchExpired(a,now+std::chrono::milliseconds(501)));assert(!a.active);
 // The panel can resume its heartbeat without a new adjustment; this must not reactivate tilt.
 hb.observe(101,now+std::chrono::milliseconds(600));a.expires=hb.deadline(now+std::chrono::milliseconds(600));assert(!a.active);
 a.active=true;a.expires=now;assert(latchExpired(a,now));assert(!a.active);
 std::cout<<"PASS: heartbeat progress, dead-panel deadline, latched expiration across resumed heartbeats\n";
}
