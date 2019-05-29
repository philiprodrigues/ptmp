#include "ptmp/api.h"
#include "ptmp/cmdline.h"

#include <vector>
#include <string>
#include <iostream>

using namespace std;
using json = nlohmann::json;
using namespace ptmp::cmdline;

int main(int argc, char* argv[])
{
    CLI::App app{"A proxy to apply windowing on a stream of TPSets"};

    int toffset = 0;
    app.add_option("-o,--offset", toffset, "HW time offset");

    int tspan = 300;
    app.add_option("-s,--span", tspan, "HW time span");

    int countdown = -1;         // forever
    app.add_option("-c,--count", countdown,
                   "Number of seconds to count down before exiting, simulating real app work");
    
    CLI::App* isocks = app.add_subcommand("input", "Input socket specification");
    CLI::App* osocks = app.add_subcommand("output", "Output socket specification");
    app.require_subcommand(2);

    sock_options_t iopt{1000,"SUB", "connect"}, oopt{1000,"PUB", "bind"};
    add_socket_options(isocks, iopt);
    add_socket_options(osocks, oopt);

    CLI11_PARSE(app, argc, argv);
    
    json jcfg;
    jcfg["input"] = to_json(iopt);
    jcfg["output"] = to_json(oopt);
    jcfg["toffset"] = toffset;
    jcfg["tspan"] = tspan;
    
    // std::cerr << "Using config: " << jcfg << std::endl;

    std::string cfgstr = jcfg.dump();

    {
        ptmp::TPWindow proxy(cfgstr);

        int snooze = 1000;
        while (countdown != 0) {
            -- countdown;
            int64_t t1 = zclock_usecs ();
            zclock_sleep(snooze);
            int64_t t2 = zclock_usecs ();
            if (std::abs((t2-t1)/1000-snooze) > 10) {
                std::stringstream ss;
                ss << "check_window: sleep interrupted, "
                   << " dt=" << (t2-t1)/1000-snooze
                   <<" t1="<<t1<<", t2="<<t2<<", snooze="<<snooze;
                std::cerr << ss.str() << std::endl;
                zsys_info(ss.str().c_str());
                break;
            }

            zsys_debug("tick %d", countdown);
        }

        std::cerr << "check_window exiting\n";
    }

    return 0;
}
