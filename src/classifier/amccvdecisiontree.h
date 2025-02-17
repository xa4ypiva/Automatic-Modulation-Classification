#ifndef AMCCVDECISIONTREE_H
#define AMCCVDECISIONTREE_H

#include <opencv2/core/core.hpp>
#include <opencv2/ml/ml.hpp>
#include <boost/smart_ptr.hpp>

#include "amc.h"

#include "amcclassifier.h"

class AmcCvDecisionTree : public AmcClassifier<double, AMC::ModType>
{
public:
    AmcCvDecisionTree(int max_depth = INT_MAX,
                      int min_sample_count = 10,
                      float regression_accuracy = 0.01f,
                      bool use_surrogates = true,
                      int max_categories = 10,
                      int cv_folds = 10,
                      bool use_1se_rule = true,
                      bool truncate_pruned_tree = true,
                      const float* priors = NULL);

    AMC::ModType classify(const std::vector<double> &predictData);
    bool train(const std::vector<std::vector<double> > &trainData, const std::vector<AMC::ModType> &responses);

    void save(const std::string & fileName);
    void load(const std::string & fileName);

private:
    boost::scoped_ptr<CvDTree> _tree;

    int _maxDepth;
    int _minSampleCount;
    float _regressionAccuracy;
    bool _useSurrogates;
    int _maxCategories;
    int _cvFolds;
    bool _use1SERule;
    bool _truncatePrunedTree;
    const float * _priors;
};


#endif // AMCCVDECISIONTREE_H
