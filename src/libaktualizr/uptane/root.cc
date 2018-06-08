
#include "logging/logging.h"
#include "uptane/exceptions.h"
#include "uptane/tuf.h"

using Uptane::Root;

Root::Root(const TimeStamp &now, const std::string &repository, const Json::Value &json, Root &root)
    : Root(repository, json) {
  root.UnpackSignedObject(now, repository, json);
  this->UnpackSignedObject(now, repository, json);
}

Root::Root(const std::string &repository, const Json::Value &json) : policy_(Policy::Check) {
  if (!json.isObject() || !json.isMember("signed")) {
    throw Uptane::InvalidMetadata("", "", "invalid metadata json");
  }

  version_ = json["signed"]["version"].asInt();
  expiry_ = Uptane::TimeStamp(json["signed"]["expires"].asString());
  original_object_ = json;

  if (!json.isObject() || !json["signed"].isMember("keys") || !json["signed"].isMember("roles")) {
    throw Uptane::InvalidMetadata(repository, "root", "missing keys/roles field");
  }

  Json::Value keys = json["signed"]["keys"];
  for (Json::ValueIterator it = keys.begin(); it != keys.end(); ++it) {
    std::string key_type = boost::algorithm::to_lower_copy((*it)["keytype"].asString());
    if (key_type != "rsa" && key_type != "ed25519") {
      throw SecurityException(repository, "Unsupported key type: " + (*it)["keytype"].asString());
    }
    KeyId keyid = it.key().asString();
    PublicKey key(*it);
    keys_[keyid] = key;
  }

  Json::Value roles = json["signed"]["roles"];
  for (Json::ValueIterator it = roles.begin(); it != roles.end(); it++) {
    std::string role_name = it.key().asString();
    Role role = Role(role_name);
    if (role == Role::InvalidRole()) {
      LOG_WARNING << "Invalid role in root.json";
      LOG_TRACE << "Role name:" << role_name;
      LOG_TRACE << "root.json is:" << json;
      continue;
    }
    // Threshold
    int64_t requiredThreshold = (*it)["threshold"].asInt64();
    if (requiredThreshold < kMinSignatures) {
      // static_cast<int> is to stop << taking a reference to kMinSignatures
      // http://www.stroustrup.com/bs_faq2.html#in-class
      // this occurs in Boost 1.62 (and possibly other versions)
      LOG_DEBUG << "Failing with threshold for role " << role << " too small: " << requiredThreshold << " < "
                << static_cast<int>(kMinSignatures);
      throw IllegalThreshold(repository, "The role " + role.ToString() + " had an illegal signature threshold.");
    }
    if (kMaxSignatures < requiredThreshold) {
      // static_cast<int> is to stop << taking a reference to kMaxSignatures
      // http://www.stroustrup.com/bs_faq2.html#in-class
      // this occurs in Boost 1.62  (and possibly other versions)
      LOG_DEBUG << "Failing with threshold for role " << role << " too large: " << static_cast<int>(kMaxSignatures)
                << " < " << requiredThreshold;
      throw IllegalThreshold(repository, "root.json contains a role that requires too many signatures");
    }
    thresholds_for_role_[role] = requiredThreshold;

    // KeyIds
    Json::Value keyids = (*it)["keyids"];
    for (Json::ValueIterator itk = keyids.begin(); itk != keyids.end(); ++itk) {
      keys_for_role_.insert(std::make_pair(role, (*itk).asString()));
    }
  }
}

Json::Value Root::toJson() const {
  Json::Value res = BaseMeta::toJson();

  if (policy_ != Policy::Check) {
    throw Uptane::InvalidMetadata("", "root", "json representation will be invalid");
  }

  res["_type"] = "Root";
  res["consistent_snapshot"] = false;

  res["keys"] = Json::objectValue;
  std::map<KeyId, PublicKey>::const_iterator key_it;
  for (key_it = keys_.begin(); key_it != keys_.end(); key_it++) {
    res["keys"][key_it->first] = key_it->second.ToUptane();
  }
  // assuming that Root object has keys for all targets,
  //   should be a part of verification process
  res["roles"]["root"]["keyids"] = Json::arrayValue;
  res["roles"]["snapshot"]["keyids"] = Json::arrayValue;
  res["roles"]["targets"]["keyids"] = Json::arrayValue;
  res["roles"]["timestamp"]["keyids"] = Json::arrayValue;

  std::set<std::pair<Role, KeyId> >::const_iterator role_key_it;
  for (role_key_it = keys_for_role_.begin(); role_key_it != keys_for_role_.end(); role_key_it++) {
    res["roles"][role_key_it->first.ToString()]["keyids"].append(role_key_it->second);
  }

  auto th_it = thresholds_for_role_.find(Role::Root());
  if (th_it != thresholds_for_role_.end()) {
    res["roles"]["root"]["threshold"] = th_it->second;
  }

  th_it = thresholds_for_role_.find(Role::Snapshot());
  if (th_it != thresholds_for_role_.end()) {
    res["roles"]["snapshot"]["threshold"] = th_it->second;
  }

  th_it = thresholds_for_role_.find(Role::Targets());
  if (th_it != thresholds_for_role_.end()) {
    res["roles"]["targets"]["threshold"] = th_it->second;
  }

  th_it = thresholds_for_role_.find(Role::Timestamp());
  if (th_it != thresholds_for_role_.end()) {
    res["roles"]["timestamp"]["threshold"] = th_it->second;
  }

  return res;
}

void Uptane::Root::UnpackSignedObject(const TimeStamp &now, const std::string &repository,
                                      const Json::Value &signed_object) {
  Uptane::Role role(signed_object["signed"]["_type"].asString());
  if (policy_ == Policy::AcceptAll) {
    return;
  }
  if (policy_ == Policy::RejectAll) {
    throw SecurityException(repository, "Root policy is Policy::RejectAll");
  }
  assert(policy_ == Policy::Check);

  std::string canonical = Json::FastWriter().write(signed_object["signed"]);
  Json::Value signatures = signed_object["signatures"];
  int valid_signatures = 0;

  std::vector<std::string> sig_values;
  for (Json::ValueIterator sig = signatures.begin(); sig != signatures.end(); ++sig) {
    std::string signature = (*sig)["sig"].asString();
    if (std::find(sig_values.begin(), sig_values.end(), signature) != sig_values.end()) {
      throw NonUniqueSignatures(repository, role.ToString());
    }
    sig_values.push_back(signature);

    std::string method((*sig)["method"].asString());
    std::transform(method.begin(), method.end(), method.begin(), ::tolower);

    if (method != "rsassa-pss" && method != "rsassa-pss-sha256" && method != "ed25519") {
      throw SecurityException(repository, std::string("Unsupported sign method: ") + (*sig)["method"].asString());
    }
    std::string keyid = (*sig)["keyid"].asString();
    if (keys_.count(keyid) == 0u) {
      LOG_DEBUG << "Signed by unknown keyid, " << keyid << ", skipping";
      continue;
    }

    if (keys_for_role_.count(std::make_pair(role, keyid)) == 0u) {
      LOG_WARNING << "KeyId is not valid to sign for this role";
      LOG_TRACE << "KeyId: " << keyid;
      continue;
    }

    if (keys_[keyid].VerifySignature(signature, canonical)) {
      valid_signatures++;
    } else {
      LOG_WARNING << "Signature was present but invalid";
      LOG_TRACE << "  keyid:" << keyid;
      LOG_TRACE << "  signature:" << signature;
      // throw SecurityException(repository, "Invalid signature, verification failed");
    }
  }
  int threshold = thresholds_for_role_[role];
  if (threshold < kMinSignatures || kMaxSignatures < threshold) {
    throw IllegalThreshold(repository, "Invalid signature threshold");
  }
  if (valid_signatures < threshold) {
    throw UnmetThreshold(repository, role.ToString());
  }

  // TODO: check _type matches role
  // TODO: check timestamp
  Uptane::TimeStamp expiry(Uptane::TimeStamp(signed_object["signed"]["expires"].asString()));
  if (expiry.IsExpiredAt(now)) {
    LOG_WARNING << "Metadata expired at:" << expiry;
    throw ExpiredMetadata(repository, role.ToString());
  }

  Uptane::Role actual_role(Uptane::Role(signed_object["signed"]["_type"].asString()));
  if (role != actual_role) {
    LOG_ERROR << "Object was signed for a different role";
    LOG_TRACE << "  role:" << role;
    LOG_TRACE << "  actual_role:" << actual_role;
    throw SecurityException(repository, "Object was signed for a different role");
  }
}
