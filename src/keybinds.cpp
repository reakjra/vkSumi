#include "keybinds.hpp"
#include "logger.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cctype>
#include <cstring>
#include <mutex>
#include <vector>

namespace vksumi
{
    namespace
    {
        std::mutex            g_lock;
        Display*              g_dpy        = nullptr;
        bool                  g_attempted  = false;
        std::string           g_lastCombo;
        std::vector<KeyCode>  g_codes;
        bool                  g_prevPressed = false;

        Display* openDisplay()
        {
            if (!g_attempted)
            {
                g_attempted = true;
                g_dpy = XOpenDisplay(nullptr);
                if (g_dpy)
                    VKSUMI_LOG("hotkey: connected to X display %s", DisplayString(g_dpy));
                else
                    VKSUMI_TRACE("hotkey: no X display, set $DISPLAY if you want hotkeys");
            }
            return g_dpy;
        }

        void parseCombo(const std::string& combo, Display* d)
        {
            g_codes.clear();
            std::string token;
            auto flush = [&] {
                while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.erase(0, 1);
                while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))  token.pop_back();
                if (token.empty()) return;
                KeySym ks = XStringToKeysym(token.c_str());
                if (ks == NoSymbol) { VKSUMI_LOG("hotkey: unknown key '%s'", token.c_str()); }
                else
                {
                    KeyCode kc = XKeysymToKeycode(d, ks);
                    if (kc) g_codes.push_back(kc);
                }
                token.clear();
            };
            for (char c : combo)
            {
                if (c == '+') flush();
                else token.push_back(c);
            }
            flush();
            VKSUMI_LOG("hotkey: combo '%s' → %zu keycodes", combo.c_str(), g_codes.size());
        }
    } // anon

    bool toggleKeyJustPressed(const std::string& combo)
    {
        std::lock_guard<std::mutex> l(g_lock);
        Display* d = openDisplay();
        if (!d || combo.empty()) return false;

        if (combo != g_lastCombo)
        {
            g_lastCombo   = combo;
            g_prevPressed = false;
            parseCombo(combo, d);
        }
        if (g_codes.empty()) return false;

        char keys[32];
        XQueryKeymap(d, keys);

        bool all = true;
        for (KeyCode kc : g_codes)
        {
            if (!((keys[kc / 8] >> (kc % 8)) & 1)) { all = false; break; }
        }

        bool edge   = all && !g_prevPressed;
        g_prevPressed = all;
        return edge;
    }
}
