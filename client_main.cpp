// AUTHOR: Zhaoning Kong
// Email: jonnykong@cs.ucla.edu

#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <iostream>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>
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
        m_svs(m_options.m_id,
              std::bind(&Program::processSyncUpdate, this, std::placeholders::_1)) {
    printf("SVS client %llu starts\n", m_options.m_id);

    // Suppress warning
    Interest::setDefaultCanBePrefix(true);
  }

  void run() {
    m_svs.registerPrefix();

    // Create other thread to run
    std::thread thread_svs([this] { m_svs.run(); });

    std::string init_msg = "User " +
                           boost::lexical_cast<std::string>(m_options.m_id) +
                           " has joined the groupchat";
    //m_svs.publishMsg(init_msg);
    m_svs.doUpdate();
    //TODO: application is not suppose to send msg to sync layer, only need to notify sync layer that new data has been generated using doUpdate()
    //m_svs.doUpdate();

    std::string userInput = "";


    while (true) {
      // send to Sync
      std::getline(std::cin, userInput);
      //m_svs.publishMsg(userInput);
      std::cout << "\n Message content: " << userInput << "/" << std::endl;
      m_svs.doUpdate();
      
    }

    thread_svs.join();
  }


 private:
  /**
   * onMsg() - Callback on receiving msg from sync layer.
   * processSyncUpdate() - Receive vector of updates
   */

  Face m_face;
  KeyChain m_keyChain;
  Scheduler m_scheduler;  // Use io_service from face
  std::unordered_map<Name, std::shared_ptr<const Data>> m_data_store;
  std::uint16_t seq_no = 0;  //initialize seq number for generating data name, this is current consistent with sync layer seq no. TODO: need to support arbitray data name  

  void processSyncUpdate(const std::vector<MissingDataInfo>& updates){
    for (const auto& update : updates){
      for (uint64_t i = update.lowSeq; i <= update.highSeq; i++){
        //print out updated node id/seq numbers
        std::cout << "\n Update:" << update.nodeID << "/" << i << std::endl;
      }
    }
  }

  void publishMsg(const std::string &msg)
  {
    printf(">> %s\n\n", msg.c_str());
    fflush(stdout);

    // Set data name
    auto n = MakeDataName(m_options.m_id, ++seq_no);
    std::shared_ptr<Data> data = std::make_shared<Data>(n);

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