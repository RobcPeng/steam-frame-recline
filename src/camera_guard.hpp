#pragma once
#include "openvr.h"
#include <cstdint>
#include <cstring>
namespace recline {
// Private, read-only camera queries. Fail closed on an unrecognized runtime.
// These exact implementations were inspected in vrclient.so SHA256
// 19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0.
// Slot 7 copies shared camera-state byte 0 to a one-byte output; slot 11
// returns byte 1. Neither changes camera state. Live positive-visibility
// acceptance is required before calling this integration verified.
struct CameraGuard {
 void *instance=nullptr; void **methods=nullptr; bool known=false;
 CameraGuard(){
#if defined(__aarch64__)
  vr::EVRInitError e{};
  instance=vr::VR_GetGenericInterface("IVRCameraPassthroughInternal_001",&e);
  if(!instance||e!=vr::VRInitError_None)return;
  methods=*reinterpret_cast<void***>(instance);
  const uint32_t query[]={0xa9be7bfd,0x91006000,0x910003fd,0xf9000bf3,0xaa0103f3,0x9402d4f5,0xb4000073,0x39400000,0x39000260,0x52800000,0xf9400bf3,0xa8c27bfd,0xd65f03c0};
  const uint32_t rendering[]={0xa9bf7bfd,0x91006000,0x910003fd,0x9402d4e9,0x39400400,0xa8c17bfd,0xd65f03c0};
  known=methods&&methods[7]&&methods[11]&&std::memcmp(methods[7],query,sizeof(query))==0&&std::memcmp(methods[11],rendering,sizeof(rendering))==0;
#endif
 }
 bool blocked()const{
  if(!known)return true;
  uint8_t visible=255;
  int result=reinterpret_cast<int(*)(void*,uint8_t*)>(methods[7])(instance,&visible);
  int rendering=reinterpret_cast<int(*)(void*)>(methods[11])(instance);
  if(result||visible>1||rendering<0||rendering>1)return true;
  return visible||rendering||vr::VROverlay()->IsDashboardVisible();
 }
};
}
