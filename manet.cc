#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/dsdv-module.h"
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

uint32_t totalRoutingPackets = 0;
void TraceRoutingPackets(std::string context, const Ptr<const Packet> p, double signalStrength) {
  totalRoutingPackets++;
}

void setStaticMobility(uint32_t start, uint32_t numberOfNodes, NodeContainer nodes) {
  // Create a NodeContainer for the static nodes  
  NodeContainer staticNodes;
  for (uint32_t i = start; i < start + numberOfNodes; ++i) {
    staticNodes.Add(nodes.Get(i));
  }

  // Configure static mobility
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

  for (uint32_t i = 0; i < staticNodes.GetN(); ++i) {
    double x = (i % 10) * 45.0;
    double y = (i / 10) * 45.0;
    positionAlloc->Add(Vector(x, y, 0.0));  // Adjust positions as needed
  }

  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(staticNodes);
}

void setRandomMobility(uint32_t start, uint32_t numberOfNodes, NodeContainer nodes, double areaSize) {
  // Create a NodeContainer for the random mobility nodes
  NodeContainer randomNodes;
  for (uint32_t i = start; i < start + numberOfNodes; ++i) {
    randomNodes.Add(nodes.Get(i));
  }

// Configure static mobility
  MobilityHelper mobility;
  ObjectFactory pos;
  pos.SetTypeId("ns3::RandomRectanglePositionAllocator");
  pos.Set("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
  pos.Set("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));

  Ptr<PositionAllocator> positionAlloc = pos.Create()->GetObject<PositionAllocator>();
  
  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue ("ns3::UniformRandomVariable[Min=5.0|Max=25.0]"),
                             "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                             "PositionAllocator", PointerValue (positionAlloc));
  mobility.Install(randomNodes);
}



void startSimulation(uint32_t packet, uint32_t numOfNodes, std::string routingProtocol) {
  // Set simulation parameters
  double simulationTime = 200.0;
  uint32_t packetSize = packet;
  uint32_t numberOfNodes = numOfNodes;
  double txRange = 25.0;
  double areaSize = 500.0;

  std::cout << "Set packet size to " << packetSize << std::endl;
  std::cout << "Set transmission range to " << txRange << std::endl;
  std::cout << " " << std::endl;
  std::cout << " Running simulation..." << std::endl;

  // Create nodes
  NodeContainer nodes;
  nodes.Create(numberOfNodes);

  // Install WiFi
  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211g);
  YansWifiPhyHelper wifiPhy;
  wifiPhy.Set("TxPowerStart", DoubleValue(txRange));
  wifiPhy.Set("TxPowerEnd", DoubleValue(txRange));
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
  wifiPhy.SetChannel(wifiChannel.Create());
  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");
  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

  // Install Internet stack
  InternetStackHelper stack;

  if (routingProtocol == "dsdv") {
    DsdvHelper dsdv;
    stack.SetRoutingHelper(dsdv);
  } else if (routingProtocol == "aodv") {
    AodvHelper aodv;
    stack.SetRoutingHelper(aodv);
  } else {
    std::cout << "Invalid routing protocol specified. Using default (DSDV)." << std::endl;
    DsdvHelper dsdv;
    stack.SetRoutingHelper(dsdv);
  }

  stack.Install(nodes);

  // Install trace sources
  for (uint32_t i = 0; i < numberOfNodes; ++i) {
    if (i != 0) {
      std::ostringstream txTracePath;
      txTracePath << "/NodeList/" << i << "/DeviceList/0/$ns3::WifiNetDevice/Phy/PhyTxBegin";
      Config::Connect(txTracePath.str(), MakeCallback(&TraceRoutingPackets));
    }
  }

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Set mobility
  setStaticMobility(0, numberOfNodes / 2, nodes);
  setRandomMobility(numberOfNodes / 2, numberOfNodes / 2, nodes, areaSize);

  // Configure applications
  uint16_t port = 9;
  OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(interfaces.GetAddress(1), port)));
  onoff.SetConstantRate(DataRate("50kbps"));
  onoff.SetAttribute("PacketSize", UintegerValue(packetSize));
  ApplicationContainer apps = onoff.Install(nodes.Get(0));
  apps.Start(Seconds(1.0));
  apps.Stop(Seconds(simulationTime));

  PacketSinkHelper sink("ns3::UdpSocketFactory", Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
  apps = sink.Install(nodes.Get(1));
  apps.Start(Seconds(1.0));
  apps.Stop(Seconds(simulationTime));

  // Enable tracing with ASCII
  AsciiTraceHelper ascii;
  wifiPhy.EnableAsciiAll(ascii.CreateFileStream("store/" + routingProtocol + "-wifi.tr"));
  wifiPhy.EnablePcapAll("store/" + routingProtocol + "-pcap");  // Enable pcap tracing

  // Set up FlowMonitor (check data activities/flows between nodes)
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

  // Run simulation
  Simulator::Stop(Seconds(200));
  std::cout << "Set time to " << simulationTime << std::endl;

  // Enable NetAnim tracing
  AnimationInterface anim("store/" + routingProtocol + "-anim.xml");

  // Set up node colors and descriptions for NetAnim
  for (uint32_t i = 0; i < numberOfNodes; ++i) {
    anim.UpdateNodeDescription(nodes.Get(i), "Node" + std::to_string(i));
    anim.UpdateNodeColor(nodes.Get(i), 255, 0, 0); // Red color for nodes
  }

  // Set update interval for NetAnim
  anim.SetMobilityPollInterval(Seconds(1));
  anim.EnableIpv4RouteTracking("store/" + routingProtocol + "-anim-routes.routes", Seconds(0), Seconds(simulationTime), Seconds(1)); 

  Simulator::Run();
  std::cout << "Done! Printing results..." << std::endl;
  std::cout << " " << std::endl;

  // Collect and monitor results
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  uint64_t totalTxPackets = 0;
  uint64_t totalRxPackets = 0;
  uint64_t totalLostPackets = 0;
  Time totalDelay = Time(0);

  for (const auto& entry : stats) {
    FlowMonitor::FlowStats flowStats = entry.second;

    totalTxPackets += flowStats.txPackets;
    totalRxPackets += flowStats.rxPackets;
    totalLostPackets += flowStats.lostPackets;

    totalDelay += flowStats.delaySum; // Calculate end-to-end delay
  }

  std::cout << "Total Routing Packets: " << totalRoutingPackets << std::endl;
  std::cout << "Transmitted Packets: " << totalTxPackets << std::endl;
  std::cout << "Received Packets: " << totalRxPackets << std::endl;
  std::cout << "Lost Packets: " << totalLostPackets << std::endl;
  std::cout << " " << std::endl;

  double pdr = (totalRxPackets > 0) ? (static_cast<double>(totalRxPackets) / totalTxPackets) * 100 : 0.0;
  double routingOverhead = (totalTxPackets > 0) ? (static_cast<double>(totalRoutingPackets) / (totalTxPackets + totalRoutingPackets)) * 100 : 0.0;
  double avgDelay = (totalRxPackets > 0) ? (totalDelay.GetSeconds() / totalRxPackets) : 0.0;

  std::cout << "Packet Delivery Ratio (PDR): " << pdr << "%" << std::endl;
  std::cout << "Routing Overhead: " << routingOverhead << "%" << std::endl;
  std::cout << "End-to-End Delay: " << avgDelay << " seconds" << std::endl;

  Simulator::Destroy();
}


int main(int argc, char *argv[]) {
  std::string routingProtocol;
  bool validInput = false;

  while (!validInput) {
    std::cout << "Choose aodv or dsdv: ";
    std::cin >> routingProtocol;

    if (routingProtocol == "aodv" || routingProtocol == "dsdv") 
      validInput = true;
    else 
      std::cout << "Please enter 'aodv' or 'dsdv'." << std::endl;
  }

  std::cout << "----------Running simulation with 30 nodes----------" << std::endl;
  startSimulation(256, 30, routingProtocol);
  std::cout << "----------Simulation completed for 30 nodes----------" << std::endl;
  std::cout << " " << std::endl;
  return 0;
}
