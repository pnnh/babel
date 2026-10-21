#include "babel/src/fastpath/EnglishWordList.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace babel::fastpath {

namespace {

std::string toLowerAscii(const std::string &input) {
  std::string result(input.size(), '\0');
  std::transform(input.begin(), input.end(), result.begin(),
                  [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

/// 去除行尾 \r（兼容 CRLF 来源文件）与首尾空白；空行返回空字符串。
std::string trim(const std::string &line) {
  std::size_t begin = 0;
  std::size_t end = line.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(line[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
    --end;
  }
  return line.substr(begin, end - begin);
}

}  // namespace

EnglishWordList EnglishWordList::loadFromFile(const std::filesystem::path &path) {
  EnglishWordList result;
  std::ifstream in(path);
  if (!in.is_open()) {
    return result;
  }
  std::string line;
  while (std::getline(in, line)) {
    const std::string trimmed = trim(line);
    if (trimmed.empty()) {
      continue;
    }
    result.m_words.insert(toLowerAscii(trimmed));
  }
  return result;
}

bool EnglishWordList::contains(const std::string &token) const {
  if (token.empty()) {
    return false;
  }
  return m_words.find(toLowerAscii(token)) != m_words.end();
}

EnglishWordList EnglishWordList::fromWords(const std::vector<std::string> &words) {
  EnglishWordList result;
  for (const auto &word : words) {
    const std::string trimmed = trim(word);
    if (trimmed.empty()) {
      continue;
    }
    result.m_words.insert(toLowerAscii(trimmed));
  }
  return result;
}

}  // namespace babel::fastpath
