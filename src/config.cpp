#include "config.hpp"
#include "logger.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <poll.h>

namespace vksumi
{
    namespace
    {
        std::mutex                              g_configMutex;
        std::shared_ptr<const Config>           g_config;
        std::atomic<bool>                       g_watcherRunning{false};
        int                                     g_watcherStopFd[2] = {-1, -1};
        std::string                             g_watchedDir;

        // what we drop into ~/.config/vkSumi/games/<exe>.conf the first time a
        // game's swapchain attaches. mirrors vkSumi.conf.example.
        constexpr const char* TEMPLATE = R"VKSUMI(# vkSumi per-game config, auto-created on first launch.
# Merges on top of ~/.config/vkSumi/vkSumi.conf, only sets what differs.

# 0 = no change for every knob.
# + = more / brighter.
# - = less / darker.

enabled = true

brightness  = 0.0
contrast    = 0.0
exposure    = 0.0
gamma       = 0.0

saturation  = 0.0
vibrance    = 0.0
hue_deg     = 0.0
temperature = 0.0
tint        = 0.0

red_gain    = 0.0
green_gain  = 0.0
blue_gain   = 0.0

shadows     = 0.0
midtones    = 0.0
highlights  = 0.0

# toggle_keys = Shift_R+F9
)VKSUMI";

        std::string trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return {};
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        bool parseBool(const std::string& v, bool fallback)
        {
            std::string s;
            s.reserve(v.size());
            for (char c : v) s.push_back(static_cast<char>(::tolower(c)));
            if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
            if (s == "0" || s == "false" || s == "no" || s == "off") return false;
            return fallback;
        }

        bool parseFloat(const std::string& v, float& out)
        {
            try { out = std::stof(v); return true; }
            catch (...) { return false; }
        }

        void applyKey(Config& cfg, const std::string& key, const std::string& val)
        {
            #define F(name) if (key == #name) { float f; if (parseFloat(val, f)) cfg.knobs.name = f; return; }
            F(brightness)
            F(contrast)
            F(exposure)
            F(saturation)
            F(vibrance)
            F(hue_deg)
            F(gamma)
            F(temperature)
            F(tint)
            F(red_gain)
            F(green_gain)
            F(blue_gain)
            F(shadows)
            F(midtones)
            F(highlights)
            #undef F
            if (key == "enabled")     { cfg.enabled = parseBool(val, true); return; }
            if (key == "toggle_keys") { cfg.toggle_keys = val; return; }
            VKSUMI_TRACE("conf: unknown key '%s'", key.c_str());
        }

        bool parseFile(const std::string& path, Config& cfg)
        {
            std::ifstream in(path);
            if (!in) return false;
            std::string line;
            while (std::getline(in, line))
            {
                size_t hash = line.find('#');
                if (hash != std::string::npos) line.resize(hash);
                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = trim(line.substr(0, eq));
                std::string val = trim(line.substr(eq + 1));
                if (key.empty() || val.empty()) continue;
                applyKey(cfg, key, val);
            }
            cfg.sources.push_back(path);
            return true;
        }

        std::string envOr(const char* name, const std::string& fallback)
        {
            const char* v = std::getenv(name);
            return v ? std::string(v) : fallback;
        }

        // basename, handles both / and \ cuz Wine paths are wild
        std::string basename(const std::string& path)
        {
            size_t fwd = path.find_last_of('/');
            size_t bwd = path.find_last_of('\\');
            size_t cut = std::string::npos;
            if (fwd != std::string::npos) cut = fwd;
            if (bwd != std::string::npos && (cut == std::string::npos || bwd > cut)) cut = bwd;
            return cut == std::string::npos ? path : path.substr(cut + 1);
        }

        bool endsWithIcase(const std::string& s, const std::string& suffix)
        {
            if (s.size() < suffix.size()) return false;
            for (size_t i = 0; i < suffix.size(); ++i)
            {
                char a = static_cast<char>(::tolower(s[s.size() - suffix.size() + i]));
                char b = static_cast<char>(::tolower(suffix[i]));
                if (a != b) return false;
            }
            return true;
        }

        // for Wine/Proton games, /proc/self/exe is just wine64-preloader, useless.
        // scan /proc/self/cmdline for the last .exe arg, that's the actual game.
        // native Linux games don't have a .exe in argv so we fall back to /proc/self/exe
        std::string exeBasename()
        {
            std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
            if (cmdline)
            {
                std::string buf((std::istreambuf_iterator<char>(cmdline)),
                                 std::istreambuf_iterator<char>());
                std::string lastExe;
                size_t start = 0;
                for (size_t i = 0; i <= buf.size(); ++i)
                {
                    if (i == buf.size() || buf[i] == '\0')
                    {
                        if (i > start)
                        {
                            std::string arg = buf.substr(start, i - start);
                            if (endsWithIcase(arg, ".exe"))
                                lastExe = basename(arg);
                        }
                        start = i + 1;
                    }
                }
                if (!lastExe.empty()) return lastExe;
            }

            char buf[4096];
            ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (n <= 0) return {};
            buf[n] = '\0';
            return basename(buf);
        }

        bool fileExists(const std::string& p)
        {
            struct stat st{};
            return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
        }

        void mkdir_p(const std::string& path)
        {
            std::string cur;
            for (size_t i = 0; i <= path.size(); ++i)
            {
                if (i == path.size() || path[i] == '/')
                {
                    if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
                    if (i < path.size()) cur.push_back('/');
                }
                else cur.push_back(path[i]);
            }
        }

        std::string configHomeDir()
        {
            std::string xdg = envOr("XDG_CONFIG_HOME", envOr("HOME", ".") + "/.config");
            return xdg + "/vkSumi";
        }

        // drop the .exe suffix so we get "Game.conf" not "Game.exe.conf", looks nicer
        std::string perGameConfName()
        {
            std::string b = exeBasename();
            if (b.empty()) return {};
            if (endsWithIcase(b, ".exe")) b.resize(b.size() - 4);
            if (b.empty()) return {};
            return b + ".conf";
        }

        std::string perGameConfPath()
        {
            std::string n = perGameConfName();
            return n.empty() ? std::string{} : configHomeDir() + "/games/" + n;
        }

        // merge ladder, low to high precedence. files later in the list win over
        // earlier ones field-by-field. auto-create of the per-game file is in
        // ensurePerGameConfig, see why in the header
        std::vector<std::string> buildLadder(std::string& outConfigDir)
        {
            std::vector<std::string> ladder;
            std::string home = configHomeDir();
            outConfigDir     = home;

            ladder.push_back(home + "/vkSumi.conf");                            // global

            std::string per = perGameConfPath();
            if (!per.empty()) ladder.push_back(per);                            // per-game

            ladder.push_back("./vkSumi.conf");                                  // game install dir

            if (const char* p = std::getenv("VKSUMI_CONFIG"))                   // explicit override
                ladder.emplace_back(p);

            return ladder;
        }
    } // anon

    void ensurePerGameConfig()
    {
        std::string p = perGameConfPath();
        if (p.empty() || fileExists(p)) return;

        mkdir_p(configHomeDir() + "/games");
        std::ofstream out(p);
        if (!out) return;
        out << TEMPLATE;
        VKSUMI_LOG("created per-game conf: %s", p.c_str());

        // re-merge so the new file's defaults kick in right now
        setCurrentConfig(loadConfig());
    }

    std::shared_ptr<const Config> loadConfig()
    {
        auto cfg = std::make_shared<Config>();
        std::string watchDir;
        auto ladder = buildLadder(watchDir);

        bool any = false;
        for (const auto& p : ladder)
        {
            if (parseFile(p, *cfg)) { any = true; VKSUMI_TRACE("merged conf: %s", p.c_str()); }
        }

        if (any)
            VKSUMI_LOG("config loaded (%zu file%s)",
                       cfg->sources.size(), cfg->sources.size() == 1 ? "" : "s");
        else
            VKSUMI_LOG("no config files found, using defaults");

        // stash the dir for the watcher, first-loader wins, reloads are no-ops
        std::lock_guard<std::mutex> l(g_configMutex);
        if (g_watchedDir.empty()) g_watchedDir = watchDir;
        return cfg;
    }

    std::shared_ptr<const Config> currentConfig()
    {
        // double-checked: read under lock, load without it, swap under lock.
        // we cant hold g_configMutex during loadConfig cuz loadConfig grabs it
        // too and std::mutex deadlocks on recursive lock. found that one the
        // hard way ofc
        {
            std::lock_guard<std::mutex> l(g_configMutex);
            if (g_config) return g_config;
        }
        auto loaded = loadConfig();
        std::lock_guard<std::mutex> l(g_configMutex);
        if (!g_config) g_config = loaded;
        return g_config;
    }

    void setCurrentConfig(std::shared_ptr<const Config> next)
    {
        std::lock_guard<std::mutex> l(g_configMutex);
        g_config = std::move(next);
    }

    static void watcherLoop(std::string watchDir)
    {
        int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (ifd < 0) { VKSUMI_LOG("inotify_init1 failed: %s", std::strerror(errno)); return; }

        // watch the root and games/ subdir. any .conf change triggers a full ladder reload
        mkdir_p(watchDir);
        mkdir_p(watchDir + "/games");
        const uint32_t mask = IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;
        int wd_root  = inotify_add_watch(ifd, watchDir.c_str(),              mask);
        int wd_games = inotify_add_watch(ifd, (watchDir + "/games").c_str(), mask);
        if (wd_root < 0 && wd_games < 0)
        {
            VKSUMI_LOG("inotify_add_watch(%s) failed: %s", watchDir.c_str(), std::strerror(errno));
            close(ifd); return;
        }

        VKSUMI_LOG("watching %s (and games/) for conf changes", watchDir.c_str());

        struct pollfd pfds[2];
        pfds[0] = { ifd,                  POLLIN, 0 };
        pfds[1] = { g_watcherStopFd[0],   POLLIN, 0 };

        char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

        while (g_watcherRunning.load())
        {
            int n = poll(pfds, 2, -1);
            if (n < 0) { if (errno == EINTR) continue; break; }
            if (pfds[1].revents & POLLIN) break;
            if (!(pfds[0].revents & POLLIN)) continue;

            ssize_t got = read(ifd, buf, sizeof(buf));
            if (got <= 0) continue;

            bool need_reload = false;
            for (char* p = buf; p < buf + got; )
            {
                auto* ev = reinterpret_cast<struct inotify_event*>(p);
                if (ev->len > 0)
                {
                    const char* dot = std::strrchr(ev->name, '.');
                    if (dot && std::strcmp(dot, ".conf") == 0) need_reload = true;
                }
                p += sizeof(struct inotify_event) + ev->len;
            }

            if (need_reload)
            {
                setCurrentConfig(loadConfig());
                VKSUMI_LOG("config reloaded");
            }
        }

        if (wd_root  >= 0) inotify_rm_watch(ifd, wd_root);
        if (wd_games >= 0) inotify_rm_watch(ifd, wd_games);
        close(ifd);
    }

    void startConfigWatcher()
    {
        bool expected = false;
        if (!g_watcherRunning.compare_exchange_strong(expected, true)) return;

        // force config to load so g_watchedDir is populated
        (void)currentConfig();
        std::string dir;
        { std::lock_guard<std::mutex> l(g_configMutex); dir = g_watchedDir; }
        if (dir.empty()) { g_watcherRunning.store(false); return; }

        if (pipe(g_watcherStopFd) != 0) { g_watcherRunning.store(false); return; }
        // detach! a joinable std::thread destructor at process exit calls
        // std::terminate which crashes the host app. daemon thread, kernel
        // reaps it when the process dies anyway.
        std::thread(watcherLoop, dir).detach();
    }

    void stopConfigWatcher()
    {
        // watcher is detached (see startConfigWatcher), no join needed
        if (!g_watcherRunning.exchange(false)) return;
        if (g_watcherStopFd[1] >= 0) { (void)!::write(g_watcherStopFd[1], "x", 1); }
    }
}
