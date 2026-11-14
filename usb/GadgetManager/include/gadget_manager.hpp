// S10CarOS GadgetManager core: ConfigFS gadget builder, FunctionFS descriptor
// blobs and the EP0 role-switch request handler. No Android dependencies.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace s10 {

// ---------------------------------------------------------------- filesystem
// Every filesystem touch goes through Fs so dry-run and tests are exact.
struct Fs {
    std::string root;        // prefix, e.g. "" on device or a temp dir in tests
    bool dry_run = false;    // log only, never touch
    bool create_attrs = false;  // fake tree: attribute writes may create files
    std::vector<std::string> log;  // one line per operation

    std::string path(const std::string& p) const { return root + p; }
    bool mkdir(const std::string& p);
    bool rmdir(const std::string& p);
    bool write(const std::string& p, const std::string& v);
    bool symlink(const std::string& target, const std::string& link);
    bool unlink(const std::string& p);
    bool exists(const std::string& p) const;
    std::string read(const std::string& p) const;          // trimmed, "" if absent
    std::vector<std::string> list(const std::string& p) const;  // sorted names
};

// ------------------------------------------------------------------- gadget
struct FfsFunction {
    std::string instance;   // "carplay" -> functions/ffs.carplay
};

struct GadgetConfig {
    std::string configfs = "/config/usb_gadget";
    std::string udc_dir = "/sys/class/udc";
    std::string name = "s10carplay";
    uint16_t vid = 0x05ac;          // Apple. See research/usb-feasibility.md sec 4
    uint16_t pid = 0x12a8;          // QNX matches 0x1200-0x12FF
    uint16_t bcd_usb = 0x0200;      // USB 2.0: the swap trace is HS
    uint16_t bcd_device = 0x0414;
    std::string manufacturer = "Apple Inc.";
    std::string product = "iPhone";
    std::string serial = "S10CARPLAY0001";
    std::string config_label = "carplay";
    unsigned max_power_ma = 500;
    std::vector<FfsFunction> ffs = {{"carplay"}};
    std::string takeover;           // existing gadget to unbind first ("g1")
};

class GadgetManager {
public:
    GadgetManager(Fs& fs, GadgetConfig cfg) : fs_(fs), cfg_(std::move(cfg)) {}
    bool setup();                              // build tree, no UDC bind
    bool bind(const std::string& udc = "");    // write UDC; "" = first of udc_dir
    bool unbind();
    bool teardown();                           // unbind + remove everything
    std::string status() const;
    std::string gadget_path() const { return cfg_.configfs + "/" + cfg_.name; }
    std::string pick_udc() const;
private:
    bool release_takeover();
    Fs& fs_;
    GadgetConfig cfg_;
};

// ----------------------------------------------------------- FunctionFS blobs
struct FfsEndpoint { uint8_t addr; uint8_t attrs; uint16_t max_packet_fs, max_packet_hs; };
struct FfsInterface {
    uint8_t cls = 0xff, sub = 0x00, proto = 0x00;  // iAP2 values UNKNOWN
    uint8_t string_idx = 1;
    std::vector<FfsEndpoint> eps;
};
// flags: FUNCTIONFS_HAS_FS_DESC|HS_DESC|ALL_CTRL_RECIP|CONFIG0_SETUP
constexpr uint32_t kFfsHasFs = 1, kFfsHasHs = 2, kFfsAllCtrlRecip = 64, kFfsConfig0Setup = 128;
std::vector<uint8_t> build_ffs_descriptors(const std::vector<FfsInterface>& ifs,
                                           uint32_t extra_flags = kFfsAllCtrlRecip | kFfsConfig0Setup);
std::vector<uint8_t> build_ffs_strings(const std::string& s);

// --------------------------------------------------- EP0 role-switch handler
// Wire facts from USENIX VehicleSec 2025 (Won et al.), Table 3: radio sends
//   C0 53 00 00 00 00 04 00 -> device answers 01 00 00 00
//   40 51 01 00 00 00 00 00 (text says wLength 04) -> link drops, roles swap.
struct SetupPacket { uint8_t type, request; uint16_t value, index, length; };
enum class Ep0Action { Stall, Reply, Ack };
struct Ep0Result {
    Ep0Action action = Ep0Action::Stall;
    std::vector<uint8_t> data;
    bool role_switch_requested = false;   // caller must now swap to host
};
class RoleSwitchHandler {
public:
    enum State { Idle, Probed, SwitchRequested };
    Ep0Result handle(const SetupPacket& p);
    State state() const { return state_; }
private:
    State state_ = Idle;
};
SetupPacket parse_setup(const uint8_t b[8]);

}  // namespace s10
