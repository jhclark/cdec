#include "dtree.h"

void ParseOpts(int argc, char** argv, po::variables_map* conf) {
  po::options_description opts("Configuration options");
  opts.add_options()
    ("loss_function,l",po::value<string>(), "Loss function being optimized")
    ("src_sents,s",po::value<string>(), "Source sentences (that we will be asking questions about)")
    ("err_surface,e",po::value<string>(), "Directory containing error surfaces for each sentence")
    ("help,h", "Help");

  po::options_description dcmdline_options;
  dcmdline_options.add(opts);
  po::store(parse_command_line(argc, argv, dcmdline_options), *conf);
  bool flag = conf->count("loss_function") == 0;
  if (flag || conf->count("help")) {
    cerr << dcmdline_options << endl;
    exit(1);
  }
}


void LoadSents(const string& file,
	       vector<DTSent>* sents) {

  ReadFile rf(file);
  istream& in = *rf.stream();
  while(in) {
    string line;
    getline(in, line);
    if (line.empty() && !in) break;
    
    sents->resize(sents->size()+1);
    DTSent& sent = sents->back();
    
    ProcessAndStripSGML(&line, &sent.sgml);
    TD::ConvertSentence(line, &sent.src);
  }
}

void LoadErrSurfaces(const string& file,
		     const ScoreType& type,
		     SparseVector<double>* origin,
		     vector<SparseVector<double> >* dirs,
		     vector<vector<ErrorSurface> >* surfaces_by_dir_by_sent) {

  string last_key;
  vector<ErrorSurface> esv;

  ReadFile rf(file);
  istream& in = *rf.stream();
  while(in) {
    string line;
    getline(in, line);
    if (line.empty()) continue;

    size_t ks = line.find("\t");
    assert(string::npos != ks);
    assert(ks > 2);
    string key = line.substr(2, ks - 2);
    string val = line.substr(ks + 1);
    if (key != last_key) {
      // TODO: Parse key
      if (!last_key.empty()) {
	// TODO: Push esv onto results...
      }
      last_key = key;
      esv.clear();
    }

    // value has "sent_id b64_str"
    size_t valsep = val.find(" ");
    assert(string::npos != valsep);
    assert(valsep >= 0);
    string sent_id = val.substr(0, valsep); //unused
    string b64val = val.substr(valsep+1);

    if (b64val.size() % 4 != 0) {
      cerr << "B64 encoding error 1! Skipping.\n";
      abort();
    }
    string encoded(b64val.size() / 4 * 3, '\0');
    if (!B64::b64decode(reinterpret_cast<const unsigned char*>(&b64val[0]), b64val.size(), &encoded[0], encoded.size())) {
      cerr << "B64 encoding error 2! Skipping.\n";
      abort();
    }
    esv.push_back(ErrorSurface());
    esv.back().Deserialize(type, encoded);
  }

  if (!esv.empty()) {
    // TODO: PUSH esv ONTO RESULTS
  }
}

// TODO: This needs to more closely mirror the mr_vest_reducer
// Could we just write a special DT line optimizer instead
// of having a LinearLineOptimizer?
int main(int argc, char** argv) {
  po::variables_map conf;
  ParseOpts(argc, argv, &conf);

  const string loss_function = conf["loss_function"].as<string>();
  ScoreType type = ScoreTypeFromString(loss_function);
  LineOptimizer::ScoreType opt_type = LineOptimizer::GetOptType(type);

  vector<DTSent> src_sents;
  string inFile = conf["src_sents"].as<string>();
  string errFile = conf["err_surface"].as<string>();
  LoadSents(inFile, &src_sents);

  SparseVector<double> origin;
  vector<vector<ErrorSurface> > surfaces_by_dir_by_sent;
  vector<SparseVector<double> > dirs;
  LoadErrSurfaces(errFile, type, &origin, &dirs, &surfaces_by_dir_by_sent);

  const float DEFAULT_LINE_EPSILON = 1.0/65536.0;
  float dt_epsilon = 1.0/65536.0;
  int min_sents_per_node = 100;

  // TODO: verbosity?
  DTreeOptimizer opt(opt_type, DEFAULT_LINE_EPSILON, dt_epsilon, min_sents_per_node);

  // TODO: Load existing decision tree
  DTNode dtree(NULL);

  vector<bool> active_sents;
  for(size_t i=0; i<src_sents.size(); ++i) {
    active_sents[i] = true;
  }

  float best_score = opt.GrowTree(origin, dirs, src_sents, surfaces_by_dir_by_sent, active_sents, dtree);

  // TODO: Save decision tree for decoder use
  // Serialize(outFile, dtree);
}
