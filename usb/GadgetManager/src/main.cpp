// gadget_manager [opts] setup|bind|unbind|teardown|status
#include "gadget_manager.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace s10;

int main(int argc, char** argv) {
    Fs fs;
    GadgetConfig c;
    std::string cmd, udc;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--dry-run") fs.dry_run = true;
        else if (a == "--fake-root") { fs.root = val(); fs.create_attrs = true; }
        else if (a == "--name") c.name = val();
        else if (a == "--udc") udc = val();
        else if (a == "--takeover") c.takeover = val();
        else if (a == "--vid") c.vid = strtoul(val().c_str(), 0, 0);
        else if (a == "--pid") c.pid = strtoul(val().c_str(), 0, 0);
        else if (a[0] != '-') cmd = a;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (cmd.empty()) {
        fprintf(stderr, "usage: gadget_manager [--dry-run] [--fake-root DIR] [--name N] [--udc U] "
                        "[--takeover g1] [--vid V] [--pid P] setup|bind|unbind|teardown|status\n");
        return 2;
    }
    GadgetManager m(fs, c);
    bool ok = true;
    if (cmd == "setup") ok = m.setup();
    else if (cmd == "bind") ok = m.bind(udc);
    else if (cmd == "unbind") ok = m.unbind();
    else if (cmd == "teardown") ok = m.teardown();
    else if (cmd == "status") { fputs(m.status().c_str(), stdout); return 0; }
    else { fprintf(stderr, "unknown command %s\n", cmd.c_str()); return 2; }
    for (auto& l : fs.log) puts(l.c_str());
    return ok ? 0 : 1;
}
