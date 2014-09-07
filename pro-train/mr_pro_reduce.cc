#include <cstdlib>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>

#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/algorithm/string.hpp>

#include "fast_oscar.h"

#include "stringlib.h"
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

struct LineFeatureGroup {
  double C; // regularization strength for this group
  int window_size; // number of points on either side to average
  vector<int> feat_ids; // sequence of feature ids that make up this line to be smoothed
};

void InitCommandLine(int argc, char** argv, po::variables_map* conf) {
  po::options_description opts("Configuration options");
  opts.add_options()
        ("optimizer_name",po::value<string>()->default_value("lbfgs"), "Which optimizer? 'lbfgs' or 'adagrad'")
        ("init_learning_rate",po::value<double>()->default_value(0.01), "AdaGrad initial learning rate. (Ignored by LBFGS). We use a value much lower than 1.0 since we're already in the neighborhood.")
        ("nonadapted_learning_rate",po::value<double>()->default_value(0.0), "AdaGrad constant learning rate (additive with adapted learning rate), not adapted using previous gradients (Ignored by LBFGS)")
        ("gradient_buffer_size",po::value<int>()->default_value(-1), "AdaGrad gradient buffer size -- how many previous iterations' gradients should be used to adapt the learning rate -1 means unbounded (standard AdaGrad). (Ignored by LBFGS)")
        ("num_iterations,N",po::value<int>()->default_value(100), "Number of AdaGrad iterations (Ignored by LBFGS)")
        ("weights,w", po::value<string>(), "Weights from previous iteration (used as initialization and interpolation")
        ("regularization_strength,C",po::value<double>()->default_value(500.0), "L2 regularization strength")
        ("L1_regularization_strength",po::value<double>()->default_value(0.0), "L1 regularization strength")
        ("Linf_regularization_strength",po::value<double>()->default_value(0.0), "Pairwise L_infinity regularization strength")
        ("conjunction_regularization_strength,c",po::value<double>()->default_value(0.0), "l2 regularization strength applied only to conjoined features (any feature that contains a double underscore)")
        ("normalize_regularizer,n", po::bool_switch()->default_value(false), "Normalize regularization constant C by the number of features")
        ("graph_regularization_strength,G",po::value<double>()->default_value(0.0), "l2 regularization strength for graph regularizer")
        ("graph_regularization_file,g",po::value<string>(), "file to read graph regularization precision matrix from (format: 'feat1_name feat2_name weight' -- weighted by G, not C; this line means 'feat1 is penalized for being dissimilar from feat2 proportional to weight')")
        ("tangent_regularization_file,L",po::value<string>(), "file to read tangent regularization configuration from (format: 'reg_strength window_size feat1_name feat2_name [feat3_name...]' -- weights are per line, not cumulative with C; this line means 'feat1...featN participate in a preferably smooth line (the line implied by the features' weights). window_size features on either side of each segment of this line will be used for approximating a tangent line and each line segment will receive a penalty with strength reg_strength for being dissimilar to that slope)")
        ("oscar_feats_file,O",po::value<string>(), "file to read OSCAR features from. If specified, only feature names listed in this file will have the OSCAR L1 and L_inf regularization terms applied to them.")
        ("regularize_by_group,x",po::bool_switch()->default_value(false), "For feature groups (lines) defined in the tangent_regularization_file, apply the regularizer to the average weight over all bins.")
        ("regularization_file,F",po::value<string>(), "a file containing per-feature regularization weights (additive with normal L2 regularizer, but not weighted by C)")
        ("regularize_to_weights,y",po::value<double>()->default_value(5000.0), "Differences in learned weights to previous weights are penalized with an l2 penalty with this strength; 0.0 = no effect")
        ("dominant_feat",po::value<string>(), "the name of a single feature, which *must* be present somewhere in at least one of the sampled exemplars, whose weight should guarantee that it always dominates all other features (i.e. that it is the primary sort criterion for hypotheses)")
        ("memory_buffers,m",po::value<unsigned>()->default_value(100), "Number of memory buffers (LBFGS)")
        ("min_reg,r",po::value<double>()->default_value(0.01), "When tuning (-T) regularization strength, minimum regularization strenght")
        ("max_reg,R",po::value<double>()->default_value(1e6), "When tuning (-T) regularization strength, maximum regularization strenght")
        ("testset,t",po::value<string>(), "Optional held-out test set")
        ("tune_regularizer,T", "Use the held out test set (-t) to tune the regularization strength")
        ("interpolate_with_weights,p",po::value<double>()->default_value(1.0), "[deprecated] Output weights are p*w + (1-p)*w_prev; 1.0 = no effect")
        ("verbose",po::bool_switch()->default_value(false), "Spew very verbose information during AdaGrad optimization")
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
                                const vector<SparseVector<weight_t> >& graph_reg_matrix,
                                const vector<LineFeatureGroup>& lines,
                                const bool regularize_by_group,
                                vector<weight_t>* g) {
  assert(weights.size() == g->size());
  double reg = 0;
  for (size_t i = 0; i < weights.size(); ++i) {
    const double prev_w_i = (i < prev_weights.size() ? prev_weights[i] : 0.0);
    const double& w_i = weights.at(i);
    // even if regularize_by_group is true, we will first add this term
    // and then subtract it off later when we actually iterate over the lines
    double& g_i = (*g)[i];
    reg += (C + feat_reg.at(i)) * w_i * w_i;
    g_i += 2 * (C + feat_reg.at(i)) * w_i;

    // TODO: REVIEW THIS
    // Note: We don't enforce that columns sum to 1, even though it's a good idea
    // Note: We don't need an |abs| in the gradient since the regularization graph is directed

    // apply graph regularization (e.g. linear neighbor regularization)
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

  // apply tangent regularization
  for (size_t i = 0; i < lines.size(); i++) {
    const LineFeatureGroup group = lines.at(i);
    assert(group.window_size == 1);

    //cerr << "Analyzing feature group " << i << endl;
    // don't apply any regularization to the feature group's endpoints
    for (size_t j = 1; j < group.feat_ids.size() - 1; j++) {
      int my_fid = group.feat_ids.at(j);
      double my_weight = weights.at(my_fid);
      double& g_j = g->at(my_fid);

      if (regularize_by_group) {
        // first, subtract off the original regularization term for this feature
        // and add back 1/N of the regularizer, based on how many points we have in our line
        double C_delta = -C + C / group.feat_ids.size();
        assert(C_delta < 0.0);
        reg += C_delta  * my_weight * my_weight;
        g_j += 2 * C_delta * my_weight;
      }

      // bathtub steepness parameter
      int gamma = 2;

      // determine where we would ideally like to see j
      // by averaging the 2 weights neighboring j
      int before_fid = group.feat_ids.at(j-1);
      int after_fid = group.feat_ids.at(j+1);
      double before_weight = weights.at(before_fid);
      double after_weight = weights.at(after_fid);
      double expected_weight = 0.5 * (before_weight + after_weight);
      bool is_monotone = (before_weight <= my_weight && my_weight <= after_weight) || (before_weight >= my_weight && my_weight >= after_weight);
      //cerr << "Before " << before_weight << "; my " << my_weight << "; after " << after_weight << " " << (is_monotone ? "GOOD" : "BAD") << endl;

      // scale the deviation penalty by how far apart the points on either side are
      // this means we don't penalize too much when a function is quickly changing
      // but penalize very harshly when we have evidence that we're in a region where the function isn't changing much elsewhere
      double half_span = 0.5 * (before_weight - after_weight);

      double deviation = expected_weight - my_weight;
      //cerr << "Desired delta is " << desired_delta << "; actual delta is " << actual_delta << "; badness is " << badness <<  endl;

      if (deviation != 0.0) {

        //cerr << "Gradient before " << g_j << endl;
        double scaled_deviation = deviation / half_span;
        double MAX_SCALED_DEVIATION = 10.0;
        double bathtub_damping;
        // if we're seeing a flat line, don't dampen regularizer at all
        // if the scaled_deviation is sufficiently large, bathtub_damping quickly approaches 1 -- and we may see inf if we try to compute it numerically
        if (before_weight == after_weight || abs(scaled_deviation) >= MAX_SCALED_DEVIATION) {
          bathtub_damping = 1.0;
          g_j -= 2 * group.C * deviation;
        } else {
          bathtub_damping = pow(tanh(scaled_deviation), 2 * gamma);

          /*
          double sech_scaled_deviation = 1.0 / cosh(scaled_deviation);
          double sinh_2scaled_deviation = sinh(2 * scaled_deviation);
          double scaling = -1.0 / half_span;
          double term1 = group.C * deviation * sech_scaled_deviation * sech_scaled_deviation * half_span * sinh_2scaled_deviation;
          double term2 = 2 * gamma * deviation * pow( tanh(scaled_deviation), 2 * gamma - 1);
          double gradient_penalty = scaling * (term1 + term2);
          cerr << "scaled_deviation is " << scaled_deviation
            //<< "; sech_scaled_deviation is " << sech_scaled_deviation
            // << "; sinh_2scaled_deviation " << sinh_2scaled_deviation
            // << "; half_span is " << half_span
               << "; scaling is " << scaling
               << "; term1 is " << term1
               << "; term2 is " << term2
               << "; gradient_penalty is " << gradient_penalty
               << endl;
          */

          double sech_scaled_deviation = 1.0 / cosh(scaled_deviation);
          double tanh_scaled_deviation = tanh(scaled_deviation);
          double term1 = 2.0 * gamma / half_span * deviation * deviation * sech_scaled_deviation * sech_scaled_deviation * pow(tanh_scaled_deviation, 2*gamma-1);
          double term2 = 2.0 * deviation * pow(tanh_scaled_deviation, 2.0 * gamma);
          double gradient_penalty = group.C * (term1 + term2);
          //cerr << "scaled_deviation is " << scaled_deviation
            //<< "; sech_scaled_deviation is " << sech_scaled_deviation
            //<< "; tanh_scaled_deviation is " << tanh_scaled_deviation
            // << "; half_span is " << half_span
          //     << "; term1 is " << term1
          //     << "; term2 is " << term2
          //     << "; gradient_penalty is " << gradient_penalty
          //     << endl;

          g_j -= gradient_penalty;
        }
        //cerr << "Gradient after " << g_j << endl;
        //cerr << "Deviation is " << deviation << "; bathtub_damping is " << bathtub_damping << "; overall penalty is " << (bathtub_damping * deviation * deviation) << endl;
        //cerr << "-------" << endl;
        reg += group.C * bathtub_damping * deviation * deviation;
      }
    }
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
template <typename Optimizer>
double LearnParameters(const vector<pair<bool, SparseVector<weight_t> > >& training,
                       const vector<pair<bool, SparseVector<weight_t> > >& testing,
                       const double C,
                       const double T,
                       const vector<weight_t>& prev_x,
		       const vector<weight_t>& feat_reg,
                       const double graph_reg_C,
                       const vector<SparseVector<weight_t> >& graph_reg_matrix,
                       const vector<LineFeatureGroup>& lines,
                       const bool regularize_by_group,
                       int dominant_feat_id,
                       Optimizer& opt,
                       vector<weight_t>* px) {

  vector<weight_t>& x = *px;
  vector<weight_t> vg(FD::NumFeats(), 0.0);
  vector<weight_t> vg_noreg(FD::NumFeats(), 0.0);
  bool converged = false;

  int iteration = 1;
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
    vg_noreg = vg;
    double reg = ApplyRegularizationTerms(C, T, x, prev_x, feat_reg, graph_reg_C, graph_reg_matrix, lines, regularize_by_group, &vg);
    cll += reg;
    cerr << iteration << ": " << cll << " (REG=" << reg << ")\tPPL=" << ppl << "\t TEST_PPL=" << tppl << "\t" << endl;
    try {
      // remove fixed parameters from gradient
      bool fixed_passthrough = false;
      if (fixed_passthrough) {
        cerr << "Fixing passthrough penalty" << endl;
        const int passthrough_fid = FD::Convert("PassThrough");
        vg[passthrough_fid] = 0.0;
      }
      if (dominant_feat_id != -1) {
        vg[dominant_feat_id] = 0.0;
      }

      opt.Optimize(cll, vg_noreg, vg, &x);
      converged = opt.HasConverged();

      // Check if any feats are too close to dominant feat
      // If so, rescale all features weights to be less than 100X
      // NOTE: This is not guaranteed to make the feature dominant (but should be reasonable)
      if (dominant_feat_id != -1) {
        double second_biggest_weight = 0.0; // by magnitude
        int second_biggest_id = 0;
        for (size_t i = 1; i < x.size(); ++i) {
          if (i != dominant_feat_id) {
            double w = fabs(x.at(i));
            if (w > second_biggest_weight) {
              second_biggest_weight = w;
              second_biggest_id = i;
            }
          }
        }
        const double dominant_weight = x.at(dominant_feat_id);
        cerr << "Dominant feature weight after optimization: " << dominant_weight << endl;
        cerr << "Desired next highest weight after optimization: " << second_biggest_weight << endl;

        // if second biggest weight, isn't actually the second biggest weight, fix it.
        const double MULTIPLIER = 100.0; // make dominant weight 100X any other weight
        if (fabs(second_biggest_weight * MULTIPLIER) > dominant_weight) {

          double scaling_factor = 1.0 / MULTIPLIER;
          if (fabs(dominant_weight) < fabs(second_biggest_weight)) {
            // divide something smaller by something larger to multiply in something < 1.0
            scaling_factor *= fabs(dominant_weight) / fabs(second_biggest_weight);
          }
          cerr << "Rescaling all non-dominant features by: " << scaling_factor << endl;
          assert(scaling_factor < 1.0);

          for (size_t i = 1; i < x.size(); ++i) {
            if (i != dominant_feat_id) {
              cerr << "Feature " << i << ": " << FD::Convert(i) << ": " << x.at(i) << " => ";
              x[i] *= scaling_factor;
              cerr << x.at(i) << endl;
              const double EPSILON = 0.000001;
              if ( fabs(x.at(i) * MULTIPLIER) > fabs(dominant_weight) + EPSILON ) {
                cerr << fabs(x.at(i) * MULTIPLIER) << " > " << (fabs(dominant_weight) + EPSILON) << endl;
                assert( fabs(x.at(i) * MULTIPLIER) <= fabs(dominant_weight) + EPSILON);
              }
            }
          }
        }
      }

      // debug write weights
      //Weights::WriteToFile("-", x);
      
    } catch (...) {
      cerr << "Exception caught, assuming convergence is close enough...\n";
      converged = true;
    }
    if (fabs(x[0]) > MAX_BIAS) {
      cerr << "Biased model learned. Are your training instances wrong?\n";
      cerr << "  BIAS: " << x[0] << endl;
    }
    iteration++;
  }
  return tppl;
}

// reads a "feature matrix" file with lines like:
// Feat1 Feat2 weight
// this is used for reading the precision matrix in graph regularization
// note: this code is ripped off from Weights::InitFromFile, but contains some non-trivial changes
void ReadFeatMatrix(const string& filename,
                    vector<SparseVector<weight_t> >* pweights) {

  vector<SparseVector<weight_t> >& weights = *pweights;
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
    if (std::isnan(val)) {
      cerr << FD::Convert(fid1) << " has weight NaN!\n";
      abort();
    }

    if (weights.size() <= fid1) {
      weights.resize(fid1 + 1);
    }
    /* SparseVector
    if (weights[fid1].size() <= fid2) {
      weights[fid1].resize(fid2 + 1);
    }
    */
    max_feat = max<size_t>(max_feat, fid1);
    max_feat = max<size_t>(max_feat, fid2);

    SparseVector<weight_t>& inner_vec = weights[fid1];
    inner_vec.set_value(fid2, val);
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

// reads a file that lists features that should be included in OSCAR regularization
void ReadOscarFeaturesFile(const string& filename,
                           vector<bool>* oscar_feats) {

  if (!SILENT) cerr << "Reading OSCAR features from " << filename << endl;
  ReadFile in_file(filename);
  istream& in = *in_file.stream();
  assert(in);

  // start by applying OSCAR to no features
  // then set to true as we observe them in the file
  for (size_t i = 0; i < oscar_feats->size(); ++i) {
    (*oscar_feats)[i] = false;
  }

  string buf;
  while (in) {

    getline(in, buf);
    if (buf.size() == 0) continue;
    if (buf[0] == '#') continue;
    if (buf[0] == ' ') {
      cerr << "OSCAR feats file lines may not start with whitespace.\n" << buf << endl;
      abort();
    }

    boost::trim(buf);
    const int fid = FD::Convert(buf);
    if (oscar_feats->size() <= fid) {
      oscar_feats->resize(fid + 1);
    }
    (*oscar_feats)[fid] = true;
  }
}

void ReadTangentRegularizationFile(string filename, vector<LineFeatureGroup>* lines) {

  if (!SILENT) cerr << "Reading tangent regularization configuration from " << filename << endl;
  ReadFile in_file(filename);
  istream& in = *in_file.stream();
  assert(in);
  
  bool fl = false;
  string buf;
  while (in) {

    getline(in, buf);
    if (buf.size() == 0) continue;
    if (buf[0] == '#') continue;
    if (buf[0] == ' ') {
      cerr << "Tangent regularization file lines may not start with whitespace.\n" << buf << endl;
      abort();
    }

    // = becomes space
    for (int i = buf.size() - 1; i > 0; --i)
      if (buf[i] == '=' || buf[i] == '\t') { buf[i] = ' '; break; }

    vector<string> toks;
    Tokenize(buf, ' ', &toks);
    if (!toks.size() >= 4) {
      cerr << "ERROR: Expected tangent regularizer configuration line to contain 'reg_strength window_size feat_id1 feat_id2 [feat_ids...]' but instead found: " << buf << endl;
      abort();
    }

    LineFeatureGroup group;

    // read the regularizer strength C
    group.C = strtod(toks.at(0).c_str(), NULL);
    cerr << "Parsed C " << group.C << endl;

    // read the window size
    group.window_size = atoi(toks.at(1).c_str());
    cerr << "Parsed window size " << group.window_size << endl;

    // read each feature that is part of this line
    for (size_t i = 2; i < toks.size(); i++) {
      int feat_id = FD::Convert(toks.at(i));
      group.feat_ids.push_back(feat_id);
    }

    if (!SILENT) {
          // TODO: Cute informational message about what was loaded so far
    }
    lines->push_back(group);
  } // end for each line in config file...

  if (!SILENT) {
    if (fl) { cerr << endl; }
    // TODO: Cute informational message about what was loaded
  }
}

void ResizeMatrix(const size_t size, vector<SparseVector<weight_t> >* pweights) {
  vector<SparseVector<weight_t> >& weights = *pweights;

  // make the entire matrix square
  if (weights.size() < size) {
    weights.resize(size);
  }
  /* SparseVector
  for (int i=0; i < weights.size(); ++i) {
    if (weights[i].size() < size) {
      weights[i].resize(size);
    }
  }
  */
  //cerr << "Feature matrix dimensions are " << weights.size() << " x " << weights[0].size() << " = " << (weights.size() * weights[0].size()) << endl;
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
  double C_1 = conf["L1_regularization_strength"].as<double>();
  double C_inf = conf["Linf_regularization_strength"].as<double>();
  const double T = conf["regularize_to_weights"].as<double>();
  assert(C >= 0.0);
  assert(min_reg >= 0.0);
  assert(max_reg >= 0.0);
  assert(max_reg >= min_reg);

  int dominant_feat_id;
  if (conf.count("dominant_feat")) {
    dominant_feat_id = FD::Convert(conf["dominant_feat"].as<string>());
  } else {
    dominant_feat_id = -1;
  }

  const double psi = conf["interpolate_with_weights"].as<double>();
  if (psi < 0.0 || psi > 1.0) { cerr << "Invalid interpolation weight: " << psi << endl; return 1; }
  ReadCorpus(&cin, &training);
  if (conf.count("testset")) {
    ReadFile rf(conf["testset"].as<string>());
    ReadCorpus(rf.stream(), &testing);
  }
  cerr << "Number of features in corpus: " << FD::NumFeats() << endl;

  {
    bool normalize_regularizer = conf["normalize_regularizer"].as<bool>();
    if (normalize_regularizer) {
      C /= FD::NumFeats();
      cerr << "Normalized regularizer is " << C << endl;
    } else {
      cerr << "Regularizer is not normalized: " << C << endl;
    }
  }

  // read additional per feature regularization weights
  // NOTE: Must do this *after* reading the corpus!
  vector<weight_t> feat_reg;
  if (conf.count("regularization_file")) {
    Weights::InitFromFile(conf["regularization_file"].as<string>(), &feat_reg);
  }

  bool regularize_by_group = conf.count("regularize_by_group") > 0;
  if (regularize_by_group)
    cerr << "Regularize by group: ON" << endl;
  else
    cerr << "Regularize by group: OFF" << endl;

  double graph_reg_C = conf["graph_regularization_strength"].as<double>(); // will be overridden if parameter is tuned

  // read additional feature pair regularization weights for graph regularization
  // pre-size the 2D vector at num_feats by num_feats
  // NOTE: Must do this *after* reading the corpus!
  vector<SparseVector<weight_t> > graph_reg_matrix(FD::NumFeats(), SparseVector<weight_t>());
  for (size_t i = 0; i < FD::NumFeats(); i++) {
    for (size_t j = 0; j < FD::NumFeats(); j++) {
      assert(graph_reg_matrix.at(i).at(j) == 0);
    }
  }
  if (conf.count("graph_regularization_file")) {
    ReadFeatMatrix(conf["graph_regularization_file"].as<string>(), &graph_reg_matrix);
    //cerr << "Read feature matrix:" << endl;
    //    for (int i=0; i < graph_reg_matrix.size(); ++i)
    //      for (int j=0; j < graph_reg_matrix[i].size(); ++j)
    //        cerr << "FeatMatrix: " << FD::Convert(i) << " " << FD::Convert(j) << " " << graph_reg_matrix[i][j] << endl;
  }

  // read in configuration for tangent regularization
  vector<LineFeatureGroup> lines;
  if (conf.count("tangent_regularization_file")) {
    ReadTangentRegularizationFile(conf["tangent_regularization_file"].as<string>(), &lines);
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
  feat_reg.resize(FD::NumFeats(), 0.0);
  ResizeMatrix(FD::NumFeats(), &graph_reg_matrix);

  // set regularization strength for conjoined features
  // now that we've enumerated all features in our corpus
  double C_conj = conf["conjunction_regularization_strength"].as<double>();
  for (int i = 0; i < FD::NumFeats(); i++) {
    string feat_name = FD::Convert(i);
    if (feat_name.find("__") != string::npos) {
      if (conf["verbose"].as<bool>())
	cerr << "Applying conjunction regularization to feature " << feat_name << " with strength " << C_conj << endl;
      feat_reg[i] += C_conj;
    }
  }

  string optimizer_name = conf["optimizer_name"].as<string>();
  cerr << "Using optimizer: " << optimizer_name << endl;
  if (optimizer_name == "lbfgs") {
    if (C_1 != 0.0) {
      cerr << "L1 regularizer not supported with LBFGS" << endl;
      abort();
    }
    if (C_inf != 0.0) {
      cerr << "pairwise L_infinity regularizer not supported with LBFGS" << endl;
      abort();
    }
  } else if (optimizer_name == "adagrad") {
  } else {
    cerr << "Unrecognized optimizer name" << endl;
    abort();
  }
  
  // we create both optimizers here out of laziness (this should be refactored!)
  // but we will use only one
  // TODO: Enable command line argument to switch between LBFGS and OSCAR
  unsigned memory_buffers = conf["memory_buffers"].as<unsigned>();
  LBFGSOptimizer lbfgs_opt(FD::NumFeats(), memory_buffers);

  // features that should be included in OSCAR
  // by default, apply OSCAR to all features, unless the user specifies a file that lists oscar features
  vector<bool> oscar_feats(FD::NumFeats(), true);
  string oscar_features_file = conf["oscar_feats_file"].as<string>();
  if (conf.count("oscar_feats_file")) {
    ReadOscarFeaturesFile(oscar_features_file, &oscar_feats);
  }

  double init_learning_rate = conf["init_learning_rate"].as<double>();
  double nonadapted_learning_rate = conf["nonadapted_learning_rate"].as<double>();
  int gradient_buffer_size = conf["gradient_buffer_size"].as<int>();
  int num_iterations = conf["num_iterations"].as<int>();
  AdaGradOscarOptimizer oscar_opt(C_1, C_inf, init_learning_rate, nonadapted_learning_rate, gradient_buffer_size, num_iterations, prev_x, oscar_feats, conf["verbose"].as<bool>());

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
      if (optimizer_name == "lbfgs") {
        tppl = LearnParameters(training, testing, C, T, prev_x,
                               feat_reg, graph_reg_C, graph_reg_matrix, lines, regularize_by_group, dominant_feat_id, lbfgs_opt, &x);
      } else {
        tppl = LearnParameters(training, testing, C, T, prev_x,
                               feat_reg, graph_reg_C, graph_reg_matrix, lines, regularize_by_group, dominant_feat_id, oscar_opt, &x);
        
      }
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

  if (optimizer_name == "lbfgs") {
    tppl = LearnParameters(training, testing, C, T, prev_x,
                           feat_reg, graph_reg_C, graph_reg_matrix, lines, regularize_by_group, dominant_feat_id, lbfgs_opt, &x);
  } else {
    tppl = LearnParameters(training, testing, C, T, prev_x,
                           feat_reg, graph_reg_C, graph_reg_matrix, lines, regularize_by_group, dominant_feat_id, oscar_opt, &x);
  }

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
