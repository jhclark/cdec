#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>

#include "ces.h"
#include "filelib.h"
#include "stringlib.h"
#include "sparse_vector.h"
#include "weights.h"
#include "scorer.h"
#include "viterbi.h"
#include "viterbi_envelope.h"
#include "inside_outside.h"
#include "error_surface.h"
#include "b64tools.h"
#include "hg_io.h"

using namespace std;
namespace po = boost::program_options;

void InitCommandLine(int argc, char** argv, po::variables_map* conf) {
  po::options_description opts("Configuration options");
  opts.add_options()
        ("input,i",po::value<string>()->default_value("-"), "Input file to map (- is STDIN)")
        ("weights,w",po::value<string>()->default_value(""), "Weights string (optional)")
        ("help,h", "Help");
  po::options_description dcmdline_options;
  dcmdline_options.add(opts);
  po::store(parse_command_line(argc, argv, dcmdline_options), *conf);
  bool flag = false;
  if (flag || conf->count("help")) {
    cerr << dcmdline_options << endl;
    exit(1);
  }
}

int main(int argc, char** argv) {
  po::variables_map conf;
  InitCommandLine(argc, argv, &conf);
  string file = conf["input"].as<string>();
  ReadFile rf(file);
  Hypergraph hg;
  HypergraphIO::ReadFromJSON(rf.stream(), &hg);

  vector<WordID> trans;
  prob_t vs = ViterbiESentence(hg, &trans);
  cerr << "  Viterbi logp: " << log(vs) << endl;
  cerr << "       Viterbi: " << TD::GetString(trans) << endl;

  string wstr = conf["weights"].as<string>();
  if(!wstr.empty()) {
    cerr << "Reweighting..." << endl;
    SparseVector<double> w;
    Weights::ReadSparseVectorString(wstr, &w);
    hg.Reweight(w);

    vs = ViterbiESentence(hg, &trans);
    cerr << "  Viterbi logp: " << log(vs) << endl;
    cerr << "       Viterbi: " << TD::GetString(trans) << endl;
  }
  cout << TD::GetString(trans) << endl;

  /*
  ViterbiEnvelopeWeightFunction wf(origin, axis);
  ViterbiEnvelope ve = Inside<ViterbiEnvelope, ViterbiEnvelopeWeightFunction>(hg, NULL, wf);
  ErrorSurface es;
  ComputeErrorSurface(*ds[sent_id], ve, &es, type, hg);
  cerr << "Viterbi envelope has " << ve.size() << " segments\n";
  cerr << "Error surface has " << es.size() << " segments\n";
  */

  return 0;
}
