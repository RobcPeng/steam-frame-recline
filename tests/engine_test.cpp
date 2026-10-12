#include "engine.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace shared_recline;
static bool near(double a,double b){return std::abs(a-b)<1e-5;}
static void samePose(XrPosef a,XrPosef b){
 assert(near(a.position.x,b.position.x)&&near(a.position.y,b.position.y)&&near(a.position.z,b.position.z));
 auto dot=a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w;
 assert(near(std::abs(dot),1));
}
static vr::HmdQuaternion_t native(XrQuaternionf q){return {q.w,q.x,q.y,q.z};}
int main(){
 vr::DriverPose_t p{};p.poseIsValid=p.deviceIsConnected=true;p.qWorldFromDriverRotation.w=p.qDriverFromHeadRotation.w=p.qRotation.w=1;p.vecPosition[0]=.25;p.vecPosition[1]=1.5;p.vecVelocity[2]=.4;
 auto now=Clock::now();Adjustment a;auto out=apply(p,a,now);assert(!memcmp(&p,&out,sizeof(p)));
 a.active=true;a.expires=now+std::chrono::seconds(2);a.transform=recline::inverse(recline::offset({0,1.5f,0},0,float(M_PI/2),0));
 out=apply(p,a,now);assert(near(out.qWorldFromDriverRotation.w,std::sqrt(.5)));assert(near(out.qWorldFromDriverRotation.x,-std::sqrt(.5)));
 // Predictor inputs remain in the driver's coordinate frame: the world transform carries the adjustment.
 assert(!memcmp(p.vecVelocity,out.vecVelocity,sizeof(p.vecVelocity)));assert(!memcmp(p.vecPosition,out.vecPosition,sizeof(p.vecPosition)));
 auto q=XrQuaternionf{float(out.qWorldFromDriverRotation.x),float(out.qWorldFromDriverRotation.y),float(out.qWorldFromDriverRotation.z),float(out.qWorldFromDriverRotation.w)};
 auto pivot=recline::rotate(q,{0,1.5f,0});assert(near(pivot.x+out.vecWorldFromDriverTranslation[0],0));assert(near(pivot.y+out.vecWorldFromDriverTranslation[1],1.5));assert(near(pivot.z+out.vecWorldFromDriverTranslation[2],0));
 out=apply(p,a,now+std::chrono::seconds(3));assert(!memcmp(&p,&out,sizeof(p)));
 a.active=false;out=apply(p,a,now);assert(!memcmp(&p,&out,sizeof(p)));
 a.active=true;p.poseIsValid=false;out=apply(p,a,now);assert(!memcmp(&p,&out,sizeof(p)));
 // Nonidentity driver/head spaces and a yawed pivot must preserve the entire
 // controller-to-head relationship under a common world adjustment.
 p.poseIsValid=true;p.qWorldFromDriverRotation=native(recline::axis(0,1,0,.7f));
 p.vecWorldFromDriverTranslation[0]=2;p.vecWorldFromDriverTranslation[2]=-3;
 p.qRotation=native(recline::axis(1,0,0,.3f));p.qDriverFromHeadRotation=native(recline::axis(0,0,1,.2f));
 p.vecDriverFromHeadTranslation[0]=.08;p.vecDriverFromHeadTranslation[2]=-.1;
 auto controller=p;controller.vecPosition[0]=.8;controller.vecPosition[1]=1.1;controller.vecPosition[2]=-.4;
 controller.qRotation=native(recline::axis(0,0,1,-.5f));
 auto beforeHead=head(p),beforeController=head(controller);
 a.transform=recline::inverse(recline::offset(beforeHead.position,1.1f,.6f,.2f));
 auto afterHead=head(apply(p,a,now)),afterController=head(apply(controller,a,now));
 samePose(afterHead,recline::compose(a.transform,beforeHead));
 samePose(afterController,recline::compose(a.transform,beforeController));
 samePose(recline::compose(recline::inverse(beforeHead),beforeController),recline::compose(recline::inverse(afterHead),afterController));
 // With no height offset the captured head position stays fixed for arbitrary yaw.
 a.transform=recline::inverse(recline::offset(beforeHead.position,1.1f,.6f,0));
 auto fixed=head(apply(p,a,now));assert(near(fixed.position.x,beforeHead.position.x)&&near(fixed.position.y,beforeHead.position.y)&&near(fixed.position.z,beforeHead.position.z));
 std::cout<<"PASS: tilt around head pivot, predictor input preservation, expiry, reset, invalid tracking\n";
 std::cout<<"PASS: nonidentity tracking spaces, yawed pivot, controller/head relative alignment\n";
}
