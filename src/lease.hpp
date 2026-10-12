#pragma once
#include "engine.hpp"
#include <algorithm>
namespace shared_recline {
struct Heartbeat {
 bool seen=false;long long token=0;Clock::time_point progressed{};
 bool observe(long long value,Clock::time_point now){if(seen&&value==token)return false;seen=true;token=value;progressed=now;return true;}
 Clock::time_point deadline(Clock::time_point now)const{return std::min(progressed+std::chrono::seconds(1),now+std::chrono::milliseconds(500));}
};
inline bool latchExpired(Adjustment&a,Clock::time_point now){if(a.active&&now>=a.expires){a.active=false;return true;}return false;}
}
