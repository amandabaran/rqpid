#include "SkipVectorDM.h"
#include <fstream>

int kCoroCnt = 4;
int kNodeCount = 4;          // 3 MS + 1 CS
int kComputeNodeCount = 1;
int kMemoryNodeCount = 3;

DMVerbs* dmv;
SkipVectorDM* client;

int main(int argc, char* argv[]) {
	std::cout << "Running" << std::endl;

    DMConfig config;
    config.dsmSize      = 2;  // GB; SkipVector region is ~2.75 GB
    config.machineNR = kNodeCount;
	config.ComputeNumber = kComputeNodeCount;
	config.MemoryNumber = kMemoryNodeCount;

    dmv = DMVerbs::getInstance(config);

    dmv->registerThread();
    dmv->set_barrier("init");
    dmv->set_barrier("loading");
    dmv->set_barrier("running");
    dmv->set_barrier("finish");

    int a = 0;
    while(a == 0) {
        std::cin>>a;
    }
	return 0;
}