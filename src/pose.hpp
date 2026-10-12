#pragma once
#include <cmath>
#include "openxr/openxr.h"
namespace recline {
inline XrQuaternionf mul(XrQuaternionf a,XrQuaternionf b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline XrVector3f rotate(XrQuaternionf q,XrVector3f p){auto r=mul(mul(q,{p.x,p.y,p.z,0}),{-q.x,-q.y,-q.z,q.w});return {r.x,r.y,r.z};}
inline XrPosef identity(){return {{0,0,0,1},{0,0,0}};}
inline XrPosef compose(XrPosef a,XrPosef b){auto p=rotate(a.orientation,b.position);return {mul(a.orientation,b.orientation),{p.x+a.position.x,p.y+a.position.y,p.z+a.position.z}};}
inline XrPosef inverse(XrPosef a){auto q=a.orientation;q.x=-q.x;q.y=-q.y;q.z=-q.z;return {q,rotate(q,{-a.position.x,-a.position.y,-a.position.z})};}
inline XrQuaternionf axis(float x,float y,float z,float angle){float s=sin(angle/2);return {x*s,y*s,z*s,float(cos(angle/2))};}
inline XrPosef offset(XrVector3f pivot,float yaw,float pitch,float height){auto q=mul(mul(axis(0,1,0,yaw),axis(1,0,0,pitch)),axis(0,1,0,-yaw));auto r=rotate(q,pivot);auto up=rotate(q,{0,1,0});return {q,{pivot.x-r.x-height*up.x,pivot.y-r.y-height*up.y,pivot.z-r.z-height*up.z}};}
}
