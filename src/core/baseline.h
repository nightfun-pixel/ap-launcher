#pragma once
// The pinned 0.1.0 baseline for Azur Promilia JP 1.0.2 resource revision 3219598. It mirrors
// studio/ops/launcher_targets.json and is used only for the offline idle gate; an actual install
// still validates the fetched manifest.

#include <cstdint>
#include <string>
#include <vector>

namespace blueward {

struct BaselineTarget {
    std::string target;  // game-root-relative, forward slashes
    std::uint64_t size = 0;
    std::string sha256;
};

inline const std::vector<BaselineTarget>& Baseline3219598() {
    static const std::vector<BaselineTarget> targets = {
        {"AzurPromiliaJP_Data/.res/raw_base_package/BundleFiles/z5xzl3cg6ao0ilpbzykexg.r", 133622104,
         "99b8129d49586d26745630a76423609123ddcb1a7aad3e0a971886b13336a878"},
        {"AzurPromiliaJP_Data/.res/raw_streaming_package/BundleFiles/fcl0wxz2z6hoffogv4xgzw.r", 28840170,
         "296c11b437e5817ae5144ecd6096e5884f4cf9c35b630a01aa9c33024839d599"},
        {"AzurPromiliaJP_Data/.res/default_package/BundleFiles/yr49bxop0f3reujfsmyfca.r", 4993892,
         "701f04842f5156da177bd4946a1cf9b7c055f2a614003c2c13c8567fa14e0fc3"},
        {"AzurPromiliaJP_Data/StreamingAssets/.res/static_package/6pq4a7hlp7zidu91dvvww.r", 58707878,
         "c7c80b98b263f161583a2ab52a5b7039636acbabcac8c365f9f37224d11bc4b3"},
        {"AzurPromiliaJP_Data/StreamingAssets/.res/default_package/hqqoy0ojvzpggukcf5m85w.r", 5109385,
         "a584d52f66f66a32dc2a2e9bfd7a119eed10290fc79795b6730896a5440d2a7f"},
    };
    return targets;
}

}  // namespace blueward
