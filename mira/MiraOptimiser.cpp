#include "Optimiser.h"
#include "Hildreth.h"

using namespace Moses;
using namespace std;

namespace Mira {

vector<int> MiraOptimiser::updateWeights(ScoreComponentCollection& currWeights,
    const vector<vector<ScoreComponentCollection> >& featureValues,
    const vector<vector<float> >& losses,
    const vector<vector<float> >& bleuScores,
    const vector<ScoreComponentCollection>& oracleFeatureValues,
    const vector<float> oracleBleuScores, const vector<size_t> sentenceIds,
    float learning_rate, float max_sentence_update, size_t rank, size_t epoch,
    int updates_per_epoch,
    bool controlUpdates) {

	// add every oracle in batch to list of oracles (under certain conditions)
	for (size_t i = 0; i < oracleFeatureValues.size(); ++i) {
		float newWeightedScore = oracleFeatureValues[i].GetWeightedScore();
		size_t sentenceId = sentenceIds[i];

		// compare new oracle with existing oracles:
		// if same translation exists, just update the bleu score
		// if not, add the oracle
		bool updated = false;
		size_t indexOfWorst = 0;
		float worstWeightedScore = 0;
		for (size_t j = 0; j < m_oracles[sentenceId].size(); ++j) {
			float currentWeightedScore = m_oracles[sentenceId][j].GetWeightedScore();
			if (currentWeightedScore == newWeightedScore) {
			  cerr << "Rank " << rank << ", Bleu score of oracle updated at batch position " << i << ", " << m_bleu_of_oracles[sentenceId][j] << " --> " << oracleBleuScores[j] << endl;
				m_bleu_of_oracles[sentenceId][j] = oracleBleuScores[j];
				updated = true;
				break;
			} else if (worstWeightedScore == 0 || currentWeightedScore
			    > worstWeightedScore) {
				worstWeightedScore = currentWeightedScore;
				indexOfWorst = j;
			}
		}

		if (!updated) {
			// add if number of maximum oracles not exceeded, otherwise override the worst
			if (m_max_number_oracles > m_oracles[sentenceId].size()) {
				m_oracles[sentenceId].push_back(oracleFeatureValues[i]);
				m_bleu_of_oracles[sentenceId].push_back(oracleBleuScores[i]);
			} else {
				m_oracles[sentenceId][indexOfWorst] = oracleFeatureValues[i];
				m_bleu_of_oracles[sentenceId][indexOfWorst] = oracleBleuScores[i];
			}
		}
	}

	// vector of feature values differences for all created constraints
	vector<ScoreComponentCollection> featureValueDiffs;
	vector<float> lossMinusModelScoreDiffs;
	vector<float> all_losses;

	// most violated constraint in batch
	ScoreComponentCollection max_batch_featureValueDiff;
	float max_batch_loss = -1;
	float max_batch_lossMinusModelScoreDiff = -1;

	// Make constraints for new hypothesis translations
	float epsilon = 0.0001;
	int violatedConstraintsBefore = 0;
	float oldDistanceFromOptimum = 0;
	// iterate over input sentences (1 (online) or more (batch))
	for (size_t i = 0; i < featureValues.size(); ++i) {
		size_t sentenceId = sentenceIds[i];
		if (m_oracles[sentenceId].size() > 1)
			cerr << "Rank " << rank << ", available oracles for source sentence " << sentenceId << ": "  << m_oracles[sentenceId].size() << endl;
		// iterate over hypothesis translations for one input sentence
		for (size_t j = 0; j < featureValues[i].size(); ++j) {
			for (size_t k = 0; k < m_oracles[sentenceId].size(); ++k) {
				ScoreComponentCollection featureValueDiff = m_oracles[sentenceId][k];
				featureValueDiff.MinusEquals(featureValues[i][j]);
				float modelScoreDiff = featureValueDiff.InnerProduct(currWeights);
				float loss = losses[i][j];
				loss *= m_marginScaleFactor;
		    if (m_weightedLossFunction == 1) {
		    	loss *= bleuScores[i][j];
		    }
		    else if (m_weightedLossFunction == 2) {
		    	loss *= log2(bleuScores[i][j]);
		    }
		    else if (m_weightedLossFunction == 10) {
		    	loss *= log10(bleuScores[i][j]);
		    }

		  	// check if constraint is violated
				bool violated = false;
				bool addConstraint = true;
				float diff = loss - modelScoreDiff;
				if (diff > epsilon) {
					violated = true;
				}
				else if (m_onlyViolatedConstraints) {
					addConstraint = false;
				}

				float lossMinusModelScoreDiff = loss - modelScoreDiff;
				if (violated) {
					if (m_accumulateMostViolatedConstraints || m_pastAndCurrentConstraints) {
						// find the most violated constraint per batch
						if (lossMinusModelScoreDiff > max_batch_lossMinusModelScoreDiff) {
							max_batch_featureValueDiff = featureValueDiff;
							max_batch_loss = loss;
					  }
					}
				}

				if (addConstraint && !m_accumulateMostViolatedConstraints) {
					featureValueDiffs.push_back(featureValueDiff);
					lossMinusModelScoreDiffs.push_back(lossMinusModelScoreDiff);
					all_losses.push_back(loss);

					if (violated) {
						++violatedConstraintsBefore;
						oldDistanceFromOptimum += (loss - modelScoreDiff);
					}
				}
			}
		}
	}

	if (m_pastAndCurrentConstraints || m_accumulateMostViolatedConstraints) {
		cerr << "Rank " << rank << ", epoch " << epoch << ", number of current constraints: " << featureValueDiffs.size() << endl;
		cerr << "Rank " << rank << ", epoch " << epoch << ", number of current violated constraints: " << violatedConstraintsBefore << endl;
	}

	if (m_max_number_oracles == 1) {
		for (size_t k = 0; k < sentenceIds.size(); ++k) {
			size_t sentenceId = sentenceIds[k];
			m_oracles[sentenceId].clear();
		}
	}

	size_t pastViolatedConstraints = 0;
	// Add constraints from past iterations (BEFORE updating that list)
	if (m_pastAndCurrentConstraints || m_accumulateMostViolatedConstraints) {
	  // add all past (most violated) constraints to the list of current constraints, computed with current weights!
	  for (size_t i = 0; i < m_featureValueDiffs.size(); ++i) {
	  	float modelScoreDiff = m_featureValueDiffs[i].InnerProduct(currWeights);

	  	// check if constraint is violated
			bool violated = false;
			bool addConstraint = true;
			float diff = m_losses[i] - modelScoreDiff;
			if (diff > epsilon) {
				violated = true;
			}
			else if (m_onlyViolatedConstraints) {
				addConstraint = false;
			}

	    if (addConstraint) {
	    	featureValueDiffs.push_back(m_featureValueDiffs[i]);
	    	lossMinusModelScoreDiffs.push_back(m_losses[i] - modelScoreDiff);
	    	all_losses.push_back(m_losses[i]);

	    	if (violated) {
	    		++violatedConstraintsBefore;
	    		++pastViolatedConstraints;
	    		oldDistanceFromOptimum += diff;
	    	}
	    }
	  }
	}

	if (m_pastAndCurrentConstraints || m_accumulateMostViolatedConstraints) {
		cerr << "Rank " << rank << ", epoch " << epoch << ", number of past constraints: " << m_featureValueDiffs.size() << endl;
		cerr << "Rank " << rank << ", epoch " << epoch << ", number of past violated constraints: " << pastViolatedConstraints << endl;
	}

	// Add new most violated constraint to the list of current constraints
	if (m_accumulateMostViolatedConstraints) {
		if (max_batch_loss != -1) {
			float modelScoreDiff = max_batch_featureValueDiff.InnerProduct(currWeights);
			float diff = max_batch_loss - modelScoreDiff;
    	++violatedConstraintsBefore;
    	++pastViolatedConstraints;
    	oldDistanceFromOptimum += (diff);

    	featureValueDiffs.push_back(max_batch_featureValueDiff);
    	lossMinusModelScoreDiffs.push_back(max_batch_loss - modelScoreDiff);
    	all_losses.push_back(max_batch_loss);
		}
	}

	// Update the list of accumulated most violated constraints
	if (max_batch_loss != -1) {
		m_featureValueDiffs.push_back(max_batch_featureValueDiff);
		m_losses.push_back(max_batch_loss);
	}

	// run optimisation: compute alphas for all given constraints
	vector<float> alphas;
	ScoreComponentCollection summedUpdate;
	if (violatedConstraintsBefore > 0) {
	  cerr << "Rank " << rank << ", epoch " << epoch << ", number of constraints passed to optimizer: " << featureValueDiffs.size() << endl;
	  cerr << "Rank " << rank << ", epoch " << epoch << ", number of violated constraints passed to optimizer: " << violatedConstraintsBefore << endl;
	  if (m_slack != 0) {
	    alphas = Hildreth::optimise(featureValueDiffs, lossMinusModelScoreDiffs, m_slack);
	  } else {
	    alphas = Hildreth::optimise(featureValueDiffs, lossMinusModelScoreDiffs);
	  }
  
	  // Update the weight vector according to the alphas and the feature value differences
	  // * w' = w' + SUM alpha_i * (h_i(oracle) - h_i(hypothesis))
	  for (size_t k = 0; k < featureValueDiffs.size(); ++k) {
	  	float alpha = alphas[k];
	  	ScoreComponentCollection update(featureValueDiffs[k]);
	    update.MultiplyEquals(alpha);
	    
	    // sum up update
	    summedUpdate.PlusEquals(update);
	  }
	} 
	else {
	  cerr << "Rank " << rank << ", epoch " << epoch << ", check, no constraint violated for this batch" << endl;
	  vector<int> status(3);
	  status[0] = 1;
	  status[1] = 0;
	  status[2] = 0;
	  return status;
	}

	ScoreComponentCollection newWeights(currWeights);
	newWeights.PlusEquals(summedUpdate);

	// Sanity check: are there still violated constraints after optimisation?
	int violatedConstraintsAfter = 0;
	float newDistanceFromOptimum = 0;
	for (size_t i = 0; i < featureValueDiffs.size(); ++i) {
		float modelScoreDiff = featureValueDiffs[i].InnerProduct(newWeights);
		float loss = all_losses[i];
		float diff = loss - modelScoreDiff;
		if (diff > epsilon) {
			++violatedConstraintsAfter;
			newDistanceFromOptimum += diff;
		}
	}
	cerr << "Rank " << rank << ", epoch " << epoch << ", check, violated constraint before: " << violatedConstraintsBefore << ", after: " << violatedConstraintsAfter  << ", change: " << violatedConstraintsBefore - violatedConstraintsAfter << endl;
	cerr << "Rank " << rank << ", epoch " << epoch << ", check, error before: " << oldDistanceFromOptimum << ", after: " << newDistanceFromOptimum << ", change: " << oldDistanceFromOptimum - newDistanceFromOptimum << endl;

	if (controlUpdates && violatedConstraintsAfter > 0) {
		float distanceChange = oldDistanceFromOptimum - newDistanceFromOptimum;
		if ((violatedConstraintsBefore - violatedConstraintsAfter) < 0 && distanceChange < 0) {
			vector<int> statusPlus(3);
			statusPlus[0] = -1;
			statusPlus[1] = violatedConstraintsBefore;
			statusPlus[2] = violatedConstraintsAfter;
			return statusPlus;
	  }
	}

	// Apply learning rate (fixed or flexible)
	if (learning_rate != 1) {
		cerr << "Rank " << rank << ", update before applying learning rate: " << summedUpdate << endl;
		summedUpdate.MultiplyEquals(learning_rate);
		cerr << "Rank " << rank << ", update after applying learning rate: " << summedUpdate << endl;
	}

	// Apply threshold scaling
	if (max_sentence_update != -1) {
		cerr << "Rank " << rank << ", update before scaling to max-sentence-update: " << summedUpdate << endl;
		summedUpdate.ThresholdScaling(max_sentence_update);
		cerr << "Rank " << rank << ", update after scaling to max-sentence-update: " << summedUpdate << endl;
	}

	// Apply update to weight vector or store it for later
	if (updates_per_epoch > 0) {
		m_accumulatedUpdates.PlusEquals(summedUpdate);
		cerr << "Rank " << rank << ", new accumulated updates:" << m_accumulatedUpdates << endl;
	} else {
		// apply update to weight vector
		cerr << "Rank " << rank << ", weights before update: " << currWeights << endl;
		currWeights.PlusEquals(summedUpdate);
		cerr << "Rank " << rank << ", weights after update: " << currWeights << endl;
	}

	vector<int> statusPlus(3);
	statusPlus[0] = 0;
	statusPlus[1] = violatedConstraintsBefore;
	statusPlus[2] = violatedConstraintsAfter;
	return statusPlus;
}

vector<int> MiraOptimiser::updateWeightsAnalytically(ScoreComponentCollection& currWeights,
    const ScoreComponentCollection& featureValues,
    float loss,
    const ScoreComponentCollection& oracleFeatureValues,
    float oracleBleuScore,
    size_t sentenceId,
    float learning_rate,
    float max_sentence_update,
    size_t rank,
    size_t epoch,
    bool controlUpdates) {

  float epsilon = 0.0001;
  float oldDistanceFromOptimum = 0;
  bool constraintViolatedBefore = false;
  ScoreComponentCollection weightUpdate;

  ScoreComponentCollection featureValueDiff = oracleFeatureValues;
  featureValueDiff.MinusEquals(featureValues);
  float modelScoreDiff = featureValueDiff.InnerProduct(currWeights);
  float diff = loss - modelScoreDiff;
  // approximate comparison between floats
  if (diff > epsilon) {
    // constraint violated
    oldDistanceFromOptimum += (loss - modelScoreDiff);
    constraintViolatedBefore = true;
    float lossMinusModelScoreDiff = loss - modelScoreDiff;

    // compute alpha for given constraint: (loss - model score diff) / || feature value diff ||^2
    // featureValueDiff.GetL2Norm() * featureValueDiff.GetL2Norm() == featureValueDiff.InnerProduct(featureValueDiff)
    // from Crammer&Singer 2006: alpha = min {C , l_t/ ||x||^2}
    cerr << "Rank " << rank << ", epoch " << epoch << ", feature value diff: " << featureValueDiff << endl;
    float squaredNorm = featureValueDiff.GetL2Norm() * featureValueDiff.GetL2Norm();

    if (squaredNorm > 0) {
    	float alpha = (lossMinusModelScoreDiff) / squaredNorm;
    	if (m_slack > 0 && alpha > m_slack) {
    		alpha = m_slack;
    	}

    	cerr << "Rank " << rank << ", epoch " << epoch << ", alpha: " << alpha << endl;
    	featureValueDiff.MultiplyEquals(alpha);
    	weightUpdate.PlusEquals(featureValueDiff);
    }
    else {
    	cerr << "Rank " << rank << ", epoch " << epoch << ", no update because squared norm is 0, can only happen if oracle == hypothesis, are bleu scores equal as well?" << endl;
    }
  }


  if (m_max_number_oracles == 1) {
    m_oracles[sentenceId].clear();
  }

  if (!constraintViolatedBefore) {
    // constraint satisfied, nothing to do
    cerr << "Rank " << rank << ", epoch " << epoch << ", check, constraint already satisfied" << endl;
    vector<int> status(3);
    status[0] = 1;
    status[1] = 0;
    status[2] = 0;
    return status;
  }

  // sanity check: constraint still violated after optimisation?
  ScoreComponentCollection newWeights(currWeights);
  newWeights.PlusEquals(weightUpdate);

  bool constraintViolatedAfter = false;
  float newDistanceFromOptimum = 0;
  featureValueDiff = oracleFeatureValues;
  featureValueDiff.MinusEquals(featureValues);
  modelScoreDiff = featureValueDiff.InnerProduct(newWeights);
  diff = loss - modelScoreDiff;
  // approximate comparison between floats!
  if (diff > epsilon) {
    constraintViolatedAfter = true;
    newDistanceFromOptimum += (loss - modelScoreDiff);
  }

  cerr << "Rank " << rank << ", epoch " << epoch << ", check, constraint violated before? " << constraintViolatedBefore << ", after? " << constraintViolatedAfter << endl;
  cerr << "Rank " << rank << ", epoch " << epoch << ", check, error before: " << oldDistanceFromOptimum << ", after: " << newDistanceFromOptimum << ", change: " << oldDistanceFromOptimum - newDistanceFromOptimum << endl;

  float distanceChange = oldDistanceFromOptimum - newDistanceFromOptimum;
  if (controlUpdates && constraintViolatedAfter && distanceChange < 0) {
    vector<int> statusPlus(3);
    statusPlus[0] = -1;
    statusPlus[1] = 1;
    statusPlus[2] = 1;
    return statusPlus;
  }

  // apply learning rate
  if (learning_rate != 1) {
    cerr << "Rank " << rank << ", update before applying learning rate: " << weightUpdate << endl;
    weightUpdate.MultiplyEquals(learning_rate);
    cerr << "Rank " << rank << ", update after applying learning rate: " << weightUpdate << endl;
  }

  // apply threshold scaling
  if (max_sentence_update != -1) {
    cerr << "Rank " << rank << ", update before scaling to max-sentence-update: " << weightUpdate << endl;
    weightUpdate.ThresholdScaling(max_sentence_update);
    cerr << "Rank " << rank << ", update after scaling to max-sentence-update: " << weightUpdate << endl;
  }

  // apply update to weight vector
  cerr << "Rank " << rank << ", weights before update: " << currWeights << endl;
  currWeights.PlusEquals(weightUpdate);
  cerr << "Rank " << rank << ", weights after update: " << currWeights << endl;

  vector<int> statusPlus(3);
  statusPlus[0] = 0;
  statusPlus[1] = 1;
  statusPlus[2] = constraintViolatedAfter ? 1 : 0;
  return statusPlus;
}

}
