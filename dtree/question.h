#include <vector>
#include <map>
#include <iostream>
#include <string>
#include <sstream>
using namespace std;

struct DTSent {
  vector<WordID> src;
  map<string, string> sgml;
};

class Question {
public:
  virtual bool Ask(const DTSent& sent) const =0;
  virtual string ToString() const =0;
  virtual void Serialize(ostream& out) const =0;
};

inline ostream& operator<<(ostream& out, const Question& q) {
  out << q.ToString();
  return out;
}

class QuestionQuestion : public Question {
public:
  QuestionQuestion() {
    qmark_ = TD::Convert("?");
  }

  bool Ask(const DTSent& sent) const {
    WordID lastTok = sent.src.back();
    return lastTok == qmark_;
  }

  string ToString() const {
    return "Is last token '?'";
  }

  void Serialize(ostream& out) const {
    out << "QQ";
  }
private:
  WordID qmark_;
};

class LengthQuestion : public Question {
public:
  LengthQuestion(const int len) : len_(len) {}

  bool Ask(const DTSent& sent) const {
    return sent.src.size() >= len_;
  }

  string ToString() const {
    stringstream s;
    s << "Has length >= " << len_;
    return s.str();
  }

  void Serialize(ostream& out) const {
    out << "L" << len_;
  }
private:
  int len_;
};
