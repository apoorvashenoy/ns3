/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/** An experiment of network failure scenarios during disasters.  Randomly fails the given links/nodes
    within the given region, running a RonClient on each node, who contacts the chosen RonServer
    within the disaster region.  Can run this class multiple times, once for each set of parameters.  **/

#include "geocron-experiment.h"
#include "ron-trace-functions.h"
#include "failure-helper-functions.h"

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <sstream>
#include <iomanip>
#include <fstream>
#include <ctime>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("GeocronExperiment");
  //NS_OBJECT_ENSURE_REGISTERED (GeocronExperiment);

GeocronExperiment::GeocronExperiment ()
{
  appStopTime = Time (Seconds (30.0));
  simulationLength = Seconds (10.0);
  overlayPeers = Create<RonPeerTable> ();

  currLocation = "";
  currFprob = 0.0;
  currRun = 0;
  contactAttempts = 10;
  traceFile = "";
  nruns = 1;
}


void
GeocronExperiment::SetTimeout (Time newTimeout)
{
  timeout = newTimeout;
}


void
GeocronExperiment::NextHeuristic ()
{
  //heuristicIdx++;
}

void
GeocronExperiment::NextDisasterLocation ()
{
  return;
  //locationIdx++;
}


void
GeocronExperiment::NextFailureProbability ()
{
  return;
  //fpropIdx++;
}


////////////////////////////////////////////////////////////////////////////////
////////////////////   Helper Functions     ////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

bool
GeocronExperiment::IsDisasterNode (Ptr<Node> node)
{
  return disasterNodes[currLocation].count ((node)->GetId ());
}


void
GeocronExperiment::ReadLatencyFile (std::string latencyFile)
{
  // Load latency information if requested
  if (latencyFile != "")
    {
      if (! boost::filesystem::exists(latencyFile))
        {
          NS_LOG_ERROR("File does not exist: " + latencyFile);
          exit(-1);
        }
      latencies = RocketfuelTopologyReader::ReadLatencies (latencyFile);
    }
}


void
GeocronExperiment::ReadLocationFile (std::string locationFile)
{
  if (locationFile != "")
    {
      if (! boost::filesystem::exists (locationFile))
        {
          NS_LOG_ERROR("File does not exist: " + locationFile);
          exit(-1);
        }

      std::ifstream infile (locationFile.c_str ());
      std::string loc, line;
      double lat = 0.0, lon = 0.0;
      std::vector<std::string> parts; //for splitting up line

      while (std::getline (infile, line))
        {
          boost::split (parts, line, boost::is_any_of ("\t"));
          loc = parts[0];
          lat = boost::lexical_cast<double> (parts[1]);
          lon = boost::lexical_cast<double> (parts[2]);

          //std::cout << "Loc=" << loc << ", lat=" << lat << ", lon=" << lon << std::endl;
          locations[loc] = Vector (lat, lon, 1.0); //z position of 1 means we do have a location
        }
    }
}


/** Read network topology, store nodes and devices. */
void
GeocronExperiment::ReadTopology (std::string topologyFile)
{
  this->topologyFile = topologyFile;

  if (! boost::filesystem::exists(topologyFile)) {
    NS_LOG_ERROR("File does not exist: " + topologyFile);
    exit(-1);
  }
  
  RocketfuelTopologyReader topo_reader;
  topo_reader.SetFileName(topologyFile);
  nodes = topo_reader.Read();
  NS_LOG_INFO ("Nodes read from file: " + boost::lexical_cast<std::string> (nodes.GetN()));

  NS_LOG_INFO ("Assigning addresses and installing interfaces...");

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("100Gbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  // NixHelper to install nix-vector routing
  Ipv4NixVectorHelper nixRouting;
  nixRouting.SetAttribute("FollowDownEdges", BooleanValue (true));
  Ipv4StaticRoutingHelper staticRouting;
  Ipv4ListRoutingHelper routingList;
  routingList.Add (staticRouting, 0);
  routingList.Add (nixRouting, 10);
  InternetStackHelper stack;
  stack.SetRoutingHelper (routingList); // has effect on the next Install ()
  stack.Install (nodes);

  //For each link in topology, add a connection between nodes and assign IP
  //addresses to the new network they've created between themselves.
  Ipv4AddressHelper address;
  address.SetBase ("10.1.0.0", "255.255.255.252");
  //NetDeviceContainer router_devices;
  //Ipv4InterfaceContainer router_interfaces;

  NS_LOG_INFO ("Generating links and checking failure model.");

  for (TopologyReader::ConstLinksIterator iter = topo_reader.LinksBegin();
       iter != topo_reader.LinksEnd(); iter++) {
    std::string fromLocation = iter->GetAttribute ("From Location");
    std::string toLocation = iter->GetAttribute ("To Location");

    //NS_LOG_INFO ("Link from " << fromLocation << " to " << toLocation);

    // Set latency for this link if we loaded that information
    if (latencies.size())
      {
        std::map<std::string, std::string>::iterator latency = latencies.find (fromLocation + " -> " + toLocation);
        //if (iter->GetAttributeFailSafe ("Latency", latency))
        
        if (latency != latencies.end())
          {
            pointToPoint.SetChannelAttribute ("Delay", StringValue (latency->second + "ms"));
          }
        else
          {
            latency = latencies.find (toLocation + " -> " + fromLocation);
            if (latency != latencies.end())
              pointToPoint.SetChannelAttribute ("Delay", StringValue (latency->second + "ms"));
            else
              pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));
          }
      }

    Ptr<Node> from_node = iter->GetFromNode();
    Ptr<Node> to_node = iter->GetToNode();
    NodeContainer both_nodes (from_node);
    both_nodes.Add (to_node);

    NetDeviceContainer new_devs = pointToPoint.Install (both_nodes);
    NetDeviceContainer from_dev;
    from_dev.Add(new_devs.Get(0));
    NetDeviceContainer to_dev;
    to_dev.Add(new_devs.Get(1));
    //router_devices.Add(new_devs);

    Ipv4InterfaceContainer new_interfaces = address.Assign (new_devs);
    //router_interfaces.Add(new_interfaces);
    address.NewNetwork();

    // Mobility model to set positions for geographically-correlated information
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAllocator = CreateObject<ListPositionAllocator> ();
    Vector fromPosition = locations.count (fromLocation)  ? (locations.find (fromLocation))->second : Vector (0.0, 0.0, 0.0);
    Vector toPosition = locations.count (toLocation) ? (locations.find (toLocation))->second : Vector (0.0, 0.0, 0.0);
    positionAllocator->Add (fromPosition);
    positionAllocator->Add (toPosition);
    mobility.SetPositionAllocator (positionAllocator);
    mobility.Install (both_nodes);

    // If the node is in a disaster region, add it to the corresponding list
    // Also add potential servers (randomly chosen from outside that disaster region)
    for (std::vector<std::string>::iterator disasterLocation = disasterLocations->begin ();
         disasterLocation != disasterLocations->end (); disasterLocation++)
      {
        if (fromLocation == *disasterLocation)
          {
            disasterNodes[*disasterLocation].insert(std::pair<uint32_t, Ptr<Node> > (from_node->GetId (), from_node));
          }
        // this check made each time 1 link added, so if the node reaches our min-degree it should be added ONCE
        else if (from_node->GetNDevices () == maxNDevs + 1)
          serverNodeCandidates[*disasterLocation].Add (from_node);

        if (toLocation == *disasterLocation)
          {
            disasterNodes[*disasterLocation].insert(std::pair<uint32_t, Ptr<Node> > (to_node->GetId (), to_node));
          }
        else if (to_node->GetNDevices () == maxNDevs + 1)
          serverNodeCandidates[*disasterLocation].Add (to_node);

        // Failure model: if either endpoint is in the disaster location,
        // add it to the list of potential ifaces to kill
        if (iter->GetAttribute ("From Location") == *disasterLocation ||
            iter->GetAttribute ("To Location") == *disasterLocation)
          //random.GetValue () <= currFprob)
          {
            potentialIfacesToKill[*disasterLocation].Add(new_interfaces.Get (0));
            potentialIfacesToKill[*disasterLocation].Add(new_interfaces.Get (1));
          }
      }
  } //end link iteration

  NS_LOG_INFO ("Topology finished.  Choosing & installing clients.");

  NodeContainer overlayNodes;

  for (NodeContainer::Iterator node = nodes.Begin ();
       node != nodes.End (); node++)
    {
      // Sanity check that a node has some actual links, otherwise remove it from the simulation
      // this happened with some disconnected Rocketfuel models and made null pointers
      if ((*node)->GetNDevices () <= 1)
        {
          NS_LOG_INFO ("Node " << (*node)->GetId () << " has no links!");
          //node.erase ();
          disasterNodes[currLocation].erase ((*node)->GetId ());
          continue;
        }
      
      // We may only install the overlay application on clients attached to stub networks,
      // so we just choose the stub network nodes here
      // (note that all nodes have a loopback device)
      if (!maxNDevs or (*node)->GetNDevices () <= maxNDevs) 
        {
          overlayNodes.Add (*node);
          overlayPeers->AddPeer (*node);
          // OLD SILLY HEURISTIC  $$$$//
          // Only add address of overlay if we're contacting local peers and it is one,
          // or if it's outside of the local region
          //if ((disasterNodes[currLocation].count ((*node)->GetId ()) != 0) or
          // TODO: this is a bug, only works if choosing external nodes only.  If use_local_overlays is true,
          // all overlay nodes will become candidates
          //(!use_local_overlays and disasterNodes[currLocation].count ((*node)->GetId ()) == 0))
        }
    }
      
  RonClientHelper ronClient (Ipv4Address (), 9);
  ronClient.SetAttribute ("Interval", TimeValue (Seconds (1.)));
  ronClient.SetAttribute ("PacketSize", UintegerValue (1024));
  ronClient.SetAttribute ("Timeout", TimeValue (timeout));
  
  // Install client app and set number of server contacts they make based on whether all nodes
  // should report the disaster or only the ones in the disaster region.
  for (NodeContainer::Iterator node = overlayNodes.Begin ();
       node != overlayNodes.End (); node++)
    {
      ApplicationContainer newApp = ronClient.Install (*node);
      clientApps.Add (newApp);
      Ptr<RonClient> newClient = DynamicCast<RonClient> (newApp.Get (0));
      newClient->SetAttribute ("MaxPackets", UintegerValue (0)); //explicitly enable sensor reporting when disaster area set
      newClient->SetPeerTable (overlayPeers); //TODO: make this part of the helper?? or some p2p overlay algorithm? or the table itself? give out a callback function with that nodes peers?
    }
  
  clientApps.Start (Seconds (2.0));
  clientApps.Stop (appStopTime);
}


void
GeocronExperiment::SetDisasterLocation (std::string newDisasterLocation)
{
  currLocation = newDisasterLocation;
}


void
GeocronExperiment::SetFailureProbability (double newFailureProbability)
{
  currFprob = newFailureProbability;
}


/*void
GeocronExperiment::SetHeuristic (newHeuristic)
{

}*/



void
GeocronExperiment::SetTraceFile (std::string newTraceFile)
{
  NS_LOG_INFO ("New trace file is: " + newTraceFile);

  traceFile = newTraceFile;
}


void
GeocronExperiment::AutoSetTraceFile ()
{
  boost::filesystem::path newTraceFile ("ron_output");
  newTraceFile /= boost::filesystem::path(topologyFile).stem ();
  newTraceFile /= boost::algorithm::replace_all_copy (currLocation, " ", "_");

  // round the fprob to 1 decimal place
  std::ostringstream fprob;
  std::setprecision (1);
  fprob << currFprob;
  newTraceFile /= fprob.str ();
  //newTraceFile /= boost::lexical_cast<std::string> (currFprob);

  // extract unique filename from heuristic to summarize parameters, aggregations, etc.
  TypeId::AttributeInformation info;
  NS_ASSERT (currHeuristic->GetTypeId ().LookupAttributeByName ("SummaryName", &info));
  StringValue summary;
  info.checker->Check (summary);
  newTraceFile /= summary.Get ();

  // set output number if start_run_number is specified
  uint32_t outnum = currRun;
  if (start_run_number and nruns > 1)
    outnum = currRun + start_run_number;
  
  std::string fname = "run";
  fname += boost::lexical_cast<std::string> (outnum);
  newTraceFile /= (fname);
  newTraceFile.replace_extension(".out");

  // Change name to avoid overwriting
  uint32_t copy = 0;
  while (boost::filesystem::exists (newTraceFile))
    {
      newTraceFile.replace_extension (".out(" + boost::lexical_cast<std::string> (copy++) + ")");
    }

  boost::filesystem::create_directories (newTraceFile.parent_path ());

  SetTraceFile (newTraceFile.string ());
}


/** Run through all the possible combinations of disaster locations, failure probabilities,
    heuristics, and other parameters for the given number of runs. */
void
GeocronExperiment::RunAllScenarios ()
{
  SeedManager::SetSeed(std::time (NULL));
  int runSeed = 0;
  for (std::vector<std::string>::iterator disasterLocation = disasterLocations->begin ();
       disasterLocation != disasterLocations->end (); disasterLocation++)
    {
      SetDisasterLocation (*disasterLocation); 
      for (std::vector<double>::iterator fprob = failureProbabilities->begin ();
           fprob != failureProbabilities->end (); fprob++)
        {
          SetFailureProbability (*fprob);
          for (currRun = 0; currRun < nruns; currRun++)
            {
              // We want to compare each heuristic to each other for each configuration of failures
              ApplyFailureModel ();
              SetNextServer ();
              for (uint32_t h = 0; h < heuristics->size (); h++)
                {
                  currHeuristic = heuristics->at (h);
                  SeedManager::SetRun(runSeed++);
                  AutoSetTraceFile ();
                  Run ();
                }
              UnapplyFailureModel ();
            }
        }
    }
}


/** Connect the defined traces to the specified client applications. */
void
GeocronExperiment::ConnectAppTraces ()
{
  //need to prevent stupid waf from throwing errors for unused definitions...
  (void) PacketForwarded;
  (void) PacketSent;
  (void) AckReceived;

  if (traceFile != "")
    {
      Ptr<OutputStreamWrapper> traceOutputStream;
      AsciiTraceHelper asciiTraceHelper;
      traceOutputStream = asciiTraceHelper.CreateFileStream (traceFile);
  
      for (ApplicationContainer::Iterator itr = clientApps.Begin ();
           itr != clientApps.End (); itr++)
        {
          Ptr<RonClient> app = DynamicCast<RonClient> (*itr);
          app->ConnectTraces (traceOutputStream);
        }
    }
}


//////////////////////////////////////////////////////////////////////
/////************************************************************/////
////////////////        APPLY FAILURE MODEL         //////////////////
/////************************************************************/////
//////////////////////////////////////////////////////////////////////
/**   Fail the links that were chosen with given probability */
void
GeocronExperiment::ApplyFailureModel () {
  NS_LOG_INFO ("Applying failure model.");

  // Keep track of these and unfail them later
  failNodes = NodeContainer ();
  for (std::map<uint32_t, Ptr <Node> >::iterator nodeItr = disasterNodes[currLocation].begin ();
       nodeItr != disasterNodes[currLocation].end (); nodeItr++)
    {
      Ptr<Node> node = nodeItr->second;
      // Fail nodes within the disaster region with some probability
      if (random.GetValue () < currFprob)
        {
          failNodes.Add (node);
          NS_LOG_LOGIC ("Node " << (node)->GetId () << " will fail.");
        }
    }
  for (NodeContainer::Iterator node = failNodes.Begin ();
       node != failNodes.End (); node++)
    {
      FailNode (*node);
    }

  // Same with these
  ifacesToKill = Ipv4InterfaceContainer ();
  for (Ipv4InterfaceContainer::Iterator iface = potentialIfacesToKill[currLocation].Begin ();
       iface != potentialIfacesToKill[currLocation].End (); iface++)
    {
      if (random.GetValue () < currFprob)
        ifacesToKill.Add (*iface);
    }
  for (Ipv4InterfaceContainer::Iterator iface = ifacesToKill.Begin ();
       iface != ifacesToKill.End (); iface++)
    {
        FailIpv4 (iface->first, iface->second);
    }
}


void
GeocronExperiment::UnapplyFailureModel () {
  // Unfail the links that were chosen
  for (Ipv4InterfaceContainer::Iterator iface = ifacesToKill.Begin ();
       iface != ifacesToKill.End (); iface++)
    {
      UnfailIpv4 (iface->first, iface->second);
    }

  // Unfail the nodes that were chosen
  for (NodeContainer::Iterator node = failNodes.Begin ();
       node != failNodes.End (); node++)
    {
      UnfailNode (*node, appStopTime);
    }


  clientApps.Start (Seconds (2.0));
  clientApps.Stop (appStopTime);
}


void
GeocronExperiment::SetNextServer () {
  NS_LOG_LOGIC ("Choosing from " << serverNodeCandidates[currLocation].GetN () << " server provider candidates.");

  Ptr<Node> serverNode = (serverNodeCandidates[currLocation].GetN () ?
                          serverNodeCandidates[currLocation].Get (random.GetInteger (0, serverNodeCandidates[currLocation].GetN () - 1)) :
                          nodes.Get (random.GetInteger (0, serverNodeCandidates[currLocation].GetN () - 1)));
  Ipv4Address serverAddress = GetNodeAddress (serverNode);

  NS_LOG_INFO ("Server is at: " << serverAddress);

  //Application
  RonServerHelper ronServer (9);

  ApplicationContainer serverApps = ronServer.Install (serverNode);
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (appStopTime);

  serverPeer = Create<RonPeerEntry> (serverNode);
}


void
GeocronExperiment::Run ()
{
  // Set some parameters for this run
  for (std::map<uint32_t, Ptr <Node> >::iterator nodeItr = disasterNodes[currLocation].begin ();
       nodeItr != disasterNodes[currLocation].end (); nodeItr++)
    /*  for (ApplicationContainer::Iterator app = disasterNodes[currLocation].Begin ();
        app != clientApps.End (); app++)*/
    {
      for (uint32_t i = 0; i < nodeItr->second->GetNApplications (); i++) {
        Ptr<RonClient> ronClient = DynamicCast<RonClient> (nodeItr->second->GetApplication (0));
        //if (ronClient == NULL)
        //continue;
        //TODO: different heuristics
        Ptr<RonPathHeuristic> heuristic = currHeuristic->Create<RonPathHeuristic> ();
        // Must set heuristic first so that source will be set and heuristic can make its heap
        ronClient->SetHeuristic (heuristic);
        ronClient->SetRemotePeer (serverPeer);
        heuristic->SetPeerTable (overlayPeers);
        ronClient->SetAttribute ("MaxPackets", UintegerValue (contactAttempts));
      }
    }

  //NS_LOG_INFO ("Populating routing tables; please be patient it takes a while...");
  //Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  //NS_LOG_INFO ("Done populating routing tables!");
  
  ConnectAppTraces ();

  // pointToPoint.EnablePcap("rocketfuel-example",router_devices.Get(0),true);

  NS_LOG_UNCOND ("Starting simulation on map file " << topologyFile << ": " << std::endl
                 << nodes.GetN () << " total nodes" << std::endl
                 << overlayPeers->GetN () << " total overlay nodes" << std::endl
                 << disasterNodes[currLocation].size () << " nodes in " << currLocation << " total" << std::endl
                 //TODO: get size from table <<  << " overlay nodes in " << currLocation << std::endl
                 << std::endl << "Failure probability: " << currFprob << std::endl
                 << failNodes.GetN () << " nodes failed" << std::endl
                 << ifacesToKill.GetN () / 2 << " links failed");

  Simulator::Stop (simulationLength);
  Simulator::Run ();
  Simulator::Destroy ();

  NS_LOG_INFO ("Next simulation run...");

  //serverNode->GetObject<Ipv4NixVectorRouting> ()->FlushGlobalNixRoutingCache ();
  
  // reset apps
  for (ApplicationContainer::Iterator app = clientApps.Begin ();
       app != clientApps.End (); app++)
    {
      Ptr<RonClient> ronClient = DynamicCast<RonClient> (*app);
      ronClient->Reset ();
    }
}
    /* Was used in a (failed) attempt to mimic the actual addresses from the file to exploit topology information...


  uint32_t network_mask = 0xffffff00;
  uint32_t host_mask = 0x000000ff;
  Ipv4Mask ip_mask("255.255.255.0");

    Ipv4Address from_addr = Ipv4Address(iter->GetAttribute("From Address").c_str());
    Ipv4Address to_addr = Ipv4Address(iter->GetAttribute("To Address").c_str());
    // Try to find an unused address near to this one (hopefully within the subnet)    while (Ipv4AddressGenerator::IsAllocated(from_addr) ||
           ((from_addr.Get() & host_mask) == 0) ||
           ((from_addr.Get() & host_mask) == 255)) {
      from_addr.Set(from_addr.Get() + 1);
    }

    address.SetBase (Ipv4Address( from_addr.Get() & network_mask), ip_mask, Ipv4Address( from_addr.Get() & host_mask));
    Ipv4InterfaceContainer from_iface = address.Assign (from_dev);

    // Try to find an unused address near to this one (hopefully within the subnet)
    while (Ipv4AddressGenerator::IsAllocated(to_addr) ||
           ((to_addr.Get() & host_mask) == 0) ||
           ((to_addr.Get() & host_mask) == 255)) {
      to_addr.Set(to_addr.Get() + 1);
    }

    address.SetBase (Ipv4Address( to_addr.Get() & network_mask), ip_mask, Ipv4Address( to_addr.Get() & host_mask));
    Ipv4InterfaceContainer to_iface = address.Assign (to_dev);

    router_interfaces.Add(from_iface);
    router_interfaces.Add(to_iface);*/



        //Attributes
        /*        for (TopologyReader::Link::ConstAttributesIterator attr = iter->AttributesBegin();
             attr != iter->AttributesEnd(); attr++) {
          //NS_LOG_INFO("Attribute: " + boost::lexical_cast<std::string>(attr->second));
          }*/
