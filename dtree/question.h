#include <vector>
#include <map>
using namespace std;

struct DTSent {
  vector<WordID> src;
  map<string, string> sgml;
};

class Question {
public:
  virtual bool ask(const DTSent& sent) const =0;
};

class QuestionQuestion : public Question {
public:
  QuestionQuestion() {
    qmark_ = TD::Convert("?");
  }

  bool ask(const DTSent& sent) const {
    WordID lastTok = sent.src.back();
    return lastTok == qmark_;
  }
private:
  WordID qmark_;
};

class LengthQuestion : public Question {
public:
  LengthQuestion(const int len) : len_(len) {}

  bool ask(const DTSent& sent) const {
    return sent.src.size() >= len_;
  }
private:
  int len_;
};
