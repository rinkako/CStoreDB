#include "DBCCandidateFunction.h"
CSTORE_NS_BEGIN
// CandidateFunction的构造器
CandidateFunction::CandidateFunction() : DBObject("CandidateFunction", this) {
  candidateProcesser = nullptr;
}

// CandidateFunction的拷贝构造器
CandidateFunction::CandidateFunction(const CandidateFunction& _other)
  : DBObject("CandidateFunction", this) {
  this->candidateProcesser = _other.candidateProcesser;
  this->candidateType = _other.candidateType;
}

// CandidateFunction带产生式的构造器
CandidateFunction::CandidateFunction(iHandle _proc, CFunctionType _pt)
  : DBObject("CandidateFunction", this) {
  this->SetProc(_proc, _pt);
}

// CandidateFunction设置产生式
void CandidateFunction::SetProc(iHandle _proc, CFunctionType _pt) {
  this->candidateProcesser = _proc;
  this->candidateType = _pt;
}

// CandidateFunction获得产生式向量
iHandle CandidateFunction::GetProc() const {
  return this->candidateProcesser;
}

// CandidateFunction获得产生式类型
CFunctionType CandidateFunction::GetType() const {
  return this->candidateType;
}

// CandidateFunction调用产生式
SyntaxTreeNode* CandidateFunction::Call(SyntaxTreeNode* _root, void* _parser, SyntaxType _syntax, istr _detail) const {
  return this->candidateProcesser(_root, _parser, this->candidateType, _syntax, _detail);
}

CSTORE_NS_END