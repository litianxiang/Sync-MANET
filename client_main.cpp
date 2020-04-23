// AUTHOR: Zhaoning Kong
// Email: jonnykong@cs.ucla.edu

#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <iostream>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/interest-filter.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <string>
#include <thread>
#include <vector>

#include "svs.hpp"

class Options {
 public:
  Options() : prefix("/ndn/svs") {}

 public:
  ndn::Name prefix;
  uint64_t m_id;
};

namespace ndn {
namespace svs {

class Program {
 public:
  explicit Program(const Options &options)
      : m_options(options),
        m_scheduler(m_face.getIoService()),
        m_svs(m_options.m_id,
              std::bind(&Program::processSyncUpdate, this, std::placeholders::_1)) {
    printf("SVS client %llu starts\n", m_options.m_id);

    // Suppress warning
    Interest::setDefaultCanBePrefix(true);
  }
public:
  void run() {
    m_svs.registerPrefix();

    m_face.setInterestFilter(InterestFilter(kSyncDataPrefix),
                           bind(&Program::onDataInterest, this, _2), nullptr);

    // Create other thread to run
    std::thread thread_svs([this] { m_svs.run(); });

    std::string init_msg = "User " +
                           boost::lexical_cast<std::string>(m_options.m_id) +
                           " has joined the groupchat";
    //m_svs.publishMsg(init_msg);
    m_svs.doUpdate();
    //TODO: application is not suppose to send msg to sync layer, 
    //only need to notify sync layer that new data has been generated using doUpdate()
    //m_svs.doUpdate();
    m_face.processEvents();

    std::string userInput = "";


    while (true) {
      // send to Sync
      std::getline(std::cin, userInput);
      //m_svs.publishMsg(userInput);
      std::cout << "\n Message content sent: " << userInput << "/" << std::endl;
      m_svs.doUpdate();
      
    }

    thread_svs.join();
  }


 private:
  /**
   * processSyncUpdate() - Receive vector of updates
   */

  Face m_face;
  KeyChain m_keyChain;
  Scheduler m_scheduler;  // Use io_service from face
  std::unordered_map<Name, std::shared_ptr<const Data>> m_data_store;
  std::uint16_t seq_no = 1;  //initialize seq number for generating data name, this is current consistent with sync layer seq no. TODO: need to support arbitray data name  

  //Generate Data Name format
  inline Name GenerateDataName(const NodeID &nid, uint64_t seq) {
    Name n(kSyncDataPrefix);
    n.appendNumber(nid).appendNumber(seq).appendNumber(0);
    std::cout << "generateDataName: " << n << std::endl;
    return n;
  }

  //generate new message, publish data, notify sync layer
  void publishMsg(const std::string &msg)
  {
    printf(">> %s\n\n", msg.c_str());
    fflush(stdout);

    printf("debug publishMsg 1");

    // Set data name
    auto n = GenerateDataName(m_options.m_id, ++seq_no);
    std::shared_ptr<Data> data = std::make_shared<Data>(n);

        printf("debug publishMsg 2");


    // Set data content
    Buffer contentBuf;
    for (size_t i = 0; i < msg.length(); ++i)
      contentBuf.push_back((uint8_t)msg[i]);
    data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());
    m_keyChain.sign(
        *data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
    data->setFreshnessPeriod(time::milliseconds(1000));

    m_data_store[n] = data;

    m_svs.doUpdate();
  }

  /**
   * onDataInterest() -
   */
  void onDataInterest(const Interest &interest) {
    const auto &n = interest.getName();
    auto iter = m_data_store.find(n);
    printf("Received interest: %s\n", n.toUri().c_str());


    // If have data, reply. Otherwise forward with probability
    if (iter != m_data_store.end()) {
      std::shared_ptr<Packet> packet;
      packet->packet_type = Packet::DATA_TYPE;
      packet->data = iter->second;

      if (n.compare(0, 3, kSyncDataPrefix) == 0) {
        m_face.put(*packet->data);
      }
    }
    else {
      assert(0);
    }
  }
  /**
   * onDataReply() - Save data to data store, and call application callback to
   *  pass the data northbound.
   */
  void onDataReply(const Data &data){
    const auto &n = data.getName();
    std::cout << "Debug: received data" << n << std::endl;
    NodeID nid_other = ExtractNodeID(n);

    // Drop duplicate data
    if (m_data_store.find(n) != m_data_store.end()) return;

    printf("Received data: %s\n", n.toUri().c_str());
    m_data_store[n] = data.shared_from_this();

    // Pass msg to application in format: <sender_id>:<content>
    size_t data_size = data.getContent().value_size();
    std::string content_str((char *)data.getContent().value(), data_size);
    content_str = boost::lexical_cast<std::string>(nid_other) + ":" + content_str;  
    printf("Message Received: %s\n", content_str.c_str());
  }

  /**
   * onNack() - Print error msg from NFD.
   */
  void onNack(const Interest &interest, const lp::Nack &nack) {
    std::cout << "received Nack with reason "
              << " for interest " << interest << std::endl;
  }

  /**
   * onTimeout() - Print timeout msg.
   */
  void onTimeout(const Interest &interest) {
    std::cout << "Timeout " << interest << std::endl;
  }

  // void onMsg(const std::string &msg) {
  //   // Parse received msg
  //   std::vector<std::string> result;
  //   size_t cursor = msg.find(":");
  //   result.push_back(msg.substr(0, cursor));
  //   if (cursor < msg.length() - 1)
  //     result.push_back(msg.substr(cursor + 1));
  //   else
  //     result.push_back("");

  //   // Print to stdout
  //   printf("User %s>> %s\n\n", result[0].c_str(), result[1].c_str());
  // }

  /**
   * processSyncUpdate() - Receive vector of updates, 
   * send data interest to fetch missing data
   */

  void processSyncUpdate(const std::vector<MissingDataInfo>& updates){
    for (const auto& update : updates){
      for (uint64_t i = update.lowSeq; i <= update.highSeq; i++){
        //print out updated node id/seq numbers
        std::cout << "\n Received Sync Update:" << update.nodeID << "/" << i << std::endl;
        //Send Data Interest to fetch missing data
        
        printf("debug: 0");

        auto n = GenerateDataName(update.nodeID,i); 
        
        printf("debug: 1");

        std::shared_ptr<Packet> packet = std::make_shared<Packet>();
        packet->packet_type = Packet::INTEREST_TYPE;
        packet->interest =
        std::make_shared<Interest>(n, time::milliseconds(1000));

        n = packet->interest->getName();

        printf("debug: 2");
        std::cout << "InterestName: " << n << std::endl;


        // Data Interest
        if (n.compare(0, 3, kSyncDataPrefix) == 0) {
          // Drop falsy data interest
          if (m_data_store.find(n) != m_data_store.end()) {
            std::cout << "\n Data Interest name Did not match" << std::endl;
          }

          m_face.expressInterest(*packet->interest,
                                 std::bind(&Program::onDataReply, this, _2),
                                 std::bind(&Program::onNack, this, _1, _2),
                                 std::bind(&Program::onTimeout, this, _1));

        printf("debug: 3");

        // pending_data_interest.push_back(std::make_shared<Packet>(packet));
        m_face.processEvents();
        }
      }
    }
  }  


  const Options m_options;
  SVS m_svs;
};

}  // namespace svs
}  // namespace ndn

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: TODO\n");
    exit(1);
  }

  Options opt;
  opt.m_id = std::stoll(argv[1]);

  ndn::svs::Program program(opt);
  program.run();
  return 0;
}