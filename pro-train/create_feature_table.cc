#include <set>
#include <string>
#include <iostream>
#include <unordered_map>

#include <boost/algorithm/string/trim.hpp>

#include "filelib.h"
#include "fdict.h"
#include "hg.h"
#include "hg_io.h"

int main(int argc, char** argv) {

  if (argc != 2) {
    std::cerr << "Usage: program kbest/kbest.feats.gz" << std::endl;
    return 1;
  }

  int next_feat_id = 1;
  std::unordered_map<std::string, int> seen;

  const std::string& featsFile = argv[1];
  std::cerr << "Reading previous feature mapping from " << featsFile << std::endl;
  {
    ReadFile in_read(featsFile);
    std::istream &in=*in_read.stream();
    while(in) {
      std::string line;
      getline(in, line);
      
      if (line.empty()) continue;

      std::cerr << line << std::endl;

      std::istringstream is(line);
      int feat_id;
      std::string feat_name;
      // featID featName
      is >> feat_id >> feat_name;
      assert(seen.find(feat_name) == seen.end());
      seen[feat_name] = feat_id;
      next_feat_id = std::max(next_feat_id, feat_id + 1);

      std::cerr << "Read old mapping: " << feat_id << " " << feat_name << " " << next_feat_id << std::endl;
    }
  }

  // first, read in old kbest.feats.gz and fix those numbers from previous iterations
  // only then should we add new features from this iteration...

  ReadFile in_read("/dev/stdin");
  std::istream &in=*in_read.stream();

  while(in) {
    std::string line;
    getline(in, line);
    if (line.empty()) continue;

    std::istringstream is(line);
    int sent_id;
    std::string file;
    // path-to-file (JSON) sent_id
    is >> file >> sent_id;

    ReadFile rf(file);
    Hypergraph hg;
    HypergraphIO::ReadFromJSON(rf.stream(), &hg);
    std::cerr << ".";
    for (const Hypergraph::Edge edge : hg.edges_) {
      for (auto it = edge.feature_values_.begin(); it != edge.feature_values_.end(); ++it) {
            const std::pair<int,weight_t>& pair = *it;
            int cdec_feat_id = pair.first;
            if (cdec_feat_id >= 0 && cdec_feat_id <= FD::NumFeats()) {
              const std::string& feat_name = FD::Convert(cdec_feat_id);
              if (seen.find(feat_name) == seen.end()) {
                // haven't added this feature name yet...
                seen[feat_name] = next_feat_id;
                std::cerr << "New mapping: " << feat_name << " " << next_feat_id << std::endl;
                next_feat_id++;
              }
            } else {
              //std::cerr << "WARNING: Ignoring out of bounds feature ID: " << pair.first << std::endl;
            }
        }
    }
  }

  {
    WriteFile wf(featsFile);
    std::cerr << "Writing " << seen.size() << " mappings" << std::endl;
    for (const auto& pair : seen) {
      const std::string& feat_name = pair.first;
      int mapped_feat_id = pair.second;
      *wf << mapped_feat_id << " " << feat_name << std::endl;
    }
  }
}
