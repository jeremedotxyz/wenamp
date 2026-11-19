// wenamp: the SDL2 window front-end. SDL is used only for the window, input and
// blitting the finished software frame; audio goes through miniaudio.

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "wenamp/player.hpp"
#include "wenamp/playlist.hpp"
#include "wenamp/ui.hpp"
#include "wenamp/version.hpp"

using namespace wenamp;

namespace {

struct Args {
  std::vector<std::string> files;
  std::string theme = "classic";
  int scale = 2;
  bool eq = false;
  bool playlist = false;
  int frames = 0;        // >0: headless smoke mode, run this many frames then exit
  std::string dump;      // PPM path written in smoke mode
};

void usage() {
  std::puts(
      "wenamp [options] [files or .m3u playlists...]\n"
      "  --theme classic|ember|paper   skin colours\n"
      "  --scale N                     window scale factor (default 2)\n"
      "  --eq / --playlist             start with the equalizer / playlist window open\n"
      "  --frames N [--dump out.ppm]   run N frames headless (smoke test) and exit\n"
      "\n"
      "keys: Z prev  X play  C pause  V stop  B next  Space pause  Left/Right seek 5s\n"
      "      Up/Down volume  S shuffle  R repeat  E equalizer  P playlist  T theme\n"
      "      Enter play selected  Del remove selected  Q/Esc quit");
}

bool isM3U(const std::string& f) {
  auto ends = [&](const char* ext) {
    const std::size_t n = std::strlen(ext);
    if (f.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i)
      if (std::tolower(static_cast<unsigned char>(f[f.size() - n + i])) != ext[i]) return false;
    return true;
  };
  return ends(".m3u") || ends(".m3u8");
}

std::size_t addFile(Playlist& pl, const std::string& f) {
  const std::size_t before = pl.size();
  if (isM3U(f)) pl.loadM3U(f);
  else pl.addPath(f);
  return before;
}

struct HitCtx {
  Ui* ui;
  int scale;
};

SDL_HitTestResult windowHitTest(SDL_Window*, const SDL_Point* p, void* data) {
  auto* c = static_cast<HitCtx*>(data);
  return c->ui->isDragRegion(p->x / c->scale, p->y / c->scale) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

bool mapKey(SDL_Keycode k, Key& out) {
  switch (k) {
    case SDLK_z: out = Key::Prev; return true;
    case SDLK_x: out = Key::Play; return true;
    case SDLK_c: out = Key::Pause; return true;
    case SDLK_v: out = Key::Stop; return true;
    case SDLK_b: out = Key::Next; return true;
    case SDLK_SPACE: out = Key::TogglePause; return true;
    case SDLK_LEFT: out = Key::SeekBack; return true;
    case SDLK_RIGHT: out = Key::SeekFwd; return true;
    case SDLK_UP: out = Key::VolUp; return true;
    case SDLK_DOWN: out = Key::VolDown; return true;
    case SDLK_s: out = Key::Shuffle; return true;
    case SDLK_r: out = Key::Repeat; return true;
    case SDLK_e: out = Key::ToggleEq; return true;
    case SDLK_p: out = Key::TogglePlaylist; return true;
    case SDLK_t: out = Key::NextTheme; return true;
    case SDLK_DELETE: out = Key::DeleteSelected; return true;
    default: return false;
  }
}

}  // namespace

int main(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    if (s == "--help" || s == "-h") return usage(), 0;
    if (s == "--version") return std::printf("wenamp %s\n", kVersion), 0;
    if (s == "--theme" && i + 1 < argc) a.theme = argv[++i];
    else if (s == "--scale" && i + 1 < argc) a.scale = std::max(1, std::atoi(argv[++i]));
    else if (s == "--eq") a.eq = true;
    else if (s == "--playlist") a.playlist = true;
    else if (s == "--frames" && i + 1 < argc) a.frames = std::atoi(argv[++i]);
    else if (s == "--dump" && i + 1 < argc) a.dump = argv[++i];
    else a.files.push_back(s);
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  Player player;
  Playlist playlist;
  Ui ui(player, playlist, Theme::byName(a.theme));
  ui.showEq(a.eq);
  ui.showPlaylist(a.playlist || a.files.size() > 1);
  for (const auto& f : a.files) addFile(playlist, f);

  std::string err;
  if (!player.startDevice(&err)) std::fprintf(stderr, "warning: %s (UI will run silently)\n", err.c_str());

  int scale = a.scale;
  SDL_Window* win = SDL_CreateWindow("wenamp", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     Ui::kWidth * scale, ui.windowHeight() * scale, SDL_WINDOW_BORDERLESS);
  if (!win) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) {
    std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  const int maxH = Ui::kMainH + Ui::kEqH + Ui::kPlH;
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Ui::kWidth, maxH);
  if (!tex) {
    std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    return 1;
  }

  HitCtx ctx{&ui, scale};
  SDL_SetWindowHitTest(win, windowHitTest, &ctx);

  if (!playlist.empty()) ui.playIndex(0);

  Canvas canvas;
  bool running = true;
  int frame = 0;
  int lastH = ui.windowHeight();
  Uint64 last = SDL_GetPerformanceCounter();
  double titleTimer = 0.0;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_MOUSEBUTTONDOWN:
          if (e.button.button == SDL_BUTTON_LEFT)
            ui.mouseDown(e.button.x / scale, e.button.y / scale, e.button.clicks >= 2);
          break;
        case SDL_MOUSEBUTTONUP:
          if (e.button.button == SDL_BUTTON_LEFT) ui.mouseUp(e.button.x / scale, e.button.y / scale);
          break;
        case SDL_MOUSEMOTION: ui.mouseMove(e.motion.x / scale, e.motion.y / scale); break;
        case SDL_MOUSEWHEEL: {
          int mx = 0, my = 0;
          SDL_GetMouseState(&mx, &my);
          ui.wheel(mx / scale, my / scale, e.wheel.y);
          break;
        }
        case SDL_DROPFILE: {
          const std::string f = e.drop.file;
          SDL_free(e.drop.file);
          const std::size_t first = addFile(playlist, f);
          if (!player.hasTrack() || player.state() == PlayState::Stopped) {
            if (first < playlist.size()) ui.playIndex(static_cast<int>(first));
          }
          break;
        }
        case SDL_KEYDOWN: {
          if (e.key.repeat && e.key.keysym.sym != SDLK_LEFT && e.key.keysym.sym != SDLK_RIGHT &&
              e.key.keysym.sym != SDLK_UP && e.key.keysym.sym != SDLK_DOWN)
            break;
          const SDL_Keycode k = e.key.keysym.sym;
          Key mapped;
          if (k == SDLK_q || k == SDLK_ESCAPE) running = false;
          else if (k == SDLK_RETURN && ui.selectedRow() >= 0) ui.playIndex(ui.selectedRow());
          else if (mapKey(k, mapped)) ui.key(mapped);
          break;
        }
        default: break;
      }
    }
    if (ui.wantsQuit()) running = false;

    const Uint64 now = SDL_GetPerformanceCounter();
    const double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
    last = now;
    ui.tick(std::min(dt, 0.1));

    if (ui.windowHeight() != lastH) {
      lastH = ui.windowHeight();
      SDL_SetWindowSize(win, Ui::kWidth * scale, lastH * scale);
    }

    ui.render(canvas);
    SDL_Rect src{0, 0, canvas.width(), canvas.height()};
    SDL_UpdateTexture(tex, &src, canvas.data(), canvas.width() * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, &src, nullptr);
    SDL_RenderPresent(ren);

    titleTimer += dt;
    if (titleTimer > 0.5) {
      titleTimer = 0.0;
      SDL_SetWindowTitle(win, ("wenamp - " + ui.titleLine()).c_str());
    }

    ++frame;
    if (a.frames > 0 && frame >= a.frames) running = false;
    if (a.frames > 0) SDL_Delay(4);  // headless: don't spin
  }

  if (a.frames > 0 && !a.dump.empty()) {
    ui.render(canvas);
    if (!canvas.writePPM(a.dump)) std::fprintf(stderr, "could not write %s\n", a.dump.c_str());
  }

  player.stopDevice();
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
