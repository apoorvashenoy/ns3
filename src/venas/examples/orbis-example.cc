/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/** A simple example showing an mesh topology of point-to-point connected nodes
    generated using ORBIS. **/

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/topology-read-module.h"
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("OrbisExample");

int 
main (int argc, char *argv[])
{
  bool verbose = true;
  std::string orbis_file = "orbis/topos/hot30";

  CommandLine cmd;
  cmd.AddValue ("file", "File to read Orbis topology from", orbis_file);
  cmd.AddValue ("verbose", "Whether to print verbose log info", verbose);

  cmd.Parse (argc,argv);

  if (verbose)
    {
      LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
    }

  //Setup nodes/devices and install stacks/IP addresses
  OrbisTopologyReader topo_reader;
  topo_reader.SetFileName(orbis_file);
  NodeContainer routers = topo_reader.Read();
  NS_LOG_INFO("Nodes read from file: " + routers.GetN());

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  //For each link in topology, add a connection between routers and assign IP
  //addresses to the new network they've created between themselves.
  NetDeviceContainer router_devices;
  Ipv4AddressHelper address;
  address.SetBase ("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer router_interfaces;
  InternetStackHelper stack;
  stack.Install (routers);

  for (TopologyReader::ConstLinksIterator iter = topo_reader.LinksBegin();
       iter != topo_reader.LinksEnd(); iter++) {
    NodeContainer new_nodes;
    new_nodes.Add(iter->GetFromNode());
    new_nodes.Add(iter->GetToNode());

    NetDeviceContainer new_devs = pointToPoint.Install(new_nodes);
    router_devices.Add(new_devs);

    router_interfaces.Add(address.Assign (new_devs));
    address.NewNetwork();
  }
  //router_devices = pointToPoint.Install(routers.Get(2),routers.Get(3));

  //Application
  UdpEchoServerHelper echoServer (9);

  ApplicationContainer serverApps = echoServer.Install (routers.Get (0));
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (10.0));

  UdpEchoClientHelper echoClient (router_interfaces.GetAddress (0), 9);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

  ApplicationContainer clientApps = 
    echoClient.Install (routers.Get (2));
  clientApps.Start (Seconds (2.0));
  clientApps.Stop (Seconds (10.0));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  
  pointToPoint.EnablePcap("orbis-example",router_devices.Get(0),true);
  pointToPoint.EnablePcap("orbis-example",router_devices.Get(2),true);
  pointToPoint.EnablePcap("orbis-example",router_devices.Get(21),true);

  Simulator::Stop (Seconds (10.0));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
