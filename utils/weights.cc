#include "weights.h"

#include <sstream>

#include "fdict.h"
#include "filelib.h"
#include "verbose.h"
#include "stringlib.h"

using namespace std;

void Weights::InitFromFile(const std::string& filename, vector<string>* feature_list) {
  if (!SILENT) cerr << "Reading weights from " << filename << endl;
  ReadFile in_file(filename);
  istream& in = *in_file.stream();
  assert(in);
  int weight_count = 0;
  bool fl = false;
  string buf;
  double val = 0;
  while (in) {
    getline(in, buf);
    if (buf.size() == 0) continue;
    if (buf[0] == '#') continue;
    for (int i = 0; i < buf.size(); ++i)
      if (buf[i] == '=') buf[i] = ' ';
    int start = 0;
    while(start < buf.size() && buf[start] == ' ') ++start;
    int end = 0;
    while(end < buf.size() && buf[end] != ' ') ++end;
    const int fid = FD::Convert(buf.substr(start, end - start));
    while(end < buf.size() && buf[end] == ' ') ++end;
    val = strtod(&buf.c_str()[end], NULL);
    if (isnan(val)) {
      cerr << FD::Convert(fid) << " has weight NaN!\n";
     abort();
    }
    if (wv_.size() <= fid)
      wv_.resize(fid + 1);
    wv_[fid] = val;
    if (feature_list) { feature_list->push_back(FD::Convert(fid)); }
    ++weight_count;
    if (!SILENT) {
      if (weight_count %   50000 == 0) { cerr << '.' << flush; fl = true; }
      if (weight_count % 2000000 == 0) { cerr << " [" << weight_count << "]\n"; fl = false; }
    }
  }
  if (!SILENT) {
    if (fl) { cerr << endl; }
    cerr << "Loaded " << weight_count << " feature weights\n";
  }
}

void Weights::WriteToFile(const std::string& fname, bool hide_zero_value_features, const string* extra) const {
  WriteFile out(fname);
  ostream& o = *out.stream();
  assert(o);
  if (extra) { o << "# " << *extra << endl; }
  o.precision(17);
  const int num_feats = FD::NumFeats();
  for (int i = 1; i < num_feats; ++i) {
    const double val = (i < wv_.size() ? wv_[i] : 0.0);
    if (hide_zero_value_features && val == 0.0) continue;
    o << FD::Convert(i) << ' ' << val << endl;
  }
}

void Weights::InitVector(std::vector<double>* w) const {
  *w = wv_;
}

void Weights::InitSparseVector(SparseVector<double>* w) const {
  for (int i = 1; i < wv_.size(); ++i) {
    const double& weight = wv_[i];
    if (weight) w->set_value(i, weight);
  }
}

void Weights::InitFromVector(const std::vector<double>& w) {
  wv_ = w;
  if (wv_.size() > FD::NumFeats())
    cerr << "WARNING: initializing weight vector has more features than the global feature dictionary!\n";
  wv_.resize(FD::NumFeats(), 0);
}

void Weights::InitFromVector(const SparseVector<double>& w) {
  wv_.clear();
  wv_.resize(FD::NumFeats(), 0.0);
  for (int i = 1; i < FD::NumFeats(); ++i)
    wv_[i] = w.value(i);
}

bool Weights::ReadSparseVectorString(const string& s, SparseVector<double>* v) {
#if 0
  // this should work, but untested.
  std::istringstream i(s);
  i>>*v;
#else
  vector<string> fields;
  Tokenize(s, ';', &fields);
  if (fields.empty()) return false;
  for (int i = 0; i < fields.size(); ++i) {
    vector<string> pair(2);
    Tokenize(fields[i], '=', &pair);
    if (pair.size() != 2) {
      cerr << "Error parsing vector string: " << fields[i] << endl;
      return false;
    }
    v->set_value(FD::Convert(pair[0]), atof(pair[1].c_str()));
  }
  return true;
#endif
}
