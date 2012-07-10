#include <cstdlib>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>

#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>

#include "filelib.h"
#include "weights.h"
#include "sparse_vector.h"
#include "optimize.h"
#include "verbose.h"

using namespace std;
namespace po = boost::program_options;

// since this is a ranking model, there should be equal numbers of
// positive and negative examples, so the bias should be 0
static const double MAX_BIAS = 1e-10;

void InitCommandLine(int argc, char** argv, po::variables_map* conf) {
  po::options_description opts("Configuration options");
  opts.add_options()
        ("weights,w", po::value<string>(), "Weights from previous iteration (used as initialization and interpolation")
        ("regularization_strength,C",po::value<double>()->default_value(500.0), "l2 regularization strength")
        ("graph_regularization_strength,G",po::value<double>()->default_value(500.0), "l2 regularization strength for graph regularizer")
        ("graph_regularization_file,g",po::value<string>(), "file to read graph regularization precision matrix from (format: 'feat1_name feat2_name weight' -- weighted by G, not C)")
        ("regularization_file,F",po::value<string>(), "a file containing per-feature regularization weights (additive with normal L2 regularizer, but not weighted by C)")
        ("regularize_to_weights,y",po::value<double>()->default_value(5000.0), "Differences in learned weights to previous weights are penalized with an l2 penalty with this strength; 0.0 = no effect")
        ("memory_buffers,m",po::value<unsigned>()->default_value(100), "Number of memory buffers (LBFGS)")
        ("min_reg,r",po::value<double>()->default_value(0.01), "When tuning (-T) regularization strength, minimum regularization strenght")
        ("max_reg,R",po::value<double>()->default_value(1e6), "When tuning (-T) regularization strength, maximum regularization strenght")
        ("testset,t",po::value<string>(), "Optional held-out test set")
        ("tune_regularizer,T", "Use the held out test set (-t) to tune the regularization strength")
        ("interpolate_with_weights,p",po::value<double>()->default_value(1.0), "[deprecated] Output weights are p*w + (1-p)*w_prev; 1.0 = no effect")
        ("help,h", "Help");
  po::options_description dcmdline_options;
  dcmdline_options.add(opts);
  po::store(parse_command_line(argc, argv, dcmdline_options), *conf);
  if (conf->count("help")) {
    cerr << dcmdline_options << endl;
    exit(1);
  }
}

void ParseSparseVector(string& line, size_t cur, SparseVector<weight_t>* out) {
  SparseVector<weight_t>& x = *out;
  size_t last_start = cur;
  size_t last_comma = string::npos;
  while(cur <= line.size()) {
    if (line[cur] == ' ' || cur == line.size()) {
      if (!(cur > last_start && last_comma != string::npos && cur > last_comma)) {
        cerr << "[ERROR] " << line << endl << "  position = " << cur << endl;
        exit(1);
      }
      const int fid = FD::Convert(line.substr(last_start, last_comma - last_start));
      if (cur < line.size()) line[cur] = 0;
      const weight_t val = strtod(&line[last_comma + 1], NULL);
      x.set_value(fid, val);

      last_comma = string::npos;
      last_start = cur+1;
    } else {
      if (line[cur] == '=')
        last_comma = cur;
    }
    ++cur;
  }
}

void ReadCorpus(istream* pin, vector<pair<bool, SparseVector<weight_t> > >* corpus) {
  istream& in = *pin;
  corpus->clear();
  bool flag = false;
  int lc = 0;
  string line;
  SparseVector<weight_t> x;
  while(getline(in, line)) {
    ++lc;
    if (lc % 1000 == 0) { cerr << '.'; flag = true; }
    if (lc % 40000 == 0) { cerr << " [" << lc << "]\n"; flag = false; }
    if (line.empty()) continue;
    const size_t ks = line.find("\t");
    assert(string::npos != ks);
    assert(ks == 1);
    const bool y = line[0] == '1';
    x.clear();
    ParseSparseVector(line, ks + 1, &x);
    corpus->push_back(make_pair(y, x));
  }
  if (flag) cerr << endl;
}

void GradAdd(const SparseVector<weight_t>& v, const double scale, vector<weight_t>* acc) {
  for (SparseVector<weight_t>::const_iterator it = v.begin();
       it != v.end(); ++it) {
    (*acc)[it->first] += it->second * scale;
  }
}

// C is the regularization constant
// feat_reg are feature-level regularization weights
double ApplyRegularizationTerms(const double C,
                                const double T,
                                const vector<weight_t>& weights,
                                const vector<weight_t>& prev_weights,
				const vector<weight_t>& feat_reg,
                                const double graph_reg_C,
                                const vector<vector<weight_t> >& graph_reg_matrix,
                                vector<weight_t>* g) {
  assert(weights.size() == g->size());
  double reg = 0;
  for (size_t i = 0; i < weights.size(); ++i) {
    const double prev_w_i = (i < prev_weights.size() ? prev_weights[i] : 0.0);
    const double& w_i = weights.at(i);
    double& g_i = (*g)[i];
    reg += (C + feat_reg.at(i)) * w_i * w_i;
    g_i += 2 * (C + feat_reg.at(i)) * w_i;

    // TODO: REVIEW THIS
    // Note: We don't enforce that columns sum to 1, even though it's a good idea
    // Note: We don't need an |abs| in the gradient since the regularization graph is directed

    // apply graph regularization (e.g. neighbor regularization)
    if (graph_reg_C != 0.0) {
      double diff_i_J = w_i;
      for (int j=0; j<weights.size(); j++) {
        diff_i_J -= graph_reg_matrix.at(i).at(j) * weights.at(j);
      }
      reg += graph_reg_C * diff_i_J * diff_i_J;
      g_i += 2 * graph_reg_C * diff_i_J;
    }

    // regularize to the previous iteration's weights
    const double diff_i = w_i - prev_w_i;
    reg += T * diff_i * diff_i;
    g_i += 2 * T * diff_i;
  }
  return reg;
}

double TrainingInference(const vector<weight_t>& x,
                         const vector<pair<bool, SparseVector<weight_t> > >& corpus,
                         vector<weight_t>* g = NULL) {
  double cll = 0;
  for (int i = 0; i < corpus.size(); ++i) {
    const double dotprod = corpus[i].second.dot(x) + (x.size() ? x[0] : weight_t()); // x[0] is bias
    double lp_false = dotprod;
    double lp_true = -dotprod;
    if (0 < lp_true) {
      lp_true += log1p(exp(-lp_true));
      lp_false = log1p(exp(lp_false));
    } else {
      lp_true = log1p(exp(lp_true));
      lp_false += log1p(exp(-lp_false));
    }
    lp_true*=-1;
    lp_false*=-1;
    if (corpus[i].first) {  // true label
      cll -= lp_true;
      if (g) {
        // g -= corpus[i].second * exp(lp_false);
        GradAdd(corpus[i].second, -exp(lp_false), g);
        (*g)[0] -= exp(lp_false); // bias
      }
    } else {                  // false label
      cll -= lp_false;
      if (g) {
        // g += corpus[i].second * exp(lp_true);
        GradAdd(corpus[i].second, exp(lp_true), g);
        (*g)[0] += exp(lp_true); // bias
      }
    }
  }
  return cll;
}

// return held-out log likelihood
double LearnParameters(const vector<pair<bool, SparseVector<weight_t> > >& training,
                       const vector<pair<bool, SparseVector<weight_t> > >& testing,
                       const double C,
                       const double T,
                       const unsigned memory_buffers,
                       const vector<weight_t>& prev_x,
		       const vector<weight_t>& feat_reg,
                       const double graph_reg_C,
                       const vector<vector<weight_t> >& graph_reg_matrix,
                       vector<weight_t>* px) {
  vector<weight_t>& x = *px;
  vector<weight_t> vg(FD::NumFeats(), 0.0);
  bool converged = false;
  LBFGSOptimizer opt(FD::NumFeats(), memory_buffers);
  double tppl = 0.0;
  while(!converged) {
    fill(vg.begin(), vg.end(), 0.0);
    double cll = TrainingInference(x, training, &vg);
    double ppl = cll / log(2);
    ppl /= training.size();
    ppl = pow(2.0, ppl);

    // evaluate optional held-out test set
    if (testing.size()) {
      tppl = TrainingInference(x, testing) / log(2);
      tppl /= testing.size();
      tppl = pow(2.0, tppl);
    }

    // handle regularizer
    double reg = ApplyRegularizationTerms(C, T, x, prev_x, feat_reg, graph_reg_C, graph_reg_matrix, &vg);
    cll += reg;
    cerr << cll << " (REG=" << reg << ")\tPPL=" << ppl << "\t TEST_PPL=" << tppl << "\t" << endl;
    try {
      // remove fixed parameters from gradient
      bool fixed_passthrough = false;
      if(fixed_passthrough) {
        cerr << "Fixing passthrough penalty" << endl;
        const int passthrough_fid = FD::Convert("PassThrough");
        vg[passthrough_fid] = 0.0;
      }

      opt.Optimize(cll, vg, &x);
      converged = opt.HasConverged();
    } catch (...) {
      cerr << "Exception caught, assuming convergence is close enough...\n";
      converged = true;
    }
    if (fabs(x[0]) > MAX_BIAS) {
      cerr << "Biased model learned. Are your training instances wrong?\n";
      cerr << "  BIAS: " << x[0] << endl;
    }
  }
  return tppl;
}

// reads a "feature matrix" file with lines like:
// Feat1 Feat2 weight
// this is used for reading the precision matrix in graph regularization
// note: this code is ripped off from Weights::InitFromFile, but contains some non-trivial changes
void ReadFeatMatrix(const string& filename,
                    vector<vector<weight_t> >* pweights) {

  vector<vector<weight_t> >& weights = *pweights;
  if (!SILENT) cerr << "Reading feature matrix from " << filename << endl;
  ReadFile in_file(filename);
  istream& in = *in_file.stream();
  assert(in);
  
  int weight_count = 0;
  bool fl = false;
  string buf;
  size_t max_feat = max<size_t>(weights.size(), FD::NumFeats());
  while (in) {

    getline(in, buf);
    if (buf.size() == 0) continue;
    if (buf[0] == '#') continue;
    if (buf[0] == ' ') {
      cerr << "Weight matrix file lines may not start with whitespace.\n" << buf << endl;
      abort();
    }
    // = becomes space
    for (int i = buf.size() - 1; i > 0; --i)
      if (buf[i] == '=' || buf[i] == '\t') { buf[i] = ' '; break; }

    // read the first feature
    int start1 = 0;
    while(start1 < buf.size() && buf[start1] == ' ') ++start1; // this appears to do nothing (see error condition above)
    int end1 = 0;
    while(end1 < buf.size() && buf[end1] != ' ') ++end1;
    const int fid1 = FD::Convert(buf.substr(start1, end1 - start1));

    // read the second feature
    int start2 = end1;
    while(start2 < buf.size() && buf[start2] == ' ') ++start2;
    int end2 = start2;
    while(end2 < buf.size() && buf[end2] != ' ') ++end2;
    const int fid2 = FD::Convert(buf.substr(start2, end2 - start2));

    // read the weight
    int weight_begin = end2;
    while(weight_begin < buf.size() && buf[weight_begin] == ' ') ++weight_begin;
    weight_t val = strtod(&buf.c_str()[weight_begin], NULL);
    if (isnan(val)) {
      cerr << FD::Convert(fid1) << " has weight NaN!\n";
      abort();
    }

    if (weights.size() <= fid1) {
      weights.resize(fid1 + 1);
    }
    if (weights[fid1].size() <= fid2) {
      weights[fid1].resize(fid2 + 1);
    }
    max_feat = max<size_t>(max_feat, fid1);
    max_feat = max<size_t>(max_feat, fid2);

    weights[fid1][fid2] = val;
    ++weight_count;
    if (!SILENT) {
      if (weight_count %   50000 == 0) { cerr << '.' << flush; fl = true; }
      if (weight_count % 2000000 == 0) { cerr << " [" << weight_count << "]\n"; fl = false; }
    }
  }

  if (!SILENT) {
    if (fl) { cerr << endl; }
    cerr << "Loaded " << weight_count << " feature pair weights\n";
  }
}

void ResizeMatrix(const size_t size, vector<vector<weight_t> >* pweights) {
  vector<vector<weight_t> >& weights = *pweights;

  // make the entire matrix square
  if (weights.size() < size) {
    weights.resize(size);
  }
  for (int i=0; i < weights.size(); ++i) {
    if (weights[i].size() < size) {
      weights[i].resize(size);
    }
  }
  cerr << "Feature matrix dimensions are" << weights.size() << " x " << weights[0].size() << endl;
}


int main(int argc, char** argv) {
  po::variables_map conf;
  InitCommandLine(argc, argv, &conf);
  string line;
  vector<pair<bool, SparseVector<weight_t> > > training, testing;
  const bool tune_regularizer = conf.count("tune_regularizer");
  if (tune_regularizer && !conf.count("testset")) {
    cerr << "--tune_regularizer requires --testset to be set\n";
    return 1;
  }

  const double min_reg = conf["min_reg"].as<double>();
  const double max_reg = conf["max_reg"].as<double>();
  double C = conf["regularization_strength"].as<double>(); // will be overridden if parameter is tuned
  const double T = conf["regularize_to_weights"].as<double>();
  assert(C > 0.0);
  assert(min_reg > 0.0);
  assert(max_reg > 0.0);
  assert(max_reg > min_reg);

  const double psi = conf["interpolate_with_weights"].as<double>();
  if (psi < 0.0 || psi > 1.0) { cerr << "Invalid interpolation weight: " << psi << endl; return 1; }
  ReadCorpus(&cin, &training);
  if (conf.count("testset")) {
    ReadFile rf(conf["testset"].as<string>());
    ReadCorpus(rf.stream(), &testing);
  }
  cerr << "Number of features in corpus: " << FD::NumFeats() << endl;

  // read additional per feature regularization weights
  // NOTE: Must do this *after* reading the corpus!
  vector<weight_t> feat_reg;
  if (conf.count("regularization_file")) {
    Weights::InitFromFile(conf["regularization_file"].as<string>(), &feat_reg);
  }

  double graph_reg_C = conf["graph_regularization_strength"].as<double>(); // will be overridden if parameter is tuned

  // read additional feature pair regularization weights for graph regularization
  // pre-size the 2D vector at num_feats by num_feats
  // NOTE: Must do this *after* reading the corpus!
  vector<vector<weight_t> > graph_reg_matrix(FD::NumFeats(), vector<weight_t>(FD::NumFeats()));
  if (conf.count("graph_regularization_file")) {
    ReadFeatMatrix(conf["graph_regularization_file"].as<string>(), &graph_reg_matrix);
    //cerr << "Read feature matrix:" << endl;
    //    for (int i=0; i < graph_reg_matrix.size(); ++i)
    //      for (int j=0; j < graph_reg_matrix[i].size(); ++j)
    //        cerr << "FeatMatrix: " << FD::Convert(i) << " " << FD::Convert(j) << " " << graph_reg_matrix[i][j] << endl;
  }

  vector<weight_t> x, prev_x;  // x[0] is bias
  if (conf.count("weights")) {
    Weights::InitFromFile(conf["weights"].as<string>(), &x);
    x.resize(FD::NumFeats());
    prev_x = x;
  } else {
    x.resize(FD::NumFeats());
    prev_x = x;
  }
  cerr << "         Number of features: " << x.size() << endl;
  cerr << "Number of training examples: " << training.size() << endl;
  cerr << "Number of  testing examples: " << testing.size() << endl;

  // make sure new regularizers have consistent dimensions
  feat_reg.resize(FD::NumFeats());
  ResizeMatrix(FD::NumFeats(), &graph_reg_matrix);

  double tppl = 0.0;
  vector<pair<double,double> > sp;
  vector<double> smoothed;
  if (tune_regularizer) {
    C = min_reg;
    const double steps = 18;
    double sweep_factor = exp((log(max_reg) - log(min_reg)) / steps);
    cerr << "SWEEP FACTOR: " << sweep_factor << endl;
    while(C < max_reg) {
      cerr << "C=" << C << "\tT=" <<T << endl;
      tppl = LearnParameters(training, testing, C, T, conf["memory_buffers"].as<unsigned>(), prev_x,
                             feat_reg, graph_reg_C, graph_reg_matrix, &x);
      sp.push_back(make_pair(C, tppl));
      C *= sweep_factor;
    }
    smoothed.resize(sp.size(), 0);
    smoothed[0] = sp[0].second;
    smoothed.back() = sp.back().second; 
    for (int i = 1; i < sp.size()-1; ++i) {
      double prev = sp[i-1].second;
      double next = sp[i+1].second;
      double cur = sp[i].second;
      smoothed[i] = (prev*0.2) + cur * 0.6 + (0.2*next);
    }
    double best_ppl = 9999999;
    unsigned best_i = 0;
    for (unsigned i = 0; i < sp.size(); ++i) {
      if (smoothed[i] < best_ppl) {
        best_ppl = smoothed[i];
        best_i = i;
      }
    }
    C = sp[best_i].first;
  }  // tune regularizer
  tppl = LearnParameters(training, testing, C, T, conf["memory_buffers"].as<unsigned>(), prev_x,
                         feat_reg, graph_reg_C, graph_reg_matrix, &x);
  if (conf.count("weights")) {
    for (int i = 1; i < x.size(); ++i) {
      x[i] = (x[i] * psi) + prev_x[i] * (1.0 - psi);
    }
  }
  cout.precision(15);
  cout << "# C=" << C << "\theld out perplexity=";
  if (tppl) { cout << tppl << endl; } else { cout << "N/A\n"; }
  if (sp.size()) {
    cout << "# Parameter sweep:\n";
    for (int i = 0; i < sp.size(); ++i) {
      cout << "# " << sp[i].first << "\t" << sp[i].second << "\t" << smoothed[i] << endl;
    }
  }
  Weights::WriteToFile("-", x);
  return 0;
}
