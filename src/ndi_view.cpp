// ndi_view — live NDI viewer + norns remote control
//
// Screen: streamed via NDI (libndi).
// Input:  injected via matron's websocket REPL (_norns.key / _norns.enc),
//         the same dispatch point as physical hardware — works with any script.
//
// Buttons: click = press+release. Drag off while held = leave held down.
//          Click a held button again = release.
// Encoders: mouse wheel over E1/E2/E3. WHEEL_PER_DETENT ticks per step.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <cmath>
#include <string>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <SDL.h>
#include <Processing.NDI.Lib.h>

// ---------------- config ----------------
static const int   LW = 128, LH = 132;     // logical canvas
static const int   SCALE = 5;
static const float WHEEL_PER_DETENT = 3.f; // wheel sensitivity (higher = less sensitive)

// ---------------- tiny 3x5 font ----------------
struct Glyph { char c; uint8_t row[5]; };
static const Glyph GLYPHS[] = {
    {'1', {0x2,0x6,0x2,0x2,0x7}},
    {'2', {0x6,0x1,0x2,0x4,0x7}},
    {'3', {0x6,0x1,0x2,0x1,0x6}},
    {'E', {0x7,0x4,0x6,0x4,0x7}},
    {'K', {0x5,0x6,0x4,0x6,0x5}},
    {'+', {0x0,0x2,0x7,0x2,0x0}},
};
static const Glyph* glyph_for(char c) {
    for (auto& g : GLYPHS) if (g.c == c) return &g;
    return nullptr;
}
static void draw_text(SDL_Renderer* r, int x, int y, const char* s) {
    SDL_Rect px = {x, y, 1, 1};
    for (const char* p = s; *p; p++) {
        const Glyph* g = glyph_for(*p);
        if (g) for (int ry = 0; ry < 5; ry++) for (int rx = 0; rx < 3; rx++)
            if (g->row[ry] & (1 << (2 - rx))) {
                px.x = x + rx; px.y = y + ry;
                SDL_RenderFillRect(r, &px);
            }
        x += 4;
    }
}
static int text_w(const char* s) { return (int)strlen(s) * 4 - 1; }

// ---------------- base64 (for ws key) ----------------
static std::string b64(const uint8_t* d, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16 | (i+1 < n ? d[i+1] << 8 : 0) | (i+2 < n ? d[i+2] : 0);
        o += T[v >> 18 & 63]; o += T[v >> 12 & 63];
        o += (i+1 < n) ? T[v >> 6 & 63] : '=';
        o += (i+2 < n) ? T[v & 63] : '=';
    }
    return o;
}

// ---------------- minimal websocket client (fire-and-forget text frames) ----------------
struct WSClient {
    int fd = -1;
    std::string host;
    int port = 5555;
    double last_try = 0;
    double last_hb = 0;
    int hb_miss = 0;

    bool connected() const { return fd >= 0; }

    bool connect_now() {
        disconnect();
        addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        char portstr[8]; snprintf(portstr, 8, "%d", port);
        if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0) return false;
        int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        bool ok = false;
        if (s >= 0) {
            timeval tv{3, 0};
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            if (::connect(s, res->ai_addr, res->ai_addrlen) == 0) {
                uint8_t raw[16];
                for (int i = 0; i < 16; i++) raw[i] = rand() & 0xff;
                char req[1024];
                snprintf(req, sizeof req,
                    "GET / HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
                    "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
                    "Sec-WebSocket-Version: 13\r\n"
                    "Sec-WebSocket-Protocol: bus.sp.nanomsg.org\r\n\r\n",
                    host.c_str(), port, b64(raw, 16).c_str());
                if (send(s, req, strlen(req), 0) > 0) {
                    std::string resp;
                    char buf[1024];
                    while (resp.find("\r\n\r\n") == std::string::npos) {
                        ssize_t n = recv(s, buf, sizeof buf, 0);
                        if (n <= 0) break;
                        resp.append(buf, n);
                    }
                    ok = resp.find(" 101 ") != std::string::npos;
                }
            }
        }
        if (res) freeaddrinfo(res);
        if (ok) {
            int flags = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, flags | O_NONBLOCK);
            fd = s;
            fprintf(stderr, "ws: connected to %s:%d\n", host.c_str(), port);
        } else if (s >= 0) close(s);
        return ok;
    }

    void ensure(double now) {
        if (!connected() && now - last_try > 5.0) {
            last_try = now;
            connect_now();
        }
    }

    void disconnect() { if (fd >= 0) { close(fd); fd = -1; } }

    void drain() {  // discard server replies so buffers don't fill
        if (fd < 0) return;
        char buf[4096];
        ssize_t n;
        while ((n = recv(fd, buf, sizeof buf, 0)) > 0) hb_miss = 0;
        if (n == 0) {  // orderly close from peer
            fprintf(stderr, "ws: peer closed connection\n");
            disconnect();
        }
    }

    // matron replies <ok> to every line; if several heartbeats go unanswered
    // the socket is dead (e.g. device rebooted without a FIN)
    void heartbeat(double now) {
        if (fd < 0) return;
        if (now - last_hb < 3.0) return;
        last_hb = now;
        if (++hb_miss > 4) {
            fprintf(stderr, "ws: heartbeat lost, reconnecting\n");
            disconnect();
            return;
        }
        send_lua("--hb");
    }

    bool send_lua(const char* line) {
        if (fd < 0) return false;
        std::string p = std::string(line) + "\n";
        size_t n = p.size();
        uint8_t hdr[14]; size_t h = 0;
        hdr[h++] = 0x81;
        uint8_t mask[4] = {(uint8_t)rand(), (uint8_t)rand(), (uint8_t)rand(), (uint8_t)rand()};
        if (n < 126) hdr[h++] = 0x80 | (uint8_t)n;
        else { hdr[h++] = 0x80 | 126; hdr[h++] = n >> 8; hdr[h++] = n & 0xff; }
        memcpy(hdr + h, mask, 4); h += 4;
        std::string frame((char*)hdr, h);
        for (size_t i = 0; i < n; i++) frame += (char)(p[i] ^ mask[i % 4]);
        ssize_t w = send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
        if (w < (ssize_t)frame.size()) { disconnect(); return false; }
        return true;
    }

    void key(int n, int z) {
        char b[64]; snprintf(b, 64, "_norns.key(%d,%d)", n, z);
        fprintf(stderr, "sent: %s %s\n", b, send_lua(b) ? "" : "(FAILED)");
    }
    // both events in one REPL line so they evaluate back-to-back
    void key2(int a, int za, int b, int zb) {
        char b_[128]; snprintf(b_, 128, "_norns.key(%d,%d) _norns.key(%d,%d)", a, za, b, zb);
        fprintf(stderr, "sent: %s %s\n", b_, send_lua(b_) ? "" : "(FAILED)");
    }
    void enc(int n, int d) {
        char b[64]; snprintf(b, 64, "_norns.enc(%d,%d)", n, d);
        if (!send_lua(b)) fprintf(stderr, "sent: %s (FAILED)\n", b);
    }
};

// ---------------- UI model ----------------
static const uint32_t COMBO_MIN_HOLD_MS = 250; // min overlap for combo clicks
struct Button {
    SDL_Rect r;
    const char* label;
    int keys[2];
    int nkeys;
    bool held = false;    // key(s) currently down on the device
    bool suppress_up = false;
    uint32_t down_at = 0;
    bool release_pending = false;
    uint32_t release_at = 0;
};
struct EncZone {
    SDL_Rect r;
    const char* label;
    int n;
    float acc = 0;
    float angle = 0;
};

static volatile bool running = true;
static void on_sig(int) { running = false; }

int main(int argc, char** argv) {
    const char* match = argc > 1 ? argv[1] : "NORNS";
    srand(12345);

    if (!NDIlib_initialize()) { fprintf(stderr, "NDI init failed\n"); return 1; }
    NDIlib_find_create_t find_desc = {};
    find_desc.show_local_sources = true;
    NDIlib_find_instance_t finder = NDIlib_find_create_v2(&find_desc);
    if (!finder) { fprintf(stderr, "find_create failed\n"); return 1; }

    printf("Looking for NDI source matching '%s'...\n", match);
    const NDIlib_source_t* src = nullptr;
    NDIlib_source_t src_copy;
    for (int a = 0; a < 20 && !src; a++) {
        NDIlib_find_wait_for_sources(finder, 1000);
        uint32_t n = 0;
        const NDIlib_source_t* sources = NDIlib_find_get_current_sources(finder, &n);
        for (uint32_t i = 0; i < n; i++)
            if (strstr(sources[i].p_ndi_name, match)) { src_copy = sources[i]; src = &src_copy; break; }
    }
    if (!src) { fprintf(stderr, "source not found\n"); return 2; }
    printf("Connecting to '%s'...\n", src->p_ndi_name);

    // control host from the NDI source URL ("192.168.1.139:5961")
    WSClient ws;
    if (src->p_url_address) {
        ws.host = src->p_url_address;
        auto colon = ws.host.find(':');
        if (colon != std::string::npos) ws.host = ws.host.substr(0, colon);
    } else ws.host = "norns.local";

    NDIlib_recv_create_v3_t recv_desc = {};
    recv_desc.source_to_connect_to = *src;
    recv_desc.color_format = NDIlib_recv_color_format_BGRX_BGRA;
    recv_desc.bandwidth = NDIlib_recv_bandwidth_highest;
    recv_desc.allow_video_fields = false;
    recv_desc.p_ndi_recv_name = "ndi-view";
    NDIlib_recv_instance_t recv = NDIlib_recv_create_v3(&recv_desc);
    if (!recv) { fprintf(stderr, "recv_create failed\n"); return 1; }
    NDIlib_recv_connect(recv, src);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    SDL_Window* win = SDL_CreateWindow("norns remote",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, LW * SCALE, LH * SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(ren, LW, LH);
    SDL_Texture* tex = nullptr;
    int tex_w = 0, tex_h = 0;

    Button buttons[] = {
        {{  8, 100, 18, 16}, "K1", {1, 0}, 1},
        {{ 55, 100, 18, 16}, "K2", {2, 0}, 1},
        {{102, 100, 18, 16}, "K3", {3, 0}, 1},
        {{  2, 120, 40, 11}, "K1+K2", {1, 2}, 2},
        {{ 44, 120, 40, 11}, "K1+K3", {1, 3}, 2},
        {{ 86, 120, 40, 11}, "K2+K3", {2, 3}, 2},
    };
    const int NBTN = sizeof buttons / sizeof buttons[0];
    EncZone encs[] = {
        {{  2, 68, 38, 28}, "E1", 1},
        {{ 45, 68, 38, 28}, "E2", 2},
        {{ 88, 68, 38, 28}, "E3", 3},
    };
    const int NENC = sizeof encs / sizeof encs[0];

    ws.ensure(6.0);  // first connect attempt now

    auto btn_press = [&](Button& b) {
        fprintf(stderr, "press %s t=%u\n", b.label, SDL_GetTicks());
        if (b.nkeys == 2) ws.key2(b.keys[0], 1, b.keys[1], 1);
        else ws.key(b.keys[0], 1);
        b.held = true;
        b.down_at = SDL_GetTicks();
        b.release_pending = false;
    };
    auto btn_release = [&](Button& b) {
        if (b.nkeys == 2) {
            uint32_t earliest = b.down_at + COMBO_MIN_HOLD_MS;
            if (SDL_GetTicks() < earliest) {  // enforce min overlap for combos
                b.release_pending = true;
                b.release_at = earliest;
                return;
            }
            ws.key2(b.keys[0], 0, b.keys[1], 0);
        } else ws.key(b.keys[0], 0);
        b.held = false;
    };

    Button* active = nullptr;
    double last_video = SDL_GetTicks() / 1000.0;
    while (running) {
        double now_s = SDL_GetTicks() / 1000.0;
        ws.ensure(now_s);
        ws.drain();
        ws.heartbeat(now_s);

        // deferred combo releases (minimum hold enforcement)
        for (int i = 0; i < NBTN; i++) {
            Button& b = buttons[i];
            if (b.release_pending && SDL_GetTicks() >= b.release_at) {
                fprintf(stderr, "release %s (deferred) t=%u\n", b.label, SDL_GetTicks());
                ws.key2(b.keys[0], 0, b.keys[1], 0);
                b.held = false;
                b.release_pending = false;
            }
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            float lx, ly;
            switch (ev.type) {
            case SDL_QUIT: running = false; break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                // sdl2-compat/SDL3 delivers mouse event coords already in logical space
                lx = ev.button.x; ly = ev.button.y;
                fprintf(stderr, "click logical=(%.1f,%.1f)\n", lx, ly);
                for (int i = 0; i < NBTN; i++) {
                    Button& b = buttons[i];
                    if (lx >= b.r.x && lx < b.r.x + b.r.w && ly >= b.r.y && ly < b.r.y + b.r.h) {
                        fprintf(stderr, "hit %s held=%d\n", b.label, (int)b.held);
                        if (b.held && !b.release_pending) {  // click on held button: release it
                            btn_release(b);
                            b.suppress_up = true;
                        } else if (!b.held) {                // press
                            btn_press(b);
                            b.suppress_up = false;
                        }
                        active = &b;
                        break;
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button != SDL_BUTTON_LEFT || !active) break;
                lx = ev.button.x; ly = ev.button.y;
                if (active->suppress_up) {
                    active->suppress_up = false;
                } else if (active->held && !active->release_pending &&
                           lx >= active->r.x && lx < active->r.x + active->r.w &&
                           ly >= active->r.y && ly < active->r.y + active->r.h) {
                    // released on the button: normal click ends the press
                    btn_release(*active);
                }
                // released off the button: stays held (latched)
                active = nullptr;
                break;
            case SDL_MOUSEWHEEL: {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                SDL_RenderWindowToLogical(ren, mx, my, &lx, &ly);
                float dir = (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.f : 1.f;
                float dy = (ev.wheel.preciseY != 0 ? ev.wheel.preciseY : (float)ev.wheel.y) * dir;
                for (int i = 0; i < NENC; i++) {
                    EncZone& e = encs[i];
                    if (lx >= e.r.x && lx < e.r.x + e.r.w && ly >= e.r.y && ly < e.r.y + e.r.h) {
                        e.acc += dy;
                        while (e.acc >= WHEEL_PER_DETENT)  { ws.enc(e.n,  1); e.angle += 0.5f; e.acc -= WHEEL_PER_DETENT; }
                        while (e.acc <= -WHEEL_PER_DETENT) { ws.enc(e.n, -1); e.angle -= 0.5f; e.acc += WHEEL_PER_DETENT; }
                        break;
                    }
                }
                break;
            }
            }
        }

        NDIlib_video_frame_v2_t video;
        NDIlib_audio_frame_v3_t audio;
        NDIlib_metadata_frame_t meta;
        if (recv) switch (NDIlib_recv_capture_v3(recv, &video, &audio, &meta, 30)) {
            case NDIlib_frame_type_video:
                last_video = now_s;
                if (!tex || video.xres != tex_w || video.yres != tex_h) {
                    if (tex) SDL_DestroyTexture(tex);
                    tex_w = video.xres; tex_h = video.yres;
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_BGRX32, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
                }
                SDL_UpdateTexture(tex, nullptr, video.p_data, video.line_stride_in_bytes);
                NDIlib_recv_free_video_v2(recv, &video);
                break;
            case NDIlib_frame_type_audio:  NDIlib_recv_free_audio_v3(recv, &audio); break;
            case NDIlib_frame_type_metadata: NDIlib_recv_free_metadata(recv, &meta); break;
            default: break;
        }

        // video stalled (device rebooted / sender restarted): re-find and reconnect
        if (now_s - last_video > 10.0) {
            fprintf(stderr, "ndi: no video for 10s, reconnecting...\n");
            NDIlib_find_wait_for_sources(finder, 2000);
            uint32_t n2 = 0;
            const NDIlib_source_t* list = NDIlib_find_get_current_sources(finder, &n2);
            for (uint32_t i = 0; i < n2; i++) {
                if (strstr(list[i].p_ndi_name, match)) {
                    src_copy = list[i];
                    if (recv) NDIlib_recv_destroy(recv);
                    recv_desc.source_to_connect_to = src_copy;
                    recv = NDIlib_recv_create_v3(&recv_desc);
                    if (recv) {
                        NDIlib_recv_connect(recv, &src_copy);
                        fprintf(stderr, "ndi: reconnected to '%s'\n", src_copy.p_ndi_name);
                    }
                    break;
                }
            }
            last_video = now_s;  // retry at most every 10s
        }

        // ---- draw ----
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (tex) {
            SDL_Rect dst{0, 0, 128, 64};
            SDL_RenderCopy(ren, tex, nullptr, &dst);
        }
        SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
        SDL_RenderDrawLine(ren, 0, 65, 127, 65);
        // ws status dot (top-right of controls area)
        if (ws.connected()) SDL_SetRenderDrawColor(ren, 90, 220, 90, 255);
        else                SDL_SetRenderDrawColor(ren, 220, 60, 60, 255);
        { SDL_Rect dot{122, 67, 4, 4}; SDL_RenderFillRect(ren, &dot); }

        for (int i = 0; i < NENC; i++) {  // encoder knobs
            EncZone& e = encs[i];
            int cx = e.r.x + e.r.w / 2, cy = e.r.y + 16, rad = 9;
            SDL_SetRenderDrawColor(ren, 90, 90, 90, 255);
            draw_text(ren, cx - text_w(e.label) / 2, e.r.y + 1, e.label);
            // circle (midpoint)
            int x = rad, y = 0, err = 1 - rad;
            SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
            while (x >= y) {
                SDL_Point pts[8] = {{cx+x,cy+y},{cx+y,cy+x},{cx-y,cy+x},{cx-x,cy+y},
                                    {cx-x,cy-y},{cx-y,cy-x},{cx+y,cy-x},{cx+x,cy-y}};
                SDL_RenderDrawPoints(ren, pts, 8);
                y++; if (err < 0) err += 2*y + 1; else { x--; err += 2*(y-x) + 1; }
            }
            for (int t = 0; t <= rad - 2; t++) {  // pointer
                SDL_RenderDrawPoint(ren, cx + (int)lroundf(cosf(e.angle) * t),
                                         cy + (int)lroundf(sinf(e.angle) * t));
            }
        }
        for (int i = 0; i < NBTN; i++) {  // buttons
            Button& b = buttons[i];
            if (b.held) {
                SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
                SDL_RenderFillRect(ren, &b.r);
                SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                SDL_RenderDrawRect(ren, &b.r);
            }
            int ty = b.r.y + (b.r.h - 5) / 2;
            draw_text(ren, b.r.x + (b.r.w - text_w(b.label)) / 2, ty, b.label);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(8);
    }

    // release anything still held so the device is never left with stuck keys
    for (int i = 0; i < NBTN; i++) {
        Button& b = buttons[i];
        if (b.held) {
            if (b.nkeys == 2) ws.key2(b.keys[0], 0, b.keys[1], 0);
            else ws.key(b.keys[0], 0);
        }
    }
    ws.disconnect();
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    NDIlib_recv_destroy(recv);
    NDIlib_find_destroy(finder);
    NDIlib_destroy();
    return 0;
}
