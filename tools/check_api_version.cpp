#include <iostream>

#include "google/protobuf/descriptor.h"
#include "generated/playout.grpc.pb.h"
#include "generated/playout.pb.h"

int main() {
  const google::protobuf::FileDescriptor* file_descriptor =
      google::protobuf::DescriptorPool::generated_pool()->FindFileByName("retrovue/playout.proto");

  if (file_descriptor == nullptr) {
    std::cerr << "Unable to locate playout proto descriptor." << std::endl;
    return 1;
  }

  if (!file_descriptor->options().HasExtension(retrovue::playout::PLAYOUT_API_VERSION)) {
    std::cerr << "PLAYOUT_API_VERSION extension not present in descriptor." << std::endl;
    return 1;
  }

  const auto& version =
      file_descriptor->options().GetExtension(retrovue::playout::PLAYOUT_API_VERSION);

  std::cout << "RetroVue Playout API version: " << version << std::endl;
  return 0;
}

