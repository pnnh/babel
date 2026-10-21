#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace babel::fastpath {

/// 高频英文常用词表（bugfix: english-word-pinyin-misdetection 候选方向 1）。
///
/// 背景：`FastCorrector::looksLikeLatin()` 判定为真的 token 此前无条件进入三级
/// 拼音匹配管线（全拼 Trie → 简拼 Trie → 编辑距离回退），导致大量合法英文常用词
/// （如 "I"/"want"/"name"/"nice"/"meeting"）因与真实词库（约 89 万条）中的拼音
/// 字符串发生前缀/编辑距离碰撞而被误替换为语义无关的汉字（详见
/// `.specify/bugs/english-word-pinyin-misdetection/assessment.md`）。本类提供一
/// 个 O(1) 哈希查找的词表，供 `FastCorrector::buildLatticeCell()` 在
/// `looksLikeLatin()` 判定为真之后、进入拼音匹配管线之前优先拦截命中词，直接走
/// 透传分支（类比既有 `!looksLikeLatin()` 透传分支），避免这些词被当作拼音解析。
///
/// 数据来源：工作区既有 `temp/COCA60000.txt`（约 6 万条英文高频词，来源疑似
/// COCA 语料库词频表）。该文件在本仓库中长期以未提交的工作区文件形式存在，其
/// 具体来源出处与分发许可未经正式确认；用户已就此采用 "accept_risk" 决策，知情
/// 接受许可不确定性风险，明确不要求在本次修复中撰写正式的 LICENSE/NOTICE 声明
/// 文件（与 002 阶段对 GPLv3 iDvel/rime-ice 词库的处理方式不同）。随包分发的副本
/// 见 `babel/content/dictionaries/common-english-words.txt`。
class EnglishWordList {
 public:
  EnglishWordList() = default;

  /// 从纯文本文件加载词表（每行一个英文单词，忽略空行；大小写不敏感，内部统一
  /// 转为小写存储）。文件不存在/无法打开时返回空词表（不视为错误，调用方据此
  /// 静默跳过词表拦截，退化为"仅依赖后续匹配管线阈值收紧"的覆盖面，不中断主
  /// 流程——与仓库内其余词库加载失败时的兜底风格一致）。
  static EnglishWordList loadFromFile(const std::filesystem::path &path);

  /// 从内存中的词条列表直接构建（大小写不敏感，内部统一转为小写存储）。供单元
  /// 测试在不依赖磁盘文件的前提下构造词表使用；生产路径统一经
  /// `loadFromFile()` 加载随包分发的词表文件。
  static EnglishWordList fromWords(const std::vector<std::string> &words);

  /// 大小写不敏感查询：`token` 是否命中词表。内部对 `token` 做小写归一化后再查
  /// 找，调用方无需预先转换大小写。
  bool contains(const std::string &token) const;

  /// 已加载的词条数量，供诊断日志使用。
  std::size_t size() const { return m_words.size(); }

 private:
  std::unordered_set<std::string> m_words;
};

}  // namespace babel::fastpath
