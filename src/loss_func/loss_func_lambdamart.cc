/* Copyright 2016 Jiang Chen <criver@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "loss_func_lambdamart.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "loss_func_math.h"
#include "src/data_store/column.h"
#include "src/proto/config.pb.h"

using namespace std::placeholders;

namespace gbdt {

const int kNumPrecomputedDiscounts = 100;

inline double discount(uint rank, float base) {
  return log(base) / log(base + rank);
}

LambdaMART::LambdaMART(const LossFuncConfig& config)
    : Pairwise(config, [](double delta_target, double delta_func) {
                 return ComputeLogLoss(1, delta_func); }) {
  if (config.lambdamart_config().dcg_base() > 0) {
    dcg_base_ = config.lambdamart_config().dcg_base();
  }
  precomputed_discounts_.reserve(kNumPrecomputedDiscounts);
  for (int i = 0; i < kNumPrecomputedDiscounts; ++i) {
    precomputed_discounts_[i] = discount(i, dcg_base_);
  }

  discount_ = [this] (uint rank) {
    return rank < precomputed_discounts_.size() ? precomputed_discounts_[rank] : discount(rank, dcg_base_);
  };
}

// TODO(criver): Solve the following problem:
// At the beginning of the training, all scores are zero. We would sort them anyway and
// use the ranks to weight different pairs differently. Instead, we should know that
// and put uniform weights on all pairs at the beginning.
//
// Proposal:
// Sort scores in descending order.
// Compute the rank delta between adjacent store by p = 1 / (1 + exp(f_i - f_j)).
// rank_j =  rank_i + 2 * (p - 0.5).
// At the beginning of the training, there are less separation of scores. All ranks will be 0.
// With more score separation, the ranks will be more separated out.
vector<uint> ComputeRanks(const vector<uint>& group, const vector<double>& f) {
  // Computes the ranking based on the current function.
  vector<uint> ranking(group.size());
  for (int i = 0; i < ranking.size(); ++i) {
    ranking[i] = i;
  }

  // Sort by f.
  sort(ranking.begin(), ranking.end(),
       [&](uint i, uint j) { return f[group[i]] > f[group[j]]; });

  // For each index, store its rank.
  vector<uint> ranks(group.size());
  for (int i = 0; i < ranking.size(); ++i) {
    ranks[ranking[i]] = i;
  }

  return ranks;
}

function<double(const pair<uint, uint>&)> LambdaMART::GeneratePairWeightingFunc(
    const vector<uint>& group, const vector<double>& f) {
  ranks_ = ComputeRanks(group, f);

  // The weight is set to the delta_dcg if the pair is inverted.
  return [&, this] (const pair<uint, uint>& p) {
    double target_diff = (*target_column_)[group[p.first]] - (*target_column_)[group[p.second]];
    double discount_diff = fabs(discount_(ranks_[p.first]) - discount_(ranks_[p.second]));
    return target_diff * discount_diff;
  };
}

}  // namespace gbdt
