#pragma once
#include "openvr_driver.h"
#include "pose.hpp"
#include <chrono>
namespace shared_recline {
using Clock=std::chrono::steady_clock;
struct Adjustment { bool active=false; bool suspended=false; XrPosef transform=recline::identity(); Clock::time_point expires{}; };
inline XrQuaternionf quat(vr::HmdQuaternion_t q){return {float(q.x),float(q.y),float(q.z),float(q.w)};}
inline XrPosef world(const vr::DriverPose_t&p){return {quat(p.qWorldFromDriverRotation),{float(p.vecWorldFromDriverTranslation[0]),float(p.vecWorldFromDriverTranslation[1]),float(p.vecWorldFromDriverTranslation[2])}};}
inline XrPosef head(const vr::DriverPose_t&p){return recline::compose(recline::compose(world(p),{quat(p.qRotation),{float(p.vecPosition[0]),float(p.vecPosition[1]),float(p.vecPosition[2])}}),{quat(p.qDriverFromHeadRotation),{float(p.vecDriverFromHeadTranslation[0]),float(p.vecDriverFromHeadTranslation[1]),float(p.vecDriverFromHeadTranslation[2])}});}
inline vr::DriverPose_t apply(const vr::DriverPose_t &p,const Adjustment&a,Clock::time_point now){
 if(!a.active||a.suspended||now>=a.expires||!p.poseIsValid||!p.deviceIsConnected)return p;
 auto t=recline::compose(a.transform,world(p));auto out=p;
 out.qWorldFromDriverRotation={t.orientation.w,t.orientation.x,t.orientation.y,t.orientation.z};
 out.vecWorldFromDriverTranslation[0]=t.position.x;out.vecWorldFromDriverTranslation[1]=t.position.y;out.vecWorldFromDriverTranslation[2]=t.position.z;
 return out;
}
}
