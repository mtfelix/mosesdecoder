#pragma once
/*
 *  TargetPhrase.h
 *  CreateBerkeleyPt
 *
 *  Created by Hieu Hoang on 31/07/2009.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include "Phrase.h"

class Db;

namespace MosesBerkeleyPt
{

class TargetPhrase : public Phrase
{
protected:
	typedef std::pair<int, int>  AlignPair;
	typedef std::vector<AlignPair> AlignType;
	AlignType m_align;
	std::vector<float>	m_scores;
	std::vector<Word>		m_headWords;
	long m_targetId; // set when saved to db

	char *WritePhraseToMemory(size_t &memUsed,  int numScores, size_t sourceWordSize, size_t targetWordSize) const;

	size_t WriteAlignToMemory(char *mem) const;
	size_t WriteScoresToMemory(char *mem) const;

	size_t ReadAlignFromMemory(const char *mem);
	size_t ReadScoresFromMemory(const char *mem, size_t numScores);

	size_t ReadPhraseFromMemory(const char *mem, size_t numFactors);

public:
	void CreateAlignFromString(const std::string &alignString);
	void CreateScoresFromString(const std::string &inString);
	void CreateHeadwordsFromString(const std::string &inString, Vocab &vocab);

	const Word &GetHeadWords(size_t ind) const
	{ return m_headWords[ind]; }
	
	const AlignType &GetAlign() const
	{ return m_align; }
	size_t GetAlign(size_t sourcePos) const;
	
	long SaveTargetPhrase(Db &dbTarget, long &nextTargetId
												,int numScores, size_t sourceWordSize, size_t targetWordSize);	

	size_t ReadOtherInfoFromMemory(const char *mem
																, size_t numSourceFactors, size_t numTargetFactors
																, size_t numScores);
	char *WriteOtherInfoToMemory(size_t &memUsed, int numScores, size_t sourceWordSize, size_t targetWordSize) const;

	void Load(const Db &db, size_t numTargetFactors);

};
}; // namespace
