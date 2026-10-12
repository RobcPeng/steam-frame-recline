#include "panel.hpp"
#include "openvr.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
int main(int argc, char **argv) {
  try {
    std::string cmd = argc > 1 ? argv[1] : "panel";
    if (cmd == "--help") { std::cout << "Recline Mode [panel|status|restore|smoke]\n"; return 0; }
    if (cmd != "panel" && cmd != "status" && cmd != "restore" && cmd != "smoke")
      throw std::runtime_error("Unknown command; use --help");
    const char *home = std::getenv("HOME");
    if (!home) throw std::runtime_error("HOME is unavailable");
    auto root = std::filesystem::path(home) / ".local/state/recline-mode";
    std::filesystem::create_directories(root); chmod(root.c_str(),0700);
    if (cmd == "status") {
      std::ifstream f(std::filesystem::path(home)/".local/share/Steam/logs/recline-driver-status.txt");
      if (!f) throw std::runtime_error("No VR game has connected yet");
      std::cout << f.rdbuf(); return 0;
    }
    auto sockPath = (root/"control.sock").string();
    if (sockPath.size() >= sizeof(sockaddr_un::sun_path)) throw std::runtime_error("Control socket path too long");
    sockaddr_un address{}; address.sun_family=AF_UNIX; std::strcpy(address.sun_path,sockPath.c_str());
    if (cmd == "restore") {
      int fd=socket(AF_UNIX,SOCK_DGRAM,0);
      auto sent=sendto(fd,"restore",7,0,reinterpret_cast<sockaddr*>(&address),sizeof(address)); close(fd);
      if (sent != 7) {
        auto path=std::filesystem::path(home)/".config/openvr/config/recline-driver-control.txt";
        auto tmp=path.string()+".tmp"; std::ofstream f(tmp);
        f << std::chrono::system_clock::now().time_since_epoch().count() << " reset 0 0 " << std::time(nullptr) << "\n";
        f.close(); if (!f || std::rename(tmp.c_str(),path.c_str())) throw std::runtime_error("Cannot request reset");
      }
      std::cout << "Upright reset requested\n"; return 0;
    }
    int lock=open((root/"instance.lock").c_str(),O_CREAT|O_RDWR,0600);
    if (lock<0 || flock(lock,LOCK_EX|LOCK_NB)) throw std::runtime_error("Recline Mode is already open, or its lock is unavailable");
    vr::EVRInitError error=vr::VRInitError_None;
    vr::VR_Init(&error,vr::VRApplication_Overlay);
    if (error!=vr::VRInitError_None) throw std::runtime_error(vr::VR_GetVRInitErrorAsEnglishDescription(error));
    struct Shutdown { ~Shutdown(){vr::VR_Shutdown();} } shutdown;
    if (!vr::VROverlay()) throw std::runtime_error("SteamVR dashboard is unavailable");
    int control=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK,0);
    if(control<0) throw std::runtime_error("Cannot create control socket");
    unlink(sockPath.c_str());
    if(bind(control,reinterpret_cast<sockaddr*>(&address),sizeof(address))) throw std::runtime_error("Cannot bind control socket");
    chmod(sockPath.c_str(),0600);
    int result=recline::panel(control,cmd=="smoke");
    close(control); unlink(sockPath.c_str()); close(lock); return result;
  } catch(const std::exception &e){std::cerr << "Recline Mode: " << e.what() << '\n';return 2;}
}
