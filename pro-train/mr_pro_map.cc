#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include <boost/functional/hash.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "sampler.h"
#include "filelib.h"
#include "stringlib.h"
#include "weights.h"
#include "inside_outside.h"
#include "hg_io.h"
#include "kbest.h"
#include "viterbi.h"
#include "ns.h"
#include "ns_docscorer.h"

// This is Figure 4 (Algorithm Sampler) from Hopkins&May (2011)

using namespace std;
namespace po = boost::program_options;

struct ApproxVectorHasher {
  static const size_t MASK = 0xFFFFFFFFull;
  union UType {
    double f;   // leave as double
    size_t i;
  };
  static inline double round(const double x) {
    UType t;
    t.f = x;
    size_t r = t.i & MASK;
    if ((r << 1) > MASK)
      t.i += MASK - r + 1;
    else
      t.i &= (1ull - MASK);
    return t.f;
  }
  size_t operator()(const SparseVector<weight_t>& x) const {
    size_t h = 0x573915839;
    for (SparseVector<weight_t>::const_iterator it = x.begin(); it != x.end(); ++it) {
      UType t;
      t.f = it->second;
      if (t.f) {
        size_t z = (t.i >> 32);
        boost::hash_combine(h, it->first);
        boost::hash_combine(h, z);
      }
    }
    return h;
  }
};

struct ApproxVectorEquals {
  bool operator()(const SparseVector<weight_t>& a, const SparseVector<weight_t>& b) const {
    SparseVector<weight_t>::const_iterator bit = b.begin();
    for (SparseVector<weight_t>::const_iterator ait = a.begin(); ait != a.end(); ++ait) {
      if (bit == b.end() ||
          ait->first != bit->first ||
          ApproxVectorHasher::round(ait->second) != ApproxVectorHasher::round(bit->second))
        return false;
      ++bit;
    }
    if (bit != b.end()) return false;
    return true;
  }
};

boost::shared_ptr<MT19937> rng;

void InitCommandLine(int argc, char** argv, po::variables_map* conf) {
  po::options_description opts("Configuration options");
  opts.add_options()
        ("reference,r",po::value<vector<string> >(), "[REQD] Reference translation (tokenized text)")
        ("weights,w",po::value<string>(), "[REQD] Weights files from current iterations")
        ("kbest_repository,K",po::value<string>()->default_value("./kbest"),"K-best list repository (directory)")
        ("input,i",po::value<string>()->default_value("-"), "Input file to map (- is STDIN)")
        ("source,s",po::value<string>()->default_value(""), "Source file (ignored, except for AER)")
        ("evaluation_metric,m",po::value<string>()->default_value("IBM_BLEU"), "Evaluation metric (ibm_bleu, koehn_bleu, nist_bleu, ter, meteor, etc.)")
        ("kbest_size,k",po::value<unsigned>()->default_value(1500u), "Top k-hypotheses to extract")
        ("candidate_pairs,G", po::value<unsigned>()->default_value(5000u), "Number of pairs to sample per hypothesis (Gamma)")
        ("best_pairs,X", po::value<unsigned>()->default_value(50u), "Number of pairs, ranked by magnitude of objective delta, to retain (Xi)")
        ("prune_kbest_by_length_hammer", po::value<int>()->default_value(0), "Require the 'LengthHammer' feature to have a value of zero for all extracted k-best entries? (prior to diffing) -- Use 0 for false and 1 for true")
        ("random_seed,S", po::value<uint32_t>(), "Random seed (if not specified, /dev/random will be used)")
        ("help,h", "Help");
  po::options_description dcmdline_options;
  dcmdline_options.add(opts);
  po::store(parse_command_line(argc, argv, dcmdline_options), *conf);
  bool flag = false;
  if (!conf->count("reference")) {
    cerr << "Please specify one or more references using -r <REF.TXT>\n";
    flag = true;
  }
  if (!conf->count("weights")) {
    cerr << "Please specify weights using -w <WEIGHTS.TXT>\n";
    flag = true;
  }
  if (flag || conf->count("help")) {
    cerr << dcmdline_options << endl;
    exit(1);
  }
}

struct HypInfo {
  HypInfo() : g_(-100.0f) {}
  HypInfo(const vector<WordID>& h, const SparseVector<weight_t>& feats) : hyp(h), g_(-100.0f), x(feats) {}

  // lazy evaluation
  double g(const SegmentEvaluator& scorer, const EvaluationMetric* metric) const {
    if (g_ == -100.0f) {
      SufficientStats ss;
      scorer.Evaluate(hyp, &ss);
      g_ = metric->ComputeScore(ss);
    }
    return g_;
  }
  vector<WordID> hyp;
  mutable float g_;
  SparseVector<weight_t> x;
};

struct HypInfoCompare {
  bool operator()(const HypInfo& a, const HypInfo& b) const {
    ApproxVectorEquals comp;
    return (a.hyp == b.hyp && comp(a.x,b.x));
  }
};

struct HypInfoHasher {
  size_t operator()(const HypInfo& x) const {
    boost::hash<vector<WordID> > hhasher;
    ApproxVectorHasher vhasher;
    size_t ha = hhasher(x.hyp);
    boost::hash_combine(ha, vhasher(x.x));
    return ha;
  }
};

void WriteKBest(const string& file, const vector<HypInfo>& kbest, const vector<int> fid_to_kbest_ids) {
  WriteFile wf(file);
  ostream& out = *wf.stream();
  out.precision(10);
  for (int i = 0; i < kbest.size(); ++i) {
    const HypInfo& info = kbest.at(i);
    const SparseVector<weight_t>& feats = info.x;

    out << TD::GetString(info.hyp) << endl;
    //out << feats << endl;
    //for (typename FastSparseVector::const_iterator it = other.begin(); it != end; ++it) {
    for (auto pair : feats) {
      // note: we're writing the integer feature ID instead of the name
      // so that gzip gets a better compression ratio (~5X for Jon's large feature sets)
      // we'll also need the full mapping of feature ID's to feature names to read this file later
      // since this mapping is valid only within the current process
      assert(pair.first != 0);
      if (pair.first >= fid_to_kbest_ids.size()) {
        cerr << "ERROR: Feature not found in kbest feature mapping: " << pair.first << endl;
      }
      int kbest_feat_id = fid_to_kbest_ids.at(pair.first);
      out << " " << pair.first << "=" << pair.second;
    }
    out << endl;
  }
}

// warning: mutates line
void ParseSparseVector(string& line, const std::unordered_map<int, string>& feat_names, size_t cur, SparseVector<weight_t>* out) {
  SparseVector<weight_t>& x = *out;
  size_t last_start = cur;
  size_t last_equals = string::npos;
  while (cur <= line.size()) {
    if (line[cur] == ' ' || cur == line.size()) {
      if (!(cur > last_start && last_equals != string::npos && cur > last_equals)) {
        cerr << "[ERROR] " << line << endl << "  position = " << cur << endl;
        exit(1);
      }

      const string old_fid_str = line.substr(last_start, last_equals - last_start);
      int fid;
      if (feat_names.size() == 0) {
        // no feature names were read, so just use the string directly
        fid = FD::Convert(old_fid_str);
      } else {
        int old_fid;
        if (!(stringstream(old_fid_str) >> old_fid)) {
          cerr << "Invalid feature ID: " << old_fid << endl;
          abort();
        }
        if (old_fid == 0 || old_fid >= feat_names.size()) {
          cerr << "ERROR: Got an old feature ID that is out of range for the feature names file: " << old_fid << endl;
          abort();
        }
        const string feat_name = feat_names.at(old_fid);
        fid = FD::Convert(feat_name);
      }
      if (cur < line.size()) line[cur] = 0;
      const double val = strtod(&line[last_equals + 1], NULL);
      x.set_value(fid, val);

      last_equals = string::npos;
      last_start = cur+1;
    } else {
      if (line[cur] == '=')
        last_equals = cur;
    }
    ++cur;
  }
}

void ReadKBest(const string& file, const std::unordered_map<int, string>& feat_names, vector<HypInfo>* kbest) {
  cerr << "Reading from " << file << endl;
  ReadFile rf(file);
  istream& in = *rf.stream();
  string cand;
  string feats;
  while(getline(in, cand)) {
    getline(in, feats);
    assert(in);
    std::cerr << "CAND: " << cand << std::endl;
    std::cerr << "FEATS: " << feats << std::endl;
    kbest->push_back(HypInfo());
    TD::ConvertSentence(cand, &kbest->back().hyp);
    boost::trim(feats);
    ParseSparseVector(feats, feat_names, 0, &kbest->back().x);
  }
  cerr << "  read " << kbest->size() << " hypotheses\n";
}

void Dedup(vector<HypInfo>* h) {
  cerr << "Dedup in=" << h->size();
  tr1::unordered_set<HypInfo, HypInfoHasher, HypInfoCompare> u;
  while(h->size() > 0) {
    u.insert(h->back());
    h->pop_back();
  }
  tr1::unordered_set<HypInfo, HypInfoHasher, HypInfoCompare>::iterator it = u.begin();
  while (it != u.end()) {
    h->push_back(*it);
    it = u.erase(it);
  }
  cerr << "  out=" << h->size() << endl;
}

struct ThresholdAlpha {
  explicit ThresholdAlpha(double t = 0.05) : threshold(t) {}
  double operator()(double mag) const {
    if (mag < threshold) return 0.0; else return 1.0;
  }
  const double threshold;
};

struct TrainingInstance {
  TrainingInstance(const SparseVector<weight_t>& feats, bool positive, float diff) : x(feats), y(positive), gdiff(diff) {}
  SparseVector<weight_t> x;
#undef DEBUGGING_PRO
#ifdef DEBUGGING_PRO
  vector<WordID> a;
  vector<WordID> b;
#endif
  bool y;
  float gdiff;
};
#ifdef DEBUGGING_PRO
ostream& operator<<(ostream& os, const TrainingInstance& d) {
  return os << d.gdiff << " y=" << d.y << "\tA:" << TD::GetString(d.a) << "\n\tB: " << TD::GetString(d.b) << "\n\tX: " << d.x;
}
#endif

struct DiffOrder {
  bool operator()(const TrainingInstance& a, const TrainingInstance& b) const {
    return a.gdiff > b.gdiff;
  }
};

void Sample(const unsigned gamma,
            const unsigned xi,
            const vector<HypInfo>& J_i,
            const SegmentEvaluator& scorer,
            const EvaluationMetric* metric,
            vector<TrainingInstance>* pv) {
  assert(J_i.size() > 0);

  const bool invert_score = metric->IsErrorMetric();
  vector<TrainingInstance> v1, v2;
  float avg_diff = 0;
  for (unsigned i = 0; i < gamma; ++i) {
    const size_t a = rng->inclusive(0, J_i.size() - 1)();
    const size_t b = rng->inclusive(0, J_i.size() - 1)();
    if (a == b) continue;
    float ga = J_i[a].g(scorer, metric);
    float gb = J_i[b].g(scorer, metric);
    bool positive = gb < ga;
    if (invert_score) positive = !positive;
    const float gdiff = fabs(ga - gb);
    if (!gdiff) continue;
    avg_diff += gdiff;
    SparseVector<weight_t> xdiff = (J_i[a].x - J_i[b].x).erase_zeros();

    if (xdiff.empty()) {
      cerr << "Empty diff:\n  " << TD::GetString(J_i[a].hyp) << endl << "x=" << J_i[a].x << endl;
      cerr << "  " << TD::GetString(J_i[b].hyp) << endl << "x=" << J_i[b].x << endl;
      continue;
    }
    v1.push_back(TrainingInstance(xdiff, positive, gdiff));
#ifdef DEBUGGING_PRO
    v1.back().a = J_i[a].hyp;
    v1.back().b = J_i[b].hyp;
    cerr << "N: " << v1.back() << endl;
#endif
  }
  avg_diff /= v1.size();

  bool uniform_sampling = true;
  if(uniform_sampling) {
    // TODO: Do we even need to shuffle here?
    std::random_shuffle(v1.begin(), v1.end());

    vector<TrainingInstance>::iterator mid = v1.begin() + xi;
    if (xi > v1.size()) {
      mid = v1.end();
    }
    copy(v1.begin(), mid, back_inserter(*pv));

  } else {
    for (unsigned i = 0; i < v1.size(); ++i) {
      // TODO: Make this an option
      double p = 1.0 / (1.0 + exp(-avg_diff - v1[i].gdiff));
      // cerr << "avg_diff=" << avg_diff << "  gdiff=" << v1[i].gdiff << "  p=" << p << endl;
      if (rng->next() < p) v2.push_back(v1[i]);
    }
    vector<TrainingInstance>::iterator mid = v2.begin() + xi;
    if (xi > v2.size()) mid = v2.end();
    partial_sort(v2.begin(), mid, v2.end(), DiffOrder());
    copy(v2.begin(), mid, back_inserter(*pv));
#ifdef DEBUGGING_PRO
    if (v2.size() >= 5) {
      for (int i =0; i < (mid - v2.begin()); ++i) {
        cerr << v2[i] << endl;
      }
      cerr << pv->back() << endl;
    }
#endif
  }
}

void ReadFeatureNames(const std::string& filename, std::unordered_map<int, std::string>* feat_names, std::vector<int>* fid_to_kbest_ids) {
  assert(feat_names != nullptr);
  assert(fid_to_kbest_ids != nullptr);

  ifstream checker(filename);
  if (checker.good()) {
    std::cerr << "Reading feature name mapping from " << filename << std::endl;

    ReadFile in_read(filename);
    istream& in = *in_read.stream();
    string line;
    
    
    // add the invalid feature ID zero
    feat_names->clear();
    while (getline(in, line)) {
      std::vector<std::string> toks;
      Tokenize(line, ' ', &toks);
      assert(toks.size() == 2);
      const std::string& str_key = toks.at(0);
      const int key = atoi(str_key.c_str());
      const std::string& feat_name = toks.at(1);
      feat_names->emplace(key, feat_name);
    }
    
    fid_to_kbest_ids->resize(FD::NumFeats(), -1);
    for (auto pair : *feat_names) {
      const int kbest_id = pair.first;
      const std::string feat_name = pair.second;
      int fid = FD::Convert(feat_name);
      if (fid >= fid_to_kbest_ids->size())
        fid_to_kbest_ids->resize(fid+1, -1);
      (*fid_to_kbest_ids)[fid] = kbest_id;
    }

    for (int i = 1; i < fid_to_kbest_ids->size(); i++) {
      assert(fid_to_kbest_ids->at(i) != -1);
    }
    
  } else {
    cerr << "File does not exist: skipping reading of feature names: " << filename << endl;
  }
}

int main(int argc, char** argv) {
  po::variables_map conf;
  InitCommandLine(argc, argv, &conf);
  if (conf.count("random_seed"))
    rng.reset(new MT19937(conf["random_seed"].as<uint32_t>()));
  else
    rng.reset(new MT19937);
  const string evaluation_metric = conf["evaluation_metric"].as<string>();

  EvaluationMetric* metric = EvaluationMetric::Instance(evaluation_metric);
  DocumentScorer ds(metric, conf["reference"].as<vector<string> >());
  cerr << "Loaded " << ds.size() << " references for scoring with " << evaluation_metric << endl;

  Hypergraph hg;
  string last_file;
  ReadFile in_read(conf["input"].as<string>());
  istream &in=*in_read.stream();
  const unsigned kbest_size = conf["kbest_size"].as<unsigned>();
  const unsigned gamma = conf["candidate_pairs"].as<unsigned>();
  const unsigned xi = conf["best_pairs"].as<unsigned>();

  const bool prune_kbest_by_length_hammer = (conf["prune_kbest_by_length_hammer"].as<int>() != 0);

  string kbest_repo = conf["kbest_repository"].as<string>();

  ostringstream os_mapping;
  os_mapping << kbest_repo << "/kbest.feats.gz";
  const string kbest_mapping_file = os_mapping.str();
  std::unordered_map<int, string> old_feat_names;
  std::vector<int> fid_to_kbest_ids;
  ReadFeatureNames(kbest_mapping_file, &old_feat_names, &fid_to_kbest_ids);

  string weightsf = conf["weights"].as<string>();
  vector<weight_t> weights;

  Weights::InitFromFile(weightsf, &weights);
  // this has been moved out into dist_pro.pl
  // This can cause an abort on Lustre due to race conditions
  //MkDirP(kbest_repo);
  size_t num_sampled = 0;
  while(in) {
    string line;
    getline(in, line);
    if (line.empty()) continue;
    istringstream is(line);
    int sent_id;
    string file;
    // path-to-file (JSON) sent_id
    is >> file >> sent_id;
    vector<HypInfo> J_i;
    ReadFile rf(file);

    ostringstream os;
    os << kbest_repo << "/kbest." << sent_id << ".txt.gz";
    const string kbest_file = os.str();

    if (FileExists(kbest_file))
      ReadKBest(kbest_file, old_feat_names, &J_i);

    HypergraphIO::ReadFromJSON(rf.stream(), &hg);
    hg.Reweight(weights);
    KBest::KBestDerivations<vector<WordID>, ESentenceTraversal> kbest(hg, kbest_size);

    int length_hammer_id = FD::Convert("LengthHammer");
    for (int i = 0; i < kbest_size; ++i) {
      const KBest::KBestDerivations<vector<WordID>, ESentenceTraversal>::Derivation* d =
        kbest.LazyKthBest(hg.nodes_.size() - 1, i);
      if (!d) break;
      // check if length hammer constraint is violated
      if (prune_kbest_by_length_hammer && d->feature_values.nonzero(length_hammer_id)) {
	// just keep trying to fill up the k-best list without this nasty entry
	// eventually, we'll find enough entries that fit our length restriction
	// or we'll run out of hypothesis space and break out of this loop above
	continue;
      }
      J_i.push_back(HypInfo(d->yield, d->feature_values));
    }
    Dedup(&J_i);
    WriteKBest(kbest_file, J_i, fid_to_kbest_ids);

    // the kbest hammer might have thrown out all usable hypotheses
    if (J_i.size() > 0) {
      vector<TrainingInstance> v;
      Sample(gamma, xi, J_i, *ds[sent_id], metric, &v);
      for (unsigned i = 0; i < v.size(); ++i) {
        const TrainingInstance& vi = v[i];
        // TODO: Append sent_id and hyp ranks (these may no longer be meaningful)... or just metric scores.
        cout << vi.y << "\t" << vi.x << endl;
        cout << (!vi.y) << "\t" << (vi.x * -1.0) << endl;
      }
      num_sampled += v.size();
    }
  }
  
  if (num_sampled < 1) {
    cerr << "ERROR: Zero training examplars were sampled" << endl;
    abort();
  }
  return 0;
}
