#include "custom-secondary.h"

#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>

#include <thread>
#include <chrono>

#include "crypto/crypto.h"
#include "utilities/fault_injection.h"
#include "utilities/utils.h"


bool custom_install(const std::string & data) {
  std::cout << "Data length: " << data.length() << std::endl;
  std::cout << "Data hash: " << boost::algorithm::hex(Crypto::sha256digest(data)) << std::endl;
  return true;
}

/* Comment out this sendFirmware, replaced with storeFirmware since more suitable for our update chain 
bool CustomSecondary::sendFirmware(const std::string& data) {
  if (custom_install(data)) {
    Utils::writeFile(sconfig.target_name_path, expected_target_name);
    Utils::writeFile(sconfig.target_size_path, expected_target_length);
    Utils::writeFile(sconfig.target_hash_path, boost::algorithm::to_lower_copy(expected_target_hashes[0].HashString()));
    Utils::writeFile(sconfig.firmware_path, content);
    
    std::this_thread::sleep_for (std::chrono::seconds(30));
    
    return true;
  } else {
    return false;
  }
}
*/

bool CustomSecondary::storeFirmware(const std::string& target_name, const std::string& content) {
  //Initialize the return_status_code to a value different from 0
  int return_status_code = 1;
  std::cout << "Saving firmware to the primary file-system" << std::endl;
  Utils::writeFile(sconfig.firmware_path, content);
  std::cout << "Extracting the update packet for " << sconfig.ecu_hardware_id << std::endl;
  //system("cd /var/sota/displayecu/ && unzip -o firmware-display");
  //return_status_code = system("python3 /var/sota/displayecu/dashboard_update_routine.py");
  char command_extract_firmware[256];
  char command_execute_script[256];
  char command_extension[sconfig.full_client_dir.size() + 1];
  strcpy(command_extension, sconfig.full_client_dir.c_str());
  snprintf(command_extract_firmware, 256, "cd %s && unzip -o firmware", command_extension);
  snprintf(command_execute_script, 256, "python3 %s/update_routine.py", command_extension);
  system(command_extract_firmware);
  std::cout << "Starting script for update installation on Secondary ECU..." << std::endl;
  return_status_code = system(command_execute_script);
  
  if (return_status_code == 0) {
    //The update is successful, so write the corresponding "metadata" to the Primary file-system
    std::cout << "Update successful!" << std::endl;
    Utils::writeFile(sconfig.target_name_path, expected_target_name);
    Utils::writeFile(sconfig.target_size_path, expected_target_length);
    Utils::writeFile(sconfig.target_hash_path, boost::algorithm::to_lower_copy(expected_target_hashes[0].HashString()));
    sync();
    return true;
  } else {
    //The update procedure has failed
    std::cout << "Update procedure failed!" << std::endl;
    return false;
  }
}

bool CustomSecondary::getFirmwareInfo(Uptane::InstalledImageInfo& firmware_info) const {
  std::string content;

  if (!boost::filesystem::exists(sconfig.target_name_path)) {
    firmware_info.name = std::string("noimage");
  } else {
    firmware_info.name = Utils::readFile(sconfig.target_name_path.string());
  }
  if (!boost::filesystem::exists(sconfig.target_hash_path)) {
    firmware_info.hash = std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  } else {
    firmware_info.hash = Utils::readFile(sconfig.target_hash_path.string());
  }
  if (!boost::filesystem::exists(sconfig.target_size_path)) {
    firmware_info.len = 0;
  } else {
    firmware_info.len = std::stoi(Utils::readFile(sconfig.target_size_path.string()));
  }

  return true;
}

