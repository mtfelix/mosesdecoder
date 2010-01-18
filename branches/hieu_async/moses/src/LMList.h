// $Id: LMList.h 158 2007-10-22 00:47:01Z hieu $

/***********************************************************************
Moses - factored phrase-based language decoder
Copyright (C) 2006 University of Edinburgh

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
***********************************************************************/

#pragma once

#include <list>
#include "LanguageModel.h"

class Phrase;
class ScoreColl;
class ScoreComponentCollection;
class FactorMask;

//! List of language models
class LMList : public std::list < LanguageModel* >	
{
public:
	void CalcScore(const FactorMask &mask, const Phrase &phrase, float &retFullScore, float &retNGramScore, ScoreComponentCollection* breakdown) const;

};