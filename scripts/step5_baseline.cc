// step5_baseline.cc
// Full paper-scale baseline network, 0% to N% attackers (Table V parameters).
// 25 drones + 1 fixed GBS, AODV, 3D GMM, 10 source/destination pairs,
// destinations relay collected data to the GBS starting at t=10s.
//
// This version also logs, at every progress tick, how much REAL wall-clock
// time has elapsed so far -- so we can see whether the simulation's cost
// per simulated second stays constant or grows over time.

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <sstream>
#include <vector>

using namespace ns3;

static std::chrono::steady_clock::time_point g_startTime;

void
PrintProgress(double simTime, double interval)
{
    double elapsedReal =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - g_startTime).count();
    std::cout << "sim time: " << Simulator::Now().GetSeconds() << "s"
               << " | real elapsed: " << elapsedReal << "s" << std::endl;
    std::cout.flush();
    if (Simulator::Now().GetSeconds() < simTime)
    {
        Simulator::Schedule(Seconds(interval), &PrintProgress, simTime, interval);
    }
}

int
main(int argc, char* argv[])
{
    uint32_t nDrones = 25;
    double simTime = 1800.0;
    double areaXY = 12000.0;
    double areaZ = 300.0;
    double range = 250.0;
    double alpha = 0.25;
    double avgSpeed = 100.0;
    double progressInterval = 10.0;
    uint32_t run = 1;
    double attackerRatio = 0.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nDrones", "Number of drones (GBS is additional)", nDrones);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.AddValue("alpha", "Gauss-Markov alpha", alpha);
    cmd.AddValue("areaXY", "X/Y area size (m) -- diagnostic knob, paper value is 12000", areaXY);
    cmd.AddValue("progressInterval", "Seconds (sim time) between progress prints", progressInterval);
    cmd.AddValue("run", "RNG run number", run);
    cmd.AddValue("attackerRatio",
                 "Fraction of eligible (non-source/destination) drones that are sinkhole attackers",
                 attackerRatio);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(run);

    NodeContainer drones;
    drones.Create(nDrones);
    NodeContainer gbs;
    gbs.Create(1);
    NodeContainer all(drones, gbs);

    // ---- Mobility: drones get 3D Gauss-Markov, GBS is fixed at the centre ----
    MobilityHelper droneMobility;
    ObjectFactory posFactory;
    posFactory.SetTypeId("ns3::RandomBoxPositionAllocator");
    std::ostringstream xy, z;
    xy << "ns3::UniformRandomVariable[Min=0|Max=" << areaXY << "]";
    z << "ns3::UniformRandomVariable[Min=0|Max=" << areaZ << "]";
    posFactory.Set("X", StringValue(xy.str()));
    posFactory.Set("Y", StringValue(xy.str()));
    posFactory.Set("Z", StringValue(z.str()));
    Ptr<PositionAllocator> alloc = posFactory.Create<PositionAllocator>();

    droneMobility.SetPositionAllocator(alloc);
    droneMobility.SetMobilityModel(
        "ns3::GaussMarkovMobilityModel",
        "Bounds", BoxValue(Box(0, areaXY, 0, areaXY, 0, areaZ)),
        "TimeStep", TimeValue(Seconds(1)),
        "Alpha", DoubleValue(alpha),
        "MeanVelocity", StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(avgSpeed) + "]"),
        "MeanDirection", StringValue("ns3::UniformRandomVariable[Min=0|Max=6.283185307]"),
        "MeanPitch", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=0.0]"),
        "NormalVelocity", StringValue("ns3::NormalRandomVariable[Mean=0.0|Variance=0.0|Bound=0.0]"),
        "NormalDirection", StringValue("ns3::NormalRandomVariable[Mean=0.0|Variance=0.2|Bound=0.4]"),
        "NormalPitch", StringValue("ns3::NormalRandomVariable[Mean=0.0|Variance=0.0|Bound=0.0]"));
    droneMobility.Install(drones);

    MobilityHelper gbsMobility;
    Ptr<ListPositionAllocator> gbsPos = CreateObject<ListPositionAllocator>();
    gbsPos->Add(Vector(areaXY / 2, areaXY / 2, 0));
    gbsMobility.SetPositionAllocator(gbsPos);
    gbsMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gbsMobility.Install(gbs);

    // ---- Wifi: 802.11b @ 11 Mbps, 250m range via RangePropagationLossModel ----
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("DsssRate11Mbps"),
                                 "ControlMode", StringValue("DsssRate1Mbps"));
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(range));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, all);

    // ---- AODV + Internet stack on everyone, including the GBS ----
    AodvHelper aodv;
    InternetStackHelper stack;
    stack.SetRoutingHelper(aodv);
    stack.Install(all);

    Ipv4AddressHelper address;
    address.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // ---- Randomly pick 10 sources + 10 destinations (distinct) from the drones ----
    std::vector<uint32_t> idx(nDrones);
    std::iota(idx.begin(), idx.end(), 0);
    Ptr<UniformRandomVariable> rv = CreateObject<UniformRandomVariable>();
    for (uint32_t i = nDrones - 1; i > 0; --i)
    {
        uint32_t j = rv->GetInteger(0, i);
        std::swap(idx[i], idx[j]);
    }
    std::vector<uint32_t> sources(idx.begin(), idx.begin() + 10);
    std::vector<uint32_t> destinations(idx.begin() + 10, idx.begin() + 20);

    // ---- Mark a chosen subset of drones as sinkhole attackers ----
    // Attackers are only chosen from drones NOT already used as a source or destination
    // (matches the paper's own methodology -- see Finding 2 in NOTES.md).
    std::vector<uint32_t> eligibleForAttack;
    for (uint32_t i = 0; i < nDrones; ++i)
    {
        bool isSrc = std::find(sources.begin(), sources.end(), i) != sources.end();
        bool isDst = std::find(destinations.begin(), destinations.end(), i) != destinations.end();
        if (!isSrc && !isDst)
        {
            eligibleForAttack.push_back(i);
        }
    }

    uint32_t nAttackers = std::min(
        (uint32_t)(attackerRatio * nDrones + 0.5), // round to nearest
        (uint32_t)eligibleForAttack.size());        // can't exceed what's actually available

    std::cout << "\n--- Attacker assignment ---" << std::endl;
    std::cout << "Eligible (non-source/destination) drones: " << eligibleForAttack.size()
               << " out of " << nDrones << std::endl;
    std::cout << "Requested attacker ratio: " << attackerRatio * 100 << "%  ->  " << nAttackers
               << " attacker(s)" << std::endl;

    std::vector<uint32_t> attackerIndices;  // <-- NEW: remember which drones are attackers
    for (uint32_t i = 0; i < nAttackers; ++i)
    {
        uint32_t attackerIdx = eligibleForAttack[i];
        Ptr<Ipv4> ipv4 = drones.Get(attackerIdx)->GetObject<Ipv4>();
        Ptr<aodv::RoutingProtocol> aodvProto =
            DynamicCast<aodv::RoutingProtocol>(ipv4->GetRoutingProtocol());
        NS_ASSERT_MSG(aodvProto, "Node is not running AODV");
        aodvProto->SetAttribute("Malicious", BooleanValue(true));
        attackerIndices.push_back(attackerIdx);  // <-- NEW
        std::cout << "Drone " << attackerIdx << " is a sinkhole attacker" << std::endl;
    }

    uint16_t sdPort = 5000;
    uint16_t gbsPort = 6000;
    uint32_t gbsIndex = nDrones; // GBS's position inside "all" / "interfaces"

    std::cout << "\n--- Source/Destination assignment (for this run) ---" << std::endl;
    for (uint32_t i = 0; i < 10; ++i)
    {
        std::cout << "pair " << i << ": drone " << sources[i] << " -> drone " << destinations[i]
                   << std::endl;
    }
    std::cout.flush();

    for (uint32_t i = 0; i < 10; ++i)
    {
        uint32_t srcIdx = sources[i];
        uint32_t dstIdx = destinations[i];

        // Flow 1: source -> its paired destination
        UdpServerHelper sdServer(sdPort + i);
        ApplicationContainer sdServerApp = sdServer.Install(drones.Get(dstIdx));
        sdServerApp.Start(Seconds(1.0));
        sdServerApp.Stop(Seconds(simTime));

        UdpClientHelper sdClient(interfaces.GetAddress(dstIdx), sdPort + i);
        sdClient.SetAttribute("MaxPackets", UintegerValue(4294967295u));
        sdClient.SetAttribute("Interval", TimeValue(Seconds(1.0))); // 1 packet/s
        sdClient.SetAttribute("PacketSize", UintegerValue(512));
        ApplicationContainer sdClientApp = sdClient.Install(drones.Get(srcIdx));
        sdClientApp.Start(Seconds(2.0));
        sdClientApp.Stop(Seconds(simTime));

        // Flow 2: that destination relays collected data to the GBS, from t = 10s
        UdpServerHelper gbsServer(gbsPort + i);
        ApplicationContainer gbsServerApp = gbsServer.Install(gbs.Get(0));
        gbsServerApp.Start(Seconds(1.0));
        gbsServerApp.Stop(Seconds(simTime));

        UdpClientHelper gbsClient(interfaces.GetAddress(gbsIndex), gbsPort + i);
        gbsClient.SetAttribute("MaxPackets", UintegerValue(4294967295u));
        gbsClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        gbsClient.SetAttribute("PacketSize", UintegerValue(512));
        ApplicationContainer gbsClientApp = gbsClient.Install(drones.Get(dstIdx));
        gbsClientApp.Start(Seconds(10.0));
        gbsClientApp.Stop(Seconds(simTime));
    }

    FlowMonitorHelper fmh;
    Ptr<FlowMonitor> monitor = fmh.InstallAll();

    AnimationInterface anim("step5-baseline.xml");

    // ---- NEW: color the sinkhole attacker(s) red in NetAnim so they stand out visually ----
    for (uint32_t attackerIdx : attackerIndices)
    {
        anim.UpdateNodeColor(drones.Get(attackerIdx), 0, 255, 0);
        anim.UpdateNodeDescription(drones.Get(attackerIdx), "ATTACKER");
    }

    g_startTime = std::chrono::steady_clock::now();
    Simulator::Schedule(Seconds(progressInterval), &PrintProgress, simTime, progressInterval);
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();

    // AODV's own control traffic (RREQ/RREP/RERR) runs over this well-known port
    // in ns-3 -- see src/aodv/model/aodv-routing-protocol.h, RoutingProtocol::AODV_PORT.
    const uint16_t aodvPort = 654;

    // ---- Per-flow breakdown: match each flow back to its source/destination pair ----
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(fmh.GetClassifier());
    std::cout << "\n--- Per-pair breakdown ---" << std::endl;
    uint32_t zeroPdrSdPairs = 0;
    uint64_t controlTx = 0, controlRx = 0;
    for (auto& kv : monitor->GetFlowStats())
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        double flowPdr = kv.second.txPackets ? 100.0 * kv.second.rxPackets / kv.second.txPackets : 0.0;
        double flowE2e = kv.second.rxPackets ? kv.second.delaySum.GetSeconds() / kv.second.rxPackets : 0.0;

        std::string label = "unknown flow";
        bool isSdFlow = false;
        if (t.destinationPort >= sdPort && t.destinationPort < sdPort + 10)
        {
            uint32_t i = t.destinationPort - sdPort;
            isSdFlow = true;
            label = "SRC->DST pair " + std::to_string(i) + " (drone " + std::to_string(sources[i]) +
                    " -> drone " + std::to_string(destinations[i]) + ")";
        }
        else if (t.destinationPort >= gbsPort && t.destinationPort < gbsPort + 10)
        {
            uint32_t i = t.destinationPort - gbsPort;
            label = "DST->GBS  pair " + std::to_string(i) + " (drone " +
                    std::to_string(destinations[i]) + " -> GBS)";
        }
        else if (t.destinationPort == aodvPort || t.sourcePort == aodvPort)
        {
            label = "AODV control traffic";
            controlTx += kv.second.txPackets;
            controlRx += kv.second.rxPackets;
        }

        if (isSdFlow && flowPdr == 0.0)
        {
            zeroPdrSdPairs++;
        }

        std::cout << label << " | tx=" << kv.second.txPackets << " rx=" << kv.second.rxPackets
                   << " PDR(%)=" << flowPdr << " E2E(s)=" << flowE2e << std::endl;
    }

    std::cout << "\nSource->Destination pairs with 0% delivery: " << zeroPdrSdPairs << " out of 10"
               << std::endl;

    // ---- Aggregate summary (same as before, for comparison against Table VI) ----
    uint64_t tx = 0, rx = 0;
    double delaySum = 0;
    for (auto& kv : monitor->GetFlowStats())
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        bool isControl = (t.destinationPort == aodvPort || t.sourcePort == aodvPort);
        if (isControl)
        {
            continue; // control traffic is reported separately as overhead, not folded into data PDR/E2E
        }
        tx += kv.second.txPackets;
        rx += kv.second.rxPackets;
        delaySum += kv.second.delaySum.GetSeconds();
    }
    double pdr = tx ? 100.0 * rx / tx : 0;
    double e2e = rx ? delaySum / rx : 0;
    // Overhead, as defined in the paper: ratio of total control packets generated by the
    // routing protocol and received by the nodes, to the total number of data packets received.
    double ovh = rx ? (double)controlRx / (double)rx : 0;

    std::cout << "\n=== FINAL SUMMARY ===" << std::endl;
    std::cout << "nDrones=" << nDrones << " alpha=" << alpha << " attackerRatio=" << attackerRatio
               << " nAttackers=" << nAttackers << " PDR(%)=" << pdr << " E2E(s)=" << e2e
               << " OVH=" << ovh << " (controlTx=" << controlTx << " controlRx=" << controlRx << ")"
               << std::endl;

    Simulator::Destroy();
    return 0;
}
