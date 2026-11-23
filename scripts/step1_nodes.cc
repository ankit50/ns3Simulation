#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"

using namespace ns3;

int
main(int argc, char* argv[])
{
    uint32_t nNodes = 10;

    NodeContainer nodes;
    nodes.Create(nNodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        positionAlloc->Add(Vector(i * 300, 0, 0)); // 300 m apart, in a line
    }
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    AnimationInterface anim("step1-nodes.xml");

    Simulator::Stop(Seconds(5));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
