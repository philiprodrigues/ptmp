/**
   A ptmper can run an arbitrary set of PTMP actor proxies as driven
   by unified JSON configuration.

   The configuration consists of an object with these attributes:

   - ttl :: time to live in seconds.

   - snooze :: snooze time in milliseconds.  Every wake up prints a
     diagnostic.  TTL is tested after a snooze so this sets the
     resolution in actual run time.

   - pause :: delay in seconds before any proxies are created.

   - reprieve :: delay in seconds after ttl has passed and before proxies are destroyed.

   - proxies :: a JSON array with elements described next

   Each element of the proxies array is a JSON object with these top level attributes.

   - name :: uniquely identify this instance

   - type :: the C++ type name of the proxy class

   - data :: a type-specific JSON object to give as configuration to the proxy

 */

#include "ptmp/api.h"

#include <iostream>
#include <fstream>
#include <vector>

#include "json.hpp"

using json = nlohmann::json;



int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "usage: ptmper config.json\n";
        return -1;
    }

    zsys_init();

    std::ifstream cfgfile(argv[1]);
    json config;
    cfgfile >> config;

    int ttl = 0;
    if (config["ttl"].is_number()) {
        ttl = config["ttl"];
    }
    int snooze = 1000;
    if (config["snooze"].is_number()) {
        snooze = config["snooze"];
    }
    int pause = 0;
    if (config["pause"].is_number()) {
        pause = config["pause"];
    }
    int reprieve = 0;
    if (config["reprieve"].is_number()) {
        reprieve = config["reprieve"];
    }

    if (pause > 0) {
        zclock_sleep(pause*1000);
    }

    std::vector<ptmp::TPAgent*> agents;
    for (auto& jprox : config["proxies"]) {
        const std::string name = jprox["name"];
        const std::string type = jprox["type"];
        const std::string data = jprox["data"].dump();

        ptmp::TPAgent* agent = ptmp::agent_factory(type, data);
        if (!agent) {
            std::cerr << "failed to create agent \"" << name << "\" of type " << type << "\n";
            return -1;
        }
        agents.push_back(agent);
    }
    
    const auto die_at = zclock_usecs() + ttl*1000000;
    zsys_debug("ttl %d", ttl);

    int tick = 0;
    while (!zsys_interrupted) {

        if (ttl > 0 and die_at < zclock_usecs()) {
            zsys_debug("ttl of %d s reached", ttl);
            break;
        }

        zclock_sleep(snooze);
        zsys_info("tick %d", tick);
        ++tick;

    }

    if (reprieve) {
        zsys_debug("sleep %d seconds before deleting agents", reprieve);
        zclock_sleep(reprieve*1000);
    }

    zsys_debug("deleting agents");
    for (auto& agent : agents) {
        zsys_debug("delete");
        delete agent;
    }

    return 0;
}
