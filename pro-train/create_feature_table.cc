#include <set>
#include <string>
#include <iostream>

#include <boost/algorithm/string/trim.hpp>

#include "filelib.h"
#include "fdict.h"
#include "hg.h"
#include "hg_io.h"

int main(int argc, char** argv) {
  std::set<int> seen;

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
            if (pair.first >= 0 && pair.first <= FD::NumFeats()) {
              seen.insert(pair.first);
            } else {
              //std::cerr << "WARNING: Ignoring out of bounds feature ID: " << pair.first << std::endl;
            }
        }
    }
  }
  for (int fid : seen) {
    //std::cerr << fid << std::endl;
    const std::string name = FD::Convert(fid);
    std::cout << fid << " " << name << std::endl;
  }
}
