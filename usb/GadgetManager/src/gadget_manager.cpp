#include "gadget_manager.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace s10 {

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
}

bool Fs::mkdir(const std::string& p) {
    log.push_back("mkdir " + p);
    if (dry_run) return true;
    if (create_attrs) {  // fake tree: emulate configfs auto-created parents (configs/, strings/...)
        for (size_t i = 1; i < p.size(); i++)
            if (p[i] == '/') ::mkdir(path(p.substr(0, i)).c_str(), 0770);
    }
    if (::mkdir(path(p).c_str(), 0770) == 0 || errno == EEXIST) return true;
    log.push_back("  ERR " + std::string(strerror(errno)));
    return false;
}
bool Fs::rmdir(const std::string& p) {
    log.push_back("rmdir " + p);
    if (dry_run) return true;
    if (create_attrs)  // fake tree: emulate configfs removing empty default groups itself
        for (const char* d : {"configs", "functions", "strings", "os_desc"})
            ::rmdir(path(p + "/" + d).c_str());
    if (::rmdir(path(p).c_str()) == 0 || errno == ENOENT) return true;
    log.push_back("  ERR " + std::string(strerror(errno)));
    return false;
}
bool Fs::write(const std::string& p, const std::string& v) {
    {
        std::string e;
        for (char ch : v) { if (ch == '\n') e += "\\n"; else e += ch; }
        log.push_back("write " + p + " <- '" + e + "'");
    }
    if (dry_run) return true;
    int fl = O_WRONLY | O_TRUNC | (create_attrs ? O_CREAT : 0);
    int fd = ::open(path(p).c_str(), fl, 0660);
    if (fd < 0) { log.push_back("  ERR open: " + std::string(strerror(errno))); return false; }
    ssize_t n = ::write(fd, v.data(), v.size());
    ::close(fd);
    if (n != (ssize_t)v.size()) { log.push_back("  ERR short write"); return false; }
    return true;
}
bool Fs::symlink(const std::string& target, const std::string& link) {
    log.push_back("symlink " + link + " -> " + target);
    if (dry_run) return true;
    // targets are given as in-tree absolute paths; re-root them for fake trees
    if (::symlink(path(target).c_str(), path(link).c_str()) == 0 || errno == EEXIST) return true;
    log.push_back("  ERR " + std::string(strerror(errno)));
    return false;
}
bool Fs::unlink(const std::string& p) {
    log.push_back("unlink " + p);
    if (dry_run) return true;
    if (::unlink(path(p).c_str()) == 0 || errno == ENOENT) return true;
    log.push_back("  ERR " + std::string(strerror(errno)));
    return false;
}
bool Fs::exists(const std::string& p) const {
    struct stat st;
    return ::lstat(path(p).c_str(), &st) == 0;
}
std::string Fs::read(const std::string& p) const {
    FILE* f = fopen(path(p).c_str(), "r");
    if (!f) return "";
    char buf[512] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    return trim(std::string(buf, n));
}
std::vector<std::string> Fs::list(const std::string& p) const {
    std::vector<std::string> out;
    DIR* d = opendir(path(p).c_str());
    if (!d) return out;
    while (auto* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        out.push_back(e->d_name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

static std::string hex4(unsigned v) { char b[16]; snprintf(b, sizeof b, "0x%04x", v); return b; }

std::string GadgetManager::pick_udc() const {
    auto l = fs_.list(cfg_.udc_dir);
    return l.empty() ? "" : l.front();
}

bool GadgetManager::release_takeover() {
    if (cfg_.takeover.empty()) return true;
    // Android's USB HAL owns /config/usb_gadget/g1; only one gadget per UDC.
    std::string u = cfg_.configfs + "/" + cfg_.takeover + "/UDC";
    if (!fs_.dry_run && fs_.read(u).empty()) return true;
    return fs_.write(u, "\n");
}

static std::string link_name(const FfsFunction& f) { return "s10_" + f.instance; }

bool GadgetManager::setup() {
    const std::string g = gadget_path();
    if (composing()) {  // only create the functions; linking needs the gadget unbound (bind())
        bool ok = true;
        for (auto& f : cfg_.ffs) ok &= fs_.mkdir(g + "/functions/ffs." + f.instance);
        return ok;
    }
    const std::string cfgdir = g + "/configs/b.1";
    bool ok = true;
    ok &= fs_.mkdir(g);
    ok &= fs_.write(g + "/idVendor", hex4(cfg_.vid));
    ok &= fs_.write(g + "/idProduct", hex4(cfg_.pid));
    ok &= fs_.write(g + "/bcdUSB", hex4(cfg_.bcd_usb));
    ok &= fs_.write(g + "/bcdDevice", hex4(cfg_.bcd_device));
    ok &= fs_.mkdir(g + "/strings/0x409");
    ok &= fs_.write(g + "/strings/0x409/manufacturer", cfg_.manufacturer);
    ok &= fs_.write(g + "/strings/0x409/product", cfg_.product);
    ok &= fs_.write(g + "/strings/0x409/serialnumber", cfg_.serial);
    ok &= fs_.mkdir(cfgdir);
    ok &= fs_.mkdir(cfgdir + "/strings/0x409");
    ok &= fs_.write(cfgdir + "/strings/0x409/configuration", cfg_.config_label);
    ok &= fs_.write(cfgdir + "/MaxPower", std::to_string(cfg_.max_power_ma));
    for (auto& f : cfg_.ffs) {
        std::string fd = g + "/functions/ffs." + f.instance;
        ok &= fs_.mkdir(fd);
        ok &= fs_.symlink(fd, cfgdir + "/ffs." + f.instance);
    }
    return ok;
}

bool GadgetManager::bind(const std::string& udc) {
    std::string u = udc.empty() ? pick_udc() : udc;
    if (u.empty() && !fs_.dry_run) { fs_.log.push_back("ERR no UDC found in " + cfg_.udc_dir); return false; }
    if (u.empty()) u = "<udc>";
    if (composing()) {
        // The host sees a re-enumeration; the pre-existing functions (adb) come back with it.
        const std::string g = gadget_path();
        if (!fs_.dry_run) {
            std::string cur = fs_.read(g + "/UDC");
            if (!cur.empty()) u = cur;  // keep the UDC the gadget was already bound to
        }
        bool ok = fs_.write(g + "/UDC", "\n");
        for (auto& f : cfg_.ffs)
            ok &= fs_.symlink(g + "/functions/ffs." + f.instance, g + "/configs/b.1/" + link_name(f));
        return ok & fs_.write(g + "/UDC", u);
    }
    if (!release_takeover()) return false;
    return fs_.write(gadget_path() + "/UDC", u);
}

bool GadgetManager::unbind() { return fs_.write(gadget_path() + "/UDC", "\n"); }

bool GadgetManager::teardown() {
    const std::string g = gadget_path();
    if (composing()) {  // remove only what we added, then give the gadget back to the system
        std::string u = fs_.dry_run ? "<udc>" : fs_.read(g + "/UDC");
        // If the FFS daemon already exited, the kernel has unbound the gadget and UDC reads empty. Without this the
        // gadget was never given back (phone off the bus until a reboot, seen repeatedly on the S10+): rebind to the
        // controller the system would use.
        if (u.empty()) u = pick_udc();
        // The kernel may already have dropped the gadget when the FFS daemon closed ep0, in
        // which case this write fails (seen on the S10+); cleanup must go on regardless.
        bool ok = true;
        (void)fs_.write(g + "/UDC", "\n");
        for (auto& f : cfg_.ffs) ok &= fs_.unlink(g + "/configs/b.1/" + link_name(f));
        for (auto& f : cfg_.ffs) ok &= fs_.rmdir(g + "/functions/ffs." + f.instance);
        if (!u.empty()) ok &= fs_.write(g + "/UDC", u);
        return ok;
    }
    const std::string cfgdir = g + "/configs/b.1";
    bool ok = true;
    if (fs_.dry_run || fs_.exists(g + "/UDC")) ok &= unbind();
    for (auto& f : cfg_.ffs) ok &= fs_.unlink(cfgdir + "/ffs." + f.instance);
    ok &= fs_.rmdir(cfgdir + "/strings/0x409");
    ok &= fs_.rmdir(cfgdir);
    for (auto& f : cfg_.ffs) ok &= fs_.rmdir(g + "/functions/ffs." + f.instance);
    ok &= fs_.rmdir(g + "/strings/0x409");
    ok &= fs_.rmdir(g);
    return ok;
}

std::string GadgetManager::status() const {
    const std::string g = gadget_path();
    std::string s;
    if (!fs_.exists(g)) return "gadget " + cfg_.name + ": absent\n";
    s += "gadget " + cfg_.name + ": present\n";
    s += "  UDC=" + fs_.read(g + "/UDC") + "\n";
    s += "  idVendor=" + fs_.read(g + "/idVendor") + " idProduct=" + fs_.read(g + "/idProduct") + "\n";
    for (auto& f : fs_.list(g + "/functions")) s += "  function " + f + "\n";
    s += "  available UDCs:";
    for (auto& u : fs_.list(cfg_.udc_dir)) s += " " + u;
    s += "\n";
    return s;
}

// ------------------------------------------------------------ FunctionFS blobs
static void le16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
static void le32(std::vector<uint8_t>& v, uint32_t x) { le16(v, x & 0xffff); le16(v, x >> 16); }

static std::vector<uint8_t> speed_block(const std::vector<FfsInterface>& ifs, bool hs) {
    std::vector<uint8_t> d;
    uint8_t n = 0;
    for (auto& i : ifs) {
        d.insert(d.end(), {9, 4, n++, 0, (uint8_t)i.eps.size(), i.cls, i.sub, i.proto, i.string_idx});
        for (auto& e : i.eps) {
            d.insert(d.end(), {7, 5, e.addr, e.attrs});
            le16(d, hs ? e.max_packet_hs : e.max_packet_fs);
            d.push_back(0);
        }
    }
    return d;
}
static unsigned count_descs(const std::vector<FfsInterface>& ifs) {
    unsigned c = 0;
    for (auto& i : ifs) c += 1 + i.eps.size();
    return c;
}

std::vector<uint8_t> build_ffs_descriptors(const std::vector<FfsInterface>& ifs, uint32_t extra) {
    auto fs = speed_block(ifs, false), hs = speed_block(ifs, true);
    uint32_t flags = kFfsHasFs | kFfsHasHs | extra;
    std::vector<uint8_t> v;
    le32(v, 3);  // FUNCTIONFS_DESCRIPTORS_MAGIC_V2
    le32(v, 0);  // length, patched below
    le32(v, flags);
    le32(v, count_descs(ifs));
    le32(v, count_descs(ifs));
    v.insert(v.end(), fs.begin(), fs.end());
    v.insert(v.end(), hs.begin(), hs.end());
    uint32_t len = v.size();
    v[4] = len; v[5] = len >> 8; v[6] = len >> 16; v[7] = len >> 24;
    return v;
}

std::vector<uint8_t> build_ffs_strings(const std::string& s) {
    std::vector<uint8_t> v;
    le32(v, 2);  // FUNCTIONFS_STRINGS_MAGIC
    le32(v, 16 + 2 + s.size() + 1);
    le32(v, 1);  // str_count
    le32(v, 1);  // lang_count
    le16(v, 0x409);
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
    return v;
}

// ------------------------------------------------------------- EP0 handler
SetupPacket parse_setup(const uint8_t b[8]) {
    return {b[0], b[1], (uint16_t)(b[2] | b[3] << 8), (uint16_t)(b[4] | b[5] << 8),
            (uint16_t)(b[6] | b[7] << 8)};
}

Ep0Result RoleSwitchHandler::handle(const SetupPacket& p) {
    Ep0Result r;
    if (p.type == 0xC0 && p.request == 0x53 && p.length >= 4) {
        r.action = Ep0Action::Reply;
        r.data = {1, 0, 0, 0};
        state_ = Probed;  // every new probe starts a fresh attempt (the radio retries every ~25 s)
    } else if (p.type == 0x40 && p.request == 0x51 && p.value == 1 && state_ == Probed) {
        r.action = Ep0Action::Ack;
        r.role_switch_requested = true;
        state_ = SwitchRequested;
    }  // everything else: Stall ("stay in iPod mode" per csdn blog, hypothesis)
    return r;
}

}  // namespace s10
