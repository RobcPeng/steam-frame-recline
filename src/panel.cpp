#include "panel.hpp"
#include "camera_guard.hpp"
#include <filesystem>
#include "openvr.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fstream>
#include <ctime>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <vector>
namespace recline {
namespace {
volatile std::sig_atomic_t stop = 0;
void stopped(int) { stop = 1; }
struct Button {
  SDL_Rect rect;
  std::string title;
  int action;
};
void checked(vr::EVROverlayError e) {
  if (e != vr::VROverlayError_None)
    throw std::runtime_error(vr::VROverlay()->GetOverlayErrorNameFromEnum(e));
}
struct Surface {
  SDL_Surface *value = nullptr;
  ~Surface() {
    if (value)
      SDL_FreeSurface(value);
  }
};
struct Font {
  TTF_Font *value = nullptr;
  ~Font() {
    if (value)
      TTF_CloseFont(value);
  }
};
struct Overlay {
  vr::VROverlayHandle_t main = 0, thumb = 0;
  ~Overlay() {
    if (main)
      vr::VROverlay()->DestroyOverlay(main);
    if (thumb)
      vr::VROverlay()->DestroyOverlay(thumb);
  }
};
void text(SDL_Surface *dest, TTF_Font *font, const std::string &s, int x, int y,
          SDL_Color color = {229, 236, 246, 255}) {
  Surface rendered{TTF_RenderUTF8_Blended(font, s.c_str(), color)};
  if (!rendered.value)
    return;
  SDL_Rect position{x, y, 0, 0};
  SDL_BlitSurface(rendered.value, nullptr, dest, &position);
}
void wrapped(SDL_Surface *dest, TTF_Font *font, const std::string &s, int x,
             int y, int width) {
  Surface rendered{TTF_RenderUTF8_Blended_Wrapped(font, s.c_str(),
                                                  {189, 201, 218, 255}, width)};
  if (!rendered.value)
    return;
  SDL_Rect r{x, y, 0, 0};
  SDL_BlitSurface(rendered.value, nullptr, dest, &r);
}
} // namespace
int panel(int socket, bool smoke) {
  stop = 0;
  std::signal(SIGTERM, stopped);
  std::signal(SIGINT, stopped);
  if (SDL_Init(0) || TTF_Init())
    throw std::runtime_error("Could not initialize text renderer");
  struct Cleanup {
    ~Cleanup() {
      TTF_Quit();
      SDL_Quit();
    }
  } cleanup;
  Font font, title;
  for (auto path : {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                    "/usr/share/fonts/TTF/DejaVuSans.ttf",
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                    "/usr/share/fonts/dejavu/DejaVuSans.ttf"}) {
    font.value = TTF_OpenFont(path, 27);
    if (font.value) {
      title.value = TTF_OpenFont(path, 42);
      break;
    }
  }
  if (!font.value || !title.value)
    throw std::runtime_error("No supported UI font available");
  Surface surface{
      SDL_CreateRGBSurfaceWithFormat(0, 1000, 780, 32, SDL_PIXELFORMAT_RGBA32)};
  if (!surface.value)
    throw std::runtime_error("Cannot allocate panel surface");
  Overlay overlay;
  checked(vr::VROverlay()->CreateDashboardOverlay(
      "local.frame.recline.experimental", "Recline Mode", &overlay.main, &overlay.thumb));
  checked(vr::VROverlay()->SetOverlayWidthInMeters(overlay.main, 1.4f));
  checked(vr::VROverlay()->SetOverlayInputMethod(
      overlay.main, vr::VROverlayInputMethod_Mouse));
  vr::HmdVector2_t scale{{1000, 780}};
  checked(vr::VROverlay()->SetOverlayMouseScale(overlay.main, &scale));
  std::vector<Button> buttons = {{{40, 190, 920, 80}, "Recenter here", 0},
    {{40, 310, 240, 80}, "Tilt -1 degree", 1}, {{720, 310, 240, 80}, "Tilt +1 degree", 2},
    {{40, 430, 240, 80}, "Lower 5 cm", 3}, {{720, 430, 240, 80}, "Raise 5 cm", 4},
    {{40, 640, 600, 85}, "Reset upright", 5}, {{670, 640, 290, 85}, "Close", 6}};
  std::string message = "Put on the headset and wake the controllers.";
  const auto home = std::filesystem::path(std::getenv("HOME"));
  const auto controlPath = home / ".config/openvr/config/recline-driver-control.txt";
  const auto statusPath = home / ".local/share/Steam/logs/recline-driver-status.txt";
  long long sequence = std::chrono::system_clock::now().time_since_epoch().count(), acknowledged = -1;
  std::string action = "reset";
  double pitch = 0, height = 0, requestedPitch = 0, requestedHeight = 0;
  bool ready = false, active = false, suspended = true;
  CameraGuard camera; bool blocked=camera.blocked();
  std::cout << "Camera visibility guard supported: " << camera.known << std::endl;
  long long issuedAt=0;
  std::optional<std::chrono::steady_clock::time_point> pending;
  auto publish = [&] {
    if (smoke) return;
    auto temp = controlPath.string() + ".tmp";
    std::ofstream f(temp); f << sequence << " " << action << " " << requestedPitch << " " << requestedHeight << " " << std::time(nullptr) << " " << issuedAt << " " << int(blocked) << "\n";
    f.close(); if (!f || std::rename(temp.c_str(), controlPath.c_str())) throw std::runtime_error("Could not send the adjustment");
  };
  auto start = std::chrono::steady_clock::now();
  auto last = start - std::chrono::seconds(1);
  auto enabled = [&](int id) { return id >= 5 || (ready && camera.known && !pending && sequence == acknowledged); };
  auto click = [&](int id) {
    if (!enabled(id)) return;
    if (id == 0) { pending=std::chrono::steady_clock::now()+std::chrono::seconds(3);return; }
    pending.reset();++sequence;
    issuedAt=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(id>=5){action="reset";requestedPitch=requestedHeight=0;}
    else {
      action=active?"set":"recenter";
      requestedPitch=std::clamp(pitch+(id==1?-1:id==2?1:0),0.0,90.0);
      requestedHeight=std::clamp(height+(id==3?-.05:id==4?.05:0),-1.0,1.0);
    }
    publish();
    if (id == 6) stop = 1;
  };
  bool first = true;
  std::vector<unsigned char> previousPixels;
  unsigned uploads = 0;
  while (!stop) {
    char command[64];
    auto n = recv(socket, command, sizeof(command), MSG_DONTWAIT);
    if (n == 7 && std::string(command, 7) == "restore") {
      click(5);
    }
    vr::VREvent_t event{};
    while (vr::VROverlay()->PollNextOverlayEvent(overlay.main, &event,
                                                 sizeof(event))) {
      if (event.eventType == vr::VREvent_Quit) {
        pending.reset();
        stop = 1;
        vr::VRSystem()->AcknowledgeQuit_Exiting();
      }
      if (event.eventType == vr::VREvent_MouseButtonUp &&
          event.data.mouse.button == vr::VRMouseButton_Left) {
        SDL_Point p{int(event.data.mouse.x), 780 - int(event.data.mouse.y)};
        for (auto &b : buttons)
          if (SDL_PointInRect(&p, &b.rect)) {
            click(b.action);
            break;
          }
      }
    }
    auto now = std::chrono::steady_clock::now();
    bool nextBlocked=camera.blocked();
    if(nextBlocked!=blocked){blocked=nextBlocked;publish();}
    if(!stop&&pending&&now>=*pending){
      pending.reset();
      if(ready){++sequence;action="recenter";requestedPitch=-1;requestedHeight=0;issuedAt=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();publish();}
    }
    if (now - last > std::chrono::milliseconds(200)) {
      last = now;
      publish();
      std::ifstream state(statusPath); long long timestamp = 0; int r = 0, a = 0;
      if (state >> timestamp >> r >> a >> pitch >> height >> acknowledged) {
        int paused=1;state>>paused;suspended=paused!=0;
        state.ignore(4096, '\n'); std::getline(state, message);
        bool fresh=std::abs((long long)std::time(nullptr)-timestamp)<=4;
        ready = r && fresh;
        active = ready && a;
        if(!fresh)message="Recline is not connected. Restart the Frame, then reopen this app.";
        else if(!ready)message="Put on the headset and wake the controllers. Waiting for tracking.";
      } else { ready = active = false;message="Recline is not connected. Restart the Frame, then reopen this app."; }
      if(ready&&!pending&&(message=="Experimental shared tilt active."||message=="Upright restored."||message=="Waiting for tracking and Recline panel."))message=active?"Recline stays on when you return to your game.":"Lean back, then choose Recenter here. Captures after 3 seconds.";
      if(!camera.known)message="Camera protection needs an update for this SteamVR version. Tilt stays paused.";
      else if(active&&suspended&&message=="Recline stays on when you return to your game.")message="Your angle is saved. Recline resumes when you return to the game.";
      if(pending)message="Starts in "+std::to_string(int(std::ceil(std::chrono::duration<double>(*pending-now).count())))+" seconds. Press Steam to return to your game.";

      SDL_FillRect(surface.value, nullptr,
                   SDL_MapRGBA(surface.value->format, 17, 24, 36, 255));
      text(surface.value, title.value, "Recline Mode", 40, 28);
      text(surface.value, font.value, active ? (suspended?"Recline paused for camera / menu":"Recline active") : ready ? "Ready to recline" : "Waiting for headset tracking", 40, 90, {110, 210, 196, 255});
      text(surface.value, font.value, "Keep open while playing · Close restores upright", 40, 130);
      text(surface.value, title.value, std::to_string(int(std::lround(pitch))) + " degrees", 375, 325);
      text(surface.value, title.value, std::to_string(int(std::lround(height*100))) + " cm", 410, 445);
      for (auto &b : buttons) {
        const bool buttonEnabled = enabled(b.action);
        SDL_FillRect(surface.value, &b.rect,
                     SDL_MapRGBA(surface.value->format, buttonEnabled ? 38 : 28,
                                 buttonEnabled ? 74 : 39, buttonEnabled ? 89 : 51, 255));
        int w, h;
        TTF_SizeUTF8(font.value, b.title.c_str(), &w, &h);
        text(surface.value, font.value, b.title, b.rect.x + (b.rect.w - w) / 2,
             b.rect.y + (b.rect.h - h) / 2,
             buttonEnabled ? SDL_Color{239, 245, 255, 255}
                     : SDL_Color{113, 125, 141, 255});
      }
      wrapped(surface.value, font.value, message, 40, 555, 920);
      const auto bytes = static_cast<size_t>(surface.value->pitch * surface.value->h);
      const auto pixels = static_cast<unsigned char *>(surface.value->pixels);
      if (previousPixels.size() != bytes || std::memcmp(previousPixels.data(), pixels, bytes) != 0) {
        checked(vr::VROverlay()->SetOverlayRaw(overlay.main, pixels, 1000, 780, 4));
        previousPixels.assign(pixels, pixels + bytes);
        ++uploads;
      }
      if (first) {
        if (smoke)
          SDL_SaveBMP(surface.value, "panel-preview.bmp");
        first = false;
        std::vector<unsigned char> icon(128 * 128 * 4, 255);
        for (size_t i = 0; i < icon.size(); i += 4) {
          icon[i] = 38;
          icon[i + 1] = 110;
          icon[i + 2] = 125;
        }
        checked(vr::VROverlay()->SetOverlayRaw(overlay.thumb, icon.data(), 128,
                                               128, 4));
        if (!smoke)
          vr::VROverlay()->ShowDashboard("local.frame.recline.experimental");
        std::cout << "Dashboard panel ready; mode inactive until user action"
                  << std::endl;
      }
    }
    if (smoke && now - start > std::chrono::seconds(2))
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  ++sequence; action = "reset"; requestedPitch = requestedHeight = 0; publish();
  std::cout << "Panel texture uploads: " << uploads << std::endl;
  return 0;
}
} // namespace recline
