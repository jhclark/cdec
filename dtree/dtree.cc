#indlude "dtree.h"

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
  ScoreP acc;
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
		     vector<ErrorSurface>* surfaces_by_sent) {
}

// TODO: This needs to more closely mirror the mr_vest_reducer
// Could we just write a special DT line optimizer instead
// of having a LinearLineOptimizer?
int main(int argc, char** argv) {
  po::variables_map conf;
  ParseOpts(argc, argv, &conf);
  const string loss_function = conf["loss_function"].as<string>();

  vector<DTSent> src_sents;
  vector<ErrorSurface> surfaces_by_sent;
  string inFile = conf["src_sents"].as<string>();
  string errFile = conf["err_surface"].as<string>();
  LoadSents(inFile, &src_sents);
  LoadErrSurfaces(errFile, &surfaces_by_sent);

  ScoreType type = ScoreTypeFromString(loss_function);  
  LineOptimizer::ScoreType opt_type = LineOptimizer::GetOptType(type);

  const float DEFAULT_LINE_EPSILON = 1.0/65536.0;
  float dt_epsilon = 1.0/65536.0;
  int min_sents_per_node = 100;

  // TODO: verbosity?
  DTreeOptimizer opt(opt_type, DEFAULT_LINE_EPSILON, dt_epsilon, min_sents_per_node);
  DTNode dtree(NULL);
  Score best_score_stats;
  float best_score;
  opt.BuildTree(src_sents, surfaces_by_sent,
		&dtree, &best_score_stats, &best_score);

  // TODO: Save decision tree for decoder use
  // Serialize(outFile, dtree);
}
