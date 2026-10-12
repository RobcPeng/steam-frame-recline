#include "openvr_driver.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
using namespace std::chrono_literals;
struct Host:vr::IVRServerDriverHost {
 vr::DriverPose_t delivered[2]{};
 bool TrackedDeviceAdded(const char*,vr::ETrackedDeviceClass,vr::ITrackedDeviceServerDriver*)override{return false;}
 void TrackedDevicePoseUpdated(uint32_t id,const vr::DriverPose_t&p,uint32_t size)override{assert(size==sizeof(p));if(id<2)delivered[id]=p;}
 void VsyncEvent(double)override{}
 void VendorSpecificEvent(uint32_t,vr::EVREventType,const vr::VREvent_Data_t&,double)override{}
 bool IsExiting()override{return false;}
 bool PollNextEvent(vr::VREvent_t*,uint32_t)override{return false;}
 void GetRawTrackedDevicePoses(float,vr::TrackedDevicePose_t*p,uint32_t count)override{for(unsigned i=0;i<count;++i)p[i]={};}
 void RequestRestart(const char*,const char*,const char*,const char*)override{assert(false);}
 uint32_t GetFrameTimings(vr::Compositor_FrameTiming*,uint32_t)override{return 0;}
 void SetDisplayEyeToHead(uint32_t,const vr::HmdMatrix34_t&,const vr::HmdMatrix34_t&)override{}
 void SetDisplayProjectionRaw(uint32_t,const vr::HmdRect2_t&,const vr::HmdRect2_t&)override{}
 void SetRecommendedRenderTargetSize(uint32_t,uint32_t,uint32_t)override{}
};
struct Logger:vr::IVRDriverLog {void Log(const char*s)override{std::cout<<s<<'\n';}};
struct Context:vr::IVRDriverContext {
 Host host;Logger log;
 void*GetGenericInterface(const char*n,vr::EVRInitError*e=nullptr)override{
  if(e)*e=vr::VRInitError_None;
  if(!strcmp(n,vr::IVRServerDriverHost_Version))return &host;
  if(!strcmp(n,vr::IVRDriverLog_Version))return &log;
  return nullptr;
 }
 vr::DriverHandle_t GetDriverHandle()override{return 1;}
};
__attribute__((noinline)) static void submit(vr::IVRServerDriverHost*host,unsigned id,const vr::DriverPose_t&p){host->TrackedDevicePoseUpdated(id,p,sizeof(p));}
static bool near(double a,double b){return std::abs(a-b)<1e-5;}
int main(int argc,char**argv){
 assert(argc==2||argc==3);bool full=argc==3;char directory[]="/tmp/recline-driver-test-XXXXXX";
 auto root=std::filesystem::path(mkdtemp(directory));assert(!root.empty());
 struct Cleanup{std::filesystem::path root;~Cleanup(){std::filesystem::remove_all(root);}} cleanup{root};
 auto config=root/".config/openvr/config";auto state=root/".local/state/recline-mode";
 std::filesystem::create_directories(config);std::filesystem::create_directories(state);
 std::filesystem::create_directories(root/".local/share/Steam/logs");
 std::ofstream(state/"recline-shared.enable")<<(full?"full\n":"test\n");
 setenv("RECLINE_DATA_HOME",root.c_str(),1);
 auto library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!library)std::cerr<<dlerror()<<'\n';assert(library);
 using Factory=void*(*)(const char*,int*);auto factory=reinterpret_cast<Factory>(dlsym(library,"HmdDriverFactory"));assert(factory);
 int error{};auto driver=static_cast<vr::IServerTrackedDeviceProvider*>(factory(vr::IServerTrackedDeviceProvider_Version,&error));assert(driver);
 Context context;assert(driver->Init(&context)==vr::VRInitError_None);
 vr::DriverPose_t head{};head.poseIsValid=head.deviceIsConnected=true;
 head.qWorldFromDriverRotation.w=head.qDriverFromHeadRotation.w=head.qRotation.w=1;head.vecPosition[1]=1.5;
 auto controller=head;controller.vecPosition[0]=.3;controller.vecPosition[1]=1.2;
 bool camera=false;
 long long sequence=0,publishedSequence=-1,issuedAt=0;
 auto publish=[&](const char*action,double pitch,double height=0){
  if(sequence!=publishedSequence){issuedAt=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();publishedSequence=sequence;}
  auto temp=config/"control.tmp";std::ofstream(temp)<<sequence<<' '<<action<<' '<<pitch<<" "<<height<<" "<<time(nullptr)<<' '<<issuedAt<<' '<<int(camera)<<'\n';std::filesystem::rename(temp,config/"recline-driver-control.txt");
 };
 auto tick=[&]{submit(&context.host,0,head);driver->RunFrame();submit(&context.host,0,head);submit(&context.host,1,controller);std::this_thread::sleep_for(30ms);};
 auto upright=[&]{assert(near(context.host.delivered[0].qWorldFromDriverRotation.x,0));assert(near(context.host.delivered[0].qWorldFromDriverRotation.w,1));};
 auto tilted=[&]{assert(near(context.host.delivered[0].qWorldFromDriverRotation.x,-std::sin(5*M_PI/360)));assert(near(context.host.delivered[1].qWorldFromDriverRotation.x,context.host.delivered[0].qWorldFromDriverRotation.x));};
 ++sequence;publish("reset",0);tick();tick();upright();
 // A request rejected while tracking is lost must not start on reacquisition.
 head.poseIsValid=false;++sequence;publish("test",5);tick();upright();
 head.poseIsValid=true;publish("test",5);tick();upright();
 // Continuing heartbeats must not keep an unprocessed start request alive.
 ++sequence;publish("test",5);std::this_thread::sleep_for(600ms);publish("test",5);tick();upright();
 if(full){
  // Unprocessed normal adjustments must expire even if the panel resumes.
  ++sequence;publish("recenter",45);std::this_thread::sleep_for(600ms);publish("recenter",45);tick();upright();
  ++sequence;publish("recenter",45);tick();
  assert(near(context.host.delivered[0].qWorldFromDriverRotation.x,-std::sin(45*M_PI/360)));
  // Malformed/missing state must never authorize camera tilt.
  for(const char* flag:{"", "unknown"}){
   auto temp=config/"control.tmp";
   std::ofstream(temp)<<sequence<<" recenter 45 0 "<<time(nullptr)<<' '<<issuedAt<<' '<<flag<<'\n';
   std::filesystem::rename(temp,config/"recline-driver-control.txt");tick();upright();
  }
  camera=true;publish("recenter",45);tick();upright();
  camera=false;publish("recenter",45);tick();
  assert(near(context.host.delivered[0].qWorldFromDriverRotation.x,-std::sin(45*M_PI/360)));
  auto end=std::chrono::steady_clock::now()+2300ms;
  while(std::chrono::steady_clock::now()<end){publish("recenter",45);tick();}
  assert(near(context.host.delivered[0].qWorldFromDriverRotation.x,-std::sin(45*M_PI/360)));
  ++sequence;publish("set",90);tick();assert(near(context.host.delivered[0].qWorldFromDriverRotation.x,-std::sin(M_PI/4)));
  ++sequence;publish("reset",0);tick();upright();
  // Automatic recenter from a yawed, reclining raw head; height lifts its pivot.
  const double yaw=.7, tilt=.6;
  head.qRotation={std::cos(yaw/2)*std::cos(tilt/2),std::cos(yaw/2)*std::sin(tilt/2),std::sin(yaw/2)*std::cos(tilt/2),-std::sin(yaw/2)*std::sin(tilt/2)};
  ++sequence;publish("recenter",-1,.25);tick();
  auto world=context.host.delivered[0].qWorldFromDriverRotation;
  assert(near(world.w,std::cos(tilt/2)));
  assert(near(world.x,-std::cos(yaw)*std::sin(tilt/2)));
  assert(near(world.z,std::sin(yaw)*std::sin(tilt/2)));
  // Rotate the raw head position by the delivered world quaternion.
  const double rotatedY=1.5*(1-2*(world.x*world.x+world.z*world.z));
  assert(near(rotatedY+context.host.delivered[0].vecWorldFromDriverTranslation[1],1.75));
  auto end2=std::chrono::steady_clock::now()+1200ms;while(std::chrono::steady_clock::now()<end2)tick();upright();
  publish("recenter",-1,.25);tick();upright();
  head.qRotation={1,0,0,0};
  ++sequence;publish("reset",0);tick();upright();
  std::cout<<"PASS: full controls, auto recenter, yaw, height, persistent angle and reset\n";
 }
 // Test mode must refuse unrestricted recenter commands.
 if(!full){++sequence;publish("recenter",90);tick();upright();}
 ++sequence;publish("test",5);tick();tilted();
 // A healthy, continuing heartbeat must not extend the two-second test.
 auto until=std::chrono::steady_clock::now()+2300ms;
 while(std::chrono::steady_clock::now()<until){publish("test",5);tick();}
 upright();
 ++sequence;publish("test",5);tick();tilted();
 ++sequence;publish("reset",0);tick();upright();
 ++sequence;publish("test",5);tick();tilted();
 // No fresh heartbeat: the actual driver must stop, not just the math helper.
 until=std::chrono::steady_clock::now()+1200ms;while(std::chrono::steady_clock::now()<until)tick();upright();
 publish("test",5);tick();upright();
 ++sequence;publish("test",5);tick();tilted();
 // The tracking callback independently expires if the server loop stalls.
 std::this_thread::sleep_for(600ms);submit(&context.host,0,head);upright();
 publish("test",5);tick();upright();
 ++sequence;publish("test",5);tick();tilted();
 driver->Cleanup();submit(&context.host,0,head);upright();
 dlclose(library);submit(&context.host,0,head);upright();
 std::cout<<"PASS: actual driver test limit, reset, dead-panel reset, service-stall reset, cleanup and retained callback\n";
}
