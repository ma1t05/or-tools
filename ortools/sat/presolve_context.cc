// Copyright 2010-2018 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ortools/sat/presolve_context.h"

#include "ortools/base/map_util.h"
#include "ortools/base/mathutil.h"
#include "ortools/port/proto_utils.h"

namespace operations_research {
namespace sat {

void PresolveContext::ClearStats() { stats_by_rule_name.clear(); }

int PresolveContext::NewIntVar(const Domain& domain) {
  IntegerVariableProto* const var = working_model->add_variables();
  FillDomainInProto(domain, var);
  InitializeNewDomains();
  return working_model->variables_size() - 1;
}

int PresolveContext::NewBoolVar() { return NewIntVar(Domain(0, 1)); }

int PresolveContext::GetOrCreateConstantVar(int64 cst) {
  if (!gtl::ContainsKey(constant_to_ref, cst)) {
    constant_to_ref[cst] = working_model->variables_size();
    IntegerVariableProto* const var_proto = working_model->add_variables();
    var_proto->add_domain(cst);
    var_proto->add_domain(cst);
    InitializeNewDomains();
  }
  return constant_to_ref[cst];
}

// a => b.
void PresolveContext::AddImplication(int a, int b) {
  ConstraintProto* const ct = working_model->add_constraints();
  ct->add_enforcement_literal(a);
  ct->mutable_bool_and()->add_literals(b);
}

// b => x in [lb, ub].
void PresolveContext::AddImplyInDomain(int b, int x, const Domain& domain) {
  ConstraintProto* const imply = working_model->add_constraints();

  // Doing it like this seems to use slightly less memory.
  // TODO(user): Find the best way to create such small proto.
  imply->mutable_enforcement_literal()->Resize(1, b);
  LinearConstraintProto* mutable_linear = imply->mutable_linear();
  mutable_linear->mutable_vars()->Resize(1, x);
  mutable_linear->mutable_coeffs()->Resize(1, 1);
  FillDomainInProto(domain, mutable_linear);
}

bool PresolveContext::DomainIsEmpty(int ref) const {
  return domains[PositiveRef(ref)].IsEmpty();
}

bool PresolveContext::IsFixed(int ref) const {
  DCHECK(!DomainIsEmpty(ref));
  return domains[PositiveRef(ref)].IsFixed();
}

bool PresolveContext::CanBeUsedAsLiteral(int ref) const {
  const int var = PositiveRef(ref);
  return domains[var].Min() >= 0 && domains[var].Max() <= 1;
}

bool PresolveContext::LiteralIsTrue(int lit) const {
  DCHECK(CanBeUsedAsLiteral(lit));
  if (RefIsPositive(lit)) {
    return domains[lit].Min() == 1;
  } else {
    return domains[PositiveRef(lit)].Max() == 0;
  }
}

bool PresolveContext::LiteralIsFalse(int lit) const {
  DCHECK(CanBeUsedAsLiteral(lit));
  if (RefIsPositive(lit)) {
    return domains[lit].Max() == 0;
  } else {
    return domains[PositiveRef(lit)].Min() == 1;
  }
}

int64 PresolveContext::MinOf(int ref) const {
  DCHECK(!DomainIsEmpty(ref));
  return RefIsPositive(ref) ? domains[PositiveRef(ref)].Min()
                            : -domains[PositiveRef(ref)].Max();
}

int64 PresolveContext::MaxOf(int ref) const {
  DCHECK(!DomainIsEmpty(ref));
  return RefIsPositive(ref) ? domains[PositiveRef(ref)].Max()
                            : -domains[PositiveRef(ref)].Min();
}

int64 PresolveContext::MinOf(const LinearExpressionProto& expr) const {
  int64 result = expr.offset();
  for (int i = 0; i < expr.vars_size(); ++i) {
    const int64 coeff = expr.coeffs(i);
    if (coeff > 0) {
      result += coeff * MinOf(expr.vars(i));
    } else {
      result += coeff * MaxOf(expr.vars(i));
    }
  }
  return result;
}

int64 PresolveContext::MaxOf(const LinearExpressionProto& expr) const {
  int64 result = expr.offset();
  for (int i = 0; i < expr.vars_size(); ++i) {
    const int64 coeff = expr.coeffs(i);
    if (coeff > 0) {
      result += coeff * MaxOf(expr.vars(i));
    } else {
      result += coeff * MinOf(expr.vars(i));
    }
  }
  return result;
}

bool PresolveContext::VariableIsNotRepresentativeOfEquivalenceClass(
    int var) const {
  DCHECK(RefIsPositive(var));
  if (affine_relations_.ClassSize(var) == 1) return true;
  return GetAffineRelation(var).representative != var;
}

// Tricky: If this variable is equivalent to another one (but not the
// representative) and appear in just one constraint, then this constraint must
// be the affine defining one. And in this case the code using this function
// should do the proper stuff.
bool PresolveContext::VariableIsUniqueAndRemovable(int ref) const {
  if (!ConstraintVariableGraphIsUpToDate()) return false;
  const int var = PositiveRef(ref);
  return var_to_constraints_[var].size() == 1 &&
         VariableIsNotRepresentativeOfEquivalenceClass(var) &&
         !keep_all_feasible_solutions;
}

// Tricky: Same remark as for VariableIsUniqueAndRemovable().
bool PresolveContext::VariableWithCostIsUniqueAndRemovable(int ref) const {
  if (!ConstraintVariableGraphIsUpToDate()) return false;
  const int var = PositiveRef(ref);
  return !keep_all_feasible_solutions &&
         var_to_constraints_[var].contains(-1) &&
         var_to_constraints_[var].size() == 2 &&
         VariableIsNotRepresentativeOfEquivalenceClass(var);
}

// Here, even if the variable is equivalent to others, if its affine defining
// constraints where removed, then it is not needed anymore.
bool PresolveContext::VariableIsNotUsedAnymore(int ref) const {
  if (!ConstraintVariableGraphIsUpToDate()) return false;
  return var_to_constraints_[PositiveRef(ref)].empty();
}

bool PresolveContext::VariableIsOnlyUsedInEncoding(int ref) const {
  if (!ConstraintVariableGraphIsUpToDate()) return false;
  const int var = PositiveRef(ref);
  return var_to_num_linear1_[var] == var_to_constraints_[var].size();
}

Domain PresolveContext::DomainOf(int ref) const {
  Domain result;
  if (RefIsPositive(ref)) {
    result = domains[ref];
  } else {
    result = domains[PositiveRef(ref)].Negation();
  }
  return result;
}

bool PresolveContext::DomainContains(int ref, int64 value) const {
  if (!RefIsPositive(ref)) {
    return domains[PositiveRef(ref)].Contains(-value);
  }
  return domains[ref].Contains(value);
}

ABSL_MUST_USE_RESULT bool PresolveContext::IntersectDomainWith(
    int ref, const Domain& domain, bool* domain_modified) {
  DCHECK(!DomainIsEmpty(ref));
  const int var = PositiveRef(ref);

  if (RefIsPositive(ref)) {
    if (domains[var].IsIncludedIn(domain)) {
      return true;
    }
    domains[var] = domains[var].IntersectionWith(domain);
  } else {
    const Domain temp = domain.Negation();
    if (domains[var].IsIncludedIn(temp)) {
      return true;
    }
    domains[var] = domains[var].IntersectionWith(temp);
  }

  if (domain_modified != nullptr) {
    *domain_modified = true;
  }
  modified_domains.Set(var);
  if (domains[var].IsEmpty()) {
    is_unsat = true;
    return false;
  }
  return true;
}

ABSL_MUST_USE_RESULT bool PresolveContext::SetLiteralToFalse(int lit) {
  const int var = PositiveRef(lit);
  const int64 value = RefIsPositive(lit) ? 0 : 1;
  return IntersectDomainWith(var, Domain(value));
}

ABSL_MUST_USE_RESULT bool PresolveContext::SetLiteralToTrue(int lit) {
  return SetLiteralToFalse(NegatedRef(lit));
}

void PresolveContext::UpdateRuleStats(const std::string& name) {
  if (enable_stats) {
    VLOG(1) << num_presolve_operations << " : " << name;
    stats_by_rule_name[name]++;
  }
  num_presolve_operations++;
}

void PresolveContext::UpdateLinear1Usage(const ConstraintProto& ct, int c) {
  const int old_var = constraint_to_linear1_var_[c];
  if (old_var >= 0) {
    var_to_num_linear1_[old_var]--;
  }
  if (ct.constraint_case() == ConstraintProto::ConstraintCase::kLinear &&
      ct.linear().vars().size() == 1) {
    const int var = PositiveRef(ct.linear().vars(0));
    constraint_to_linear1_var_[c] = var;
    var_to_num_linear1_[var]++;
  }
}

void PresolveContext::AddVariableUsage(int c) {
  const ConstraintProto& ct = working_model->constraints(c);
  constraint_to_vars_[c] = UsedVariables(ct);
  constraint_to_intervals_[c] = UsedIntervals(ct);
  for (const int v : constraint_to_vars_[c]) var_to_constraints_[v].insert(c);
  for (const int i : constraint_to_intervals_[c]) interval_usage_[i]++;
  UpdateLinear1Usage(ct, c);
}

void PresolveContext::UpdateConstraintVariableUsage(int c) {
  DCHECK_EQ(constraint_to_vars_.size(), working_model->constraints_size());
  const ConstraintProto& ct = working_model->constraints(c);

  // We don't optimize the interval usage as this is not super frequent.
  for (const int i : constraint_to_intervals_[c]) interval_usage_[i]--;
  constraint_to_intervals_[c] = UsedIntervals(ct);
  for (const int i : constraint_to_intervals_[c]) interval_usage_[i]++;

  // For the variables, we avoid an erase() followed by an insert() for the
  // variables that didn't change.
  tmp_new_usage_ = UsedVariables(ct);
  const std::vector<int>& old_usage = constraint_to_vars_[c];
  const int old_size = old_usage.size();
  int i = 0;
  for (const int var : tmp_new_usage_) {
    while (i < old_size && old_usage[i] < var) {
      var_to_constraints_[old_usage[i]].erase(c);
      ++i;
    }
    if (i < old_size && old_usage[i] == var) {
      ++i;
    } else {
      var_to_constraints_[var].insert(c);
    }
  }
  for (; i < old_size; ++i) var_to_constraints_[old_usage[i]].erase(c);
  constraint_to_vars_[c] = tmp_new_usage_;

  UpdateLinear1Usage(ct, c);
}

bool PresolveContext::ConstraintVariableGraphIsUpToDate() const {
  return constraint_to_vars_.size() == working_model->constraints_size();
}

void PresolveContext::UpdateNewConstraintsVariableUsage() {
  const int old_size = constraint_to_vars_.size();
  const int new_size = working_model->constraints_size();
  CHECK_LE(old_size, new_size);
  constraint_to_vars_.resize(new_size);
  constraint_to_linear1_var_.resize(new_size, -1);
  constraint_to_intervals_.resize(new_size);
  interval_usage_.resize(new_size);
  for (int c = old_size; c < new_size; ++c) {
    AddVariableUsage(c);
  }
}

bool PresolveContext::ConstraintVariableUsageIsConsistent() {
  if (is_unsat) return true;  // We do not care in this case.
  if (constraint_to_vars_.size() != working_model->constraints_size()) {
    LOG(INFO) << "Wrong constraint_to_vars size!";
    return false;
  }
  for (int c = 0; c < constraint_to_vars_.size(); ++c) {
    if (constraint_to_vars_[c] !=
        UsedVariables(working_model->constraints(c))) {
      LOG(INFO) << "Wrong variables usage for constraint: \n"
                << ProtobufDebugString(working_model->constraints(c))
                << "old_size: " << constraint_to_vars_[c].size();
      return false;
    }
  }
  return true;
}

// If a Boolean variable (one with domain [0, 1]) appear in this affine
// equivalence class, then we want its representative to be Boolean. Note that
// this is always possible because a Boolean variable can never be equal to a
// multiple of another if std::abs(coeff) is greater than 1 and if it is not
// fixed to zero. This is important because it allows to simply use the same
// representative for any referenced literals.
//
// Note(user): When both domain contains [0,1] and later the wrong variable
// become usable as boolean, then we have a bug. Because of that, the code
// for GetLiteralRepresentative() is not as simple as it should be.
bool PresolveContext::AddRelation(int x, int y, int c, int o,
                                  AffineRelation* repo) {
  // When the coefficient is larger than one, then if later one variable becomes
  // Boolean, it must be the representative.
  if (std::abs(c) != 1) return repo->TryAdd(x, y, c, o);

  const int rep_x = repo->Get(x).representative;
  const int rep_y = repo->Get(y).representative;
  const bool allow_rep_x = CanBeUsedAsLiteral(rep_x);
  const bool allow_rep_y = CanBeUsedAsLiteral(rep_y);
  if (allow_rep_x || allow_rep_y) {
    return repo->TryAdd(x, y, c, o, allow_rep_x, allow_rep_y);
  } else {
    // If none are boolean, we do not care about which is used as
    // representative.
    return repo->TryAdd(x, y, c, o);
  }
}

void PresolveContext::ExploitFixedDomain(int var) {
  CHECK(RefIsPositive(var));
  CHECK(IsFixed(var));
  const int min = MinOf(var);
  if (gtl::ContainsKey(constant_to_ref, min)) {
    const int representative = constant_to_ref[min];
    if (representative != var) {
      AddRelation(var, representative, 1, 0, &affine_relations_);
      AddRelation(var, representative, 1, 0, &var_equiv_relations_);
    }
  } else {
    constant_to_ref[min] = var;
  }
}

void PresolveContext::StoreAffineRelation(const ConstraintProto& ct, int ref_x,
                                          int ref_y, int64 coeff,
                                          int64 offset) {
  if (is_unsat) return;
  if (IsFixed(ref_x) || IsFixed(ref_y)) return;

  const int x = PositiveRef(ref_x);
  const int y = PositiveRef(ref_y);
  const int64 c = RefIsPositive(ref_x) == RefIsPositive(ref_y) ? coeff : -coeff;
  const int64 o = RefIsPositive(ref_x) ? offset : -offset;

  // TODO(user): can we force the rep and remove GetAffineRelation()?
  bool added = AddRelation(x, y, c, o, &affine_relations_);
  if ((c == 1 || c == -1) && o == 0) {
    added |= AddRelation(x, y, c, o, &var_equiv_relations_);
  }
  if (added) {
    // The domain didn't change, but this notification allows to re-process any
    // constraint containing these variables. Note that we do not need to
    // retrigger a propagation of the constraint containing a variable whose
    // representative didn't change.
    if (GetAffineRelation(x).representative != x) modified_domains.Set(x);
    if (GetAffineRelation(y).representative != y) modified_domains.Set(y);
    affine_constraints.insert(&ct);
  }
}

void PresolveContext::StoreBooleanEqualityRelation(int ref_a, int ref_b) {
  CHECK(CanBeUsedAsLiteral(ref_a));
  CHECK(CanBeUsedAsLiteral(ref_b));
  if (ref_a == ref_b) return;
  if (ref_a == NegatedRef(ref_b)) {
    is_unsat = true;
    return;
  }

  const int var_a = PositiveRef(ref_a);
  const int var_b = PositiveRef(ref_b);

  if (GetAffineRelation(var_a).representative == var_b ||
      GetAffineRelation(var_b).representative == var_a) {
    return;
  }

  // For now, we do need to add the relation ref_a == ref_b so we have a
  // proper variable usage count and propagation between ref_a and ref_b.
  //
  // TODO(user): This looks unclean. We should probably handle the affine
  // relation together without the need of keep all the constraints that
  // define them around.
  ConstraintProto* ct = working_model->add_constraints();
  auto* arg = ct->mutable_linear();
  arg->add_vars(var_a);
  arg->add_coeffs(1);
  arg->add_vars(var_b);
  if (RefIsPositive(ref_a) == RefIsPositive(ref_b)) {
    // a = b
    arg->add_coeffs(-1);
    arg->add_domain(0);
    arg->add_domain(0);
    StoreAffineRelation(*ct, var_a, var_b, /*coeff=*/1, /*offset=*/0);
  } else {
    // a = 1 - b
    arg->add_coeffs(1);
    arg->add_domain(1);
    arg->add_domain(1);
    StoreAffineRelation(*ct, var_a, var_b, /*coeff=*/-1, /*offset=*/1);
  }
}

bool PresolveContext::StoreAbsRelation(int target_ref, int ref) {
  const auto insert_status =
      abs_relations.insert(std::make_pair(target_ref, PositiveRef(ref)));
  return insert_status.second;
}

int PresolveContext::GetLiteralRepresentative(int ref) const {
  const AffineRelation::Relation r = GetAffineRelation(PositiveRef(ref));

  CHECK(CanBeUsedAsLiteral(ref));
  if (!CanBeUsedAsLiteral(r.representative)) {
    // Note(user): This can happen is some corner cases where the affine
    // relation where added before the variable became usable as Boolean. When
    // this is the case, the domain will be of the form [x, x + 1] and should be
    // later remapped to a Boolean variable.
    return ref;
  }

  // We made sure that the affine representative can always be used as a
  // literal. However, if some variable are fixed, we might not have only
  // (coeff=1 offset=0) or (coeff=-1 offset=1) and we might have something like
  // (coeff=8 offset=0) which is only valid for both variable at zero...
  //
  // What is sure is that depending on the value, only one mapping can be valid
  // because r.coeff can never be zero.
  const bool positive_possible = (r.offset == 0 || r.coeff + r.offset == 1);
  const bool negative_possible = (r.offset == 1 || r.coeff + r.offset == 0);
  DCHECK_NE(positive_possible, negative_possible);
  if (RefIsPositive(ref)) {
    return positive_possible ? r.representative : NegatedRef(r.representative);
  } else {
    return positive_possible ? NegatedRef(r.representative) : r.representative;
  }
}

int PresolveContext::GetVariableRepresentative(int ref) const {
  const AffineRelation::Relation r = var_equiv_relations_.Get(PositiveRef(ref));
  CHECK_EQ(std::abs(r.coeff), 1);
  CHECK_EQ(r.offset, 0);
  return RefIsPositive(ref) == (r.coeff == 1) ? r.representative
                                              : NegatedRef(r.representative);
}

// This makes sure that the affine relation only uses one of the
// representative from the var_equiv_relations_.
AffineRelation::Relation PresolveContext::GetAffineRelation(int ref) const {
  AffineRelation::Relation r = affine_relations_.Get(PositiveRef(ref));
  AffineRelation::Relation o = var_equiv_relations_.Get(r.representative);
  r.representative = o.representative;
  if (o.coeff == -1) r.coeff = -r.coeff;
  if (!RefIsPositive(ref)) {
    r.coeff *= -1;
    r.offset *= -1;
  }
  return r;
}

// Create the internal structure for any new variables in working_model.
void PresolveContext::InitializeNewDomains() {
  for (int i = domains.size(); i < working_model->variables_size(); ++i) {
    domains.emplace_back(ReadDomainFromProto(working_model->variables(i)));
    if (domains.back().IsEmpty()) {
      is_unsat = true;
      return;
    }
    if (IsFixed(i)) ExploitFixedDomain(i);
  }
  modified_domains.Resize(domains.size());
  var_to_constraints_.resize(domains.size());
  var_to_num_linear1_.resize(domains.size());
  var_to_ub_only_constraints.resize(domains.size());
  var_to_lb_only_constraints.resize(domains.size());
}

void PresolveContext::InsertVarValueEncoding(int literal, int ref,
                                             int64 value) {
  const int var = PositiveRef(ref);
  const int64 var_value = RefIsPositive(ref) ? value : -value;
  const std::pair<std::pair<int, int64>, int> key =
      std::make_pair(std::make_pair(var, var_value), literal);
  const auto& insert = encoding.insert(key);
  if (insert.second) {
    if (DomainOf(var).Size() == 2) {
      // Encode the other literal.
      const int64 var_min = MinOf(var);
      const int64 var_max = MaxOf(var);
      const int64 other_value = value == var_min ? var_max : var_min;
      const std::pair<int, int64> other_key{var, other_value};
      auto other_it = encoding.find(other_key);
      if (other_it != encoding.end()) {
        // Other value in the domain was already encoded.
        const int previous_other_literal = other_it->second;
        if (previous_other_literal != NegatedRef(literal)) {
          StoreBooleanEqualityRelation(literal,
                                       NegatedRef(previous_other_literal));
        }
      } else {
        encoding[other_key] = NegatedRef(literal);
        // Add affine relation.
        // TODO(user): In linear presolve, recover var-value encoding from
        //     linear constraints like the one created below. This would be
        //     useful in case the variable has an affine representative, and the
        //     below constraint is rewritten.
        ConstraintProto* const ct = working_model->add_constraints();
        LinearConstraintProto* const lin = ct->mutable_linear();
        lin->add_vars(var);
        lin->add_coeffs(1);
        lin->add_vars(PositiveRef(literal));
        if (RefIsPositive(literal) == (var_value == var_max)) {
          lin->add_coeffs(var_min - var_max);
          lin->add_domain(var_min);
          lin->add_domain(var_min);
          StoreAffineRelation(*ct, var, PositiveRef(literal), var_max - var_min,
                              var_min);
        } else {
          lin->add_coeffs(var_max - var_min);
          lin->add_domain(var_max);
          lin->add_domain(var_max);
          StoreAffineRelation(*ct, var, PositiveRef(literal), var_min - var_max,
                              var_max);
        }
      }
    } else {
      VLOG(2) << "Insert lit(" << literal << ") <=> var(" << var
              << ") == " << value;
      const std::pair<int, int64> key{var, var_value};
      eq_half_encoding[key].insert(literal);
      AddImplyInDomain(literal, var, Domain(var_value));
      neq_half_encoding[key].insert(NegatedRef(literal));
      AddImplyInDomain(NegatedRef(literal), var,
                       Domain(var_value).Complement());
    }
  } else {
    const int previous_literal = insert.first->second;
    if (literal != previous_literal) {
      StoreBooleanEqualityRelation(literal, previous_literal);
    }
  }
}

bool PresolveContext::InsertHalfVarValueEncoding(int literal, int var,
                                                 int64 value, bool imply_eq) {
  CHECK(RefIsPositive(var));

  // Creates the linking maps on demand.
  const std::pair<int, int64> key{var, value};
  auto& direct_map = imply_eq ? eq_half_encoding[key] : neq_half_encoding[key];
  auto& other_map = imply_eq ? neq_half_encoding[key] : eq_half_encoding[key];

  // Insert the reference literal in the half encoding map.
  const auto& new_info = direct_map.insert(literal);
  if (new_info.second) {
    VLOG(2) << "Collect lit(" << literal << ") implies var(" << var
            << (imply_eq ? ") == " : ") != ") << value;
    UpdateRuleStats("variables: detect half reified value encoding");

    if (other_map.contains(NegatedRef(literal))) {
      const int imply_eq_literal = imply_eq ? literal : NegatedRef(literal);

      const auto insert_encoding_status =
          encoding.insert(std::make_pair(key, imply_eq_literal));
      if (insert_encoding_status.second) {
        VLOG(2) << "Detect and store lit(" << imply_eq_literal << ") <=> var("
                << var << ") == " << value;
        UpdateRuleStats("variables: detect fully reified value encoding");
      } else if (imply_eq_literal != insert_encoding_status.first->second) {
        const int previous_imply_eq_literal =
            insert_encoding_status.first->second;
        VLOG(2) << "Detect duplicate encoding lit(" << imply_eq_literal
                << ") == lit(" << previous_imply_eq_literal << ") <=> var("
                << var << ") == " << value;
        StoreBooleanEqualityRelation(imply_eq_literal,
                                     previous_imply_eq_literal);

        UpdateRuleStats(
            "variables: merge equivalent var value encoding literals");
      }
    }
  }

  return new_info.second;
}

bool PresolveContext::StoreLiteralImpliesVarEqValue(int literal, int var,
                                                    int64 value) {
  return InsertHalfVarValueEncoding(literal, var, value, /*imply_eq=*/true);
}

bool PresolveContext::StoreLiteralImpliesVarNEqValue(int literal, int var,
                                                     int64 value) {
  return InsertHalfVarValueEncoding(literal, var, value, /*imply_eq=*/false);
}

bool PresolveContext::HasVarValueEncoding(int ref, int64 value, int* literal) {
  const int var = PositiveRef(ref);
  const int64 var_value = RefIsPositive(ref) ? value : -value;
  const std::pair<int, int64> key{var, var_value};
  const auto& it = encoding.find(key);
  if (it != encoding.end()) {
    if (literal != nullptr) {
      *literal = GetLiteralRepresentative(it->second);
    }
    return true;
  } else {
    return false;
  }
}

int PresolveContext::GetOrCreateVarValueEncoding(int ref, int64 value) {
  // TODO(user,user): use affine relation here.
  const int var = PositiveRef(ref);
  const int64 var_value = RefIsPositive(ref) ? value : -value;

  // Returns the false literal if the value is not in the domain.
  if (!domains[var].Contains(var_value)) {
    return GetOrCreateConstantVar(0);
  }

  // Returns the associated literal if already present.
  const std::pair<int, int64> key{var, var_value};
  auto it = encoding.find(key);
  if (it != encoding.end()) {
    return GetLiteralRepresentative(it->second);
  }

  // Special case for fixed domains.
  if (domains[var].Size() == 1) {
    const int true_literal = GetOrCreateConstantVar(1);
    encoding[key] = true_literal;
    return true_literal;
  }

  // Special case for domains of size 2.
  const int64 var_min = MinOf(var);
  const int64 var_max = MaxOf(var);
  if (domains[var].Size() == 2) {
    // Checks if the other value is already encoded.
    const int64 other_value = var_value == var_min ? var_max : var_min;
    const std::pair<int, int64> other_key{var, other_value};
    auto other_it = encoding.find(other_key);
    if (other_it != encoding.end()) {
      // Update the encoding map. The domain could have been reduced to size
      // two after the creation of the first literal.
      const int other_literal =
          GetLiteralRepresentative(NegatedRef(other_it->second));
      encoding[key] = other_literal;
      return other_literal;
    }

    if (var_min == 0 && var_max == 1) {
      const int representative = GetLiteralRepresentative(var);
      encoding[{var, 1}] = representative;
      encoding[{var, 0}] = NegatedRef(representative);
      return value == 1 ? representative : NegatedRef(representative);
    } else {
      const int literal = NewBoolVar();
      InsertVarValueEncoding(literal, var, var_max);
      const int representative = GetLiteralRepresentative(literal);
      return var_value == var_max ? representative : NegatedRef(representative);
    }
  }

  const int literal = NewBoolVar();
  InsertVarValueEncoding(literal, var, var_value);
  return GetLiteralRepresentative(literal);
}

void PresolveContext::ReadObjectiveFromProto() {
  const CpObjectiveProto& obj = working_model->objective();

  objective_offset = obj.offset();
  objective_scaling_factor = obj.scaling_factor();
  if (objective_scaling_factor == 0.0) {
    objective_scaling_factor = 1.0;
  }
  if (!obj.domain().empty()) {
    // We might relax this in CanonicalizeObjective() when we will compute
    // the possible objective domain from the domains of the variables.
    objective_domain_is_constraining = true;
    objective_domain = ReadDomainFromProto(obj);
  } else {
    objective_domain_is_constraining = false;
    objective_domain = Domain::AllValues();
  }

  objective_map.clear();
  for (int i = 0; i < obj.vars_size(); ++i) {
    const int ref = obj.vars(i);
    int64 coeff = obj.coeffs(i);
    if (!RefIsPositive(ref)) coeff = -coeff;
    int var = PositiveRef(ref);

    objective_map[var] += coeff;
    if (objective_map[var] == 0) {
      objective_map.erase(var);
      var_to_constraints_[var].erase(-1);
    } else {
      var_to_constraints_[var].insert(-1);
    }
  }
}

bool PresolveContext::CanonicalizeObjective() {
  int64 offset_change = 0;

  // We replace each entry by its affine representative.
  // Note that the non-deterministic loop is fine, but because we iterate
  // one the map while modifying it, it is safer to do a copy rather than to
  // try to handle that in one pass.
  tmp_entries.clear();
  for (const auto& entry : objective_map) {
    tmp_entries.push_back(entry);
  }

  // TODO(user): This is a bit duplicated with the presolve linear code.
  // We also do not propagate back any domain restriction from the objective to
  // the variables if any.
  for (const auto& entry : tmp_entries) {
    const int var = entry.first;
    const auto it = objective_map.find(var);
    if (it == objective_map.end()) continue;
    const int64 coeff = it->second;

    // If a variable only appear in objective, we can fix it!
    // Note that we don't care if it was in affine relation, because if none
    // of the relations are left, then we can still fix it.
    if (!keep_all_feasible_solutions && !objective_domain_is_constraining &&
        ConstraintVariableGraphIsUpToDate() &&
        var_to_constraints_[var].size() == 1 &&
        var_to_constraints_[var].contains(-1)) {
      UpdateRuleStats("objective: variable not used elsewhere");
      if (coeff > 0) {
        if (!IntersectDomainWith(var, Domain(MinOf(var)))) {
          return false;
        }
      } else {
        if (!IntersectDomainWith(var, Domain(MaxOf(var)))) {
          return false;
        }
      }
    }

    if (IsFixed(var)) {
      offset_change += coeff * MinOf(var);
      var_to_constraints_[var].erase(-1);
      objective_map.erase(var);
      continue;
    }

    const AffineRelation::Relation r = GetAffineRelation(var);
    if (r.representative == var) continue;

    objective_map.erase(var);
    var_to_constraints_[var].erase(-1);

    // Do the substitution.
    offset_change += coeff * r.offset;
    const int64 new_coeff = objective_map[r.representative] += coeff * r.coeff;

    // Process new term.
    if (new_coeff == 0) {
      objective_map.erase(r.representative);
      var_to_constraints_[r.representative].erase(-1);
    } else {
      var_to_constraints_[r.representative].insert(-1);
      if (IsFixed(r.representative)) {
        offset_change += new_coeff * MinOf(r.representative);
        var_to_constraints_[r.representative].erase(-1);
        objective_map.erase(r.representative);
      }
    }
  }

  Domain implied_domain(0);
  int64 gcd(0);

  // We need to sort the entries to be deterministic.
  tmp_entries.clear();
  for (const auto& entry : objective_map) {
    tmp_entries.push_back(entry);
  }
  std::sort(tmp_entries.begin(), tmp_entries.end());
  for (const auto& entry : tmp_entries) {
    const int var = entry.first;
    const int64 coeff = entry.second;
    gcd = MathUtil::GCD64(gcd, std::abs(coeff));
    implied_domain =
        implied_domain.AdditionWith(DomainOf(var).MultiplicationBy(coeff))
            .RelaxIfTooComplex();
  }

  // This is the new domain.
  // Note that the domain never include the offset.
  objective_domain = objective_domain.AdditionWith(Domain(-offset_change))
                         .IntersectionWith(implied_domain);
  objective_domain =
      objective_domain.SimplifyUsingImpliedDomain(implied_domain);

  // Updat the offset.
  objective_offset += offset_change;

  // Maybe divide by GCD.
  if (gcd > 1) {
    for (auto& entry : objective_map) {
      entry.second /= gcd;
    }
    objective_domain = objective_domain.InverseMultiplicationBy(gcd);
    objective_offset /= static_cast<double>(gcd);
    objective_scaling_factor *= static_cast<double>(gcd);
  }

  if (objective_domain.IsEmpty()) return false;

  // Detect if the objective domain do not limit the "optimal" objective value.
  // If this is true, then we can apply any reduction that reduce the objective
  // value without any issues.
  objective_domain_is_constraining =
      !implied_domain
           .IntersectionWith(Domain(kint64min, objective_domain.Max()))
           .IsIncludedIn(objective_domain);
  return true;
}

void PresolveContext::SubstituteVariableInObjective(
    int var_in_equality, int64 coeff_in_equality,
    const ConstraintProto& equality, std::vector<int>* new_vars_in_objective) {
  CHECK(equality.enforcement_literal().empty());
  CHECK(RefIsPositive(var_in_equality));

  if (new_vars_in_objective != nullptr) new_vars_in_objective->clear();

  // We can only "easily" substitute if the objective coefficient is a multiple
  // of the one in the constraint.
  const int64 coeff_in_objective =
      gtl::FindOrDie(objective_map, var_in_equality);
  CHECK_NE(coeff_in_equality, 0);
  CHECK_EQ(coeff_in_objective % coeff_in_equality, 0);
  const int64 multiplier = coeff_in_objective / coeff_in_equality;

  for (int i = 0; i < equality.linear().vars().size(); ++i) {
    int var = equality.linear().vars(i);
    int64 coeff = equality.linear().coeffs(i);
    if (!RefIsPositive(var)) {
      var = NegatedRef(var);
      coeff = -coeff;
    }
    if (var == var_in_equality) continue;

    int64& map_ref = objective_map[var];
    if (map_ref == 0 && new_vars_in_objective != nullptr) {
      new_vars_in_objective->push_back(var);
    }
    map_ref -= coeff * multiplier;

    if (map_ref == 0) {
      objective_map.erase(var);
      var_to_constraints_[var].erase(-1);
    } else {
      var_to_constraints_[var].insert(-1);
    }
  }

  objective_map.erase(var_in_equality);
  var_to_constraints_[var_in_equality].erase(-1);

  // Deal with the offset.
  Domain offset = ReadDomainFromProto(equality.linear());
  DCHECK_EQ(offset.Min(), offset.Max());
  bool exact = true;
  offset = offset.MultiplicationBy(multiplier, &exact);
  CHECK(exact);

  // Tricky: The objective domain is without the offset, so we need to shift it.
  objective_offset += static_cast<double>(offset.Min());
  objective_domain = objective_domain.AdditionWith(Domain(-offset.Min()));

  // Because we can assume that the constraint we used was constraining
  // (otherwise it would have been removed), the objective domain should be now
  // constraining.
  objective_domain_is_constraining = true;
}

void PresolveContext::WriteObjectiveToProto() {
  if (objective_domain.IsEmpty()) {
    return (void)NotifyThatModelIsUnsat();
  }

  // We need to sort the entries to be deterministic.
  std::vector<std::pair<int, int64>> entries;
  for (const auto& entry : objective_map) {
    entries.push_back(entry);
  }
  std::sort(entries.begin(), entries.end());

  CpObjectiveProto* mutable_obj = working_model->mutable_objective();
  mutable_obj->set_offset(objective_offset);
  mutable_obj->set_scaling_factor(objective_scaling_factor);
  FillDomainInProto(objective_domain, mutable_obj);
  mutable_obj->clear_vars();
  mutable_obj->clear_coeffs();
  for (const auto& entry : entries) {
    mutable_obj->add_vars(entry.first);
    mutable_obj->add_coeffs(entry.second);
  }
}

}  // namespace sat
}  // namespace operations_research
