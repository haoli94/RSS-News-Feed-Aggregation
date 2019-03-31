/**
 * File: stream-tokenizer.cc
 * -------------------------
 * Provides the implementation of the StreamTokenizer method set, which
 * operates on C++ strings, but is sensitive to the possibility that the
 * characters arrays inside are UTF8 encodings of (in some cases, multi-byte)
 * characters.
 */

#include <istream>
#include <string>
#include <cassert>
#include "stream-tokenizer.h"
using namespace std;

StreamTokenizer::StreamTokenizer(istream& is,
                                 const string& delimiters,
                                 bool skipDelimiters) : 
  is(is), delimiters(delimiters), skipDelimiters(skipDelimiters), saved(EOF) {
}

bool StreamTokenizer::hasMoreTokens() const {
  if (skipDelimiters) {
    while (true) {
      int ch = getNextChar();
      if (ch == EOF) return false;
		if (delimiters.find(ch) == string::npos) {
        saved = ch;
        return true;
      }
    }
  }
  
  if (saved != EOF) return true;
  saved = getNextChar();
  return saved != EOF;
}

string StreamTokenizer::nextToken() {
  assert(hasMoreTokens());
  string token;
  char ch = getNextChar();
  token += ch;
  assert(token.size() == 1);
  if (delimiters.find(ch) != string::npos) 
    return token;
  
  while (true) {
    ch = getNextChar();
    if (ch == EOF || delimiters.find(ch) != string::npos) break;
    token += ch;
  }
  
  if (ch != EOF) saved = ch;
  return token;
}

/**
 * Method: getNextChar
 * -------------------
 * Returns the next character in the stream, or EOF if 
 * there are no more characters.  Note that if saved is a
 * non-EOF character, then it is depleted and that's returned
 * as the next character.
 */
int StreamTokenizer::getNextChar() const {
  if (saved != EOF) {
    char next = saved;
    saved = EOF;
    return next;
  }

  return is.get();
}
