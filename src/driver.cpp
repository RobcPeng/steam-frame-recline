// Experimental shared tracking backend. Inert unless explicitly armed before SteamVR starts.
#include "openvr_driver.h"
#include "callback_slot.hpp"
#include "engine.hpp"
#include "lease.hpp"
#include <filesystem>
#include <dlfcn.h>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
using Update=void(*)(void*,unsigned,const vr::DriverPose_t&,unsigned);
static std::mutex stateMutex;
static shared_recline::Adjustment adjustment;
static vr::DriverPose_t rawHead{};
static shared_recline::Clock::time_point headSeen{};
static std::atomic<Update> original{};static std::atomic<unsigned long> counts[vr::k_unMaxTrackedDeviceCount]{};
static void observe(void*host,unsigned id,const vr::DriverPose_t&pose,unsigned size){
 auto forward=original.load(std::memory_order_acquire);
 if(!forward)return;
 if(id<vr::k_unMaxTrackedDeviceCount)++counts[id];
 if(size!=sizeof(vr::DriverPose_t)){forward(host,id,pose,size);return;}
 vr::DriverPose_t out;
 {std::lock_guard<std::mutex> lock(stateMutex);auto now=shared_recline::Clock::now();
  if(id==vr::k_unTrackedDeviceIndex_Hmd){rawHead=pose;headSeen=now;}
  out=shared_recline::apply(pose,adjustment,now);
 }
 forward(host,id,out,size);
}
class Driver final:public vr::IServerTrackedDeviceProvider {
 void *modulePin{};bool initialized=false;shared_recline::Heartbeat heartbeat;vr::IVRDriverLog *log{};std::string path,controlPath;bool armed=false,captured=false,testOnly=true;long long sequence=-1;
 shared_recline::Clock::time_point testUntil=shared_recline::Clock::time_point::max();
 float pitch=0,height=0,yaw=0;XrVector3f pivot{};std::string message="Experimental backend is not armed.";
 std::chrono::steady_clock::time_point last{};
public:
 vr::EVRInitError Init(vr::IVRDriverContext*context)override{
  vr::EVRInitError e{};auto host=static_cast<vr::IVRServerDriverHost*>(context->GetGenericInterface("IVRServerDriverHost_006",&e));
  log=static_cast<vr::IVRDriverLog*>(context->GetGenericInterface(vr::IVRDriverLog_Version));
  const char*home=getenv("RECLINE_DATA_HOME");if(!home)home=getenv("HOME");
  if(!host||!log||!home)return vr::VRInitError_Driver_Failed;
  path=std::string(home)+"/.local/share/Steam/logs/recline-driver-status.txt";
  controlPath=std::string(home)+"/.config/openvr/config/recline-driver-control.txt";
  {std::ifstream arm(std::string(home)+"/.local/state/recline-mode/recline-shared.enable");armed=bool(arm);std::string mode;arm>>mode;testOnly=mode!="full";}
  {std::ifstream f(controlPath);f>>sequence;}

  if(initialized){log->Log("Restart SteamVR before reloading this experimental driver.");return vr::VRInitError_Driver_Failed;}
  vr::TrackedDevicePose_t existing[vr::k_unMaxTrackedDeviceCount]{};
  host->GetRawTrackedDevicePoses(0,existing,vr::k_unMaxTrackedDeviceCount);
  for(const auto &pose:existing)if(pose.bDeviceIsConnected){log->Log("Refusing to patch tracking after devices connect; restart SteamVR.");return vr::VRInitError_Driver_Failed;}
  Dl_info info{};
  if(!dladdr(reinterpret_cast<void*>(observe),&info)||!info.dli_fname)return vr::VRInitError_Driver_Failed;
  modulePin=dlopen(info.dli_fname,RTLD_NOW|RTLD_NOLOAD|RTLD_NODELETE);
  if(!modulePin){log->Log("Cannot pin tracking callback code for process lifetime.");return vr::VRInitError_Driver_Failed;}
  auto slot=&(*reinterpret_cast<void***>(host))[1];
  auto target=reinterpret_cast<Update>(__atomic_load_n(slot,__ATOMIC_ACQUIRE));
  original.store(target,std::memory_order_release);
  // Retain the callback code for process lifetime; no executable code patch,
  // trampoline allocation, or hot-uninstall is needed.
  initialized=true;
  auto result=shared_recline::installCallback(slot,reinterpret_cast<void*>(target),reinterpret_cast<void*>(observe));
  if(result!=shared_recline::SlotResult::installed){armed=false;log->Log("Callback installation failed or page protection could not be restored; restart SteamVR.");return vr::VRInitError_Driver_Failed;}
  log->Log("Recline shared pose callback installed. Adjustments require explicit local arming and a live panel.");return vr::VRInitError_None;
 }
 // Keep the pass-through callback code alive until vrserver exits.
 // Unloading can race with callbacks owned by other drivers.
 void Cleanup()override{std::lock_guard<std::mutex> lock(stateMutex);adjustment.active=false;armed=false;}

 const char *const *GetInterfaceVersions()override{static const char*v[]={"IVRServerDriverHost_006",nullptr};return v;}
 void RunFrame()override{
  auto now=shared_recline::Clock::now();if(now-last<std::chrono::milliseconds(20))return;last=now;
  std::ifstream f(controlPath);long long seq=0,stamp=0;std::string action;float p=0,h=0;
  bool command=bool(f>>seq>>action>>p>>h>>stamp)&&std::abs((long long)time(nullptr)-stamp)<=4&&seq>=sequence;
  long long issuedAt=0;bool hasIssueTime=bool(f>>issuedAt);
  int camera=1;bool cameraClear=bool(f>>camera)&&camera==0;
  auto currentMs=std::chrono::duration_cast<std::chrono::milliseconds>(shared_recline::Clock::now().time_since_epoch()).count();
  bool freshTest=hasIssueTime&&issuedAt>0&&currentMs>=issuedAt&&currentMs-issuedAt<=500;
  std::error_code fileError;auto changed=std::filesystem::last_write_time(controlPath,fileError);
  if(command&&!fileError)heartbeat.observe(changed.time_since_epoch().count(),now);
  auto deadline=heartbeat.deadline(now);
  command=command&&!fileError&&now<deadline;
  bool ready=false,active=false; bool valid=false,connected=false;long long age=0;XrPosef raw{},expected{};
  {std::lock_guard<std::mutex> lock(stateMutex);
   adjustment.suspended=!testOnly&&!cameraClear;
   if(shared_recline::latchExpired(adjustment,now)){captured=false;pitch=height=0;message=now>=testUntil?"Test finished. Upright restored.":"Tracking paused. Upright restored.";}
   if(command&&seq>sequence&&action=="reset"){sequence=seq;adjustment.active=false;captured=false;pitch=height=0;message="Upright restored.";}
   ready=armed&&rawHead.poseIsValid&&rawHead.deviceIsConnected&&now-headSeen<std::chrono::milliseconds(500);
   if(!ready||!command){
    if(command&&seq>sequence)sequence=seq;
    adjustment.active=false;captured=false;pitch=height=0;message=armed?"Waiting for tracking and Recline panel.":"Experimental backend is not armed.";
   }
   else {
    if(seq>sequence){sequence=seq;
     if(action=="reset"){adjustment.active=false;captured=false;pitch=height=0;message="Upright restored.";}
     else if(!std::isfinite(p)||!std::isfinite(h)||p < -1||p>90||h < -1||h>1){message="Invalid adjustment rejected.";}
     else if((action=="test"||action=="recenter"||action=="set")&&!freshTest){message="Adjustment request expired. Try again.";}
     else if(testOnly&&action!="test"){message="Test build: use the brief 5 degree test.";}
     else if(action=="recenter"||action=="set"||action=="test"){
      bool canApply=captured;
      if(action=="recenter"||action=="test"){
       auto head=shared_recline::head(rawHead);pivot=head.position;auto forward=recline::rotate(head.orientation,{0,0,-1});
       if(std::hypot(forward.x,forward.z)>.05f)yaw=std::atan2(-forward.x,-forward.z);
       if(p<0)p=std::clamp(float(std::asin(std::clamp(forward.y,-1.f,1.f))*180/3.141592653589793),0.f,90.f);
       captured=canApply=true;
      }
      if(action=="test"){p=5;h=0;testUntil=now+std::chrono::seconds(2);}
      else testUntil=shared_recline::Clock::time_point::max();
      if(canApply&&p>=0){pitch=p;height=h;adjustment.transform=recline::inverse(recline::offset(pivot,yaw,p*float(3.141592653589793/180),h));adjustment.active=true;message="Experimental shared tilt active.";}
      else message="Choose Recenter here first.";
     }
    }
    adjustment.expires=std::min(deadline,testUntil);
   }
   active=adjustment.active;valid=rawHead.poseIsValid;connected=rawHead.deviceIsConnected;age=std::chrono::duration_cast<std::chrono::milliseconds>(now-headSeen).count();
   raw=shared_recline::head(rawHead);expected=shared_recline::head(shared_recline::apply(rawHead,adjustment,now));
  }
  // Status is replaced atomically; the tracking callback performs no file I/O.
  std::ofstream out(path+".tmp");out<<time(nullptr)<<" "<<ready<<" "<<active<<" "<<pitch<<" "<<height<<" "<<sequence<<" "<<int(adjustment.suspended)<<"\n"<<message<<"\n";out<<"tracking "<<valid<<" "<<connected<<" "<<age<<" counts";for(unsigned i=0;i<vr::k_unMaxTrackedDeviceCount;++i){auto n=counts[i].load();if(n)out<<" "<<i<<":"<<n;}out<<"\n";
  for(auto v:{raw,expected})out<<v.orientation.x<<" "<<v.orientation.y<<" "<<v.orientation.z<<" "<<v.orientation.w<<" "<<v.position.x<<" "<<v.position.y<<" "<<v.position.z<<"\n";
  out.close();if(out)rename((path+".tmp").c_str(),path.c_str());
 }

 bool ShouldBlockStandbyMode()override{return false;}void EnterStandby()override{}void LeaveStandby()override{}
};
static Driver driver;
extern "C" __attribute__((visibility("default"))) void*HmdDriverFactory(const char*n,int*e){if(n&&!strcmp(n,vr::IServerTrackedDeviceProvider_Version))return &driver;if(e)*e=vr::VRInitError_Init_InterfaceNotFound;return nullptr;}
